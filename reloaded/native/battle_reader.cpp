#include "battle_reader.h"

#include "game_reader.h"
#include "ui_reader.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sparking {
namespace {

constexpr std::int32_t charge_key = 11; // EKoratBattleKey::Charge
constexpr auto guide_off_interval = std::chrono::milliseconds{750};
constexpr auto guide_paint_interval = std::chrono::milliseconds{160};

// These widget inputs and the side remap are pinned to the executable hash in
// executable.cpp. The setters and HUD update were traced in Ghidra.
constexpr std::size_t display_sides_swapped = 0x70d; // 1427185b0, 142725920
constexpr std::size_t hp_value = 0x488;              // USSHPGauge::SetHP, 1427211d0
constexpr std::size_t sp_value = 0x49c;              // USSSPGauge::SetSPGaugeValue, 142721f90
constexpr std::size_t sp_segment = 0x490;            // USSSPGauge::SetMaxValue, 142721d80
constexpr std::size_t stock_count = 0x4ac;           // USSBlastGauge::SetStockCount, 1427225c0
constexpr std::size_t sparking_child = 0x458;        // HUD follows the bound inner widget, 142725240/142725920
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

    bool attached(ObjectRef item, ObjectRef ancestor = {}) const
    {
        for (unsigned depth = 0; depth < 32 && game.visible(item); ++depth) {
            if (game.number(item, "ColorAndOpacity.A").value_or(1) <= 0.001) return false;
            if (item == ancestor) return true;
            if (std::ranges::find(roots, item) != roots.end()) return !ancestor;
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
    ObjectRef main, hp, ki, skill, sparking_meter, combo, guide;
    bool has_sparking_surface{};
};

struct BattleWidgets {
    std::array<SideWidgets, 2> sides;
    ObjectRef timer, result;
};

struct DisplaySides {
    std::string_view prefix, main, ki, skill, sparking, combo, guide;
};

constexpr DisplaySides battle_sides[]{
    {"HUDWidgetSet.P1", "WBP_Rep_MainGauge_P1_C", "WBP_GRP_SpGauge_P1_C",
     "WBP_Rep_BuGauge2_P1_C", "WBP_OBJ_KingGauge_P1_C", "WBP_Rep_ComboSet_P1_C",
     "WBP_Rep_SkillGuideSet_P1_C"},
    {"HUDWidgetSet.P2", "WBP_Rep_MainGauge_P2_C", "WBP_GRP_SpGauge_P2_C",
     "WBP_Rep_BuGauge2_P2_C", "WBP_OBJ_KingGauge_P2_C", "WBP_Rep_ComboSet_P2_C",
     "WBP_Rep_SkillGuideSet_P2_C"}
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

ObjectRef sparking_widget(const VisibleWidgets& screen, const BattleContext& context, ObjectRef main,
                          const std::string& field, std::string_view class_name)
{
    auto& game = screen.game;
    const auto from_main = game.object(main, "SparkingGauge");
    const auto registered = registered_widget(screen, context, field, "SSSparkingGauge");
    const auto special = game.object(main, "SpecialGaugeSet.SparkingGauge");
    auto painted = viewport_widget(screen, class_name, "SSSparkingGauge");
    if (painted && from_main && painted != from_main && painted != registered && painted != special)
        painted = {};
    for (const auto candidate : {from_main, registered, special, painted}) {
        if (!screen.is_a(candidate, "SSSparkingGauge") || !screen.on_screen(candidate)) continue;
        const auto child = read<Address>(candidate.address + sparking_child);
        if (!child) continue;
        if (*child) {
            const auto meter = game.engine().capture(*child);
            if (screen.is_a(meter, "SSSparkingGauge") && meter != candidate) return meter;
            continue;
        }
        if (candidate != from_main && game.engine().class_name(candidate) != class_name)
            return candidate;
    }
    return {};
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
        if (context.display_side >= 0)
            side.sparking_meter = sparking_widget(screen, context, side.main,
                                                 prefix + "SparkingGauge", names.sparking);
        side.has_sparking_surface = static_cast<bool>(side.sparking_meter);
        side.guide = registered_widget(screen, context, prefix + "SkillGuideWidget", "SSSkillGuideSet");
        side.combo = registered_widget(screen, context, prefix + "ComboSeteWidget", "SSComboSet");
        // Training can attach the charge guide on its own and leave the HUD registration empty.
        if (!side.guide && context.display_side == index)
            side.guide = viewport_widget(screen, names.guide, "SSSkillGuideSet");
        if (!context.hud || !side.main) continue;
        if (!side.ki) side.ki = viewport_widget(screen, names.ki, "SSSPGauge");
        if (!side.skill) side.skill = viewport_widget(screen, names.skill, "SSBlastGauge");
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
    std::optional<int> percent;
};

std::optional<SparkingGauge> sparking(const VisibleWidgets& screen, ObjectRef gauge)
{
    if (!gauge) return {};
    const auto mode = read<std::uint8_t>(gauge.address + sparking_mode);
    if (!mode || *mode > 2) return {};
    if (*mode == 0) return SparkingGauge{};
    if (!screen.on_screen(gauge)) return {};
    const auto max = read<float>(gauge.address + sparking_capacity);
    const auto fill = read<float>(gauge.address + sparking_fill);
    const auto percent = max && fill ? scaled_value(*fill, *max / 100, 100) : std::nullopt;
    return SparkingGauge{*mode, percent};
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
        if (screen.is_a(item, "TextBlock") || screen.is_a(item, "RichTextBlock") ||
            screen.is_a(item, "SpTextBlock")) {
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

struct GuideSlot {
    std::string_view field, name;
};

constexpr std::array guide_slots{
    GuideSlot{"SkillGuideSPSlot1", "SkillGuide_TypeB_01"},
    GuideSlot{"SkillGuideSPSlot2", "SkillGuide_TypeB_02"},
    GuideSlot{"SkillGuideUltimate", "SkillGuide_TypeC_01"},
    GuideSlot{"SkillGuideBStockSlot1", "SkillGuide_TypeA_01"},
    GuideSlot{"SkillGuideBStockSlot2", "SkillGuide_TypeA_02"}
};
constexpr std::size_t ultimate_guide_slot = 2;

ObjectRef visible_child(const VisibleWidgets& screen, ObjectRef root, std::string_view name)
{
    auto& game = screen.game;
    ObjectRef found;
    auto pending = game.widget_children(root);
    for (std::size_t index = 0; index < pending.size() && index < 96; ++index) {
        const auto item = pending[index];
        if (!game.visible(item)) continue;
        if (game.engine().name_of(item) == name && screen.on_screen(item)) {
            if (found && found != item) return {};
            found = item;
        }
        const auto children = game.widget_children(item);
        pending.insert(pending.end(), children.begin(), children.end());
    }
    return found;
}

std::string guide_text(GameReader& game, ObjectRef widget)
{
    if (game.integer(widget, "ColorAndOpacity.ColorUseRule") == 0 &&
        game.number(widget, "ColorAndOpacity.SpecifiedColor.A").value_or(1) <= 0.001) return {};
    auto text = game.text(widget, "Text");
    if (text.empty()) text = game.call_text(widget, "GetText");
    return read_ui_text(text);
}

std::string painted_guide_text(const VisibleWidgets& screen, ObjectRef widget)
{
    return screen.on_screen(widget) ? guide_text(screen.game, widget) : std::string{};
}

std::optional<WidgetBounds> guide_clip(WidgetBounds a, std::optional<WidgetBounds> b)
{
    if (!b) return {};
    const auto left = (std::max)(a.x, b->x);
    const auto top = (std::max)(a.y, b->y);
    const auto right = (std::min)(a.x + a.width, b->x + b->width);
    const auto bottom = (std::min)(a.y + a.height, b->y + b->height);
    if (right - left < 1 || bottom - top < 1) return {};
    return WidgetBounds{left, top, right - left, bottom - top};
}

std::string visible_ultimate_button(GameReader& game, ObjectRef guide)
{
    struct Pending { ObjectRef widget; WidgetBounds clip; bool in_ultimate; };
    const auto clip = game.geometry(guide);
    if (!clip) return {};
    std::vector<Pending> pending{{guide, *clip, false}};
    ObjectRef row;
    std::string glyph;
    for (std::size_t index = 0; index < pending.size() && index < 256; ++index) {
        auto [widget, area, in_ultimate] = pending[index];
        if (!game.visible(widget) || game.number(widget, "ColorAndOpacity.A").value_or(1) <= 0.001) continue;
        const auto name = game.engine().name_of(widget);
        const auto clipping = game.integer(widget, "Clipping").value_or(0);
        if ((clipping >= 1 && clipping <= 3) || game.engine().class_name(widget).ends_with("ScrollBox")) {
            const auto clipped = guide_clip(area, game.geometry(widget));
            if (!clipped) continue;
            area = *clipped;
        }
        if (name == guide_slots[ultimate_guide_slot].name) {
            if (row && row != widget) return {};
            row = widget;
            in_ultimate = true;
        }
        if (in_ultimate && (name == "GuideButton" || name == "GuideButton_01") &&
            game.engine().class_name(widget) == "RichTextBlock" && guide_clip(area, game.geometry(widget))) {
            auto text = guide_text(game, widget);
            if (text.empty() || text.find("Unidentified control") != text.npos) continue;
            if (!glyph.empty() && glyph != text) return {};
            glyph = std::move(text);
            continue;
        }
        for (const auto child : game.widget_children(widget))
            pending.push_back({child, area, in_ultimate});
    }
    return glyph;
}

struct GuideRows {
    std::array<std::string, guide_slots.size()> text;
    std::string ultimate_button;
    bool has_blasts{};
};

GuideRows skill_guide(const VisibleWidgets& screen, ObjectRef guide, bool sparking_charge_popup)
{
    constexpr std::array<std::string_view, 2> titles{"TEXT_SkillName", "TEXT_SkillName_01"};
    constexpr std::array<std::string_view, 2> buttons{"GuideButton", "GuideButton_01"};
    constexpr std::array<std::string_view, 2> button_fields{"GuideButtonText", "GuideButtonText01"};
    GuideRows rows;
    for (std::size_t i = 0; i < guide_slots.size(); ++i) {
        const auto& slot = guide_slots[i];
        const auto bound = screen.game.object(guide, slot.field);
        const auto row = screen.is_a(bound, "SSSkillGuideBase") && screen.attached(bound, guide) &&
                         screen.game.geometry(bound) ? bound : visible_child(screen, guide, slot.name);
        if (i < ultimate_guide_slot && (row || bound)) rows.has_blasts = true;
        if (!screen.is_a(row, "SSSkillGuideBase")) continue;

        auto guide_button = [&](std::size_t variant) {
            const auto key = screen.game.object(row, button_fields[variant]);
            if (screen.is_a(key, "RichTextBlock") && screen.attached(key, row)) {
                auto glyph = painted_guide_text(screen, key);
                if (!glyph.empty()) return glyph;
            }
            const auto painted = visible_child(screen, row, buttons[variant]);
            return screen.is_a(painted, "RichTextBlock") ? painted_guide_text(screen, painted) : std::string{};
        };

        // A slot can present a second style of caption and glyph. Match each
        // painted caption to the glyph variant in its own row.
        for (std::size_t variant = 0; variant < titles.size(); ++variant) {
            auto label = painted_guide_text(screen, visible_child(screen, row, titles[variant]));
            if (label.empty()) continue;
            auto glyph = guide_button(variant);
            if (glyph.empty()) continue;
            rows.text[i] = std::move(label) + ", " + glyph;
            break;
        }
    }
    // The Blueprint can draw its Ultimate button without filling its caption.
    if (sparking_charge_popup && rows.text[ultimate_guide_slot].empty())
        rows.ultimate_button = visible_ultimate_button(screen.game, guide);
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

BattleReading BattleReader::sample(std::span<const ObjectRef> roots, Clock::time_point now)
{
    BattleReading result;
    if (roots.empty()) return result;
    const VisibleWidgets screen{game_, roots};
    const auto context = current_battle(screen);
    const auto widgets = visible_battle(screen, context);
    result.active = widgets.timer || widgets.result || std::ranges::any_of(widgets.sides, [](const auto& side) {
        return side.main || side.hp || side.ki || side.skill || side.has_sparking_surface || side.combo || side.guide;
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
    if (!context.hud || battle_menu(screen)) return result;
    if (context.hud != hud_ || context.controller != controller_) {
        hud_ = context.hud;
        controller_ = context.controller;
        fighters_ = {};
        timer_ = {};
        time_.reset();
        guide_pawn_ = {};
        hold_time_.reset();
        guide_hidden_since_.reset();
        rearm_guide();
    }

    if (context.player_side >= 0) {
        const auto mine = context.pawns[context.player_side];
        const auto held = game_.call_bool(mine, "IsBattleKeyPressed", charge_key);
        if (guide_pawn_ != mine) {
            guide_pawn_ = mine;
            rearm_guide();
            hold_time_.reset();
            guide_hidden_since_.reset();
        }
        if (held == true) {
            const auto hold = game_.call_number(mine, "GetBattleKeyHoldTime", charge_key);
            if (hold && *hold > 0) {
                if (hold_time_ && *hold + 0.001 < *hold_time_) rearm_guide();
                hold_time_ = hold;
            }
        }

        const auto& local_side = widgets.sides[context.display_side];
        const auto local_sparking = sparking(screen, local_side.sparking_meter);
        const auto guide = local_side.guide;
        if (guide) {
            const bool charge_popup =
                game_.engine().class_name(guide) == battle_sides[context.display_side].guide;
            // A continuous hold can hide individual rows or rebuild the visible guide.
            // Without positive native hold evidence, a prolonged absence starts a new read.
            if (held != true && guide_hidden_since_ && now - *guide_hidden_since_ >= guide_off_interval)
                rearm_guide();
            guide_hidden_since_.reset();
            const auto rows = skill_guide(screen, guide, charge_popup && local_sparking && local_sparking->mode == 2);
            for (const auto& row : rows.text) add_once(result.items, row);
            if (!rows.ultimate_button.empty()) add_once(result.items, "Ultimate Blast, " + rows.ultimate_button);
            result.guide_visible = !result.items.empty();
            // The game can mask the pawn's Charge key while this exact HUD is painted.
            if ((held != false || charge_popup) && result.guide_visible) {
                if (!guide_first_row_) guide_first_row_ = now;
                const bool opening = !guide_ultimate_button_announced_ &&
                                     std::ranges::none_of(guide_announced_, [](bool value) { return value; });
                const bool settling = rows.has_blasts && (rows.text[0].empty() || rows.text[1].empty()) &&
                                      now - *guide_first_row_ < guide_paint_interval;
                if (!opening || !settling) {
                    std::array<std::vector<std::string>, 3> groups;
                    for (std::size_t i = 0; i < rows.text.size(); ++i) {
                        if (guide_announced_[i] || rows.text[i].empty()) continue;
                        const std::size_t group = i < ultimate_guide_slot ? 0 : (i == ultimate_guide_slot ? 1 : 2);
                        add_once(groups[group], rows.text[i]);
                        guide_announced_[i] = true;
                    }
                    if (!guide_ultimate_button_announced_ && !guide_announced_[ultimate_guide_slot] &&
                        !rows.ultimate_button.empty()) {
                        add_once(groups[1], rows.ultimate_button);
                        guide_ultimate_button_announced_ = true;
                    }
                    constexpr std::array headings{"Blasts. ", "Ultimate Blast. ", "Skills. "};
                    for (std::size_t group = 0; group < groups.size(); ++group) {
                        if (groups[group].empty()) continue;
                        if (!result.guide.empty()) result.guide += ". ";
                        result.guide += headings[group] + join(groups[group], ". ");
                    }
                    result.guide_opened = opening && !result.guide.empty();
                }
            }
        } else {
            if (!guide_hidden_since_) guide_hidden_since_ = now;
            if (held == false) {
                rearm_guide();
                hold_time_.reset();
            }
        }

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
                if (previous && *shown != *previous && (*shown / step != *previous / step || !*shown))
                    result.announcements.push_back(std::move(text));
                previous = shown;
            };
            meter(fighter.health, health(screen, side.hp),
                  [&](int x) { return gauge_text(owner, "health", x, true); }, 10);
            meter(fighter.ki, ki(screen, side.ki), [&](int x) { return gauge_text(owner, "ki", x); }, 1);
            meter(fighter.skill, skill(screen, side.skill),
                  [&](int x) { return std::string(owner) + " skill count, " + std::to_string(x); }, 1);
            if (const auto gauge = local ? local_sparking : sparking(screen, side.sparking_meter)) {
                const bool active = gauge->mode == 2;
                const auto mode_text = std::string(owner) + " Sparking mode";
                if (active) result.items.push_back(mode_text);
                if (gauge->mode && gauge->percent)
                    result.items.push_back(std::string(owner) + (active ? " Sparking gauge, " : " Sparking charge, ") +
                                           std::to_string(*gauge->percent) + " percent");
                const bool previously_active = fighter.sparking_meter == side.sparking_meter &&
                                               fighter.sparking.value_or(false);
                if (active != previously_active)
                    result.announcements.push_back(active ? mode_text : mode_text + " ended");
                fighter.sparking_meter = side.sparking_meter;
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

void BattleReader::rearm_guide()
{
    guide_announced_.fill(false);
    guide_ultimate_button_announced_ = false;
    guide_first_row_.reset();
}

void BattleReader::reset()
{
    hud_ = controller_ = timer_ = guide_pawn_ = {};
    fighters_ = {};
    time_.reset();
    hold_time_.reset();
    guide_hidden_since_.reset();
    rearm_guide();
    last_result_.clear();
    result_missing_ = 0;
}

}
