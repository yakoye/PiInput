#pragma once

#include "candidate_window.h"
#include "liteime/engine.h"
#include "liteime/session.h"
#include "liteime/symbols.h"
#include "liteime_tsf_guids.h"

#include <msctf.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace liteime::windows {

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
        const std::wstring& text, bool commit, bool cancel);

private:
    ~TextService();

    bool should_eat_key(WPARAM wparam) const;
    void handle_key(ITfContext* context, WPARAM wparam);
    void refresh_candidate_window();
    void request_update(ITfContext* context);
    void request_commit(ITfContext* context, const std::string& text);
    void request_cancel(ITfContext* context);
    bool choose_candidate(ITfContext* context, std::size_t index);
    void commit_raw_input(ITfContext* context);
    void move_selection(int delta);
    void move_page(int delta);
    void load_engine();
    void save_user_model() noexcept;
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
    WPARAM last_eaten_key_{};

    Engine engine_;
    SymbolIndex symbols_;
    std::unique_ptr<ImeSession> session_;
    std::filesystem::path user_model_path_;
    std::string schema_{"full"};
    std::size_t selected_index_{};
    std::size_t page_start_{};
    std::vector<std::string> symbol_candidates_;
    bool symbol_mode_{};
    CandidateWindow candidate_window_;
};

extern std::atomic<long> g_object_count;
extern HINSTANCE g_module_instance;

}  // namespace liteime::windows
