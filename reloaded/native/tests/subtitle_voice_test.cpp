#include "subtitle_reader.h"
#include "game_reader.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace sparking;

const ObjectRef instance{0x80000, 1, 1};
const ObjectRef controller{0x90000, 2, 1};
const ObjectRef level{0xa0000, 3, 1};
const ObjectRef world{0xb0000, 4, 1};
const ObjectRef player{0xc0000, 5, 1};
const ObjectRef manager{0xd0000, 6, 1};
const ObjectRef voice_manager{0xe0000, 7, 1};
const ObjectRef voice_component{0xf0000, 8, 1};
const ObjectRef paper{0x110000, 9, 1};
const ObjectRef script{0x120000, 10, 1};
const ObjectRef scene_controller{0x130000, 11, 1};
const ObjectRef voice_player{0x140000, 12, 1};
const ObjectRef sound_handle_type{0x150000, 13, 1};
const ObjectRef story_window{0x160000, 14, 1};
const ObjectRef window{0x170000, 15, 1};
const ObjectRef text_widget{0x180000, 16, 1};
const ObjectRef conversation_window{0x190000, 17, 1};

std::string key, cue;
double playback_status = 2., volume = 1.;
bool paused{}, paper_scene{}, event_scene{}, conversation{}, unknown_world{};
unsigned lookups{}, inspected_voices{};

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

TextKey* key_getter(TextKey*, Address) { return nullptr; }
std::array<std::int32_t, 2>* voice_getter(Address, std::array<std::int32_t, 2>*, const std::uint32_t*)
{
    return nullptr;
}

void scenario()
{
    key = "ST_Line_007";
    cue = "ADVIF_Line_007";
    playback_status = 2.;
    volume = 1.;
    paused = unknown_world = paper_scene = conversation = false;
    event_scene = true;
    lookups = inspected_voices = 0;
}
}

namespace sparking {
bool guarded_copy(void* output, Address address, std::size_t size) noexcept
{
    if (size == sizeof(Address) && address == instance.address + 0x920) {
        *static_cast<Address*>(output) = manager.address;
        return true;
    }
    if (size == sizeof(std::uint32_t) &&
        (address == voice_player.address + 0x298 || address == paper.address + 0x638 ||
         address == controller.address + 0xd28)) {
        *static_cast<std::uint32_t*>(output) = 7;
        return true;
    }
    return false;
}
bool Unreal::live(ObjectRef object) const { return static_cast<bool>(object); }
ObjectRef Unreal::capture(Address address)
{
    if (address == manager.address) return manager;
    if (address == sound_handle_type.address) return sound_handle_type;
    return {};
}
ObjectRef Unreal::game_instance() { return instance; }
ObjectRef Unreal::find_class(std::string_view)
{
    return ObjectRef{0x200000, 29, 1};
}
bool Unreal::is_a(ObjectRef object, ObjectRef type) const { return object && type; }
ObjectRef Unreal::outer(ObjectRef object) { return object ? level : ObjectRef{}; }
std::string Unreal::name_of(ObjectRef object)
{
    return object == sound_handle_type ? "KoratSoundHandle" : "";
}
std::optional<Field> Unreal::property(ObjectRef, std::string_view name)
{
    if (name != "VoiceHandle") return {};
    Field handle;
    handle.kind = Kind::Struct;
    handle.offset = 0x638;
    handle.size = 4;
    handle.count = 1;
    handle.child = sound_handle_type.address;
    return handle;
}
std::vector<ObjectRef> Unreal::objects(std::string_view type, std::string_view, std::size_t,
                                       std::uint32_t, std::uint32_t, bool)
{
    ++lookups;
    if (event_scene && type == "SSEventSceneController") return {scene_controller};
    if (paper_scene && type == "SSPaperTheaterSceneController") return {paper};
    return {};
}
ObjectRef Unreal::voice_component(ObjectRef owner, std::uint32_t handle)
{
    ++inspected_voices;
    return owner == voice_manager && handle == 7 ? ::voice_component : ObjectRef{};
}

ObjectRef GameReader::object(ObjectRef owner, std::string_view field)
{
    if (owner == ::player && field == "PlayerController") return controller;
    if (owner == ::manager && field == "VoiceManager") return voice_manager;
    if (owner == ::level && field == "OwningWorld") return world;
    if (owner == ::world && field == "OwningGameInstance" && !unknown_world) return instance;
    if (owner == scene_controller && field == "Script") return script;
    if (owner == script && field == "MessageWidget") return story_window;
    if (owner == paper && field == "CutTextUi") return story_window;
    if (conversation && owner == controller && field == "TextAreaUi") return conversation_window;
    return {};
}
std::vector<ObjectRef> GameReader::objects(ObjectRef owner, std::string_view field, std::size_t)
{
    if (owner == instance && field == "LocalPlayers") return {player};
    if (owner == script && field == "VoicePlayers") return {voice_player};
    return {};
}
std::string GameReader::text_key(ObjectRef owner)
{
    return owner == text_widget ? key : std::string{};
}
std::string GameReader::call_text(ObjectRef owner, std::string_view method, std::optional<std::int32_t>)
{
    return owner == ::voice_component && method == "GetCueName" ? cue : "";
}
std::optional<double> GameReader::call_number(ObjectRef owner, std::string_view method, std::optional<std::int32_t>)
{
    if (owner != ::voice_component) return {};
    if (method == "GetStatus") return playback_status;
    if (method == "GetVolume") return volume;
    return {};
}
std::optional<bool> GameReader::call_bool(ObjectRef owner, std::string_view method, std::optional<std::int32_t>)
{
    if (owner == ::voice_component && method == "IsPaused") return paused;
    return {};
}
}

int main()
{
    using namespace sparking;
    EngineApi api{};
    api.text_key = key_getter;
    api.voice_component = voice_getter;
    Unreal engine(api);
    GameReader game(engine);
    SubtitleReader reader(game);
    const auto now = SubtitleReader::Clock::time_point{} + std::chrono::seconds(1);
    const std::array lines{SubtitleLine{window, text_widget, story_window, "A story line."}};
    scenario();
    check(reader.sample(lines, true, now) == lines[0].text && inspected_voices == 0,
          "Reading all dialogue performed a native voice lookup");
    reader.reset();
    check(reader.sample(lines, false, now).empty() && inspected_voices == 1,
          "The exact voiced event line was read over the game's dialogue");
    check(reader.sample(lines, true, now).empty(), "Changing F9 replayed a suppressed line");

    auto next = lines;
    next[0].text = "A different story line.";
    key = "ST_Line_008";
    check(reader.sample(next, false, now) == next[0].text && lookups == 2,
          "A retained EventScene voice suppressed a different displayed line");

    scenario();
    reader.reset();
    key.clear();
    check(reader.sample(lines, false, now) == lines[0].text && inspected_voices == 0,
          "A line with no native identity was hidden");
    scenario();
    reader.reset();
    unknown_world = true;
    check(reader.sample(lines, false, now) == lines[0].text && inspected_voices == 0,
          "A voice without a current-world owner hid the line");

    scenario();
    reader.reset();
    event_scene = false;
    paper_scene = true;
    check(reader.sample(lines, false, now).empty() && inspected_voices == 1,
          "A matching Paper Theater voice was announced twice");
    reader.reset();
    paused = true;
    check(reader.sample(lines, false, now) == lines[0].text,
          "A paused story voice hid visible story text");

    scenario();
    reader.reset();
    paper_scene = true;
    check(reader.sample(lines, false, now) == lines[0].text && inspected_voices == 0,
          "Ambiguous story ownership hid the line");

    scenario();
    reader.reset();
    conversation = true;
    const std::array conversation_lines{SubtitleLine{conversation_window, text_widget, {}, "Ready?"}};
    check(reader.sample(conversation_lines, false, now).empty() && inspected_voices == 1,
          "A matching battle or menu voice was announced twice");
    reader.reset();
    conversation = false;
    check(reader.sample(conversation_lines, false, now) == conversation_lines[0].text,
          "An unrelated conversation handle hid dialogue");
}
