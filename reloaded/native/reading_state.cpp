#include "reading_state.h"

#include <algorithm>

namespace sparking {
namespace {
constexpr auto tutorial_repeat = std::chrono::seconds(20);
constexpr unsigned missing_samples = 3;
constexpr auto hud_value_lifetime = std::chrono::seconds(6);
constexpr auto hud_transition_lifetime = std::chrono::milliseconds(650);
constexpr std::size_t maximum_hud_lines = 24;
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

void ReadingState::update_hud(std::span<const std::string> visible, std::span<const std::string> changes,
                              Clock::time_point now)
{
    if (visible.empty()) {
        clear_hud();
        return;
    }
    const auto is_visible = [&](const std::string& text) {
        return std::ranges::find(visible, text) != visible.end();
    };
    std::erase_if(hud_, [&](const HudLine& line) {
        return now >= line.expiry || (line.needs_visible_item && !is_visible(line.text));
    });
    for (const auto& text : changes) {
        if (text.empty() || std::ranges::find(hud_, text, &HudLine::text) != hud_.end()) continue;
        if (hud_.size() >= maximum_hud_lines) break;
        const bool meter = is_visible(text);
        hud_.push_back({text, now + (meter ? hud_value_lifetime : hud_transition_lifetime), meter});
    }
}

std::string ReadingState::pending_hud() const
{
    std::string text;
    for (const auto& line : hud_) {
        if (!text.empty()) text += ". ";
        text += line.text;
    }
    return text;
}

}
