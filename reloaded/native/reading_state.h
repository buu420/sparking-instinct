#pragma once

#include "unreal.h"

#include <chrono>
#include <string>

namespace sparking {

class ReadingState {
public:
    using Clock = std::chrono::steady_clock;

    void reset();
    bool screen_changed(ObjectRef root, ObjectRef focus, const std::string& value);
    bool tutorial_due(const std::string& value, Clock::time_point now);
    bool subtitle_changed(const std::string& value);

private:
    ObjectRef root_{};
    ObjectRef focus_{};
    std::string screen_;
    std::string tutorial_;
    std::string subtitle_;
    Clock::time_point next_tutorial_{};
    unsigned missing_screen_{};
    unsigned missing_tutorial_{};
    unsigned missing_subtitle_{};
};

}
