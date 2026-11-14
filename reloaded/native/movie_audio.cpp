#include "movie_audio.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sparking {
namespace {
using namespace std::chrono_literals;
constexpr auto frame_stall = 240ms;
constexpr auto start_expiry = 120ms;
constexpr auto lease_check = 25ms;
constexpr std::uint32_t sample_rate = 24'000;
constexpr std::uint16_t block_align = sizeof(std::int16_t);
constexpr std::uint32_t bytes_per_second = sample_rate * block_align;
constexpr std::size_t max_wave_bytes = bytes_per_second * 6;

constexpr int ms(double seconds)
{
    return static_cast<int>(seconds * 1000.0 + 0.5);
}

#include "movie_cues.inc"

constexpr bool valid_schedules()
{
    for (const auto& movie : schedules) {
        if (movie.fps <= 0 || movie.frame_count <= 0 || movie.polling_margin_ms < 0) return false;
        int previous_end{};
        for (const auto& cue : movie.cues) {
            if (cue.start_ms < previous_end || cue.audio_ms <= 0 ||
                cue.audio_ms > cue.end_ms - cue.start_ms - movie.polling_margin_ms ||
                cue.end_ms * movie.fps > movie.frame_count * 1000) return false;
            previous_end = cue.end_ms;
        }
    }
    return true;
}
static_assert(valid_schedules(), "Narration must fit its verified movie windows.");

char lowercase_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
    return ch;
}

template<class T> T wave_value(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

std::span<const std::uint8_t> wave_samples(std::span<const std::uint8_t> bytes, int duration_ms)
{
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0 ||
        wave_value<std::uint32_t>(bytes, 4) != bytes.size() - 8) return {};

    bool pcm_format{};
    std::span<const std::uint8_t> samples;
    for (std::size_t offset = 12; offset < bytes.size();) {
        if (bytes.size() - offset < 8) return {};
        const auto id = bytes.data() + offset;
        const auto size = static_cast<std::size_t>(wave_value<std::uint32_t>(bytes, offset + 4));
        offset += 8;
        const auto padded = size + (size % 2);
        if (padded > bytes.size() - offset) return {};
        if (std::memcmp(id, "fmt ", 4) == 0) {
            if (pcm_format || size < 16 || wave_value<std::uint16_t>(bytes, offset) != WAVE_FORMAT_PCM ||
                wave_value<std::uint16_t>(bytes, offset + 2) != 1 ||
                wave_value<std::uint32_t>(bytes, offset + 4) != sample_rate ||
                wave_value<std::uint32_t>(bytes, offset + 8) != bytes_per_second ||
                wave_value<std::uint16_t>(bytes, offset + 12) != block_align ||
                wave_value<std::uint16_t>(bytes, offset + 14) != 16) return {};
            pcm_format = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!samples.empty() || !size || size % block_align != 0) return {};
            samples = bytes.subspan(offset, size);
        }
        offset += padded;
    }
    if (!pcm_format || duration_ms <= 0 ||
        samples.size() * 1000 != static_cast<std::uint64_t>(duration_ms) * bytes_per_second) return {};
    return samples;
}

std::vector<std::uint8_t> load_clip(const std::filesystem::path& path, int duration_ms)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > static_cast<std::streamoff>(max_wave_bytes)) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    const auto samples = wave_samples(bytes, duration_ms);
    return {samples.begin(), samples.end()};
}

struct ComApartment {
    const HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

struct AudioEngine {
    IXAudio2* engine{};
    IXAudio2MasteringVoice* output{};
    IXAudio2SourceVoice* source{};

    ~AudioEngine()
    {
        stop();
        if (output) output->DestroyVoice();
        if (engine) engine->Release();
    }

    bool start()
    {
        return SUCCEEDED(XAudio2Create(&engine)) && engine && SUCCEEDED(engine->CreateMasteringVoice(&output));
    }

    void stop()
    {
        if (!source) return;
        source->DestroyVoice();
        source = nullptr;
    }

    bool play(std::span<const std::uint8_t> clip)
    {
        const WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, sample_rate, bytes_per_second, block_align, 16, 0};
        if (FAILED(engine->CreateSourceVoice(&source, &format, XAUDIO2_VOICE_NOPITCH))) return false;
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<std::uint32_t>(clip.size());
        buffer.pAudioData = clip.data();
        if (SUCCEEDED(source->SubmitSourceBuffer(&buffer)) && SUCCEEDED(source->Start())) return true;
        stop();
        return false;
    }

    bool playing() const
    {
        if (!source) return false;
        XAUDIO2_VOICE_STATE state{};
        source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return state.BuffersQueued != 0;
    }
};
}

const MovieSchedule* match_movie(std::string_view uri)
{
    const auto separator = uri.find_last_of("/\\");
    if (separator != uri.npos) uri.remove_prefix(separator + 1);
    for (const auto& movie : schedules) {
        for (const auto alias : movie.aliases) {
            if (std::equal(uri.begin(), uri.end(), alias.begin(), alias.end(), [](char a, char b) {
                    return lowercase_ascii(a) == lowercase_ascii(b);
                })) return &movie;
        }
    }
    return nullptr;
}

bool valid_narration_clip(const std::filesystem::path& path, int duration_ms)
{
    return !load_clip(path, duration_ms).empty();
}

MovieAction MovieTimeline::advance(const MovieSchedule* movie, int frame, MovieClock::time_point now)
{
    if (!movie || frame < 0 || frame >= movie->frame_count) {
        const bool was_visible = last_frame_.has_value();
        last_frame_.reset();
        cue_window_open_ = false;
        return {.stop = was_visible};
    }

    const bool changed_movie = movie != movie_;
    const bool previous_movie = movie_ != nullptr;
    if (changed_movie) {
        reset();
        movie_ = movie;
    }
    if (!last_frame_) {
        if (last_cue_ && (frame + 1) * 1000 < last_cue_->start_ms * movie->fps)
            last_cue_ = nullptr;
        last_frame_ = frame;
        advanced_at_ = now;
        return {.stop = changed_movie && previous_movie};
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - advanced_at_);
    const int previous = *last_frame_;
    if (elapsed >= frame_stall) {
        if (frame + 1 < previous && last_cue_ && frame * 1000 < last_cue_->start_ms * movie->fps)
            last_cue_ = nullptr;
        last_frame_ = frame;
        advanced_at_ = now;
        cue_window_open_ = false;
        return {.stop = true};
    }
    if (frame == previous) return {};
    last_frame_ = frame;
    advanced_at_ = now;
    const auto frame_time = frame * 1000;
    const auto jump = (frame - previous) * 1000;
    if (frame < previous || jump > (elapsed.count() + movie->polling_margin_ms) * movie->fps) {
        if (frame + 1 < previous && last_cue_ && frame_time < last_cue_->start_ms * movie->fps)
            last_cue_ = nullptr;
        cue_window_open_ = false;
        return {.stop = true};
    }

    MovieAction result{.advancing = true};
    if (cue_window_open_ && frame_time >= last_cue_->end_ms * movie->fps) {
        result.stop = true;
        cue_window_open_ = false;
    }
    for (const auto& cue : movie->cues) {
        const auto start = cue.start_ms * movie->fps;
        if (frame_time < start) break;
        if (&cue != last_cue_ && previous * 1000 <= start &&
            frame_time <= (cue.start_ms + movie->polling_margin_ms) * movie->fps) {
            last_cue_ = &cue;
            cue_window_open_ = true;
            result.clip = cue.file;
            break;
        }
    }
    return result;
}

void MovieTimeline::reset()
{
    *this = {};
}

struct MovieAudio::Worker {
    struct Request {
        std::string_view clip;
        MovieClock::time_point issued;
    };

    Worker(std::filesystem::path directory, MovieReport log)
        : narration(std::move(directory)), report(std::move(log)), thread([this] { run(); }) {}

    ~Worker()
    {
        {
            std::lock_guard lock(mutex);
            closing = true;
        }
        ready.notify_one();
        thread.join();
    }

    void post(Request request)
    {
        {
            std::lock_guard lock(mutex);
            next = request;
        }
        ready.notify_one();
    }

    void log(std::string_view message) const noexcept
    {
        if (!report) return;
        try {
            report(message);
        } catch (...) {
            // A log sink must not terminate the audio worker.
        }
    }

    void run() noexcept
    {
        try {
            std::unordered_map<std::string_view, std::vector<std::uint8_t>> clips;
            for (const auto& movie : schedules) {
                for (const auto& cue : movie.cues) {
                    auto clip = load_clip(narration / std::filesystem::path(cue.file), cue.audio_ms);
                    if (clip.empty()) log("Movie narration WAV is missing or invalid. " + std::string(cue.file));
                    else clips.emplace(cue.file, std::move(clip));
                }
            }
            if (clips.empty()) return;

            ComApartment apartment;
            AudioEngine audio;
            if (FAILED(apartment.result) || !audio.start()) {
                log("Movie narration could not open the Windows audio output.");
                return;
            }
            for (;;) {
                std::optional<Request> request;
                {
                    std::unique_lock lock(mutex);
                    const auto work_ready = [this] { return next.has_value() || closing; };
                    if (audio.source) ready.wait_for(lock, lease_check, work_ready);
                    else ready.wait(lock, work_ready);
                    if (closing) return;
                    request = std::exchange(next, std::nullopt);
                }

                const auto now = MovieClock::now();
                const auto last_progress = MovieClock::time_point{MovieClock::duration{native_progress.load()}};
                if (now - last_progress >= frame_stall || !audio.playing()) audio.stop();
                if (!request) continue;
                audio.stop();
                if (request->clip.empty() || now - request->issued >= start_expiry ||
                    now - last_progress >= frame_stall) continue;
                const auto found = clips.find(request->clip);
                if (found == clips.end()) continue;
                if (audio.play(found->second)) log("Movie narration started. " + std::string(request->clip));
                else log("Movie narration failed to play. " + std::string(request->clip));
            }
        } catch (...) {
            log("Movie narration worker stopped after a Windows audio error.");
        }
    }

    std::filesystem::path narration;
    MovieReport report;
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Request> next;
    bool closing{};
    std::atomic<MovieClock::duration::rep> native_progress{};
    std::thread thread;
};

MovieAudio::MovieAudio(std::filesystem::path narration, MovieReport report)
    : worker_(std::make_unique<Worker>(std::move(narration), std::move(report))) {}

MovieAudio::~MovieAudio() = default;

void MovieAudio::play(std::string_view clip, MovieClock::time_point issued)
{
    worker_->post({clip, issued});
}

void MovieAudio::stop()
{
    worker_->native_progress.store(0);
    worker_->post({});
}

void MovieAudio::clock_advanced(MovieClock::time_point now)
{
    worker_->native_progress.store(now.time_since_epoch().count());
}

}
