#include "battle_reader.h"

#include "game_reader.h"
#include "ui_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sparking {
namespace {

constexpr std::int32_t charge_key = 11; // EKoratBattleKey::Charge

// These widget inputs and the side remap are pinned to the executable hash in
// executable.cpp. The setters and HUD update were traced in Ghidra.
constexpr std::size_t display_sides_swapped = 0x70d; // 1427185b0, 142725920
constexpr std::size_t hp_value = 0x488;              // USSHPGauge::SetHP, 1427211d0
constexpr std::size_t sp_value = 0x49c;              // USSSPGauge::SetSPGaugeValue, 142721f90
constexpr std::size_t sp_segment = 0x490;            // USSSPGauge::SetMaxValue, 142721d80
constexpr std::size_t stock_count = 0x4ac;           // USSBlastGauge::SetStockCount, 1427225c0
constexpr std::size_t sparking_mode = 0x488;         // USSSparkingGauge::ChangeSparkingActive, 141fd6dd0
constexpr std::size_t sparking_capacity = 0x500;     // USSSparkingGauge::SetMaxValue, 142721e40
constexpr std::size_t sparking_fill = 0x504;         // USSSparkingGauge::SetSparkingChargeValue, 1427224b0
constexpr std::size_t clock_seconds = 0x3e8;          // USSBattleTimer digit update, 1427350b0
constexpr std::size_t clock_limit = 0x3f0;            // USSBattleTimer digit limit, 1427350b0

struct VisibleWidgets {
    GameReader& game;
    std::span<const ObjectRef> roots;

    bool is_a(ObjectRef object, std::string_view type) const
    {
        return object && game.engine().is_a(object, game.engine().find_class(type));
    }

    bool attached(ObjectRef item) const
    {
        for (unsigned depth = 0; depth < 32 && game.visible(item); ++depth) {
            if (game.number(item, "ColorAndOpacity.A").value_or(1) <= 0.001) return false;
            if (std::ranges::find(roots, item) != roots.end()) return true;
            if (const auto slot = game.object(item, "Slot"); slot) {
                const auto parent = game.object(slot, "Parent");
                if (!parent || game.object(slot, "Content") != item) return false;
                if (is_a(parent, "WidgetSwitcher") &&
                    game.call_object(parent, "GetActiveWidget") != item) return false;
                item = parent;
                continue;
            }
            const auto tree = game.engine().outer(item);
            if (!is_a(tree, "WidgetTree") || game.object(tree, "RootWidget") != item) return false;
            item = game.engine().outer(tree);
        }
        return false;
    }

    bool on_screen(ObjectRef widget) const
    {
        return attached(widget) && game.geometry(widget).has_value();
    }

    bool has_brush(ObjectRef image) const
    {
        return on_screen(image) && game.integer(image, "Brush.DrawAs").value_or(3) != 0 &&
               game.number(image, "Brush.TintColor.SpecifiedColor.A").value_or(1) > 0.001;
    }
};

void add_once(std::vector<std::string>& items, std::string item)
{
    if (!item.empty() && std::ranges::find(items, item) == items.end())
        items.push_back(std::move(item));
}

std::string join(const std::vector<std::string>& items, std::string_view separator)
{
    std::string result;
    for (const auto& item : items) {
        if (!result.empty()) result += separator;
        result += item;
    }
    return result;
}

struct BattleContext {
    ObjectRef instance, controller, hud, state;
    std::array<ObjectRef, 2> pawns;
    std::array<ObjectRef, 2> hud_roots;
    int player_side = -1;
    int display_side = -1;
};

BattleContext current_battle(const VisibleWidgets& screen)
{
    BattleContext context;
    auto& game = screen.game;
    auto& engine = game.engine();
    context.instance = engine.game_instance();
    if (!screen.is_a(context.instance, "SSGameInstance")) return {};
    const auto players = game.objects(context.instance, "LocalPlayers", 4);
    if (players.empty()) return {};
    context.controller = game.object(players.front(), "PlayerController");
    if (!screen.is_a(context.controller, "SSBattlePlayerController") ||
        !game.call_bool(context.controller, "IsLocalPlayerController").value_or(false)) return {};
    context.hud = game.object(context.controller, "MyHUD");
    if (!screen.is_a(context.hud, "SSMainGameHUD") ||
        game.object(context.hud, "PlayerOwner") != context.controller) return {};
    if (game.boolean(context.hud, "bShowHUD") == false) return {};
    context.hud_roots = {game.object(context.hud, "HUDCommonWidgetRoot"),
                         game.object(context.hud, "HUDSideWidgetRoot")};

    const auto level = engine.outer(context.controller);
    const auto world = game.object(level, "OwningWorld");
    if (!world || game.object(world, "OwningGameInstance") != context.instance) return context;
    context.state = game.object(world, "GameState");
    if (!screen.is_a(context.state, "SSBattleGameStateBase")) return context;
    const auto side = game.call_number(context.controller, "GetPlaySide");
    if (!side || (*side != 0 && *side != 1)) return context;
    const auto logical_side = static_cast<int>(*side);
    const auto local_pawn = game.call_object(context.state, "GetPlayerCharacter", logical_side);
    if (!screen.is_a(local_pawn, "SSCharacter") ||
        game.object(engine.outer(local_pawn), "OwningWorld") != world) return context;

    // A local controller can possess a camera pawn. The fighting pawn must be
    // the current game-state selection for the same side and belong to that controller.
    const auto pawn = game.object(context.controller, "Pawn");
    const auto pawn_controller = game.object(local_pawn, "Controller");
    const auto viewing_controller = game.object(local_pawn, "ViewingController");
    if (local_pawn != pawn && pawn_controller != context.controller &&
        viewing_controller != context.controller) return context;

    const auto swapped = read<std::uint8_t>(context.instance.address + display_sides_swapped);
    if (!swapped || *swapped > 1) return context;
    context.player_side = logical_side;
    context.display_side = logical_side ^ *swapped;
    context.pawns[logical_side] = local_pawn;
    const auto other = game.call_object(context.state, "GetPlayerCharacter", logical_side ^ 1);
    if (other != local_pawn && screen.is_a(other, "SSCharacter") &&
        game.object(engine.outer(other), "OwningWorld") == world)
        context.pawns[logical_side ^ 1] = other;
    return context;
}

struct SideWidgets {
    ObjectRef main, hp, ki, skill, sparking, combo, guide;
};

struct BattleWidgets {
    std::array<SideWidgets, 2> sides;
    ObjectRef timer, result;
};

struct DisplaySides {
    std::string_view prefix, main, ki, skill, combo, guide;
};

constexpr DisplaySides battle_sides[]{
    {"HUDWidgetSet.P1", "WBP_Rep_MainGauge_P1_C", "WBP_GRP_SpGauge_P1_C",
     "WBP_Rep_BuGauge2_P1_C", "WBP_Rep_ComboSet_P1_C", "WBP_Rep_SkillGuideSet_P1_C"},
    {"HUDWidgetSet.P2", "WBP_Rep_MainGauge_P2_C", "WBP_GRP_SpGauge_P2_C",
     "WBP_Rep_BuGauge2_P2_C", "WBP_Rep_ComboSet_P2_C", "WBP_Rep_SkillGuideSet_P2_C"}
};

ObjectRef viewport_widget(const VisibleWidgets& screen, std::string_view name, std::string_view type)
{
    ObjectRef found;
    for (const auto root : screen.roots) {
        if (screen.game.engine().class_name(root) != name || !screen.is_a(root, type) ||
            !screen.on_screen(root)) continue;
        if (found && found != root) return {};
        found = root;
    }
    return found;
}

ObjectRef registered_widget(const VisibleWidgets& screen, const BattleContext& context,
                            std::string_view field, std::string_view type)
{
    for (const auto root : context.hud_roots) {
        const auto widget = screen.game.object(root, field);
        if (screen.is_a(widget, type) && screen.on_screen(widget)) return widget;
    }
    return {};
}

ObjectRef widget_field(const VisibleWidgets& screen, ObjectRef parent, std::string_view field,
                       std::string_view type)
{
    const auto widget = screen.game.object(parent, field);
    return screen.is_a(widget, type) && screen.on_screen(widget) ? widget : ObjectRef{};
}

BattleWidgets visible_battle(const VisibleWidgets& screen, const BattleContext& context)
{
    auto& game = screen.game;
    BattleWidgets found;
    for (int index = 0; index < 2; ++index) {
        auto& side = found.sides[index];
        const auto& names = battle_sides[index];
        const std::string prefix(names.prefix);
        side.main = registered_widget(screen, context, prefix + "MainGaugeWidget", "SSMainGauge");
        if (!side.main && context.hud) side.main = viewport_widget(screen, names.main, "SSMainGauge");
        side.hp = widget_field(screen, side.main, "HPGauge", "SSHPGauge");
        side.ki = registered_widget(screen, context, prefix + "SPGauge", "SSSPGauge");
        if (!side.ki) side.ki = widget_field(screen, side.main, "SPGauge", "SSSPGauge");
        side.skill = registered_widget(screen, context, prefix + "BlastGaugeSet", "SSBlastGauge");
        if (!side.skill) side.skill = widget_field(screen, side.main, "BlastGauge", "SSBlastGauge");
        side.sparking = registered_widget(screen, context, prefix + "SparkingGauge", "SSSparkingGauge");
        if (!side.sparking)
            side.sparking = widget_field(screen, side.main, "SparkingGauge", "SSSparkingGauge");
        if (!side.sparking) {
            const auto outer_gauge = game.object(side.main, "SparkingGauge");
            side.sparking = widget_field(screen, outer_gauge, "SparkingGauge", "SSSparkingGauge");
        }
        side.guide = registered_widget(screen, context, prefix + "SkillGuideWidget", "SSSkillGuideSet");
        side.combo = registered_widget(screen, context, prefix + "ComboSeteWidget", "SSComboSet");
        if (!context.hud || !side.main) continue;
        if (!side.ki) side.ki = viewport_widget(screen, names.ki, "SSSPGauge");
        if (!side.skill) side.skill = viewport_widget(screen, names.skill, "SSBlastGauge");
        if (!side.guide) side.guide = viewport_widget(screen, names.guide, "SSSkillGuideSet");
        if (!side.combo) side.combo = viewport_widget(screen, names.combo, "SSComboSet");
    }
    found.timer = registered_widget(screen, context, "HUDWidgetSet.TimerWidget", "SSBattleTimer");
    if (!found.timer) found.timer = registered_widget(screen, context, "TimerWidget", "SSBattleTimer");
    for (const auto root : screen.roots) {
        if (!game.visible(root)) continue;
        if (screen.is_a(root, "SSShowBattleResultWidget") && screen.on_screen(root)) found.result = root;
    }
    return found;
}

std::optional<int> scaled_value(double value, double scale, int maximum)
{
    if (!std::isfinite(value) || value < 0 || !std::isfinite(scale) || scale <= 0 ||
        value / scale > maximum + 0.01) return {};
    return static_cast<int>(std::lround(value / scale));
}

std::optional<int> health(const VisibleWidgets& screen, ObjectRef gauge)
{
    if (!gauge || !screen.has_brush(screen.game.object(gauge, "CurrentGaugeImage"))) return {};
    const auto bar = screen.game.number(gauge, "HPPerGauge");
    const auto value = read<float>(gauge.address + hp_value);
    if (!bar || !value) return {};
    const auto tenths = scaled_value(*value, *bar / 10, 200);
    if (tenths == 0 && *value > 0) return 1;
    return tenths;
}

std::optional<int> ki(const VisibleWidgets& screen, ObjectRef gauge)
{
    if (!gauge) return {};
    const auto parts = screen.game.objects(gauge, "SPGaugeWidgets", 12);
    const auto visible = std::ranges::count_if(parts, [&](ObjectRef part) { return screen.on_screen(part); });
    if (!visible || static_cast<std::size_t>(visible) != parts.size()) return {};
    const auto value = read<float>(gauge.address + sp_value);
    const auto segment = read<float>(gauge.address + sp_segment);
    if (!value || !segment || !std::isfinite(*value) || !std::isfinite(*segment) ||
        *value < 0 || *segment <= 0) return {};
    const double bars = static_cast<double>(*value) / *segment;
    return static_cast<int>((std::min)(std::floor(bars + 0.00001), static_cast<double>(visible)));
}

std::optional<int> skill(const VisibleWidgets& screen, ObjectRef gauge)
{
    if (!gauge) return {};
    const auto material = screen.game.object(gauge, "StockCountMaterial");
    if (!material) return {};
    const auto image = screen.game.engine().outer(material);
    if (!screen.is_a(image, "Image") || !screen.has_brush(image) ||
        screen.game.object(image, "Brush.ResourceObject") != material) return {};
    const auto count = read<std::int32_t>(gauge.address + stock_count);
    if (!count || *count < 0 || *count > 99) return {};
    return *count;
}

std::string gauge_text(std::string_view owner, std::string_view meter, int value, bool tenths = false)
{
    const auto whole = tenths ? value / 10 : value;
    std::string amount = std::to_string(whole);
    if (tenths && value % 10) amount += "." + std::to_string(value % 10);
    return std::string(owner) + " " + std::string(meter) + ", " + amount +
           (value == (tenths ? 10 : 1) ? " bar" : " bars");
}

std::string time_text(int value)
{
    return "Time remaining, " + std::to_string(value) + (value == 1 ? " second" : " seconds");
}

struct SparkingGauge {
    std::uint8_t mode{};
    int percent{};
};

std::optional<SparkingGauge> sparking(const VisibleWidgets& screen, ObjectRef gauge)
{
    if (!gauge) return {};
    const auto mode = read<std::uint8_t>(gauge.address + sparking_mode);
    if (!mode || *mode > 2) return {};
    if (*mode == 0) return SparkingGauge{};
    const auto max = read<float>(gauge.address + sparking_capacity);
    const auto fill = read<float>(gauge.address + sparking_fill);
    if (!max || !fill) return {};
    const auto percent = scaled_value(*fill, *max / 100, 100);
    if (!percent || !screen.on_screen(gauge)) return {};
    return SparkingGauge{*mode, *percent};
}

std::optional<int> digits(const VisibleWidgets& screen, ObjectRef number,
                          const std::vector<ObjectRef>& textures, std::string_view field)
{
    struct Digit { double x; int value; };
    std::vector<Digit> rendered;
    for (const auto digit : screen.game.objects(number, field, 12)) {
        if (!screen.attached(digit)) continue;
        std::optional<Digit> painted;
        for (const auto path : {"CurrentNumberImage", "NextNumberImage"}) {
            const auto image = screen.game.object(digit, path);
            if (!screen.has_brush(image)) continue;
            const auto brush = screen.game.object(image, "Brush.ResourceObject");
            if (!brush) continue;
            const auto match = std::ranges::find(textures, brush);
            if (match == textures.end() || match - textures.begin() > 9) return {};
            const auto bounds = screen.game.geometry(image);
            if (!bounds) return {};
            const auto value = static_cast<int>(match - textures.begin());
            if (painted && painted->value != value) return {};
            painted = Digit{bounds->x, value};
        }
        if (!painted) return {};
        rendered.push_back(*painted);
    }
    if (rendered.empty() || rendered.size() > 8) return {};
    std::ranges::sort(rendered, {}, &Digit::x);
    int value = 0;
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        if (i && rendered[i].x - rendered[i - 1].x < 1) return {};
        value = value * 10 + rendered[i].value;
    }
    return value;
}

std::optional<int> shown_number(const VisibleWidgets& screen, ObjectRef widget, bool timer = false)
{
    if (!widget) return {};
    const auto textures = screen.game.objects(widget, "TexturesNumber", 16);
    if (textures.size() < 10) return {};
    const auto first = digits(screen, widget, textures, "WidgetNumbers");
    if (!timer) return first;
    const auto second = digits(screen, widget, textures, "WidgetNumbers2");
    if (first && second && first != second) return {};
    return first ? first : second;
}

std::optional<int> round_clock(const VisibleWidgets& screen, ObjectRef timer)
{
    const auto rendered = shown_number(screen, timer, true);
    const auto seconds = read<std::int32_t>(timer.address + clock_seconds);
    const auto limit = read<std::int32_t>(timer.address + clock_limit);
    if (!seconds || *seconds < 0 || !limit || *limit <= 0) return {};
    const int shown = (std::min)(*seconds, *limit);
    return rendered == shown ? rendered : std::nullopt;
}

std::vector<std::string> visible_text(const VisibleWidgets& screen, ObjectRef root,
                                      std::span<const ObjectRef> excluded = {})
{
    std::vector<ObjectRef> pending{root}, visited;
    std::vector<std::string> result;
    while (!pending.empty() && visited.size() < 96) {
        const auto item = pending.back();
        pending.pop_back();
        if (!screen.game.visible(item) || std::ranges::find(excluded, item) != excluded.end() ||
            std::ranges::find(visited, item) != visited.end()) continue;
        visited.push_back(item);
        if (screen.is_a(item, "TextBlock") || screen.is_a(item, "RichTextBlock")) {
            if (screen.on_screen(item)) {
                auto text = screen.game.text(item, "Text");
                if (text.empty()) text = screen.game.call_text(item, "GetText");
                add_once(result, read_ui_text(text));
            }
        } else {
            const auto children = screen.game.widget_children(item);
            pending.insert(pending.end(), children.rbegin(), children.rend());
        }
    }
    return result;
}

std::vector<std::string> skill_guide(const VisibleWidgets& screen, ObjectRef guide)
{
    constexpr std::string_view slots[]{
        "SkillGuideBStockSlot1", "SkillGuideBStockSlot2", "SkillGuideSPSlot1",
        "SkillGuideSPSlot2", "SkillGuideUltimate"
    };
    std::vector<std::string> rows;
    for (const auto field : slots) {
        const auto row = widget_field(screen, guide, field, "SSSkillGuideBase");
        if (!row) continue;
        const std::array keys{screen.game.object(row, "GuideButtonText"),
                              screen.game.object(row, "GuideButtonText01")};
        auto labels = visible_text(screen, row, keys);
        if (labels.empty()) continue;
        for (const auto key : keys) {
            if (screen.on_screen(key)) {
                auto text = screen.game.text(key, "Text");
                if (text.empty()) text = screen.game.call_text(key, "GetText");
                add_once(labels, read_ui_text(text));
            }
        }
        add_once(rows, join(labels, ", "));
    }
    return rows;
}

bool battle_menu(const VisibleWidgets& screen)
{
    return std::ranges::any_of(screen.roots, [&](ObjectRef root) {
        return (screen.is_a(root, "SSPauseManager") || screen.is_a(root, "SSBattleResult01Manager") ||
                screen.is_a(root, "SSBattleResult02Manager") || screen.is_a(root, "SSBattleResult03Manager") ||
                screen.game.engine().class_name(root) == "WBP_Pause_C") && screen.on_screen(root);
    });
}

}

BattleReading BattleReader::sample(std::span<const ObjectRef> roots)
{
    BattleReading result;
    if (roots.empty()) return result;
    const VisibleWidgets screen{game_, roots};
    const auto context = current_battle(screen);
    const auto widgets = visible_battle(screen, context);
    result.active = widgets.timer || widgets.result || std::ranges::any_of(widgets.sides, [](const auto& side) {
        return side.main || side.hp || side.ki || side.skill || side.sparking || side.combo || side.guide;
    });

    const auto battle_result = widgets.result ? join(visible_text(screen, widgets.result), ". ") : std::string{};
    if (!battle_result.empty()) {
        result_missing_ = 0;
        if (battle_result != last_result_) result.announcements.push_back(battle_result);
        last_result_ = battle_result;
        result.items.push_back(battle_result);
    } else if (result_missing_ < 3 && ++result_missing_ == 3) {
        last_result_.clear();
    }
    if (!context.hud || !result.active || battle_menu(screen)) return result;
    if (context.hud != hud_ || context.controller != controller_) {
        hud_ = context.hud;
        controller_ = context.controller;
        fighters_ = {};
        timer_ = {};
        time_.reset();
        guide_pawn_ = {};
        hold_time_.reset();
        guide_announced_ = false;
    }

    if (context.player_side >= 0) {
        const auto mine = context.pawns[context.player_side];
        const auto held = game_.call_bool(mine, "IsBattleKeyPressed", charge_key);
        const auto hold_time = held == true ? game_.call_number(mine, "GetBattleKeyHoldTime", charge_key)
                                           : std::nullopt;
        if (guide_pawn_ != mine || (hold_time && hold_time_ && *hold_time + 0.001 < *hold_time_)) {
            guide_pawn_ = mine;
            guide_announced_ = false;
            hold_time_.reset();
        }
        if (held == false) {
            guide_announced_ = false;
            hold_time_.reset();
        }
        if (hold_time) hold_time_ = hold_time;

        for (int index = 0; index < 2; ++index) {
            const bool local = index == 0;
            const auto pawn = context.pawns[context.player_side ^ index];
            if (!pawn) continue;
            const auto& side = widgets.sides[context.display_side ^ index];
            auto& fighter = fighters_[index];
            if (pawn != fighter.pawn) {
                fighter = {};
                fighter.pawn = pawn;
            }
            const std::string_view owner = local ? "Your" : "Opponent";
            auto meter = [&](std::optional<int>& previous, std::optional<int> shown, auto describe, int step) {
                if (!shown) return;
                auto text = describe(*shown);
                result.items.push_back(text);
                if (previous && *shown != *previous && (*shown / step != *previous / step || !*shown) &&
                    (!local || held != true))
                    result.announcements.push_back(std::move(text));
                previous = shown;
            };
            meter(fighter.health, health(screen, side.hp),
                  [&](int x) { return gauge_text(owner, "health", x, true); }, 10);
            meter(fighter.ki, ki(screen, side.ki), [&](int x) { return gauge_text(owner, "ki", x); }, 1);
            meter(fighter.skill, skill(screen, side.skill),
                  [&](int x) { return std::string(owner) + " skill count, " + std::to_string(x); }, 1);
            if (const auto gauge = sparking(screen, side.sparking)) {
                const bool active = gauge->mode == 2;
                if (gauge->mode)
                    result.items.push_back(std::string(owner) +
                        (active ? " Sparking mode, " : " Sparking charge, ") +
                        std::to_string(gauge->percent) + " percent");
                if (fighter.sparking.value_or(false) != active)
                    result.announcements.push_back(std::string(owner) +
                        (active ? " Sparking mode" : " Sparking mode ended"));
                fighter.sparking = active;
            }

            const auto combo = widget_field(screen, side.combo, "ComboWidget", "SSCombo");
            const auto hits = shown_number(screen, combo);
            if (hits && *hits >= 2) {
                auto text = std::string(owner) + " combo, " + std::to_string(*hits) + " hits";
                const auto damage = widget_field(screen, side.combo, "DamageWidget", "SSDamage");
                if (const auto value = shown_number(screen, damage))
                    text += ", " + std::to_string(*value) + " damage";
                result.items.push_back(text);
                if (text == fighter.combo && !fighter.combo_announced) {
                    result.announcements.push_back(text);
                    fighter.combo_announced = true;
                }
                if (text != fighter.combo) fighter.combo_announced = false;
                fighter.combo = std::move(text);
            } else if (!combo) {
                fighter.combo.clear();
                fighter.combo_announced = false;
            }
        }

        if (const auto guide = widgets.sides[context.display_side].guide) {
            const auto rows = skill_guide(screen, guide);
            result.guide_visible = !rows.empty();
            result.items.insert(result.items.end(), rows.begin(), rows.end());
            if (held == true && !guide_announced_ && !rows.empty()) {
                result.guide = "Skill guide. " + join(rows, ". ");
                guide_announced_ = true;
            }
        }
    }

    if (widgets.timer != timer_) {
        timer_ = widgets.timer;
        time_.reset();
    }
    if (timer_) {
        if (screen.on_screen(game_.object(timer_, "InfinityWidget"))) {
            result.items.emplace_back("No time limit");
            time_.reset();
        } else if (const auto seconds = round_clock(screen, timer_); seconds && *seconds <= 999) {
            result.items.push_back(time_text(*seconds));
            if (time_ && *seconds < *time_ && (*seconds == 0 ||
                (*seconds <= 10 && *time_ > 10) || (*seconds <= 30 && *time_ > 30)))
                result.announcements.push_back(time_text(*seconds));
            time_ = seconds;
        }
    }
    return result;
}

void BattleReader::reset()
{
    hud_ = controller_ = timer_ = guide_pawn_ = {};
    fighters_ = {};
    time_.reset();
    hold_time_.reset();
    guide_announced_ = false;
    last_result_.clear();
    result_missing_ = 0;
}

}
