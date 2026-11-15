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
constexpr auto capture_interval = 500ms;
constexpr std::size_t max_utterance = 12000;
constexpr std::size_t capture_limit = 6144;
constexpr std::size_t capture_roots = 24;

std::string_view utf8_prefix(std::string_view text, std::size_t bytes)
{
    if (text.size() <= bytes) return text;
    while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) --bytes;
    return text.substr(0, bytes);
}

void append_capture(std::string& packet, std::string_view field, std::string_view value)
{
    constexpr std::string_view clipped = " [truncated]";
    const auto available = capture_limit - packet.size();
    if (available <= field.size() + clipped.size()) return;
    packet += field;
    const auto budget = capture_limit - packet.size();
    if (value.size() <= budget) {
        packet += value;
    } else {
        packet += utf8_prefix(value, budget - clipped.size());
        packet += clipped;
    }
}

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

void append_missing(std::vector<std::string>& items, std::span<const std::string> additional)
{
    for (const auto& item : additional)
        if (!item.empty() && std::ranges::find(items, item) == items.end()) items.push_back(item);
}

std::string together(const auto& items)
{
    std::string result;
    for (const std::string_view item : items) {
        if (item.empty()) continue;
        if (!result.empty() && result.size() + item.size() + 2 > max_utterance) break;
        if (!result.empty()) result += ". ";
        if (item.size() <= max_utterance) result += item;
        else {
            result += utf8_prefix(item, max_utterance);
            break;
        }
    }
    return result;
}
}

Runtime::Runtime(Unreal& engine, const std::filesystem::path& mod,
                 const std::filesystem::path& game_bin, Report report)
    : engine_(engine), game_(engine), ui_(game_), battle_(game_), movie_(game_, mod / L"AudioDescription", report),
      subtitle_reader_(game_), preference_(game_bin / L"SparkingZeroAccess-subtitles.ini"), report_(std::move(report)),
      subtitles_(saved_subtitles(preference_))
{
}

bool Runtime::speak(std::string_view message, bool interrupt)
{
    if (message.empty()) return false;
    const bool queued = speech::say(message, interrupt);
    std::string record;
    append_capture(record, queued ? "Speech: " : "Speech rejected: ", message);
    if (queued) append_capture(record, " ", interrupt ? "[interrupt]" : "[queue]");
    report_(record);
    return queued;
}

void Runtime::reset()
{
    reading_.reset();
    subtitle_reader_.reset();
    ui_.reset();
    battle_.reset();
    movie_.reset();
    screen_ = {};
    hud_ = {};
    items_.clear();
    review_index_ = 0;
    movie_visible_ = false;
    next_ = next_movie_ = next_battle_speech_ = next_capture_ = {};
    captured_.reset();
    capture_dirty_ = false;
}

void Runtime::foreground_lost()
{
    if (!foreground_) return;
    foreground_ = false;
    speech::set_foreground(false);
    report_("Game window lost focus.");
    reset();
}

void Runtime::map_start()
{
    if (map_depth_++ != 0) return;
    report_("Map loading.");
    speech::stop();
    reset();
}

void Runtime::map_end()
{
    if (!map_depth_ || --map_depth_) return;
    game_.reset();
    engine_.clear_layouts();
    report_("Map load finished.");
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
        report_("Game window in focus. Live capture active.");
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
            subtitle_reader_.reset();
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
    hud_ = movie_visible_ ? BattleReading{} : battle_.sample(roots, now);
    if (engine_.faulted()) return;
    reading_.update_hud(hud_.items, hud_.announcements, now);
    auto next_items = screen_.items;
    append_missing(next_items, hud_.items);
    const bool items_changed = next_items != items_;
    if (items_changed) {
        const auto previous = items_.empty() ? std::string_view{} : std::string_view(items_[review_index_]);
        const auto found = std::ranges::find(next_items, previous);
        review_index_ = found == next_items.end() ? 0 : static_cast<std::size_t>(found - next_items.begin());
        items_ = std::move(next_items);
    }

    const bool changed = reading_.screen_changed(screen_.root, screen_.focus, screen_.announcement);
    const bool control_details = reading_.control_details_changed(screen_.control_details, changed);
    const bool tutorial = reading_.tutorial_due(screen_.tutorial, now);
    const auto subtitle = subtitle_reader_.sample(screen_.subtitles, subtitles_, now, screen_.tutorial);
    const bool help = reading_.battle_help_changed(screen_.battle_help);
    const bool menu = changed && screen_.announcement != screen_.tutorial &&
        std::ranges::find(screen_.subtitles, screen_.announcement, &SubtitleLine::text) == screen_.subtitles.end();
    const std::array<std::string_view, 6> lines{
        menu ? std::string_view(screen_.announcement) : std::string_view{},
        control_details ? std::string_view(screen_.control_details) : std::string_view{},
        hud_.guide,
        tutorial ? std::string_view(screen_.tutorial) : std::string_view{},
        help ? std::string_view(screen_.battle_help) : std::string_view{},
        subtitle
    };
    // The current selection supersedes earlier selections even if a HUD still
    // exists behind the menu. A settled detail card follows its selected row.
    const bool in_battle = (hud_.active && !hud_.items.empty()) || !screen_.battle_help.empty();
    capture_screen(now, roots, items_changed, in_battle);
    const bool interrupt = (menu && screen_.focus) || hud_.guide_opened || (!in_battle && !control_details);
    const bool speaking = speak(together(lines), interrupt);
    if (!hud_.guide.empty())
        next_battle_speech_ = now + battle_speech_gap;
    if (!speaking && now >= next_battle_speech_) {
        const auto pending = reading_.pending_hud();
        if (!pending.empty() && !speech::speaking() && speak(pending, false)) {
            reading_.clear_hud();
            next_battle_speech_ = now + battle_speech_gap;
        }
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

void Runtime::capture_screen(Clock::time_point now, std::span<const ObjectRef> roots, bool items_changed, bool in_battle)
{
    capture_dirty_ |= items_changed;
    const auto viewport = roots.first((std::min)(roots.size(), capture_roots));
    const bool changed = !captured_ || captured_->root != screen_.root || captured_->focus != screen_.focus ||
        captured_->announcement != screen_.announcement || captured_->details != screen_.control_details ||
        captured_->selection != screen_.selection_details || captured_->battle != in_battle ||
        captured_->movie != movie_visible_ || !std::ranges::equal(viewport, captured_->roots);
    if (!changed && (now < next_capture_ || (!capture_dirty_ && !debug_))) return;

    const auto describe = [&](ObjectRef ref) {
        if (!ref) return std::string("(none)");
        return engine_.class_name(ref) + " " + engine_.name_of(ref);
    };
    std::string record = "Live screen";
    append_capture(record, " | Screen root: ", describe(screen_.root));
    append_capture(record, " | Screen focus: ", describe(screen_.focus));
    if (!screen_.selection_details.empty())
        append_capture(record, " | Selection: ", screen_.selection_details);
    append_capture(record, " | State: ", movie_visible_ ? "movie" : in_battle ? "battle" : "menu");
    append_capture(record, " | Automatic menu speech: ", screen_.announcement.empty() ? "(none)" : screen_.announcement);
    if (!screen_.control_details.empty())
        append_capture(record, " | Control details: ", screen_.control_details);
    for (const auto root : viewport)
        append_capture(record, " | Visible root: ", engine_.class_name(root));
    append_capture(record, " | Review items: ", std::to_string(items_.size()));
    for (const auto& item : items_) {
        if (record.size() + 24 >= capture_limit) break;
        append_capture(record, " | Text: ", item);
    }
    if (report_(record)) {
        captured_ = CapturedScreen{screen_.root, screen_.focus, {viewport.begin(), viewport.end()},
            screen_.announcement, screen_.control_details, screen_.selection_details, in_battle, movie_visible_};
        next_capture_ = now + capture_interval;
        capture_dirty_ = false;
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
    auto captured = battle ? ScreenReading{} : ui_.sample(game_.viewport_roots(), movie_visible_, true);
    if (!battle) append_missing(captured.items, hud_.items);
    const auto& observed = battle ? screen_ : captured;
    const auto& lines = battle ? hud_.items : captured.items;
    report_(std::string(battle ? "Battle HUD review, " : "Screen review, ") +
            std::to_string(lines.size()) + " items.");
    if (observed.root)
        report_("Screen root: " + engine_.class_name(observed.root));
    if (observed.focus)
        report_("Screen focus: " + engine_.class_name(observed.focus) + " " + engine_.name_of(observed.focus));
    if (!battle) {
        for (const auto& surface : captured.visible_surfaces)
            report_("Visible surface: " + surface);
        if (!observed.announcement.empty()) report_("Automatic menu speech: " + observed.announcement);
    }
    for (const auto& item : lines) report_(item);
}

void Runtime::toggle_subtitles()
{
    subtitles_ = !subtitles_;
    const bool saved = save_subtitles(preference_, subtitles_);
    std::string message = subtitles_ ? "All visible dialogue will be read." : "Skip dialogue that the game voices.";
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
        speak(debug_ ? "Detailed capture on." : "Detailed capture off.");
        break;
    case VK_F6: review(0); break;
    case VK_F7: review(-1); break;
    case VK_F8: review(1); break;
    case VK_F9: toggle_subtitles(); break;
    default: break;
    }
}

}
