#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <dsound.h>

#include <bit>
#include <cwchar>

DWORD WINAPI start_reloaded(void* module) noexcept;

namespace {
HMODULE own_module{};
HMODULE system_audio{};
bool static_worker_started{};
INIT_ONCE audio_once = INIT_ONCE_STATIC_INIT;
INIT_ONCE worker_once = INIT_ONCE_STATIC_INIT;

DWORD WINAPI run_worker(void* reference)
{
    start_reloaded(reference);
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference), 0);
}

BOOL CALLBACK start_worker(PINIT_ONCE, void*, void**)
{
    if (static_worker_started) return TRUE;
    HMODULE reference{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&run_worker), &reference)) {
        if (const auto worker = CreateThread(nullptr, 0, run_worker, reference, 0, nullptr))
            CloseHandle(worker);
        else
            FreeLibrary(reference);
    }
    return TRUE;
}

BOOL CALLBACK find_system_audio(PINIT_ONCE, void*, void**)
{
    wchar_t path[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(path, MAX_PATH);
    if (length && length < MAX_PATH && wcscat_s(path, L"\\dsound.dll") == 0) {
        const auto module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != own_module) system_audio = module;
    }
    return TRUE;
}

template<class Function>
Function system_export(const char* name)
{
    InitOnceExecuteOnce(&worker_once, start_worker, nullptr, nullptr);
    InitOnceExecuteOnce(&audio_once, find_system_audio, nullptr, nullptr);
    return system_audio ? std::bit_cast<Function>(GetProcAddress(system_audio, name)) : nullptr;
}

constexpr HRESULT missing_export = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

// Preserve System32 signatures and ordinals. A dynamic load also has a
// first-call path, with an independent DLL reference for the worker.
#define FORWARD(name, parameters, arguments) \
    extern "C" HRESULT WINAPI name parameters \
    { \
        static const auto call = system_export<decltype(&name)>(#name); \
        return call ? call arguments : missing_export; \
    }

FORWARD(DirectSoundCreate, (LPCGUID device, LPDIRECTSOUND* out, LPUNKNOWN outer), (device, out, outer))
FORWARD(DirectSoundCreate8, (LPCGUID device, LPDIRECTSOUND8* out, LPUNKNOWN outer), (device, out, outer))
FORWARD(DirectSoundEnumerateA, (LPDSENUMCALLBACKA callback, LPVOID data), (callback, data))
FORWARD(DirectSoundEnumerateW, (LPDSENUMCALLBACKW callback, LPVOID data), (callback, data))
FORWARD(DirectSoundCaptureCreate, (LPCGUID device, LPDIRECTSOUNDCAPTURE* out, LPUNKNOWN outer), (device, out, outer))
FORWARD(DirectSoundCaptureCreate8, (LPCGUID device, LPDIRECTSOUNDCAPTURE8* out, LPUNKNOWN outer), (device, out, outer))
FORWARD(DirectSoundCaptureEnumerateA, (LPDSENUMCALLBACKA callback, LPVOID data), (callback, data))
FORWARD(DirectSoundCaptureEnumerateW, (LPDSENUMCALLBACKW callback, LPVOID data), (callback, data))
FORWARD(GetDeviceID, (LPCGUID source, LPGUID destination), (source, destination))
FORWARD(DllGetClassObject, (REFCLSID clsid, REFIID iid, LPVOID* out), (clsid, iid, out))
FORWARD(DirectSoundFullDuplexCreate,
    (LPCGUID capture, LPCGUID render, LPCDSCBUFFERDESC capture_desc, LPCDSBUFFERDESC render_desc,
     HWND window, DWORD level, LPDIRECTSOUNDFULLDUPLEX* duplex, LPDIRECTSOUNDCAPTUREBUFFER8* capture_buffer,
     LPDIRECTSOUNDBUFFER8* render_buffer, LPUNKNOWN outer),
    (capture, render, capture_desc, render_desc, window, level, duplex, capture_buffer, render_buffer, outer))

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    static const auto call = system_export<decltype(&DllCanUnloadNow)>("DllCanUnloadNow");
    return call ? call() : S_FALSE;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void* static_load)
{
    if (reason == DLL_PROCESS_ATTACH) {
        own_module = module;
        // The game imports this DLL but can use a different audio backend.
        // At process startup the import holds the module alive. Do not wait:
        // Windows starts the worker after the DLL attach callbacks finish.
        if (static_load) {
            if (const auto worker = CreateThread(nullptr, 0, start_reloaded, module, 0, nullptr)) {
                static_worker_started = true;
                CloseHandle(worker);
            }
        }
    }
    return TRUE;
}
