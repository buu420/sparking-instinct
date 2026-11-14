#include "subtitle_reader.h"
#include "game_reader.h"

#include <algorithm>

namespace sparking {
namespace {
using namespace std::chrono_literals;
constexpr std::size_t owner_limit = 32;
constexpr std::size_t voices_per_line = 8;
constexpr unsigned missed_samples = 3;
constexpr auto lookup_interval = 500ms;

ObjectRef world_of(GameReader& game, ObjectRef actor)
{
    return game.object(game.engine().outer(actor), "OwningWorld");
}

ObjectRef local_controller(GameReader& game, ObjectRef instance)
{
    auto& engine = game.engine();
    const auto players = game.objects(instance, "LocalPlayers", 1);
    if (players.empty()) return {};
    const auto controller = game.object(players.front(), "PlayerController");
    if (!engine.is_a(controller, engine.find_class("PlayerController")) ||
        game.object(world_of(game, controller), "OwningGameInstance") != instance) return {};
    return controller;
}

bool playing_voice(GameReader& game, ObjectRef voice_manager, std::uint32_t handle, std::string_view key)
{
    const auto component = game.engine().voice_component(voice_manager, handle);
    if (!component ||
        !subtitle_detail::matches_voice_cue(key, game.call_text(component, "GetCueName"))) return false;
    return subtitle_detail::voice_is_audible(game.call_number(component, "GetStatus"),
        game.call_bool(component, "IsPaused"), game.call_number(component, "GetVolume"));
}
}

bool subtitle_detail::matches_voice_cue(std::string_view text_key, std::string_view playing_cue)
{
    // The current game's EventScene and Paper Theater convert ST_ to ADVIF_.
    constexpr std::string_view text_prefix = "ST_";
    constexpr std::string_view cue_prefix = "ADVIF_";
    if (!text_key.starts_with(text_prefix) || text_key.size() == text_prefix.size() ||
        !playing_cue.starts_with(cue_prefix)) return false;
    return text_key.substr(text_prefix.size()) == playing_cue.substr(cue_prefix.size());
}

bool subtitle_detail::voice_is_audible(std::optional<double> status, std::optional<bool> paused,
                                      std::optional<double> volume)
{
    // EAtomComponentStatus::Playing, with a live, unpaused playback instance.
    return status == 2.0 && paused == false && volume.value_or(0) > 0.0;
}

void SubtitleReader::reset()
{
    visible_.clear();
    story_window_ = event_script_ = paper_controller_ = {};
    next_owner_lookup_ = {};
}

void SubtitleReader::find_story_owner(ObjectRef controller, ObjectRef window, Clock::time_point now)
{
    if (story_window_ != window) {
        story_window_ = window;
        next_owner_lookup_ = {};
        event_script_ = paper_controller_ = {};
    }

    auto& engine = game_.engine();
    const auto world = world_of(game_, controller);
    if (world_of(game_, event_script_) == world && game_.object(event_script_, "MessageWidget") == window)
        return;
    if (world_of(game_, paper_controller_) == world && game_.object(paper_controller_, "CutTextUi") == window)
        return;
    event_script_ = paper_controller_ = {};
    if (!world || now < next_owner_lookup_) return;
    next_owner_lookup_ = now + lookup_interval;

    const auto controllers = engine.objects("SSEventSceneController", {}, owner_limit + 1, 0, 0, false);
    const auto paper_controllers = engine.objects("SSPaperTheaterSceneController", {}, owner_limit + 1, 0, 0, false);
    if (controllers.size() > owner_limit || paper_controllers.size() > owner_limit) return;
    for (const auto candidate : controllers) {
        if (world_of(game_, candidate) != world) continue;
        const auto script = game_.object(candidate, "Script");
        if (!engine.is_a(script, engine.find_class("SSEventSceneScript")) ||
            world_of(game_, script) != world || game_.object(script, "MessageWidget") != window) continue;
        if (event_script_ && event_script_ != script) {
            event_script_ = {};
            return;
        }
        event_script_ = script;
    }
    for (const auto candidate : paper_controllers) {
        if (world_of(game_, candidate) != world || game_.object(candidate, "CutTextUi") != window) continue;
        if (event_script_ || (paper_controller_ && paper_controller_ != candidate)) {
            event_script_ = paper_controller_ = {};
            return;
        }
        paper_controller_ = candidate;
    }
}

bool SubtitleReader::voiced(const SubtitleLine& line, Clock::time_point now)
{
    auto& engine = game_.engine();
    if (!engine.can_inspect_voice() || !engine.live(line.window) || !engine.live(line.text_widget)) return false;
    const auto instance = engine.game_instance();
    if (!engine.is_a(instance, engine.find_class("SSGameInstance"))) return false;

    // This runtime binds the exact Steam executable. The game obtains the
    // current SSSoundManager through this unreflected SSGameInstance slot.
    constexpr Address sound_manager_offset = 0x920;
    const auto manager = engine.capture(read<Address>(instance.address + sound_manager_offset).value_or(0));
    if (!engine.is_a(manager, engine.find_class("SSSoundManager"))) return false;
    const auto voices = game_.object(manager, "VoiceManager");
    if (!voices) return false;
    const auto controller = local_controller(game_, instance);
    if (!controller) return false;
    const auto key = game_.text_key(line.text_widget);
    if (!key.starts_with("ST_")) return false;

    if (line.event_window) {
        find_story_owner(controller, line.event_window, now);
        if (paper_controller_) {
            // The current CutTextUi message stores a four-byte KoratSoundHandle.
            const auto paper_type = engine.find_class("SSPaperTheaterSceneController");
            const auto property = engine.property(paper_type, "VoiceHandle");
            const auto handle_type = property ? engine.capture(property->child) : ObjectRef{};
            if (!property || property->kind != Kind::Struct || property->size != sizeof(std::uint32_t) ||
                property->count != 1 || engine.name_of(handle_type) != "KoratSoundHandle") return false;
            const auto handle = read<std::uint32_t>(paper_controller_.address + property->offset).value_or(0);
            return playing_voice(game_, voices, handle, key);
        }
        if (event_script_) {
            const auto player_type = engine.find_class("SSEventVoicePlayer");
            for (const auto player : game_.objects(event_script_, "VoicePlayers", voices_per_line)) {
                if (!engine.is_a(player, player_type) ||
                    world_of(game_, player) != world_of(game_, event_script_)) continue;
                // Older voices may remain in this array. Require the active
                // cue to match this line's own string-table key.
                constexpr Address event_player_handle_offset = 0x298;
                const auto handle = read<std::uint32_t>(player.address + event_player_handle_offset).value_or(0);
                if (playing_voice(game_, voices, handle, key)) return true;
            }
        }
        return false;
    }

    // This is the conversation player's own handle, after its native
    // interface offset. These windows are also used by non-conversation
    // writers, so ownership alone is insufficient to suppress their text.
    if (!engine.is_a(controller, engine.find_class("SSBattlePlayerController"))) return false;
    for (const auto field : {"TextAreaUi", "TextAreaUiBattleSetup", "TextAreaUiMainMenu"}) {
        if (game_.object(controller, field) != line.window) continue;
        constexpr Address conversation_handle_offset = 0xd28;
        const auto handle = read<std::uint32_t>(controller.address + conversation_handle_offset).value_or(0);
        return playing_voice(game_, voices, handle, key);
    }
    return false;
}

std::string SubtitleReader::sample(std::span<const SubtitleLine> lines, bool include_voiced,
                                   Clock::time_point now, std::string_view tutorial)
{
    std::string spoken;
    for (const auto& line : lines) {
        if (!line.window || line.text.empty()) continue;
        const auto previous = std::ranges::find_if(visible_, [&](const auto& item) {
            return item.window == line.window && item.text_widget == line.text_widget;
        });
        if (previous != visible_.end()) {
            previous->missing = 0;
            if (previous->text == line.text) continue;
            previous->text = line.text;
        } else {
            visible_.push_back({line.window, line.text_widget, line.text, 0});
        }
        if (line.text == tutorial || (!include_voiced && voiced(line, now))) continue;
        if (!spoken.empty()) spoken += '\n';
        spoken += line.text;
    }

    std::erase_if(visible_, [&](VisibleLine& previous) {
        return !std::ranges::any_of(lines, [&](const SubtitleLine& line) {
            return line.window == previous.window && line.text_widget == previous.text_widget && !line.text.empty();
        }) && ++previous.missing >= missed_samples;
    });
    return spoken;
}

}
