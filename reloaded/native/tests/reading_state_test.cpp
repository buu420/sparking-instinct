#include "reading_state.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool value, const char* failure)
{
    if (value) return;
    std::cerr << failure << '\n';
    std::exit(1);
}
}

int main()
{
    using namespace std::chrono_literals;
    using sparking::ObjectRef;
    using sparking::ReadingState;
    const ObjectRef root{10, 1, 8};
    const ObjectRef row{20, 2, 9};
    const ObjectRef same_label_other_row{30, 3, 10};
    const auto start = ReadingState::Clock::time_point{};
    ReadingState state;

    check(state.screen_changed(root, row, "Display Language, English"), "Initial focus was silent");
    check(!state.screen_changed(root, row, "Display Language, English"), "Stable focus was repeated");
    check(!state.screen_changed(root, {}, ""), "Missing focus was announced");
    check(!state.screen_changed(root, row, "Display Language, English"), "Single empty poll reannounced the row");
    check(state.screen_changed(root, row, "Display Language, French"), "Horizontal value change was missed");
    check(state.screen_changed(root, same_label_other_row, "Display Language, French"),
          "Different focus with the same title was missed");

    check(state.tutorial_due("Charge your ki", start), "Initial tutorial instruction was missed");
    check(!state.tutorial_due("Charge your ki", start + 19s), "Tutorial repeated early");
    check(!state.tutorial_due("", start + 19s), "A missing tutorial was announced");
    check(state.tutorial_due("Charge your ki", start + 20s), "Tutorial did not repeat after twenty seconds");
    check(!state.tutorial_due("Charge your ki", start + 21s), "Tutorial did not reset the repeat deadline");
    check(state.tutorial_due("Use the transformation menu", start + 21s), "A new instruction was missed");

    check(state.subtitle_changed("Stay focused!"), "Initial subtitle was missed");
    check(!state.subtitle_changed("Stay focused!"), "An unchanged subtitle repeated");
    check(!state.subtitle_changed(""), "An empty line was announced");
    check(!state.subtitle_changed("Stay focused!"), "One empty frame repeated the subtitle");
    check(state.subtitle_changed("Here we go!"), "A new subtitle was missed");
    state.reset();
    check(state.subtitle_changed("Here we go!"), "A map transition retained stale subtitle state");
    check(!state.tutorial_due("", start + 41s), "A map transition retained a tutorial reminder");

    const std::array two_bars{std::string("Your ki, 2 bars")};
    const std::array next_values{std::string("Your ki, 3 bars"), std::string("Your skill count, 2")};
    state.update_hud(two_bars, two_bars, start);
    state.update_hud(two_bars, {}, start + 650ms);
    check(state.pending_hud() == two_bars[0], "Busy speech dropped a visible ki change");
    state.update_hud(next_values, next_values, start + 1s);
    check(state.pending_hud() == "Your ki, 3 bars. Your skill count, 2",
          "HUD speech did not keep both the latest ki and the new skill count");
    state.update_hud(next_values, {}, start + 7s);
    check(state.pending_hud().empty(), "A late meter announcement was retained");

    const std::array transition{std::string("Your Sparking mode ended")};
    state.update_hud(two_bars, transition, start + 8s);
    check(state.pending_hud() == transition[0], "A recent visible-gauge transition was dropped");
    state.update_hud(two_bars, {}, start + 9s);
    check(state.pending_hud().empty(), "A stale gauge transition remained pending");
    state.update_hud(two_bars, two_bars, start + 10s);
    state.update_hud({}, {}, start + 11s);
    check(state.pending_hud().empty(), "The hidden HUD retained a pending meter");
}
