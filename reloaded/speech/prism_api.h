#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <prism.h>

#include <filesystem>
#include <optional>

namespace sparking::speech {

// The release header defines an aggregate return ABI for prism_config_init.
// Check the actual loaded release before using that export.
class PrismApi {
public:
    explicit PrismApi(const std::filesystem::path& path) noexcept;
    ~PrismApi();
    PrismApi(const PrismApi&) = delete;
    PrismApi& operator=(const PrismApi&) = delete;

    explicit operator bool() const noexcept { return error_ == nullptr; }
    const char* error() const noexcept { return error_; }
    DWORD load_error() const noexcept { return load_error_; }
    std::optional<PrismConfig> make_config() const;

    decltype(&prism_init) init = nullptr;
    decltype(&prism_shutdown) shutdown = nullptr;
    decltype(&prism_registry_count) registry_count = nullptr;
    decltype(&prism_registry_create_best) create_best = nullptr;
    decltype(&prism_backend_free) free_backend = nullptr;
    decltype(&prism_backend_name) name = nullptr;
    decltype(&prism_backend_speak) speak = nullptr;
    decltype(&prism_backend_stop) stop = nullptr;
    decltype(&prism_backend_is_speaking) is_speaking = nullptr;

private:
    HMODULE library_ = nullptr;
    const char* error_ = "Prism library was not loaded";
    DWORD load_error_ = 0;
    decltype(&prism_config_init) config_init_ = nullptr;
};

}
