#include "reading_state.h"

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
}
