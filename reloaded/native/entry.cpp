#include "profile.h"
#include "runtime.h"
#include "../speech/prism_speech.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>

namespace {
using sparking::Address;
using Clock = std::chrono::steady_clock;
using Tick = void(*)(Address, float, bool);
using LoadMap = bool(*)(Address, Address, Address, Address, Address);

struct LogQueue {
    void write(std::string_view line)
    {
        const std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock || lines.size() >= 128) return;
        lines.emplace_back(line.substr(0, 4096));
        ready.notify_one();
    }

    void flush(std::ofstream& output)
    {
        std::deque<std::string> pending;
        {
            std::unique_lock lock(mutex);
            ready.wait_for(lock, std::chrono::seconds(1), [&] { return !lines.empty(); });
            pending.swap(lines);
        }
        for (const auto& line : pending) output << line << '\n';
        output.flush();
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> lines;
};

struct Session {
    Session(Address base, std::filesystem::path module, std::filesystem::path game)
        : engine(sparking::engine_api(base)), mod_root(module.parent_path()),
          game_bin(game.parent_path())
    {
        const auto mod = module.parent_path();
        const auto manager = mod.parent_path().parent_path();
        log_dir = _wcsicmp(manager.filename().c_str(), L"SparkingZeroAccess-Reloaded") == 0
            ? manager / L"Logs" : mod / L"Logs";
    }

    sparking::Unreal engine;
    std::filesystem::path mod_root;
    std::filesystem::path game_bin;
    std::filesystem::path log_dir;
    LogQueue logs;
    sparking::Runtime* runtime{};
    bool disabled{};
    Clock::time_point last_loader_check{};
};

std::atomic<bool> started{};
std::atomic<Session*> session{};
std::atomic<DWORD> owner_thread{};
Tick original_tick{};
LoadMap original_load_map{};

std::filesystem::path path_for(HMODULE module)
{
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return buffer;
}

bool foreground_game()
{
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

void game_frame(Session& current, Address engine)
{
    if (current.disabled) return;
    const auto now = Clock::now();
    if (now - current.last_loader_check >= std::chrono::seconds(1)) {
        current.last_loader_check = now;
        if (GetModuleHandleW(L"UE4SS.dll")) {
            if (current.runtime) current.runtime->foreground_lost();
            current.logs.write("[SparkingZeroAccess.Reloaded] Existing UE4SS detected. Reader paused.");
            current.disabled = true;
            return;
        }
    }

    current.engine.set_engine(engine);
    const bool foreground = foreground_game();
    if (!current.runtime) {
        if (!foreground || !current.engine.readable() || !current.engine.game_instance()) return;
        sparking::speech::set_foreground(true);
        auto runtime = std::make_unique<sparking::Runtime>(current.engine, current.mod_root,
            current.game_bin, [&current](std::string_view line) { current.logs.write(line); });
        current.runtime = runtime.release();
        current.logs.write("[SparkingZeroAccess.Reloaded] Sparking Instinct " SPARKING_MOD_VERSION " reader ready");
    }
    if (current.engine.faulted()) {
        current.runtime->foreground_lost();
        current.logs.write("[SparkingZeroAccess.Reloaded] Native read failed. Reader paused for this session.");
        current.disabled = true;
        return;
    }
    current.runtime->frame(foreground);
}

void tick(Address engine, float seconds, bool idle)
{
    original_tick(engine, seconds, idle);
    auto* current = session.load(std::memory_order_acquire);
    if (!current) return;
    const auto thread = GetCurrentThreadId();
    DWORD unbound{};
    owner_thread.compare_exchange_strong(unbound, thread);
    if (owner_thread.load() != thread) return;
    try {
        game_frame(*current, engine);
    } catch (...) {
        current->disabled = true;
        sparking::speech::set_foreground(false);
        current->logs.write("[SparkingZeroAccess.Reloaded] Reader exception. Reader paused for this session.");
    }
}

bool load_map(Address engine, Address context, Address url, Address pending, Address error)
{
    auto* current = session.load(std::memory_order_acquire);
    auto* runtime = current && owner_thread.load() == GetCurrentThreadId() && !current->disabled
        ? current->runtime : nullptr;
    if (runtime) {
        try {
            runtime->map_start();
        } catch (...) {
            current->disabled = true;
        }
    }
    const bool loaded = original_load_map(engine, context, url, pending, error);
    if (runtime && !current->disabled) {
        try {
            runtime->map_end();
        } catch (...) {
            current->disabled = true;
        }
    }
    return loaded;
}

bool hooks(Address base)
{
    const auto initialization = MH_Initialize();
    if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto* tick_address = reinterpret_cast<void*>(base + sparking::tick_rva);
    auto* map_address = reinterpret_cast<void*>(base + sparking::load_map_rva);
    void* tick_original{};
    void* map_original{};
    if (MH_CreateHook(tick_address, reinterpret_cast<void*>(&tick), &tick_original) != MH_OK)
        return false;
    if (MH_CreateHook(map_address, reinterpret_cast<void*>(&load_map), &map_original) != MH_OK) {
        MH_RemoveHook(tick_address);
        return false;
    }
    original_tick = reinterpret_cast<Tick>(tick_original);
    original_load_map = reinterpret_cast<LoadMap>(map_original);
    if (MH_QueueEnableHook(tick_address) == MH_OK &&
        MH_QueueEnableHook(map_address) == MH_OK && MH_ApplyQueued() == MH_OK) return true;
    MH_RemoveHook(tick_address);
    MH_RemoveHook(map_address);
    return false;
}

DWORD WINAPI initialize(void* module)
{
    try {
        const auto own_file = path_for(static_cast<HMODULE>(module));
        const auto executable = path_for(nullptr);
        if (own_file.empty() || executable.empty()) return 0;
        const auto base = reinterpret_cast<Address>(GetModuleHandleW(nullptr));
        auto current = std::make_unique<Session>(base, own_file, executable);
        std::error_code path_error;
        std::filesystem::create_directories(current->log_dir, path_error);
        std::ofstream log(current->log_dir / L"SparkingZeroAccess.Reloaded.log", std::ios::trunc);
        if (!log) return 0;
        log << "[SparkingZeroAccess.Reloaded] Native mod start pid=" << GetCurrentProcessId()
            << '\n' << std::flush;
        if (!sparking::supported_executable(executable) || !sparking::bound_image(base)) {
            log << "Unsupported executable or unexpected native code. Reader did not start.\n";
            return 0;
        }
        if (GetModuleHandleW(L"UE4SS.dll")) {
            log << "Existing UE4SS detected. Use the Reloaded-only game launch.\n";
            return 0;
        }
        if (!sparking::speech::start(current->mod_root / L"prism.dll",
                                     current->log_dir / L"prism.log")) {
            log << "The Prism speech worker could not start.\n";
            return 0;
        }
        session.store(current.get(), std::memory_order_release);
        if (!hooks(base)) {
            session.store(nullptr, std::memory_order_release);
            log << "The validated game-thread hooks could not be installed.\n";
            sparking::speech::shutdown();
            return 0;
        }
        // Reloaded retains this module until process exit. Keep engine and reader
        // state alive with the hooks and avoid audio or COM cleanup under the loader lock.
        auto* active = current.release();
        log << "[SparkingZeroAccess.Reloaded] Steam build 24953175 verified. Game-thread hooks active.\n"
            << std::flush;
        for (;;) active->logs.flush(log);
    } catch (...) {
        OutputDebugStringW(L"SparkingZeroAccess Reloaded initialization failed.");
    }
    return 0;
}
}

extern "C" __declspec(dllexport) void ReloadedStart()
{
    if (started.exchange(true)) return;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&ReloadedStart), &module)) return;
    const auto worker = CreateThread(nullptr, 0, initialize, module, 0, nullptr);
    if (worker) CloseHandle(worker);
}

extern "C" __declspec(dllexport) bool ReloadedCanUnload() { return false; }
extern "C" __declspec(dllexport) bool ReloadedCanSuspend() { return false; }

