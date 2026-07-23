#include "text_service.h"

#include "piinput/utf.h"

#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace piinput::windows {

std::atomic<long> g_object_count{0};
HINSTANCE g_module_instance = nullptr;

namespace {

class EditSession final : public ITfEditSession {
public:
    EditSession(TextService* service, ITfContext* context, std::wstring text, bool commit, bool cancel)
        : service_(service), context_(context), text_(std::move(text)), commit_(commit), cancel_(cancel) {
        service_->AddRef();
        context_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfEditSession)) {
            *object = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG value = --ref_count_;
        if (value == 0U) {
            delete this;
        }
        return value;
    }

    STDMETHODIMP DoEditSession(const TfEditCookie edit_cookie) override {
        return service_->apply_composition_edit(context_, edit_cookie, text_, commit_, cancel_);
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
    bool commit_{};
    bool cancel_{};
};

[[nodiscard]] std::filesystem::path module_directory(const HINSTANCE module) {
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

[[nodiscard]] std::filesystem::path local_app_data() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || path == nullptr) {
        throw std::runtime_error("SHGetKnownFolderPath failed");
    }
    const std::filesystem::path output(path);
    CoTaskMemFree(path);
    return output;
}

[[nodiscard]] bool has_disallowed_modifier() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
           (GetKeyState(VK_MENU) & 0x8000) != 0 ||
           (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
           (GetKeyState(VK_RWIN) & 0x8000) != 0;
}

[[nodiscard]] bool is_ascii_letter(const WPARAM key) {
    return key >= static_cast<WPARAM>('A') && key <= static_cast<WPARAM>('Z');
}

[[nodiscard]] bool is_number_key(const WPARAM key) {
    return key >= static_cast<WPARAM>('1') && key <= static_cast<WPARAM>('9');
}

[[nodiscard]] bool is_shift_key(const WPARAM key) {
    return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

[[nodiscard]] bool shift_is_down() {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

[[nodiscard]] bool is_punctuation_key(const WPARAM key) {
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

[[nodiscard]] char punctuation_base_key(const WPARAM key) {
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

}  // namespace

TextService::TextService(const HINSTANCE module)
    : module_(module),
      settings_(default_settings()),
      candidate_grid_(settings_.candidates, 0U) {
    ++g_object_count;
}

TextService::~TextService() {
    Deactivate();
    --g_object_count;
}

STDMETHODIMP TextService::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
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

STDMETHODIMP_(ULONG) TextService::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) TextService::Release() {
    const ULONG value = --ref_count_;
    if (value == 0U) {
        delete this;
    }
    return value;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* const thread_manager, const TfClientId client_id) {
    if (thread_manager == nullptr) {
        return E_INVALIDARG;
    }
    Deactivate();
    thread_manager_ = thread_manager;
    thread_manager_->AddRef();
    client_id_ = client_id;

    HRESULT result = thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager_));
    if (FAILED(result)) {
        Deactivate();
        return result;
    }
    result = keystroke_manager_->AdviseKeyEventSink(client_id_, this, TRUE);
    if (FAILED(result)) {
        Deactivate();
        return result;
    }
    key_sink_advised_ = true;

    try {
        load_engine();
    } catch (...) {
        Deactivate();
        return E_FAIL;
    }
    candidate_window_.create(module_);
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    candidate_window_.hide();
    candidate_window_.destroy();
    if (composition_ != nullptr) {
        composition_->Release();
        composition_ = nullptr;
    }
    if (key_sink_advised_ && keystroke_manager_ != nullptr && client_id_ != TF_CLIENTID_NULL) {
        keystroke_manager_->UnadviseKeyEventSink(client_id_);
    }
    key_sink_advised_ = false;
    if (keystroke_manager_ != nullptr) {
        keystroke_manager_->Release();
        keystroke_manager_ = nullptr;
    }
    if (thread_manager_ != nullptr) {
        thread_manager_->Release();
        thread_manager_ = nullptr;
    }
    session_.reset();
    settings_manager_.reset();
    client_id_ = TF_CLIENTID_NULL;
    candidate_grid_.reset(0U);
    symbol_candidates_.clear();
    symbol_mode_ = false;
    english_mode_ = false;
    shift_toggle_.reset();
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(const BOOL foreground) {
    foreground_ = foreground != FALSE;
    if (!foreground_) {
        candidate_window_.hide();
    } else {
        refresh_candidate_window();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(
    ITfContext*, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) {
        return E_POINTER;
    }
    if (is_shift_key(wparam)) {
        shift_toggle_.on_shift_down(has_disallowed_modifier());
    } else {
        shift_toggle_.on_other_key_down();
    }
    *eaten = should_eat_key(wparam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) {
        return E_POINTER;
    }
    if (is_shift_key(wparam)) {
        shift_toggle_.on_shift_down(has_disallowed_modifier());
    } else {
        shift_toggle_.on_other_key_down();
    }
    const bool consume = should_eat_key(wparam);
    *eaten = consume ? TRUE : FALSE;
    if (consume) {
        last_eaten_key_ = wparam;
        if (!is_shift_key(wparam)) {
            handle_key(context, wparam);
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(
    ITfContext*, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) {
        return E_POINTER;
    }
    *eaten = (is_shift_key(wparam) && !has_disallowed_modifier()) || wparam == last_eaten_key_ ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(
    ITfContext* const context, const WPARAM wparam, LPARAM, BOOL* const eaten) {
    if (eaten == nullptr) {
        return E_POINTER;
    }
    *eaten = (is_shift_key(wparam) || wparam == last_eaten_key_) ? TRUE : FALSE;
    if (is_shift_key(wparam) && shift_toggle_.on_shift_up()) {
        toggle_input_mode(context);
    }
    if (wparam == last_eaten_key_) {
        last_eaten_key_ = 0U;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* const eaten) {
    if (eaten == nullptr) {
        return E_POINTER;
    }
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* const composition) {
    if (composition_ != nullptr && (composition == nullptr || composition == composition_)) {
        composition_->Release();
        composition_ = nullptr;
    }
    if (session_ != nullptr) {
        session_->clear();
    }
    candidate_grid_.reset(0U);
    apply_settings_at_composition_boundary();
    symbol_candidates_.clear();
    symbol_mode_ = false;
    candidate_window_.hide();
    return S_OK;
}

bool TextService::should_eat_key(const WPARAM wparam) const {
    if (session_ == nullptr || has_disallowed_modifier()) {
        return false;
    }
    if (is_shift_key(wparam)) {
        return true;
    }
    if (english_mode_) {
        return false;
    }
    const bool composing = !session_->snapshot().input.empty();
    const bool shifted = shift_is_down();
    if (is_punctuation_key(wparam)) {
        if (composing && !shifted &&
            (wparam == VK_OEM_MINUS || wparam == VK_OEM_PLUS || wparam == VK_OEM_7)) {
            return true;
        }
        return true;
    }
    if (!composing) {
        if (shifted) {
            return false;
        }
        return is_ascii_letter(wparam);
    }
    if (is_ascii_letter(wparam) || is_number_key(wparam)) {
        return true;
    }
    switch (wparam) {
    case VK_OEM_7:
    case VK_BACK:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
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

void TextService::handle_key(ITfContext* const context, const WPARAM wparam) {
    if (session_ == nullptr || context == nullptr) {
        return;
    }
    if (is_ascii_letter(wparam)) {
        if (session_->snapshot().input.empty()) {
            apply_settings_at_composition_boundary();
        }
        session_->insert(static_cast<char>(std::tolower(static_cast<unsigned char>(wparam))));
        candidate_grid_.reset(session_->snapshot().candidates.size());
        request_update(context);
        refresh_candidate_window();
        return;
    }
    const bool shifted = shift_is_down();
    const bool composing = !session_->snapshot().input.empty();
    if (wparam == VK_OEM_7 && composing && !shifted) {
        session_->insert('\'');
        candidate_grid_.reset(session_->snapshot().candidates.size());
        request_update(context);
        refresh_candidate_window();
        return;
    }
    if (is_punctuation_key(wparam) &&
        !(composing && !shifted && (wparam == VK_OEM_MINUS || wparam == VK_OEM_PLUS))) {
        const char base_key = punctuation_base_key(wparam);
        if (base_key == '\0') {
            return;
        }
        const std::string punctuation = punctuation_.transform(
            base_key, PunctuationMode::chinese, shifted);
        if (composing) {
            if (!choose_candidate(context, candidate_grid_.selected_index())) {
                commit_raw_input(context);
            }
        }
        request_commit(context, punctuation);
        return;
    }
    if (wparam == VK_BACK) {
        session_->backspace();
        candidate_grid_.reset(session_->snapshot().candidates.size());
        if (session_->snapshot().input.empty()) {
            request_cancel(context);
        } else {
            request_update(context);
        }
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_DELETE) {
        session_->delete_forward();
        candidate_grid_.reset(session_->snapshot().candidates.size());
        if (session_->snapshot().input.empty()) {
            request_cancel(context);
        } else {
            request_update(context);
        }
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_LEFT) {
        session_->move_left();
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_RIGHT) {
        session_->move_right();
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_HOME) {
        session_->move_home();
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_END) {
        session_->move_end();
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_UP) {
        move_row(-1);
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_DOWN) {
        move_row(1);
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_PRIOR) {
        move_page(-1);
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_NEXT) {
        move_page(1);
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_OEM_MINUS) {
        move_row(-1);
        refresh_candidate_window();
        return;
    }
    if (wparam == VK_OEM_PLUS) {
        move_row(1);
        refresh_candidate_window();
        return;
    }
    if (is_number_key(wparam)) {
        const std::size_t digit = static_cast<std::size_t>(
            wparam - static_cast<WPARAM>('0'));
        const std::size_t index = candidate_grid_.candidate_index_for_digit(digit);
        if (index != CandidateGrid::invalid_index) {
            choose_candidate(context, index);
        }
        return;
    }
    if (wparam == VK_SPACE) {
        if (!choose_candidate(context, candidate_grid_.selected_index())) {
            commit_raw_input(context);
        }
        return;
    }
    if (wparam == VK_RETURN) {
        commit_raw_input(context);
        return;
    }
    if (wparam == VK_ESCAPE) {
        session_->clear();
        candidate_grid_.reset(0U);
        request_cancel(context);
        refresh_candidate_window();
    }
}

void TextService::request_update(ITfContext* const context) {
    if (session_ == nullptr) {
        return;
    }
    const std::wstring text = utf8_to_wide(session_->snapshot().input);
    auto* edit_session = new EditSession(this, context, text, false, false);
    HRESULT session_result = E_FAIL;
    context->RequestEditSession(client_id_, edit_session,
        TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    edit_session->Release();
}

void TextService::request_commit(ITfContext* const context, const std::string& text) {
    candidate_grid_.reset(0U);
    symbol_candidates_.clear();
    symbol_mode_ = false;
    apply_settings_at_composition_boundary();
    auto* edit_session = new EditSession(this, context, utf8_to_wide(text), true, false);
    HRESULT session_result = E_FAIL;
    context->RequestEditSession(client_id_, edit_session,
        TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    edit_session->Release();
    candidate_window_.hide();
}

void TextService::request_cancel(ITfContext* const context) {
    candidate_grid_.reset(0U);
    symbol_candidates_.clear();
    symbol_mode_ = false;
    apply_settings_at_composition_boundary();
    auto* edit_session = new EditSession(this, context, L"", false, true);
    HRESULT session_result = E_FAIL;
    context->RequestEditSession(client_id_, edit_session,
        TF_ES_SYNC | TF_ES_READWRITE, &session_result);
    edit_session->Release();
    candidate_window_.hide();
}

HRESULT TextService::apply_composition_edit(
    ITfContext* const context,
    const TfEditCookie edit_cookie,
    const std::wstring& text,
    const bool commit,
    const bool cancel) {
    if (context == nullptr) {
        return E_INVALIDARG;
    }

    if (composition_ == nullptr && !cancel) {
        TF_SELECTION selection{};
        ULONG fetched = 0U;
        HRESULT result = context->GetSelection(edit_cookie, TF_DEFAULT_SELECTION, 1U, &selection, &fetched);
        if (FAILED(result) || fetched == 0U || selection.range == nullptr) {
            return FAILED(result) ? result : E_FAIL;
        }
        selection.range->Collapse(edit_cookie, TF_ANCHOR_END);
        ITfContextComposition* composition_context = nullptr;
        result = context->QueryInterface(IID_PPV_ARGS(&composition_context));
        if (SUCCEEDED(result)) {
            result = composition_context->StartComposition(
                edit_cookie, selection.range, this, &composition_);
            composition_context->Release();
        }
        selection.range->Release();
        if (FAILED(result) || composition_ == nullptr) {
            return FAILED(result) ? result : E_FAIL;
        }
    }

    if (composition_ == nullptr) {
        return S_OK;
    }

    ITfRange* range = nullptr;
    HRESULT result = composition_->GetRange(&range);
    if (FAILED(result) || range == nullptr) {
        return FAILED(result) ? result : E_FAIL;
    }
    const wchar_t* data = text.empty() ? L"" : text.c_str();
    result = range->SetText(edit_cookie, 0U, data, static_cast<LONG>(text.size()));
    if (SUCCEEDED(result)) {
        TF_SELECTION selection{};
        selection.range = range;
        selection.style.ase = TF_AE_END;
        selection.style.fInterimChar = FALSE;
        range->Collapse(edit_cookie, TF_ANCHOR_END);
        result = context->SetSelection(edit_cookie, 1U, &selection);
    }

    if (SUCCEEDED(result) && (commit || cancel)) {
        ITfComposition* ending = composition_;
        composition_ = nullptr;
        result = ending->EndComposition(edit_cookie);
        ending->Release();
    }
    range->Release();
    return result;
}

bool TextService::choose_candidate(ITfContext* const context, const std::size_t index) {
    if (session_ == nullptr) {
        return false;
    }
    if (symbol_mode_) {
        if (index >= symbol_candidates_.size()) {
            return false;
        }
        const std::string text = symbol_candidates_[index];
        session_->clear();
        symbol_candidates_.clear();
        symbol_mode_ = false;
        candidate_grid_.reset(0U);
        request_commit(context, text);
        return true;
    }
    const auto& candidates = session_->snapshot().candidates;
    if (index >= candidates.size()) {
        return false;
    }
    const auto id = candidates[index].id;
    const auto chosen = session_->choose(id);
    if (!chosen.has_value()) {
        return false;
    }
    candidate_grid_.reset(0U);
    save_user_model();
    request_commit(context, *chosen);
    return true;
}

void TextService::commit_raw_input(ITfContext* const context) {
    if (session_ == nullptr || session_->snapshot().input.empty()) {
        return;
    }
    const std::string raw = session_->snapshot().input;
    session_->clear();
    symbol_candidates_.clear();
    symbol_mode_ = false;
    candidate_grid_.reset(0U);
    request_commit(context, raw);
}

void TextService::move_row(const int delta) {
    if (session_ == nullptr) {
        return;
    }
    const std::size_t count = symbol_mode_ ? symbol_candidates_.size() : session_->snapshot().candidates.size();
    candidate_grid_.set_candidate_count(count);
    candidate_grid_.move_row(delta);
}

void TextService::move_page(const int delta) {
    if (session_ == nullptr) {
        return;
    }
    const std::size_t count = symbol_mode_ ? symbol_candidates_.size() : session_->snapshot().candidates.size();
    candidate_grid_.set_candidate_count(count);
    candidate_grid_.move_page(delta);
}

void TextService::refresh_candidate_window() {
    if (session_ == nullptr || !foreground_ || session_->snapshot().input.empty()) {
        if (session_ == nullptr || session_->snapshot().input.empty()) {
            candidate_grid_.reset(0U);
        }
        candidate_window_.hide();
        return;
    }
    std::vector<std::wstring> display;
    const std::string& input = session_->snapshot().input;
    symbol_mode_ = !input.empty() && input.front() == ';';
    symbol_candidates_.clear();
    if (symbol_mode_) {
        const auto results = symbols_.search(
            input.substr(1U), static_cast<std::size_t>(settings_.candidates.max_items));
        for (const auto& result : results) {
            symbol_candidates_.push_back(result.symbol);
            display.push_back(utf8_to_wide(result.symbol + "  " + result.name));
        }
    } else {
        for (const auto& item : session_->snapshot().candidates) {
            display.push_back(utf8_to_wide(item.candidate.word));
        }
    }
    candidate_grid_.set_candidate_count(display.size());
    candidate_window_.update(
        schema_display_name(),
        utf8_to_wide(input),
        display,
        candidate_grid_.selected_index(),
        candidate_grid_.active_row(),
        candidate_grid_.first_visible_row(),
        candidate_grid_.items_per_row(),
        candidate_grid_.visible_rows());
    candidate_window_.show_near_caret();
}

void TextService::load_engine() {
    const auto data_root = local_app_data() / L"PiInput" / L"UserData";
    const auto lexicon_directory = data_root / L"lexicons";
    const auto combined = lexicon_directory / L"piinput-imported.lex";
    const auto base_binary = lexicon_directory / L"piinput-base.lex";
    const auto installed_data = module_directory(module_).parent_path() / L"data";
    const auto base_tsv = installed_data / L"base_lexicon.tsv";

    if (std::filesystem::exists(combined)) {
        engine_.load_lexicon(combined);
    } else if (std::filesystem::exists(base_binary)) {
        engine_.load_lexicon(base_binary);
    } else {
        engine_.load_lexicon(base_tsv);
    }
    user_model_path_ = data_root / L"user_model.tsv";
    engine_.load_user_model(user_model_path_);
    symbols_.load_tsv(installed_data / L"symbols.tsv");
    schema_ = load_schema();
    settings_manager_ = std::make_unique<SettingsManager>(data_root / L"settings.ini");
    apply_settings_at_composition_boundary();
}

void TextService::apply_settings_at_composition_boundary() {
    if (settings_manager_ == nullptr ||
        (session_ != nullptr && !session_->snapshot().input.empty())) {
        return;
    }

    settings_manager_->poll();
    settings_manager_->apply_pending_at_composition_boundary();
    const auto next = settings_manager_->current();
    if (next == nullptr) {
        return;
    }

    const bool candidates_changed = next->candidates != settings_.candidates;
    settings_ = *next;
    if (candidates_changed) {
        candidate_grid_ = CandidateGrid(settings_.candidates, 0U);
    } else {
        candidate_grid_.reset(0U);
    }
    if (session_ == nullptr || candidates_changed) {
        session_ = std::make_unique<ImeSession>(
            engine_, schema_, static_cast<std::size_t>(settings_.candidates.max_items));
    }
}

void TextService::toggle_input_mode(ITfContext* const context) {
    english_mode_ = !english_mode_;
    punctuation_.reset_quotes();
    candidate_grid_.reset(0U);
    if (session_ != nullptr && !session_->snapshot().input.empty()) {
        session_->clear();
        symbol_candidates_.clear();
        symbol_mode_ = false;
        if (context != nullptr) {
            request_cancel(context);
        }
    }
    apply_settings_at_composition_boundary();
    candidate_window_.hide();
}

void TextService::save_user_model() noexcept {
    try {
        engine_.save_user_model(user_model_path_);
    } catch (...) {
        // Input must not fail only because the learning file could not be written.
    }
}

std::string TextService::load_schema() const {
    const auto path = local_app_data() / L"PiInput" / L"UserData" / L"settings.ini";
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        constexpr std::string_view prefix = "schema=";
        if (line.rfind(prefix, 0U) == 0U) {
            const std::string value = line.substr(prefix.size());
            if (value == "full" || engine_.shuangpin().has_scheme(value)) {
                return value;
            }
        }
    }
    return "full";
}

std::wstring TextService::schema_display_name() const {
    if (english_mode_) {
        return L"英文";
    }
    if (schema_ == "full") {
        return L"全拼";
    }
    for (const auto& item : engine_.shuangpin().schemes()) {
        if (item.id == schema_) {
            return utf8_to_wide(item.name);
        }
    }
    return utf8_to_wide(schema_);
}

}  // namespace piinput::windows
