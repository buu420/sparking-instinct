#include "prism_api.h"

#include <string_view>

namespace sparking::speech {
namespace {
template<class Function>
Function import(HMODULE library, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(library, name));
}
}

static_assert(sizeof(void*) == 8);
static_assert(PRISM_CONFIG_VERSION == 4 && sizeof(PrismConfig) == 56);

PrismApi::PrismApi(const std::filesystem::path& path) noexcept
{
    if (!path.is_absolute()) {
        error_ = "Expected an absolute path to Prism";
        return;
    }
    library_ = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_) {
        load_error_ = GetLastError();
        return;
    }

    const auto version = import<decltype(&prism_version_string)>(library_, "prism_version_string");
    const char* value = version ? version() : nullptr;
    if (!value || std::string_view(value) != PRISM_VERSION_STRING) {
        error_ = "Prism version does not match the compiled ABI";
        return;
    }

    config_init_ = import<decltype(config_init_)>(library_, "prism_config_init");
    init = import<decltype(init)>(library_, "prism_init");
    shutdown = import<decltype(shutdown)>(library_, "prism_shutdown");
    registry_count = import<decltype(registry_count)>(library_, "prism_registry_count");
    create_best = import<decltype(create_best)>(library_, "prism_registry_create_best");
    free_backend = import<decltype(free_backend)>(library_, "prism_backend_free");
    name = import<decltype(name)>(library_, "prism_backend_name");
    speak = import<decltype(speak)>(library_, "prism_backend_speak");
    stop = import<decltype(stop)>(library_, "prism_backend_stop");
    is_speaking = import<decltype(is_speaking)>(library_, "prism_backend_is_speaking");
    if (!config_init_ || !init || !shutdown || !registry_count || !create_best || !free_backend ||
        !name || !speak || !stop || !is_speaking) {
        error_ = "Prism is missing required exports";
        return;
    }
    error_ = nullptr;
}

PrismApi::~PrismApi()
{
    if (library_) {
        FreeLibrary(library_);
    }
}

std::optional<PrismConfig> PrismApi::make_config() const
{
    if (error_) {
        return std::nullopt;
    }
    const auto config = config_init_();
    if (config.version != PRISM_CONFIG_VERSION) {
        return std::nullopt;
    }
    return config;
}

}
