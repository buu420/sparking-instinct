#include "prism_speech.h"
#include "prism_api.h"
#include "speech_mailbox.h"

#include <objbase.h>

#include <array>
#include <atomic>
#include <fstream>
#include <memory>
#include <thread>

namespace sparking::speech {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

bool game_has_foreground() noexcept
{
    DWORD owner = 0;
    if (const auto window = GetForegroundWindow()) {
        GetWindowThreadProcessId(window, &owner);
    }
    return owner == GetCurrentProcessId();
}

class ComApartment {
public:
    ComApartment() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComApartment() { if (initialized_) CoUninitialize(); }

private:
    bool initialized_;
};

class BackendSession {
public:
    explicit BackendSession(PrismApi& api) : api_(api)
    {
        if (auto config = api_.make_config()) {
            config->availability_callback = availability_changed;
            config->availability_userdata = this;
            config->availability_backoff_max_ms = 8000;
            context_ = api_.init(&*config);
        }
    }

    ~BackendSession()
    {
        if (backend_) {
            api_.free_backend(backend_);
        }
        if (context_) {
            // Prism joins its availability watcher before this session is released.
            api_.shutdown(context_);
        }
    }

    explicit operator bool() const { return context_ != nullptr; }
    PrismBackend* backend() const { return backend_; }
    bool changed() { return changed_.exchange(false, std::memory_order_acq_rel); }

    bool select(bool recreate)
    {
        auto* replacement = api_.create_best(context_);
        if (!replacement) {
            return false;
        }
        if (backend_) {
            if (!recreate && name(replacement) == name(backend_)) {
                api_.free_backend(replacement);
                return false;
            }
            api_.free_backend(backend_);
        }
        backend_ = replacement;
        return true;
    }

    std::string_view name() const { return name(backend_); }

private:
    static void PRISM_CALL availability_changed(void* userdata, PrismBackendId, const char*, bool)
    {
        static_cast<BackendSession*>(userdata)->changed_.store(true, std::memory_order_release);
    }

    std::string_view name(PrismBackend* backend) const
    {
        const char* value = backend ? api_.name(backend) : nullptr;
        return value ? value : "";
    }

    PrismApi& api_;
    PrismContext* context_ = nullptr;
    PrismBackend* backend_ = nullptr;
    std::atomic<bool> changed_{false};
};

class Worker {
public:
    Worker(const std::filesystem::path& dll, const std::filesystem::path& log)
        : dll_(dll), log_path_(log) {}

    void launch() { thread_ = std::thread([this] { run(); }); }
    void close()
    {
        mailbox_.close();
        thread_.join();
    }

    bool say(std::string_view text, bool interrupt) { return mailbox_.say(text, interrupt); }
    void stop() { mailbox_.stop(); }
    void foreground(bool active)
    {
        mailbox_.set_foreground(active);
        if (!active) {
            speaking_.store(false, std::memory_order_relaxed);
        }
    }
    bool speaking()
    {
        check_speaking_.store(true, std::memory_order_relaxed);
        return game_has_foreground() && (mailbox_.has_speech() || speaking_.load(std::memory_order_relaxed));
    }
    std::array<char, 128> name() const
    {
        std::lock_guard lock(name_mutex_);
        return backend_name_;
    }

private:
    void report(const std::string& text)
    {
        if (log_) {
            log_ << text << '\n' << std::flush;
        }
        OutputDebugStringA(("[SparkingZeroAccess] " + text + '\n').c_str());
    }

    void report_selection(const BackendSession& session)
    {
        const auto name = session.name();
        {
            std::lock_guard lock(name_mutex_);
            backend_name_.fill('\0');
            name.copy(backend_name_.data(), backend_name_.size() - 1);
        }
        if (session.backend()) {
            report("Prism " PRISM_VERSION_STRING " backend: " + std::string(name));
        } else {
            report("Prism has no usable screen reader yet");
        }
    }

    bool can_deliver(const Message& message) const
    {
        return mailbox_.can_deliver(message) &&
               (message.kind == Message::Kind::stop || game_has_foreground());
    }

    void update_speaking(const PrismApi& api, PrismBackend* backend)
    {
        bool speaking = false;
        if (backend && game_has_foreground() && api.is_speaking(backend, &speaking) != PRISM_OK) {
            speaking = false;
        }
        speaking_.store(speaking, std::memory_order_relaxed);
    }

    void run() noexcept
    {
        try {
            if (!log_path_.empty()) {
                log_.open(log_path_, std::ios::app);
            }
            run_prism();
        } catch (const std::exception& error) {
            try {
                report(std::string("Prism worker failed: ") + error.what());
            } catch (...) {
            }
        } catch (...) {
            OutputDebugStringA("[SparkingZeroAccess] Prism worker failed\n");
        }
        mailbox_.close();
        speaking_.store(false, std::memory_order_relaxed);
    }

    void run_prism()
    {
        ComApartment com;
        PrismApi api(dll_);
        if (!api) {
            report(std::string(api.error()) + " (Win32 " + std::to_string(api.load_error()) + ')');
            return;
        }

        BackendSession session(api);
        if (!session) {
            report("Prism context initialization failed");
            return;
        }

        session.select(false);
        report_selection(session);
        auto retry_at = Clock::now() + 1s;
        bool retry_backend = false;
        PrismError last_error = PRISM_OK;
        Message message;
        while (mailbox_.next(message, 200ms)) {
            if (message.kind == Message::Kind::idle || !can_deliver(message)) {
                if (check_speaking_.exchange(false, std::memory_order_relaxed)) {
                    update_speaking(api, session.backend());
                }
                continue;
            }
            if (message.kind == Message::Kind::stop) {
                if (session.backend()) {
                    static_cast<void>(api.stop(session.backend()));
                }
                speaking_.store(false, std::memory_order_relaxed);
                continue;
            }

            const auto now = Clock::now();
            if (session.changed() || ((!session.backend() || retry_backend) && now >= retry_at)) {
                if (session.select(retry_backend)) {
                    report_selection(session);
                }
                retry_at = now + 1s;
                retry_backend = false;
            }
            // Selecting a backend can take long enough for the window or focus row to change.
            if (!session.backend() || !can_deliver(message)) {
                continue;
            }
            const auto result = api.speak(session.backend(), message.text.c_str(), message.interrupt);
            if (result != PRISM_OK && result != last_error) {
                report("Prism speech failed (error " + std::to_string(static_cast<int>(result)) + ')');
            }
            last_error = result;
            if (result == PRISM_ERROR_NOT_INITIALIZED || result == PRISM_ERROR_BACKEND_NOT_AVAILABLE ||
                result == PRISM_ERROR_SPEAK_FAILURE || result == PRISM_ERROR_INTERNAL ||
                result == PRISM_ERROR_BACKEND_ENTERED_UNDEFINED_STATE) {
                retry_backend = true;
            }
            if (check_speaking_.exchange(false, std::memory_order_relaxed)) {
                update_speaking(api, session.backend());
            }
        }
    }

    const std::filesystem::path dll_;
    const std::filesystem::path log_path_;
    std::thread thread_;
    SpeechMailbox mailbox_;
    std::atomic<bool> speaking_{false};
    std::atomic<bool> check_speaking_{false};
    mutable std::mutex name_mutex_;
    std::array<char, 128> backend_name_{};
    std::ofstream log_;
};

struct Service {
    std::mutex lifecycle;
    std::atomic<std::shared_ptr<Worker>> active;
};

std::atomic<Service*> initialized_service{nullptr};

Service& initialize_service()
{
    // The host is resident for the game process. A DLL static destructor must
    // never try to join Prism while Windows holds the loader lock.
    static auto* const service = [] {
        auto* value = new Service;
        initialized_service.store(value, std::memory_order_release);
        return value;
    }();
    return *service;
}

std::shared_ptr<Worker> worker() noexcept
{
    auto* service = initialized_service.load(std::memory_order_acquire);
    return service ? service->active.load() : nullptr;
}

}

bool start(const std::filesystem::path& dll, const std::filesystem::path& log) noexcept
{
    if (!dll.is_absolute()) {
        return false;
    }
    try {
        auto& service = initialize_service();
        std::lock_guard lock(service.lifecycle);
        if (!service.active.load()) {
            auto instance = std::make_shared<Worker>(dll, log);
            instance->launch();
            service.active.store(instance);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool say(std::string_view text, bool interrupt) noexcept
{
    try {
        if (auto instance = worker()) return instance->say(text, interrupt);
    } catch (...) {
    }
    return false;
}

void stop() noexcept
{
    if (auto instance = worker()) instance->stop();
}

bool speaking() noexcept
{
    if (auto instance = worker()) return instance->speaking();
    return false;
}


void set_foreground(bool foreground) noexcept
{
    if (const auto instance = worker()) {
        instance->foreground(foreground);
    }
}

void shutdown() noexcept
{
    if (auto* service = initialized_service.load(std::memory_order_acquire)) {
        std::lock_guard lock(service->lifecycle);
        if (const auto instance = service->active.exchange(nullptr)) {
            instance->close();
        }
    }
}

}
