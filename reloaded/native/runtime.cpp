#include "runtime.h"
#include "../speech/prism_speech.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <fstream>

namespace sparking {
namespace {
using namespace std::chrono_literals;
constexpr auto display_interval = 80ms;
constexpr auto movie_interval = 33ms;
constexpr auto stats_interval = 20s;
constexpr auto battle_speech_gap = 650ms;
constexpr std::size_t max_utterance = 12000;

bool saved_subtitles(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() > 4096) return true;
    input.seekg(0);
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
        if (line == "AutomaticSubtitles=false") return false;
        if (line == "AutomaticSubtitles=true") return true;
    }
    return true;
}

bool save_subtitles(const std::filesystem::path& path, bool enabled)
{
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << (enabled ? "AutomaticSubtitles=true\n" : "AutomaticSubtitles=false\n");
        output.close();
        if (!output) {
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    DeleteFileW(temporary.c_str());
    return false;
}

std::string together(const std::vector<std::string>& items)
{
    std::string result;
    for (const auto& item : items) {
        if (item.empty()) continue;
        if (!result.empty() && result.size() + item.size() + 2 > max_utterance) break;
        if (!result.empty()) result += ". ";
        if (item.size() <= max_utterance) result += item;
        else {
            std::size_t end = max_utterance;
            while (end && (static_cast<unsigned char>(item[end]) & 0xc0) == 0x80) --end;
            result.append(item, 0, end);
            break;
        }
    }
    return result;
}
}

Runtime::Runtime(Unreal& engine, const std::filesystem::path& mod,
                 const std::filesystem::path& game_bin, Report report)
    : engine_(engine), game_(engine), ui_(game_), battle_(game_), movie_(game_, mod / L"AudioDescription", report),
      preference_(game_bin / L"SparkingZeroAccess-subtitles.ini"), report_(std::move(report)),
      subtitles_(saved_subtitles(preference_))
{
}

bool Runtime::speak(std::string_view message, bool interrupt)
{
    if (message.empty() || !speech::say(message, interrupt)) return false;
    if (debug_) report_(std::string("Speech: ") + std::string(message));
    return true;
}

void Runtime::reset()
{
    reading_.reset();
    ui_.reset();
    battle_.reset();
    movie_.reset();
    screen_ = {};
    hud_ = {};
    items_.clear();
    review_index_ = 0;
    movie_visible_ = false;
    next_ = next_movie_ = next_battle_speech_ = {};
}

void Runtime::foreground_lost()
{
    if (!foreground_) return;
    foreground_ = false;
    speech::set_foreground(false);
    reset();
}

void Runtime::map_start()
{
    if (map_depth_++ != 0) return;
    speech::stop();
    reset();
}

void Runtime::map_end()
{
    if (!map_depth_ || --map_depth_) return;
    game_.reset();
    engine_.clear_layouts();
}

void Runtime::sample_keys(bool dispatch)
{
    for (std::size_t i = 0; i < down_.size(); ++i) {
        const auto key = VK_F3 + static_cast<int>(i);
        const auto held = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (dispatch && held && !down_[i]) hotkey(key);
        down_[i] = held;
    }
}

void Runtime::frame(bool foreground)
{
    if (!foreground) {
        foreground_lost();
        return;
    }
    if (!foreground_) {
        foreground_ = true;
        speech::set_foreground(true);
        sample_keys(false);
    }
    if (map_depth_ || !engine_.readable()) {
        if (movie_visible_) movie_.reset();
        movie_visible_ = false;
        next_ = next_movie_ = {};
        sample_keys(false);
        return;
    }

    const auto now = Clock::now();
    if (now >= next_ || (movie_visible_ && now >= next_movie_)) sample(now);
    sample_keys(!engine_.faulted());
}

void Runtime::sample(Clock::time_point now)
{
    const bool read_screen = now >= next_;
    if (read_screen) next_ = now + display_interval;
    const auto roots = game_.viewport_roots();
    if (now >= next_movie_) {
        const bool was_visible = movie_visible_;
        movie_visible_ = movie_.sample(roots);
        next_movie_ = now + movie_interval;
        if (movie_visible_ != was_visible) {
            reading_.reset();
            ui_.reset();
            battle_.reset();
            items_.clear();
            screen_ = {};
            hud_ = {};
            review_index_ = 0;
            speech::stop();
        }
    }
    if (!read_screen || engine_.faulted()) return;

    screen_ = ui_.sample(roots, movie_visible_);
    hud_ = movie_visible_ ? BattleReading{} : battle_.sample(roots);
    if (engine_.faulted()) return;
    auto next_items = screen_.items;
    for (const auto& item : hud_.items) {
        if (!item.empty() && std::ranges::find(next_items, item) == next_items.end())
            next_items.push_back(item);
    }
    if (next_items != items_) {
        const auto previous = items_.empty() ? std::string_view{} : std::string_view(items_[review_index_]);
        const auto found = std::ranges::find(next_items, previous);
        review_index_ = found == next_items.end() ? 0 : static_cast<std::size_t>(found - next_items.begin());
        items_ = std::move(next_items);
    }

    const bool changed = reading_.screen_changed(screen_.root, screen_.focus, screen_.announcement);
    const bool tutorial = reading_.tutorial_due(screen_.tutorial, now);
    const bool subtitle = reading_.subtitle_changed(screen_.subtitle);
    bool speaking = false;
    if (changed && screen_.announcement != screen_.tutorial && screen_.announcement != screen_.subtitle)
        speaking = speak(screen_.announcement);
    if (tutorial) speaking = speak(screen_.tutorial, !speaking) || speaking;
    if (subtitle && subtitles_ && screen_.subtitle != screen_.tutorial)
        speaking = speak(screen_.subtitle, !speaking) || speaking;
    if (!hud_.guide.empty()) {
        speaking = speak(hud_.guide, !speaking) || speaking;
        next_battle_speech_ = now + battle_speech_gap;
    }
    if (!speaking && !hud_.guide_visible && !hud_.announcements.empty() &&
        now >= next_battle_speech_ && !speech::speaking()) {
        if (speak(together(hud_.announcements), false))
            next_battle_speech_ = now + battle_speech_gap;
    }

    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - now).count();
    ++polls_;
    total_ms_ += elapsed;
    peak_ms_ = (std::max)(peak_ms_, elapsed);
    if (stats_since_ == Clock::time_point{}) stats_since_ = now;
    if (now - stats_since_ >= stats_interval) {
        auto timing = "Reader timing: " + std::to_string(total_ms_ / polls_) + " ms average, " +
                      std::to_string(peak_ms_) + " ms maximum, " + std::to_string(polls_) + " samples.";
        if (screen_.root) timing += " Screen: " + engine_.class_name(screen_.root) + ".";
        timing += " Items: " + std::to_string(items_.size()) + ".";
        if (screen_.focus) timing += " Focus: " + engine_.class_name(screen_.focus) + ".";
        report_(timing);
        stats_since_ = now;
        total_ms_ = peak_ms_ = 0;
        polls_ = 0;
    }
}

void Runtime::review(int direction)
{
    if (items_.empty()) {
        speak(movie_visible_ ? "A movie is on screen." : "No readable text on this screen.");
        return;
    }
    if (!direction) {
        review_index_ = 0;
        speak(together(items_));
        return;
    }
    if (direction < 0)
        review_index_ = review_index_ ? review_index_ - 1 : items_.size() - 1;
    else
        review_index_ = (review_index_ + 1) % items_.size();
    speak(items_[review_index_]);
}

void Runtime::dump(bool battle)
{
    const auto& lines = battle ? hud_.items : items_;
    report_(std::string(battle ? "Battle HUD review, " : "Screen review, ") +
            std::to_string(lines.size()) + " items.");
    if (screen_.root)
        report_("Screen root: " + engine_.class_name(screen_.root));
    if (screen_.focus)
        report_("Screen focus: " + engine_.class_name(screen_.focus) + " " + engine_.name_of(screen_.focus));
    if (!battle) {
        for (const auto root : game_.viewport_roots())
            report_("Visible root: " + engine_.class_name(root));
    }
    for (const auto& item : lines) report_(item);
}

void Runtime::toggle_subtitles()
{
    subtitles_ = !subtitles_;
    const bool saved = save_subtitles(preference_, subtitles_);
    std::string message = subtitles_ ? "Automatic subtitle speech on." : "Automatic subtitle speech off.";
    if (!saved) {
        report_("Could not save the automatic subtitle speech preference.");
        message += " The setting could not be saved.";
    }
    speak(message);
}

void Runtime::hotkey(int key)
{
    switch (key) {
    case VK_F3: dump(true); break;
    case VK_F4: dump(false); break;
    case VK_F5:
        debug_ = !debug_;
        report_(debug_ ? "Reader diagnostics on." : "Reader diagnostics off.");
        speak(debug_ ? "Reader diagnostics on." : "Reader diagnostics off.");
        break;
    case VK_F6: review(0); break;
    case VK_F7: review(-1); break;
    case VK_F8: review(1); break;
    case VK_F9: toggle_subtitles(); break;
    default: break;
    }
}

}
