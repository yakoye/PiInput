#pragma once

#include "profile_registration.h"

#include <array>
#include <string>
#include <vector>

namespace piinput::windows::tsf {

enum class MachineRegistrationStage {
    none,
    com_server,
    profile,
    category_manager,
    category,
};

struct MachineRegistrationResult {
    HRESULT result{S_OK};
    MachineRegistrationStage stage{MachineRegistrationStage::none};
};

inline std::wstring machine_com_class_key() {
    std::array<wchar_t, 64U> text{};
    if (StringFromGUID2(CLSID_PiInputTextService, text.data(),
            static_cast<int>(text.size())) == 0) {
        return {};
    }
    return L"Software\\Classes\\CLSID\\" + std::wstring(text.data());
}

inline std::wstring read_machine_com_server() {
    const std::wstring base = machine_com_class_key();
    if (base.empty()) return {};
    const std::wstring key = base + L"\\InprocServer32";
    DWORD bytes = 0U;
    LONG result = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr,
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, nullptr, &bytes);
    if (result != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    result = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), nullptr,
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value.data(), &bytes);
    if (result != ERROR_SUCCESS) return {};
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

inline HRESULT write_machine_registry_string(
    const std::wstring& key_path,
    const wchar_t* const value_name,
    const std::wstring_view value) {
    HKEY key = nullptr;
    const LONG created = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_path.c_str(), 0U,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) return HRESULT_FROM_WIN32(created);
    const std::wstring stored(value);
    const DWORD bytes = static_cast<DWORD>((stored.size() + 1U) * sizeof(wchar_t));
    const LONG written = RegSetValueExW(key, value_name, 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(stored.c_str()), bytes);
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(written);
}

inline HRESULT register_machine_com_server(const std::wstring_view dll) {
    if (dll.empty()) return E_INVALIDARG;
    const std::wstring base = machine_com_class_key();
    if (base.empty()) return E_FAIL;
    HRESULT result = write_machine_registry_string(
        base, nullptr, L"PiInput Text Service");
    if (FAILED(result)) return result;
    result = write_machine_registry_string(base + L"\\InprocServer32", nullptr, dll);
    if (FAILED(result)) return result;
    return write_machine_registry_string(
        base + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
}

inline HRESULT unregister_machine_com_server() {
    const std::wstring base = machine_com_class_key();
    if (base.empty()) return E_FAIL;
    const LONG removed = RegDeleteTreeW(HKEY_LOCAL_MACHINE, base.c_str());
    if (removed == ERROR_FILE_NOT_FOUND || removed == ERROR_PATH_NOT_FOUND) return S_FALSE;
    return HRESULT_FROM_WIN32(removed);
}

inline void restore_machine_com_server(const std::wstring& previous) {
    if (previous.empty()) {
        (void)unregister_machine_com_server();
    } else {
        (void)register_machine_com_server(previous);
    }
}

inline constexpr std::array<const GUID*, 5U> kPiInputTsfCategories = {
    // Keyboard makes this a TIP; the capability categories cover the taskbar,
    // UI-element/immersive hosts and the 中/英 conversion-mode compartment.
    // COMLESS and SECUREMODE are intentionally absent because PiInput does not
    // implement those activation contracts.
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
};

inline HRESULT category_is_registered(
    ITfCategoryMgr* const manager,
    const GUID& category,
    bool* const registered) {
    if (manager == nullptr || registered == nullptr) return E_POINTER;
    *registered = false;

    IEnumGUID* categories = nullptr;
    HRESULT result = manager->EnumCategoriesInItem(CLSID_PiInputTextService, &categories);
    if (FAILED(result)) return result;

    GUID current{};
    ULONG fetched = 0U;
    while ((result = categories->Next(1U, &current, &fetched)) == S_OK) {
        if (IsEqualGUID(current, category)) {
            *registered = true;
            break;
        }
    }
    categories->Release();
    return FAILED(result) ? result : S_OK;
}

inline MachineRegistrationResult register_machine_tsf(
    const std::wstring_view icon_file) {
    const std::wstring previous_com = read_machine_com_server();
    HRESULT result = register_machine_com_server(icon_file);
    if (FAILED(result)) {
        restore_machine_com_server(previous_com);
        return {result, MachineRegistrationStage::com_server};
    }

    TF_INPUTPROCESSORPROFILE existing_profile{};
    const bool profile_existed = SUCCEEDED(get_profile(&existing_profile));
    result = register_profile(icon_file);
    if (FAILED(result)) {
        restore_machine_com_server(previous_com);
        return {result, MachineRegistrationStage::profile};
    }

    ITfCategoryMgr* manager = nullptr;
    result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(result) || manager == nullptr) {
        if (!profile_existed) (void)unregister_profile();
        restore_machine_com_server(previous_com);
        return {FAILED(result) ? result : E_NOINTERFACE,
            MachineRegistrationStage::category_manager};
    }

    std::vector<const GUID*> added;
    for (const GUID* const category : kPiInputTsfCategories) {
        bool existed = false;
        const HRESULT queried = category_is_registered(manager, *category, &existed);
        result = manager->RegisterCategory(
            CLSID_PiInputTextService, *category, CLSID_PiInputTextService);
        if (FAILED(result)) {
            // RegisterCategory is not consistently idempotent across Windows
            // versions. An E_FAIL for a relationship that is already present is
            // success; an absent relationship is a real registration failure.
            bool present = false;
            if (SUCCEEDED(category_is_registered(manager, *category, &present)) && present) {
                continue;
            }
            for (const GUID* const rollback : added) {
                (void)manager->UnregisterCategory(
                    CLSID_PiInputTextService, *rollback, CLSID_PiInputTextService);
            }
            manager->Release();
            if (!profile_existed) (void)unregister_profile();
            restore_machine_com_server(previous_com);
            return {result, MachineRegistrationStage::category};
        }
        if (SUCCEEDED(queried) && !existed) added.push_back(category);
    }
    manager->Release();
    return {S_OK, MachineRegistrationStage::none};
}

inline MachineRegistrationResult unregister_machine_tsf() {
    ITfCategoryMgr* manager = nullptr;
    HRESULT first_failure = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    MachineRegistrationStage stage = MachineRegistrationStage::category_manager;
    if (SUCCEEDED(first_failure) && manager == nullptr) {
        first_failure = E_NOINTERFACE;
    } else if (SUCCEEDED(first_failure)) {
        first_failure = S_OK;
        stage = MachineRegistrationStage::none;
        for (const GUID* const category : kPiInputTsfCategories) {
            const HRESULT removed = manager->UnregisterCategory(
                CLSID_PiInputTextService, *category, CLSID_PiInputTextService);
            if (FAILED(removed) && removed != E_FAIL && SUCCEEDED(first_failure)) {
                first_failure = removed;
                stage = MachineRegistrationStage::category;
            }
        }
        manager->Release();
    }

    const HRESULT profile = unregister_profile();
    const HRESULT com = unregister_machine_com_server();
    if (FAILED(profile)) return {profile, MachineRegistrationStage::profile};
    if (FAILED(first_failure)) return {first_failure, stage};
    if (FAILED(com)) return {com, MachineRegistrationStage::com_server};
    return {S_OK, MachineRegistrationStage::none};
}

}  // namespace piinput::windows::tsf
