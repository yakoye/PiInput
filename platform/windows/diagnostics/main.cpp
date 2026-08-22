#include "piinput/host_protocol.h"
#include "piinput_tsf_guids.h"
#include "profile_registration.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::wstring registry_string(
    const HKEY root, const wchar_t* const key, const wchar_t* const name) {
    DWORD bytes = 0U;
    DWORD type = 0U;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, &type, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

[[nodiscard]] std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

[[nodiscard]] std::string json_escape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    return output.str();
}

[[nodiscard]] std::string sha256_file(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    DWORD object_bytes = 0U;
    DWORD digest_bytes = 0U;
    DWORD returned = 0U;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &returned, 0U) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digest_bytes), sizeof(digest_bytes), &returned, 0U) >= 0) {
        std::vector<UCHAR> object(object_bytes);
        std::vector<UCHAR> digest(digest_bytes);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_bytes,
                nullptr, 0U, 0U) >= 0) {
            std::array<UCHAR, 65536U> buffer{};
            DWORD read = 0U;
            bool ok = true;
            while (true) {
                if (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                        &read, nullptr) == FALSE) {
                    ok = false;
                    break;
                }
                if (read == 0U) break;
                if (BCryptHashData(hash, buffer.data(), read, 0U) < 0) { ok = false; break; }
            }
            if (ok && BCryptFinishHash(hash, digest.data(), digest_bytes, 0U) >= 0) {
                std::ostringstream text;
                text << std::hex << std::setfill('0');
                for (const UCHAR byte : digest) text << std::setw(2) << static_cast<unsigned int>(byte);
                result = text.str();
            }
        }
    }
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0U);
    CloseHandle(file);
    return result;
}

[[nodiscard]] std::wstring pipe_name() {
    DWORD session = 0U;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session) == FALSE) return {};
    return L"\\\\.\\pipe\\PiInput.Host.v1." + std::to_wstring(session);
}

[[nodiscard]] std::string host_health() {
    const std::wstring name = pipe_name();
    if (name.empty()) return {};
    const HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U,
        nullptr, OPEN_EXISTING, 0U, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return {};
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr) == FALSE) {
        CloseHandle(pipe);
        return {};
    }
    const piinput::HostEnvelope request{
        .version = piinput::host_protocol_current,
        .client_id = static_cast<std::uint64_t>(GetCurrentProcessId()),
        .session_id = 1U,
        .sequence = 1U,
        .generation = 1U,
        .type = piinput::HostMessageType::health,
    };
    const auto bytes = piinput::encode_host_envelope(request);
    DWORD written = 0U;
    std::vector<std::byte> response(piinput::host_header_bytes + 4096U);
    DWORD read = 0U;
    const bool exchanged = WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()),
        &written, nullptr) != FALSE && written == bytes.size() &&
        ReadFile(pipe, response.data(), static_cast<DWORD>(response.size()), &read, nullptr) != FALSE;
    CloseHandle(pipe);
    if (!exchanged) return {};
    response.resize(read);
    piinput::ProtocolError error = piinput::ProtocolError::none;
    const auto envelope = piinput::decode_host_envelope(response, error);
    if (!envelope.has_value()) return {};
    std::string result;
    result.reserve(envelope->payload.size());
    for (const auto value : envelope->payload) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return result;
}

}  // namespace

int wmain() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
        std::cerr << "CoInitializeEx failed\n";
        return 2;
    }
    constexpr wchar_t clsid_path[] =
        L"Software\\Classes\\CLSID\\{13EB305F-2DA3-4CF7-8C45-16B016B801B5}\\InprocServer32";
    const std::wstring shim = registry_string(HKEY_CURRENT_USER, clsid_path, nullptr);
    const std::wstring host = registry_string(
        HKEY_CURRENT_USER, L"Software\\PiInput\\Runtime", L"CurrentHostPath");
    TF_INPUTPROCESSORPROFILE profile{};
    const HRESULT profile_result = piinput::windows::tsf::get_profile(&profile);
    const bool registered = SUCCEEDED(profile_result);
    const bool enabled = registered && (profile.dwFlags & TF_IPP_FLAG_ENABLED) != 0U;
    const bool active = registered && (profile.dwFlags & TF_IPP_FLAG_ACTIVE) != 0U;
    const bool shim_exists = !shim.empty() && std::filesystem::is_regular_file(shim);
    const bool host_exists = !host.empty() && std::filesystem::is_regular_file(host);
    const std::filesystem::path lexicon = host_exists
        ? std::filesystem::path(host).parent_path().parent_path() / L"data" / L"piinput-base.lex"
        : std::filesystem::path{};
    const bool lexicon_exists = !lexicon.empty() && std::filesystem::is_regular_file(lexicon);
    const std::string health = host_health();

    std::cout << "{\n"
              << "  \"protocol_version\": " << piinput::host_protocol_current << ",\n"
              << "  \"profile_registered\": " << (registered ? "true" : "false") << ",\n"
              << "  \"profile_enabled\": " << (enabled ? "true" : "false") << ",\n"
              << "  \"profile_active\": " << (active ? "true" : "false") << ",\n"
              << "  \"shim_path\": \"" << json_escape(utf8(shim)) << "\",\n"
              << "  \"shim_exists\": " << (shim_exists ? "true" : "false") << ",\n"
              << "  \"shim_sha256\": \"" << (shim_exists ? sha256_file(shim) : "") << "\",\n"
              << "  \"host_path\": \"" << json_escape(utf8(host)) << "\",\n"
              << "  \"host_exists\": " << (host_exists ? "true" : "false") << ",\n"
              << "  \"host_sha256\": \"" << (host_exists ? sha256_file(host) : "") << "\",\n"
              << "  \"lexicon_path\": \"" << json_escape(utf8(lexicon.wstring())) << "\",\n"
              << "  \"lexicon_exists\": " << (lexicon_exists ? "true" : "false") << ",\n"
              << "  \"lexicon_sha256\": \""
              << (lexicon_exists ? sha256_file(lexicon) : "") << "\",\n"
              << "  \"host_connected\": " << (!health.empty() ? "true" : "false") << ",\n"
              << "  \"host_health\": \"" << json_escape(health) << "\",\n"
              << "  \"legacy_module_scan\": \"not_performed\"\n"
              << "}\n";
    if (SUCCEEDED(com_result)) CoUninitialize();
    return 0;
}
