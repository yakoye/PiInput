#include "user_keyboard_registration.h"

#include "piinput_tsf_guids.h"

#include <objbase.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <string>

namespace piinput::windows::tsf {
namespace {

constexpr DWORD kIlotUninstall = 0x00000001U;

[[nodiscard]] std::wstring guid_string(const GUID& guid) {
    std::array<wchar_t, 64> buffer{};
    if (StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size())) == 0) {
        return {};
    }
    return buffer.data();
}

[[nodiscard]] std::filesystem::path system_input_dll() {
    std::array<wchar_t, 32768> directory{};
    const UINT length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (length == 0U || length >= directory.size()) {
        return {};
    }
    return std::filesystem::path(directory.data()) / L"input.dll";
}

}  // namespace

std::wstring profile_tip_identifier() {
    wchar_t language[16]{};
    if (swprintf_s(language, L"0x%04X:", static_cast<unsigned int>(kPiInputLanguageId)) < 0) {
        return {};
    }
    return std::wstring(language) + guid_string(CLSID_PiInputTextService) +
        guid_string(GUID_PiInputProfile) + L";";
}

HRESULT apply_user_keyboard_registration(
    const InstallLayoutOrTipFunction function,
    const bool uninstall) {
    if (function == nullptr) {
        return E_POINTER;
    }
    const std::wstring identifier = profile_tip_identifier();
    if (identifier.empty()) {
        return E_FAIL;
    }
    SetLastError(ERROR_SUCCESS);
    if (function(identifier.c_str(), uninstall ? kIlotUninstall : 0U) != FALSE) {
        return S_OK;
    }
    DWORD error = GetLastError();
    if (error == ERROR_SUCCESS) {
        error = ERROR_GEN_FAILURE;
    }
    return HRESULT_FROM_WIN32(error);
}

namespace {

HRESULT update_user_keyboard_registration(const bool uninstall) {
    const std::filesystem::path input_dll = system_input_dll();
    if (input_dll.empty()) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const HMODULE module = LoadLibraryExW(input_dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const auto function = reinterpret_cast<InstallLayoutOrTipFunction>(
        GetProcAddress(module, "InstallLayoutOrTip"));
    const HRESULT result = apply_user_keyboard_registration(function, uninstall);
    FreeLibrary(module);
    return result;
}

}  // namespace

HRESULT enable_user_keyboard() {
    return update_user_keyboard_registration(false);
}

HRESULT disable_user_keyboard() {
    return update_user_keyboard_registration(true);
}

}  // namespace piinput::windows::tsf
