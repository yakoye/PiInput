#include "host_runtime.h"

#include <shlobj.h>

#include <array>
#include <system_error>

namespace piinput::windows {

namespace {

[[nodiscard]] std::string schema_name(const InputSchema schema) {
    switch (schema) {
    case InputSchema::full: return "full";
    case InputSchema::flypy: return "flypy";
    case InputSchema::natural: return "natural";
    case InputSchema::mspy: return "mspy";
    case InputSchema::abc: return "abc";
    }
    return "flypy";
}

[[nodiscard]] bool path_exists(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] std::filesystem::path environment_path(const wchar_t* const name) noexcept {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0U);
    if (required <= 1U) return {};
    try {
        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
        if (written == 0U || written >= required) return {};
        value.resize(written);
        return std::filesystem::path(value);
    } catch (...) {
        return {};
    }
}

}  // namespace

bool HostRuntime::load(const HostRuntimePaths& paths, std::string& error) noexcept {
    try {
        settings_manager_ = std::make_unique<SettingsManager>(
            paths.user_data / L"settings.ini");
        settings_manager_->apply_pending_at_composition_boundary();
        if (const auto current = settings_manager_->current(); current != nullptr) {
            settings_ = *current;
        } else {
            settings_ = default_settings();
        }
        schema_ = schema_name(settings_.general.schema);

        const auto lexicon_root = paths.user_data / L"lexicons";
        const std::array candidates{
            lexicon_root / L"piinput-imported.lex",
            lexicon_root / L"piinput-base.lex",
            paths.package_data / L"piinput-base.lex",
            paths.package_data / L"base_lexicon.tsv",
        };
        loaded_lexicon_.clear();
        for (const auto& candidate : candidates) {
            if (!path_exists(candidate)) continue;
            engine_.load_lexicon(candidate);
            loaded_lexicon_ = candidate;
            break;
        }
        if (loaded_lexicon_.empty() || engine_.entry_count() == 0U) {
            error = "no usable PiInput lexicon was found";
            return false;
        }

        user_model_path_ = paths.user_data / L"user_model.tsv";
        if (path_exists(user_model_path_)) engine_.load_user_model(user_model_path_);

        if (settings_.english.builtin_dictionary) {
            (void)english_.load_builtin_tsv(paths.package_data / L"english_lexicon.tsv");
            (void)english_.load_builtin_tsv(paths.package_data / L"english_supplement.tsv");
            (void)english_.load_completion_preferences_tsv(
                paths.package_data / L"english_completion_preferences.tsv");
            (void)english_.load_builtin_tsv(paths.user_data / L"english_downloaded.tsv");
        }
        if (settings_.english.user_dictionary) {
            (void)english_.load_user_tsv(paths.user_data / L"english_user.tsv");
        }
        if (settings_.english.user_learning) {
            (void)english_.load_learning_tsv(paths.user_data / L"english_learning.tsv");
        }
        symbols_.load_tsv(paths.package_data / L"symbols.tsv");
        // Typing a symbol name as pinyin offers the symbol among the ordinary
        // candidates: pai gives π, qiuhe gives ∑. Built from the same table the
        // symbol search uses, so there is one place to add a symbol.
        engine_.set_symbol_shortcuts(symbols_.shortcuts_by_alias());

        // Build the bounded one-letter prefix cache before the health endpoint is
        // published. This keeps the first key in a newly focused application on
        // the same microsecond-scale path as subsequent keys.
        prewarmed_prefix_count_ = 0U;
        for (char initial = 'a'; initial <= 'z'; ++initial) {
            (void)engine_.query(
                std::string(1U, initial), schema_, settings_.candidates.max_items, settings_);
            ++prewarmed_prefix_count_;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "unknown PiInput Host runtime initialization error";
        return false;
    }
}

bool HostRuntime::save_user_model(std::string& error) const noexcept {
    try {
        if (user_model_path_.empty()) {
            error = "user model path is unavailable";
            return false;
        }
        engine_.save_user_model(user_model_path_);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "unknown user model save error";
        return false;
    }
}

void HostRuntime::poll_settings_at_composition_boundary() noexcept {
    if (settings_manager_ == nullptr) return;
    settings_manager_->poll();
    settings_manager_->apply_pending_at_composition_boundary();
    if (const auto current = settings_manager_->current(); current != nullptr) {
        settings_ = *current;
        schema_ = schema_name(settings_.general.schema);
    }
}

Engine& HostRuntime::engine() noexcept { return engine_; }
EnglishLexicon& HostRuntime::english() noexcept { return english_; }
SymbolIndex& HostRuntime::symbols() noexcept { return symbols_; }
const SettingsSnapshot& HostRuntime::settings() const noexcept { return settings_; }
const std::string& HostRuntime::schema() const noexcept { return schema_; }
const std::filesystem::path& HostRuntime::loaded_lexicon() const noexcept {
    return loaded_lexicon_;
}

std::size_t HostRuntime::prewarmed_prefix_count() const noexcept {
    return prewarmed_prefix_count_;
}

HostRuntimePaths discover_host_runtime_paths(const HINSTANCE module) noexcept {
    HostRuntimePaths result;
    result.package_data = environment_path(L"PIINPUT_PACKAGE_DATA_DIR");
    result.user_data = environment_path(L"PIINPUT_USER_DATA_DIR");
    std::array<wchar_t, 32768U> module_path{};
    const DWORD length = GetModuleFileNameW(
        module, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (result.package_data.empty() && length != 0U && length < module_path.size()) {
        result.package_data =
            std::filesystem::path(std::wstring_view(module_path.data(), length))
                .parent_path().parent_path() / L"data";
    }
    PWSTR local = nullptr;
    if (result.user_data.empty() &&
        SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local)) &&
        local != nullptr) {
        result.user_data = std::filesystem::path(local) / L"PiInput" / L"UserData";
    }
    if (local != nullptr) CoTaskMemFree(local);
    return result;
}

std::filesystem::path settings_executable_for_host(
    const std::filesystem::path& host_executable) {
    return host_executable.parent_path() / L"PiInput-Settings.exe";
}

}  // namespace piinput::windows
