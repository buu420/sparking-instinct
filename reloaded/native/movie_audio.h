#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sparking {

struct MovieCue {
    std::string_view file;
    int start_ms;
    int end_ms;
    int audio_ms;
};

struct MovieSchedule {
    std::span<const std::string_view> aliases;
    int fps;
    int frame_count;
    int polling_margin_ms;
    std::span<const MovieCue> cues;
};

const MovieSchedule* match_movie(std::string_view uri);
bool valid_narration_clip(const std::filesystem::path& path, int duration_ms);

using MovieClock = std::chrono::steady_clock;
using MovieReport = std::function<void(std::string_view)>;

struct MovieAction {
    std::optional<std::string_view> clip;
    bool stop{};
    bool advancing{};
};

class MovieTimeline {
public:
    MovieAction advance(const MovieSchedule* movie, int frame, MovieClock::time_point now);
    void reset();

private:
    const MovieSchedule* movie_{};
    const MovieCue* last_cue_{};
    std::optional<int> last_frame_;
    MovieClock::time_point advanced_at_{};
    bool cue_window_open_{};
};

class MovieAudio {
public:
    explicit MovieAudio(std::filesystem::path narration_dir, MovieReport report = {});
    ~MovieAudio();

    // Clip names are borrowed from the generated movie schedule.
    void play(std::string_view clip, MovieClock::time_point issued);
    void stop();
    void clock_advanced(MovieClock::time_point now);

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
};

}
