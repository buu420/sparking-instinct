#pragma once

#include "unreal.h"

#include <chrono>
#include <span>
#include <string>

namespace sparking {

class ReadingState {
public:
    using Clock = std::chrono::steady_clock;

    void reset();
    bool screen_changed(ObjectRef root, ObjectRef focus, const std::string& value);
    bool control_details_changed(const std::string& value, bool selection_changed);
    bool tutorial_due(const std::string& value, Clock::time_point now);
    bool battle_help_changed(const std::string& value);
    void update_hud(std::span<const std::string> visible, std::span<const std::string> changes,
                    Clock::time_point now);
    std::string pending_hud() const;
    void clear_hud() { hud_.clear(); }

private:
    struct HudLine {
        std::string text;
        Clock::time_point expiry;
        bool needs_visible_item;
    };
    ObjectRef root_{};
    ObjectRef focus_{};
    std::string screen_;
    std::string control_details_;
    std::string tutorial_;
    std::string battle_help_;
    std::vector<HudLine> hud_;
    Clock::time_point next_tutorial_{};
    unsigned missing_screen_{};
    unsigned missing_control_details_{};
    unsigned missing_tutorial_{};
    unsigned missing_battle_help_{};
};

}
