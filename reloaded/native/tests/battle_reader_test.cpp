#include "battle_reader.h"
#include "game_reader.h"
#include "ui_reader.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>

namespace {
using namespace sparking;

struct Widget {
    std::string type, name;
    std::string blueprint;
    std::int32_t index{};
    std::map<std::string, ObjectRef, std::less<>> refs;
    std::map<std::string, std::vector<ObjectRef>, std::less<>> arrays;
    std::map<std::string, double, std::less<>> numbers;
    std::map<std::string, bool, std::less<>> flags;
    std::map<std::string, std::string, std::less<>> text;
    std::vector<ObjectRef> children;
    ObjectRef outer{};
    bool shown = true;
    std::array<std::byte, 0x800> storage{};
};

std::map<Address, std::unique_ptr<Widget>> records;
std::map<std::string, ObjectRef, std::less<>> types;
ObjectRef instance;

ObjectRef add(std::string type, std::string name = {})
{
    auto widget = std::make_unique<Widget>();
    widget->name = std::move(name);
    widget->type = std::move(type);
    const auto index = widget->index = static_cast<std::int32_t>(records.size() + 1);
    const auto address = reinterpret_cast<Address>(widget->storage.data());
    records.emplace(address, std::move(widget));
    return {address, index, 1};
}

Widget& data(ObjectRef target) { return *records.at(target.address); }

template<class T>
void store(ObjectRef target, std::size_t offset, T value)
{
    std::memcpy(data(target).storage.data() + offset, &value, sizeof value);
}

void parent(ObjectRef child, ObjectRef target)
{
    const auto slot = add("PanelSlot");
    data(slot).refs["Parent"] = target;
    data(slot).refs["Content"] = child;
    data(child).refs["Slot"] = slot;
    data(target).children.push_back(child);
}

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool contains(const std::vector<std::string>& values, std::string_view expected)
{
    return std::ranges::find(values, expected) != values.end();
}
}

namespace sparking {
ObjectRef Unreal::game_instance() { return instance; }
ObjectRef Unreal::capture(Address address)
{
    return records.contains(address) ? ObjectRef{address, data({address}).index, 1} : ObjectRef{};
}
bool Unreal::live(ObjectRef target) const { return records.contains(target.address); }
ObjectRef Unreal::outer(ObjectRef target) { return target ? data(target).outer : ObjectRef{}; }
ObjectRef Unreal::find_class(std::string_view name)
{
    auto [entry, inserted] = types.try_emplace(std::string(name));
    if (inserted) entry->second = add("Class", std::string(name));
    return entry->second;
}
bool Unreal::is_a(ObjectRef target, ObjectRef type) const
{
    return live(target) && live(type) && data(target).type == data(type).name;
}
std::string Unreal::name_of(ObjectRef target) { return target ? data(target).name : std::string{}; }
std::string Unreal::class_name(ObjectRef target)
{
    if (!target) return {};
    const auto& entry = data(target);
    return entry.blueprint.empty() ? entry.type : entry.blueprint;
}
bool guarded_copy(void* destination, Address address, std::size_t bytes) noexcept
{
    auto entry = records.upper_bound(address);
    if (entry == records.begin()) return false;
    --entry;
    const auto offset = address - entry->first;
    if (offset > entry->second->storage.size() ||
        bytes > entry->second->storage.size() - offset) return false;
    std::memcpy(destination, entry->second->storage.data() + offset, bytes);
    return true;
}
ObjectRef GameReader::object(ObjectRef target, std::string_view path)
{
    if (!target) return {};
    const auto direct = data(target).refs.find(path);
    if (direct != data(target).refs.end()) return direct->second;
    const auto dot = path.find('.');
    if (dot == path.npos) return {};
    const auto owner = object(target, path.substr(0, dot));
    return object(owner, path.substr(dot + 1));
}
std::optional<double> GameReader::number(ObjectRef target, std::string_view path)
{
    if (!target) return {};
    const auto value = data(target).numbers.find(path);
    if (value == data(target).numbers.end()) return {};
    return value->second;
}
std::optional<std::int64_t> GameReader::integer(ObjectRef target, std::string_view path)
{
    const auto value = number(target, path);
    return value ? std::optional(static_cast<std::int64_t>(*value)) : std::nullopt;
}
std::optional<bool> GameReader::boolean(ObjectRef target, std::string_view path)
{
    if (!target) return {};
    const auto value = data(target).flags.find(path);
    if (value == data(target).flags.end()) return {};
    return value->second;
}
std::string GameReader::text(ObjectRef target, std::string_view path)
{
    if (!target) return {};
    const auto value = data(target).text.find(path);
    return value == data(target).text.end() ? std::string{} : value->second;
}
std::vector<ObjectRef> GameReader::objects(ObjectRef target, std::string_view path, std::size_t limit)
{
    if (!target) return {};
    const auto value = data(target).arrays.find(path);
    if (value == data(target).arrays.end()) return {};
    const auto end = (std::min)(limit, value->second.size());
    return {value->second.begin(), value->second.begin() + static_cast<std::ptrdiff_t>(end)};
}
std::string indexed(std::string_view method, std::optional<std::int32_t> argument)
{
    return std::string(method) + (argument ? std::to_string(*argument) : std::string{});
}
ObjectRef GameReader::call_object(ObjectRef target, std::string_view method, std::optional<std::int32_t> argument)
{
    return object(target, indexed(method, argument));
}
std::optional<double> GameReader::call_number(ObjectRef target, std::string_view method, std::optional<std::int32_t> argument)
{
    return number(target, indexed(method, argument));
}
std::optional<bool> GameReader::call_bool(ObjectRef target, std::string_view method, std::optional<std::int32_t> argument)
{
    return boolean(target, indexed(method, argument));
}
std::string GameReader::call_text(ObjectRef target, std::string_view method, std::optional<std::int32_t> argument)
{
    return text(target, indexed(method, argument));
}
bool GameReader::visible(ObjectRef target) { return target && data(target).shown; }
std::vector<ObjectRef> GameReader::widget_children(ObjectRef target)
{
    return target ? data(target).children : std::vector<ObjectRef>{};
}
std::optional<WidgetBounds> GameReader::geometry(ObjectRef target)
{
    if (!visible(target)) return {};
    return WidgetBounds{number(target, "x").value_or(0), 0, 20, 20};
}
std::string read_ui_text(std::string_view value) { return std::string(value); }
}

int main()
{
    using namespace sparking;
    Unreal engine({});
    GameReader game(engine);
    BattleReader reader(game);

    instance = add("SSGameInstance");
    const auto local = add("LocalPlayer");
    const auto controller = add("SSBattlePlayerController");
    const auto hud = add("SSMainGameHUD");
    const auto hud_root = add("SSMainGameHUDWidgetRoot");
    const auto level = add("Level");
    const auto world = add("World");
    const auto state = add("SSBattleGameStateBase");
    const auto enemy = add("SSCharacter");
    const auto player = add("SSCharacter");
    data(instance).arrays["LocalPlayers"] = {local};
    data(local).refs["PlayerController"] = controller;
    data(controller).flags["IsLocalPlayerController"] = true;
    data(controller).numbers["GetPlaySide"] = 1;
    data(controller).outer = level;
    data(controller).refs["MyHUD"] = hud;
    data(controller).refs["Pawn"] = player;
    data(hud).refs["PlayerOwner"] = controller;
    data(hud).refs["HUDCommonWidgetRoot"] = hud_root;
    data(level).refs["OwningWorld"] = world;
    data(world).refs["OwningGameInstance"] = instance;
    data(world).refs["GameState"] = state;
    data(state).refs["GetPlayerCharacter0"] = enemy;
    data(state).refs["GetPlayerCharacter1"] = player;
    data(player).refs["Controller"] = controller;
    data(player).outer = level;
    data(enemy).outer = level;

    // Logical Player 2 is mapped into the first displayed gauge by the game's HUD.
    store(instance, 0x70d, std::uint8_t{1});
    const auto main = add("SSMainGauge");
    const auto hp = add("SSHPGauge");
    const auto image = add("Image");
    data(hud_root).refs["HUDWidgetSet.P1MainGaugeWidget"] = main;
    data(main).refs["HPGauge"] = hp;
    data(hp).refs["CurrentGaugeImage"] = image;
    data(hp).numbers["HPPerGauge"] = 10000;
    store(hp, 0x488, 40000.0f);
    parent(hp, main);
    parent(image, hp);

    const auto sp = add("SSSPGauge");
    std::vector<ObjectRef> segments;
    for (int i = 0; i < 5; ++i) {
        segments.push_back(add("SSSPGaugeParts"));
        parent(segments.back(), sp);
    }
    data(hud_root).refs["HUDWidgetSet.P1SPGauge"] = sp;
    data(sp).arrays["SPGaugeWidgets"] = segments;
    store(sp, 0x490, 1000.0f);
    store(sp, 0x49c, 2300.0f);
    store(sp, 0x3ec, 5000.0f);

    const auto stock = add("SSBlastGauge");
    const auto stock_image = add("Image");
    const auto stock_material = add("MaterialInstanceDynamic");
    data(stock_material).outer = stock_image;
    data(stock_image).refs["Brush.ResourceObject"] = stock_material;
    data(stock).refs["StockCountMaterial"] = stock_material;
    data(hud_root).refs["HUDWidgetSet.P1BlastGaugeSet"] = stock;
    store(stock, 0x4ac, std::int32_t{2});
    parent(stock_image, stock);

    const auto sparking = add("SSSparkingGauge");
    const auto sparking_wrapper = add("SSSparkingGauge");
    data(sparking_wrapper).blueprint = "WBP_OBJ_KingGauge_P1_C";
    data(hud_root).refs["HUDWidgetSet.P1SparkingGauge"] = sparking_wrapper;
    data(main).refs["SparkingGauge"] = sparking_wrapper;
    parent(sparking, sparking_wrapper);
    store(sparking_wrapper, 0x458, sparking.address);
    store(sparking, 0x488, std::uint8_t{1});
    store(sparking, 0x500, 5000.0f);
    store(sparking, 0x504, 2500.0f);

    const auto guide = add("SSSkillGuideSet");
    const auto row = add("SSSkillGuideBase", "SkillGuide_TypeA_01");
    const auto caption = add("TextBlock", "TEXT_SkillName");
    const auto key = add("RichTextBlock", "GuideButton");
    data(hud_root).refs["HUDWidgetSet.P1SkillGuideWidget"] = guide;
    data(guide).refs["SkillGuideBStockSlot1"] = row;
    data(row).refs["GuideButtonText"] = key;
    data(caption).text["Text"] = "Instant Transmission";
    data(key).text["Text"] = "Y";
    parent(row, guide);
    parent(caption, row);
    parent(key, row);
    data(player).flags["IsBattleKeyPressed11"] = true;

    auto frame = reader.sample(std::array{main});
    check(frame.active && contains(frame.items, "Your health, 4 bars"), "player-side remap");
    check(!frame.guide_visible && frame.guide.empty(), "detached guide");
    frame = reader.sample(std::array{main, guide});
    check(frame.guide_visible && frame.guide.find("Instant Transmission") != std::string::npos,
          "first visible guide during a hold");
    check(contains(frame.items, "Instant Transmission, Y"), "manual guide review");

    data(guide).shown = false;
    frame = reader.sample(std::array{main, guide});
    check(!frame.guide_visible && frame.guide.empty(), "hidden guide");
    data(guide).shown = true;
    frame = reader.sample(std::array{main, guide});
    check(frame.guide_visible && frame.guide.empty(), "guide blink during one hold");
    data(player).numbers["GetBattleKeyHoldTime11"] = 2;
    (void)reader.sample(std::array{main, guide});
    data(player).numbers["GetBattleKeyHoldTime11"] = 0.05;
    check(!reader.sample(std::array{main, guide}).guide.empty(), "new hold between polls");
    data(player).flags["IsBattleKeyPressed11"] = false;
    (void)reader.sample(std::array{main});
    data(player).flags["IsBattleKeyPressed11"] = true;
    check(!reader.sample(std::array{main, guide}).guide.empty(), "second hold");

    data(image).shown = false;
    frame = reader.sample(std::array{main});
    check(!contains(frame.items, "Your health, 4 bars"), "individually hidden gauge");
    data(image).shown = true;
    data(state).refs.erase("GetPlayerCharacter1");
    frame = reader.sample(std::array{main, guide});
    check(!contains(frame.items, "Your health, 4 bars") && frame.guide.empty(), "unknown local fighter");
    data(state).refs["GetPlayerCharacter1"] = player;
    data(player).flags["IsBattleKeyPressed11"] = false;
    frame = reader.sample(std::array{main});
    check(frame.announcements.empty(), "missing identity preserves gauge baseline");
    store(hp, 0x488, 30000.0f);
    frame = reader.sample(std::array{main});
    check(contains(frame.announcements, "Your health, 3 bars"), "visible gauge change");

    frame = reader.sample(std::array{main, sp, stock, sparking_wrapper});
    check(contains(frame.items, "Your ki, 2 bars"), "visible native Ki segments");
    check(contains(frame.items, "Your skill count, 2"), "visible native Skill Count");
    check(contains(frame.items, "Your Sparking charge, 50 percent"), "visible native Sparking charge");
    data(player).flags["IsBattleKeyPressed11"] = true;
    store(sp, 0x49c, 3500.0f);
    store(stock, 0x4ac, std::int32_t{3});
    frame = reader.sample(std::array{main, sp, stock, guide});
    check(contains(frame.announcements, "Your ki, 3 bars") &&
          contains(frame.announcements, "Your skill count, 3"), "meter deltas while Charge is held");
    data(player).flags["IsBattleKeyPressed11"] = false;
    data(segments.back()).shown = false;
    data(stock_image).numbers["Brush.DrawAs"] = 0;
    frame = reader.sample(std::array{main, sp, stock, sparking_wrapper});
    check(!contains(frame.items, "Your ki, 3 bars"), "hidden Ki segment prevents an inferred count");
    check(!contains(frame.items, "Your skill count, 3"), "undrawn stock number");
    store(sparking, 0x488, std::uint8_t{2});
    frame = reader.sample(std::array{main, sparking_wrapper});
    check(contains(frame.announcements, "Your Sparking mode"), "visible native Sparking activation");
    check(contains(frame.items, "Your Sparking mode") &&
          contains(frame.items, "Your Sparking gauge, 50 percent"), "stable activation and visible fill");
    store(sparking, 0x504, 2200.0f);
    frame = reader.sample(std::array{main, sparking_wrapper});
    check(contains(frame.items, "Your Sparking mode") &&
          contains(frame.items, "Your Sparking gauge, 44 percent") && frame.announcements.empty(),
          "fill movement keeps stable active state without repeated speech");

    data(sparking).shown = false;
    frame = reader.sample(std::array{main, sparking_wrapper});
    check(!contains(frame.items, "Your Sparking mode") && frame.announcements.empty(),
          "hidden inner gauge cannot announce its active state or infer an end");
    store(sparking, 0x488, std::uint8_t{0});
    data(sparking_wrapper).shown = false;
    frame = reader.sample(std::array{main, sparking_wrapper});
    check(frame.announcements.empty(), "hiding the entire representation prevents end speech");
    data(sparking_wrapper).shown = true;
    frame = reader.sample(std::array{main, sparking_wrapper});
    check(contains(frame.announcements, "Your Sparking mode ended"), "native normal state in visible owned HUD");
    check(reader.sample(std::array{main, sparking_wrapper}).announcements.empty(), "one observed ending");

    const auto timer = add("SSBattleTimer");
    const auto digit0 = add("SSSingleNumber");
    const auto digit1 = add("SSSingleNumber");
    const auto image0 = add("Image");
    const auto image1 = add("Image");
    const auto next1 = add("Image");
    const auto infinity = add("Image");
    std::vector<ObjectRef> digits;
    for (int i = 0; i < 10; ++i) digits.push_back(add("Texture2D"));
    data(hud_root).refs["HUDWidgetSet.TimerWidget"] = timer;
    data(timer).refs["InfinityWidget"] = infinity;
    data(timer).arrays["TexturesNumber"] = digits;
    data(timer).arrays["WidgetNumbers"] = {digit0, digit1};
    data(digit0).refs["CurrentNumberImage"] = image0;
    data(digit1).refs["CurrentNumberImage"] = image1;
    data(digit1).refs["NextNumberImage"] = next1;
    data(image0).refs["Brush.ResourceObject"] = digits[3];
    data(image1).refs["Brush.ResourceObject"] = digits[1];
    data(next1).refs["Brush.ResourceObject"] = digits[0];
    data(image1).numbers["x"] = 12;
    data(next1).numbers["x"] = 12;
    data(next1).shown = false;
    store(timer, 0x3e8, std::int32_t{31});
    store(timer, 0x3f0, std::int32_t{999});
    parent(digit0, timer);
    parent(digit1, timer);
    parent(image0, digit0);
    parent(image1, digit1);
    parent(next1, digit1);
    parent(infinity, timer);
    data(infinity).shown = false;

    frame = reader.sample(std::array{main, timer});
    check(contains(frame.items, "Time remaining, 31 seconds"), "two rendered timer digits");
    store(timer, 0x3e8, std::int32_t{30});
    data(next1).shown = true;
    frame = reader.sample(std::array{main, timer});
    check(!contains(frame.items, "Time remaining, 31 seconds") &&
          !contains(frame.items, "Time remaining, 30 seconds"), "ambiguous digit transition");
    data(next1).shown = false;
    data(image0).refs["Brush.ResourceObject"] = digits[2];
    data(image1).refs["Brush.ResourceObject"] = digits[9];
    store(timer, 0x3e8, std::int32_t{29});
    frame = reader.sample(std::array{main, timer});
    check(contains(frame.announcements, "Time remaining, 29 seconds"), "timer threshold crossed");
    data(infinity).shown = true;
    frame = reader.sample(std::array{main, timer});
    check(contains(frame.items, "No time limit"), "visible infinity timer");

    // This representation is independently attached in Training, while the
    // registered native guide pointer may be unavailable.
    data(hud_root).refs.erase("HUDWidgetSet.P1SkillGuideWidget");
    data(guide).blueprint = "WBP_Rep_SkillGuideSet_P1_C";
    data(caption).type = "SpTextBlock";
    data(player).flags["IsBattleKeyPressed11"] = true;
    frame = reader.sample(std::array{guide});
    check(frame.guide_visible && frame.guide.find("Instant Transmission") != std::string::npos,
          "visible standalone local guide with the native text block");

    // The visible tree also survives an absent slot binding.
    data(guide).refs.erase("SkillGuideBStockSlot1");
    data(player).flags.erase("IsBattleKeyPressed11");
    reader.reset();
    using namespace std::chrono_literals;
    const auto start = BattleReader::Clock::time_point{};
    frame = reader.sample(std::array{guide}, start);
    check(frame.guide_visible && frame.guide.find("Instant Transmission") != std::string::npos &&
          contains(frame.items, "Instant Transmission, Y"), "visible guide fallback without native hold");
    (void)reader.sample(std::array{main}, start + 20ms);
    check(reader.sample(std::array{guide}, start + 200ms).guide.empty(), "short missing-API visibility blink");
    (void)reader.sample(std::array{main}, start + 220ms);
    check(!reader.sample(std::array{guide}, start + 1s).guide.empty(), "long missing-API off interval");
    data(guide).blueprint = "WBP_Rep_SkillGuideSet_P2_C";
    reader.reset();
    frame = reader.sample(std::array{guide}, start);
    check(!frame.guide_visible && frame.guide.empty(), "standalone opponent guide");
    data(guide).blueprint = "WBP_Rep_SkillGuideSet_P1_C";
    data(player).flags["IsBattleKeyPressed11"] = false;
    frame = reader.sample(std::array{guide}, start);
    check(frame.guide_visible && frame.guide.find("Instant Transmission") != std::string::npos,
          "the visible local Charge popup survives a false native key read");
    check(reader.sample(std::array{guide}, start + 80ms).guide.empty(), "one visible hold while key is false");
    (void)reader.sample(std::array{main}, start + 160ms);
    check(!reader.sample(std::array{guide}, start + 320ms).guide.empty(), "native release with hidden popup rearms");

    data(guide).blueprint = "WBP_Rep_TransformGuideSet_P1_C";
    data(hud_root).refs["HUDWidgetSet.P1SkillGuideWidget"] = guide;
    reader.reset();
    frame = reader.sample(std::array{guide}, start);
    check(frame.guide.empty(), "unrecognized visual is ineligible for false-key fallback");

    const auto bind_guide_row = [&](std::string name, std::string field, std::string move,
                                    std::string button, bool bind) {
        const auto entry = add("SSSkillGuideBase", std::move(name));
        const auto title = add("TextBlock", "TEXT_SkillName");
        const auto glyph = add("RichTextBlock", "GuideButton");
        if (bind) data(guide).refs[std::move(field)] = entry;
        data(entry).refs["GuideButtonText"] = glyph;
        data(title).text["Text"] = std::move(move);
        data(glyph).text["Text"] = std::move(button);
        parent(entry, guide);
        parent(title, entry);
        parent(glyph, entry);
        return title;
    };
    const auto blast = bind_guide_row("SkillGuide_TypeB_01", "SkillGuideSPSlot1", "Super Kamehameha", "X", true);
    const auto second_blast = bind_guide_row("SkillGuide_TypeB_02", "SkillGuideSPSlot2", "Dragon Burst", "Y", true);
    const auto ultimate = bind_guide_row("SkillGuide_TypeC_01", "SkillGuideUltimate", "Super Spirit Bomb", "B", false);
    data(guide).blueprint = "WBP_Rep_SkillGuideSet_P1_C";
    data(guide).refs["SkillGuideBStockSlot1"] = row;
    data(ultimate).shown = false;
    data(second_blast).shown = false;
    data(player).flags["IsBattleKeyPressed11"] = true;
    reader.reset();
    frame = reader.sample(std::array{guide}, start);
    check(frame.guide.empty() && !contains(frame.items, "B"), "give the second painted Blast time, without an orphan glyph");
    data(second_blast).shown = true;
    frame = reader.sample(std::array{guide}, start + 80ms);
    check(frame.guide_opened &&
          frame.guide == "Blasts. Super Kamehameha, X. Dragon Burst, Y. Skills. Instant Transmission, Y",
          "first speech puts visible Blasts before support");
    data(ultimate).shown = true;
    frame = reader.sample(std::array{guide}, start + 160ms);
    check(!frame.guide_opened && frame.guide == "Ultimate Blast. Super Spirit Bomb, B" &&
          contains(frame.items, "Super Spirit Bomb, B"), "late attached Ultimate fallback is spoken alone");
    check(reader.sample(std::array{guide}, start + 240ms).guide.empty(), "unchanged popup does not repeat");
    data(ultimate).shown = false;
    (void)reader.sample(std::array{guide}, start + 320ms);
    data(ultimate).shown = true;
    check(reader.sample(std::array{guide}, start + 400ms).guide.empty(), "Ultimate flicker does not repeat");

    data(player).flags["IsBattleKeyPressed11"] = false;
    (void)reader.sample(std::array{main}, start + 500ms);
    data(player).flags["IsBattleKeyPressed11"] = true;
    data(second_blast).shown = false;
    data(ultimate).shown = false;
    frame = reader.sample(std::array{guide}, start + 600ms);
    check(frame.guide.empty(), "first partial visible group has a bounded wait");
    frame = reader.sample(std::array{guide}, start + 950ms);
    check(frame.guide == "Blasts. Super Kamehameha, X. Skills. Instant Transmission, Y" &&
          contains(frame.items, "Super Kamehameha, X"), "one visible Blast speaks after the wait");
    data(blast).shown = false;
    frame = reader.sample(std::array{guide}, start + 1100ms);
    check(!contains(frame.items, "Super Kamehameha, X"), "hidden Blast leaves the manual review");
}
