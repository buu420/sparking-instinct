#include "movie_audio.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
using namespace sparking;
using namespace std::chrono_literals;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void check_clip(std::optional<std::string_view> result, std::string_view name)
{
    check(result == name, "Wrong narration cue for the displayed frame");
}
}

int main(int argc, char** argv)
{
    check(argc == 2, "The narration fixture directory is required");
    const std::filesystem::path bank = argv[1];
    const auto op2 = match_movie("file://../../../SparkingZERO/Content/CriWareData/Movies/MV_Opening/Korat_OP2_4K_CP.usm");
    const auto op3 = match_movie("MV_Opening\\KORAT_OP3_2K_CP.USM");
    check(op2 && op2->frame_count == 2557 && op2->fps == 30 && op2->cues.size() == 18,
          "OP2 resolution alias did not select its validated schedule");
    check(op3 && op3->frame_count == 2717 && op3->fps == 30 && op3->cues.size() == 19,
          "OP3 resolution alias did not select its validated schedule");
    check(match_movie("Korat_OP2_1K_CP.usm") == op2 && match_movie("Korat_OP2_2K_CP.usm") == op2,
          "OP2 1K and 2K do not use the same schedule");
    check(match_movie("Korat_OP3_1K_CP.usm") == op3 && match_movie("Korat_OP3_4K_CP.usm") == op3,
          "OP3 1K and 4K do not use the same schedule");
    check(!match_movie("Korat_OP1_4K_CP.usm") && !match_movie("copy-Korat_OP3_1K_CP.usm") &&
          !match_movie("Korat_OP3_1K_CP.usm.extra"), "An unmatched movie selected wording from another source");

    for (const auto* movie : {op2, op3}) {
        for (const auto& cue : movie->cues) {
            check(valid_narration_clip(bank / std::filesystem::path(cue.file), cue.audio_ms),
                  "A packaged PCM narration clip is unavailable or has a different duration");
        }
    }
    check(!valid_narration_clip(bank / "missing.wav", 3120) &&
          !valid_narration_clip(bank / "cue-sheet.json", 3120) &&
          !valid_narration_clip(bank / "op2-01.wav", 3100), "The audio bank accepted corrupt clip data");

    MovieTimeline timeline;
    const auto start = std::chrono::steady_clock::time_point{10s};
    auto tracked = [&](const MovieSchedule* movie, int frame, std::chrono::milliseconds when) {
        return timeline.advance(movie, frame, start + when);
    };
    check(!tracked(op3, -1, 0ms).clip, "A negative native frame was accepted");
    check(!tracked(op3, 17, 30ms).clip, "A cue started before its scene");
    check_clip(tracked(op3, 18, 65ms).clip, "op3-01.wav");
    check(!tracked(op3, 18, 95ms).clip && !tracked(op3, 19, 120ms).clip,
          "The same moving or paused frame repeated narration");
    check(tracked(op3, 19, 400ms).stop, "A stalled native frame left narration running");
    check(!tracked(op3, 22, 435ms).clip, "Resuming a stalled movie replayed the interrupted cue");
    check(!tracked(op3, 149, 465ms).clip, "A forward seek queued a past cue");
    check_clip(tracked(op3, 150, 500ms).clip, "op3-02.wav");
    check(tracked(nullptr, 150, 535ms).stop, "Losing the native movie left narration running");
    check(!tracked(op3, 151, 565ms).clip && !tracked(op3, 152, 600ms).clip,
          "A revalidated player replayed the previous cue within its window");
    check(tracked(op3, 0, 635ms).stop, "A rewind left the previous clip playing");
    check(!tracked(op3, 17, 1200ms).clip, "A replay queued narration outside its scene");
    check_clip(tracked(op3, 18, 1235ms).clip, "op3-01.wav");
    check(tracked(op2, 48, 1265ms).stop, "Switching movie source left the old narration playing");
    check_clip(tracked(op2, 49, 1300ms).clip, "op2-01.wav");
    check(tracked(op2, 2557, 1335ms).stop, "A frame beyond the verified film left narration playing");

    timeline.reset();
    check(!tracked(op2, 51, 0ms).clip && !tracked(op2, 52, 30ms).clip,
          "Returning while already inside a scene replayed its narration");
    check(!tracked(op2, 224, 65ms).clip, "A forward jump queued a past scene");
    check(!tracked(op2, 236, 465ms).clip, "A late cue started outside its polling margin");
    check(!tracked(op2, 332, 3700ms).clip, "A stalled poll started an old cue");
    check_clip(tracked(op2, 334, 3740ms).clip, "op2-03.wav");

    timeline.reset();
    check(!tracked(op3, 17, 0ms).clip, "An unvalidated movie played narration");
    check_clip(tracked(op3, 18, 35ms).clip, "op3-01.wav");
    check(tracked(nullptr, -1, 70ms).stop, "Losing the movie failed to stop its clip");
    check(!tracked(op3, 0, 700ms).clip, "The replay produced a cue before its scene");
    check(!tracked(op3, 17, 1265ms).clip, "The scene was narrated early after a replay");
    check_clip(tracked(op3, 18, 1300ms).clip, "op3-01.wav");
}
