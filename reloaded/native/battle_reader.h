#pragma once

#include "unreal.h"

#include <chrono>
#include <span>

namespace sparking {

class GameReader;

struct BattleReading {
    bool active{};
    bool guide_visible{};
    bool guide_opened{};
    std::string guide;
    std::vector<std::string> items;
    std::vector<std::string> announcements;
};

class BattleReader {
public:
    using Clock = std::chrono::steady_clock;

    explicit BattleReader(GameReader& game) : game_(game) {}
    BattleReading sample(std::span<const ObjectRef> roots, Clock::time_point now = Clock::now());
    void reset();

private:
    struct Fighter {
        ObjectRef pawn{};
        std::optional<int> health, ki, skill;
        ObjectRef sparking_meter{};
        std::optional<bool> sparking;
        std::string combo;
        bool combo_announced{};
    };

    void rearm_guide();
    GameReader& game_;
    ObjectRef hud_{}, controller_{}, timer_{}, guide_pawn_{};
    std::array<Fighter, 2> fighters_{};
    std::optional<int> time_;
    std::optional<double> hold_time_;
    std::optional<Clock::time_point> guide_hidden_since_;
    std::optional<Clock::time_point> guide_first_row_;
    std::array<bool, 5> guide_announced_{};
    bool guide_ultimate_button_announced_{};
    std::string last_result_;
    unsigned result_missing_{};
};

}
