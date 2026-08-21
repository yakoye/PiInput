#pragma once

#include "piinput/windows_compat.h"

#include <ctffunc.h>
#include <msctf.h>

#include <functional>
#include <string>

namespace piinput::windows {

// What the user picked from a language bar button.
enum class LangBarCommand : std::uint8_t {
    toggle_language,
    toggle_schema,
    symbols,
    settings,
    about,
    help,
};

// One button in the Windows input indicator -- the strip on the taskbar where
// Microsoft Pinyin shows 中 and 拼 side by side.
//
// This is deliberately not a notification-area icon. Those live in a different
// part of the taskbar and cannot sit next to the input indicator, which is why
// the first attempt ended up with separate icons in the overflow area instead
// of one group. Text buttons also let Windows draw the label itself, so 中/英
// follows the system theme with no background of our own.
class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    using Handler = std::function<void(LangBarCommand)>;

    // `fixed_icon` is the product logo. When null the button paints the 中/英
    // mark itself and repaints it when the system theme flips. The button does
    // not own a fixed icon; the caller outlives it.
    LangBarButton(
        const GUID& item_guid,
        std::wstring description,
        bool show_menu,
        Handler handler,
        HICON fixed_icon = nullptr,
        ULONG sort_order = 0U);

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
    STDMETHODIMP GetStatus(DWORD* status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT point, const RECT* area) override;
    STDMETHODIMP InitMenu(ITfMenu* menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON* icon) override;
    STDMETHODIMP GetText(BSTR* text) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

    // The label Windows draws. Changing it tells the shell to redraw.
    // 中 or 英. Windows draws the item from GetIcon, so the mark is an icon
    // rather than text; it is generated to match the current system theme.
    void set_chinese(bool chinese) noexcept;
    void set_menu_state(std::wstring language, std::wstring schema);

private:
    // The shell sends right clicks to OnClick instead of asking for a menu, so
    // this puts one up directly.
    void show_context_menu(POINT point) noexcept;

    LONG references_{1};
    GUID guid_{};
    std::wstring description_;
    bool chinese_{true};
    std::wstring language_label_;
    std::wstring schema_label_;
    bool show_menu_{};
    HICON icon_{nullptr};
    HICON fixed_icon_{nullptr};
    ULONG sort_order_{};
    bool icon_dark_{};
    Handler handler_;
    ITfLangBarItemSink* sink_{nullptr};
    DWORD sink_cookie_{TF_INVALID_COOKIE};
};

// Whether Windows is running a dark theme. Shared so the mode popup and the
// indicator mark pick the same colours.
[[nodiscard]] bool system_uses_dark_theme() noexcept;

// Records whether the indicator icon loaded, into the same log the language bar
// writes. A missing icon used to make both buttons disappear.
void trace_lang_bar_icon(bool loaded) noexcept;

// Shared with the text service so conversion-mode reporting lands in the same
// log as the language bar registration.
void trace_lang_bar_stage(const char* stage, long detail) noexcept;

// Registers the buttons with the thread's language bar and keeps them updated.
class LangBar final {
public:
    [[nodiscard]] bool create(
        ITfThreadMgr* thread_manager,
        TfClientId client_id,
        HICON icon,
        LangBarButton::Handler handler);
    void destroy() noexcept;
    void set_state(bool chinese_input, const std::wstring& schema_label);

private:
    ITfLangBarItemMgr* manager_{nullptr};
    LangBarButton* language_{nullptr};
    LangBarButton* product_{nullptr};
};

}  // namespace piinput::windows
