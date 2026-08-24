#pragma once

#include "profile_registration.h"

#include <array>
#include <vector>

namespace piinput::windows::tsf {

enum class MachineRegistrationStage {
    none,
    profile,
    category_manager,
    category,
};

struct MachineRegistrationResult {
    HRESULT result{S_OK};
    MachineRegistrationStage stage{MachineRegistrationStage::none};
};

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
    TF_INPUTPROCESSORPROFILE existing_profile{};
    const bool profile_existed = SUCCEEDED(get_profile(&existing_profile));
    HRESULT result = register_profile(icon_file);
    if (FAILED(result)) {
        return {result, MachineRegistrationStage::profile};
    }

    ITfCategoryMgr* manager = nullptr;
    result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(result) || manager == nullptr) {
        if (!profile_existed) (void)unregister_profile();
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
    if (FAILED(profile)) return {profile, MachineRegistrationStage::profile};
    return {first_failure, stage};
}

}  // namespace piinput::windows::tsf
