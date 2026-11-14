#include "subtitle_reader.h"
#include "game_reader.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sparking;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}
}

int main()
{
    Unreal engine({});
    GameReader game(engine);
    SubtitleReader reader(game);
    const auto now = SubtitleReader::Clock::time_point{} + std::chrono::seconds(1);
    const ObjectRef window{0x10000, 10, 5};
    const ObjectRef other_window{0x20000, 11, 8};
    const ObjectRef text_widget{0x30000, 12, 9};
    const std::array lines{
        SubtitleLine{window, text_widget, {}, "Before the next battle..."},
        SubtitleLine{other_window, text_widget, {}, "Goku. Let's find out."}
    };
    check(reader.sample(lines, false, now) == "Before the next battle...\nGoku. Let's find out.",
          "F9 filtering hid text whose voice could not be verified");
    check(reader.sample(lines, false, now).empty(), "An unchanged visible line repeated");
    check(reader.sample(lines, true, now).empty(), "Enabling all dialogue replayed a visible line");
    reader.sample({}, false, now);
    check(reader.sample(lines, false, now).empty(), "A one-frame visibility loss replayed the text");
    for (unsigned i = 0; i < 3; ++i) reader.sample({}, false, now);
    check(!reader.sample(lines, false, now).empty(), "A returning subtitle remained stale");
    reader.reset();
    check(!reader.sample(lines, true, now).empty(), "A new scene did not clear the subtitle history");
    const std::array transition{
        SubtitleLine{ObjectRef{window.address, window.index, window.serial + 1},
                     text_widget, {}, lines[0].text},
        lines[1]
    };
    check(reader.sample(transition, true, now) == lines[0].text, "A reused widget inherited an old line");
    const ObjectRef other_text{0x40000, 13, 2};
    reader.reset();
    const std::array same_window{
        lines[0],
        SubtitleLine{window, other_text, {}, "A second visible line."}
    };
    check(reader.sample(same_window, false, now) == "Before the next battle...\nA second visible line.",
          "Independent text rows in one subtitle window were collapsed");
    check(reader.sample(same_window, false, now).empty(), "Two stable rows repeated in one window");
    reader.reset();
    check(reader.sample(lines, true, now, lines[1].text) == lines[0].text,
          "A subtitle duplicated the same text already owned by the tutorial reader");

    using subtitle_detail::matches_voice_cue;
    check(matches_voice_cue("ST_Line_001", "ADVIF_Line_001"), "Native key-to-cue mapping was not recognized");
    check(!matches_voice_cue("ST_Line_001", "ADVIF_Line_002"), "An unrelated active voice hid the line");
    check(!matches_voice_cue("ST_Line_001", "ADVIF_Line_001_more"), "A cue prefix hid a distinct line");
    check(!matches_voice_cue("", "ADVIF_Line_001"), "Missing native key hid prose");
    check(!matches_voice_cue("The hero enters.", "The hero enters."), "Unkeyed dialogue matched a voice");
    check(!matches_voice_cue("ST_", "ADVIF_"), "Empty cue payload matched");

    using subtitle_detail::voice_is_audible;
    check(voice_is_audible(2., false, 1.), "An active unpaused voice was not recognized");
    check(!voice_is_audible(1., false, 1.), "A preparing or failed voice hid the line");
    check(!voice_is_audible(2., true, 1.), "A paused voice hid the line");
    check(!voice_is_audible(2., false, 0.), "A silent voice hid the line");
    check(!voice_is_audible(2., {}, 1.), "An unknown pause state hid the line");
}
