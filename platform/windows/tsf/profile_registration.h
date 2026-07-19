#pragma once

#include "piinput_tsf_guids.h"

#include <msctf.h>
#include <objbase.h>

#include <cstddef>
#include <iterator>

namespace piinput::windows::tsf {

inline HRESULT create_profile_manager(ITfInputProcessorProfileMgr** manager) {
    if (manager == nullptr) {
        return E_POINTER;
    }
    *manager = nullptr;

    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&profiles));
    if (FAILED(result)) {
        return result;
    }

    result = profiles->QueryInterface(IID_PPV_ARGS(manager));
    profiles->Release();
    return result;
}

inline HRESULT get_profile(TF_INPUTPROCESSORPROFILE* profile) {
    if (profile == nullptr) {
        return E_POINTER;
    }
    *profile = {};

    ITfInputProcessorProfileMgr* manager = nullptr;
    HRESULT result = create_profile_manager(&manager);
    if (FAILED(result)) {
        return result;
    }

    result = manager->GetProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        kPiInputLanguageId,
        CLSID_PiInputTextService,
        GUID_PiInputProfile,
        nullptr,
        profile);
    manager->Release();
    return result;
}

inline HRESULT register_profile() {
    ITfInputProcessorProfileMgr* manager = nullptr;
    HRESULT result = create_profile_manager(&manager);
    if (FAILED(result)) {
        return result;
    }

    constexpr wchar_t description[] = L"PiInput 中文输入法（开发版）";
    result = manager->RegisterProfile(
        CLSID_PiInputTextService,
        kPiInputLanguageId,
        GUID_PiInputProfile,
        description,
        static_cast<ULONG>(std::size(description) - 1U),
        nullptr,
        0U,
        0U,
        nullptr,
        0U,
        TRUE,
        0U);
    manager->Release();
    return result;
}

inline HRESULT unregister_profile() {
    ITfInputProcessorProfileMgr* manager = nullptr;
    HRESULT result = create_profile_manager(&manager);
    if (FAILED(result)) {
        return result;
    }

    TF_INPUTPROCESSORPROFILE profile{};
    result = manager->GetProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        kPiInputLanguageId,
        CLSID_PiInputTextService,
        GUID_PiInputProfile,
        nullptr,
        &profile);
    if (FAILED(result)) {
        manager->Release();
        return S_FALSE;
    }

    result = manager->UnregisterProfile(
        CLSID_PiInputTextService,
        kPiInputLanguageId,
        GUID_PiInputProfile,
        0U);
    manager->Release();
    return result;
}

inline HRESULT activate_profile() {
    TF_INPUTPROCESSORPROFILE profile{};
    HRESULT result = get_profile(&profile);
    if (FAILED(result)) {
        return result;
    }

    ITfInputProcessorProfileMgr* manager = nullptr;
    result = create_profile_manager(&manager);
    if (FAILED(result)) {
        return result;
    }

    result = manager->ActivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        kPiInputLanguageId,
        CLSID_PiInputTextService,
        GUID_PiInputProfile,
        nullptr,
        TF_IPPMF_ENABLEPROFILE |
            TF_IPPMF_FORSESSION |
            TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    manager->Release();
    return result;
}

inline HRESULT deactivate_profile() {
    TF_INPUTPROCESSORPROFILE profile{};
    HRESULT result = get_profile(&profile);
    if (FAILED(result)) {
        return S_FALSE;
    }
    if ((profile.dwFlags & TF_IPP_FLAG_ACTIVE) == 0U) {
        return S_FALSE;
    }

    ITfInputProcessorProfileMgr* manager = nullptr;
    result = create_profile_manager(&manager);
    if (FAILED(result)) {
        return result;
    }

    result = manager->DeactivateProfile(
        TF_PROFILETYPE_INPUTPROCESSOR,
        kPiInputLanguageId,
        CLSID_PiInputTextService,
        GUID_PiInputProfile,
        nullptr,
        TF_IPPMF_FORSESSION);
    manager->Release();
    return result;
}

}  // namespace piinput::windows::tsf
