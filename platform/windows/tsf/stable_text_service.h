#pragma once

#include "composition_mirror.h"
#include "candidate_ui_element.h"
#include "deferred_update_queue.h"
#include "final_edit_key_queue.h"
#include "pipe_client.h"
#include "shim_pipe_transport.h"

#include "piinput/input_mode.h"
#include "piinput/smart_punctuation.h"
#include "lang_bar_item.h"
#include "mode_indicator.h"

#include "piinput/windows_compat.h"

#include <msctf.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace piinput::windows {

enum class EditRequestResult {
    failed,
    completed,
    pending,
};

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink, public ITfCompositionSink {
public:
    explicit TextService(HINSTANCE module);
    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Activate(ITfThreadMgr* thread_manager, TfClientId client_id) override;
    STDMETHODIMP ActivateEx(
        ITfThreadMgr* thread_manager,
        TfClientId client_id,
        DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

    STDMETHODIMP OnCompositionTerminated(
        TfEditCookie edit_cookie,
        ITfComposition* composition) override;

    HRESULT apply_composition_edit(
        ITfContext* context,
        TfEditCookie edit_cookie,
        const std::wstring& text,
        std::size_t caret,
        bool commit,
        bool cancel);
    // Commits that arrive with no composition in flight -- direct English
    // letters and standalone punctuation -- insert straight at the selection.
    // Wrapping one character in a StartComposition/EndComposition pair makes
    // every keystroke a full IME state transition inside the host application.
    static HRESULT insert_text_at_selection(
        ITfContext* context,
        TfEditCookie edit_cookie,
        const std::wstring& text);
    [[nodiscard]] bool is_current_update(
        const MirrorRequest& request) const noexcept;
    void capture_composition_caret(
        ITfContext* context,
        TfEditCookie edit_cookie,
        HostCaretUpdate& update) const noexcept;
    void complete_deferred_edit(
        ITfContext* context,
        HRESULT result,
        bool commit,
        bool cancel,
        const MirrorRequest* request,
        const HostCaretUpdate* anchor) noexcept;
    void complete_smart_punctuation_edit(
        ITfContext* context,
        HRESULT result,
        bool commit,
        bool cancel,
        std::uint64_t smart_session_id) noexcept;
    [[nodiscard]] bool is_current_smart_punctuation(
        ITfContext* context,
        std::uint64_t smart_session_id) const noexcept;

private:
    struct PendingContext final {
        std::uint64_t session_id{};
        ITfContext* context{};
        // A Resume request only synchronizes the Host session. It must not make
        // ordinary digits/navigation look like candidate-selection input.
        bool key_request{};
        bool replayed_key{};
    };
    struct ProvisionalPunctuation final {
        char ascii{};
        std::string chinese;
        std::string rule_id;
        std::string accumulated_text;
    };
    ~TextService();

    [[nodiscard]] bool should_eat_key(WPARAM wparam) const noexcept;
    [[nodiscard]] bool has_pending_key_request() const noexcept;
    [[nodiscard]] HostKeyEvent map_key(WPARAM wparam) const noexcept;
    [[nodiscard]] bool handle_smart_punctuation_key(
        ITfContext* context,
        WPARAM wparam);
    [[nodiscard]] bool resolve_smart_punctuation_key(
        ITfContext* context,
        WPARAM wparam);
    void clear_smart_punctuation() noexcept;
    void replay_virtual_key(WPARAM wparam) noexcept;
    void toggle_input_mode(ITfContext* context);
    [[nodiscard]] bool dispatch(ITfContext* context, HostKeyEvent event);
    [[nodiscard]] bool dispatch_now(
        ITfContext* context,
        HostKeyEvent event,
        bool replayed_key);
    void dispatch_replayed_key(std::optional<HostKeyEvent> event) noexcept;
    void handle_reply(HostEnvelope envelope) noexcept;
    void release_pending_contexts() noexcept;
    [[nodiscard]] ITfContext* focused_context() const noexcept;
    [[nodiscard]] bool context_has_sensitive_input_scope(ITfContext* context) const noexcept;
    [[nodiscard]] bool query_surrounding_text(
        ITfContext* context,
        std::string& left,
        std::string& right) const noexcept;
    [[nodiscard]] bool bind_context(ITfContext* context);
    void release_active_context() noexcept;
    [[nodiscard]] bool request_resume(ITfContext* context, const HostResumeState& state);
    [[nodiscard]] EditRequestResult request_update_edit(
        ITfContext* context,
        const MirrorRequest& request,
        const std::string& text,
        std::size_t caret);
    void replay_deferred_update() noexcept;
    void clear_deferred_updates() noexcept;
    [[nodiscard]] bool create_callback_window() noexcept;
    void destroy_callback_window() noexcept;
    [[nodiscard]] EditRequestResult request_edit(
        ITfContext* context,
        const std::string& text,
        std::size_t caret,
        bool commit,
        bool cancel,
        HostCaretUpdate* anchor = nullptr,
        bool allow_async = true,
        const MirrorRequest* request = nullptr,
        bool smart_punctuation_completion = false);
    void send_candidate_anchor(
        const MirrorRequest& request,
        const HostCaretUpdate& update) noexcept;
    void send_commit_result(std::uint64_t generation, bool succeeded) noexcept;
    void cancel_from_host_ui() noexcept;
    // Keeps the input-indicator buttons showing the current state.
    // The one way to change the mode. Assigning the field directly is what let
    // the taskbar mark drift: a new context resets the mode to the configured
    // default, and without a refresh the indicator kept showing the mark from
    // whatever the last Shift press left behind.
    void set_english_mode(bool english) noexcept;
    // What settings.ini asks a fresh session to start in, so the mark and the
    // host agree from the first keystroke instead of after the first reply.
    [[nodiscard]] bool default_language_is_english() const;
    // The mode a fresh context starts in: what the user last chose, or the
    // configured default if they have not chosen yet this session.
    [[nodiscard]] bool starting_english_mode() const;
    void refresh_lang_bar() noexcept;
    // Tells Windows whether the input mode is Chinese or English. The taskbar
    // input indicator draws 中/英 from this, which is how every other input
    // method gets that mark next to its icon -- they do not draw it themselves.
    void publish_conversion_mode() noexcept;
    // Puts the mode popup up beside the caret. Called only where the sticky
    // state actually changed, never for a held Shift.
    void show_mode_popup() noexcept;
    void on_lang_bar_command(LangBarCommand command) noexcept;
    void launch_symbol_tool() noexcept;
    void launch_settings() noexcept;
    void complete_candidate_action(HostAction action, bool edit_succeeded) noexcept;
    [[nodiscard]] std::wstring schema_display_name() const;
    // A candidate the user clicked in the host's candidate window. Replayed
    // through the ordinary selection path so it commits exactly like a digit.
    void select_candidate_from_host_ui(std::uint64_t candidate_id) noexcept;
    [[nodiscard]] bool update_candidate_ui(
        ITfContext* context,
        const HostSnapshot& snapshot) noexcept;
    void end_candidate_ui() noexcept;

    static LRESULT CALLBACK callback_window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

    std::atomic<ULONG> ref_count_{1U};
    HINSTANCE module_{};
    ITfThreadMgr* thread_manager_{};
    ITfKeystrokeMgr* keystroke_manager_{};
    ITfComposition* composition_{};
    // Exactly what was last written into composition_, so the range can be
    // checked before it is overwritten.
    std::wstring composition_written_;
    ITfContext* active_context_{};
    TfClientId client_id_{TF_CLIENTID_NULL};
    DWORD activation_flags_{};
    bool key_sink_advised_{};
    bool foreground_{true};
    // Password, private and PIN scopes bypass PiInput completely: no Host
    // request, candidate UI, composition mirror or learning confirmation.
    bool sensitive_context_{};
    bool english_mode_{};
    LangBar lang_bar_;
    ModeIndicator mode_indicator_;
    HICON lang_bar_icon_{nullptr};
    // Set once the Host has echoed a plain English letter straight back as a
    // one-character commit, which only happens when English candidates are off.
    // From then on those letters are inserted inside the key handler instead of
    // making a cross-process round trip and an asynchronous hop back through
    // the application message loop for every keystroke.
    bool english_direct_{};
    ShiftToggleState shift_toggle_;
    WPARAM last_eaten_key_{};
    HWND callback_window_{};
    std::uint64_t session_id_{};
    CompositionMirror mirror_;
    DeferredUpdateQueue deferred_updates_;
    FinalEditKeyQueue final_edit_keys_;
    SmartPunctuationEngine smart_punctuation_engine_;
    std::optional<ProvisionalPunctuation> provisional_punctuation_;
    std::optional<HostKeyEvent> smart_replay_event_;
    WPARAM smart_replay_virtual_key_{};
    std::optional<DeferredCompositionUpdate> scheduled_update_;
    std::unique_ptr<ShimPipeTransport> transport_;
    std::unique_ptr<PipeClient> pipe_client_;
    std::unordered_map<std::uint64_t, PendingContext> pending_contexts_;
    ITfUIElementMgr* ui_element_manager_{};
    CandidateUiElement* candidate_ui_{};
    DWORD candidate_ui_id_{static_cast<DWORD>(-1)};
    bool show_custom_candidate_ui_{true};
};

extern std::atomic<long> g_object_count;
extern HINSTANCE g_module_instance;

}  // namespace piinput::windows
