#include "wizard.h"

#include <commctrl.h>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace piinput::windows {

namespace {

constexpr wchar_t kWizardClass[] = L"PiInputInstallWizardWindow";

constexpr int kWindowWidth = 620;
constexpr int kWindowHeight = 460;
// 左侧那条色带是向导的标志性布局，也是「这是安装程序」与「这是系统对话框」之间
// 最便宜的区别。
constexpr int kBannerWidth = 170;
constexpr int kContentLeft = kBannerWidth + 26;
constexpr int kContentRight = kWindowWidth - 24;
constexpr int kContentWidth = kContentRight - kContentLeft;
constexpr int kFooterHeight = 52;

constexpr int kBack = 100;
constexpr int kNext = 101;
constexpr int kCancel = 102;
constexpr int kProgressBar = 103;
constexpr int kCheckSettings = 104;
constexpr int kCheckGuide = 105;
constexpr int kCheckActivate = 106;
constexpr int kTimer = 1;

enum class Page : int {
    welcome,
    notes,
    location,
    progress,
    finished,
    failed,
};

struct PageText final {
    const wchar_t* heading;
    const wchar_t* body;
};

struct WizardState final {
    const WizardContext* context{};
    const WizardInstallAction* action{};
    WizardOutcome outcome;
    Page page{Page::welcome};
    HFONT heading_font{};
    HFONT body_font{};
    HFONT banner_font{};
    HWND heading{};
    HWND body{};
    HWND detail{};
    HWND progress{};
    HWND progress_text{};
    HWND check_settings{};
    HWND check_guide{};
    HWND check_activate{};
    HWND back{};
    HWND next{};
    HWND cancel{};
    // 工作线程与界面线程之间只有这几个原子量来回；文字加锁，结果在 join 之后读。
    std::thread worker;
    std::atomic<int> percent{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
    std::mutex text_mutex;
    std::wstring step_text{L"正在准备……"};
    std::wstring error;
    bool done{};
};

HWND make_control(
    const wchar_t* const class_name,
    const wchar_t* const text,
    const DWORD style,
    const int x,
    const int y,
    const int width,
    const int height,
    const HWND parent,
    const int id) {
    return CreateWindowExW(0, class_name, text, WS_CHILD | style,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

[[nodiscard]] PageText page_text(const Page page, const bool upgrade) {
    switch (page) {
    case Page::welcome:
        return {
            upgrade ? L"升级 PiInput 输入法" : L"安装 PiInput 输入法",
            upgrade
                ? L"检测到这台机器上已经装过 PiInput，本次是覆盖升级。\n\n"
                  L"你的用户词库、设置和学习记录都会保留，不需要先卸载，也不需要关闭"
                  L"正在使用的程序。\n\n"
                  L"升级完成后已经打开的程序仍在用旧的组件——输入法是 DLL，程序不重启"
                  L"就还是旧版本。要用上新版本，把那些程序重新打开一次即可。"
                : L"PiInput 是一个 Windows 拼音输入法：全拼、双拼、中英混输、"
                  L"自定义常用语，候选框跟随光标。\n\n"
                  L"接下来三步会说明装到哪里、为什么需要一次管理员权限，以及"
                  L"Windows 的安全机制可能拦下什么。全部读完大约半分钟。\n\n"
                  L"安装过程不会关闭任何正在运行的程序，也不需要重启电脑。",
        };
    case Page::notes:
        return {
            L"关于 Windows 的安全提示",
            L"PiInput 目前没有代码签名证书，因此在开启了防护的机器上可能被拦下。"
            L"这不是程序有问题，是 Windows 无法验证发布者。\n\n"
            L"· Microsoft Defender SmartScreen：弹出「Windows 已保护你的电脑」时，"
            L"点「更多信息」→「仍要运行」即可放行。\n\n"
            L"· 智能应用控制（Smart App Control）：显示「已阻止可能不安全的应用」，"
            L"强制模式下不提供放行入口，只能到「Windows 安全中心 → 应用和浏览器控制」"
            L"里关闭它。注意这个操作不可逆。\n\n"
            L"· 另外 Defender 有时会把引擎进程误判为木马并隔离，表现是任务栏图标还在"
            L"但一个字也打不出来。遇到这种情况，把安装目录加入排除项即可，"
            L"随包的操作指引里有具体步骤。",
        };
    case Page::location:
        return {
            L"安装位置",
            L"程序文件和用户数据分开存放：\n",
        };
    case Page::progress:
        return {upgrade ? L"正在升级" : L"正在安装", L""};
    case Page::finished:
        return {
            L"安装完成",
            L"输入法已经就位，不需要重启电脑。\n\n"
            L"按 Win+Space 切换到 PiInput 即可开始使用。已经打开的程序要重新打开"
            L"一次才会加载新组件。\n\n"
            L"下面三项可以现在就做：",
        };
    case Page::failed:
    default:
        return {L"安装未完成", L""};
    }
}

void layout_page(WizardState& state) {
    const bool upgrade = state.context != nullptr && state.context->upgrade;
    const auto text = page_text(state.page, upgrade);
    SetWindowTextW(state.heading, text.heading);

    std::wstring body = text.body;
    std::wstring detail;
    if (state.page == Page::location && state.context != nullptr) {
        // 多行 EDIT 只认 \r\n。只给 \n 会把每一段首尾接在一起，路径和正文连成一片。
        detail = L"程序文件\r\n" + state.context->program_root +
            L"\r\n\r\n用户设置和词库\r\n" + state.context->user_data +
            L"\r\n\r\n安装过程中 Windows 会请求一次管理员权限，用来把输入法入口注册到"
            L"系统。那一步只写系统范围的输入法注册信息；你的设置和词库始终留在"
            L"当前账户下，卸载时默认保留。";
    } else if (state.page == Page::failed) {
        const std::lock_guard lock(state.text_mutex);
        detail = state.error.empty() ? L"未知错误。" : state.error;
        // 失败文案来自异常消息，里面用的是 \n。多行 EDIT 只认 \r\n，不补的话
        // 整段会挤成一行——而这正是最需要读清楚的一段。
        std::wstring normalized;
        normalized.reserve(detail.size() + 16U);
        for (const wchar_t character : detail) {
            if (character == L'\n') normalized.push_back(L'\r');
            normalized.push_back(character);
        }
        detail = std::move(normalized);
    }
    SetWindowTextW(state.body, body.c_str());
    SetWindowTextW(state.detail, detail.c_str());

    const bool progress_page = state.page == Page::progress;
    const bool finished_page = state.page == Page::finished;
    // 正文框按有没有 detail 伸缩。固定高度装不下安全提示那一页——它有十几行，
    // 被裁在半句话上，而被裁掉的恰好是「智能应用控制关掉之后不可逆」这种必须读到
    // 的内容。
    constexpr int kBodyTop = 74;
    constexpr int kBodyTall = 318;
    constexpr int kBodyShort = 86;
    constexpr int kDetailTop = kBodyTop + kBodyShort + 10;
    SetWindowPos(state.body, nullptr, kContentLeft, kBodyTop, kContentWidth,
        detail.empty() ? kBodyTall : kBodyShort, SWP_NOZORDER);
    SetWindowPos(state.detail, nullptr, kContentLeft, kDetailTop, kContentWidth,
        kBodyTop + kBodyTall - kDetailTop, SWP_NOZORDER);
    ShowWindow(state.detail, detail.empty() ? SW_HIDE : SW_SHOW);
    ShowWindow(state.progress, progress_page ? SW_SHOW : SW_HIDE);
    ShowWindow(state.progress_text, progress_page ? SW_SHOW : SW_HIDE);
    for (const HWND check : {state.check_settings, state.check_guide, state.check_activate}) {
        ShowWindow(check, finished_page ? SW_SHOW : SW_HIDE);
    }

    // 安装一旦开始就没有「上一步」了：那之前什么都没写过，那之后回退没有意义。
    EnableWindow(state.back, state.page == Page::notes || state.page == Page::location);
    ShowWindow(state.back,
        state.page == Page::progress || finished_page || state.page == Page::failed
            ? SW_HIDE : SW_SHOW);
    // 「下一步」在每一页都在，只有安装进行中不可按。它必须显式 Show：控件建出来
    // 时没有 WS_VISIBLE，光 Enable 是看不见的。
    ShowWindow(state.next, SW_SHOW);
    EnableWindow(state.next, !progress_page);
    const wchar_t* next_label = L"下一步";
    if (state.page == Page::location) next_label = upgrade ? L"开始升级" : L"开始安装";
    if (finished_page) next_label = L"完成";
    if (state.page == Page::failed) next_label = L"关闭";
    SetWindowTextW(state.next, next_label);
    ShowWindow(state.cancel,
        progress_page || finished_page || state.page == Page::failed ? SW_HIDE : SW_SHOW);
    InvalidateRect(GetParent(state.heading), nullptr, TRUE);
}

void start_install(const HWND window, WizardState& state) {
    state.outcome.started = true;
    state.page = Page::progress;
    layout_page(state);
    SendMessageW(state.progress, PBM_SETRANGE32, 0, 100);
    SendMessageW(state.progress, PBM_SETPOS, 0, 0);
    SetTimer(window, kTimer, 120U, nullptr);
    state.worker = std::thread([&state] {
        std::wstring error;
        const bool ok = state.action != nullptr && (*state.action)(
            [&state](const int percent, std::wstring text) {
                state.percent.store(percent);
                const std::lock_guard lock(state.text_mutex);
                state.step_text = std::move(text);
            },
            error);
        {
            const std::lock_guard lock(state.text_mutex);
            state.error = std::move(error);
        }
        state.failed.store(!ok);
        state.finished.store(true);
    });
}

void finish_install(const HWND window, WizardState& state) {
    KillTimer(window, kTimer);
    if (state.worker.joinable()) state.worker.join();
    state.outcome.succeeded = !state.failed.load();
    state.page = state.outcome.succeeded ? Page::finished : Page::failed;
    if (state.outcome.succeeded) {
        SendMessageW(state.progress, PBM_SETPOS, 100, 0);
        // 默认全部勾上：这三件事都是用户装完之后本来就要做的，而「立即激活」那一项
        // 尤其值得默认——profile 刚注册完有时并不在前台生效，而用户只会看到任务栏里
        // 找不到输入法。
        for (const HWND check : {state.check_settings, state.check_guide, state.check_activate}) {
            SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
        }
    }
    layout_page(state);
}

LRESULT CALLBACK wizard_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    auto* state = reinterpret_cast<WizardState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<WizardState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_CREATE && state != nullptr) {
        state->heading_font = CreateFontW(-23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Microsoft YaHei UI");
        state->body_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Microsoft YaHei UI");
        state->banner_font = CreateFontW(-34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Microsoft YaHei UI");

        state->heading = make_control(L"STATIC", L"", SS_LEFT | WS_VISIBLE,
            kContentLeft, 28, kContentWidth, 34, window, 0);
        state->body = make_control(L"STATIC", L"", SS_LEFT | WS_VISIBLE,
            kContentLeft, 74, kContentWidth, 186, window, 0);
        state->detail = make_control(L"EDIT", L"",
            ES_MULTILINE | ES_READONLY | WS_VISIBLE | ES_AUTOVSCROLL,
            kContentLeft, 170, kContentWidth, 150, window, 0);
        state->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD,
            kContentLeft, 180, kContentWidth, 20, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProgressBar)),
            GetModuleHandleW(nullptr), nullptr);
        state->progress_text = make_control(L"STATIC", L"", SS_LEFT,
            kContentLeft, 210, kContentWidth, 24, window, 0);
        state->check_settings = make_control(L"BUTTON", L"打开设置程序",
            BS_AUTOCHECKBOX | WS_TABSTOP, kContentLeft, 244, kContentWidth, 24,
            window, kCheckSettings);
        state->check_guide = make_control(L"BUTTON", L"查看操作指引（推荐第一次使用时看一眼）",
            BS_AUTOCHECKBOX | WS_TABSTOP, kContentLeft, 272, kContentWidth, 24,
            window, kCheckGuide);
        state->check_activate = make_control(L"BUTTON", L"立即激活输入法",
            BS_AUTOCHECKBOX | WS_TABSTOP, kContentLeft, 300, kContentWidth, 24,
            window, kCheckActivate);

        const int footer_y = kWindowHeight - kFooterHeight + 10;
        state->back = make_control(L"BUTTON", L"上一步", BS_PUSHBUTTON | WS_TABSTOP,
            kContentRight - 282, footer_y, 88, 30, window, kBack);
        state->next = make_control(L"BUTTON", L"下一步",
            BS_DEFPUSHBUTTON | WS_TABSTOP, kContentRight - 188, footer_y, 88, 30,
            window, kNext);
        state->cancel = make_control(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
            kContentRight - 94, footer_y, 88, 30, window, kCancel);

        SendMessageW(state->heading, WM_SETFONT,
            reinterpret_cast<WPARAM>(state->heading_font), TRUE);
        for (const HWND child : {state->body, state->detail, state->progress_text,
                 state->check_settings, state->check_guide, state->check_activate,
                 state->back, state->next, state->cancel}) {
            SendMessageW(child, WM_SETFONT,
                reinterpret_cast<WPARAM>(state->body_font), TRUE);
        }
        layout_page(*state);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC && state != nullptr) {
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (message == WM_ERASEBKGND && state != nullptr) {
        const auto device = reinterpret_cast<HDC>(wparam);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(device, &client, GetSysColorBrush(COLOR_WINDOW));

        RECT banner{0, 0, kBannerWidth, client.bottom - kFooterHeight};
        const HBRUSH banner_brush = CreateSolidBrush(RGB(32, 78, 120));
        FillRect(device, &banner, banner_brush);
        DeleteObject(banner_brush);

        const HGDIOBJ previous = SelectObject(device, state->banner_font);
        SetBkMode(device, TRANSPARENT);
        SetTextColor(device, RGB(255, 255, 255));
        RECT mark{0, 150, kBannerWidth, 200};
        DrawTextW(device, L"PiInput", -1, &mark,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(device, state->body_font);
        SetTextColor(device, RGB(188, 212, 232));
        RECT version{0, 202, kBannerWidth, 230};
        const std::wstring label = state->context != nullptr
            ? L"v" + state->context->version : std::wstring{};
        DrawTextW(device, label.c_str(), -1, &version,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(device, previous);

        RECT footer{0, client.bottom - kFooterHeight, client.right, client.bottom};
        FillRect(device, &footer, GetSysColorBrush(COLOR_BTNFACE));
        RECT divider{0, client.bottom - kFooterHeight, client.right,
            client.bottom - kFooterHeight + 1};
        FillRect(device, &divider, GetSysColorBrush(COLOR_3DSHADOW));
        return 1;
    }
    if (message == WM_TIMER && state != nullptr && wparam == kTimer) {
        SendMessageW(state->progress, PBM_SETPOS, state->percent.load(), 0);
        {
            const std::lock_guard lock(state->text_mutex);
            SetWindowTextW(state->progress_text, state->step_text.c_str());
        }
        if (state->finished.load()) finish_install(window, *state);
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        const int id = LOWORD(wparam);
        if (id == kCancel) {
            DestroyWindow(window);
            return 0;
        }
        if (id == kBack) {
            if (state->page == Page::notes) state->page = Page::welcome;
            else if (state->page == Page::location) state->page = Page::notes;
            layout_page(*state);
            return 0;
        }
        if (id == kNext) {
            switch (state->page) {
            case Page::welcome:
                state->page = Page::notes;
                layout_page(*state);
                return 0;
            case Page::notes:
                state->page = Page::location;
                layout_page(*state);
                return 0;
            case Page::location:
                start_install(window, *state);
                return 0;
            case Page::finished:
                state->outcome.open_settings =
                    SendMessageW(state->check_settings, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->outcome.open_guide =
                    SendMessageW(state->check_guide, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->outcome.activate_profile =
                    SendMessageW(state->check_activate, BM_GETCHECK, 0, 0) == BST_CHECKED;
                DestroyWindow(window);
                return 0;
            case Page::failed:
                DestroyWindow(window);
                return 0;
            case Page::progress:
            default:
                return 0;
            }
        }
    }
    if (message == WM_CLOSE && state != nullptr) {
        // 安装进行中不让关：工作线程正在写文件和注册表，半途撤掉窗口只会留下一个
        // 装了一半的状态。
        if (state->page == Page::progress) return 0;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && state != nullptr) {
        KillTimer(window, kTimer);
        if (state->worker.joinable()) state->worker.join();
        for (const HFONT font : {state->heading_font, state->body_font, state->banner_font}) {
            if (font != nullptr) DeleteObject(font);
        }
        state->done = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

WizardOutcome run_install_wizard(
    const HINSTANCE instance,
    const WizardContext& context,
    const WizardInstallAction& run_install) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    (void)InitCommonControlsEx(&controls);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = wizard_proc;
    window_class.lpszClassName = kWizardClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, L"PIINPUT_ICON");
    // 背景自绘，交给系统填会在左侧色带上闪一下白。
    window_class.hbrBackground = nullptr;
    if (RegisterClassExW(&window_class) == 0U &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return {};
    }

    WizardState state;
    state.context = &context;
    state.action = &run_install;

    RECT frame{0, 0, kWindowWidth, kWindowHeight};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&frame, style, FALSE, 0);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    const HWND window = CreateWindowExW(0, kWizardClass,
        context.upgrade ? L"升级 PiInput" : L"安装 PiInput",
        style, x, y, width, height, nullptr, nullptr, instance, &state);
    if (window == nullptr) return {};
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0U, 0U) > 0) {
        if (IsDialogMessageW(window, &message) != FALSE) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return std::move(state.outcome);
}

}  // namespace piinput::windows
