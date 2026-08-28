#include "stable_text_service.h"
#include "composition_edit_policy.h"
#include "shim_ui_control.h"
#include "client_identity.h"
#include "input_scope_policy.h"
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

// inputscope.h declares GUID_PROP_INPUTSCOPE via DEFINE_GUID, but the MinGW and
// MSVC link surfaces are not consistent about supplying its storage. Keep the
// documented property value local so the TSF DLL has no extra GUID dependency.
constexpr GUID kInputScopePropertyGuid{
    0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};

std::atomic<long> g_object_count{0};
HINSTANCE g_module_instance = nullptr;

namespace {

std::atomic<std::uint64_t> next_session_id{1U};

[[nodiscard]] std::string settings_value(const std::string& key);

class InputScopeQuerySession final : public ITfEditSession {
public:
    explicit InputScopeQuerySession(ITfContext* const context) : context_(context) {
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
        resolved_ = true;
        TF_SELECTION selection{};
        ULONG fetched = 0U;
        if (FAILED(context_->GetSelection(
                cookie, TF_DEFAULT_SELECTION, 1U, &selection, &fetched)) ||
            fetched == 0U || selection.range == nullptr) {
            if (selection.range != nullptr) selection.range->Release();
            return S_OK;
        }

        ITfReadOnlyProperty* property = nullptr;
        const HRESULT property_result = context_->GetAppProperty(
            kInputScopePropertyGuid, &property);
        if (SUCCEEDED(property_result) && property != nullptr) {
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(property->GetValue(cookie, selection.range, &value)) &&
                value.vt == VT_UNKNOWN && value.punkVal != nullptr) {
                ITfInputScope* input_scope = nullptr;
                if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&input_scope))) &&
                    input_scope != nullptr) {
                    InputScope* scopes = nullptr;
                    UINT count = 0U;
                    if (SUCCEEDED(input_scope->GetInputScopes(&scopes, &count)) &&
                        scopes != nullptr) {
                        for (UINT index = 0U; index < count; ++index) {
                            if (sensitive_input_scope(scopes[index])) {
                                sensitive_ = true;
                                break;
                            }
                        }
                        CoTaskMemFree(scopes);
                    }
                    input_scope->Release();
                }
            }
            (void)VariantClear(&value);
            property->Release();
        }
        selection.range->Release();
        return S_OK;
    }

    [[nodiscard]] bool resolved() const noexcept { return resolved_; }
    [[nodiscard]] bool sensitive() const noexcept { return sensitive_; }

private:
    ~InputScopeQuerySession() { context_->Release(); }

    std::atomic<ULONG> ref_count_{1U};
    ITfContext* context_{};
    bool resolved_{};
    bool sensitive_{};
};

class SurroundingTextQuerySession final : public ITfEditSession {
public:
    explicit SurroundingTextQuerySession(ITfContext* const context) : context_(context) {
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
        TF_SELECTION selection{};
        ULONG fetched = 0U;
        if (FAILED(context_->GetSelection(
                cookie, TF_DEFAULT_SELECTION, 1U, &selection, &fetched)) ||
            fetched == 0U || selection.range == nullptr) {
            if (selection.range != nullptr) selection.range->Release();
            return S_OK;
        }
        ITfRange* left = nullptr;
        ITfRange* right = nullptr;
        const HRESULT left_clone = selection.range->Clone(&left);
        const HRESULT right_clone = selection.range->Clone(&right);
        selection.range->Release();
        if (FAILED(left_clone) || FAILED(right_clone) || left == nullptr || right == nullptr) {
            if (left != nullptr) left->Release();
            if (right != nullptr) right->Release();
            return S_OK;
        }

        std::array<WCHAR, 65U> left_buffer{};
        std::array<WCHAR, 65U> right_buffer{};
        ULONG left_read = 0U;
        ULONG right_read = 0U;
        LONG shifted = 0L;
        const bool left_ok = SUCCEEDED(left->Collapse(cookie, TF_ANCHOR_START)) &&
            SUCCEEDED(left->ShiftStart(cookie, -64L, &shifted, nullptr)) &&
            SUCCEEDED(left->GetText(cookie, 0U, left_buffer.data(), 64U, &left_read));
        shifted = 0L;
        const bool right_ok = SUCCEEDED(right->Collapse(cookie, TF_ANCHOR_END)) &&
            SUCCEEDED(right->ShiftEnd(cookie, 64L, &shifted, nullptr)) &&
            SUCCEEDED(right->GetText(cookie, 0U, right_buffer.data(), 64U, &right_read));
        left->Release();
        right->Release();
        if (!left_ok || !right_ok) return S_OK;
        left_.assign(left_buffer.data(), left_read);
        right_.assign(right_buffer.data(), right_read);
        resolved_ = true;
        return S_OK;
    }

    [[nodiscard]] bool resolved() const noexcept { return resolved_; }
    [[nodiscard]] const std::wstring& left() const noexcept { return left_; }
    [[nodiscard]] const std::wstring& right() const noexcept { return right_; }

private:
    ~SurroundingTextQuerySession() { context_->Release(); }

    std::atomic<ULONG> ref_count_{1U};
    ITfContext* context_{};
    bool resolved_{};
    std::wstring left_;
    std::wstring right_;
};

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
        const MirrorRequest* const request = nullptr,
        const bool smart_punctuation_completion = false,
        const std::uint64_t smart_session_id = 0U)
        : service_(service), context_(context), text_(std::move(text)), caret_(caret),
          commit_(commit), cancel_(cancel), anchor_(anchor),
          deferred_completion_(deferred_completion),
          smart_punctuation_completion_(smart_punctuation_completion),
          smart_session_id_(smart_session_id) {
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
        if (smart_punctuation_completion_ &&
            !service_->is_current_smart_punctuation(context_, smart_session_id_)) {
            service_->complete_smart_punctuation_edit(
                context_, E_ABORT, commit_, cancel_, smart_session_id_);
            return S_OK;
        }
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
            if (smart_punctuation_completion_) {
                service_->complete_smart_punctuation_edit(
                    context_, result, commit_, cancel_, smart_session_id_);
            } else {
                service_->complete_deferred_edit(
                    context_, result, commit_, cancel_,
                    deferred_request_ ? &*deferred_request_ : nullptr,
                    deferred_request_ ? &deferred_anchor_ : nullptr);
            }
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
    bool smart_punctuation_completion_{};
    std::uint64_t smart_session_id_{};
    std::optional<MirrorRequest> deferred_request_;
    HostCaretUpdate deferred_anchor_;
};

// The insertion point as the window manager knows it. Measured against the real
// caret in Notepad++ this was exact 44 times out of 44, while GetTextExt on the
// same document answered with the whole composition extent and reported the
// wrong line. Applications that keep no system caret -- Chromium and anything
// built on it -- return nothing here, and GetTextExt remains the fallback.
// Whether a reported caret could belong to this window at all.
//
// Deliberately generous: the window's own rectangle plus a margin, because a
// caret at the last column of a maximised editor legitimately sits on the
// frame. This is not measuring accuracy, only catching an answer that cannot
// be about this window -- a point on another monitor, or the corner of the
// primary screen when the window is elsewhere.
[[nodiscard]] bool rect_is_within_window(const RECT& rect, const HWND window) noexcept {
    if (window == nullptr) return true;
    RECT bounds{};
    if (GetWindowRect(window, &bounds) == FALSE) return true;
    constexpr LONG margin = 64;
    return rect.left >= bounds.left - margin && rect.right <= bounds.right + margin &&
           rect.top >= bounds.top - margin && rect.bottom <= bounds.bottom + margin;
}

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
    return key >= static_cast<WPARAM>('0') && key <= static_cast<WPARAM>('9') &&
        (GetKeyState(VK_SHIFT) & 0x8000) == 0;
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

[[nodiscard]] char smart_punctuation_symbol(const WPARAM key) noexcept {
    const bool shifted = shift_is_down();
    if (!shifted && key == VK_OEM_PERIOD) return '.';
    if (!shifted && key == VK_OEM_COMMA) return ',';
    if (!shifted && key == VK_OEM_2) return '/';
    if (shifted && key == VK_OEM_2) return '?';
    if (shifted && key == VK_OEM_1) return ':';
    if (!shifted && key == VK_OEM_4) return '[';
    if (!shifted && key == VK_OEM_6) return ']';
    if (!shifted && key == VK_OEM_7) return '\'';
    if (shifted && key == VK_OEM_7) return '"';
    if (shifted && key == VK_OEM_MINUS) return '_';
    if (shifted && key == static_cast<WPARAM>('1')) return '!';
    if (shifted && key == static_cast<WPARAM>('9')) return '(';
    if (shifted && key == static_cast<WPARAM>('0')) return ')';
    return '\0';
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

[[nodiscard]] std::string wide_to_utf8_local(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), result.data(), needed, nullptr, nullptr) != needed) {
        return {};
    }
    return result;
}

inline constexpr ULONG_PTR smart_punctuation_replay_tag =
    static_cast<ULONG_PTR>(0x504950554E435452ULL);  // "PIPUNCTR"

[[nodiscard]] bool is_smart_punctuation_replay() noexcept {
    return static_cast<ULONG_PTR>(GetMessageExtraInfo()) == smart_punctuation_replay_tag;
}

// Scintilla intentionally exposes only its active TSF composition through the
// TSF document store.  Already committed direct keys (notably digits) can
// therefore be absent from an otherwise successful ITfRange read.  Read the
// same bounded window from Scintilla's documented, read-only message surface as
// a host adapter.  This remains document truth; it is not inferred from which
// key callbacks happened to fire.
[[nodiscard]] bool query_scintilla_surrounding_text(
    std::string& left,
    std::string& right) noexcept {
    HWND editor = GetFocus();
    if (editor == nullptr) {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(0U, &info) != FALSE) editor = info.hwndFocus;
    }
    if (editor == nullptr) return false;
    DWORD process_id = 0U;
    (void)GetWindowThreadProcessId(editor, &process_id);
    if (process_id != GetCurrentProcessId()) return false;
    std::array<wchar_t, 32U> class_name{};
    if (GetClassNameW(editor, class_name.data(), static_cast<int>(class_name.size())) <= 0 ||
        _wcsicmp(class_name.data(), L"Scintilla") != 0) {
        return false;
    }

    constexpr UINT sci_get_length = 2006U;
    constexpr UINT sci_get_char_at = 2007U;
    constexpr UINT sci_get_current_pos = 2008U;
    const LRESULT length_result = SendMessageW(editor, sci_get_length, 0U, 0L);
    const LRESULT caret_result = SendMessageW(editor, sci_get_current_pos, 0U, 0L);
    if (length_result < 0 || caret_result < 0 || caret_result > length_result) return false;
    const std::size_t length = static_cast<std::size_t>(length_result);
    const std::size_t caret = static_cast<std::size_t>(caret_result);
    constexpr std::size_t context_bytes = 192U;
    const std::size_t left_start = caret > context_bytes ? caret - context_bytes : 0U;
    const std::size_t right_end = (std::min)(length, caret + context_bytes);
    left.clear();
    right.clear();
    left.reserve(caret - left_start);
    right.reserve(right_end - caret);
    for (std::size_t position = left_start; position < caret; ++position) {
        const LRESULT value = SendMessageW(
            editor, sci_get_char_at, static_cast<WPARAM>(position), 0L);
        left.push_back(static_cast<char>(static_cast<unsigned char>(value & 0xFF)));
    }
    for (std::size_t position = caret; position < right_end; ++position) {
        const LRESULT value = SendMessageW(
            editor, sci_get_char_at, static_cast<WPARAM>(position), 0L);
        right.push_back(static_cast<char>(static_cast<unsigned char>(value & 0xFF)));
    }
    return true;
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
    if (IsEqualIID(iid, IID_IUnknown) ||
        IsEqualIID(iid, IID_ITfTextInputProcessor) ||
        IsEqualIID(iid, IID_ITfTextInputProcessorEx)) {
        *object = static_cast<ITfTextInputProcessorEx*>(this);
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

STDMETHODIMP TextService::ActivateEx(
    ITfThreadMgr* const thread_manager,
    const TfClientId client_id,
    const DWORD flags) {
    const HRESULT result = Activate(thread_manager, client_id);
    if (SUCCEEDED(result)) activation_flags_ = flags;
    return result;
}

STDMETHODIMP TextService::Deactivate() {
    end_candidate_ui();
    mode_indicator_.destroy();
    if (pipe_client_ != nullptr) pipe_client_->stop();
    pipe_client_.reset();
    transport_.reset();
    destroy_callback_window();
    clear_deferred_updates();
    final_edit_keys_.clear();
    clear_smart_punctuation();
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
    activation_flags_ = 0U;
    english_mode_ = false;
    sensitive_context_ = false;
    english_direct_ = false;
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
    ITfContext* const context = focused_context();
    if (context != nullptr) {
        const std::uint64_t previous_session = session_id_;
        (void)bind_context(context);
        if (!sensitive_context_ && same_com_identity(active_context_, context)) {
            // bind_context already resumes a new context or a privacy-scope
            // transition. Only reconnect an unchanged session here.
            if (session_id_ == previous_session && !mirror_.connected()) {
                (void)request_resume(context, mirror_.resume_state());
            }
            const auto focus_request = mirror_.begin_request();
            (void)pipe_client_->send_focus(focus_request, true);
        }
        context->Release();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    if (is_smart_punctuation_replay()) {
        *eaten = FALSE;
        return S_OK;
    }
    // Refresh the bound scope here, not only in OnKeyDown: when a browser
    // reuses one TSF context for an ordinary field and a password field,
    // returning FALSE means OnKeyDown will not be called. bind_context also
    // clears any ordinary-field composition/candidate state on that boundary.
    if (context != nullptr) (void)bind_context(context);
    // Backstop for an application that drops the release too: a press still
    // owed from an earlier key is run before this one is judged, so the two
    // cannot arrive out of order.
    flush_dropped_key_down(context);
    const bool sensitive = context != nullptr && sensitive_context_ &&
        same_com_identity(active_context_, context);
    *eaten = sensitive ? FALSE : (should_eat_key(wparam) ? TRUE : FALSE);
    // Claimed but not yet delivered. OnKeyDown clears this the moment the
    // application hands the press over, which is what almost every
    // application does; what remains set is a press that was dropped.
    if (*eaten != FALSE && !is_shift_key(wparam)) claimed_without_keydown_ = wparam;
    // Which keys the application offers at all, and what was decided. Without
    // this, a key the application never hands over and a key PiInput declines
    // look identical in a trace -- both are simply absent -- and the boundary
    // between "we chose wrong" and "we were never asked" cannot be drawn.
    //
    // MobaXterm is the case that needed it: Backspace never appears here while
    // Enter, Space and Escape do, and those four share one branch of
    // should_eat_key. Letters are logged as a class, not by character.
    {
        char detail[32]{};
        if (is_ascii_letter(wparam)) {
            (void)std::snprintf(detail, sizeof(detail), "letter=%d",
                *eaten != FALSE ? 1 : 0);
        } else {
            (void)std::snprintf(detail, sizeof(detail), "vk%02lX=%d",
                static_cast<unsigned long>(wparam), *eaten != FALSE ? 1 : 0);
        }
        trace_key("key_offered", detail);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    // Paired with key_offered. A key claimed in OnTestKeyDown and never seen
    // here was dropped by the application between the two calls: it asked
    // whether we wanted the key, was told yes, and then neither delivered it
    // nor handled it itself. Without both lines that is indistinguishable
    // from PiInput swallowing the key on its own.
    {
        char detail[32]{};
        (void)std::snprintf(detail, sizeof(detail), "vk%02lX",
            static_cast<unsigned long>(wparam));
        trace_key("key_down", detail);
    }
    if (is_smart_punctuation_replay()) {
        *eaten = FALSE;
        return S_OK;
    }
    if (context != nullptr) (void)bind_context(context);
    if (sensitive_context_ && same_com_identity(active_context_, context)) {
        *eaten = FALSE;
        last_eaten_key_ = 0U;
        shift_toggle_.reset();
        return S_OK;
    }
    if (!is_shift_key(wparam) &&
        shift_toggle_.on_other_key_down(shift_is_down())) {
        toggle_input_mode(context);
    }
    const bool consume = should_eat_key(wparam);
    *eaten = consume ? TRUE : FALSE;
    if (!consume) return S_OK;
    // The application delivered the press it was told we wanted, so there is
    // nothing left to make good.
    claimed_without_keydown_ = 0U;
    apply_eaten_key_down(context, wparam);
    return S_OK;
}

// The body of a press PiInput has decided to take. Split out of OnKeyDown so
// that a press the application claimed to hand over and then dropped can still
// be run, from whichever callback arrives next. See flush_dropped_key_down.
void TextService::apply_eaten_key_down(
    ITfContext* const context,
    const WPARAM wparam) {
    last_eaten_key_ = wparam;
    if (is_shift_key(wparam)) {
        shift_toggle_.on_shift_down(has_disallowed_modifier());
        return;
    }
    if (!english_mode_ && mirror_.raw().empty() && !has_pending_key_request()) {
        const std::string punctuation_mode = settings_value("mode");
        smart_punctuation_enabled_ = punctuation_mode != "english" &&
            punctuation_mode != "programmer";
    }
    if (provisional_punctuation_.has_value() &&
        resolve_smart_punctuation_key(context, wparam)) {
        return;
    }
    if (!english_mode_ && handle_smart_punctuation_key(context, wparam)) {
        return;
    }
    // Direct English has no composition, no candidates and nothing for the Host
    // to decide: it echoes each letter straight back. Writing it here keeps the
    // character on the same synchronous path a plain keyboard would take. Any
    // request still in flight falls back to the ordered path so a letter cannot
    // overtake a pending punctuation commit.
    //
    // allow_async is false, and that is the whole point of this path. Letting
    // request_edit fall back to TF_ES_ASYNC would queue the letter and return
    // `pending`, which reads as success here -- and the next letter, granted a
    // synchronous lock, would then be written ahead of the queued one. Key
    // traces show deferred sessions running two to four seconds after they
    // were queued, so the window is wide. pending_contexts_ cannot catch this:
    // it tracks Host round-trips, not edit sessions.
    //
    // Refusing async keeps the ordering guarantee instead of the keystroke:
    // request_edit reports failure, english_direct_ turns off, and the letter
    // falls through to dispatch(), where the Host sequences it.
    //
    // No recorded trace has this path losing its lock -- edit_sync_refused
    // never carries `commit` -- so this closes a hazard rather than a
    // reproduced fault. It is not the explanation for `previewIdentity`
    // arriving as `previewdI`; that one is still open.
    if (english_direct_ && english_mode_ && is_ascii_letter(wparam) && context != nullptr &&
        pending_contexts_.empty() && !final_edit_keys_.should_queue(true)) {
        const std::string character(1U, letter_for_key(wparam, true));
        // This path writes straight to the document and never reaches the
        // Host, so it produces no key_dispatch line. Without a trace of its
        // own a direct letter is invisible except as a bare edit_sync, which
        // is exactly the half of the picture a reordering question needs.
        char labelled[16]{};
        (void)std::snprintf(labelled, sizeof(labelled), "direct:%c", character.front());
        trace_key("key_direct_en", labelled);
        if (request_edit(context, character, character.size(), true, false,
                nullptr, false) != EditRequestResult::failed) {
            return;
        }
        english_direct_ = false;
        trace_key("key_direct_refused", labelled);
    }
    // A key eaten here that reaches neither dispatch() nor one of the paths
    // above vanishes: the application does not get it and the Host is never
    // told. Naming the case makes that visible instead of silent.
    if (context == nullptr) {
        trace_key("key_dropped", "no_context");
        return;
    }
    (void)dispatch(context, map_key(wparam));
}

// Runs a press the application asked about, was told we wanted, and then never
// delivered.
//
// MobaXterm does this with Backspace, and only Backspace: its terminal handles
// that one key in its own window procedure, so the message never reaches the
// bridge that would call OnKeyDown. Having answered "yes" we are not given the
// key, and having been answered "yes" the terminal does not act on it either.
// The keystroke simply disappears -- the composition will not shrink and the
// terminal shows nothing, which reads as the input method having frozen.
//
// Answering "no" instead is worse: the terminal would then delete its own text
// while the composition it cannot see stays behind.
//
// So the press is run late, from the first callback that follows. The release
// of the same key is the earliest of those and is what normally fires; the next
// press is the backstop for an application that drops the release as well.
void TextService::flush_dropped_key_down(ITfContext* const context) {
    if (claimed_without_keydown_ == 0U) return;
    const WPARAM missed = claimed_without_keydown_;
    claimed_without_keydown_ = 0U;
    char detail[32]{};
    (void)std::snprintf(detail, sizeof(detail), "vk%02lX",
        static_cast<unsigned long>(missed));
    trace_key("key_down_recovered", detail);
    ITfContext* target = context;
    if (target == nullptr) target = active_context_;
    if (target == nullptr) return;
    apply_eaten_key_down(target, missed);
}

STDMETHODIMP TextService::OnTestKeyUp(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    if (is_smart_punctuation_replay()) {
        *eaten = FALSE;
        return S_OK;
    }
    // A press still owed is claimed on its release as well, so that OnKeyUp is
    // called and can run it. last_eaten_key_ cannot answer for it: that is set
    // in the OnKeyDown which never happened.
    *eaten = context_has_sensitive_input_scope(context)
        ? FALSE
        : ((is_shift_key(wparam) || wparam == last_eaten_key_ ||
            wparam == claimed_without_keydown_) ? TRUE : FALSE);
    // Whether the release of a key arrives at all. It matters for a key whose
    // press the application dropped: the release is the first callback after
    // the missing OnKeyDown, and so the earliest place the press could be made
    // good. If the release is dropped too, that recovery point does not exist.
    {
        char detail[32]{};
        (void)std::snprintf(detail, sizeof(detail), "vk%02lX=%d",
            static_cast<unsigned long>(wparam), *eaten != FALSE ? 1 : 0);
        trace_key("key_up_offered", detail);
    }
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) return E_POINTER;
    {
        char detail[32]{};
        (void)std::snprintf(detail, sizeof(detail), "vk%02lX",
            static_cast<unsigned long>(wparam));
        trace_key("key_up", detail);
    }
    if (is_smart_punctuation_replay()) {
        *eaten = FALSE;
        return S_OK;
    }
    if (context_has_sensitive_input_scope(context)) {
        *eaten = FALSE;
        last_eaten_key_ = 0U;
        claimed_without_keydown_ = 0U;
        shift_toggle_.reset();
        return S_OK;
    }
    // The usual recovery point: the release of a key whose press was dropped
    // arrives within milliseconds of where OnKeyDown would have been, so the
    // keystroke is made good before the user can notice it missing.
    if (wparam == claimed_without_keydown_) flush_dropped_key_down(context);
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
    if (sensitive_context_) {
        target->Release();
        return;
    }
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
            if (provisional_punctuation_.has_value() &&
                range_holds_exactly(terminated_range, edit_cookie, composition_written_)) {
                const std::wstring resolved =
                    utf8_to_wide_local(provisional_punctuation_->chinese +
                        provisional_punctuation_->accumulated_text);
                (void)terminated_range->SetText(edit_cookie, 0U,
                    resolved.empty() ? L"" : resolved.c_str(),
                    static_cast<LONG>(resolved.size()));
            } else if (range_holds_exactly(terminated_range, edit_cookie,
                           utf8_to_wide_local(mirror_.composition_text()))) {
                (void)terminated_range->SetText(edit_cookie, 0U, L"", 0L);
            }
            terminated_range->Release();
        }
        composition_->Release();
        composition_ = nullptr;
        composition_written_.clear();
        clear_smart_punctuation();
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
    if (provisional_punctuation_.has_value()) return true;
    // Activation queues a resume handshake on the background pipe worker. Keep
    // the very first Chinese letter behind that handshake instead of leaking it
    // as Latin text while a cold resident Host is still loading its dictionary.
    // Letters are ours, Shift or no Shift. Declining Shift+letter in Chinese
    // mode used to let the application type the capital itself, which cost
    // more than it saved: a declined key never reaches OnKeyDown, so
    // shift_toggle_ never learned the press was a chord and read the release
    // as a lone tap -- switching to English and committing the composition
    // mid-word. map_key routes the capital through the Host instead.
    //
    // Ctrl is checked again here even though has_disallowed_modifier already
    // covers it, because that check requires GetKeyState and GetAsyncKeyState
    // to agree and can be defeated -- see its own comment. The Shift test this
    // replaced happened to decline Ctrl+Shift+letter whenever that happened,
    // and Ctrl+Shift+V and Ctrl+Shift+C are worth more than the margin costs.
    // Either reading of Ctrl is enough to keep out of the way.
    if (is_ascii_letter(wparam)) {
        return (GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
               (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
    }
    if (!mirror_.connected() && mirror_.raw().empty() && pending_contexts_.empty()) return false;
    const bool composing = !mirror_.raw().empty() || has_pending_key_request();
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

bool TextService::has_pending_key_request() const noexcept {
    for (const auto& [sequence, pending] : pending_contexts_) {
        (void)sequence;
        if (pending.key_request) return true;
    }
    return false;
}

HostKeyEvent TextService::map_key(const WPARAM wparam) const noexcept {
    HostKeyEvent event;
    if (is_ascii_letter(wparam)) {
        event.kind = HostKeyKind::text;
        // Chinese mode folds letters to lower case, because they are a reading
        // rather than text. Shift is how someone says otherwise mid-word, and
        // the capital travels to the Host as itself: nothing else can put an
        // upper-case letter on this path, so it needs no key kind of its own
        // and no protocol version to carry it.
        //
        // This used to be declined and left to the application instead, which
        // broke twice over. A declined key never reaches OnKeyDown, so the
        // Shift press still looked like a lone tap when released and flipped
        // to English mid-word; and the letter reached the document outside
        // TSF, unordered against the composition that flip had just
        // committed. `previewIdentity` arrived as `previewdI` that way.
        const bool literal = !english_mode_ && shift_is_down() && !caps_lock_is_on();
        event.character = literal
            ? static_cast<char>(wparam)
            : letter_for_key(wparam, english_mode_);
        return event;
    }
    const bool composing = !mirror_.raw().empty() || has_pending_key_request();
    const bool shifted = shift_is_down();
    if (!english_mode_ && composing && !shifted && wparam == VK_OEM_7) {
        event.kind = HostKeyKind::text;
        event.character = '\'';
        return event;
    }
    // The backtick used to open a composition of its own -- ``f, ``u and so on
    // -- and it is gone. What is left is a punctuation key like any other, so
    // it falls through to the punctuation branch below.
    if (is_punctuation_key(wparam) &&
        !(composing && !shifted && (wparam == VK_OEM_MINUS || wparam == VK_OEM_PLUS))) {
        event.kind = HostKeyKind::punctuation;
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

bool TextService::handle_smart_punctuation_key(
    ITfContext* const context,
    const WPARAM wparam) {
    const char symbol = smart_punctuation_symbol(wparam);
    if (symbol == '\0' || context == nullptr) return false;

    // English punctuation is an explicit user choice even while the input
    // language remains Chinese. "programmer" remains only as a compatibility
    // alias written by older builds; both bypass the smart preview.
    if (!smart_punctuation_enabled_) return false;

    const bool composing = !mirror_.raw().empty() || has_pending_key_request();
    std::string left;
    std::string right;
    bool snapshot_available = true;
    if (!composing && symbol != '/') {
        snapshot_available = query_surrounding_text(context, left, right);
        const bool scintilla_snapshot = left.empty() &&
            query_scintilla_surrounding_text(left, right);
        snapshot_available = snapshot_available || scintilla_snapshot;
        trace_key("smart_context_source", scintilla_snapshot ? "scintilla" : "tsf");
        const char* const context_class = !snapshot_available
            ? "unavailable"
            : (left.empty()
                ? "empty"
                : (SmartPunctuationEngine::is_ascii_digit(left.back())
                    ? "digit_tail"
                    : "non_digit_tail"));
        trace_key("smart_context", context_class);
    }
    const SmartPunctuationDecision decision = smart_punctuation_engine_.decide({
        .symbol = symbol,
        .left_text = left,
        .right_text = right,
        .composing = composing,
    });
    trace_key("smart_punctuation", decision.rule_id.data());
    trace_key("smart_context_type", decision.context_type.data());

    if (decision.action == SmartPunctuationAction::transform) return false;
    if (decision.action == SmartPunctuationAction::literal) {
        HostKeyEvent event;
        event.kind = HostKeyKind::literal_punctuation;
        event.character = punctuation_base_key(wparam);
        event.shifted = shift_is_down();
        (void)dispatch(context, std::move(event));
        return true;
    }

    ProvisionalPunctuation provisional;
    provisional.ascii = symbol;
    provisional.chinese.assign(decision.chinese_text);
    provisional.rule_id.assign(decision.rule_id);
    provisional_punctuation_ = std::move(provisional);
    const std::string preview(1U, symbol);
    const EditRequestResult result = request_edit(
        context, preview, preview.size(), false, false,
        nullptr, true, nullptr, true);
    if (result == EditRequestResult::failed) {
        clear_smart_punctuation();
        return false;
    }
    return true;
}

bool TextService::resolve_smart_punctuation_key(
    ITfContext* const context,
    const WPARAM wparam) {
    if (!provisional_punctuation_.has_value()) return false;
    if (context == nullptr) {
        clear_smart_punctuation();
        return true;
    }

    if (wparam == VK_BACK && !provisional_punctuation_->accumulated_text.empty()) {
        provisional_punctuation_->accumulated_text.pop_back();
        std::string text(1U, provisional_punctuation_->ascii);
        text.append(provisional_punctuation_->accumulated_text);
        trace_key("smart_resolution", "PUNC-PENDING-BACKSPACE");
        if (request_edit(context, text, text.size(), false, false,
                nullptr, true, nullptr, true) == EditRequestResult::failed) {
            clear_smart_punctuation();
        }
        return true;
    }

    const ProvisionalPunctuation provisional = *provisional_punctuation_;
    const bool escape = wparam == VK_ESCAPE;
    bool cancel = wparam == VK_BACK || (escape && provisional.accumulated_text.empty());
    const char next_character = is_decimal_digit_key(wparam)
        ? static_cast<char>(wparam)
        : '\0';
    const SmartPunctuationResolution resolution =
        smart_punctuation_engine_.resolve_provisional(
            provisional.ascii, next_character, provisional.rule_id,
            provisional.accumulated_text);
    trace_key("smart_resolution", resolution.rule_id.data());
    std::string text;
    smart_replay_event_.reset();
    smart_replay_virtual_key_ = 0U;

    if (resolution.continue_provisional) {
        provisional_punctuation_->accumulated_text.push_back(next_character);
        text.push_back(provisional.ascii);
        text.append(provisional_punctuation_->accumulated_text);
        if (request_edit(context, text, text.size(), false, false,
                nullptr, true, nullptr, true) == EditRequestResult::failed) {
            clear_smart_punctuation();
        }
        return true;
    }

    if (escape && !provisional.accumulated_text.empty()) {
        text.assign(provisional.accumulated_text);
    } else if (!cancel && resolution.keep_ascii) {
        text.push_back(provisional.ascii);
        text.append(provisional.accumulated_text);
        text.push_back(next_character);
    } else if (!cancel && wparam == VK_SPACE) {
        text.assign(resolution.chinese_text);
        text.append(provisional.accumulated_text);
        text.push_back(' ');
    } else if (!cancel) {
        text.assign(resolution.chinese_text);
        text.append(provisional.accumulated_text);
        if (is_ascii_letter(wparam) || is_punctuation_key(wparam)) {
            smart_replay_event_ = map_key(wparam);
        } else {
            // Enter/Tab/navigation retain their application meaning. They are
            // replayed only after the punctuation edit reaches the document,
            // so a send action cannot overtake the final Chinese symbol.
            smart_replay_virtual_key_ = wparam;
        }
    }

    const EditRequestResult result = request_edit(
        context, text, text.size(), !cancel, cancel,
        nullptr, true, nullptr, true);
    if (result == EditRequestResult::completed) {
        complete_smart_punctuation_edit(
            context, S_OK, !cancel, cancel, session_id_);
    } else if (result == EditRequestResult::failed) {
        smart_replay_event_.reset();
        smart_replay_virtual_key_ = 0U;
    }
    return true;
}

void TextService::clear_smart_punctuation() noexcept {
    provisional_punctuation_.reset();
    smart_replay_event_.reset();
    smart_replay_virtual_key_ = 0U;
}

void TextService::replay_virtual_key(const WPARAM wparam) noexcept {
    if (wparam == 0U) return;
    std::array<INPUT, 2U> inputs{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = static_cast<WORD>(wparam);
    inputs[0].ki.dwExtraInfo = smart_punctuation_replay_tag;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    (void)SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

bool TextService::dispatch(ITfContext* const context, HostKeyEvent event) {
    // The character goes into the trace alongside the kind. Without it a trace
    // shows that seven letters were typed but not which, and the question
    // being asked of these traces -- whether a letter came out in the wrong
    // place -- cannot be answered from counts alone.
    char labelled[16]{};
    const char* const kind_name = key_kind_name(event.kind, english_mode_);
    const char* kind = kind_name;
    if (event.kind == HostKeyKind::text && event.character > 0x20 &&
        event.character < 0x7F && event.character != ',') {
        (void)std::snprintf(labelled, sizeof(labelled), "%s:%c", kind_name, event.character);
        kind = labelled;
    }
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
    if (pipe_client_ == nullptr || context == nullptr ||
        (sensitive_context_ && same_com_identity(active_context_, context))) return false;
    event.resume = mirror_.resume_state();
    const auto request = mirror_.begin_request();
    context->AddRef();
    pending_contexts_.emplace(
        request.sequence,
        PendingContext{request.session_id, context, true, replayed_key});
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
            const bool was_direct = english_direct_;
            english_direct_ = english_mode_ &&
                reply->action == HostAction::commit &&
                reply->snapshot.raw.empty() && reply->text.size() == 1U &&
                (reply->text.front() >= 'A' && reply->text.front() <= 'z');
            // Which path the next letter takes turns on this one flag, so a
            // trace that shows letters without it cannot explain why one went
            // to the Host and the next did not.
            if (was_direct != english_direct_) {
                trace_key("english_direct", english_direct_ ? "on" : "off");
            }
            if (reply->action == HostAction::update) {
                (void)update_candidate_ui(context, reply->snapshot);
                (void)request_update_edit(
                    context, request, mirror_.composition_text(), mirror_.caret());
            } else if (reply->action == HostAction::commit) {
                end_candidate_ui();
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
            } else if (reply->action == HostAction::cancel ||
                reply->action == HostAction::launch_symbol_tool ||
                reply->action == HostAction::launch_settings ||
                reply->action == HostAction::launch_program) {
                end_candidate_ui();
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
                    const bool completed = edit_result == EditRequestResult::completed;
                    recovery = mirror_.complete_edit(completed);
                    complete_candidate_action(reply->action, reply->text, completed);
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

bool TextService::context_has_sensitive_input_scope(
    ITfContext* const context) const noexcept {
    if (context == nullptr || client_id_ == TF_CLIENTID_NULL) return false;
    auto* session = new (std::nothrow) InputScopeQuerySession(context);
    if (session == nullptr) return false;
    HRESULT session_result = E_FAIL;
    const HRESULT request_result = context->RequestEditSession(
        client_id_, session, TF_ES_SYNC | TF_ES_READ, &session_result);
    const bool sensitive = SUCCEEDED(request_result) && SUCCEEDED(session_result) &&
        session->resolved() && session->sensitive();
    session->Release();
    return sensitive;
}

bool TextService::query_surrounding_text(
    ITfContext* const context,
    std::string& left,
    std::string& right) const noexcept {
    left.clear();
    right.clear();
    if (context == nullptr || client_id_ == TF_CLIENTID_NULL) return false;
    auto* session = new (std::nothrow) SurroundingTextQuerySession(context);
    if (session == nullptr) return false;
    HRESULT session_result = E_FAIL;
    const HRESULT request_result = context->RequestEditSession(
        client_id_, session, TF_ES_SYNC | TF_ES_READ, &session_result);
    const bool result = SUCCEEDED(request_result) && SUCCEEDED(session_result) &&
        session->resolved();
    if (result) {
        left = wide_to_utf8_local(session->left());
        right = wide_to_utf8_local(session->right());
    }
    session->Release();
    return result;
}

bool TextService::bind_context(ITfContext* const context) {
    if (context == nullptr) return false;
    const bool sensitive = context_has_sensitive_input_scope(context);
    if (same_com_identity(active_context_, context) && sensitive == sensitive_context_) {
        return true;
    }

    // A TSF service instance can be shared by several tabs/documents in one
    // process. Never carry one tab's raw composition or Host session into the
    // next context. End the old composition while its context is still known;
    // if the host rejects the synchronous edit, detach locally rather than
    // ever applying that old range to the new context.
    if (active_context_ != nullptr && composition_ != nullptr) {
        std::string final_text;
        bool commit = false;
        bool cancel = true;
        if (provisional_punctuation_.has_value()) {
            final_text = provisional_punctuation_->chinese +
                provisional_punctuation_->accumulated_text;
            commit = true;
            cancel = false;
        }
        const EditRequestResult ended = request_edit(
            active_context_, final_text, final_text.size(), commit, cancel, nullptr, false);
        if (ended != EditRequestResult::completed && composition_ != nullptr) {
            composition_->Release();
            composition_ = nullptr;
            composition_written_.clear();
        }
    }
    if (active_context_ != nullptr && pipe_client_ != nullptr) {
        const auto focus_request = mirror_.begin_request();
        (void)pipe_client_->send_focus(focus_request, false);
    }
    release_pending_contexts();
    end_candidate_ui();
    clear_deferred_updates();
    final_edit_keys_.clear();
    clear_smart_punctuation();
    release_active_context();

    active_context_ = context;
    active_context_->AddRef();
    session_id_ = next_session_id.fetch_add(1U);
    mirror_.reset_session(session_id_);
    sensitive_context_ = sensitive;
    set_english_mode(starting_english_mode());
    english_direct_ = false;
    shift_toggle_.reset();
    last_eaten_key_ = 0U;
    if (sensitive_context_) return true;
    return request_resume(context, {});
}

void TextService::release_active_context() noexcept {
    if (active_context_ == nullptr) return;
    active_context_->Release();
    active_context_ = nullptr;
    sensitive_context_ = false;
}

bool TextService::request_resume(
    ITfContext* const context,
    const HostResumeState& state) {
    if (pipe_client_ == nullptr || context == nullptr ||
        (sensitive_context_ && same_com_identity(active_context_, context))) return false;
    const auto request = mirror_.begin_request();
    context->AddRef();
    pending_contexts_.emplace(
        request.sequence, PendingContext{request.session_id, context, false, false});
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
    const MirrorRequest* const request,
    const bool smart_punctuation_completion) {
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
        this, context, wide, wide_caret, commit, cancel, nullptr, true, request,
        smart_punctuation_completion,
        smart_punctuation_completion ? session_id_ : 0U);
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
    const HostAction pending_action = mirror_.pending_action();
    const std::string pending_target = mirror_.pending_commit();
    const auto recovery = mirror_.complete_edit(SUCCEEDED(result));
    complete_candidate_action(pending_action, pending_target, SUCCEEDED(result));
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

void TextService::complete_smart_punctuation_edit(
    ITfContext* const context,
    const HRESULT result,
    const bool commit,
    const bool cancel,
    const std::uint64_t smart_session_id) noexcept {
    if (!is_current_smart_punctuation(context, smart_session_id)) return;
    trace_key("smart_edit_ran", commit ? "commit" : (cancel ? "cancel" : "update"));
    if (!commit && !cancel) {
        if (FAILED(result)) clear_smart_punctuation();
        return;
    }
    if (FAILED(result)) {
        smart_replay_event_.reset();
        smart_replay_virtual_key_ = 0U;
        return;
    }

    auto event = std::exchange(smart_replay_event_, std::nullopt);
    const WPARAM virtual_key = std::exchange(smart_replay_virtual_key_, 0U);
    provisional_punctuation_.reset();
    if (event.has_value() && context != nullptr) {
        (void)dispatch(context, std::move(*event));
    }
    if (virtual_key != 0U) replay_virtual_key(virtual_key);
}

bool TextService::is_current_smart_punctuation(
    ITfContext* const context,
    const std::uint64_t smart_session_id) const noexcept {
    return provisional_punctuation_.has_value() &&
        smart_session_id != 0U && smart_session_id == session_id_ &&
        same_com_identity(active_context_, context);
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
    // Same two sources the candidate window uses, in the same order: the
    // system caret where the application keeps one, and the position
    // GetTextExt reported otherwise. Consulting only the first left the
    // indicator with nothing in Chromium and everything built on it, so it
    // centred on the window -- in ChatGPT and Codex it landed in the middle of
    // the screen while the candidate row sat correctly at the caret.
    auto caret = system_caret_rect();
    if (!caret.has_value()) caret = last_text_caret_;
    mode_indicator_.show(mode_mark_for(english_mode_, caps_lock_is_on()), caret);
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

void TextService::launch_symbol_tool() noexcept {
    const auto configured = settings_value("symbol_tool");
    std::filesystem::path tool = configured.empty()
        ? (transport_ != nullptr
                ? transport_->resolve_program_path(L"yesymbol.exe")
                : sibling_program(module_, L"yesymbol.exe"))
        : std::filesystem::path(utf8_to_wide_local(configured));
    if (std::filesystem::is_regular_file(tool)) {
        (void)ShellExecuteW(nullptr, L"open", tool.c_str(), nullptr,
            tool.parent_path().c_str(), SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(nullptr,
        L"找不到符号工具 yesymbol.exe。\n\n"
        L"它随 PiInput 一同安装，正常在程序目录下。也可以在设置窗口的"
        L"「标点符号」页指定其他路径。",
        L"PiInput 符号", MB_OK | MB_ICONINFORMATION);
}

void TextService::launch_settings() noexcept {
    const auto settings = transport_ != nullptr
        ? transport_->resolve_program_path(L"PiInput-Settings.exe")
        : sibling_program(module_, L"PiInput-Settings.exe");
    if (std::filesystem::is_regular_file(settings)) {
        (void)ShellExecuteW(nullptr, L"open", settings.c_str(), nullptr,
            settings.parent_path().c_str(), SW_SHOWNORMAL);
    }
}

void TextService::launch_program(const std::string_view target) noexcept {
    if (target == "system:calculator") {
        (void)ShellExecuteW(nullptr, L"open", L"calc.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (target == "system:mspaint") {
        (void)ShellExecuteW(nullptr, L"open", L"mspaint.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (target == "system:everything") {
        const std::array<std::wstring, 3U> candidates{
            L"%ProgramFiles%\\Everything\\Everything.exe",
            L"%ProgramFiles(x86)%\\Everything\\Everything.exe",
            L"%LOCALAPPDATA%\\Everything\\Everything.exe",
        };
        for (const auto& candidate : candidates) {
            std::wstring expanded(32768U, L'\0');
            const DWORD length = ExpandEnvironmentStringsW(
                candidate.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
            if (length == 0U || length > expanded.size()) continue;
            expanded.resize(length - 1U);
            if (!std::filesystem::is_regular_file(expanded)) continue;
            (void)ShellExecuteW(nullptr, L"open", expanded.c_str(), nullptr,
                std::filesystem::path(expanded).parent_path().c_str(), SW_SHOWNORMAL);
            return;
        }
        (void)ShellExecuteW(nullptr, L"open", L"Everything.exe", nullptr,
            nullptr, SW_SHOWNORMAL);
        return;
    }
    if (target == "package:regcalc64") {
        const auto page = transport_ != nullptr
            ? transport_->resolve_program_path(L"RegCalc64Tool.html")
            : sibling_program(module_, L"RegCalc64Tool.html");
        if (std::filesystem::is_regular_file(page)) {
            (void)ShellExecuteW(nullptr, L"open", page.c_str(), nullptr,
                page.parent_path().c_str(), SW_SHOWNORMAL);
        }
        return;
    }
    constexpr std::string_view custom_prefix = "custom:";
    if (!target.starts_with(custom_prefix)) return;
    std::wstring configured = utf8_to_wide_local(
        std::string(target.substr(custom_prefix.size())));
    if (configured.empty()) return;
    std::wstring expanded(32768U, L'\0');
    const DWORD length = ExpandEnvironmentStringsW(
        configured.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
    if (length > 0U && length <= expanded.size()) {
        expanded.resize(length - 1U);
        configured = std::move(expanded);
    }
    constexpr std::wstring_view command_prefix = L"cmd:";
    if (configured.starts_with(command_prefix)) {
        const std::wstring command(configured.substr(command_prefix.size()));
        if (command.empty()) return;
        const std::wstring arguments = L"/d /s /c \"" + command + L"\"";
        (void)ShellExecuteW(nullptr, L"open", L"cmd.exe", arguments.c_str(),
            nullptr, SW_SHOWNORMAL);
        return;
    }
    constexpr std::wstring_view shell_prefix = L"shell:";
    if (configured.starts_with(shell_prefix)) {
        configured.erase(0U, shell_prefix.size());
        const auto separator = configured.find(L'|');
        const std::wstring program = configured.substr(0U, separator);
        const std::wstring arguments = separator == std::wstring::npos
            ? std::wstring{} : configured.substr(separator + 1U);
        if (!program.empty()) {
            (void)ShellExecuteW(nullptr, L"open", program.c_str(),
                arguments.empty() ? nullptr : arguments.c_str(), nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    (void)ShellExecuteW(nullptr, L"open", configured.c_str(), nullptr,
        nullptr, SW_SHOWNORMAL);
}

void TextService::complete_candidate_action(
    const HostAction action,
    const std::string_view target,
    const bool edit_succeeded) noexcept {
    if (!edit_succeeded) return;
    if (action == HostAction::launch_symbol_tool) {
        launch_symbol_tool();
    } else if (action == HostAction::launch_settings) {
        launch_settings();
    } else if (action == HostAction::launch_program) {
        launch_program(target);
    }
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
        launch_symbol_tool();
        return;
    }
    case LangBarCommand::settings: {
        launch_settings();
        return;
    }
    case LangBarCommand::about: {
        const auto widen_ascii = [](const char* const value) {
            const std::string text(value);
            return std::wstring(text.begin(), text.end());
        };
        const std::wstring about =
            L"PiInput " + widen_ascii(PIINPUT_VERSION) + L"\n\n"
            L"构建标识：" + widen_ascii(PIINPUT_BUILD_ID) + L"\n"
            L"构建时间：" + widen_ascii(PIINPUT_BUILD_TIME_UTC) + L"\n"
            L"Git Commit：" + widen_ascii(PIINPUT_GIT_COMMIT_ID) + L"\n"
            L"联系方式：" + widen_ascii(PIINPUT_CONTACT) + L"\n\n"
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
    end_candidate_ui();
    mirror_.discard_composition();
    clear_deferred_updates();
    final_edit_keys_.clear();
    clear_smart_punctuation();
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

bool TextService::update_candidate_ui(
    ITfContext* const context,
    const HostSnapshot& snapshot) noexcept {
    if (context == nullptr || snapshot.raw.empty() || snapshot.candidates.empty()) {
        end_candidate_ui();
        return true;
    }
    if (ui_element_manager_ == nullptr && thread_manager_ != nullptr) {
        (void)thread_manager_->QueryInterface(IID_PPV_ARGS(&ui_element_manager_));
    }
    if (ui_element_manager_ == nullptr) {
        show_custom_candidate_ui_ = true;
        return true;
    }
    if (candidate_ui_ == nullptr) {
        ITfDocumentMgr* document_manager = nullptr;
        (void)context->GetDocumentMgr(&document_manager);
        candidate_ui_ = new (std::nothrow) CandidateUiElement(
            document_manager,
            [this](const std::uint64_t candidate_id) {
                select_candidate_from_host_ui(candidate_id);
            },
            [this] { cancel_from_host_ui(); });
        if (document_manager != nullptr) document_manager->Release();
        if (candidate_ui_ == nullptr) {
            show_custom_candidate_ui_ = true;
            return true;
        }
        BOOL show = TRUE;
        DWORD id = static_cast<DWORD>(-1);
        const HRESULT begun = ui_element_manager_->BeginUIElement(
            candidate_ui_, &show, &id);
        trace_candidate_ui("BeginUIElement.hr", static_cast<long>(begun));
        trace_candidate_ui("BeginUIElement.show", show != FALSE ? 1 : 0);
        if (FAILED(begun)) {
            candidate_ui_->Release();
            candidate_ui_ = nullptr;
            show_custom_candidate_ui_ = true;
            return true;
        }
        candidate_ui_id_ = id;
        host_requested_hidden_popup_ = show == FALSE;
        host_confirmed_rendering_ = false;
        // BeginUIElement only registers the object with the sink.  In
        // particular, Windows Search's integrated candidate surface waits for
        // the first UpdateUIElement notification before it queries strings and
        // paints a row.  Publish the initial snapshot after Begin, matching the
        // lifecycle used by Microsoft's SampleIME; otherwise the system asks
        // us to hide our popup but never renders the replacement UI.
        candidate_ui_->update(snapshot);
        (void)ui_element_manager_->UpdateUIElement(candidate_ui_id_);
        show_custom_candidate_ui_ = resolve_candidate_ui_owner();
        return show_custom_candidate_ui_;
    }
    candidate_ui_->update(snapshot);
    (void)ui_element_manager_->UpdateUIElement(candidate_ui_id_);
    show_custom_candidate_ui_ = resolve_candidate_ui_owner();
    return show_custom_candidate_ui_;
}

// Whether the Host should still draw the external candidate window.
//
// Taking BeginUIElement's answer at face value is what leaves Windows Search
// with no candidates on screen at all: it asks for the popup to be withheld,
// and then never renders the integrated row it promised. Composition, decoding
// and commit all keep working there, so the input looks alive while the
// candidates are invisible -- pinyin shows up and Space still commits Chinese,
// which is exactly why this failed every attempt to verify it by typing.
//
// UpdateUIElement dispatches to ITfUIElementSink synchronously, so by the time
// it returns, a host that intends to paint has already pulled the strings out
// of the element. One that has not touched the list is not rendering anything,
// and the external window has to go back up.
bool TextService::resolve_candidate_ui_owner() noexcept {
    if (!host_requested_hidden_popup_) {
        trace_candidate_ui("owner.popup_not_withheld", 1);
        return true;
    }
    if (candidate_ui_ != nullptr && candidate_ui_->host_took_over()) {
        host_confirmed_rendering_ = true;
    }
    const bool draw_external = !host_confirmed_rendering_;
    trace_candidate_ui("owner.draw_external", draw_external ? 1 : 0);
    return draw_external;
}

void TextService::end_candidate_ui() noexcept {
    if (ui_element_manager_ != nullptr &&
        candidate_ui_id_ != static_cast<DWORD>(-1)) {
        (void)ui_element_manager_->EndUIElement(candidate_ui_id_);
    }
    candidate_ui_id_ = static_cast<DWORD>(-1);
    if (candidate_ui_ != nullptr) {
        candidate_ui_->Release();
        candidate_ui_ = nullptr;
    }
    if (ui_element_manager_ != nullptr) {
        ui_element_manager_->Release();
        ui_element_manager_ = nullptr;
    }
    show_custom_candidate_ui_ = true;
    // The next context gets its own answer from its own BeginUIElement. A
    // host that rendered for this one says nothing about the next.
    host_requested_hidden_popup_ = false;
    host_confirmed_rendering_ = false;
}

void TextService::capture_composition_caret(
    ITfContext* const context,
    const TfEditCookie edit_cookie,
    HostCaretUpdate& update) const noexcept {
    update.has_text_caret = false;
    update.owner_window = 0U;
    update.show_candidate_window = show_custom_candidate_ui_;
    if (context == nullptr) return;
    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || view == nullptr) {
        const HWND focused = GetFocus();
        const HWND root = focused == nullptr ? nullptr : GetAncestor(focused, GA_ROOT);
        update.owner_window = reinterpret_cast<std::uintptr_t>(
            root != nullptr ? root : focused);
        return;
    }

    HWND view_window = nullptr;
    const bool has_reported_view_window =
        SUCCEEDED(view->GetWnd(&view_window)) && view_window != nullptr;
    if (!has_reported_view_window) view_window = GetFocus();
    const bool has_view_window = view_window != nullptr;
    // The top-level window, which is the one the caret has to be inside. The
    // view window is whatever the text store chose to name and can be an
    // internal child with no useful geometry -- MobaXterm reports one whose
    // rectangle is a single point, so testing a caret against it rejected
    // every position including the correct ones.
    HWND owner_root = nullptr;
    if (has_view_window) {
        const HWND root = GetAncestor(view_window, GA_ROOT);
        owner_root = root != nullptr ? root : view_window;
        update.owner_window = reinterpret_cast<std::uintptr_t>(owner_root);
    }

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
        // A caret has to be inside the window whose text it belongs to.
        //
        // GetTextExt is allowed to fail, and code here handles that. What it is
        // not prepared for is an application that answers with a number that
        // was never a caret: MobaXterm returns a fixed point at the primary
        // screen's bottom-right corner, the same 1919,1019 for every keystroke,
        // while its own window is on another monitor. Nothing about the shape
        // of that rectangle is wrong -- it is caret-sized and caret-shaped --
        // so the existing plausibility test passed it and the candidates went
        // to the corner of the wrong screen.
        //
        // The first keystroke after a window opens looked right because
        // MobaXterm does keep a system caret briefly; once that goes, this took
        // over. That is exactly the reported pattern: right once, wrong after
        // committing, right again in a new session.
        const bool inside_owner = rect_is_within_window(rect, owner_root);
        if (!inside_owner && last_text_caret_.has_value()) {
            // The remembered position is where the caret last genuinely was.
            // In a terminal that is the same line, which is close enough to be
            // useful and far better than another monitor.
            rect = *last_text_caret_;
        }
        update.has_text_caret = true;
        update.left = rect.left;
        update.top = rect.top;
        update.right = rect.right;
        update.bottom = rect.bottom;
        // Which of the two sources answered, and where. Without this a trace
        // shows the keys arriving and the edits landing but says nothing about
        // why the candidate window went where it did.
        {
            RECT owner_bounds{};
            if (owner_root == nullptr || GetWindowRect(owner_root, &owner_bounds) == FALSE) {
                owner_bounds = RECT{};
            }
            // All four edges of both rectangles. Printing only the top-left
            // corner made an "-outside" verdict unreadable: the corner sat
            // inside the window and the edge that actually failed the test was
            // the one not shown.
            char detail[128]{};
            (void)std::snprintf(detail, sizeof(detail),
                "%s%s %ld.%ld-%ld.%ld in %ld.%ld-%ld.%ld",
                caret.has_value() ? "syscaret" : "gettextext",
                inside_owner ? "" : "-outside",
                static_cast<long>(rect.left), static_cast<long>(rect.top),
                static_cast<long>(rect.right), static_cast<long>(rect.bottom),
                static_cast<long>(owner_bounds.left), static_cast<long>(owner_bounds.top),
                static_cast<long>(owner_bounds.right), static_cast<long>(owner_bounds.bottom));
            trace_key("caret", detail);
        }
        // Kept for the mode indicator, which has no document lock of its own
        // and so cannot ask GetTextExt when it needs a position. Without it
        // the indicator had only the system caret to go on, and Chromium and
        // everything built on it keep none -- so in ChatGPT and Codex it fell
        // back to centring on the window while the candidate row sat correctly
        // at the caret, because the candidate row comes through here.
        //
        // Only remembered when it was inside the window. Remembering a
        // rejected position would make it the fallback for the next one.
        if (inside_owner) last_text_caret_ = rect;
    } else {
        // Neither source answered. The candidate window then has to guess, and
        // knowing that it guessed is the difference between "the position is
        // wrong" and "there was no position to have".
        trace_key("caret", "none");
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
    if (message == host_query_client_identity_message && service != nullptr) {
        const auto requested = static_cast<std::uint64_t>(wparam & 0xFFFFFFFFULL) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lparam)) << 32U);
        return requested == process_client_id() ? 1L : 0L;
    }
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0L);
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace piinput::windows
