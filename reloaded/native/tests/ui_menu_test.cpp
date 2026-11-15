#include "ui_reader.h"
#include "game_reader.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

using namespace sparking;

namespace {
struct Widget {
    std::string type, name, base, text;
    std::vector<ObjectRef> children;
    std::unordered_map<std::string, ObjectRef> links;
    WidgetBounds bounds{0, 0, 800, 600};
    bool visible{true}, focused{}, active{true};
    std::uint8_t decision_direction{}, selected_map_icon{};
};

std::vector<Widget> widgets;
std::vector<std::string> classes;

Widget& widget(ObjectRef object) { return widgets.at(object.index); }

ObjectRef make_widget(std::string type, std::string name, ObjectRef parent = {})
{
    const auto index = static_cast<int>(widgets.size());
    const ObjectRef result{static_cast<Address>(index + 1), index, 1};
    Widget entry;
    entry.type = std::move(type);
    entry.name = std::move(name);
    widgets.push_back(std::move(entry));
    if (parent) widget(parent).children.push_back(result);
    return result;
}

ObjectRef rendered(ObjectRef parent, std::string name, std::string text, double y = 20)
{
    auto ref = make_widget("TextBlock", std::move(name), parent);
    widget(ref).text = std::move(text);
    widget(ref).bounds = {25, y, 400, 15};
    return ref;
}

bool focus_path(ObjectRef node)
{
    return widget(node).focused || std::ranges::any_of(widget(node).children, focus_path);
}

void check(bool value, std::string_view message)
{
    if (!value) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

namespace sparking {
bool guarded_copy(void* destination, Address source, std::size_t bytes) noexcept
{
    constexpr Address direction_offset = 0x501;
    constexpr Address icon_offset = 0x448;
    if (bytes != sizeof(std::uint8_t)) return false;
    if (source > direction_offset) {
        const auto index = source - direction_offset - 1;
        if (index < widgets.size() && widgets[index].type == "WBP_GRP_AI_DecisionBranch_0_C") {
            *static_cast<std::uint8_t*>(destination) = widgets[index].decision_direction;
            return true;
        }
    }
    if (source <= icon_offset) return false;
    const auto index = source - icon_offset - 1;
    if (index >= widgets.size() || widgets[index].type != "WBP_OBJ_AI_Map_Piece_00_C") return false;
    *static_cast<std::uint8_t*>(destination) = widgets[index].selected_map_icon;
    return true;
}

bool Unreal::live(ObjectRef owner) const
{
    return owner && owner.index >= 0 && static_cast<std::size_t>(owner.index) < widgets.size();
}

ObjectRef GameReader::object(ObjectRef owner, std::string_view path)
{
    if (!owner) return {};
    const auto found = widget(owner).links.find(std::string(path));
    return found == widget(owner).links.end() ? ObjectRef{} : found->second;
}

std::optional<bool> GameReader::boolean(ObjectRef owner, std::string_view path)
{
    if (path == "bIsActive") return widget(owner).active;
    if (path == "bIsDisableKeyInput") return !widget(owner).active;
    return {};
}

std::optional<double> GameReader::number(ObjectRef, std::string_view) { return {}; }
std::optional<std::int64_t> GameReader::integer(ObjectRef, std::string_view) { return {}; }
std::optional<bool> GameReader::call_bool(ObjectRef owner, std::string_view method, std::optional<std::int32_t>)
{
    if (method == "IsInViewport") return true;
    if (method == "HasKeyboardFocus" || method == "HasAnyUserFocus") return widget(owner).focused;
    if (method == "HasFocusedDescendants")
        return std::ranges::any_of(widget(owner).children, focus_path);
    return {};
}
std::optional<double> GameReader::call_number(ObjectRef, std::string_view, std::optional<std::int32_t>) { return {}; }
std::string GameReader::text(ObjectRef owner, std::string_view path)
{
    return owner && path == "Text" ? widget(owner).text : std::string{};
}
std::string GameReader::call_text(ObjectRef, std::string_view, std::optional<std::int32_t>) { return {}; }
bool GameReader::visible(ObjectRef owner) { return owner && widget(owner).visible; }
std::optional<WidgetBounds> GameReader::geometry(ObjectRef owner) { return widget(owner).bounds; }
std::vector<ObjectRef> GameReader::widget_children(ObjectRef owner) { return widget(owner).children; }
std::vector<ObjectRef> GameReader::objects(ObjectRef, std::string_view, std::size_t) { return {}; }
std::string Unreal::name_of(ObjectRef owner) { return owner ? widget(owner).name : std::string{}; }
std::string Unreal::class_name(ObjectRef owner) { return owner ? widget(owner).type : std::string{}; }
ObjectRef Unreal::find(std::string_view) { return {}; }
ObjectRef Unreal::game_instance() { return {}; }
ObjectRef Unreal::outer(ObjectRef) { return {}; }
ObjectRef Unreal::find_class(std::string_view name)
{
    auto it = std::ranges::find(classes, name);
    if (it == classes.end()) it = classes.insert(classes.end(), std::string(name));
    const auto index = static_cast<int>(it - classes.begin());
    return {static_cast<Address>(10000 + index), index, 1};
}
bool Unreal::is_a(ObjectRef owner, ObjectRef type) const
{
    return owner && type &&
        (widget(owner).type == classes.at(type.index) || widget(owner).base == classes.at(type.index));
}
std::vector<ObjectRef> Unreal::objects(std::string_view, std::string_view, std::size_t, std::uint32_t,
                                       std::uint32_t, bool) { return {}; }
}

int main()
{
    Unreal engine({});
    GameReader game(engine);
    UiReader reader(game);

    auto bonus = make_widget("WBP_GRP_DB_ButtleList_C", "BonusBattleList");
    auto card = make_widget("WBP_OBJ_DB_ButtleThumbnail_C", "WBP_OBJ_DB_ButtleThumbnail_6", bonus);
    rendered(card, "Txt_MainTag", "Schtick");
    rendered(card, "Txt_MainTag_1", "Advanced Level");
    rendered(card, "Txt_TitleText", "Go For It, Mighty Mask!", 60);
    auto second = make_widget("WBP_OBJ_DB_ButtleThumbnail_C", "WBP_OBJ_DB_ButtleThumbnail_7", bonus);
    rendered(second, "Txt_MainTag", "Life-Risking Blow!");
    rendered(second, "Txt_TitleText", "The Almighty Evil Containment Wave", 90);
    widget(card).focused = true;
    auto reading = reader.sample(std::array{bonus});
    check(reading.announcement == "Go For It, Mighty Mask! Schtick. Advanced Level",
          "Focused Bonus Battles card must announce its own title and painted tags");
    widget(card).focused = false;
    widget(second).focused = true;
    reading = reader.sample(std::array{bonus});
    check(reading.announcement == "The Almighty Evil Containment Wave. Life-Risking Blow!",
          "The next Bonus Battles card must read its own text only");
    check(std::ranges::find(reading.items, "Life-Risking Blow!") == reading.items.end(),
          "Review should group the selected Bonus battle tags with their card");

    auto shop = make_widget("WBP_GRP_SH_Top_C", "ShopSelector");
    widget(shop).base = "SSMainMenuSCSelector";
    auto buy = make_widget("WBP_OBJ_SH_BTN_Shop_C", "WBP_OBJ_SH_BTN_Shop", shop);
    auto customize = make_widget("WBP_OBJ_SH_BTN_Customize_C", "WBP_OBJ_SH_BTN_Customize", shop);
    widget(shop).links = {{"ShopButton", buy}, {"CustomizeButton", customize}};
    widget(buy).focused = true;
    reading = reader.sample(std::array{shop});
    check(reading.announcement == "Shop" && std::ranges::find(reading.items, "Customize") != reading.items.end(),
          "Image-only Shop tiles must be named on focus and in review");
    widget(buy).focused = false;
    widget(customize).focused = true;
    reading = reader.sample(std::array{shop});
    check(reading.announcement == "Customize", "The second Shop tile must announce Customize");

    auto technique = make_widget("WBP_GRP_BS_SkillList_DP_C", "SkillList");
    rendered(technique, "Text_Title_1", "Technique Information");
    auto move = make_widget("WBP_OBJ_BS_SetSkillBTN_C", "SkillBTN_0", technique);
    rendered(move, "caption", "Solar Flare", 60);
    auto next_move = make_widget("WBP_OBJ_BS_SetSkillBTN_C", "SkillBTN_Ult_0", technique);
    rendered(next_move, "caption", "Spirit Bomb", 90);
    auto roster = make_widget("WBP_GRP_BS_CharaList_DP_C", "Roster");
    rendered(roster, "Others", "Super Vegeta", 95);
    auto transforms = make_widget("WBP_GRP_BS_ModeList_DP_C", "TransformList");
    rendered(transforms, "Text_Title_1", "Transformation Information", 50);
    reading = reader.sample(std::array{technique, roster, transforms});
    check(reading.root == technique && reading.announcement.find("Solar Flare") != std::string::npos &&
          std::ranges::find(reading.items, "Spirit Bomb") != reading.items.end(),
          "Technique Information needs automatic speech and granular review");
    check(std::ranges::find(reading.items, "Super Vegeta") == reading.items.end() &&
          std::ranges::find(reading.items, "Transformation Information") == reading.items.end(),
          "Technique Information review should exclude the background roster and transformations");

    auto journey = make_widget("WBP_NAD_DifficultyLevel_C", "LimitBreakerDifficulty");
    rendered(journey, "Text_Title", "Difficulty");
    rendered(journey, "Txt_Description", "Choose your difficulty", 65);
    reading = reader.sample(std::array{journey});
    check(reading.announcement == "Difficulty. Choose your difficulty",
          "Limit Breaker Journey must announce a painted difficulty panel without requiring F6");

    auto decision = make_widget("WBP_GRP_AI_DecisionBranch_0_C", "DecisionBranch");
    widget(decision).base = "SSEventSceneDecisionBranch";
    auto left = make_widget("WBP_OBJ_DecisionBranch_Btn_Left_C", "Btn_Left", decision);
    widget(left).links["Caption"] = rendered(left, "ChoiceCaption", "Work with Piccolo", 25);
    auto right = make_widget("WBP_OBJ_DecisionBranch_Btn_Right_C", "Btn_Right", decision);
    auto design = make_widget("Button", "RemoteDisplay", decision);
    widget(right).links["RemoteButton"] = design;
    widget(design).links["Caption"] = rendered(design, "ChoiceCaption", "Go it alone", 55);
    widget(left).focused = true;
    widget(decision).decision_direction = 3;
    reading = reader.sample(std::array{decision});
    check(reading.focus == left && reading.announcement == "Work with Piccolo",
          "A focused episode choice must read its own caption only");
    widget(left).focused = false;
    widget(decision).decision_direction = 4;
    reading = reader.sample(std::array{decision});
    check(reading.focus == right && reading.announcement == "Go it alone",
          "Read the native episode choice when the dialog loses Slate focus");
    check(reading.items == std::vector<std::string>{"Work with Piccolo", "Go it alone"},
          "F6 should review the visible episode choices once each");
    widget(decision).decision_direction = 0;
    reading = reader.sample(std::array{decision});
    check(reading.announcement.empty() && reading.items.size() == 2,
          "An episode decision with no selected direction must not guess a choice");

    auto pause = make_widget("WBP_EventPause_C", "EventPause");
    widget(pause).base = "SSEventScenePauseManager";
    auto play = make_widget("WBP_OBJ_DecisionBranch_Btn_Left_C", "ResumeButton", pause);
    rendered(play, "caption", "Resume", 25);
    auto quit = make_widget("WBP_OBJ_DecisionBranch_Btn_Right_C", "QuitButton", pause);
    rendered(quit, "caption", "Return to the Episode Map", 55);
    widget(quit).focused = true;
    widget(decision).decision_direction = 3;
    reading = reader.sample(std::array{decision, pause});
    check(reading.root == pause && reading.focus == quit && reading.announcement == "Return to the Episode Map",
          "The raised story pause menu must read its selected action over the story choice");
    check(reading.items == std::vector<std::string>{"Resume", "Return to the Episode Map"},
          "F6 on story pause must review the pause options once each");
    auto confirmation = make_widget("WBP_Dialog_000_C", "QuitConfirmation");
    rendered(confirmation, "caption", "Leave this episode?");
    widget(quit).focused = false;
    widget(confirmation).focused = true;
    reading = reader.sample(std::array{decision, pause, confirmation});
    check(reading.root == confirmation && reading.announcement == "Leave this episode?",
          "A focused confirmation must take precedence over the story pause options");

    auto journey_map = make_widget("WBP_NAD_Map_C", "JourneyMap");
    auto spot = make_widget("WBP_OBJ_NAD_Map_Spot_C", "Spot_1", journey_map);
    rendered(spot, "Text_Def_SP_Title", "SP", 20);
    rendered(spot, "Text_Default_SP_Y", "24,000", 45);
    auto hidden_strength = rendered(spot, "Text_Boss_SP", "999,999", 55);
    widget(hidden_strength).visible = false;
    auto other_spot = make_widget("WBP_OBJ_NAD_Map_Spot_C", "Spot_2", journey_map);
    rendered(other_spot, "Text_Boss_SP_Title", "SP", 75);
    rendered(other_spot, "Text_Boss_SP_R", "31,200", 95);
    auto stamina = make_widget("WBP_NAD_Map_Sutamina_C", "MapStamina");
    rendered(stamina, "Caption", "Stamina", 150);
    rendered(stamina, "Text_Value", "3", 175);
    widget(spot).focused = true;
    reading = reader.sample(std::array{journey_map, stamina});
    check(reading.announcement == "SP 24,000" && reading.focus == spot,
          "Journey must read the single painted SP amount on the selected map spot");
    check(std::ranges::find(reading.items, "SP 31,200") != reading.items.end() &&
          std::ranges::find(reading.items, "Stamina") != reading.items.end() &&
          std::ranges::find(reading.items, "999,999") == reading.items.end(),
          "Journey review needs visible map values and the visible HUD");
    widget(spot).focused = false;
    reading = reader.sample(std::array{journey_map, stamina});
    check(reading.announcement.empty(), "An unfocused map should not speak all spot values at once");

    auto chart = make_widget("WBP_GRP_AI_ChartTitle_C", "ChartTitle");
    rendered(chart, "Text_EventTitle", "Underlying battle");
    rendered(chart, "Text_ScenarioTitle_0", "Goku's Saga", 45);
    auto details = make_widget("WBP_GRP_AI_ChartDetails_C", "ChartDetails");
    rendered(details, "CategoryTitleText", "Saiyan Saga");
    auto episode = rendered(details, "TXT_Outline", "Selected episode", 60);
    auto hidden = rendered(details, "TXT_Outline", "Hidden branch", 80);
    widget(hidden).visible = false;
    reading = reader.sample(std::array{chart, details});
    check(reading.root == details && reading.announcement == "Saiyan Saga. Selected episode",
          "Active episode details must take precedence over the map title");
    check(std::ranges::find(reading.items, "Underlying battle") == reading.items.end() &&
          std::ranges::find(reading.items, "Hidden branch") == reading.items.end(),
          "Episode review must stay on the displayed panel");
    widget(episode).text = "Visible Sparking path";
    reading = reader.sample(std::array{chart, details});
    check(reading.announcement == "Saiyan Saga. Visible Sparking path",
          "The displayed branch must update the episode announcement");
    widget(details).active = false;
    reading = reader.sample(std::array{chart, details});
    check(reading.root == chart && reading.announcement == "Underlying battle. Goku's Saga",
          "An inactive panel must not replace the normal episode map");

    auto map = make_widget("WBP_GRP_AI_Map_0000_40_C", "StoryMap");
    widget(map).base = "SSDragonAdventureIFCTMapManager";
    auto first_icon = make_widget("WBP_OBJ_AI_Map_Piece_00_C", "FirstEpisodeIcon", map);
    auto next_icon = make_widget("WBP_OBJ_AI_Map_Piece_00_C", "NextEpisodeIcon", map);
    widget(first_icon).base = widget(next_icon).base = "SSDragonAdventureIFCTMapIconWidget";
    widget(first_icon).selected_map_icon = 1;
    auto outline = make_widget("WBP_OBJ_AI_Map_Outline_C", "MapOutline");
    widget(outline).base = "SSDragonAdventureIFCTMapOutlineManager";
    auto outline_text = rendered(outline, "TXT_Outline", "Goku learns about the Saiyans.", 85);
    widget(chart).children.clear();
    auto title = rendered(chart, "Text_EventTitle", "???");
    rendered(chart, "Text_Chapter", "Chapter 1", 35);
    rendered(chart, "Text_ScenarioTitle_0", "Goku's Saga", 55);
    reading = reader.sample(std::array{chart, map, outline});
    check(reading.root == map && reading.focus == first_icon &&
          reading.announcement == "??? Chapter 1. Goku's Saga. Goku learns about the Saiyans.",
          "The RT map should identify the displayed selection when an episode title is hidden");
    check(std::ranges::count(reading.items, "Goku learns about the Saiyans.") == 1,
          "F6 should review the actual outline once");
    widget(outline_text).text = "Raditz kidnaps Gohan.";
    widget(first_icon).selected_map_icon = 0;
    widget(next_icon).selected_map_icon = 1;
    reading = reader.sample(std::array{chart, map, outline});
    check(reading.focus == next_icon && reading.announcement == "??? Chapter 1. Goku's Saga. Raditz kidnaps Gohan.",
          "Moving between unnamed episodes with different outlines must change automatic speech");
    widget(title).text = "Goku's Brother Raditz";
    reading = reader.sample(std::array{chart, map, outline});
    check(reading.announcement == "Goku's Brother Raditz. Chapter 1. Goku's Saga",
          "A named episode must keep its short navigation announcement");
    widget(next_icon).visible = false;
    reading = reader.sample(std::array{chart, map, outline});
    check(!reading.focus, "A hidden map icon cannot announce a change in selection");

    return EXIT_SUCCESS;
}
