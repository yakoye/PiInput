#include "stable_text_service.h"
#include "composition_edit_policy.h"
#include "shim_ui_control.h"
#include "client_identity.h"
#include "text_caret_geometry.h"

#include "piinput/host_messages.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <new>
#include <share.h>
#include <fstream>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>
#include <mutex>
#include <unordered_map>
#include <string>
#include <utility>

namespace piinput::windows {

std::atomic<long> g_object_count{0};
HINSTANCE g_module_instance = nullptr;

namespace {

std::atomic<std::uint64_t> next_session_id{1U};

// The mode the user last chose, kept across activations. Switching away to
// another input method and back should come back to what was left, not to the
// configured default -- the default is where a session starts, not somewhere to
// be dragged back to on every switch. Shared across the process so every window
// agrees, the way the taskbar mark implies.
// -1 means nothing has been chosen yet, so settings.ini still decides.
std::atomic<int> remembered_english_mode{-1};

class EditSession final : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context, std::wstring text,
        const std::size_t caret, const bool commit, const bool cancel,
        HostCaretUpdate* const anchor, const bool deferred_completion = false,
        const MirrorRequest* const request = nullptr)
        : service_(service), context_(context), text_(std::move(text)), caret_(caret),
          commit_(commit), cancel_(cancel), anchor_(anchor),
          deferred_completion_(deferred_completion) {
        if (request != nullptr) deferred_request_ = *request;
        service_->AddRef();
        context_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_ITfEditSession)) {
            return E_NOINTERFACE;
        }
        *object = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG value = --ref_count_;
        if (value == 0U) delete this;
        return value;
    }
    STDMETHODIMP DoEditSession(const TfEditCookie cookie) override {
        if (deferred_completion_ && deferred_request_.has_value() &&
            !commit_ && !cancel_ &&
            !service_->is_current_update(*deferred_request_)) {
            service_->complete_deferred_edit(
                context_, S_FALSE, false, false, &*deferred_request_, nullptr);
            return S_OK;
        }
        const HRESULT result = service_->apply_composition_edit(
            context_, cookie, text_, caret_, commit_, cancel_);
        HostCaretUpdate* capture = anchor_;
        if (deferred_completion_ && deferred_request_.has_value() && !commit_ && !cancel_) {
            capture = &deferred_anchor_;
            deferred_anchor_.generation = deferred_request_->generation;
        }
        if (SUCCEEDED(result) && capture != nullptr && !commit_ && !cancel_) {
            service_->capture_composition_caret(context_, cookie, *capture);
        }
        if (deferred_completion_) {
            service_->complete_deferred_edit(
                context_, result, commit_, cancel_,
                deferred_request_ ? &*deferred_request_ : nullptr,
                deferred_request_ ? &deferred_anchor_ : nullptr);
        }
        return result;
    }

private:
    ~EditSession() {
        context_->Release();
        service_->Release();
    }
    std::atomic<ULONG> ref_count_{1U};
    TextService* service_{};
    ITfContext* context_{};
    std::wstring text_;
    std::size_t caret_{};
    bool commit_{};
    bool cancel_{};
    HostCaretUpdate* anchor_{};
    bool deferred_completion_{};
    std::optional<MirrorRequest> deferred_request_;
    HostCaretUpdate deferred_anchor_;
};

// The insertion point as the window manager knows it. Measured against the real
// caret in Notepad++ this was exact 44 times out of 44, while GetTextExt on the
// same document answered with the whole composition extent and reported the
// wrong line. Applications that keep no system caret -- Chromium and anything
// built on it -- return nothing here, and GetTextExt remains the fallback.
[[nodiscard]] std::optional<RECT> system_caret_rect() noexcept {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(0U, &info) == FALSE || info.hwndCaret == nullptr) {
        return std::nullopt;
    }
    RECT rect = info.rcCaret;
    if (rect.right <= rect.left && rect.bottom <= rect.top) return std::nullopt;
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    if (ClientToScreen(info.hwndCaret, &top_left) == FALSE ||
        ClientToScreen(info.hwndCaret, &bottom_right) == FALSE) {
        return std::nullopt;
    }
    if (bottom_right.x <= top_left.x) bottom_right.x = top_left.x + 1;
    const RECT screen{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    return caret_rect_is_plausible(screen) ? std::optional<RECT>{screen} : std::nullopt;
}

// Opt-in key tracing, off unless %TEMP%\piinput-key-trace.on exists. It records
// what happened to each key -- not what the key was, and never any text -- so it
// stays inside the rule against keeping input history. Append mode with shared
// reads, so the file survives a Host restart and can be read while running.
std::FILE* key_trace() {
    static std::FILE* file = [] () -> std::FILE* {
        char temp[MAX_PATH]{};
        if (GetTempPathA(MAX_PATH, temp) == 0U) return nullptr;
        if (GetFileAttributesA((std::string(temp) + "piinput-key-trace.on").c_str()) ==
            INVALID_FILE_ATTRIBUTES) {
            return nullptr;
        }
        const std::string path = std::string(temp) + "piinput-key-trace-" +
            std::to_string(GetCurrentProcessId()) + ".csv";
        const bool fresh = GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::FILE* const opened = _fsopen(path.c_str(), "a", _SH_DENYWR);
        if (opened != nullptr && fresh) {
            (void)std::fprintf(opened, "tick_ms,pid,stage,detail\n");
            (void)std::fflush(opened);
        }
        return opened;
    }();
    return file;
}

void trace_key(const char* const stage, const char* const detail) noexcept {
    std::FILE* const file = key_trace();
    if (file == nullptr) return;
    // Every application process has its own Shim instance writing here, so the
    // process id is what separates one editor's keys from another's.
    (void)std::fprintf(file, "%lu,%lu,%s,%s\n",
        GetTickCount(), GetCurrentProcessId(), stage, detail);
    (void)std::fflush(file);
}

[[nodiscard]] const char* key_kind_name(const HostKeyKind kind, const bool english) noexcept {
    switch (kind) {
    case HostKeyKind::text: return english ? "letter_en" : "letter";
    case HostKeyKind::punctuation: return "punctuation";
    case HostKeyKind::literal_punctuation: return "punctuation_literal";
    case HostKeyKind::space: return "space";
    case HostKeyKind::enter: return "enter";
    case HostKeyKind::select_digit: return "digit";
    case HostKeyKind::backspace: return "backspace";
    default: return "other";
    }
}

// Whether a modifier is held that makes this keystroke somebody else's
// shortcut rather than text for us.
//
// A modifier counts only when the queue state and the physical state agree.
// GetKeyState answers for the message being processed, which is the right
// question -- until another process runs AttachThreadInput against this
// thread and drives it with SendInput, as the symbol picker does to paste.
// If the synthetic Ctrl release is lost on the way out, the queue keeps
// reporting Ctrl as held forever. Every later key then looks like a shortcut,
// nothing is eaten, and every letter reaches the application as Latin text
// while the indicator still reads 中 -- the input method appears to have
// stopped converting, with no way back short of restarting the application.
//
// GetAsyncKeyState reports the physical key and is unaffected by that, so
// requiring both recovers on the next keystroke. A genuine Ctrl+C still has
// both set and still passes through.
[[nodiscard]] bool modifier_is_held(const int key) noexcept {
    return (GetKeyState(key) & 0x8000) != 0 && (GetAsyncKeyState(key) & 0x8000) != 0;
}

[[nodiscard]] bool has_disallowed_modifier() noexcept {
    return modifier_is_held(VK_CONTROL) || modifier_is_held(VK_MENU) ||
           modifier_is_held(VK_LWIN) || modifier_is_held(VK_RWIN);
}

[[nodiscard]] bool is_shift_key(const WPARAM key) noexcept {
    return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

[[nodiscard]] bool is_ascii_letter(const WPARAM key) noexcept {
    return key >= static_cast<WPARAM>('A') && key <= static_cast<WPARAM>('Z');
}

[[nodiscard]] bool is_number_key(const WPARAM key) noexcept {
    return key >= static_cast<WPARAM>('1') && key <= static_cast<WPARAM>('9');
}

[[nodiscard]] bool is_decimal_digit_key(const WPARAM key) noexcept {
    return key >= static_cast<WPARAM>('0') && key <= static_cast<WPARAM>('9');
}

[[nodiscard]] bool shift_is_down() noexcept {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

[[nodiscard]] bool is_punctuation_key(const WPARAM key) noexcept {
    if (key >= static_cast<WPARAM>('0') && key <= static_cast<WPARAM>('9')) {
        return shift_is_down();
    }
    switch (key) {
    case VK_OEM_1:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_OEM_102:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] char punctuation_base_key(const WPARAM key) noexcept {
    if (key >= static_cast<WPARAM>('0') && key <= static_cast<WPARAM>('9')) {
        return static_cast<char>(key);
    }
    switch (key) {
    case VK_OEM_1: return ';';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5:
    case VK_OEM_102: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    default: return '\0';
    }
}

// The low bit is CapsLock being toggled on. The high bit, which is what the
// same call reports for ordinary keys, is the key being physically down.
[[nodiscard]] bool caps_lock_is_on() noexcept {
    return (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
}

[[nodiscard]] char letter_for_key(const WPARAM key, const bool english_mode) noexcept {
    char value = static_cast<char>(key);
    if (!english_mode) return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    const bool shifted = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool caps = caps_lock_is_on();
    if (shifted == caps) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    return value;
}

[[nodiscard]] std::wstring utf8_to_wide_local(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), result.data(), needed) != needed) {
        return {};
    }
    return result;
}

[[nodiscard]] std::size_t utf16_caret_for_utf8(
    const std::string& text,
    const std::size_t byte_caret) {
    const std::size_t clamped = (std::min)(byte_caret, text.size());
    return utf8_to_wide_local(text.substr(0U, clamped)).size();
}

// True only when the range holds exactly `expected` -- including the empty
// string, which is what a freshly opened composition must cover. One extra
// character is requested on purpose: a longer range means the application has
// remapped it onto content we do not own.
[[nodiscard]] bool range_text_equals(
    ITfRange* const range,
    const TfEditCookie edit_cookie,
    const std::wstring& expected) {
    if (range == nullptr) return false;
    std::wstring actual(expected.size() + 1U, L'\0');
    ULONG fetched = 0U;
    if (FAILED(range->GetText(edit_cookie, 0U, actual.data(),
            static_cast<ULONG>(actual.size()), &fetched))) {
        return false;
    }
    if (fetched > expected.size()) return false;
    actual.resize(fetched);
    return actual == expected;
}

// Erasing is only ever right when this service actually wrote something there.
[[nodiscard]] bool range_holds_exactly(
    ITfRange* const range,
    const TfEditCookie edit_cookie,
    const std::wstring& expected) {
    if (expected.empty()) return false;
    return range_text_equals(range, edit_cookie, expected);
}

[[nodiscard]] bool same_com_identity(IUnknown* const left, IUnknown* const right) noexcept {
    if (left == right) return true;
    if (left == nullptr || right == nullptr) return false;
    IUnknown* left_identity = nullptr;
    IUnknown* right_identity = nullptr;
    const HRESULT left_result = left->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&left_identity));
    const HRESULT right_result = right->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&right_identity));
    const bool equal = SUCCEEDED(left_result) && SUCCEEDED(right_result) &&
        left_identity == right_identity;
    if (left_identity != nullptr) left_identity->Release();
    if (right_identity != nullptr) right_identity->Release();
    return equal;
}

}  // namespace

TextService::TextService(const HINSTANCE module)
    : module_(module),
      session_id_(next_session_id.fetch_add(1U)),
      mirror_(process_client_id(), session_id_) {
    ++g_object_count;
}

TextService::~TextService() {
    (void)Deactivate();
    --g_object_count;
}

STDMETHODIMP TextService::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfTextInputProcessor)) {
        *object = static_cast<ITfTextInputProcessor*>(this);
    } else if (IsEqualIID(iid, IID_ITfKeyEventSink)) {
        *object = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(iid, IID_ITfCompositionSink)) {
        *object = static_cast<ITfCompositionSink*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ++ref_count_; }

STDMETHODIMP_(ULONG) TextService::Release() {
    const ULONG value = --ref_count_;
    if (value == 0U) delete this;
    return value;
}

STDMETHODIMP TextService::Activate(
    ITfThreadMgr* const thread_manager,
    const TfClientId client_id) {
    if (thread_manager == nullptr) return E_INVALIDARG;
    (void)Deactivate();
    thread_manager_ = thread_manager;
    thread_manager_->AddRef();
    client_id_ = client_id;

    HRESULT result = thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager_));
    if (FAILED(result)) {
        (void)Deactivate();
        return result;
    }
    result = keystroke_manager_->AdviseKeyEventSink(client_id_, this, TRUE);
    if (FAILED(result)) {
        (void)Deactivate();
        return result;
    }
    key_sink_advised_ = true;
    if (!create_callback_window()) {
        (void)Deactivate();
        return E_FAIL;
    }

    // The 中/英 mark and the product button in the taskbar input indicator.
    // These are language bar items, not notification-area icons: only the
    // former can sit inside the indicator next to each other.
    {
        // IDI_PIINPUT from resource.h. Loading the wrong id left the icon null,
        // and a button whose GetIcon fails is dropped by the shell -- which is
        // why neither button appeared in the indicator.
        lang_bar_icon_ = static_cast<HICON>(LoadImageW(
            module_, MAKEINTRESOURCEW(101), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0U));
        trace_lang_bar_icon(lang_bar_icon_ != nullptr);
        (void)lang_bar_.create(thread_manager_, client_id_, lang_bar_icon_,
            [this](const LangBarCommand command) { on_lang_bar_command(command); });
        english_mode_ = starting_english_mode();
        refresh_lang_bar();
    }

    transport_ = std::make_unique<ShimPipeTransport>(module_);
    // Start the Host now rather than on the first key. Activation happens when
    // the user switches to this input method, seconds before they type, and the
    // dictionary takes most of a second to read even warm.
    transport_->warm_up();
    pipe_client_ = std::make_unique<PipeClient>(
        [this](const HostEnvelope& request) -> std::optional<HostEnvelope> {
            const auto reply = transport_->request(request);
            return reply.has_value() ? reply : std::optional<HostEnvelope>(request);
        },
        [this](const HostEnvelope& reply) {
            // On the transport worker: the Host has answered. Anything between
            // this and reply_handled is time spent waiting for the application
            // to pump its message loop, not time the Host spent working.
            trace_key("reply_arrived", "from_host");
            auto* copy = new (std::nothrow) HostEnvelope(reply);
            if (copy == nullptr) return;
            if (callback_window_ == nullptr ||
                PostMessageW(callback_window_, host_reply_window_message, 0U,
                    reinterpret_cast<LPARAM>(copy)) == FALSE) {
                delete copy;
            }
        });
    mirror_.disconnect();
    const auto request = mirror_.begin_request();
    (void)pipe_client_->send_resume(request, mirror_.resume_state());
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    mode_indicator_.destroy();
    if (pipe_client_ != nullptr) pipe_client_->stop();
    pipe_client_.reset();
    transport_.reset();
    destroy_callback_window();
    clear_deferred_updates();
    final_edit_keys_.clear();
    release_pending_contexts();
    release_active_context();
    if (composition_ != nullptr) {
        composition_->Release();
        composition_ = nullptr;
        composition_written_.clear();
    }
    if (key_sink_advised_ && keystroke_manager_ != nullptr && client_id_ != TF_CLIENTID_NULL) {
        (void)keystroke_manager_->UnadviseKeyEventSink(client_id_);
    }
    key_sink_advised_ = false;
    if (keystroke_manager_ != nullptr) {
        keystroke_manager_->Release();
        keystroke_manager_ = nullptr;
    }
    // Before the thread manager goes: the item is registered under
    // GUID_LBI_INPUTMODE, which is one shared slot for the whole thread. Leaving
    // it behind kept the slot occupied after this service was deactivated, so
    // the next input method could not register its own -- the two appeared to
    // take turns at random.
    lang_bar_.destroy();
    if (lang_bar_icon_ != nullptr) {
        DestroyIcon(lang_bar_icon_);
        lang_bar_icon_ = nullptr;
    }
    if (thread_manager_ != nullptr) {
        thread_manager_->Release();
        thread_manager_ = nullptr;
    }
    client_id_ = TF_CLIENTID_NULL;
    english_mode_ = false;
    english_direct_ = false;
    last_passthrough_was_digit_ = false;
    shift_toggle_.reset();
    last_eaten_key_ = 0U;
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(const BOOL foreground) {
    foreground_ = foreground != FALSE;
    if (pipe_client_ == nullptr) return S_OK;
    if (!foreground_) {
        const auto request = mirror_.begin_request();
        (void)pipe_client_->send_focus(request, false);
        return S_OK;
    }
    const auto focus_request = mirror_.begin_request();
    (void)pipe_client_->send_focus(focus_request, true);
    ITfContext* const context = focused_context();
    if (context != nullptr) {
        if (same_com_identity(active_context_, context)) {
            if (!mirror_.connected()) {
                (void)request_resume(context, mirror_.resume_state());
            }
        } else {
            (void)bind_context(context);
        }
        context->Release();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(
    ITfContext*, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = should_eat_key(wparam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    if (context != nullptr && !same_com_identity(active_context_, context)) {
        // Binding here is a lazy attach to whatever the user is already typing
        // into, not a document switch, so it must not forget that the previous
        // keystroke was a digit. Losing it turned the first "3." of a session
        // into "3。" while every retry produced the ASCII dot.
        const bool digit_run = last_passthrough_was_digit_;
        (void)bind_context(context);
        last_passthrough_was_digit_ = digit_run;
    }
    if (!is_shift_key(wparam) &&
        shift_toggle_.on_other_key_down(shift_is_down())) {
        toggle_input_mode(context);
    }
    const bool consume = should_eat_key(wparam);
    *eaten = consume ? TRUE : FALSE;
    if (!consume) {
        if (is_decimal_digit_key(wparam)) {
            last_passthrough_was_digit_ = true;
        } else if (!is_shift_key(wparam)) {
            last_passthrough_was_digit_ = false;
        }
        return S_OK;
    }
    last_eaten_key_ = wparam;
    if (is_shift_key(wparam)) {
        shift_toggle_.on_shift_down(has_disallowed_modifier());
        return S_OK;
    }
    // Direct English has no composition, no candidates and nothing for the Host
    // to decide: it echoes each letter straight back. Writing it here keeps the
    // character on the same synchronous path a plain keyboard would take. Any
    // request still in flight falls back to the ordered path so a letter cannot
    // overtake a pending punctuation commit.
    if (english_direct_ && english_mode_ && is_ascii_letter(wparam) && context != nullptr &&
        pending_contexts_.empty() && !final_edit_keys_.should_queue(true)) {
        const std::string character(1U, letter_for_key(wparam, true));
        if (request_edit(context, character, character.size(), true, false) !=
            EditRequestResult::failed) {
            last_passthrough_was_digit_ = false;
            return S_OK;
        }
        english_direct_ = false;
    }
    if (context != nullptr) (void)dispatch(context, map_key(wparam));
    last_passthrough_was_digit_ = false;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(
    ITfContext*, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = (is_shift_key(wparam) || wparam == last_eaten_key_) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = (is_shift_key(wparam) || wparam == last_eaten_key_) ? TRUE : FALSE;
    if (is_shift_key(wparam) && shift_toggle_.on_shift_up(has_disallowed_modifier())) {
        toggle_input_mode(context);
    }
    if (wparam == VK_CAPITAL) show_mode_popup();
    if (!is_shift_key(wparam)) (void)shift_toggle_.on_other_key_down(shift_is_down());
    if (wparam == last_eaten_key_) last_eaten_key_ = 0U;
    return S_OK;
}

void TextService::toggle_input_mode(ITfContext* const context) {
    ITfContext* target = context;
    if (target != nullptr) target->AddRef();
    if (target == nullptr) target = focused_context();
    if (target == nullptr) return;
    if (!same_com_identity(active_context_, target)) (void)bind_context(target);
    set_english_mode(!english_mode_);
    show_mode_popup();
    HostKeyEvent event;
    event.kind = english_mode_
        ? HostKeyKind::switch_to_english
        : HostKeyKind::switch_to_chinese;
    (void)dispatch(target, event);
    target->Release();
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(
    const TfEditCookie edit_cookie,
    ITfComposition* const composition) {
    if (composition_ != nullptr && (composition == nullptr || composition == composition_)) {
        ITfRange* terminated_range = nullptr;
        if (SUCCEEDED(composition_->GetRange(&terminated_range)) &&
            terminated_range != nullptr) {
            // This callback represents an external cancellation boundary.  Do
            // not allow the editor to turn the raw pinyin into ordinary text
            // when the user clicked elsewhere or dismissed the popup.
            //
            // Erase only what this service actually wrote. Web editors such as
            // the Claude and Codex message boxes rebuild their document on
            // paste and terminate the composition afterwards; by then this
            // range can map onto the text the user just pasted, and blanking it
            // unconditionally deletes their content. Leaving a stray syllable
            // behind is far better than destroying the user's paste.
            if (range_holds_exactly(terminated_range, edit_cookie,
                    utf8_to_wide_local(mirror_.composition_text()))) {
                (void)terminated_range->SetText(edit_cookie, 0U, L"", 0L);
            }
            terminated_range->Release();
        }
        composition_->Release();
        composition_ = nullptr;
        composition_written_.clear();
        // The application, not PiInput, ended the TSF composition (for example
        // after a screenshot overlay or a mouse click moved the insertion
        // point).  Invalidate replies issued before that boundary and cancel
        // the matching Host session.  Otherwise focus recovery can replay the
        // already-finalized raw pinyin and duplicate it in the editor.
        mirror_.discard_composition();
        clear_deferred_updates();
        final_edit_keys_.clear();
        release_pending_contexts();
        if (active_context_ != nullptr) {
            (void)dispatch(active_context_, {.kind = HostKeyKind::escape});
        }
    }
    return S_OK;
}

bool TextService::should_eat_key(const WPARAM wparam) const noexcept {
    if (has_disallowed_modifier()) return false;
    if (is_shift_key(wparam)) return true;
    // Activation queues a resume handshake on the background pipe worker. Keep
    // the very first Chinese letter behind that handshake instead of leaking it
    // as Latin text while a cold resident Host is still loading its dictionary.
    if (is_ascii_letter(wparam)) return (GetKeyState(VK_SHIFT) & 0x8000) == 0 || english_mode_;
    if (!mirror_.connected() && mirror_.raw().empty() && pending_contexts_.empty()) return false;
    const bool composing = !mirror_.raw().empty() || !pending_contexts_.empty();
    if (is_punctuation_key(wparam)) return true;
    if (!composing) return false;
    if (is_number_key(wparam)) return true;
    switch (wparam) {
    case VK_BACK:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_UP:
    case VK_DOWN:
    case VK_OEM_MINUS:
    case VK_OEM_PLUS:
    case VK_SPACE:
    case VK_RETURN:
    case VK_ESCAPE:
        return true;
    default:
        return false;
    }
}

HostKeyEvent TextService::map_key(const WPARAM wparam) const noexcept {
    HostKeyEvent event;
    if (is_ascii_letter(wparam)) {
        event.kind = HostKeyKind::text;
        event.character = letter_for_key(wparam, english_mode_);
        return event;
    }
    const bool composing = !mirror_.raw().empty() || !pending_contexts_.empty();
    const bool shifted = shift_is_down();
    if (!english_mode_ && composing && !shifted && wparam == VK_OEM_7) {
        event.kind = HostKeyKind::text;
        event.character = '\'';
        return event;
    }
    if (!english_mode_ && !shifted && wparam == VK_OEM_1 &&
        (mirror_.raw().empty() || mirror_.raw().front() == ';')) {
        event.kind = HostKeyKind::text;
        event.character = ';';
        return event;
    }
    if (!english_mode_ && !shifted && wparam == VK_OEM_3 &&
        (mirror_.raw().empty() || mirror_.raw().front() == '`')) {
        event.kind = HostKeyKind::text;
        event.character = '`';
        return event;
    }
    if (is_punctuation_key(wparam) &&
        !(composing && !shifted && (wparam == VK_OEM_MINUS || wparam == VK_OEM_PLUS))) {
        event.kind = !english_mode_ && !shifted && wparam == VK_OEM_PERIOD &&
                last_passthrough_was_digit_
            ? HostKeyKind::literal_punctuation
            : HostKeyKind::punctuation;
        event.character = punctuation_base_key(wparam);
        event.shifted = shifted;
        return event;
    }
    switch (wparam) {
    case VK_BACK: event.kind = HostKeyKind::backspace; break;
    case VK_DELETE: event.kind = HostKeyKind::delete_forward; break;
    case VK_LEFT: event.kind = HostKeyKind::previous_candidate; break;
    case VK_RIGHT: event.kind = HostKeyKind::next_candidate; break;
    case VK_HOME: event.kind = HostKeyKind::move_home; break;
    case VK_END: event.kind = HostKeyKind::move_end; break;
    case VK_DOWN:
    case VK_OEM_PLUS: event.kind = HostKeyKind::expand_next_row; break;
    case VK_UP: event.kind = HostKeyKind::previous_row; break;
    case VK_OEM_MINUS:
        // The dash pages the candidate rows, but it must never become a dead
        // key: the Host turns it back into punctuation when there is no row to
        // move to, so text such as "in-to" stays typeable while composing.
        event.kind = HostKeyKind::previous_row;
        event.character = '-';
        break;
    case VK_RETURN: event.kind = HostKeyKind::enter; break;
    case VK_ESCAPE: event.kind = HostKeyKind::escape; break;
    case VK_SPACE: event.kind = HostKeyKind::space; break;
    default:
        if (is_number_key(wparam)) {
            event.kind = HostKeyKind::select_digit;
            event.character = static_cast<char>(wparam);
        }
        break;
    }
    return event;
}

bool TextService::dispatch(ITfContext* const context, HostKeyEvent event) {
    const char* const kind = key_kind_name(event.kind, english_mode_);
    if (final_edit_keys_.should_queue(
            key_may_begin_final_edit(event.kind, english_mode_))) {
        trace_key("key_queued", kind);
        final_edit_keys_.push(std::move(event));
        return true;
    }
    trace_key("key_dispatch", kind);
    return dispatch_now(context, std::move(event), false);
}

bool TextService::dispatch_now(
    ITfContext* const context,
    HostKeyEvent event,
    const bool replayed_key) {
    if (pipe_client_ == nullptr || context == nullptr) return false;
    event.resume = mirror_.resume_state();
    const auto request = mirror_.begin_request();
    context->AddRef();
    pending_contexts_.emplace(
        request.sequence,
        PendingContext{request.session_id, context, replayed_key});
    trace_key("send_key_begin", "send");
    const bool sent = pipe_client_->send_key(request, event);
    trace_key("send_key_end", sent ? "ok" : "failed");
    if (!sent) {
        const auto found = pending_contexts_.find(request.sequence);
        if (found != pending_contexts_.end()) {
            found->second.context->Release();
            pending_contexts_.erase(found);
        }
        mirror_.disconnect();
        return false;
    }
    return true;
}

void TextService::dispatch_replayed_key(std::optional<HostKeyEvent> event) noexcept {
    if (!event.has_value()) return;
    if (active_context_ == nullptr ||
        !dispatch_now(active_context_, std::move(*event), true)) {
        final_edit_keys_.clear();
    }
}

void TextService::handle_reply(HostEnvelope envelope) noexcept {
    if (envelope.type == HostMessageType::caret || envelope.type == HostMessageType::focus ||
        envelope.type == HostMessageType::commit_result) return;
    // Separates "the Host was slow" from "the reply sat waiting for this
    // application's message loop": this fires when the reply is handed to the
    // service, the edit trace fires when it reaches the document.
    trace_key("reply_handled", "key_reply");
    const auto found = pending_contexts_.find(envelope.sequence);
    const bool matching_pending = found != pending_contexts_.end() &&
        found->second.session_id == envelope.session_id;
    ITfContext* context = matching_pending ? found->second.context : nullptr;
    const bool replayed_key = matching_pending && found->second.replayed_key;
    ITfContext* recovery_context = nullptr;
    std::optional<HostResumeState> recovery;
    std::optional<HostKeyEvent> next_replayed_key;
    bool replay_reply_completed = false;
    if (envelope.type != HostMessageType::key_reply) {
        mirror_.disconnect();
    } else {
        HostPayloadError error = HostPayloadError::none;
        const auto reply = decode_host_reply(envelope.payload, error, envelope.version);
        const MirrorRequest request{
            envelope.client_id,
            envelope.session_id,
            envelope.sequence,
            envelope.generation,
        };
        if (reply.has_value() && mirror_.confirm(request, *reply) && context != nullptr) {
            set_english_mode(reply->snapshot.mode == HostInputMode::english);
            // A one-character commit with no composition behind it is the
            // signature of direct English. With English candidates enabled the
            // same key returns a composition update instead, so this probe
            // re-answers itself after every mode switch.
            english_direct_ = english_mode_ &&
                reply->action == HostAction::commit &&
                reply->snapshot.raw.empty() && reply->text.size() == 1U &&
                (reply->text.front() >= 'A' && reply->text.front() <= 'z');
            if (reply->action == HostAction::update) {
                (void)request_update_edit(
                    context, request, mirror_.composition_text(), mirror_.caret());
            } else if (reply->action == HostAction::commit) {
                // A final edit owns the composition boundary.  Any asynchronous
                // update accepted for the just-finished raw input must never be
                // replayed into the next word after this commit completes.
                clear_deferred_updates();
                if (replayed_key) {
                    (void)final_edit_keys_.complete_replayed_reply(true);
                    replay_reply_completed = true;
                } else {
                    final_edit_keys_.begin_final_edit();
                }
                const EditRequestResult edit_result = request_edit(
                    context, mirror_.pending_commit(), mirror_.pending_commit().size(), true, false,
                    nullptr, true, &request);
                if (edit_result == EditRequestResult::pending) {
                    (void)deferred_updates_.begin(request);
                } else {
                    recovery = mirror_.complete_edit(edit_result == EditRequestResult::completed);
                    send_commit_result(
                        reply->snapshot.generation,
                        edit_result == EditRequestResult::completed);
                    next_replayed_key = final_edit_keys_.complete_final_edit();
                }
            } else if (reply->action == HostAction::cancel) {
                clear_deferred_updates();
                if (replayed_key) {
                    (void)final_edit_keys_.complete_replayed_reply(true);
                    replay_reply_completed = true;
                } else {
                    final_edit_keys_.begin_final_edit();
                }
                const EditRequestResult edit_result = request_edit(
                    context, {}, 0U, false, true, nullptr, true, &request);
                if (edit_result == EditRequestResult::pending) {
                    (void)deferred_updates_.begin(request);
                } else {
                    recovery = mirror_.complete_edit(edit_result == EditRequestResult::completed);
                    next_replayed_key = final_edit_keys_.complete_final_edit();
                }
            }
            if (recovery.has_value()) {
                context->AddRef();
                recovery_context = context;
            }
        }
    }
    if (replayed_key && !replay_reply_completed) {
        next_replayed_key = final_edit_keys_.complete_replayed_reply(false);
    }
    if (matching_pending) {
        found->second.context->Release();
        pending_contexts_.erase(found);
    }
    if (recovery_context != nullptr) {
        (void)request_resume(recovery_context, *recovery);
        recovery_context->Release();
    }
    dispatch_replayed_key(std::move(next_replayed_key));
}

void TextService::release_pending_contexts() noexcept {
    for (auto& [sequence, pending] : pending_contexts_) {
        (void)sequence;
        pending.context->Release();
    }
    pending_contexts_.clear();
}

ITfContext* TextService::focused_context() const noexcept {
    if (thread_manager_ == nullptr) return nullptr;
    ITfDocumentMgr* document = nullptr;
    HRESULT result = thread_manager_->GetFocus(&document);
    if (FAILED(result) || document == nullptr) return nullptr;
    ITfContext* context = nullptr;
    result = document->GetTop(&context);
    document->Release();
    return SUCCEEDED(result) ? context : nullptr;
}

bool TextService::bind_context(ITfContext* const context) {
    if (context == nullptr || same_com_identity(active_context_, context)) {
        return context != nullptr;
    }

    // A TSF service instance can be shared by several tabs/documents in one
    // process. Never carry one tab's raw composition or Host session into the
    // next context. End the old composition while its context is still known;
    // if the host rejects the synchronous edit, detach locally rather than
    // ever applying that old range to the new context.
    if (active_context_ != nullptr && composition_ != nullptr) {
        const EditRequestResult ended = request_edit(
            active_context_, {}, 0U, false, true, nullptr, false);
        if (ended != EditRequestResult::completed && composition_ != nullptr) {
            composition_->Release();
            composition_ = nullptr;
            composition_written_.clear();
        }
    }
    release_pending_contexts();
    clear_deferred_updates();
    final_edit_keys_.clear();
    release_active_context();

    active_context_ = context;
    active_context_->AddRef();
    session_id_ = next_session_id.fetch_add(1U);
    mirror_.reset_session(session_id_);
    set_english_mode(starting_english_mode());
    english_direct_ = false;
    last_passthrough_was_digit_ = false;
    shift_toggle_.reset();
    last_eaten_key_ = 0U;
    return request_resume(context, {});
}

void TextService::release_active_context() noexcept {
    if (active_context_ == nullptr) return;
    active_context_->Release();
    active_context_ = nullptr;
}

bool TextService::request_resume(
    ITfContext* const context,
    const HostResumeState& state) {
    if (pipe_client_ == nullptr || context == nullptr) return false;
    const auto request = mirror_.begin_request();
    context->AddRef();
    pending_contexts_.emplace(request.sequence, PendingContext{request.session_id, context});
    if (!pipe_client_->send_resume(request, state)) {
        const auto found = pending_contexts_.find(request.sequence);
        if (found != pending_contexts_.end()) {
            found->second.context->Release();
            pending_contexts_.erase(found);
        }
        mirror_.disconnect();
        return false;
    }
    mirror_.reconnect();
    return true;
}

EditRequestResult TextService::request_update_edit(
    ITfContext* const context,
    const MirrorRequest& request,
    const std::string& text,
    const std::size_t caret) {
    if (deferred_updates_.busy()) {
        trace_key("update_deferred", "queue_busy");
        deferred_updates_.defer({request, text, caret});
        return EditRequestResult::pending;
    }
    HostCaretUpdate anchor{.generation = request.generation};
    const EditRequestResult result = request_edit(
        context, text, caret, false, false, &anchor, true, &request);
    if (result == EditRequestResult::completed && !text.empty()) {
        send_candidate_anchor(request, anchor);
    } else if (result == EditRequestResult::pending) {
        (void)deferred_updates_.begin(request);
    }
    return result;
}

void TextService::replay_deferred_update() noexcept {
    auto update = std::exchange(scheduled_update_, std::nullopt);
    if (!update.has_value() || active_context_ == nullptr ||
        update->request.session_id != session_id_ ||
        !mirror_.is_current_update(update->request)) {
        return;
    }
    if (request_update_edit(
            active_context_, update->request, update->text, update->caret) ==
        EditRequestResult::failed) {
        mirror_.disconnect();
    }
}

void TextService::clear_deferred_updates() noexcept {
    deferred_updates_.clear();
    scheduled_update_.reset();
}

bool TextService::create_callback_window() noexcept {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = module_;
    window_class.lpfnWndProc = &TextService::callback_window_proc;
    window_class.lpszClassName = stable_shim_callback_window_class;
    if (RegisterClassExW(&window_class) == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    callback_window_ = CreateWindowExW(
        0U, stable_shim_callback_window_class, L"", 0U, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, module_, this);
    return callback_window_ != nullptr;
}

void TextService::destroy_callback_window() noexcept {
    if (callback_window_ == nullptr) return;
    MSG message{};
    while (PeekMessageW(&message, callback_window_, host_reply_window_message,
            host_reply_window_message, PM_REMOVE) != FALSE) {
        delete reinterpret_cast<HostEnvelope*>(message.lParam);
    }
    DestroyWindow(callback_window_);
    callback_window_ = nullptr;
}

EditRequestResult TextService::request_edit(
    ITfContext* const context,
    const std::string& text,
    const std::size_t caret,
    const bool commit,
    const bool cancel,
    HostCaretUpdate* const anchor,
    const bool allow_async,
    const MirrorRequest* const request) {
    if (context == nullptr || client_id_ == TF_CLIENTID_NULL) return EditRequestResult::failed;
    const std::wstring wide = utf8_to_wide_local(text);
    const std::size_t wide_caret = utf16_caret_for_utf8(text, caret);
    auto* session = new (std::nothrow) EditSession(
        this, context, wide, wide_caret, commit, cancel, anchor);
    if (session == nullptr) return EditRequestResult::failed;
    HRESULT session_result = E_FAIL;
    const HRESULT request_result = context->RequestEditSession(
        client_id_, session, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    session->Release();
    if (SUCCEEDED(request_result) && SUCCEEDED(session_result)) {
        trace_key("edit_sync", commit ? "commit" : (cancel ? "cancel" : "update"));
        return EditRequestResult::completed;
    }
    trace_key("edit_sync_refused", commit ? "commit" : (cancel ? "cancel" : "update"));
    if (!allow_async) return EditRequestResult::failed;

    auto* deferred = new (std::nothrow) EditSession(
        this, context, wide, wide_caret, commit, cancel, nullptr, true, request);
    if (deferred == nullptr) return EditRequestResult::failed;
    session_result = E_FAIL;
    const HRESULT deferred_request = context->RequestEditSession(
        client_id_, deferred, TF_ES_ASYNC | TF_ES_READWRITE, &session_result);
    deferred->Release();
    const bool queued = SUCCEEDED(deferred_request) && session_result == TF_S_ASYNC;
    trace_key(queued ? "edit_async_queued" : "edit_async_failed",
        commit ? "commit" : (cancel ? "cancel" : "update"));
    return queued ? EditRequestResult::pending : EditRequestResult::failed;
}

void TextService::complete_deferred_edit(
    ITfContext* const context,
    const HRESULT result,
    const bool commit,
    const bool cancel,
    const MirrorRequest* const request,
    const HostCaretUpdate* const anchor) noexcept {
    trace_key("edit_ran", commit ? "commit" : (cancel ? "cancel" : "update"));
    if (!commit && !cancel) {
        const bool current = request != nullptr && mirror_.is_current_update(*request);
        if (SUCCEEDED(result) && current && anchor != nullptr && !mirror_.raw().empty()) {
            send_candidate_anchor(*request, *anchor);
        } else if (FAILED(result) && current) {
            mirror_.disconnect();
        }
        if (request != nullptr) {
            scheduled_update_ = deferred_updates_.complete(*request);
            if (scheduled_update_.has_value() && callback_window_ != nullptr) {
                (void)PostMessageW(callback_window_, host_replay_update_message, 0U, 0L);
            }
        }
        return;
    }
    const auto recovery = mirror_.complete_edit(SUCCEEDED(result));
    if (commit && request != nullptr) {
        send_commit_result(request->generation, SUCCEEDED(result));
    }
    if ((commit || cancel) && request != nullptr) {
        scheduled_update_ = deferred_updates_.complete(*request);
        if (scheduled_update_.has_value() && callback_window_ != nullptr) {
            (void)PostMessageW(callback_window_, host_replay_update_message, 0U, 0L);
        }
    }
    if (recovery.has_value() && context != nullptr) {
        (void)request_resume(context, *recovery);
    }
    dispatch_replayed_key(final_edit_keys_.complete_final_edit());
}

void TextService::send_candidate_anchor(
    const MirrorRequest& request,
    const HostCaretUpdate& update) noexcept {
    if (pipe_client_ == nullptr) return;
    (void)pipe_client_->send_caret(request, update);
}

void TextService::send_commit_result(
    const std::uint64_t generation,
    const bool succeeded) noexcept {
    if (pipe_client_ == nullptr) return;
    const MirrorRequest request = mirror_.begin_request();
    // The pipe carries one request at a time, so anything slow here holds up
    // every key behind it.
    trace_key("commit_result_begin", "send");
    (void)pipe_client_->send_commit_result(
        request, HostCommitResult{generation, succeeded});
    trace_key("commit_result_end", "send");
}

// The taskbar indicator already carries this state, but it is at the far edge
// of the screen while the eyes are on the text. Without a hint next to the
// caret a Shift press is only discovered after a word comes out in the wrong
// language.
void TextService::show_mode_popup() noexcept {
    mode_indicator_.show(
        mode_mark_for(english_mode_, caps_lock_is_on()), system_caret_rect());
}

void TextService::refresh_lang_bar() noexcept {
    lang_bar_.set_state(!english_mode_, schema_display_name());
    publish_conversion_mode();
}

void TextService::publish_conversion_mode() noexcept {
    trace_lang_bar_stage("Conversion.enter", thread_manager_ != nullptr ? 1 : 0);
    if (thread_manager_ == nullptr) return;
    ITfCompartmentMgr* manager = nullptr;
    const HRESULT query = thread_manager_->QueryInterface(IID_PPV_ARGS(&manager));
    trace_lang_bar_stage("Conversion.mgr", static_cast<long>(query));
    if (FAILED(query) || manager == nullptr) return;
    ITfCompartment* compartment = nullptr;
    const HRESULT got = manager->GetCompartment(
        GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &compartment);
    trace_lang_bar_stage("Conversion.compartment", static_cast<long>(got));
    if (SUCCEEDED(got) && compartment != nullptr) {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        // Native means Chinese conversion is on; zero means letters go through
        // as typed. Windows renders 中 or 英 in the indicator from exactly this.
        value.lVal = english_mode_ ? 0L : static_cast<LONG>(TF_CONVERSIONMODE_NATIVE);
        const HRESULT set = compartment->SetValue(client_id_, &value);
        trace_lang_bar_stage("Conversion.set", static_cast<long>(set));
        trace_lang_bar_stage("Conversion.value", value.lVal);
        (void)VariantClear(&value);
        compartment->Release();
    }
    manager->Release();
}

namespace {

[[nodiscard]] std::filesystem::path user_settings_path() {
    PWSTR local = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)) && local != nullptr) {
        result = std::filesystem::path(local) / L"PiInput" / L"UserData" / L"settings.ini";
    }
    if (local != nullptr) CoTaskMemFree(local);
    return result;
}

// Every key in settings.ini, reparsed only when the file changes.
//
// These are read on the UI path: binding an input context asks for the default
// language, and refreshing the mark asks for the schema. Doing it uncached
// meant a shell folder lookup plus a full parse of the file for each of those,
// on every focus change, inside every application hosting the Shim.
//
// The last-write time is what invalidates it, so the settings window writing
// the file still takes effect without a restart -- which is the whole point of
// hot_reload.
[[nodiscard]] std::unordered_map<std::string, std::string> parse_settings(
    const std::filesystem::path& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream input(path, std::ios::binary);
    if (!input) return values;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::size_t start = 0U;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        if (start >= line.size() || line[start] == '#' || line[start] == ';') continue;
        const auto equals = line.find('=', start);
        if (equals == std::string::npos) continue;
        std::string found = line.substr(start, equals - start);
        while (!found.empty() && found.back() == ' ') found.pop_back();
        std::string value = line.substr(equals + 1U);
        std::size_t value_start = 0U;
        while (value_start < value.size() && value[value_start] == ' ') ++value_start;
        // First wins, matching the previous behaviour of returning the first
        // line that named the key.
        (void)values.emplace(std::move(found), value.substr(value_start));
    }
    return values;
}

[[nodiscard]] std::string settings_value(const std::string& key) {
    // One resolution of the folder for the life of the process; it cannot move
    // while the application runs.
    static const std::filesystem::path path = user_settings_path();
    static std::mutex guard;
    static std::unordered_map<std::string, std::string> cached;
    static FILETIME cached_write{};
    static bool loaded = false;

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const bool readable = !path.empty() && GetFileAttributesExW(
        path.c_str(), GetFileExInfoStandard, &attributes) != FALSE;

    const std::lock_guard lock(guard);
    const bool changed = !loaded || !readable ||
        attributes.ftLastWriteTime.dwLowDateTime != cached_write.dwLowDateTime ||
        attributes.ftLastWriteTime.dwHighDateTime != cached_write.dwHighDateTime;
    if (changed) {
        cached = parse_settings(path);
        cached_write = readable ? attributes.ftLastWriteTime : FILETIME{};
        loaded = true;
    }
    const auto found = cached.find(key);
    return found == cached.end() ? std::string{} : found->second;
}

[[nodiscard]] std::filesystem::path sibling_program(
    const HINSTANCE module,
    const wchar_t* const name) {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(module, path, MAX_PATH) == 0U) return {};
    return std::filesystem::path(path).parent_path() / name;
}

}  // namespace

std::wstring TextService::schema_display_name() const {
    const auto schema = settings_value("schema");
    if (schema == "full") return L"全拼";
    if (schema == "natural") return L"自然码双拼";
    if (schema == "mspy") return L"微软双拼";
    if (schema == "abc") return L"智能 ABC 双拼";
    return L"小鹤双拼";
}

bool TextService::default_language_is_english() const {
    return settings_value("default_language") == "english";
}

// What a newly activated or newly bound context should start in.
bool TextService::starting_english_mode() const {
    const int remembered = remembered_english_mode.load();
    if (remembered >= 0) return remembered != 0;
    return default_language_is_english();
}

void TextService::set_english_mode(const bool english) noexcept {
    remembered_english_mode.store(english ? 1 : 0);
    if (english_mode_ == english) return;
    english_mode_ = english;
    // Re-probe direct English after every switch; the setting may have changed.
    english_direct_ = false;
    refresh_lang_bar();
}

void TextService::on_lang_bar_command(const LangBarCommand command) noexcept {
    switch (command) {
    case LangBarCommand::toggle_language: {
        // Clicking the indicator moves focus to the taskbar, so there is often
        // no context to send the switch through. Flip the mode anyway and let
        // the next keystroke carry it -- otherwise the button does nothing at
        // exactly the moment the user presses it.
        ITfContext* const target = focused_context();
        if (target != nullptr) {
            toggle_input_mode(target);
            target->Release();
        } else {
            set_english_mode(!english_mode_);
            show_mode_popup();
        }
        return;
    }
    case LangBarCommand::toggle_schema: {
        // Written to settings.ini; the host reloads it at the next composition
        // boundary, so the tray, the settings window and this button all apply
        // a change the same way.
        const auto path = user_settings_path();
        const bool to_full = settings_value("schema") != "full";
        std::ifstream input(path, std::ios::binary);
        std::string text{
            std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        input.close();
        const std::string wanted = to_full ? "schema=full" : "schema=flypy";
        const auto position = text.find("schema=");
        if (position != std::string::npos) {
            const auto end = text.find('\n', position);
            text.replace(position, (end == std::string::npos ? text.size() : end) - position,
                wanted);
        } else {
            text = "[general]\n" + wanted + "\n" + text;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        refresh_lang_bar();
        return;
    }
    case LangBarCommand::symbols: {
        const auto configured = settings_value("symbol_tool");
        std::filesystem::path tool = configured.empty()
            ? sibling_program(module_, L"yesymbol.exe")
            : std::filesystem::path(std::wstring(configured.begin(), configured.end()));
        if (std::filesystem::is_regular_file(tool)) {
            (void)ShellExecuteW(nullptr, L"open", tool.c_str(), nullptr,
                tool.parent_path().c_str(), SW_SHOWNORMAL);
        } else {
            MessageBoxW(nullptr,
                L"找不到符号工具 yesymbol.exe。\n\n"
                L"它随 PiInput 一同安装，正常在程序目录下。也可以在设置窗口的"
                L"「标点符号」页指定其他路径。",
                L"PiInput 符号", MB_OK | MB_ICONINFORMATION);
        }
        return;
    }
    case LangBarCommand::settings: {
        const auto settings = sibling_program(module_, L"PiInput-Settings.exe");
        if (std::filesystem::is_regular_file(settings)) {
            (void)ShellExecuteW(nullptr, L"open", settings.c_str(), nullptr,
                settings.parent_path().c_str(), SW_SHOWNORMAL);
        }
        return;
    }
    case LangBarCommand::about: {
        const std::string version(PIINPUT_VERSION);
        const std::wstring about =
            L"PiInput " + std::wstring(version.begin(), version.end()) + L"\n\n"
            L"轻量、快速、纯离线的中文输入法。\n"
            L"不含 AI、语音、广告与云端联想。";
        MessageBoxW(nullptr, about.c_str(), L"关于 PiInput", MB_OK | MB_ICONINFORMATION);
        return;
    }
    case LangBarCommand::help: {
        const auto guide = sibling_program(module_, L"").parent_path() / L"安装与使用指南.md";
        const auto target = std::filesystem::is_regular_file(guide)
            ? guide
            : sibling_program(module_, L"").parent_path();
        (void)ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    }
}

void TextService::cancel_from_host_ui() noexcept {
    mirror_.discard_composition();
    clear_deferred_updates();
    final_edit_keys_.clear();
    release_pending_contexts();
    if (active_context_ != nullptr) {
        (void)request_edit(active_context_, {}, 0U, false, true);
    }
}

void TextService::select_candidate_from_host_ui(const std::uint64_t candidate_id) noexcept {
    if (active_context_ == nullptr) return;
    HostKeyEvent event;
    event.kind = HostKeyKind::select_candidate;
    event.candidate_id = candidate_id;
    (void)dispatch(active_context_, std::move(event));
}

void TextService::capture_composition_caret(
    ITfContext* const context,
    const TfEditCookie edit_cookie,
    HostCaretUpdate& update) const noexcept {
    update.has_text_caret = false;
    if (context == nullptr) return;
    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || view == nullptr) return;

    const auto query_rect = [&](ITfRange* const range) -> std::optional<RECT> {
        if (range == nullptr || FAILED(range->Collapse(edit_cookie, TF_ANCHOR_END))) {
            return std::nullopt;
        }
        RECT rect{};
        BOOL clipped = FALSE;
        if (FAILED(view->GetTextExt(edit_cookie, range, &rect, &clipped))) {
            return std::nullopt;
        }
        return usable_text_caret_rect(rect) ? std::optional<RECT>{rect} : std::nullopt;
    };

    std::optional<RECT> selection_rect;
    TF_SELECTION selection{};
    ULONG fetched = 0U;
    if (SUCCEEDED(context->GetSelection(
            edit_cookie, TF_DEFAULT_SELECTION, 1U, &selection, &fetched)) &&
        fetched != 0U && selection.range != nullptr) {
        selection_rect = query_rect(selection.range);
        selection.range->Release();
    }

    // Both sources are queried, not just the first that answers: the selection
    // can come back as the whole composition extent, and only the other one can
    // then supply a real caret.
    std::optional<RECT> composition_rect;
    ITfRange* composition_range = nullptr;
    if ((!selection_rect.has_value() || !caret_rect_is_plausible(*selection_rect)) &&
        composition_ != nullptr &&
        SUCCEEDED(composition_->GetRange(&composition_range)) && composition_range != nullptr) {
        composition_rect = query_rect(composition_range);
        composition_range->Release();
    }

    // The system caret wins when the application maintains one: it is the
    // position the user actually sees, and GetTextExt has been measured
    // reporting a different line for the same insertion point.
    const auto caret = system_caret_rect();
    const auto chosen = caret.has_value() ? caret : choose_text_caret_geometry(
        selection_rect ? &*selection_rect : nullptr,
        composition_rect ? &*composition_rect : nullptr);
    if (chosen.has_value()) {
        const RECT reported = *chosen;
        RECT rect = reported;
        HWND view_window = nullptr;
        const bool has_view_window =
            SUCCEEDED(view->GetWnd(&view_window)) && view_window != nullptr;
        const DPI_AWARENESS awareness = has_view_window
            ? GetAwarenessFromDpiAwarenessContext(
                GetWindowDpiAwarenessContext(view_window))
            : DPI_AWARENESS_INVALID;
        const bool per_monitor_aware =
            awareness == DPI_AWARENESS_PER_MONITOR_AWARE;
        if (has_view_window && !per_monitor_aware) {
            POINT top_left{rect.left, rect.top};
            POINT bottom_right{rect.right, rect.bottom};
            if (LogicalToPhysicalPointForPerMonitorDPI(view_window, &top_left) != FALSE &&
                LogicalToPhysicalPointForPerMonitorDPI(view_window, &bottom_right) != FALSE) {
                const RECT converted{
                    top_left.x, top_left.y, bottom_right.x, bottom_right.y};
                rect = normalized_text_caret_geometry(
                    reported, converted, per_monitor_aware);
            }
        }
        update.has_text_caret = true;
        update.left = rect.left;
        update.top = rect.top;
        update.right = rect.right;
        update.bottom = rect.bottom;
    }
    view->Release();
}

bool TextService::is_current_update(
    const MirrorRequest& request) const noexcept {
    return mirror_.is_current_update(request);
}

HRESULT TextService::insert_text_at_selection(
    ITfContext* const context,
    const TfEditCookie edit_cookie,
    const std::wstring& text) {
    if (text.empty()) return S_OK;
    ITfInsertAtSelection* insertion = nullptr;
    HRESULT result = context->QueryInterface(IID_PPV_ARGS(&insertion));
    if (FAILED(result) || insertion == nullptr) return FAILED(result) ? result : E_FAIL;
    ITfRange* range = nullptr;
    result = insertion->InsertTextAtSelection(
        edit_cookie, 0U, text.c_str(), static_cast<LONG>(text.size()), &range);
    insertion->Release();
    if (FAILED(result) || range == nullptr) return FAILED(result) ? result : E_FAIL;

    TF_SELECTION selection{};
    selection.range = range;
    selection.style.ase = TF_AE_END;
    selection.style.fInterimChar = FALSE;
    if (SUCCEEDED(range->Collapse(edit_cookie, TF_ANCHOR_END))) {
        // The text is already in the document; a host that refuses to move its
        // own caret must not turn this into a failed commit.
        (void)context->SetSelection(edit_cookie, 1U, &selection);
    }
    range->Release();
    return S_OK;
}

HRESULT TextService::apply_composition_edit(
    ITfContext* const context,
    const TfEditCookie edit_cookie,
    const std::wstring& text,
    const std::size_t caret,
    const bool commit,
    const bool cancel) {
    if (context == nullptr) return E_INVALIDARG;
    if (composition_ == nullptr && commit) {
        if (SUCCEEDED(insert_text_at_selection(context, edit_cookie, text))) return S_OK;
        // A host without ITfInsertAtSelection still has to receive the text, so
        // fall through to the slower composition path rather than dropping it.
    }
    const auto start_composition = [&]() -> HRESULT {
        TF_SELECTION selection{};
        ULONG fetched = 0U;
        HRESULT started = context->GetSelection(
            edit_cookie, TF_DEFAULT_SELECTION, 1U, &selection, &fetched);
        if (FAILED(started) || fetched == 0U || selection.range == nullptr) {
            return FAILED(started) ? started : E_FAIL;
        }
        started = selection.range->Collapse(edit_cookie, TF_ANCHOR_END);
        if (FAILED(started)) {
            selection.range->Release();
            return started;
        }
        ITfContextComposition* composition_context = nullptr;
        started = context->QueryInterface(IID_PPV_ARGS(&composition_context));
        if (SUCCEEDED(started)) {
            started = composition_context->StartComposition(
                edit_cookie, selection.range, this, &composition_);
            composition_context->Release();
        }
        selection.range->Release();
        if (FAILED(started)) return started;
        if (composition_ == nullptr) return E_FAIL;
        composition_written_.clear();
        return S_OK;
    };

    if (composition_ == nullptr && !cancel) {
        const HRESULT started = start_composition();
        if (FAILED(started)) return started;
    }
    if (composition_ == nullptr) return S_OK;

    ITfRange* range = nullptr;
    HRESULT result = composition_->GetRange(&range);
    if (FAILED(result) || range == nullptr) return FAILED(result) ? result : E_FAIL;

    // The composition range must still hold exactly what was written into it
    // last time -- nothing at all for one that just opened. Web editors such as
    // the Claude and Codex message boxes rebuild their document when text is
    // pasted, and the range TSF hands back afterwards can span the text the
    // user pasted. SetText would replace it, so drop that composition and open
    // a clean one instead of writing over content we do not own.
    if (!range_text_equals(range, edit_cookie, composition_written_)) {
        range->Release();
        range = nullptr;
        ITfComposition* const foreign = composition_;
        composition_ = nullptr;
        composition_written_.clear();
        (void)foreign->EndComposition(edit_cookie);
        foreign->Release();
        if (cancel) return S_OK;
        const HRESULT restarted = start_composition();
        if (FAILED(restarted)) return restarted;
        result = composition_->GetRange(&range);
        if (FAILED(result) || range == nullptr) return FAILED(result) ? result : E_FAIL;
        if (!range_text_equals(range, edit_cookie, std::wstring{})) {
            // Still not ours. Losing this keystroke is the correct trade: the
            // alternative is destroying whatever the application put there.
            range->Release();
            ITfComposition* const unusable = composition_;
            composition_ = nullptr;
            composition_written_.clear();
            (void)unusable->EndComposition(edit_cookie);
            unusable->Release();
            return S_OK;
        }
    }

    result = range->SetText(
        edit_cookie, 0U, text.empty() ? L"" : text.c_str(), static_cast<LONG>(text.size()));
    if (SUCCEEDED(result)) composition_written_ = text;
    const auto policy = composition_edit_policy(commit, cancel);
    if (SUCCEEDED(result) && policy.finalize_before_selection) {
        ITfComposition* ending = composition_;
        composition_ = nullptr;
        composition_written_.clear();
        result = ending->EndComposition(edit_cookie);
        ending->Release();
    }
    if (FAILED(result)) {
        range->Release();
        return result;
    }

    HRESULT selection_result = range->Collapse(edit_cookie, TF_ANCHOR_START);
    if (SUCCEEDED(selection_result)) {
        LONG shifted = 0L;
        selection_result = range->ShiftEnd(edit_cookie,
            static_cast<LONG>((std::min)(caret, text.size())), &shifted, nullptr);
    }
    if (SUCCEEDED(selection_result)) {
        TF_SELECTION selection{};
        selection.range = range;
        selection.style.ase = TF_AE_END;
        selection.style.fInterimChar = FALSE;
        selection_result = range->Collapse(edit_cookie, TF_ANCHOR_END);
        if (SUCCEEDED(selection_result)) {
            selection_result = context->SetSelection(edit_cookie, 1U, &selection);
        }
    }

    if (!policy.selection_failure_is_fatal) {
        selection_result = S_OK;
    }
    result = selection_result;
    range->Release();
    return result;
}

LRESULT CALLBACK TextService::callback_window_proc(
    const HWND window,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* service = reinterpret_cast<TextService*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == host_reply_window_message) {
        std::unique_ptr<HostEnvelope> reply(reinterpret_cast<HostEnvelope*>(lparam));
        if (service != nullptr && reply != nullptr) service->handle_reply(std::move(*reply));
        return 0L;
    }
    if (message == host_cancel_composition_message && service != nullptr &&
        static_cast<std::uint64_t>(lparam) == service->session_id_) {
        service->cancel_from_host_ui();
        return 0L;
    }
    if (message == host_replay_update_message && service != nullptr) {
        service->replay_deferred_update();
        return 0L;
    }
    if (message == host_select_candidate_message && service != nullptr) {
        service->select_candidate_from_host_ui(static_cast<std::uint64_t>(lparam));
        return 0L;
    }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0L);
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace piinput::windows
