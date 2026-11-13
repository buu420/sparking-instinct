#include "../native/executable.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define NETHOST_USE_AS_STATIC
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <bit>
#include <fstream>
#include <string>

namespace {
using std::filesystem::path;

path module_path(HMODULE module)
{
    std::wstring name(32768, L'\0');
    const auto count = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
    if (!count || count >= name.size()) return {};
    name.resize(count);
    return path(name);
}

bool same_name(const path& first, const path& second)
{
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void log_message(const path& log, const char* message, long result = 0)
{
    std::ofstream out(log, std::ios::app);
    out << message;
    if (result) out << " (" << std::hex << static_cast<unsigned long>(result) << ")";
    out << '\n';
}

template<class Function>
Function host_export(HMODULE module, const char* name)
{
    return std::bit_cast<Function>(GetProcAddress(module, name));
}

void load_local_profile(const path& manager, const path& log)
{
    const auto loader = manager / L"Loader" / L"x64";
    const auto assembly = loader / L"Reloaded.Mod.Loader.dll";
    const auto runtime = loader / L"Reloaded.Mod.Loader.runtimeconfig.json";
    if (!std::filesystem::is_regular_file(assembly) || !std::filesystem::is_regular_file(runtime)) {
        log_message(log, "The local Reloaded II runtime is missing.");
        return;
    }
    if (GetModuleHandleW(L"UE4SS.dll")) {
        log_message(log, "Another accessibility loader is already active; the reader did not load.");
        return;
    }

    std::wstring host_name(32768, L'\0');
    auto host_name_size = host_name.size();
    const get_hostfxr_parameters parameters{sizeof(parameters), assembly.c_str(), nullptr};
    const auto find_result = get_hostfxr_path(host_name.data(), &host_name_size, &parameters);
    if (find_result != 0) {
        log_message(log, "Could not locate the installed x64 .NET host.", find_result);
        return;
    }
    const auto host = LoadLibraryExW(host_name.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!host) {
        log_message(log, "Could not load the installed x64 .NET host.", GetLastError());
        return;
    }
    // Keep hostfxr loaded for the life of the runtime.
    const auto init = host_export<hostfxr_initialize_for_runtime_config_fn>(host, "hostfxr_initialize_for_runtime_config");
    const auto get_delegate = host_export<hostfxr_get_runtime_delegate_fn>(host, "hostfxr_get_runtime_delegate");
    const auto close = host_export<hostfxr_close_fn>(host, "hostfxr_close");
    if (!init || !get_delegate || !close) {
        log_message(log, "The installed .NET host does not provide the required hosting API.");
        return;
    }

    hostfxr_handle context{};
    const auto init_result = init(runtime.c_str(), nullptr, &context);
    if (init_result < 0 || !context) {
        if (context) close(context);
        log_message(log, "Could not start the x64 .NET 9 Desktop Runtime.", init_result);
        return;
    }
    void* load_assembly{};
    const auto delegate_result = get_delegate(context, hdt_load_assembly_and_get_function_pointer, &load_assembly);
    close(context);
    if (delegate_result != 0 || !load_assembly) {
        log_message(log, "Could not obtain the .NET assembly loader.", delegate_result);
        return;
    }

    const auto load = std::bit_cast<load_assembly_and_get_function_pointer_fn>(load_assembly);
    void* entry_point{};
    const auto load_result = load(assembly.c_str(), L"Reloaded.Mod.Loader.LocalEntryPoint, Reloaded.Mod.Loader",
        L"InitializeLocal", nullptr, nullptr, &entry_point);
    if (load_result != 0 || !entry_point) {
        log_message(log, "Could not find the game-local Reloaded II entry point.", load_result);
        return;
    }

    const auto start = std::bit_cast<component_entry_point_fn>(entry_point);
    const auto manager_name = manager.wstring();
    const auto result = start(const_cast<wchar_t*>(manager_name.c_str()),
        static_cast<int>(manager_name.size() * sizeof(wchar_t)));
    if (result == 0) log_message(log, "Reloaded II loaded the SparkingZeroAccess mod.");
    else log_message(log, "The game-local Reloaded II profile did not load.", result);
}
}

DWORD WINAPI start_reloaded(void* module) noexcept
{
    path log;
    try {
        const auto exe = module_path(nullptr);
        if (!same_name(exe.filename(), L"SparkingZERO-Win64-Shipping.exe")) return 0;
        const auto win64 = exe.parent_path();
        if (!same_name(module_path(static_cast<HMODULE>(module)).parent_path(), win64) ||
            !same_name(win64.filename(), L"Win64")) return 0;
        const auto binaries = win64.parent_path();
        const auto sparking = binaries.parent_path();
        if (!same_name(binaries.filename(), L"Binaries") || !same_name(sparking.filename(), L"SparkingZERO")) return 0;
        const auto manager = sparking.parent_path() / L"SparkingZeroAccess-Reloaded";
        const auto receipt = manager / L"Installation.json";
        const auto receipt_flags = GetFileAttributesW(receipt.c_str());
        if (receipt_flags == INVALID_FILE_ATTRIBUTES ||
            (receipt_flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return 0;

        const auto logs = manager / L"Logs";
        std::filesystem::create_directories(logs);
        log = logs / L"SparkingZeroAccess.AutoStart.log";
        {
            std::ofstream out(log, std::ios::trunc);
            out << "SparkingZeroAccess automatic startup pid=" << GetCurrentProcessId() << '\n';
        }
        if (!sparking::supported_executable(exe)) {
            log_message(log, "Unsupported executable; the reader did not load.");
            return 0;
        }
        load_local_profile(manager, log);
    } catch (...) {
        try {
            if (!log.empty()) log_message(log, "Automatic startup failed before the reader was ready.");
        } catch (...) {
            // Never turn an unavailable mod into a game startup failure.
        }
    }
    return 0;
}
