#pragma once

#include "piinput/host_session.h"
#include "piinput/windows_compat.h"

#include <ctffunc.h>
#include <msctf.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace piinput::windows {

// Opt-in tracing for the application-owned candidate surface, off unless
// %TEMP%\piinput-candidate-trace.on exists. Records whether an application
// that asked for the popup to be withheld actually consumes the candidates.
void trace_candidate_ui(const char* stage, long detail) noexcept;

// TSF's application-owned candidate surface. SearchHost and other immersive
// text controls can ask the text service not to draw a separate popup and
// consume this object instead. Keeping it in the in-process Shim is essential:
// the out-of-process Host cannot publish an ITfUIElement into the application's
// thread manager.
class CandidateUiElement final :
    public ITfCandidateListUIElementBehavior,
    public ITfIntegratableCandidateListUIElement {
public:
    using SelectHandler = std::function<void(std::uint64_t)>;
    using AbortHandler = std::function<void()>;

    CandidateUiElement(
        ITfDocumentMgr* document_manager,
        SelectHandler select_handler,
        AbortHandler abort_handler);
    CandidateUiElement(const CandidateUiElement&) = delete;
    CandidateUiElement& operator=(const CandidateUiElement&) = delete;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* description) override;
    STDMETHODIMP GetGUID(GUID* guid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* shown) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document_manager) override;
    STDMETHODIMP GetCount(UINT* count) override;
    STDMETHODIMP GetSelection(UINT* index) override;
    STDMETHODIMP GetString(UINT index, BSTR* text) override;
    STDMETHODIMP GetPageIndex(UINT* indexes, UINT size, UINT* page_count) override;
    STDMETHODIMP SetPageIndex(UINT* indexes, UINT page_count) override;
    STDMETHODIMP GetCurrentPage(UINT* page) override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;

    // ITfIntegratableCandidateListUIElement
    STDMETHODIMP SetIntegrationStyle(GUID style) override;
    STDMETHODIMP GetSelectionStyle(
        TfIntegratableCandidateListSelectionStyle* style) override;
    STDMETHODIMP OnKeyDown(WPARAM wparam, LPARAM lparam, BOOL* eaten) override;
    STDMETHODIMP ShowCandidateNumbers(BOOL* show) override;
    STDMETHODIMP FinalizeExactCompositionString() override;

    void update(const HostSnapshot& snapshot);

    // An application that answers BeginUIElement with "do not draw your own
    // popup" is trusted only as far as it actually reads this list. Anything
    // that genuinely renders the candidates has to pull the strings out of
    // here, or declare the search-box integration style, before it can paint a
    // row. Windows Search asks for the popup to be withheld and then does
    // neither, which leaves the user with no candidate UI at all, so the text
    // service watches this flag and puts its own window back.
    [[nodiscard]] bool host_took_over() const noexcept { return host_took_over_; }

private:
    ~CandidateUiElement();
    [[nodiscard]] std::size_t page_size() const noexcept;

    std::atomic<ULONG> ref_count_{1U};
    ITfDocumentMgr* document_manager_{};
    SelectHandler select_handler_;
    AbortHandler abort_handler_;
    std::vector<std::uint64_t> candidate_ids_;
    std::vector<std::wstring> candidates_;
    std::size_t selected_{};
    std::size_t items_per_row_{1U};
    std::size_t visible_rows_{1U};
    DWORD updated_flags_{TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT |
        TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX |
        TF_CLUIE_CURRENTPAGE};
    bool shown_{true};
    bool search_box_style_{};
    bool host_took_over_{};
};

}  // namespace piinput::windows
