#pragma once

#include "unreal.h"

#include <span>

namespace sparking {

class GameReader;

struct ScreenReading {
    ObjectRef root{}, focus{};
    std::vector<std::string> items;
    std::string announcement, tutorial, subtitle;
};

std::string read_ui_text(std::string_view rendered_text);

namespace ui_detail {
bool matches_menu_visual(std::string_view root, std::string_view hit, std::string_view visual);
}

class UiReader {
public:
    explicit UiReader(GameReader& game) : game_(game) {}
    ScreenReading sample(std::span<const ObjectRef> roots, bool cinematic = false);
    void reset();

private:
    struct ControlDetails {
        std::array<std::string, 4> lines;
        bool operator==(const ControlDetails&) const = default;
    };

    ControlDetails settled_controls(ObjectRef panel, ObjectRef focus, ControlDetails observed);

    GameReader& game_;
    ObjectRef control_panel_{}, control_focus_{};
    ControlDetails observed_controls_{}, previous_controls_{};
    unsigned control_samples_{};
};

}
