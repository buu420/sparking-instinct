#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmreg.h>
#include <dsound.h>
#include <tlhelp32.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace {
struct Export {
    WORD ordinal;
    const char* name;
};

constexpr std::array<Export, 12> exports{{
    {1, "DirectSoundCreate"},
    {2, "DirectSoundEnumerateA"},
    {3, "DirectSoundEnumerateW"},
    {4, "DllCanUnloadNow"},
    {5, "DllGetClassObject"},
    {6, "DirectSoundCaptureCreate"},
    {7, "DirectSoundCaptureEnumerateA"},
    {8, "DirectSoundCaptureEnumerateW"},
    {9, "GetDeviceID"},
    {10, "DirectSoundFullDuplexCreate"},
    {11, "DirectSoundCreate8"},
    {12, "DirectSoundCaptureCreate8"},
}};

struct Handle {
    HANDLE value;

    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

struct Library {
    HMODULE value;

    ~Library() {
        if (value) FreeLibrary(value);
    }
};

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

fs::path module_path(HMODULE module)
{
    std::wstring value(32768, L'\0');
    const auto length = GetModuleFileNameW(module, value.data(), static_cast<DWORD>(value.size()));
    require(length && length < value.size(), "Could not locate a loaded module");
    value.resize(length);
    return value;
}

fs::path system_dsound_path()
{
    std::wstring value(32768, L'\0');
    const auto length = GetSystemDirectoryW(value.data(), static_cast<UINT>(value.size()));
    require(length && length < value.size(), "Could not locate System32");
    value.resize(length);
    return fs::path(value) / L"dsound.dll";
}

void check_exports(HMODULE module)
{
    for (const auto& entry : exports) {
        const auto named = GetProcAddress(module, entry.name);
        const auto numbered = GetProcAddress(module, MAKEINTRESOURCEA(entry.ordinal));
        require(named && named == numbered, std::string("Name/ordinal mismatch for ") + entry.name);
    }
}

void check_no_reloaded()
{
    const Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId())};
    require(snapshot.value != INVALID_HANDLE_VALUE, "Could not enumerate the fixture's loaded modules");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    require(Module32FirstW(snapshot.value, &entry), "Could not read the fixture's loaded modules");
    do {
        const std::wstring name(entry.szModule);
        require(_wcsicmp(name.c_str(), L"coreclr.dll") != 0 &&
                _wcsicmp(name.c_str(), L"hostfxr.dll") != 0 &&
                _wcsnicmp(name.c_str(), L"Reloaded.", 9) != 0,
                "An unsupported fixture loaded Reloaded or the .NET host");
    } while (Module32NextW(snapshot.value, &entry));
}

fs::path startup_log(const fs::path& executable)
{
    return executable.parent_path().parent_path().parent_path().parent_path() /
        L"SparkingZeroAccess-Reloaded" / L"Logs" / L"SparkingZeroAccess.AutoStart.log";
}

void await_rejection(const fs::path& log)
{
    constexpr std::string_view rejection = "Unsupported executable; the reader did not load.";
    const auto start = GetTickCount64();
    do {
        std::ifstream input(log);
        if (input) {
            const std::string content(std::istreambuf_iterator<char>(input), {});
            if (content.find(rejection) != std::string::npos) return;
        }
        Sleep(20);
    } while (GetTickCount64() - start < 4000);
    throw std::runtime_error("Expected startup refusal did not arrive");
}

struct DeviceKey {
    bool default_device;
    std::array<unsigned char, sizeof(GUID)> id;

    auto operator<=>(const DeviceKey&) const = default;
};

using DeviceCounts = std::map<DeviceKey, unsigned>;

struct Enumeration {
    HRESULT result = E_FAIL;
    DeviceCounts devices;
    bool callback_failed = false;
};

BOOL CALLBACK on_device(LPGUID guid, LPCWSTR, LPCWSTR, LPVOID context) noexcept
{
    auto& enumeration = *static_cast<Enumeration*>(context);
    try {
        DeviceKey device{guid == nullptr, {}};
        if (guid) {
            std::memcpy(device.id.data(), guid, device.id.size());
        }
        ++enumeration.devices[std::move(device)];
    } catch (...) {
        enumeration.callback_failed = true;
        return FALSE;
    }
    return TRUE;
}

using Enumerator = HRESULT (WINAPI*)(LPDSENUMCALLBACKW, LPVOID);

Enumeration enumerate(Enumerator function)
{
    require(function != nullptr, "Enumerator export is unavailable");
    Enumeration enumeration;
    enumeration.result = function(on_device, &enumeration);
    require(!enumeration.callback_failed, "Enumeration callback could not record results");
    return enumeration;
}

void compare_enumeration(const char* name, Enumerator forwarded, HMODULE system,
                         const Enumeration& initial)
{
    const auto expected_call = reinterpret_cast<Enumerator>(GetProcAddress(system, name));
    require(expected_call != nullptr, "System32 enumerator export is unavailable");

    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto before = enumerate(expected_call);
        const auto actual = enumerate(forwarded);
        const auto after = enumerate(expected_call);
        if (before.result != after.result || before.devices != after.devices) {
            continue; // Windows can add or remove endpoints while the fixture runs.
        }
        require(initial.result == before.result, std::string(name) + " first static import call returned a different HRESULT");
        require(initial.devices == before.devices, std::string(name) + " first static import call returned different endpoint records");
        require(actual.result == before.result, std::string(name) + " returned a different HRESULT");
        require(actual.devices == before.devices, std::string(name) + " returned different endpoint records");
        std::cout << name << " matched System32 (HRESULT 0x" << std::hex
                  << static_cast<unsigned long>(actual.result) << std::dec << ", "
                  << actual.devices.size() << " distinct endpoint records)\n";
        return;
    }
    throw std::runtime_error(std::string(name) + " System32 endpoints did not stabilize");
}
}

int wmain(int argc, wchar_t** argv)
{
    try {
        const auto executable = module_path(nullptr);
        const auto proxy_file = executable.parent_path() / L"dsound.dll";
#ifdef SPARKING_DSOUND_DYNAMIC_FIXTURE
        require(argc == 2 && std::wstring_view(argv[1]) == L"--dynamic-gate",
                "Expected the dynamic startup gate fixture option");
        require(!GetModuleHandleW(proxy_file.c_str()), "The dynamic fixture imported the proxy statically");
        const Library proxy{LoadLibraryExW(proxy_file.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
        require(proxy.value && fs::equivalent(proxy_file, module_path(proxy.value)),
                "The dynamic fixture could not load its adjacent dsound.dll");
        const auto log = startup_log(executable);
        Sleep(300);
        require(!fs::exists(log) && !GetModuleHandleW(system_dsound_path().c_str()),
                "The dynamic proxy started before its first DirectSound export call");
        check_exports(proxy.value);
        const auto playback = reinterpret_cast<Enumerator>(GetProcAddress(proxy.value, "DirectSoundEnumerateW"));
        const auto first_playback = enumerate(playback);
        const auto system = GetModuleHandleW(system_dsound_path().c_str());
        require(system && system != proxy.value, "The first dynamic call did not load System32 DirectSound");
        compare_enumeration("DirectSoundEnumerateW", playback, system, first_playback);
        await_rejection(log);
        check_no_reloaded();
        std::cout << "Dynamic loading deferred startup until the first DirectSound call\n";
#else
        const auto proxy = GetModuleHandleW(proxy_file.c_str());
        require(proxy && fs::equivalent(proxy_file, module_path(proxy)),
                "Windows did not load the adjacent dsound.dll for the executable import");
        const bool watching = argc == 2 && std::wstring_view(argv[1]) == L"--watch-bootstrap";
        const bool load_only = argc == 2 && std::wstring_view(argv[1]) == L"--load-only";
        require(argc == 1 || watching || load_only, "Unexpected fixture arguments");
        if (load_only) {
            await_rejection(startup_log(executable));
            require(!GetModuleHandleW(system_dsound_path().c_str()),
                    "The proxy loaded System32 audio before its first export call");
            check_no_reloaded();
            std::cout << "Static loading started the bootstrap before the first DirectSound call\n";
            return 0;
        }
        const auto playback = reinterpret_cast<Enumerator>(GetProcAddress(proxy, "DirectSoundEnumerateW"));
        const auto capture = reinterpret_cast<Enumerator>(GetProcAddress(proxy, "DirectSoundCaptureEnumerateW"));
        const auto first_playback = enumerate(DirectSoundEnumerateW);
        const auto first_capture = enumerate(DirectSoundCaptureEnumerateW);
        check_exports(proxy);

        // The process already has an adjacent dsound.dll. The full path must
        // select the actual system implementation rather than return that module.
        const auto system_file = system_dsound_path();
        const Library system{LoadLibraryExW(system_file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
        require(system.value && system.value != proxy && fs::equivalent(system_file, module_path(system.value)),
                "An absolute System32 load selected the adjacent forwarding DLL");
        check_exports(system.value);
        compare_enumeration("DirectSoundEnumerateW", playback, system.value, first_playback);
        compare_enumeration("DirectSoundCaptureEnumerateW", capture, system.value, first_capture);

        // The shipping game's remaining DirectSound imports are the two create
        // calls. Null output arguments exercise forwarding without starting audio.
        const auto create = reinterpret_cast<decltype(&DirectSoundCreate)>(GetProcAddress(system.value, "DirectSoundCreate"));
        const auto create_capture = reinterpret_cast<decltype(&DirectSoundCaptureCreate)>(
            GetProcAddress(system.value, "DirectSoundCaptureCreate"));
        const auto render_forward_error = DirectSoundCreate(nullptr, nullptr, nullptr);
        const auto capture_forward_error = DirectSoundCaptureCreate(nullptr, nullptr, nullptr);
        const auto render_error = create(nullptr, nullptr, nullptr);
        const auto capture_error = create_capture(nullptr, nullptr, nullptr);
        require(FAILED(render_error) && render_forward_error == render_error,
                "DirectSoundCreate static import did not preserve the System32 argument error");
        require(FAILED(capture_error) && capture_forward_error == capture_error,
                "DirectSoundCaptureCreate static import did not preserve the System32 argument error");
        std::cout << "DirectSoundCreate and DirectSoundCaptureCreate matched the System32 argument errors\n";

        if (watching) {
            // Let the denied startup worker finish before checking module state.
            Sleep(1000);
        }
        check_no_reloaded();
        std::cout << "Local static import, all 12 named and ordinal exports, and System32 coexistence passed\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
