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
const ObjectRef singleton{0x1a0000, 18, 1};
const ObjectRef level_script{0x1b0000, 19, 1};
const ObjectRef sequence_player{0x1c0000, 20, 1};
constexpr Address conversation_rows = 0x220000;
constexpr Address spawned_voices = 0x230000;

std::string key, cue, row_key, suffix;
double playback_status = 2., volume = 1.;
bool paused{}, paper_scene{}, event_scene{}, conversation{}, unknown_world{}, use_sequence{};
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
    row_key = key;
    suffix.clear();
    use_sequence = false;
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
    if (size == sizeof(ArrayHeader)) {
        if (address == level_script.address + 0xc98) {
            *static_cast<ArrayHeader*>(output) = {conversation_rows, 1, 1};
            return true;
        }
        if (use_sequence && address == controller.address + 0xd18) {
            *static_cast<ArrayHeader*>(output) = {spawned_voices, 1, 1};
            return true;
        }
    }
    if (use_sequence && address == spawned_voices && size == sizeof(std::array<std::int32_t, 2>)) {
        *static_cast<std::array<std::int32_t, 2>*>(output) = {sequence_player.index, sequence_player.serial};
        return true;
    }
    if (size == sizeof(std::uint32_t) &&
        (address == voice_player.address + 0x298 || address == paper.address + 0x638 ||
         address == controller.address + 0xd28 || address == controller.address + 0xd08 ||
         address == sequence_player.address + 0x408)) {
        *static_cast<std::uint32_t*>(output) =
            address == controller.address + 0xd08 || (use_sequence && address == controller.address + 0xd28) ? 0 : 7;
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
std::string Unreal::class_name(ObjectRef) { return "CaptionWindow"; }
ObjectRef Unreal::game_instance() { return instance; }
ObjectRef Unreal::game_singleton() { return singleton; }
ObjectRef Unreal::weak(std::int32_t index, std::int32_t serial) const
{
    return index == sequence_player.index && serial == sequence_player.serial ? sequence_player : ObjectRef{};
}
std::string Unreal::text_key(Address address)
{
    return address == conversation_rows + 0x38 ? row_key : std::string{};
}
std::optional<std::string> Unreal::string(Address address) const
{
    return address == conversation_rows + 0xf8 ? std::optional{suffix} : std::nullopt;
}
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
    if (owner == singleton && field == "SoundPlayer") return manager;
    if (owner == ::manager && field == "VoiceManager") return voice_manager;
    if (owner == ::level && field == "OwningWorld") return world;
    if (owner == ::world && field == "OwningGameInstance" && !unknown_world) return instance;
    if (owner == ::world && field == "PersistentLevel") return level;
    if (owner == ::level && field == "LevelScriptActor") return level_script;
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
          "A voiced event line must resolve SoundPlayer from the game singleton");
    check(reader.sample(lines, true, now).empty(), "Changing F9 replayed a suppressed line");

    reader.reset();
    playback_status = 1.;
    check(reader.sample(lines, false, now).empty(),
          "A caption with its own preparing voice must wait for playback");
    playback_status = 2.;
    check(reader.sample(lines, false, now + std::chrono::milliseconds(80)).empty(),
          "The late-starting exact voice should suppress its caption");
    check(reader.sample(lines, false, now + std::chrono::seconds(1)).empty(),
          "A caption suppressed after preparation must stay handled");
    reader.reset();
    playback_status = 1.;
    check(reader.sample(lines, false, now).empty(), "The matching voice preparation did not defer speech");
    playback_status = 4.;
    check(reader.sample(lines, false, now + std::chrono::milliseconds(80)) == lines[0].text,
          "A failed voice start must let the visible text be read");
    check(reader.sample(lines, false, now + std::chrono::milliseconds(160)).empty(),
          "The failed voice's caption repeated");
    reader.reset();
    playback_status = 1.;
    check(reader.sample(lines, false, now).empty() &&
          reader.sample(lines, false, now + std::chrono::seconds(1)) == lines[0].text,
          "A voice that stays preparing must not hide the text indefinitely");

    scenario();
    reader.reset();
    check(reader.sample(lines, false, now).empty(), "Expected a suppressed baseline caption");

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
    cue = "Line_007";
    use_sequence = true;
    check(reader.sample(conversation_lines, false, now).empty(),
          "An active sound actor with the exact stripped conversation cue was ignored");
    reader.reset();
    cue = "ADVIF_Line_007_alt";
    suffix = "_alt";
    check(reader.sample(conversation_lines, false, now).empty(),
          "The voice suffix on the current matching conversation row was ignored");
    reader.reset();
    row_key = "ST_Line_008";
    check(reader.sample(conversation_lines, false, now) == conversation_lines[0].text,
          "A suffix from a different conversation row hid visible text");
    reader.reset();
    conversation = false;
    check(reader.sample(conversation_lines, false, now) == conversation_lines[0].text,
          "An unrelated conversation handle hid dialogue");
}
