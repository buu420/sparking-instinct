#pragma once

#include "battle_reader.h"
#include "game_reader.h"
#include "movie_reader.h"
#include "reading_state.h"
#include "ui_reader.h"

#include <array>
#include <filesystem>
#include <functional>

namespace sparking {

class Runtime {
public:
    using Report = std::function<void(std::string_view)>;

    Runtime(Unreal& engine, const std::filesystem::path& mod,
            const std::filesystem::path& game_bin, Report report);
    void frame(bool foreground);
    void map_start();
    void map_end();
    void foreground_lost();

private:
    using Clock = ReadingState::Clock;

    void reset();
    void sample(Clock::time_point now);
    void sample_keys(bool dispatch);
    void hotkey(int key);
    bool speak(std::string_view message, bool interrupt = true);
    void review(int direction);
    void dump(bool battle);
    void toggle_subtitles();

    Unreal& engine_;
    GameReader game_;
    UiReader ui_;
    BattleReader battle_;
    MovieReader movie_;
    ReadingState reading_;
    std::filesystem::path preference_;
    Report report_;
    ScreenReading screen_{};
    BattleReading hud_{};
    std::vector<std::string> items_;
    std::size_t review_index_{};
    Clock::time_point next_{};
    Clock::time_point next_movie_{};
    Clock::time_point stats_since_{};
    Clock::time_point next_battle_speech_{};
    unsigned map_depth_{};
    unsigned polls_{};
    double total_ms_{}, peak_ms_{};
    bool foreground_{}, movie_visible_{}, debug_{};
    bool subtitles_{true};
    std::array<bool, 7> down_{};
};

}
