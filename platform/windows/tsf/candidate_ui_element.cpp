#include "candidate_ui_element.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace piinput::windows {

// Opt-in tracing for the application-owned candidate surface, off unless a
// marker file named piinput-candidate-trace.on exists in the temp directory --
// the same gate the language bar, caret and key traces use, and for the same
// reason: the Shim runs inside other applications and inherits their
// environment, not the tester's.
//
// It answers the question no amount of watching the screen can: when an
// application asks for the popup to be withheld, does it then actually consume
// the candidate list, or does it leave the user with nothing on screen?
void trace_candidate_ui(const char* const stage, const long detail) noexcept {
    static std::FILE* file = [] () -> std::FILE* {
        char temp[MAX_PATH]{};
        if (GetTempPathA(MAX_PATH, temp) == 0U) return nullptr;
        const std::string marker = std::string(temp) + "piinput-candidate-trace.on";
        if (GetFileAttributesA(marker.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        const std::string path = std::string(temp) + "piinput-candidate.log";
        return _fsopen(path.c_str(), "a", _SH_DENYWR);
    }();
    if (file == nullptr) return;
    (void)std::fprintf(file, "%lu pid=%lu %s=%ld\n",
        GetTickCount(), GetCurrentProcessId(), stage, detail);
    (void)std::fflush(file);
}

namespace {

// {407A225A-7D4B-40DF-9E2D-3D419B02AE70}
inline constexpr GUID kPiInputCandidateUiElement =
    {0x407a225a, 0x7d4b, 0x40df, {0x9e, 0x2d, 0x3d, 0x41, 0x9b, 0x02, 0xae, 0x70}};
// The Windows SDK declares this as an external GUID in ctffunc.h, but not all
// SDK library combinations export it. Keep the documented permanent value in
// the Shim so Search integration never depends on link-order accidents.
inline constexpr GUID kSearchBoxIntegrationStyle =
    {0xe6d1bd11, 0x82f7, 0x4903, {0xae, 0x21, 0x1a, 0x63, 0x97, 0xcd, 0xe2, 0xeb}};

[[nodiscard]] std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
            result.data(), count) != count) {
        return {};
    }
    return result;
}

}  // namespace

CandidateUiElement::CandidateUiElement(
    ITfDocumentMgr* const document_manager,
    SelectHandler select_handler,
    AbortHandler abort_handler)
    : document_manager_(document_manager),
      select_handler_(std::move(select_handler)),
      abort_handler_(std::move(abort_handler)) {
    if (document_manager_ != nullptr) document_manager_->AddRef();
}

CandidateUiElement::~CandidateUiElement() {
    if (document_manager_ != nullptr) document_manager_->Release();
}

STDMETHODIMP CandidateUiElement::QueryInterface(REFIID iid, void** const object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) ||
        IsEqualIID(iid, IID_ITfUIElement) ||
        IsEqualIID(iid, IID_ITfCandidateListUIElement) ||
        IsEqualIID(iid, IID_ITfCandidateListUIElementBehavior)) {
        *object = static_cast<ITfCandidateListUIElementBehavior*>(this);
    } else if (IsEqualIID(iid, IID_ITfIntegratableCandidateListUIElement)) {
        trace_candidate_ui("QI.integratable", 1);
        *object = static_cast<ITfIntegratableCandidateListUIElement*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CandidateUiElement::AddRef() { return ++ref_count_; }

STDMETHODIMP_(ULONG) CandidateUiElement::Release() {
    const ULONG value = --ref_count_;
    if (value == 0U) delete this;
    return value;
}

STDMETHODIMP CandidateUiElement::GetDescription(BSTR* const description) {
    if (description == nullptr) return E_POINTER;
    *description = SysAllocString(L"PiInput 候选词");
    return *description != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUiElement::GetGUID(GUID* const guid) {
    if (guid == nullptr) return E_POINTER;
    *guid = kPiInputCandidateUiElement;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::Show(const BOOL show) {
    trace_candidate_ui("Show", show != FALSE ? 1 : 0);
    shown_ = show != FALSE;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::IsShown(BOOL* const shown) {
    if (shown == nullptr) return E_POINTER;
    *shown = shown_ ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetUpdatedFlags(DWORD* const flags) {
    trace_candidate_ui("GetUpdatedFlags", static_cast<long>(updated_flags_));
    if (flags == nullptr) return E_POINTER;
    *flags = updated_flags_;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetDocumentMgr(
    ITfDocumentMgr** const document_manager) {
    trace_candidate_ui("GetDocumentMgr", document_manager_ != nullptr ? 1 : 0);
    if (document_manager == nullptr) return E_POINTER;
    *document_manager = document_manager_;
    if (*document_manager == nullptr) return E_NOTIMPL;
    (*document_manager)->AddRef();
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetCount(UINT* const count) {
    trace_candidate_ui("GetCount", static_cast<long>(candidates_.size()));
    if (count == nullptr) return E_POINTER;
    *count = static_cast<UINT>(candidates_.size());
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetSelection(UINT* const index) {
    trace_candidate_ui("GetSelection", static_cast<long>(selected_));
    if (index == nullptr) return E_POINTER;
    *index = static_cast<UINT>(selected_);
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetString(
    const UINT index,
    BSTR* const text) {
    if (text == nullptr) return E_POINTER;
    *text = nullptr;
    if (index >= candidates_.size()) return E_INVALIDARG;
    // Reaching for a candidate string is the one thing an application cannot
    // skip if it means to paint the list itself. Record it: the text service
    // reads this to tell a host that really took the candidates over from one
    // that only asked for the popup to disappear.
    host_took_over_ = true;
    trace_candidate_ui("GetString", static_cast<long>(index));
    *text = SysAllocStringLen(
        candidates_[index].data(), static_cast<UINT>(candidates_[index].size()));
    return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

std::size_t CandidateUiElement::page_size() const noexcept {
    const std::size_t columns = (std::max)(items_per_row_, std::size_t{1U});
    const std::size_t rows = (std::max)(visible_rows_, std::size_t{1U});
    return columns > (std::numeric_limits<std::size_t>::max)() / rows
        ? candidates_.size()
        : columns * rows;
}

STDMETHODIMP CandidateUiElement::GetPageIndex(
    UINT* const indexes,
    const UINT size,
    UINT* const page_count) {
    if (page_count == nullptr) return E_POINTER;
    const std::size_t step = (std::max)(page_size(), std::size_t{1U});
    const std::size_t count = candidates_.empty()
        ? 0U
        : (candidates_.size() - 1U) / step + 1U;
    *page_count = static_cast<UINT>(count);
    if (indexes == nullptr || size == 0U) return S_OK;
    const std::size_t written = (std::min)(count, static_cast<std::size_t>(size));
    for (std::size_t page = 0U; page < written; ++page) {
        indexes[page] = static_cast<UINT>(page * step);
    }
    return S_OK;
}

STDMETHODIMP CandidateUiElement::SetPageIndex(
    UINT* const indexes,
    const UINT page_count) {
    if (page_count != 0U && indexes == nullptr) return E_POINTER;
    // PiInput's page boundaries are derived from the current visual settings.
    // Search hosts may repeat those boundaries here; accepting them keeps the
    // integration contract without letting an application corrupt selection.
    return S_OK;
}

STDMETHODIMP CandidateUiElement::GetCurrentPage(UINT* const page) {
    if (page == nullptr) return E_POINTER;
    const std::size_t step = (std::max)(page_size(), std::size_t{1U});
    *page = static_cast<UINT>(selected_ / step);
    return S_OK;
}

STDMETHODIMP CandidateUiElement::SetSelection(const UINT index) {
    trace_candidate_ui("SetSelection", static_cast<long>(index));
    if (index >= candidates_.size()) return E_INVALIDARG;
    selected_ = index;
    updated_flags_ = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::Finalize() {
    trace_candidate_ui("Finalize", static_cast<long>(selected_));
    if (selected_ >= candidate_ids_.size() || !select_handler_) return E_FAIL;
    select_handler_(candidate_ids_[selected_]);
    return S_OK;
}

STDMETHODIMP CandidateUiElement::Abort() {
    if (!abort_handler_) return E_FAIL;
    abort_handler_();
    return S_OK;
}

STDMETHODIMP CandidateUiElement::SetIntegrationStyle(const GUID style) {
    search_box_style_ = IsEqualGUID(style, kSearchBoxIntegrationStyle) != FALSE;
    // Declaring the search-box style is an application committing to draw the
    // candidates in its own surface, so it counts as taking them over even
    // before the first string is read.
    if (search_box_style_) host_took_over_ = true;
    trace_candidate_ui("SetIntegrationStyle", search_box_style_ ? 1 : 0);
    return search_box_style_ ? S_OK : E_NOTIMPL;
}

STDMETHODIMP CandidateUiElement::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* const style) {
    if (style == nullptr) return E_POINTER;
    trace_candidate_ui("GetSelectionStyle", 1);
    *style = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::OnKeyDown(
    const WPARAM wparam,
    const LPARAM lparam,
    BOOL* const eaten) {
    (void)wparam;
    (void)lparam;
    if (eaten == nullptr) return E_POINTER;
    // Search integration asks the candidate UI about the key after the text
    // service has already routed it through ITfKeyEventSink. Match the Windows
    // SampleIME contract and mark that integrated candidate input as handled;
    // returning FALSE lets the search box consume navigation a second time.
    trace_candidate_ui("OnKeyDown", static_cast<long>(wparam));
    *eaten = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::ShowCandidateNumbers(BOOL* const show) {
    trace_candidate_ui("ShowCandidateNumbers", 1);
    if (show == nullptr) return E_POINTER;
    *show = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUiElement::FinalizeExactCompositionString() {
    return E_NOTIMPL;
}

void CandidateUiElement::update(const HostSnapshot& snapshot) {
    candidate_ids_.clear();
    candidates_.clear();
    candidate_ids_.reserve(snapshot.candidates.size());
    candidates_.reserve(snapshot.candidates.size());
    for (const HostCandidate& candidate : snapshot.candidates) {
        candidate_ids_.push_back(candidate.id);
        candidates_.push_back(utf8_to_wide(candidate.text));
    }
    items_per_row_ = (std::max)(snapshot.view.items_per_row, std::size_t{1U});
    visible_rows_ = (std::max)(snapshot.view.visible_rows, std::size_t{1U});
    const std::size_t requested = snapshot.view.active_row * items_per_row_ +
        snapshot.view.active_column;
    selected_ = candidates_.empty()
        ? 0U
        : (std::min)(requested, candidates_.size() - 1U);
    shown_ = !candidates_.empty();
    updated_flags_ = TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING |
        TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
}

}  // namespace piinput::windows
