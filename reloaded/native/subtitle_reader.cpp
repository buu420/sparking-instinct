#include "subtitle_reader.h"
#include "game_reader.h"

#include <algorithm>

namespace sparking {
namespace {
using namespace std::chrono_literals;
using subtitle_detail::Playback;
using subtitle_detail::Voice;
constexpr std::size_t owner_limit = 32;
constexpr std::size_t voices_per_line = 8;
constexpr unsigned missed_samples = 3;
constexpr auto lookup_interval = 500ms;
constexpr auto voice_start_wait = 450ms;

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

Voice inspect_voice(GameReader& game, ObjectRef voice_manager, std::uint32_t handle, std::string_view key,
                    bool conversation = false, std::string_view suffix = {})
{
    const auto component = game.engine().voice_component(voice_manager, handle);
    if (!component) return {Playback::none, "voice handle unavailable"};
    const auto cue = game.call_text(component, "GetCueName");
    const auto matches = [&](std::string_view candidate) {
        return subtitle_detail::matches_voice_cue(key, candidate) ||
            (conversation && key.starts_with("ST_") && key.size() > 3 && candidate == key.substr(3));
    };
    if (!matches(cue) && (suffix.empty() || !cue.ends_with(suffix) ||
                          !matches(std::string_view(cue).substr(0, cue.size() - suffix.size()))))
        return {Playback::none, "voice cue did not match the caption"};
    const auto status = game.call_number(component, "GetStatus");
    const auto paused = game.call_bool(component, "IsPaused");
    const auto volume = game.call_number(component, "GetVolume");
    if (subtitle_detail::voice_is_audible(status, paused, volume))
        return {Playback::playing, "matching voice playing"};
    if (status == 1.0 && paused == false && volume.value_or(0) > 0.0)
        return {Playback::preparing, "matching voice preparing"};
    return {Playback::none, "matching voice was not audible"};
}

std::string conversation_suffix(GameReader& game, ObjectRef controller, std::string_view key)
{
    // The tick at 1421ed710 selects its row from the persistent level's
    // BattleConversationData. Only inspect the current, matching caption.
    auto& engine = game.engine();
    const auto level = game.object(world_of(game, controller), "PersistentLevel");
    const auto script = game.object(level, "LevelScriptActor");
    if (!engine.is_a(script, engine.find_class("SSBattleLevelScriptActor"))) return {};
    const auto rows = read<ArrayHeader>(script.address + 0xc98);
    const auto index = read<std::int32_t>(controller.address + 0xd08);
    if (!rows || !rows->data || rows->count <= 0 || rows->count > 8192 || rows->capacity < rows->count ||
        !index || *index < 0 || *index >= rows->count) return {};
    constexpr std::size_t conversation_size = 0x108;
    const auto row = rows->data + static_cast<std::size_t>(*index) * conversation_size;
    if (engine.text_key(row + 0x38) != key) return {}; // KoratBattleConversation.Text
    return engine.string(row + 0xf8).value_or("");     // KoratBattleConversation.SoundNameSuffix
}

Voice conversation_voice(GameReader& game, ObjectRef controller, ObjectRef voices, std::string_view key)
{
    const auto suffix = conversation_suffix(game, controller, key);
    // 1421fc270 considers the direct handle and the conversation's weak sound actors.
    const auto handle = read<std::uint32_t>(controller.address + 0xd28).value_or(0);
    auto current = inspect_voice(game, voices, handle, key, true, suffix);
    if (current.playback == Playback::playing) return current;
    const auto players = read<ArrayHeader>(controller.address + 0xd18);
    if (!players || !players->data || players->count <= 0 || players->capacity < players->count)
        return current;
    auto& engine = game.engine();
    const auto player_type = engine.find_class("KoratSequenceSoundPlayer");
    const auto world = world_of(game, controller);
    for (std::size_t i = 0; i < (std::min)(voices_per_line, static_cast<std::size_t>(players->count)); ++i) {
        const auto weak = read<std::array<std::int32_t, 2>>(players->data + i * sizeof(std::array<std::int32_t, 2>));
        const auto player = weak ? engine.weak((*weak)[0], (*weak)[1]) : ObjectRef{};
        if (!engine.is_a(player, player_type) || world_of(game, player) != world) continue;
        const auto player_handle = read<std::uint32_t>(player.address + 0x408).value_or(0);
        const auto observed = inspect_voice(game, voices, player_handle, key, true, suffix);
        if (observed.playback == Playback::playing) return observed;
        if (observed.playback == Playback::preparing) current = observed;
    }
    return current;
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

Voice SubtitleReader::voice(const SubtitleLine& line, Clock::time_point now)
{
    auto& engine = game_.engine();
    if (!engine.can_inspect_voice() || !engine.live(line.window) || !engine.live(line.text_widget))
        return {Playback::none, "native caption inspection unavailable"};
    const auto key = game_.text_key(line.text_widget);
    if (!key.starts_with("ST_") || key.size() <= 3)
        return {Playback::none, "caption has no voice key"};
    const auto instance = engine.game_instance();
    if (!engine.is_a(instance, engine.find_class("SSGameInstance")))
        return {Playback::none, "game instance unavailable"};

    // 14245f8e0 resolves the engine's Korat game singleton. The dialogue
    // playback paths use its reflected SoundPlayer, independently of the world.
    const auto manager = game_.object(engine.game_singleton(), "SoundPlayer");
    if (!engine.is_a(manager, engine.find_class("SSSoundManager")))
        return {Playback::none, "sound manager unavailable"};
    const auto voices = game_.object(manager, "VoiceManager");
    if (!voices) return {Playback::none, "voice manager unavailable"};
    const auto controller = local_controller(game_, instance);
    if (!controller) return {Playback::none, "current local player unavailable"};

    if (line.event_window) {
        find_story_owner(controller, line.event_window, now);
        if (paper_controller_) {
            // The current CutTextUi message stores a four-byte KoratSoundHandle.
            const auto paper_type = engine.find_class("SSPaperTheaterSceneController");
            const auto property = engine.property(paper_type, "VoiceHandle");
            const auto handle_type = property ? engine.capture(property->child) : ObjectRef{};
            if (!property || property->kind != Kind::Struct || property->size != sizeof(std::uint32_t) ||
                property->count != 1 || engine.name_of(handle_type) != "KoratSoundHandle")
                return {Playback::none, "story voice handle unavailable"};
            const auto handle = read<std::uint32_t>(paper_controller_.address + property->offset).value_or(0);
            return inspect_voice(game_, voices, handle, key);
        }
        if (event_script_) {
            Voice current{Playback::none, "no matching event voice"};
            const auto player_type = engine.find_class("SSEventVoicePlayer");
            const auto world = world_of(game_, event_script_);
            for (const auto player : game_.objects(event_script_, "VoicePlayers", voices_per_line)) {
                if (!engine.is_a(player, player_type) || world_of(game_, player) != world) continue;
                // The event script can retain voices for earlier lines.
                constexpr Address event_player_handle_offset = 0x298;
                const auto handle = read<std::uint32_t>(player.address + event_player_handle_offset).value_or(0);
                const auto observed = inspect_voice(game_, voices, handle, key);
                if (observed.playback == Playback::playing) return observed;
                if (observed.playback == Playback::preparing) current = observed;
            }
            return current;
        }
        return {Playback::none, "current story caption owner unavailable"};
    }

    if (engine.is_a(controller, engine.find_class("SSBattlePlayerController"))) {
        for (const auto field : {"TextAreaUi", "TextAreaUiBattleSetup", "TextAreaUiMainMenu"})
            if (game_.object(controller, field) == line.window)
                return conversation_voice(game_, controller, voices, key);
    }
    return {Playback::none, "caption is outside the conversation player's windows"};
}

std::string SubtitleReader::sample(std::span<const SubtitleLine> lines, bool include_voiced,
                                   Clock::time_point now, std::string_view tutorial, std::string* diagnostics)
{
    std::string spoken;
    for (const auto& line : lines) {
        if (!line.window || line.text.empty()) continue;
        auto current = std::ranges::find_if(visible_, [&](const auto& item) {
            return item.window == line.window && item.text_widget == line.text_widget;
        });
        if (current == visible_.end()) {
            visible_.push_back({line.window, line.text_widget, line.text});
            current = visible_.end() - 1;
        } else {
            current->missing = 0;
            if (current->text != line.text) {
                current->text = line.text;
                current->voice_deadline = {};
            } else if (current->voice_deadline == Clock::time_point{}) continue;
        }
        if (line.text == tutorial) {
            current->voice_deadline = {};
            continue;
        }
        const auto observed = include_voiced ? Voice{} : voice(line, now);
        if (observed.playback == Playback::preparing) {
            if (current->voice_deadline == Clock::time_point{}) current->voice_deadline = now + voice_start_wait;
            if (now < current->voice_deadline) continue;
        }
        current->voice_deadline = {};
        if (!include_voiced && diagnostics) {
            if (!diagnostics->empty()) *diagnostics += '\n';
            *diagnostics += observed.playback == Playback::playing ? "Subtitle filter kept game voice | " :
                                                                   "Subtitle filter read text | ";
            *diagnostics += game_.engine().class_name(line.window);
            *diagnostics += " | ";
            *diagnostics += observed.playback == Playback::preparing ? "voice start timed out" : observed.evidence;
        }
        if (observed.playback == Playback::playing) continue;
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
