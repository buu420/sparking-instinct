#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

namespace sparking::speech {

struct Message {
    enum class Kind { idle, say, stop };

    Kind kind = Kind::idle;
    std::string text;
    bool interrupt = false;
    std::uint64_t generation = 0;
};

class SpeechMailbox {
public:
    static constexpr std::size_t maximum_messages = 64;
    static constexpr std::size_t maximum_text_bytes = 64 * 1024;

    bool say(std::string_view text, bool interrupt)
    {
        if (text.empty() || text.size() > maximum_text_bytes || text.find('\0') != text.npos) {
            return false;
        }

        std::unique_lock lock(mutex_);
        if (!foreground_ || closed_ || (!interrupt && pending_.size() >= maximum_messages)) {
            return false;
        }
        if (interrupt) {
            discard_pending();
        }
        pending_.push_back({Message::Kind::say, std::string(text), interrupt, generation_});
        lock.unlock();
        changed_.notify_one();
        return true;
    }

    void stop()
    {
        std::unique_lock lock(mutex_);
        if (!foreground_ || closed_) {
            return;
        }
        discard_pending();
        stop_pending_ = true;
        lock.unlock();
        changed_.notify_one();
    }

    void set_foreground(bool foreground)
    {
        std::unique_lock lock(mutex_);
        if (foreground_ && !foreground) {
            discard_pending();
            stop_pending_ = true;
        }
        foreground_ = foreground;
        const bool notify = stop_pending_;
        lock.unlock();
        if (notify) changed_.notify_one();
    }

    bool next(Message& message, std::chrono::milliseconds wait)
    {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, wait, [this] {
            return closed_ || stop_pending_ || !pending_.empty();
        });
        if (closed_) {
            return false;
        }
        if (stop_pending_) {
            message = {Message::Kind::stop, {}, false, generation_};
            stop_pending_ = false;
        } else if (!pending_.empty()) {
            message = std::move(pending_.front());
            pending_.pop_front();
        } else {
            message = {};
        }
        return true;
    }

    bool can_deliver(const Message& message) const
    {
        std::lock_guard lock(mutex_);
        return !closed_ && (message.kind == Message::Kind::stop ||
                            (foreground_ && message.generation == generation_));
    }

    bool has_speech() const
    {
        std::lock_guard lock(mutex_);
        return foreground_ && !pending_.empty();
    }

    void close()
    {
        std::unique_lock lock(mutex_);
        closed_ = true;
        discard_pending();
        stop_pending_ = false;
        lock.unlock();
        changed_.notify_one();
    }

private:
    // The caller owns mutex_ while replacing or invalidating pending work.
    void discard_pending()
    {
        ++generation_;
        pending_.clear();
    }

    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Message> pending_;
    std::uint64_t generation_ = 0;
    bool foreground_ = false;
    bool stop_pending_ = false;
    bool closed_ = false;
};

}
