#pragma once

#include "subtitle_reader.h"

#include <span>

namespace sparking {

class GameReader;

struct ScreenReading {
    ObjectRef root{}, focus{};
    std::vector<std::string> items;
    std::vector<std::string> visible_surfaces;
    std::string announcement, tutorial, battle_help;
    std::vector<SubtitleLine> subtitles;
    std::string control_details, selection_details;
};

std::string read_ui_text(std::string_view rendered_text);

namespace ui_detail {
std::string compose_portrait_name(std::string full_name, std::string_view form);
bool matches_menu_visual(std::string_view root, std::string_view hit, std::string_view visual);
std::string compose_help_prompt(std::string_view combined, std::string_view keys, std::string_view action);
bool matches_training_reset_prompt(std::string_view painted);
}

class UiReader {
public:
    explicit UiReader(GameReader& game) : game_(game) {}
    ScreenReading sample(std::span<const ObjectRef> roots, bool cinematic = false, bool diagnostics = false);
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
