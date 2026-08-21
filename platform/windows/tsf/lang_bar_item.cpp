#include "lang_bar_item.h"

#include "piinput_tsf_guids.h"

#include <olectl.h>

#include <cstdio>
#include <cwchar>
#include <share.h>
#include <string>
#include <utility>

namespace piinput::windows {

// The indicator sits on the taskbar, so it follows the taskbar's theme rather
// than the app's. Windows records that choice here.
[[nodiscard]] bool system_uses_dark_theme() noexcept {
    DWORD value = 1U;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    // Falling back to light meant painting the mark black, and Windows 11
    // ships a dark taskbar by default -- a black mark on it is invisible,
    // which reads as the icon having disappeared rather than as a colour
    // being wrong. Dark is the safer guess when the value cannot be read.
    if (status != ERROR_SUCCESS) return true;
    return value == 0U;
}

namespace {

// GUID_LBI_INPUTMODE is Windows' own identity for the input-mode indicator.
// Only an item registered under it gets drawn in the taskbar input indicator;
// a privately minted GUID registers successfully, is queried by the shell, and
// is then never displayed -- which is exactly what happened with the first
// attempt here. Every input method that shows 中/英 uses this one.

constexpr UINT kMenuLanguage = 1U;
constexpr UINT kMenuSchema = 2U;
constexpr UINT kMenuSymbols = 3U;
constexpr UINT kMenuSettings = 4U;
constexpr UINT kMenuAbout = 5U;
constexpr UINT kMenuHelp = 6U;


// Draws 中 or 英 at the small-icon size. Windows renders language bar items
// from their icon, so the mark has to be painted rather than handed over as
// text -- which is also why it must be regenerated when the theme flips.
[[nodiscard]] HICON make_mode_icon(const bool chinese, const bool dark) noexcept {
    const int metric = GetSystemMetrics(SM_CXSMICON);
    const int side = metric > 0 ? metric : 16;
    const wchar_t* const glyph = chinese ? L"中" : L"英";

    const HDC screen = GetDC(nullptr);
    if (screen == nullptr) return nullptr;
    const HDC colour_dc = CreateCompatibleDC(screen);
    const HDC mask_dc = CreateCompatibleDC(screen);
    const HBITMAP colour = CreateCompatibleBitmap(screen, side, side);
    const HBITMAP mask = CreateBitmap(side, side, 1U, 1U, nullptr);
    ReleaseDC(nullptr, screen);
    if (colour_dc == nullptr || mask_dc == nullptr || colour == nullptr || mask == nullptr) {
        if (colour_dc != nullptr) DeleteDC(colour_dc);
        if (mask_dc != nullptr) DeleteDC(mask_dc);
        if (colour != nullptr) DeleteObject(colour);
        if (mask != nullptr) DeleteObject(mask);
        return nullptr;
    }

    const HFONT font = CreateFontW(
        -(side * 7 / 8), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    const RECT bounds{0, 0, side, side};
    constexpr UINT format = DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX;

    // The mask decides what shows at all: white leaves the taskbar visible,
    // black lets the colour bitmap through. Painting the glyph black on a white
    // field is what makes the background transparent instead of a solid tile.
    const auto previous_mask = SelectObject(mask_dc, mask);
    const auto previous_mask_font = SelectObject(mask_dc, font);
    RECT mask_bounds = bounds;
    FillRect(mask_dc, &mask_bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(mask_dc, TRANSPARENT);
    SetTextColor(mask_dc, RGB(0, 0, 0));
    DrawTextW(mask_dc, glyph, 1, &mask_bounds, format);
    SelectObject(mask_dc, previous_mask_font);
    SelectObject(mask_dc, previous_mask);

    // Colour supplies the glyph's ink. Black where the mask hides it, so any
    // fringe from antialiasing stays dark rather than showing as a halo.
    const auto previous_colour = SelectObject(colour_dc, colour);
    const auto previous_colour_font = SelectObject(colour_dc, font);
    RECT colour_bounds = bounds;
    FillRect(colour_dc, &colour_bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(colour_dc, TRANSPARENT);
    SetTextColor(colour_dc, dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    DrawTextW(colour_dc, glyph, 1, &colour_bounds, format);
    SelectObject(colour_dc, previous_colour_font);
    SelectObject(colour_dc, previous_colour);

    if (font != nullptr) DeleteObject(font);
    DeleteDC(colour_dc);
    DeleteDC(mask_dc);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = colour;
    const HICON created = CreateIconIndirect(&info);
    DeleteObject(colour);
    DeleteObject(mask);
    return created;
}

[[nodiscard]] BSTR copy_bstr(const std::wstring& value) {
    return SysAllocStringLen(value.c_str(), static_cast<UINT>(value.size()));
}

// Always-on, tiny, and only written while a text service starts up or while
// the shell interrogates a button. It answers the one question that matters
// when nothing appears: did registration fail, or did Windows never ask?
// Opt-in language bar tracing, off unless a marker file named
// piinput-langbar-trace.on exists in the temp directory -- the same gate the
// caret and key traces use, and for the same reason: the Shim runs inside
// other applications and inherits their environment, not the tester ones.
//
// This was left on unconditionally, opening and closing the file for every
// line. GetInfo alone is called by the shell on each redraw, so every
// application hosting the Shim was doing file I/O on its UI thread, into a
// log nothing ever truncated.
std::FILE* bar_trace() {
    static std::FILE* file = [] () -> std::FILE* {
        char temp[MAX_PATH]{};
        if (GetTempPathA(MAX_PATH, temp) == 0U) return nullptr;
        const std::string marker = std::string(temp) + "piinput-langbar-trace.on";
        if (GetFileAttributesA(marker.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return nullptr;
        }
        const std::string path = std::string(temp) + "piinput-langbar.log";
        return _fsopen(path.c_str(), "a", _SH_DENYWR);
    }();
    return file;
}

void trace_bar(const char* const stage, const long detail) noexcept {
    std::FILE* const file = bar_trace();
    if (file == nullptr) return;
    (void)std::fprintf(file, "%lu pid=%lu %s=%ld\n",
        GetTickCount(), GetCurrentProcessId(), stage, detail);
    (void)std::fflush(file);
}

}  // namespace

LangBarButton::LangBarButton(
    const GUID& item_guid,
    std::wstring description,
    const bool show_menu,
    Handler handler,
    const HICON fixed_icon,
    const ULONG sort_order)
    : guid_(item_guid),
      description_(std::move(description)),
      show_menu_(show_menu),
      fixed_icon_(fixed_icon),
      sort_order_(sort_order),
      handler_(std::move(handler)) {}

STDMETHODIMP LangBarButton::QueryInterface(REFIID iid, void** const object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfLangBarItem) ||
        IsEqualIID(iid, IID_ITfLangBarItemButton)) {
        *object = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(iid, IID_ITfSource)) {
        *object = static_cast<ITfSource*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) LangBarButton::Release() {
    const LONG remaining = InterlockedDecrement(&references_);
    if (remaining == 0) delete this;
    return static_cast<ULONG>(remaining);
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* const info) {
    trace_bar(fixed_icon_ != nullptr ? "GetInfo.product" : "GetInfo.mode", 1);
    if (info == nullptr) return E_POINTER;
    *info = {};
    // Windows shows an item only while the text service that owns it is
    // active, and it identifies that owner by this CLSID. GUID_NULL leaves
    // the item unowned, and the indicator never draws it.
    info->clsidService = CLSID_PiInputTextService;
    info->guidItem = guid_;
    // Button and menu together: a left click switches language, a right click
    // opens the menu. This is the same combination the other input methods use.
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU |
        TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = sort_order_;
    const auto length = (std::min<std::size_t>)(
        description_.size(), std::size(info->szDescription) - 1U);
    std::copy_n(description_.begin(), length, info->szDescription);
    info->szDescription[length] = L'\0';
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* const status) {
    trace_bar(fixed_icon_ != nullptr ? "GetStatus.product" : "GetStatus.mode", 1);
    if (status == nullptr) return E_POINTER;
    *status = 0U;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) {
    return E_NOTIMPL;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* const tooltip) {
    if (tooltip == nullptr) return E_POINTER;
    const std::wstring text = language_label_.empty()
        ? description_
        : L"PiInput  " + language_label_ + L"  " + schema_label_;
    *tooltip = copy_bstr(text);
    return *tooltip == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP LangBarButton::OnClick(
    const TfLBIClick click,
    const POINT point,
    const RECT*) {
    trace_bar("OnClick", static_cast<long>(click));
    if (!handler_) return S_OK;
    if (click == TF_LBI_CLK_LEFT) {
        // Same spot, same gesture as every other input method.
        handler_(LangBarCommand::toggle_language);
        return S_OK;
    }
    // A right click arrives here rather than as InitMenu: with both
    // BTN_BUTTON and BTN_MENU set, the shell routes every click to OnClick and
    // never asks for the menu. So put it up directly.
    show_context_menu(point);
    return S_OK;
}

void LangBarButton::show_context_menu(const POINT point) noexcept {
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, kMenuLanguage, language_label_.c_str());
    AppendMenuW(menu, MF_STRING, kMenuSchema, schema_label_.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSymbols, L"符号");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"设置");
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"关于 PiInput");
    AppendMenuW(menu, MF_STRING, kMenuHelp, L"帮助与反馈");

    // A popup owned by a window that is not in the foreground ignores clicks
    // outside itself and never closes. The taskbar owns the click here, so
    // borrow the foreground window and hand focus straight back afterwards.
    const HWND owner = GetForegroundWindow();
    if (owner != nullptr) SetForegroundWindow(owner);
    const UINT choice = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        point.x, point.y, owner, nullptr));
    DestroyMenu(menu);
    if (owner != nullptr) PostMessageW(owner, WM_NULL, 0U, 0U);
    trace_bar("MenuChoice", static_cast<long>(choice));
    if (choice != 0U) (void)OnMenuSelect(choice);
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* const menu) {
    trace_bar("InitMenu", 1);
    if (menu == nullptr) return E_INVALIDARG;
    if (!show_menu_) return S_OK;
    // Lengths must match the strings exactly; hand-counted values truncated the
    // labels and left entries looking broken.
    const auto add = [menu](const UINT id, const wchar_t* const label) {
        (void)menu->AddMenuItem(id, 0U, nullptr, nullptr, label,
            static_cast<ULONG>(std::wcslen(label)), nullptr);
    };
    const auto separator = [menu] {
        (void)menu->AddMenuItem(0U, TF_LBMENUF_SEPARATOR, nullptr, nullptr,
            nullptr, 0U, nullptr);
    };
    // Entries state what is in effect now rather than what clicking will do, so
    // the menu doubles as a status readout.
    add(kMenuLanguage, language_label_.c_str());
    add(kMenuSchema, schema_label_.c_str());
    separator();
    add(kMenuSymbols, L"符号");
    add(kMenuSettings, L"设置");
    separator();
    add(kMenuAbout, L"关于 PiInput");
    add(kMenuHelp, L"帮助与反馈");
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(const UINT id) {
    trace_bar("OnMenuSelect", static_cast<long>(id));
    if (!handler_) return S_OK;
    switch (id) {
    case kMenuLanguage: handler_(LangBarCommand::toggle_language); break;
    case kMenuSchema: handler_(LangBarCommand::toggle_schema); break;
    case kMenuSymbols: handler_(LangBarCommand::symbols); break;
    case kMenuSettings: handler_(LangBarCommand::settings); break;
    case kMenuAbout: handler_(LangBarCommand::about); break;
    case kMenuHelp: handler_(LangBarCommand::help); break;
    default: break;
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* const icon) {
    if (icon == nullptr) return E_POINTER;
    if (fixed_icon_ != nullptr) {
        // Windows takes ownership of what it receives here, so it gets a copy
        // rather than the handle the text service keeps.
        *icon = CopyIcon(fixed_icon_);
        return *icon == nullptr ? E_FAIL : S_OK;
    }
    const bool dark = system_uses_dark_theme();
    if (icon_ == nullptr || icon_dark_ != dark) {
        if (icon_ != nullptr) DestroyIcon(icon_);
        icon_ = make_mode_icon(chinese_, dark);
        icon_dark_ = dark;
    }
    // Windows takes ownership of what it receives here.
    *icon = icon_ == nullptr ? nullptr : CopyIcon(icon_);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetText(BSTR* const text) {
    if (text == nullptr) return E_POINTER;
    *text = copy_bstr(chinese_ ? L"中" : L"英");
    return *text == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP LangBarButton::AdviseSink(
    REFIID iid,
    IUnknown* const sink,
    DWORD* const cookie) {
    if (cookie == nullptr || sink == nullptr) return E_POINTER;
    if (!IsEqualIID(iid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
    if (sink_ != nullptr) return CONNECT_E_ADVISELIMIT;
    if (FAILED(sink->QueryInterface(IID_PPV_ARGS(&sink_)))) return E_NOINTERFACE;
    sink_cookie_ = 0U;
    *cookie = sink_cookie_;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(const DWORD cookie) {
    if (cookie != sink_cookie_ || sink_ == nullptr) return CONNECT_E_NOCONNECTION;
    sink_->Release();
    sink_ = nullptr;
    sink_cookie_ = TF_INVALID_COOKIE;
    return S_OK;
}

void LangBarButton::set_chinese(const bool chinese) noexcept {
    if (chinese_ == chinese) return;
    chinese_ = chinese;
    if (icon_ != nullptr) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
    if (sink_ != nullptr) {
        (void)sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

void LangBarButton::set_menu_state(std::wstring language, std::wstring schema) {
    language_label_ = std::move(language);
    schema_label_ = std::move(schema);
    if (sink_ != nullptr) (void)sink_->OnUpdate(TF_LBI_TOOLTIP);
}

void trace_lang_bar_icon(const bool loaded) noexcept {
    trace_bar("IconLoaded", loaded ? 1 : 0);
}

void trace_lang_bar_stage(const char* const stage, const long detail) noexcept {
    trace_bar(stage, detail);
}

bool LangBar::create(
    ITfThreadMgr* const thread_manager,
    const TfClientId client_id,
    const HICON icon,
    LangBarButton::Handler handler) {
    if (thread_manager == nullptr) return false;
    const HRESULT query = thread_manager->QueryInterface(IID_PPV_ARGS(&manager_));
    trace_bar("QueryLangBarItemMgr", static_cast<long>(query));
    if (FAILED(query) || manager_ == nullptr) return false;
    (void)client_id;

    // Two items, the way Microsoft Pinyin shows 中 next to 拼.
    //
    // An earlier version registered only the 中/英 one, on the assumption that
    // Windows would draw the product logo beside it from the language profile
    // IconFile. It does not: the Windows 11 taskbar draws the language
    // abbreviation there instead -- the button reading 简体 -- and the profile
    // icon appears only in the Win+Space switcher and in Settings. The logo
    // reaches the taskbar only as an item of our own.
    language_ = new (std::nothrow) LangBarButton(
        GUID_LBI_INPUTMODE, L"PiInput 输入模式", true, handler);
    if (language_ == nullptr) {
        destroy();
        return false;
    }
    const HRESULT added = manager_->AddItem(language_);
    trace_bar("AddItem.inputmode", static_cast<long>(added));
    if (FAILED(added)) {
        destroy();
        return false;
    }

    // Sorted after the mode mark so the pair reads 中 then the logo. A missing
    // icon is not worth failing over: the mode mark is the one that carries
    // state, and dropping it because the logo could not load would be trading
    // the useful half for the decorative one.
    if (icon != nullptr) {
        product_ = new (std::nothrow) LangBarButton(
            GUID_PiInputProductButton, L"PiInput 中文输入法", true,
            std::move(handler), icon, 1U);
        if (product_ != nullptr) {
            const HRESULT product_added = manager_->AddItem(product_);
            trace_bar("AddItem.product", static_cast<long>(product_added));
            if (FAILED(product_added)) {
                product_->Release();
                product_ = nullptr;
            }
        }
    }
    return true;
}

void LangBar::destroy() noexcept {
    if (manager_ != nullptr) {
        // Both come out before the manager goes. GUID_LBI_INPUTMODE is one
        // shared slot for the whole thread: leaving an item behind kept it
        // occupied after this service was deactivated, and the next input
        // method could not register its own.
        if (language_ != nullptr) (void)manager_->RemoveItem(language_);
        if (product_ != nullptr) (void)manager_->RemoveItem(product_);
        manager_->Release();
        manager_ = nullptr;
    }
    if (language_ != nullptr) {
        language_->Release();
        language_ = nullptr;
    }
    if (product_ != nullptr) {
        product_->Release();
        product_ = nullptr;
    }
}

void LangBar::set_state(const bool chinese_input, const std::wstring& schema_label) {
    const std::wstring language = chinese_input ? L"中文" : L"英文";
    if (language_ != nullptr) {
        language_->set_chinese(chinese_input);
        language_->set_menu_state(language, schema_label);
    }
    // The logo never changes, but its menu shows the same state, so a right
    // click on either mark reads the same.
    if (product_ != nullptr) product_->set_menu_state(language, schema_label);
}

}  // namespace piinput::windows
