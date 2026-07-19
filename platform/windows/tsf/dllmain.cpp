#include "text_service.h"
#include "piinput_tsf_guids.h"
#include "profile_registration.h"

#include "piinput/windows_compat.h"

#include <msctf.h>
#include <objbase.h>

#include <atomic>
#include <iterator>
#include <new>
#include <string>

using piinput::windows::TextService;
using piinput::windows::g_module_instance;
using piinput::windows::g_object_count;

namespace {

std::atomic<long> g_server_locks{0};

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { ++g_object_count; }
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this);
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

    STDMETHODIMP CreateInstance(IUnknown* const outer, REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        auto* service = new (std::nothrow) TextService(g_module_instance);
        if (service == nullptr) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result = service->QueryInterface(iid, object);
        service->Release();
        return result;
    }

    STDMETHODIMP LockServer(const BOOL lock) override {
        if (lock != FALSE) {
            ++g_server_locks;
        } else {
            --g_server_locks;
        }
        return S_OK;
    }

private:
    ~ClassFactory() { --g_object_count; }
    std::atomic<ULONG> ref_count_{1U};
};

[[nodiscard]] std::wstring guid_string(const GUID& guid) {
    wchar_t buffer[64]{};
    StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

HRESULT set_registry_string(
    const HKEY root,
    const std::wstring& key_path,
    const wchar_t* const value_name,
    const std::wstring& value) {
    HKEY key = nullptr;
    const LONG create_result = RegCreateKeyExW(root, key_path.c_str(), 0U, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create_result != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(create_result);
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t));
    const LONG set_result = RegSetValueExW(key, value_name, 0U, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(set_result);
}

HRESULT register_com_server() {
    wchar_t module_path[32768]{};
    const DWORD length = GetModuleFileNameW(g_module_instance, module_path, static_cast<DWORD>(std::size(module_path)));
    if (length == 0U || length >= std::size(module_path)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const std::wstring clsid = guid_string(CLSID_PiInputTextService);
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid;
    HRESULT result = set_registry_string(HKEY_CURRENT_USER, base, nullptr, L"PiInput Text Service");
    if (FAILED(result)) {
        return result;
    }
    result = set_registry_string(HKEY_CURRENT_USER, base + L"\\InprocServer32", nullptr, module_path);
    if (FAILED(result)) {
        return result;
    }
    return set_registry_string(HKEY_CURRENT_USER, base + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
}

HRESULT unregister_com_server() {
    const std::wstring path = L"Software\\Classes\\CLSID\\" + guid_string(CLSID_PiInputTextService);
    const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    if (result == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(result);
}

HRESULT register_tsf_profile() {
    HRESULT result = piinput::windows::tsf::register_profile();
    if (FAILED(result)) {
        return result;
    }

    ITfCategoryMgr* category_manager = nullptr;
    result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&category_manager));
    if (SUCCEEDED(result)) {
        result = category_manager->RegisterCategory(
            CLSID_PiInputTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_PiInputTextService);
        category_manager->Release();
    }
    if (FAILED(result)) {
        piinput::windows::tsf::unregister_profile();
    }
    return result;
}

HRESULT unregister_tsf_profile() {
    ITfCategoryMgr* category_manager = nullptr;
    HRESULT category_result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&category_manager));
    if (SUCCEEDED(category_result)) {
        category_result = category_manager->UnregisterCategory(
            CLSID_PiInputTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_PiInputTextService);
        category_manager->Release();
    }

    const HRESULT profile_result = piinput::windows::tsf::unregister_profile();
    const bool profile_missing = (profile_result == S_FALSE);
    const bool category_missing_or_removed = SUCCEEDED(category_result) || category_result == E_FAIL;
    if (profile_missing && category_missing_or_removed) {
        return S_OK;
    }
    return FAILED(profile_result) ? profile_result : category_result;
}

class ComInitializer final {
public:
    ComInitializer() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComInitializer() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_{};
};

}  // namespace

BOOL WINAPI DllMain(const HINSTANCE instance, const DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_object_count.load() == 0 && g_server_locks.load() == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetClassObject(
    REFCLSID class_id,
    REFIID iid,
    void** object) {
    if (!IsEqualCLSID(class_id, CLSID_PiInputTextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = factory->QueryInterface(iid, object);
    factory->Release();
    return result;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    ComInitializer com;
    if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
        return com.result();
    }
    HRESULT result = register_com_server();
    if (SUCCEEDED(result)) {
        result = register_tsf_profile();
        if (FAILED(result)) {
            unregister_com_server();
        }
    }
    return result;
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    ComInitializer com;
    if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
        return com.result();
    }
    const HRESULT tsf_result = unregister_tsf_profile();
    const HRESULT com_result = unregister_com_server();
    return FAILED(tsf_result) ? tsf_result : com_result;
}
