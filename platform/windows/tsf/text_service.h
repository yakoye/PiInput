#pragma once

#include "candidate_window.h"
#include "piinput/candidate_grid.h"
#include "piinput/engine.h"
#include "piinput/english_lexicon.h"
#include "piinput/english_key_policy.h"
#include "piinput/english_session.h"
#include "piinput/input_mode.h"
#include "piinput/punctuation.h"
#include "piinput/session.h"
#include "piinput/settings_manager.h"
#include "piinput/symbols.h"
#include "piinput_tsf_guids.h"

#include <msctf.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace piinput::windows {

class TextService final : public ITfTextInputProcessor, public ITfKeyEventSink, public ITfCompositionSink {
public:
    explicit TextService(HINSTANCE module);
    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* thread_manager, TfClientId client_id) override;
    STDMETHODIMP Deactivate() override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie edit_cookie, ITfComposition* composition) override;

    HRESULT apply_composition_edit(ITfContext* context, TfEditCookie edit_cookie,
        const std::wstring& text, std::size_t caret, bool commit, bool cancel);

private:
    ~TextService();

    bool should_eat_key(WPARAM wparam) const;
    void handle_key(ITfContext* context, WPARAM wparam);
    void handle_english_key(
        ITfContext* context,
        WPARAM wparam,
        const EnglishKeyDecision& decision);
    [[nodiscard]] EnglishKeyDecision english_key_decision(WPARAM wparam) const noexcept;
    void refresh_candidate_window();
    void request_update(ITfContext* context);
    void request_commit(ITfContext* context, const std::string& text);
    void request_cancel(ITfContext* context);
    bool choose_candidate(ITfContext* context, std::size_t index);
    void commit_raw_input(ITfContext* context);
    void move_row(int delta);
    void move_page(int delta);
    void apply_settings_at_composition_boundary();
    void toggle_input_mode(ITfContext* context);
    void load_engine();
    void save_user_model() noexcept;
    void save_english_learning() noexcept;
    bool ensure_english_session() noexcept;
    [[nodiscard]] bool english_composing() const noexcept;
    [[nodiscard]] CandidateSettings active_candidate_settings() const noexcept;
    std::string load_schema() const;
    std::wstring schema_display_name() const;

    std::atomic<ULONG> ref_count_{1U};
    HINSTANCE module_{};
    ITfThreadMgr* thread_manager_{};
    ITfKeystrokeMgr* keystroke_manager_{};
    ITfComposition* composition_{};
    TfClientId client_id_{TF_CLIENTID_NULL};
    bool key_sink_advised_{};
    bool foreground_{true};
    bool english_mode_{};
    WPARAM last_eaten_key_{};
    ShiftToggleState shift_toggle_;
    PunctuationTransformer punctuation_;

    Engine engine_;
    SymbolIndex symbols_;
    std::unique_ptr<ImeSession> session_;
    std::unique_ptr<EnglishLexicon> english_lexicon_;
    std::unique_ptr<EnglishSession> english_session_;
    std::filesystem::path user_model_path_;
    std::filesystem::path english_builtin_path_;
    std::filesystem::path english_downloaded_path_;
    std::filesystem::path english_user_path_;
    std::filesystem::path english_learning_path_;
    std::string schema_{"full"};
    std::vector<std::string> symbol_candidates_;
    bool symbol_mode_{};
    std::unique_ptr<SettingsManager> settings_manager_;
    SettingsPollThrottle settings_poll_throttle_;
    SettingsSnapshot settings_;
    CandidateGrid candidate_grid_;
    CandidateWindow candidate_window_;
};

extern std::atomic<long> g_object_count;
extern HINSTANCE g_module_instance;

}  // namespace piinput::windows
