#include "runtime.h"
#include "../speech/prism_speech.h"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using namespace sparking;
using namespace std::chrono_literals;

struct Utterance {
    std::string text;
    bool interrupt{};
};

ScreenReading screen;
BattleReading hud;
std::vector<Utterance> speech_log;
bool busy{};

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void sample(Runtime& runtime)
{
    // Exercise the public tick path after the normal 80 ms sampling interval.
    std::this_thread::sleep_for(90ms);
    runtime.frame(true);
}
}

namespace sparking {
bool Unreal::readable() const { return true; }
void Unreal::clear_layouts() {}
std::string Unreal::class_name(ObjectRef) { return {}; }
std::string Unreal::name_of(ObjectRef) { return {}; }

std::vector<ObjectRef> GameReader::viewport_roots() { return {}; }
void GameReader::reset() {}

ScreenReading UiReader::sample(std::span<const ObjectRef>, bool, bool) { return screen; }
void UiReader::reset() {}

std::string SubtitleReader::sample(std::span<const SubtitleLine>, bool, Clock::time_point, std::string_view)
{
    return {};
}
void SubtitleReader::reset() {}

BattleReading BattleReader::sample(std::span<const ObjectRef>, Clock::time_point) { return hud; }
void BattleReader::reset() {}

struct MovieAudio::Worker {};
MovieAudio::MovieAudio(std::filesystem::path, MovieReport) {}
MovieAudio::~MovieAudio() = default;
MovieReader::MovieReader(GameReader& game, std::filesystem::path path, MovieReport report)
    : game_(game), audio_(std::move(path), std::move(report)) {}
bool MovieReader::sample(std::span<const ObjectRef>) { return false; }
void MovieReader::reset() {}
}

namespace sparking::speech {
bool say(std::string_view text, bool interrupt) noexcept
{
    speech_log.push_back({std::string(text), interrupt});
    busy = true;
    return true;
}
void stop() noexcept { busy = false; }
bool speaking() noexcept { return busy; }
void set_foreground(bool) noexcept {}
}

int main()
{
    using namespace sparking;
    Unreal game({});
    Runtime runtime(game, {}, {}, [](std::string_view) {});
    hud.active = true;
    hud.items = {"Your ki, 3 bars"};
    screen.root = {10, 1, 1};
    screen.focus = {20, 2, 1};
    screen.announcement = "Instant Transmission";
    runtime.frame(true);
    check(speech_log.size() == 1 && speech_log.back().text == "Instant Transmission", "The first move was silent");
    check(busy, "The fixture must still be speaking the first move");

    screen.focus = {30, 3, 1};
    screen.announcement = "Super Kamehameha";
    sample(runtime);
    check(speech_log.size() == 2 && speech_log.back().text == "Super Kamehameha",
          "The newest move was not offered during the previous move's speech");
    check(speech_log.back().interrupt, "The newest move queued behind the previous move");

    // Details arrive after the focus move while the selected row is still speaking.
    hud = {};
    screen.control_details = "RT + X. Consume ki to use Blast 1.";
    sample(runtime);
    check(speech_log.size() == 3 && speech_log.back().text == screen.control_details,
          "The settled card was missed or repeated the move name");
    check(!speech_log.back().interrupt, "A late card cut off its own move name");
    sample(runtime);
    check(speech_log.size() == 3, "An unchanged detail card repeated");

    screen.focus = {40, 4, 1};
    screen.announcement = "Super Spirit Bomb";
    screen.control_details = "RT + B. Available in Sparking Mode.";
    sample(runtime);
    check(speech_log.size() == 4 && speech_log.back().interrupt &&
          speech_log.back().text.starts_with("Super Spirit Bomb. RT + B."),
          "The new move did not supersede the old card or put its name first");

    screen = {};
    hud.active = true;
    hud.items = {"Your ki, 3 bars", "Super Kamehameha, X"};
    hud.guide = "Super Kamehameha, X";
    hud.guide_opened = true;
    sample(runtime);
    check(speech_log.size() == 5 && speech_log.back().interrupt,
          "A newly opened charge guide lost its priority");
    hud.guide = "Super Spirit Bomb, B";
    hud.guide_opened = false;
    sample(runtime);
    check(speech_log.size() == 6 && speech_log.back().text == hud.guide && !speech_log.back().interrupt,
          "A follow-up charge guide cut off the current battle announcement");
}
