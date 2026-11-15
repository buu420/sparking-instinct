#include "profile.h"
#include "runtime.h"
#include "../speech/prism_speech.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {
using sparking::Address;
using Clock = std::chrono::steady_clock;
using Tick = void(*)(Address, float, bool);
using LoadMap = bool(*)(Address, Address, Address, Address, Address);
using namespace std::chrono_literals;

constexpr std::size_t log_record_bytes = 8 * 1024;
constexpr std::size_t log_segment_bytes = 4 * 1024 * 1024;
constexpr std::size_t queue_bytes = 256 * 1024;
constexpr std::size_t queue_records = 128;
constexpr auto log_retry = 1s;
constexpr std::string_view clipped_note = " [record truncated]";

bool continuation_byte(unsigned char byte) noexcept
{
    return (byte & 0xc0) == 0x80;
}

std::size_t utf8_length(std::string_view text) noexcept
{
    const auto first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) return 1;
    const std::size_t length = first >= 0xc2 && first <= 0xdf ? 2 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!length || text.size() < length) return 0;
    const auto second = static_cast<unsigned char>(text[1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return 0;
    for (std::size_t i = 1; i < length; ++i)
        if (!continuation_byte(static_cast<unsigned char>(text[i]))) return 0;
    return length;
}

std::string render_record(std::string_view text, bool clipped, std::int64_t elapsed)
{
    const auto suffix = " [t=" + std::to_string(elapsed) + " ms]\n";
    const auto budget = log_record_bytes - suffix.size() - clipped_note.size();
    std::string result;
    result.reserve(std::min(text.size(), budget) + suffix.size());
    while (!text.empty()) {
        const auto length = utf8_length(text);
        auto token = text.substr(0, length);
        if (!length) token = "\xef\xbf\xbd";
        else if (token == "\r") token = "\\r";
        else if (token == "\n") token = "\\n";
        else if (token == "\t") token = "\\t";
        else if (length == 1 && (token[0] < ' ' || token[0] == '\x7f')) token = "\xef\xbf\xbd";
        if (result.size() + token.size() > budget) {
            clipped = true;
            break;
        }
        result += token;
        text.remove_prefix(length ? length : 1);
    }
    if (clipped) result += clipped_note;
    result += suffix;
    return result;
}

class LogFile {
public:
    explicit LogFile(std::filesystem::path directory) : directory_(std::move(directory))
    {
        paths_[0] = directory_ / L"SparkingZeroAccess.Reloaded.log";
        for (std::size_t i = 1; i < paths_.size(); ++i)
            paths_[i] = directory_ / (L"SparkingZeroAccess.Reloaded.previous-" + std::to_wstring(i) + L".log");
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        char header[256]{};
        std::snprintf(header, sizeof(header),
            "[SparkingZeroAccess.Reloaded] Native mod start pid=%lu version=%s UTC=%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            GetCurrentProcessId(), SPARKING_MOD_VERSION, utc.wYear, utc.wMonth, utc.wDay,
            utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
        header_ = header;
    }

    ~LogFile() { close(); }
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    bool write(std::string_view line, bool clipped = false, Clock::time_point at = Clock::now()) noexcept
    {
        try {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(at - started_).count();
            return append(render_record(line, clipped, elapsed));
        } catch (...) {
            return failed();
        }
    }

private:
    bool append(std::string_view text)
    {
        if (!open()) return false;
        if (size_ + text.size() + (needs_header_ ? header_.size() + 14 : 0) > log_segment_bytes) {
            close();
            if (!rotate() || !open()) return failed();
        }
        if (needs_header_) {
            const auto header = (size_ ? "\n" : "") + header_ + (continued_ ? " (continued)\n" : "\n");
            if (!write_bytes(header)) return false;
            continued_ = true;
            needs_header_ = false;
        }
        return write_bytes(text);
    }

    bool open()
    {
        if (file_ != INVALID_HANDLE_VALUE) return true;
        if (Clock::now() < next_open_) return false;
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        const auto attributes = GetFileAttributesW(directory_.c_str());
        if (error || attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return failed();
        file_ = CreateFileW(paths_[0].c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return failed();
        BY_HANDLE_FILE_INFORMATION data{};
        LARGE_INTEGER end{};
        if (!GetFileInformationByHandle(file_, &data) || data.nNumberOfLinks != 1 ||
            (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !SetFilePointerEx(file_, {}, &end, FILE_END) || end.QuadPart < 0) return failed();
        size_ = static_cast<std::uint64_t>(end.QuadPart);
        needs_header_ = true;
        return true;
    }

    bool rotate()
    {
        std::array<bool, 4> exists{};
        for (std::size_t i = 0; i < paths_.size(); ++i) {
            const auto attributes = GetFileAttributesW(paths_[i].c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
            } else {
                if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
                exists[i] = true;
            }
        }
        for (std::size_t i = paths_.size() - 1; i > 0; --i)
            if (exists[i - 1] && !MoveFileExW(paths_[i - 1].c_str(), paths_[i].c_str(),
                                             MOVEFILE_REPLACE_EXISTING)) return false;
        return true;
    }

    bool write_bytes(std::string_view text)
    {
        DWORD written{};
        if (!WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size()) return failed();
        size_ += written;
        return true;
    }

    bool failed() noexcept
    {
        close();
        next_open_ = Clock::now() + log_retry;
        return false;
    }

    void close() noexcept
    {
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }

    const std::filesystem::path directory_;
    const Clock::time_point started_ = Clock::now();
    std::array<std::filesystem::path, 4> paths_;
    std::string header_;
    Clock::time_point next_open_{};
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::uint64_t size_{};
    bool needs_header_ = true;
    bool continued_{};
};

class LogQueue {
public:
    bool write(std::string_view line) noexcept
    {
        try {
            auto length = std::min(line.size(), log_record_bytes);
            if (length < line.size()) {
                while (length && continuation_byte(static_cast<unsigned char>(line[length]))) --length;
            }
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock || lines_.size() >= queue_records || bytes_ + length > queue_bytes) return drop();
            lines_.push_back({std::string(line.substr(0, length)), Clock::now(), length < line.size()});
            bytes_ += length;
            lock.unlock();
            ready_.notify_one();
            return true;
        } catch (...) {
            return drop();
        }
    }

    void flush(LogFile& output)
    {
        std::deque<Record> pending;
        {
            std::unique_lock lock(mutex_);
            ready_.wait_for(lock, log_retry, [&] { return !lines_.empty() || dropped_.load() != 0; });
            pending.swap(lines_);
            bytes_ = 0;
        }
        unwritten_ += dropped_.exchange(0);
        if (unwritten_ && output.write("[SparkingZeroAccess.Reloaded] Dropped " + std::to_string(unwritten_) +
                                      " diagnostic records (queue busy, full, or log unavailable)."))
            unwritten_ = 0;
        for (const auto& record : pending)
            if (!output.write(record.text, record.clipped, record.at)) ++unwritten_;
    }

private:
    struct Record {
        std::string text;
        Clock::time_point at;
        bool clipped;
    };

    bool drop() noexcept
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        ready_.notify_one();
        return false;
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Record> lines_;
    std::size_t bytes_{};
    std::atomic<std::uint64_t> dropped_{};
    std::uint64_t unwritten_{}; // writer thread only
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
            current.game_bin, [&current](std::string_view line) { return current.logs.write(line); });
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
        LogFile log(current->log_dir);
        log.write("[SparkingZeroAccess.Reloaded] Initializing native accessibility reader.");
        if (!sparking::supported_executable(executable) || !sparking::bound_image(base)) {
            log.write("Unsupported executable or unexpected native code. Reader did not start.");
            return 0;
        }
        if (GetModuleHandleW(L"UE4SS.dll")) {
            log.write("Existing UE4SS detected. Use the Reloaded-only game launch.");
            return 0;
        }
        if (!sparking::speech::start(current->mod_root / L"prism.dll",
                                     current->log_dir / L"prism.log")) {
            log.write("The Prism speech worker could not start.");
            return 0;
        }
        session.store(current.get(), std::memory_order_release);
        if (!hooks(base)) {
            session.store(nullptr, std::memory_order_release);
            log.write("The validated game-thread hooks could not be installed.");
            sparking::speech::shutdown();
            return 0;
        }
        // Reloaded retains this module until process exit. Keep engine and reader
        // state alive with the hooks and avoid audio or COM cleanup under the loader lock.
        auto* active = current.release();
        log.write("[SparkingZeroAccess.Reloaded] Steam build 24953175 verified. Game-thread hooks active.");
        for (;;) {
            try {
                active->logs.flush(log);
            } catch (...) {
                // A diagnostic allocation or sink error cannot stop the reader.
                Sleep(1000);
            }
        }
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

