#pragma once

#include "unreal.h"

#include <chrono>
#include <span>

namespace sparking {

class GameReader;

struct SubtitleLine {
    ObjectRef window{}, text_widget{}, event_window{};
    std::string text;
};

class SubtitleReader {
public:
    using Clock = std::chrono::steady_clock;

    explicit SubtitleReader(GameReader& game) : game_(game) {}
    std::string sample(std::span<const SubtitleLine> visible, bool include_voiced, Clock::time_point now,
                       std::string_view tutorial = {});
    void reset();

private:
    struct VisibleLine {
        ObjectRef window{}, text_widget{};
        std::string text;
        unsigned missing{};
    };

    bool voiced(const SubtitleLine& line, Clock::time_point now);
    void find_story_owner(ObjectRef controller, ObjectRef window, Clock::time_point now);

    GameReader& game_;
    std::vector<VisibleLine> visible_;
    ObjectRef story_window_{}, event_script_{}, paper_controller_{};
    Clock::time_point next_owner_lookup_{};
};

namespace subtitle_detail {
bool matches_voice_cue(std::string_view text_key, std::string_view playing_cue);
bool voice_is_audible(std::optional<double> status, std::optional<bool> paused, std::optional<double> volume);
}

}
