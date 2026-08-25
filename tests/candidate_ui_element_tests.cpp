#include "candidate_ui_element.h"

#include <oleauto.h>

#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

piinput::HostSnapshot snapshot() {
    piinput::HostSnapshot value;
    value.raw = "nihao";
    value.composition_text = "nihao";
    value.view.items_per_row = 2U;
    value.view.visible_rows = 1U;
    value.view.active_row = 0U;
    value.view.active_column = 1U;
    value.candidates = {
        {11U, "你好", "ni'hao", 100},
        {12U, "你号", "ni'hao", 90},
        {13U, "拟好", "ni'hao", 80},
    };
    return value;
}

void test_candidate_contract_and_search_integration() {
    std::uint64_t finalized = 0U;
    bool aborted = false;
    auto* element = new piinput::windows::CandidateUiElement(
        nullptr,
        [&](const std::uint64_t id) { finalized = id; },
        [&] { aborted = true; });
    element->update(snapshot());

    ITfCandidateListUIElementBehavior* behavior = nullptr;
    check(SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&behavior))) && behavior != nullptr,
        "candidate behavior interface is exposed");
    ITfIntegratableCandidateListUIElement* integrated = nullptr;
    check(SUCCEEDED(element->QueryInterface(IID_PPV_ARGS(&integrated))) && integrated != nullptr,
        "Windows Search integration interface is exposed");

    UINT count = 0U;
    UINT selection = 0U;
    check(SUCCEEDED(behavior->GetCount(&count)) && count == 3U,
        "candidate count mirrors the Host snapshot");
    check(SUCCEEDED(behavior->GetSelection(&selection)) && selection == 1U,
        "candidate selection mirrors the Host view");
    BSTR text = nullptr;
    check(SUCCEEDED(behavior->GetString(0U, &text)) && text != nullptr &&
            std::wstring_view(text) == L"你好",
        "candidate UTF-8 text is exposed as a TSF BSTR");
    SysFreeString(text);

    UINT page_count = 0U;
    UINT pages[2]{};
    check(SUCCEEDED(behavior->GetPageIndex(pages, 2U, &page_count)) &&
            page_count == 2U && pages[0] == 0U && pages[1] == 2U,
        "candidate pages follow PiInput's visible row capacity");
    check(SUCCEEDED(behavior->SetSelection(2U)) && SUCCEEDED(behavior->Finalize()) &&
            finalized == 13U,
        "an integrated candidate click selects the stable Host candidate id");
    check(SUCCEEDED(behavior->Abort()) && aborted,
        "an integrated candidate abort cancels the composition");

    const GUID search_box_style =
        {0xe6d1bd11, 0x82f7, 0x4903, {0xae, 0x21, 0x1a, 0x63, 0x97, 0xcd, 0xe2, 0xeb}};
    check(SUCCEEDED(integrated->SetIntegrationStyle(search_box_style)),
        "Windows Search integration style is accepted");
    TfIntegratableCandidateListSelectionStyle style = STYLE_IMPLIED_SELECTION;
    check(SUCCEEDED(integrated->GetSelectionStyle(&style)) &&
            style == STYLE_ACTIVE_SELECTION,
        "integrated candidate selection is explicit");
    BOOL show_numbers = FALSE;
    check(SUCCEEDED(integrated->ShowCandidateNumbers(&show_numbers)) && show_numbers,
        "integrated candidate numbers remain available");
    BOOL key_eaten = FALSE;
    check(SUCCEEDED(integrated->OnKeyDown(VK_DOWN, 0L, &key_eaten)) && key_eaten,
        "integrated candidate navigation is not consumed again by SearchHost");

    integrated->Release();
    behavior->Release();
    element->Release();
}

}  // namespace

int main() {
    test_candidate_contract_and_search_integration();
    std::cout << "Candidate UIElement tests passed\n";
    return 0;
}
