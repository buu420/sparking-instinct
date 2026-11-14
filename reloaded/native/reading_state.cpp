#include "reading_state.h"

namespace sparking {
namespace {
constexpr auto tutorial_repeat = std::chrono::seconds(20);
constexpr unsigned missing_samples = 3;
}

void ReadingState::reset()
{
    *this = {};
}

bool ReadingState::screen_changed(ObjectRef root, ObjectRef focus, const std::string& value)
{
    if (value.empty()) {
        if (root != root_ || ++missing_screen_ >= missing_samples) {
            screen_.clear();
            root_ = root;
            focus_ = {};
            missing_screen_ = 0;
        }
        return false;
    }
    missing_screen_ = 0;
    if (root_ == root && focus_ == focus && screen_ == value) return false;
    root_ = root;
    focus_ = focus;
    screen_ = value;
    return true;
}

bool ReadingState::tutorial_due(const std::string& value, Clock::time_point now)
{
    if (value.empty()) {
        if (++missing_tutorial_ >= missing_samples) {
            tutorial_.clear();
            missing_tutorial_ = 0;
        }
        return false;
    }
    missing_tutorial_ = 0;
    if (tutorial_ == value && now < next_tutorial_) return false;
    tutorial_ = value;
    next_tutorial_ = now + tutorial_repeat;
    return true;
}

bool ReadingState::subtitle_changed(const std::string& value)
{
    if (value.empty()) {
        if (++missing_subtitle_ >= missing_samples) {
            subtitle_.clear();
            missing_subtitle_ = 0;
        }
        return false;
    }
    missing_subtitle_ = 0;
    if (subtitle_ == value) return false;
    subtitle_ = value;
    return true;
}

}
