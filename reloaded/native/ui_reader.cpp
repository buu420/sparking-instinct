#include "ui_reader.h"

#include "game_reader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <unordered_set>
#include <utility>

namespace sparking {
namespace {

using namespace std::string_view_literals;

bool one_of(std::string_view value, std::initializer_list<std::string_view> candidates)
{
    return std::find(candidates.begin(), candidates.end(), value) != candidates.end();
}

constexpr bool space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

constexpr char lower(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool equals_ascii(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char x, char y) { return lower(x) == lower(y); });
}

bool name_char(char c)
{
    return (lower(c) >= 'a' && lower(c) <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

std::string_view attribute(std::string_view tag, std::string_view key)
{
    while (!tag.empty()) {
        if (!name_char(tag.front())) {
            tag.remove_prefix(1);
            continue;
        }
        const auto start = tag.begin();
        const auto end = std::find_if_not(start, tag.end(), name_char);
        const auto name = tag.substr(0, static_cast<std::size_t>(end - start));
        tag.remove_prefix(name.size());
        while (!tag.empty() && space(tag.front())) tag.remove_prefix(1);
        if (tag.empty() || tag.front() != '=') continue;
        tag.remove_prefix(1);
        while (!tag.empty() && space(tag.front())) tag.remove_prefix(1);
        if (tag.empty() || (tag.front() != '"' && tag.front() != '\'')) return {};
        const auto length = tag.find(tag.front(), 1);
        if (length == tag.npos) return {};
        if (equals_ascii(name, key)) return tag.substr(1, length - 1);
        tag.remove_prefix(length + 1);
    }
    return {};
}

std::optional<unsigned> suffix_number(std::string_view value, std::string_view prefix)
{
    if (value.size() <= prefix.size() || !equals_ascii(value.substr(0, prefix.size()), prefix)) return {};
    value.remove_prefix(prefix.size());
    unsigned number{};
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), number);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size()) return {};
    return number;
}

struct Entity {
    std::string_view markup;
    char symbol;
};

constexpr Entity entities[] = {
    {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&nbsp;", ' '}
};

std::string entity_text(std::string_view value)
{
    std::string result;
    while (!value.empty()) {
        const auto known = std::find_if(std::begin(entities), std::end(entities),
            [&](const Entity& entity) { return value.starts_with(entity.markup); });
        if (known != std::end(entities)) {
            result += known->symbol;
            value.remove_prefix(known->markup.size());
        } else {
            result += value.front();
            value.remove_prefix(1);
        }
    }
    return result;
}

// OptionData's KeyIconTextTable and PadIconFKeyTable identify the icons; the
// rich-text decorator selects separate Xbox, PlayStation, and Windows tables.
std::string glyph(std::string_view attributes)
{
    const auto id = attribute(attributes, "id");
    const auto table = attribute(attributes, "table");
    constexpr auto unknown = "Unidentified control";

    if (equals_ascii(table, "Windows")) {
        if (equals_ascii(id, "Pad_Win")) {
            const auto painted_text = attribute(attributes, "text");
            return painted_text.empty() ? unknown : entity_text(painted_text);
        }
        switch (suffix_number(id, "Key_").value_or(100)) {
        case 16: return "Space";
        case 19: return "Tab";
        case 20: return "Backspace";
        case 26: return "Left Shift";
        case 38: return "Left Mouse Button";
        case 39: return "Right Mouse Button";
        default: return unknown;
        }
    }

    const bool xbox = equals_ascii(table, "XSX");
    const bool playstation = equals_ascii(table, "PS5") || equals_ascii(table, "PS4");
    if (!xbox && !playstation) return unknown;

    constexpr std::string_view xbox_buttons[] =
        {"Menu", "View", "B", "A", "X", "Y", "LB", "LT", "LS", "Left Stick", "RB", "RT", "RS", "Right Stick"};
    constexpr std::string_view playstation_buttons[] =
        {"Options", "Touchpad", "Circle", "Cross", "Square", "Triangle", "L1", "L2", "L3",
         "Left Stick", "R1", "R2", "R3", "Right Stick"};
    constexpr std::string_view directions[] = {
        "Left Stick Up", "Left Stick Right", "Left Stick Down", "Left Stick Left",
        "Right Stick Up", "Right Stick Right", "Right Stick Down", "Right Stick Left",
        "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left",
        "Left Stick Left or Right", "Left Stick Up or Down", "Right Stick Left or Right", "Right Stick Up or Down",
        "D-pad Left or Right", "D-pad Up or Down"
    };

    const auto row = suffix_number(id, "Pad_");
    if (!row) return unknown;
    if (*row < std::size(xbox_buttons))
        return std::string((xbox ? xbox_buttons : playstation_buttons)[*row]);
    const auto direction = *row - std::size(xbox_buttons);
    return direction < std::size(directions) ? std::string(directions[direction]) : unknown;
}

std::size_t tag_end(std::string_view value)
{
    char quote{};
    for (std::size_t i = 1; i < (std::min)(value.size(), std::size_t{512}); ++i) {
        const char current = value[i];
        if (quote) {
            if (current == quote) quote = {};
        } else if (current == '"' || current == '\'') {
            quote = current;
        } else if (current == '>') {
            return i;
        } else if (current == '<') {
            return value.npos;
        }
    }
    return value.npos;
}

std::string trim_spaces(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    bool separated{};
    while (!value.empty()) {
        const auto size = value.starts_with("\xE3\x80\x80"sv) ? 3u :
            value.starts_with("\xC2\xA0"sv) ? 2u : space(value.front()) ? 1u : 0u;
        if (size) {
            separated = !result.empty();
            value.remove_prefix(size);
        } else {
            if (separated) result += ' ';
            separated = false;
            result += value.front();
            value.remove_prefix(1);
        }
    }
    return result;
}

void append(std::string& result, std::string_view current)
{
    if (current.empty() || result == current) return;
    if (!result.empty())
        result += one_of(std::string_view(&result.back(), 1), {".", "?", "!", ":"}) ? " " : ". ";
    result += current;
}

struct ViewRect {
    double l{}, t{}, r{}, b{};
};

bool reading_order(ViewRect left, ViewRect right)
{
    const auto left_line = std::floor(left.t / 12.0);
    const auto right_line = std::floor(right.t / 12.0);
    return left_line < right_line || (left_line == right_line && left.l < right.l);
}

using NodeIndex = std::size_t;
constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
constexpr std::size_t maximum_roots = 36;
constexpr std::size_t maximum_widgets = 1500;
constexpr std::size_t maximum_widgets_per_root = 950;
constexpr unsigned maximum_depth = 32;
constexpr std::size_t maximum_items = 256;

std::optional<ViewRect> rect(std::optional<WidgetBounds> bounds)
{
    if (!bounds || !std::isfinite(bounds->x) || !std::isfinite(bounds->y) ||
        !std::isfinite(bounds->width) || !std::isfinite(bounds->height) ||
        bounds->width < 1 || bounds->height < 1) return {};
    return ViewRect{bounds->x, bounds->y, bounds->x + bounds->width, bounds->y + bounds->height};
}

std::optional<ViewRect> intersection(ViewRect a, ViewRect b)
{
    const ViewRect result{(std::max)(a.l, b.l), (std::max)(a.t, b.t),
                          (std::min)(a.r, b.r), (std::min)(a.b, b.b)};
    if (result.r - result.l < 1 || result.b - result.t < 1) return {};
    return result;
}

bool text_class(std::string_view type)
{
    return type.ends_with("TextBlock") ||
           one_of(type, {"EditableText", "EditableTextBox", "MultiLineEditableText", "MultiLineEditableTextBox"});
}

bool image_class(std::string_view type)
{
    return type == "Image" || type == "SpUIImage";
}

bool story_pause(std::string_view type)
{
    return type == "WBP_EventPause_C";
}

bool root_is_cinematic(std::string_view type)
{
    return story_pause(type) || one_of(type, {"WBP_GRP_AI_EventSkip_C", "WBP_TEST_Tutorial_MoviePlayer_C",
                         "WBP_OBJ_Common_TextWindow_C", "WBP_GRP_Common_EventText_C", "WBP_GRP_AI_EndCredit_C"}) ||
           type.starts_with("WBP_GRP_AI_Event") || type.find("Subtitle") != type.npos;
}

bool root_is_dialog(std::string_view type)
{
    return story_pause(type) || type.starts_with("WBP_Dialog_") ||
           type.starts_with("WBP_GRP_AI_DecisionBranch") || type == "WBP_NAD_Dialog_000_C";
}

bool operation_guide(std::string_view type)
{
    return type == "WBP_GRP_GuideSet_C" || type == "WBP_NAD_GuideSet_C";
}

bool controls_screen(std::string_view type)
{
    return one_of(type, {"WBP_GRP_SL_Main_0_C", "WBP_GRP_SL_Main_1_C",
                         "WBP_GRP_SL_Main_1P_C", "WBP_GRP_SL_Main_2P_C"});
}

bool training_settings(std::string_view type)
{
    return type.starts_with("WBP_GRP_HUD_CSG_Trein_Set_");
}

bool tutorial_root(std::string_view type)
{
    return one_of(type, {"WBP_GRP_TTR_HUD_TutorialAll_C", "WBP_GRP_HUD_CSG_HUD_Main_C"});
}

bool control_panel(std::string_view name, std::string_view type)
{
    return type == "WBP_OBJ_SL_Content_C" && (name == "Content_1P" || name == "Content_2P");
}

bool roster_grid(std::string_view type)
{
    return type.starts_with("WBP_GRP_BS_CharaList_");
}

bool character_grid(std::string_view type)
{
    return roster_grid(type) || type.starts_with("WBP_GRP_BS_Top_00_") ||
           type.starts_with("WBP_GRP_BS_ChList_TeamList_");
}

bool episode_roster(std::string_view type)
{
    return type.starts_with("WBP_GRP_AI_CharacterSelect");
}

bool episode_chart(std::string_view type)
{
    return type == "WBP_GRP_AI_ChartTitle_C";
}

bool episode_map(std::string_view type)
{
    return type.starts_with("WBP_GRP_AI_Map_");
}

bool episode_details(std::string_view type)
{
    return type == "WBP_GRP_AI_ChartDetails_C";
}

bool episode_decision(std::string_view type)
{
    return type == "WBP_GRP_AI_DecisionBranch_0_C";
}

bool bonus_battle_list(std::string_view type)
{
    return type == "WBP_GRP_DB_ButtleList_C";
}

bool technique_panel(std::string_view type)
{
    return type == "WBP_GRP_BS_SkillList_DP_C";
}

bool journey_menu(std::string_view type)
{
    return one_of(type, {"WBP_NAD_Bring_C", "WBP_NAD_BringCheck_C", "WBP_NAD_CharaDetail_C",
        "WBP_NAD_CharaDetail_NewSkill_C", "WBP_NAD_CharaDetail_R_C", "WBP_NAD_CharaSelect_C",
        "WBP_NAD_CharaStock_C", "WBP_NAD_Difficulty_C", "WBP_NAD_DifficultyLevel_C", "WBP_NAD_Map_C",
        "WBP_NAD_Map_Pause_C", "WBP_NAD_MasterResult_C", "WBP_NAD_MasterSelect_C", "WBP_NAD_ReDraw_C",
        "WBP_NAD_ReDraw_Select_C", "WBP_NAD_Restart_C", "WBP_NAD_SPDivide_C", "WBP_NAD_Trajectory_C"});
}

bool journey_map_hud(std::string_view type)
{
    return one_of(type, {"WBP_NAD_Map_Player_C", "WBP_NAD_Map_Sutamina_C",
                         "WBP_NAD_Map_Turn_C", "WBP_NAD_Map_Ball_C"});
}

bool stage_list(std::string_view type)
{
    return type.starts_with("WBP_GRP_BS_StageList_");
}

bool team_setting(std::string_view type)
{
    return one_of(type, {"WBP_GRP_BS_Top_00_1P_C", "WBP_GRP_BS_Top_00_2P_C"});
}

bool setup_surface(std::string_view type)
{
    return stage_list(type) || character_grid(type) ||
           one_of(type, {"WBP_GRP_BS_SetBGM_List_C", "WBP_GRP_BS_RuleSetting_DP_C"});
}

bool hit_target(std::string_view type)
{
    return type.starts_with("WBP_OBJ_Common_Hit") || type.starts_with("WBP_OBJ_HoldHit");
}

bool split_focus_surface(std::string_view type)
{
    return setup_surface(type) || episode_roster(type) ||
           one_of(type, {"WBP_GRP_CSG_Top_00_C", "WBP_TBL_Menu_C", "WBP_TBL_Edit_C",
                         "WBP_MainMenuMatchingConditionSettings_C"}) ||
           type.starts_with("WBP_GRP_HUN_") || type.starts_with("WBP_GRP_Mission_") ||
           type.starts_with("WBP_GRP_SH_") || type.starts_with("WBP_GRP_Gallery_") ||
           type.starts_with("WBP_OBJ_Gallery_MyData_") || type.starts_with("WBP_GRP_BS_TA_") ||
           type.starts_with("WBP_GRP_LB_TA_");
}

bool help_panel(std::string_view type)
{
    return one_of(type, {"WBP_GRP_DB_HelpWindow_C", "WBP_GRP_DB_HelpWindow_TIPS_C",
                         "WBP_GRP_TA_HelpDialog_C", "WBP_GRP_BS_TA_Dialog_0_C",
                         "WBP_GRP_CSG_Practice_base_C", "WBP_GRP_HUD_CSG_Menu_Base_C",
                         "WBP_GRP_HUD_CSG_Menu_Base_A_C"});
}

bool raised_dialog(std::string_view type)
{
    return root_is_dialog(type) || one_of(type, {"WBP_GRP_DB_HelpWindow_C",
        "WBP_GRP_DB_HelpWindow_TIPS_C", "WBP_GRP_TA_HelpDialog_C", "WBP_GRP_BS_TA_Dialog_0_C"});
}

bool training_menu(std::string_view type)
{
    return type.starts_with("WBP_GRP_CSG_Practice_") ||
           type.starts_with("WBP_GRP_HUD_CSG_Menu_");
}

bool episode_screen(std::string_view type)
{
    return episode_roster(type) || episode_details(type) || episode_map(type) ||
           type.starts_with("WBP_GRP_AI_Chart") || type == "WBP_GRP_AI_Outline_C" ||
           type.starts_with("WBP_GRP_AI_DecisionBranch");
}

bool overview_surface(std::string_view type)
{
    return episode_screen(type) || split_focus_surface(type) || bonus_battle_list(type) || technique_panel(type) ||
           training_menu(type) || help_panel(type) || journey_menu(type) ||
           one_of(type, {"WBP_CMN_GuideMessage_C", "WBP_TBL_Entry_C", "WBP_TBL_Advanced_C",
                         "WBP_GRP_DB_ExDetails_C", "WBP_GRP_DB_ExDetails_2_C"});
}

struct Node {
    ObjectRef object{};
    NodeIndex parent{none}, root{none}, end{};
    std::string name, type, text;
    std::optional<ViewRect> painted;
    ViewRect clip{};
    bool visible{}, keyboard{}, controller{}, training_reset_prompt{};
};

class WidgetTree {
public:
    explicit WidgetTree(GameReader& game) : game_(game)
    {
        nodes_.reserve(512);
    }

    void capture(std::span<const ObjectRef> roots, bool cinematic)
    {
        struct Root {
            ObjectRef object;
            std::string type;
            bool focus_path;
            bool keyboard;
        };
        std::vector<Root> candidates;
        for (const auto root : roots) {
            if (!game_.visible(root) || !game_.call_bool(root, "IsInViewport").value_or(false)) continue;
            auto type = game_.engine().class_name(root);
            if (cinematic && !root_is_cinematic(type)) continue;
            const bool keyboard = game_.call_bool(root, "HasKeyboardFocus").value_or(false);
            const bool focus = keyboard || game_.call_bool(root, "HasAnyUserFocus").value_or(false) ||
                               game_.call_bool(root, "HasFocusedDescendants").value_or(false);
            candidates.push_back({root, std::move(type), focus, keyboard});
        }
        // Keep the focused surface and time-sensitive caption overlays ahead of large decorative trees.
        std::stable_sort(candidates.begin(), candidates.end(), [](const Root& a, const Root& b) {
            const auto priority = [](const Root& r) {
                if (raised_dialog(r.type) || r.focus_path || setup_surface(r.type) || controls_screen(r.type)) return 0;
                if (tutorial_root(r.type) || root_is_cinematic(r.type) || operation_guide(r.type) ||
                    overview_surface(r.type) || r.type == "WBP_MainMenu_Base_TextSub_C") return 1;
                return 2;
            };
            return priority(a) < priority(b);
        });
        std::size_t captured = 0;
        for (const auto& root : candidates) {
            if (captured >= maximum_roots) break;
            if (attempts_ >= maximum_widgets) break;
            const auto extent = rect(game_.geometry(root.object));
            if (!extent) continue;
            ++captured;
            root_attempts_ = 0;
            visit(root.object, none, nodes_.size(), *extent, 0, root.focus_path,
                  root.keyboard, split_focus_surface(root.type));
        }
    }

    std::span<const Node> nodes() const { return nodes_; }
    const Node& operator[](NodeIndex index) const { return nodes_[index]; }

    std::optional<NodeIndex> locate(ObjectRef object) const
    {
        const auto it = attached_.find(object.address);
        if (it == attached_.end() || nodes_[it->second].object != object) return {};
        return it->second;
    }

    template<class Predicate> NodeIndex ancestor(NodeIndex index, Predicate predicate) const
    {
        for (; index != none; index = nodes_[index].parent)
            if (predicate(nodes_[index])) return index;
        return none;
    }

    bool within(NodeIndex index, NodeIndex scope) const
    {
        return index >= scope && index < nodes_[scope].end;
    }

    std::optional<ViewRect> painted(NodeIndex index) const
    {
        const auto& node = nodes_[index];
        if (node.painted) return node.painted;
        const auto extent = rect(game_.geometry(node.object));
        return extent ? intersection(node.clip, *extent) : std::nullopt;
    }

private:
    void visit(ObjectRef object, NodeIndex parent, NodeIndex root, ViewRect clip, unsigned depth,
               bool focus_path, bool root_keyboard, bool probe_hit_targets)
    {
        if (depth > maximum_depth || attempts_ >= maximum_widgets ||
            root_attempts_ >= maximum_widgets_per_root) return;
        ++attempts_;
        ++root_attempts_;
        if (!object || attached_.contains(object.address)) return;
        auto type = game_.engine().class_name(object);
        if (type.empty()) return;
        probe_hit_targets |= split_focus_surface(type) || training_menu(type);
        const bool proxy = probe_hit_targets && parent != none && hit_target(type);
        const bool visible = game_.visible(object) && (!type.starts_with("WBP_") ||
            game_.number(object, "ColorAndOpacity.A").value_or(1) > 0.001);
        if (!visible) {
            // A split hit target can be transparent while its sibling paints the selection.
            // Keep only live, attached and non-hidden proxies for a paired visual lookup.
            if (!proxy) return;
            const auto visibility = game_.integer(object, "Visibility");
            if (!visibility || (*visibility != 0 && *visibility != 3 && *visibility != 4)) return;
        }
        // Slate clips scroll containers and any widget with an explicit clipping zone.
        const auto clipping = game_.integer(object, "Clipping").value_or(0);
        if (type.ends_with("ScrollBox") || one_of(type, {"ListView", "TileView", "TreeView"}) ||
            (clipping >= 1 && clipping <= 3)) {
            const auto extent = rect(game_.geometry(object));
            const auto clipped = extent ? intersection(clip, *extent) : std::nullopt;
            if (!clipped) return;
            clip = *clipped;
        }

        const bool is_text = text_class(type);
        const bool is_image = image_class(type);
        const auto index = nodes_.size();
        Node node;
        node.object = object;
        node.parent = parent;
        node.root = root;
        node.end = index + 1;
        node.name = game_.engine().name_of(object);
        node.type = std::move(type);
        node.clip = clip;
        node.visible = visible;
        if ((focus_path || proxy || raised_dialog(node.type)) && !is_image) {
            node.keyboard = parent == none ? root_keyboard :
                game_.call_bool(object, "HasKeyboardFocus").value_or(false);
            node.controller = node.keyboard || game_.call_bool(object, "HasAnyUserFocus").value_or(false);
        }
        if (parent == none) node.painted = clip;
        if (is_text) {
            const auto extent = rect(game_.geometry(object));
            if (extent) node.painted = intersection(clip, *extent);
            if (node.painted) {
                auto rendered = game_.text(object, "Text");
                if (rendered.empty()) rendered = game_.call_text(object, "GetText");
                node.text = read_ui_text(rendered);
                if (node.name == "RTEXT_Help_0" && parent != none && operation_guide(nodes_[root].type))
                    node.training_reset_prompt = ui_detail::matches_training_reset_prompt(rendered);
            }
        }
        if (is_image && (node.name == "IMG_Lang" || node.name == "IMG_Face_Main" ||
                         node.name == "IMG_Face_Base08_Lock" || node.name.find("Check") != node.name.npos)) {
            const auto extent = rect(game_.geometry(object));
            if (extent) node.painted = intersection(clip, *extent);
        }

        attached_.emplace(object.address, index);
        nodes_.push_back(std::move(node));
        if (visible && !is_text && !is_image) {
            const bool child_focus = (focus_path || raised_dialog(nodes_[index].type) || overview_surface(nodes_[index].type)) &&
                (parent == none || game_.call_bool(object, "HasFocusedDescendants").value_or(false));
            for (const auto child : game_.widget_children(object)) {
                if (attempts_ >= maximum_widgets || root_attempts_ >= maximum_widgets_per_root) break;
                visit(child, index, root, clip, depth + 1, child_focus, false, probe_hit_targets);
            }
        }
        nodes_[index].end = nodes_.size();
    }

    GameReader& game_;
    std::vector<Node> nodes_;
    std::unordered_map<Address, NodeIndex> attached_;
    std::size_t attempts_{}, root_attempts_{};
};

std::string named_text(const WidgetTree& tree, NodeIndex scope, std::string_view name)
{
    std::string value;
    for (auto i = scope; i < tree[scope].end; ++i) {
        const auto& node = tree[i];
        if (!equals_ascii(node.name, name) || node.text.empty()) continue;
        if (!value.empty() && value != node.text) return {};
        value = node.text;
    }
    return value;
}

template<class Predicate>
std::optional<NodeIndex> unique_widget(const WidgetTree& tree, NodeIndex scope, Predicate match)
{
    std::optional<NodeIndex> found;
    for (auto i = scope; i < tree[scope].end; ++i) {
        if (!match(tree[i])) continue;
        if (found) return {};
        found = i;
    }
    return found;
}

template<class Predicate>
NodeIndex unique_surface(const WidgetTree& tree, Predicate match)
{
    NodeIndex found = none;
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        if (!match(tree[i]) || !tree.painted(i)) continue;
        if (found != none) return none;
        found = i;
    }
    return found;
}

std::string visible_lines(const WidgetTree& tree, NodeIndex scope,
                          const std::unordered_set<NodeIndex>& consumed, std::string_view omit = {})
{
    std::string result;
    std::unordered_set<std::string_view> seen;
    std::vector<NodeIndex> ordered;
    for (auto i = scope; i < tree[scope].end; ++i) {
        const auto& node = tree[i];
        if (node.painted && !node.text.empty() && node.text != omit && !consumed.contains(i))
            ordered.push_back(i);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [&](auto a, auto b) {
        return reading_order(*tree[a].painted, *tree[b].painted);
    });
    for (auto i : ordered) {
        if (result.size() >= 4000) break;
        if (seen.insert(tree[i].text).second) append(result, tree[i].text);
    }
    return result;
}

std::string linked_text(GameReader& game, const WidgetTree& tree, NodeIndex scope, std::string_view member)
{
    const auto text = tree.locate(game.object(tree[scope].object, member));
    return text && tree.within(*text, scope) ? tree[*text].text : std::string{};
}

bool setting_row(GameReader& game, const Node& node)
{
    if (!node.type.starts_with("WBP_") && !node.type.starts_with("SS")) return false;
    if (!game.object(node.object, "Label")) return false;
    for (const auto type : {"SSPropertySettingsButton", "SSMainMenuSettingsPropertyButton",
                            "SSTournamentRuleEditorButton"}) {
        if (game.engine().is_a(node.object, game.engine().find_class(type))) return true;
    }
    return false;
}

bool active_menu(GameReader& game, const Node& node)
{
    constexpr auto state = "bIsActive";
    constexpr auto input = "bIsDisableKeyInput";
    return game.boolean(node.object, state) == true && game.boolean(node.object, input) == false;
}

bool option_row(GameReader& game, const Node& node, ObjectRef side_changer)
{
    return setting_row(game, node) ||
           (side_changer && node.type.starts_with("WBP_") && game.engine().is_a(node.object, side_changer));
}

std::string binding(const WidgetTree& tree, NodeIndex scope)
{
    std::string keys;
    for (auto i = scope; i < tree[scope].end; ++i) {
        const auto& node = tree[i];
        if (!node.text.empty() && node.name.starts_with("RichText_PadButton")) {
            if (!keys.empty()) keys += " ";
            keys += node.text;
        }
    }
    return keys;
}

std::string texture_name(GameReader& game, const Node& image)
{
    if (!image.painted) return {};
    // A zero-alpha brush or image has no visible glyph/portrait even when its widget is visible.
    if (game.number(image.object, "ColorAndOpacity.A").value_or(1) <= 0.001 ||
        game.number(image.object, "Brush.TintColor.SpecifiedColor.A").value_or(1) <= 0.001) return {};
    return game.engine().name_of(game.object(image.object, "Brush.ResourceObject"));
}

std::string language_value(std::string_view texture)
{
    constexpr auto prefix = "T_UI_Title_LanguageSelect_"sv;
    if (!texture.starts_with(prefix)) return {};
    const auto code = texture.substr(prefix.size());
    constexpr std::pair<std::string_view, std::string_view> values[] = {
        {"jp", "日本語"}, {"en", "English"}, {"de", "Deutsch"}, {"fr", "Français"},
        {"es", "Español (España)"}, {"la", "Español (América Latina)"},
        {"id", "Bahasa Indonesia"}, {"it", "Italiano"}, {"pl", "Polish"},
        {"bp", "Português (Brasil)"}, {"ru", "Русский"}, {"ar", "العربية"},
        {"ko", "한국어"}, {"th", "ภาษาไทย"}, {"zhcn", "中文 (简体)"}, {"zhtw", "中文 (繁體)"}
    };
    const auto found = std::find_if(std::begin(values), std::end(values),
                                    [&](auto value) { return value.first == code; });
    return found == std::end(values) ? std::string{} : std::string(found->second);
}

std::optional<std::string_view> portrait_id(std::string_view image)
{
    constexpr auto player_one = "T_UI_ChThumbP1_"sv;
    constexpr auto player_two = "T_UI_ChThumbP2_"sv;
    if (image.starts_with(player_one)) image.remove_prefix(player_one.size());
    else if (image.starts_with(player_two)) image.remove_prefix(player_two.size());
    else return {};
    if (image.size() != 10 || image[4] != '_' || image[7] != '_') return {};
    for (std::size_t i = 0; i < image.size(); ++i)
        if (i != 4 && i != 7 && (image[i] < '0' || image[i] > '9')) return {};
    return image.substr(0, 7);
}

std::string portrait_label(GameReader& game, const WidgetTree& tree, NodeIndex scope)
{
    std::string image;
    for (auto i = scope; i < tree[scope].end; ++i) {
        const auto& node = tree[i];
        if (node.name == "IMG_Face_Base08_Lock" &&
            texture_name(game, node) == "T_UI_BS_IconFrame08") return "Locked character";
        if (node.name == "IMG_Face_Main") image = texture_name(game, node);
    }
    if (image == "T_UI_Common_Icon_Empty_Plus") return "Empty slot";
    const auto key = portrait_id(image);
    if (!key) return {};

    // The loaded asset is identified by the currently painted portrait brush.
    // A pooled character name plate can still show the previous selection.
    const std::string object_name = "CharacterData_" + std::string(*key);
    const auto asset = game.engine().find("/Game/SS/MasterDataAsset/CharacterData/" + object_name +
                                          "." + object_name);
    const auto type = asset ? game.engine().find_class("SSCharacterDataAsset") : ObjectRef{};
    if (!type || !game.engine().is_a(asset, type)) return {};
    return ui_detail::compose_portrait_name(
        read_ui_text(game.text(asset, "CharacterDataAssetRecord.NameInfo.FullName")),
        read_ui_text(game.text(asset, "CharacterDataAssetRecord.NameInfo.TitleName")));
}

bool bonus_battle_card(const WidgetTree& tree, NodeIndex scope)
{
    return tree[scope].type == "WBP_OBJ_DB_ButtleThumbnail_C" &&
           tree.ancestor(scope, [](const Node& node) { return bonus_battle_list(node.type); }) != none;
}

std::string bonus_card_label(const WidgetTree& tree, NodeIndex scope, bool tags)
{
    auto title = named_text(tree, scope, "Txt_TitleText");
    if (title.empty() || !tags) return title;
    append(title, named_text(tree, scope, "Txt_MainTag"));
    append(title, named_text(tree, scope, "Txt_MainTag_1"));
    return title;
}

std::string shop_tile_label(GameReader& game, const WidgetTree& tree, NodeIndex scope)
{
    const auto& tile = tree[scope];
    const bool shop = tile.type == "WBP_OBJ_SH_BTN_Shop_C";
    if (!shop && tile.type != "WBP_OBJ_SH_BTN_Customize_C") return {};
    const auto selector = tree.ancestor(scope, [](const Node& node) { return node.type == "WBP_GRP_SH_Top_C"; });
    if (selector == none || !tree.painted(scope) ||
        game.object(tree[selector].object, shop ? "ShopButton" : "CustomizeButton") != tile.object) return {};
    auto caption = linked_text(game, tree, scope, "Caption");
    if (caption.empty()) caption = named_text(tree, scope, "caption");
    // The two menu-owned tiles can paint their names as localized artwork.
    return caption.empty() ? (shop ? "Shop" : "Customize") : std::move(caption);
}

std::string option_label(GameReader& game, const WidgetTree& tree, NodeIndex scope)
{
    auto title = linked_text(game, tree, scope, "Label");
    if (title.empty()) title = named_text(tree, scope, "Title");
    if (title.empty()) title = named_text(tree, scope, "Text_Title");
    auto value = linked_text(game, tree, scope, "Description");
    if (value.empty()) value = linked_text(game, tree, scope, "Caption");
    if (value.empty()) value = named_text(tree, scope, "caption");
    if (value.empty()) {
        const auto image = unique_widget(tree, scope, [](const Node& node) {
            return node.name == "IMG_Lang" && node.painted;
        });
        if (image) value = language_value(texture_name(game, tree[*image]));
    }
    if (title.empty()) title = std::move(value);
    else append(title, value);
    append(title, named_text(tree, scope, "Text_Disable"));
    return title;
}

std::string label(GameReader& game, const WidgetTree& tree, NodeIndex scope, ObjectRef side_changer)
{
    const auto& row = tree[scope];
    if (option_row(game, row, side_changer))
        return option_label(game, tree, scope);

    if (row.type.starts_with("WBP_OBJ_SH_ItemIcon_")) {
        auto item = named_text(tree, scope, "Txt_ItemName");
        if (item.empty()) return {};
        append(item, named_text(tree, scope, "TXT_PriceNum_0"));
        return item;
    }
    if (auto text = linked_text(game, tree, scope, "Caption"); !text.empty()) return text;
    for (const auto name : {"caption", "Title", "Text_Title", "Text_Name", "Text_CharaName",
                            "TXT_Title", "TXT_Name", "Text_StageName", "Text_BattleName",
                            "Text_MissionTitle_Line00", "Txt_ItemName", "Text_ItemName",
                            "Text_MissionTitle", "TXT_MissionTitle", "Text_MovieName",
                            "Text_MovieTitle", "TXT_MovieName", "Text_BGMName", "Text_ReplayTitle"}) {
        if (auto value = named_text(tree, scope, name); !value.empty()) return value;
    }

    // Some Blueprint buttons carry just one painted text leaf with a custom name.
    const auto candidate = unique_widget(tree, scope, [](const Node& node) {
        return !node.text.empty() && !node.name.starts_with("RichText_Pad") &&
               !node.name.starts_with("RTEXT_Help");
    });
    return candidate ? tree[*candidate].text : std::string{};
}



std::optional<NodeIndex> related_hit_label(GameReader& game, const WidgetTree& tree, NodeIndex scope)
{
    const auto& hit = tree[scope];
    if (!hit_target(hit.type) && !hit.type.starts_with("WBP_")) return {};
    const auto surface = tree.ancestor(scope, [](const Node& node) {
        return split_focus_surface(node.type) || training_menu(node.type);
    });
    const auto remote = tree.locate(game.object(hit.object, "RemoteButton"));
    if (remote && *remote != scope && !tree.within(scope, *remote)) return remote;
    if (surface == none || !hit_target(hit.type)) return {};

    return unique_widget(tree, surface, [&](const Node& visual) {
        return ui_detail::matches_menu_visual(tree[surface].type, hit.name, visual.name);
    });
}

bool focus_has_visual(GameReader& game, const WidgetTree& tree, NodeIndex index)
{
    const auto& node = tree[index];
    if (hit_target(node.type)) {
        if (const auto target = related_hit_label(game, tree, index)) {
            if (tree.painted(*target)) return true;
            for (auto i = *target + 1; i < tree[*target].end; ++i)
                if (!tree[i].text.empty() || (tree[i].name == "IMG_Face_Main" && tree[i].painted)) return true;
        }
    }
    return node.visible && tree.painted(index).has_value();
}

NodeIndex focused_widget(GameReader& game, const WidgetTree& tree, NodeIndex modal)
{
    NodeIndex chosen = none;
    bool ambiguous = false;
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (modal != none && !tree.within(i, modal)) continue;
        if ((!node.keyboard && !node.controller) || !focus_has_visual(game, tree, i)) continue;
        if (chosen == none || (node.keyboard && !tree[chosen].keyboard)) {
            chosen = i;
            ambiguous = false;
        } else if (node.keyboard == tree[chosen].keyboard) {
            if (tree.within(i, chosen)) chosen = i;
            else ambiguous = true;
        }
    }
    if (ambiguous || chosen != none) return ambiguous ? none : chosen;

    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& menu = tree[i];
        if (modal != none && !tree.within(i, modal)) continue;
        if ((!overview_surface(menu.type) && !controls_screen(menu.type) && !raised_dialog(menu.type)) ||
            stage_list(menu.type) || !active_menu(game, menu)) continue;
        const auto direct = game.object(menu.object, "LastFocusedWidget");
        const auto built_in = game.object(menu.object, "BuiltInMenu.LastFocusedWidget");
        const auto target = tree.locate(built_in ? built_in : direct);
        if (!target || *target == i || !tree.within(*target, i) ||
            !focus_has_visual(game, tree, *target)) continue;
        if (chosen != none && chosen != *target) return none;
        chosen = *target;
    }
    return chosen;
}

std::string focused_label(GameReader& game, const WidgetTree& tree, NodeIndex focused,
                          ObjectRef side_changer)
{
    const auto& initial = tree[focused];
    if (initial.type == "WBP_MainMenu_Base_C") return named_text(tree, focused, "TXT_GuideMessage");
    const auto setting = tree.ancestor(focused, [&](const Node& node) { return option_row(game, node, side_changer); });
    if (setting != none) return option_label(game, tree, setting);
    const auto card = tree.ancestor(focused, [](const Node& node) {
        return node.type == "WBP_OBJ_DB_ButtleThumbnail_C";
    });
    if (card != none && bonus_battle_card(tree, card)) return bonus_card_label(tree, card, true);
    if (!initial.text.empty()) return initial.text;
    for (auto scope = focused; scope != none; scope = tree[scope].parent) {
        const auto& node = tree[scope];
        if (auto text = shop_tile_label(game, tree, scope); !text.empty()) return text;
        if (node.type.starts_with("WBP_OBJ_BS_CharaIcon_")) return portrait_label(game, tree, scope);
        if (const auto remote = related_hit_label(game, tree, scope)) {
            if (bonus_battle_card(tree, *remote)) return bonus_card_label(tree, *remote, true);
            if (tree[*remote].type.starts_with("WBP_OBJ_BS_CharaIcon_"))
                return portrait_label(game, tree, *remote);
            if (auto caption = label(game, tree, *remote, side_changer); !caption.empty()) return caption;
        }

        // Keep an unlabeled hit target local. A broad ancestor label can belong to another button.
        if (hit_target(node.type)) {
            if (auto own = label(game, tree, scope, side_changer); !own.empty()) return own;
            for (auto parent = node.parent; parent != none; parent = tree[parent].parent) {
                if (tree[parent].type.starts_with("WBP_OBJ_"))
                    return label(game, tree, parent, side_changer);
            }
            return {};
        }
        if (node.type.starts_with("WBP_") || node.type == "Button") {
            if (scope == initial.root && !episode_screen(node.type) && !root_is_dialog(node.type) &&
                !stage_list(node.type)) return {};
            if (auto text = label(game, tree, scope, side_changer); !text.empty()) return text;
        }
    }
    return {};
}

bool passive_item_surface(std::string_view type)
{
    return type.starts_with("WBP_GRP_GuideSet") || type.starts_with("WBP_GRP_TTR_Opt_") ||
           type.starts_with("WBP_Option_Tips") || type == "WBP_MainMenu_Base_TextSub_C" ||
           type.starts_with("WBP_GRP_BS_CharaNameSet") || technique_panel(type) ||
           tutorial_root(type) || root_is_cinematic(type) || type.starts_with("WBP_CMN_Waiting");
}

NodeIndex episode_decision_focus(GameReader& game, const WidgetTree& tree, NodeIndex dialog, NodeIndex focus)
{
    constexpr std::array<std::string_view, 4> choices{
        "WBP_OBJ_DecisionBranch_Btn_Top_C", "WBP_OBJ_DecisionBranch_Btn_Bottom_C",
        "WBP_OBJ_DecisionBranch_Btn_Left_C", "WBP_OBJ_DecisionBranch_Btn_Right_C"};
    if (focus != none) {
        const auto current = tree.ancestor(focus, [&](const Node& node) {
            return std::ranges::find(choices, node.type) != choices.end();
        });
        if (current != none && tree.within(current, dialog) && tree.painted(current)) return current;
    }
    const auto object = tree[dialog].object;
    if (!game.engine().is_a(object, game.engine().find_class("SSEventSceneDecisionBranch")) ||
        !game.engine().live(object)) return none;
    // The native decision update stores the active painted direction even during dummy input focus.
    constexpr std::size_t selected_direction = 0x501; // USSEventSceneDecisionBranch, 142aac9c0
    const auto direction = read<std::uint8_t>(object.address + selected_direction).value_or(0);
    if (direction == 0 || direction > choices.size()) return none;
    const auto button = unique_widget(tree, dialog, [&](const Node& node) {
        return node.type == choices[direction - 1] && node.visible;
    });
    return button && tree.painted(*button) ? *button : none;
}

std::string episode_decision_label(GameReader& game, const WidgetTree& tree, NodeIndex dialog, NodeIndex choice)
{
    if (choice == none) return {};
    const auto caption = [&](NodeIndex owner) {
        const auto text = tree.locate(game.object(tree[owner].object, "Caption"));
        if (text && tree.within(*text, dialog) && !tree[*text].text.empty()) return tree[*text].text;
        return named_text(tree, owner, "caption");
    };
    if (auto text = caption(choice); !text.empty()) return text;
    const auto remote = tree.locate(game.object(tree[choice].object, "RemoteButton"));
    return remote && *remote != dialog && *remote != choice && tree.within(*remote, dialog)
        ? caption(*remote) : std::string{};
}

bool journey_strength_title(std::string_view name)
{
    return one_of(name, {"Text_Def_SP_Title", "Text_Danger_SP_Title", "Text_Boss_SP_Title",
                         "Text_LBoss_SP_Title", "Text_SBoss_SP_Title"});
}

bool journey_strength_value(std::string_view name)
{
    const auto split = name.find("_SP");
    return split != name.npos &&
           one_of(name.substr(0, split), {"Text_Default", "Text_Danger", "Text_Boss", "Text_LBoss", "Text_SBoss"}) &&
           one_of(name.substr(split + 3), {"", "_R", "_Y", "_W", "_DR"});
}

std::string journey_spot_label(const WidgetTree& tree, NodeIndex spot, std::unordered_set<NodeIndex>& consumed)
{
    std::string_view title, value;
    for (auto i = spot + 1; i < tree[spot].end; ++i) {
        const auto& leaf = tree[i];
        if (leaf.text.empty()) continue;
        if (journey_strength_value(leaf.name)) {
            if (!value.empty() && value != leaf.text) return {};
            value = leaf.text;
        } else if (journey_strength_title(leaf.name)) {
            if (!title.empty() && title != leaf.text) return {};
            title = leaf.text;
        }
    }
    std::string label;
    if (unique_widget(tree, spot, [&](const Node& leaf) {
            const auto index = static_cast<NodeIndex>(&leaf - tree.nodes().data());
            return leaf.name == "IMG_Here" && image_class(leaf.type) && tree.painted(index);
        })) label = "Current location";
    if (value.empty()) return label;
    for (auto i = spot + 1; i < tree[spot].end; ++i) {
        if (!tree[i].text.empty() && (journey_strength_value(tree[i].name) || journey_strength_title(tree[i].name)))
            consumed.insert(i);
    }
    append(label, std::string(title.empty() ? "SP" : title) + " " + std::string(value));
    return label;
}

std::string episode_chart_text(const WidgetTree& tree, NodeIndex chart)
{
    if (named_text(tree, chart, "Text_ScenarioTitle_0").empty()) return {};
    std::string result;
    for (const auto title : {"Text_EventTitle", "Text_Chapter", "Text_ScenarioTitle_1",
                            "Text_ScenarioTitle_0", "TXT_Orb"})
        append(result, named_text(tree, chart, title));
    return result;
}

NodeIndex episode_overview(GameReader& game, const WidgetTree& tree)
{
    return unique_surface(tree, [&](const Node& node) {
        return episode_map(node.type) && node.visible &&
            game.engine().is_a(node.object, game.engine().find_class("SSDragonAdventureIFCTMapManager"));
    });
}

NodeIndex episode_overview_focus(GameReader& game, const WidgetTree& tree, NodeIndex map)
{
    constexpr std::size_t selected_icon = 0x448; // USSDragonAdventureIFCTMapIconWidget, 1424f5390
    const auto type = game.engine().find_class("SSDragonAdventureIFCTMapIconWidget");
    const auto icon = unique_widget(tree, map, [&](const Node& node) {
        if (node.type != "WBP_OBJ_AI_Map_Piece_00_C" || !node.visible ||
            !game.engine().is_a(node.object, type) || !game.engine().live(node.object) ||
            read<std::uint8_t>(node.object.address + selected_icon) != 1) return false;
        const auto index = static_cast<NodeIndex>(&node - tree.nodes().data());
        return tree.painted(index).has_value();
    });
    return icon.value_or(none);
}

void collect_subtitle(GameReader& game, const WidgetTree& tree, ScreenReading& result,
                      std::unordered_set<NodeIndex>& consumed, NodeIndex modal_root)
{
    const auto text_ui = game.engine().find_class("SSMenuTextUi");
    const auto event_ui = game.engine().find_class("SSMenuTextWithChoiceUi");
    const auto captions = game.engine().find_class("SSWCTSubtitle");
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (modal_root != none && !tree.within(i, modal_root)) continue;
        if (!node.type.starts_with("WBP_")) continue;
        const bool dialog = text_ui && game.engine().is_a(node.object, text_ui);
        const bool duration_caption = captions && game.engine().is_a(node.object, captions);
        if (!dialog && !duration_caption) continue;

        const auto text = tree.locate(game.object(node.object, dialog ? "ContextText" : "TextBody"));
        if (!text || *text <= i || *text >= node.end || tree[*text].text.empty()) continue;
        consumed.insert(*text);
        std::string line;
        if (dialog) {
            const auto speaker = tree.locate(game.object(node.object, "CharaNameText"));
            if (speaker && *speaker > i && *speaker < node.end && !tree[*speaker].text.empty()) {
                consumed.insert(*speaker);
                line = tree[*speaker].text;
            }
        }
        append(line, tree[*text].text);
        const auto event = dialog ? tree.ancestor(i, [&](const Node& parent) {
            return parent.type.starts_with("WBP_GRP_Common_EventText") && event_ui &&
                   game.engine().is_a(parent.object, event_ui);
        }) : none;
        result.subtitles.push_back({node.object, tree[*text].object,
                                    event == none ? ObjectRef{} : tree[event].object, std::move(line)});
    }
}

void collect_tutorial(GameReader& game, const WidgetTree& tree, ScreenReading& result,
                      std::unordered_set<NodeIndex>& consumed, NodeIndex modal_root)
{
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (modal_root != none && !tree.within(i, modal_root)) continue;
        if (!tutorial_root(tree[node.root].type)) continue;
        if (node.type == "WBP_OBJ_TRR_HUD_TipText_C") {
            auto caption = named_text(tree, i, "caption");
            if (caption.empty()) continue;
            const auto keys = binding(tree, i);
            if (!keys.empty()) caption += ", " + keys;
            for (auto j = i + 1; j < node.end; ++j) {
                const auto& leaf = tree[j];
                if (leaf.name.starts_with("IMG_Check_") &&
                    texture_name(game, leaf) == "T_UI_DB_Common_CheckMark") append(caption, "Check mark");
                if (!leaf.text.empty()) consumed.insert(j);
            }
            append(result.tutorial, caption);
        } else if (node.type == "WBP_OBJ_HUD_CSG_CategolyText_C" ||
                   node.type == "WBP_OBJ_HUD_CSG_Command_BTN_C" ||
                   node.type == "WBP_OBJ_TRR_HUD_CMD_BTN_C" ||
                   node.type == "WBP_OBJ_CSG_HUD_GuideText_C" ||
                   node.type == "WBP_OBJ_HUD_CSG_Com_Tips_C") {
            std::vector<std::string_view> lines;
            for (auto j = i + 1; j < node.end; ++j) {
                const auto& leaf = tree[j];
                if (leaf.text.empty() ||
                    (!one_of(leaf.name, {"Text_Title", "Text_Comment", "Text_Comment_Large", "caption", "RTEXT_Help", "RTEXT_Help_0"}) &&
                     !leaf.name.starts_with("RichText_PadButton"))) continue;
                consumed.insert(j);
                if (std::find(lines.begin(), lines.end(), leaf.text) == lines.end()) {
                    lines.push_back(leaf.text);
                    append(result.tutorial, leaf.text);
                }
            }
        }
    }
}

struct Item {
    ViewRect position;
    std::string text;
};

bool guide_group(std::string_view type)
{
    return type.starts_with("WBP_OBJ_Guide_Btn_") ||
           type.starts_with("WBP_OBJ_Common_HoldButton_") ||
           type.starts_with("WBP_OBJ_Common_HoldBTN_") ||
           type == "WBP_OBJ_Common_HoldBTN_Normal_0_C";
}

std::string help_prompt(const WidgetTree& tree, NodeIndex scope)
{
    auto keys = named_text(tree, scope, "RichText_Button_1");
    auto action = named_text(tree, scope, "RTEXT_Help_1");
    if (action.empty()) {
        keys = named_text(tree, scope, "RTEXT_Button");
        action = named_text(tree, scope, "RTEXT_Help");
    }
    return ui_detail::compose_help_prompt(named_text(tree, scope, "RTEXT_Help_0"), keys, action);
}

bool native_type(GameReader& game, ObjectRef object, std::string_view type)
{
    return object && game.engine().is_a(object, game.engine().find_class(type));
}

bool visible_battle_hud(GameReader& game, const WidgetTree& tree)
{
    const auto instance = game.engine().game_instance();
    if (!native_type(game, instance, "SSGameInstance")) return false;
    const auto players = game.objects(instance, "LocalPlayers", 1);
    if (players.empty()) return false;
    const auto controller = game.object(players.front(), "PlayerController");
    if (!native_type(game, controller, "SSBattlePlayerController") ||
        !game.call_bool(controller, "IsLocalPlayerController").value_or(false)) return false;
    const auto hud = game.object(controller, "MyHUD");
    if (!native_type(game, hud, "SSMainGameHUD") || game.object(hud, "PlayerOwner") != controller ||
        game.boolean(hud, "bShowHUD") == false) return false;

    const auto world = game.object(game.engine().outer(controller), "OwningWorld");
    if (!world || game.object(world, "OwningGameInstance") != instance) return false;
    const auto state = game.object(world, "GameState");
    if (!native_type(game, state, "SSBattleGameStateBase")) return false;

    // In Free Training the main gauges can be attached separately from the
    // HUD manager. The owner and world check above constrains this fallback.
    const auto common = game.object(hud, "HUDCommonWidgetRoot");
    const auto sides = game.object(hud, "HUDSideWidgetRoot");
    for (const auto parent : {common, sides}) {
        for (const auto field : {"HUDWidgetSet.P1MainGaugeWidget", "HUDWidgetSet.P2MainGaugeWidget"}) {
            const auto gauge = game.object(parent, field);
            const auto painted = tree.locate(gauge);
            if (painted && tree.painted(*painted) && native_type(game, gauge, "SSMainGauge")) return true;
        }
    }
    for (const auto& node : tree.nodes()) {
        if (node.parent == none && one_of(node.type, {"WBP_Rep_MainGauge_P1_C", "WBP_Rep_MainGauge_P2_C"}) &&
            native_type(game, node.object, "SSMainGauge")) return true;
    }
    return false;
}

bool battle_help_obscured(const Node& root)
{
    return root_is_dialog(root.type) || controls_screen(root.type) || training_settings(root.type) ||
           split_focus_surface(root.type) || tutorial_root(root.type) || root.type == "WBP_Pause_C" ||
           root.type == "WBP_EventPause_C" || root.type == "WBP_Pause_Requirement_C" ||
           root.type.starts_with("WBP_Option_") || root.type.starts_with("WBP_GRP_BS_Result_") ||
           root.type.starts_with("WBP_GRP_HUD_CSG_Menu_");
}

void collect_free_training_help(GameReader& game, const WidgetTree& tree, ScreenReading& result)
{
    NodeIndex guide = none;
    for (const auto& root : tree.nodes()) {
        if (root.parent != none) continue;
        if (battle_help_obscured(root)) return;
        if (!operation_guide(root.type)) continue;
        if (guide != none) return;
        guide = root.root;
    }
    if (guide == none) return;

    const auto buttons = unique_widget(tree, guide, [](const Node& node) {
        return node.type == "WBP_OBJ_GuideButtonSet_C";
    });
    if (!buttons) return;
    const auto prompt = unique_widget(tree, *buttons, [](const Node& node) {
        return node.type == "WBP_OBJ_Guide_Btn_Present_C";
    });
    if (!prompt) return;
    const auto label = unique_widget(tree, *prompt, [](const Node& node) {
        return node.training_reset_prompt && !node.text.empty();
    });
    if (!label) return;
    // The same root is used for menus with multiple visible guide actions.
    for (NodeIndex i = *buttons + 1; i < tree[*buttons].end; ++i) {
        const auto& node = tree[i];
        if (i != *label && !node.text.empty() &&
            (node.name.starts_with("RTEXT_Help") || node.name.starts_with("RichText_Button"))) return;
    }
    if (visible_battle_hud(game, tree)) result.battle_help = tree[*label].text;
}

void put_item(std::vector<Item>& items, const WidgetTree& tree, NodeIndex index, std::string text)
{
    if (!text.empty() && items.size() < maximum_items) {
        if (const auto position = tree.painted(index))
            items.push_back({*position, std::move(text)});
    }
}

void consume_text(const WidgetTree& tree, NodeIndex scope, std::unordered_set<NodeIndex>& consumed,
                  std::initializer_list<std::string_view> names)
{
    for (auto i = scope + 1; i < tree[scope].end; ++i)
        if (!tree[i].text.empty() && one_of(tree[i].name, names)) consumed.insert(i);
}

NodeIndex modal_surface(GameReader& game, const WidgetTree& tree)
{
    const auto candidate = [&](const Node& node) {
        if (!raised_dialog(node.type) || !node.visible) return false;
        const auto start = static_cast<NodeIndex>(&node - tree.nodes().data());
        return std::ranges::any_of(tree.nodes().subspan(start, node.end - start),
                                  [](const Node& item) { return !item.text.empty(); });
    };
    const auto focused = unique_surface(tree, [&](const Node& node) {
        return candidate(node) && (node.keyboard || node.controller ||
            game.call_bool(node.object, "HasFocusedDescendants").value_or(false));
    });
    if (focused != none && !episode_decision(tree[focused].type)) return focused;
    const auto pause = unique_surface(tree, [&](const Node& node) {
        return story_pause(node.type) && candidate(node);
    });
    if (pause != none) return pause;
    if (focused != none) return focused;
    return unique_surface(tree, candidate);
}

NodeIndex associated_panel(GameReader& game, const WidgetTree& tree, NodeIndex member,
                           std::string_view controller_class, std::string_view member_property,
                           std::string_view panel_property)
{
    constexpr std::size_t limit = 4;
    const auto owners = game.engine().objects(controller_class, {}, limit + 1, 0, 0, false);
    if (owners.size() > limit) return none;
    NodeIndex result = none;
    for (const auto owner : owners) {
        if (game.object(owner, member_property) != tree[member].object) continue;
        const auto panel = tree.locate(game.object(owner, panel_property));
        if (!panel || !tree.painted(*panel)) continue;
        if (result != none) return none;
        result = *panel;
    }
    return result;
}

std::string episode_overview_label(GameReader& game, const WidgetTree& tree, NodeIndex outline, NodeIndex icon,
                                   NodeIndex chart, std::unordered_set<NodeIndex>& consumed)
{
    // The RT outline widget owns the selected title. Its TXT_Main is the description.
    auto title = named_text(tree, outline, "TXT_Title");
    if (title.empty()) return {};
    consume_text(tree, outline, consumed, {"TXT_Title"});
    if (icon == none) return title;

    const auto island = tree.ancestor(icon, [&](const Node& node) {
        return node.type.starts_with("WBP_OBJ_AI_Map_Block_") &&
               game.engine().is_a(node.object, game.engine().find_class("SSDragonAdventureIFCTMapIslandWidget"));
    });
    const auto area = island == none ? std::string{} : named_text(tree, island, "TXT_ChapterTitle");
    if (!area.empty()) consume_text(tree, island, consumed, {"TXT_ChapterTitle"});

    // The chart chapter applies only when both native cursors select the painted icon.
    constexpr std::size_t event_block_name = 0x440; // USSDragonAdventureIFCTMapIconWidget.EventBlockName
    const auto episode = read<NameId>(tree[icon].object.address + event_block_name);
    if (chart != none && episode && game.engine().episode_cursors_match(*episode)) {
        const bool matching_paint = area.empty() ? named_text(tree, chart, "Text_EventTitle") == title :
            named_text(tree, chart, "Text_ScenarioTitle_1") == area;
        if (matching_paint) append(title, named_text(tree, chart, "Text_Chapter"));
    }
    append(title, area);
    return title;
}

NodeIndex tournament_viewer(GameReader& game, const WidgetTree& tree, NodeIndex selection)
{
    const auto list = tree.ancestor(selection, [](const Node& node) { return node.type == "WBP_TBL_Menu_C"; });
    return list == none ? none : associated_panel(game, tree, list, "SSTournamentSelectController",
                                                  "MenuListWidget", "RuleViewerWidget");
}

std::string tournament_rules(GameReader& game, const WidgetTree& tree, NodeIndex viewer,
                             const std::unordered_set<NodeIndex>& consumed)
{
    std::vector<std::pair<ViewRect, std::string>> rows;
    for (auto i = viewer + 1; i < tree[viewer].end; ++i) {
        if (tree[i].type != "WBP_OBJ_TBL_Edit_ListItem_C") continue;
        const auto position = tree.painted(i);
        auto text = option_label(game, tree, i);
        if (position && !text.empty()) rows.emplace_back(*position, std::move(text));
    }
    std::ranges::sort(rows, [](const auto& a, const auto& b) {
        return reading_order(a.first, b.first);
    });
    std::string rules;
    for (const auto& row : rows) append(rules, row.second);
    return rules.empty() ? visible_lines(tree, viewer, consumed) : rules;
}

NodeIndex other_menu_panel(GameReader& game, const WidgetTree& tree, NodeIndex surface)
{
    const auto& type = tree[surface].type;
    if (type == "WBP_GRP_HUN_SceneList_C")
        return associated_panel(game, tree, surface, "SSModeHUNCotroller", "HUNBattleListUi", "HUNBattleDetailUi");
    if (type.starts_with("WBP_GRP_Mission_"))
        return unique_surface(tree, [](const Node& node) { return node.type == "WBP_GRP_Mission_Detail_C"; });
    if (type.starts_with("WBP_GRP_SH_"))
        return unique_surface(tree, [](const Node& node) { return node.type == "WBP_GRP_SH_Detail_00_C"; });
    if (type.starts_with("WBP_OBJ_Gallery_MyData_"))
        return unique_surface(tree, [](const Node& node) {
            return one_of(node.type, {"WBP_OBJ_Gallery_MyData_Info_Panel_C",
                "WBP_OBJ_Gallery_MyData_TitleItem_Panel_C", "WBP_OBJ_Gallery_MyData_TitleText_Panel_C",
                "WBP_OBJ_Gallery_MyData_UseRate_Panel_C"});
        });
    if (training_menu(type))
        return unique_surface(tree, [](const Node& node) {
            return node.type == "WBP_GRP_HUD_CSG_Menu_Base_A_C";
        });
    return none;
}

NodeIndex stage_surface(GameReader& game, const WidgetTree& tree)
{
    NodeIndex stage = none;
    bool has_title = false;
    for (const auto& root : tree.nodes()) {
        if (root.parent != none) continue;
        if (root_is_dialog(root.type)) return none;
        if (root.type == "WBP_GRP_BS_StageName_DP_C") {
            has_title |= !named_text(tree, root.root, "Text_StageTitle").empty();
            continue;
        }
        if (!setup_surface(root.type) ||
            game.boolean(root.object, "bIsActive") != true ||
            game.boolean(root.object, "bIsDisableKeyInput") != false) continue;
        if (!stage_list(root.type) || stage != none) return none;
        stage = root.root;
    }
    return has_title ? stage : none;
}

NodeIndex main_surface(GameReader& game, const WidgetTree& tree, NodeIndex focus, NodeIndex modal)
{
    if (modal != none) return modal;
    const auto details = unique_surface(tree, [&](const Node& node) {
        if (!episode_details(node.type)) return false;
        const auto index = static_cast<NodeIndex>(&node - tree.nodes().data());
        const bool input = (focus != none && tree.within(focus, index)) || active_menu(game, node);
        return input && (!named_text(tree, index, "CategoryTitleText").empty() ||
                         !named_text(tree, index, "TXT_Outline").empty());
    });
    if (details != none) return details;
    const auto chart = unique_surface(tree, [&](const Node& node) {
        if (!episode_chart(node.type)) return false;
        const auto index = static_cast<NodeIndex>(&node - tree.nodes().data());
        return !named_text(tree, index, "Text_ScenarioTitle_0").empty();
    });
    if (chart != none) return chart;
    if (focus != none) {
        const auto owning = tree.ancestor(focus, [](const Node& node) {
            return overview_surface(node.type) || controls_screen(node.type);
        });
        return owning != none ? owning : tree[focus].root;
    }
    if (const auto stage = stage_surface(game, tree); stage != none) return stage;

    const auto technique = unique_surface(tree, [&](const Node& node) {
        return technique_panel(node.type) && active_menu(game, node);
    });
    if (technique != none) return technique;

    // A controller can keep the team slots and technique panel on screen while the roster owns navigation.
    const auto roster = unique_surface(tree, [&](const Node& node) {
        return roster_grid(node.type) && active_menu(game, node);
    });
    if (roster != none) return roster;
    NodeIndex dialog = none;
    NodeIndex most_lines = none;
    std::size_t maximum = 0;
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& root = tree[i];
        if (root.parent != none || passive_item_surface(root.type) || stage_list(root.type)) continue;
        const auto children = tree.nodes().subspan(i, root.end - i);
        const auto count = static_cast<std::size_t>(std::count_if(children.begin(), children.end(),
            [](const Node& node) { return !node.text.empty(); }));
        if (count && root_is_dialog(root.type)) {
            if (dialog != none) return none;
            dialog = i;
        }
        if (count > maximum) {
            maximum = count;
            most_lines = i;
        }
    }
    if (dialog != none) return dialog;
    const auto active = unique_surface(tree, [&](const Node& node) {
        return overview_surface(node.type) && active_menu(game, node);
    });
    if (active != none) return active;
    const auto specific = unique_surface(tree, [](const Node& node) {
        return help_panel(node.type) || episode_roster(node.type) ||
               one_of(node.type, {"WBP_CMN_GuideMessage_C", "WBP_GRP_HUN_Details_C", "WBP_GRP_HUN_MissionDetail_C"});
    });
    return specific != none ? specific : most_lines;
}

} // namespace

std::string ui_detail::compose_portrait_name(std::string full_name, std::string_view form)
{
    if (!form.empty() && full_name.ends_with(form)) {
        const auto preceding = std::string_view(full_name).substr(0, full_name.size() - form.size());
        if (preceding.empty() || preceding.ends_with(", ") || preceding.ends_with("、") ||
            preceding.ends_with("，")) return full_name;
    }
    append(full_name, form);
    return full_name;
}

bool ui_detail::matches_menu_visual(std::string_view root, std::string_view hit, std::string_view visual)
{
    if (root == "WBP_GRP_CSG_Top_00_C") {
        // Ghidra's training top key table pairs the two hit targets in reverse suffix order.
        return (hit == "WBP_OBJ_Common_HitButton_0" && visual == "WBP_OBJ_CSG_BTN_01") ||
               (hit == "WBP_OBJ_Common_HitButton_1" && visual == "WBP_OBJ_CSG_BTN_00");
    }
    if (team_setting(root)) {
        if (hit == "WBP_OBJ_Common_HitBTN_Remove") return visual == "WBP_OBJ_Com_BTN_Remove";
        if (hit == "WBP_OBJ_Common_HitBTN_Replace") return visual == "WBP_OBJ_Com_BTN_Replace";
    }

    if (episode_roster(root)) {
        const auto index = hit == "WBP_OBJ_Common_HitButton" ? std::optional{0U} :
            suffix_number(hit, "WBP_OBJ_Common_HitButton_");
        return index && suffix_number(visual, "WBP_OBJ_AI_BTN_Menu_") == index;
    }

    auto index = suffix_number(hit, "WBP_OBJ_Common_HitBtn_Text_");
    if (!index) index = suffix_number(hit, "WBP_OBJ_Common_HitButton_");
    if (!index) return false;
    if (character_grid(root)) return suffix_number(visual, "WBP_OBJ_BS_CharaIcon_") == index;
    if (stage_list(root)) return suffix_number(visual, "WBP_OBJ_BS_BTN_Menu_DP_") == index;
    if (root == "WBP_GRP_BS_SetBGM_List_C") return suffix_number(visual, "WBP_OBJ_BS_SetBGMBTN_") == index;
    if (root == "WBP_GRP_BS_RuleSetting_DP_C") return suffix_number(visual, "WBP_OBJ_BS_SetRuleBTN_") == index;
    return false;
}

std::string ui_detail::compose_help_prompt(std::string_view combined, std::string_view keys, std::string_view action)
{
    if (!combined.empty()) return std::string(combined);
    if (keys.empty() || action.empty()) return {};
    std::string text(keys);
    text += ", ";
    text += action;
    return text;
}

bool ui_detail::matches_training_reset_prompt(std::string_view painted)
{
    const auto skip_space = [](std::string_view& text) {
        while (!text.empty() && space(text.front())) text.remove_prefix(1);
    };
    const auto icon = [&](std::string_view& text) -> std::string_view {
        skip_space(text);
        if (text.size() < 7 || !equals_ascii(text.substr(0, 5), "<icon") || !space(text[5])) return {};
        const auto end = tag_end(text);
        if (end == text.npos) return {};
        const auto attributes = text.substr(5, end - 5);
        text.remove_prefix(end + 1);
        return attributes;
    };

    const auto first = icon(painted);
    if (first.empty()) return false;
    skip_space(painted);
    if (painted.starts_with("+")) painted.remove_prefix(1);
    else if (painted.starts_with("＋"sv)) painted.remove_prefix("＋"sv.size());
    else return false;
    const auto second = icon(painted);
    if (second.empty()) return false;
    skip_space(painted);
    if (painted.empty() || painted.front() == '<' || painted.front() == '+') return false;

    const auto table = attribute(first, "table");
    if (!equals_ascii(table, attribute(second, "table"))) return false;
    const auto first_id = attribute(first, "id");
    const auto second_id = attribute(second, "id");
    if (equals_ascii(table, "Windows"))
        return equals_ascii(first_id, "Pad_Win") && !attribute(first, "text").empty() &&
               equals_ascii(second_id, "Key_19");
    if (equals_ascii(table, "XSX") || equals_ascii(table, "PS5") || equals_ascii(table, "PS4"))
        return equals_ascii(first_id, "Pad_06") && equals_ascii(second_id, "Pad_00");
    return false;
}

std::string read_ui_text(std::string_view rendered_text)
{
    std::string result;
    result.reserve(rendered_text.size());
    while (!rendered_text.empty()) {
        if (rendered_text.starts_with("＋"sv)) {
            result += " + ";
            rendered_text.remove_prefix("＋"sv.size());
            continue;
        }
        if (rendered_text.front() == '<' && rendered_text.size() > 1 &&
            ((lower(rendered_text[1]) >= 'a' && lower(rendered_text[1]) <= 'z') || rendered_text[1] == '/')) {
            const auto end = tag_end(rendered_text);
            if (end != rendered_text.npos) {
                const auto tag = rendered_text.substr(1, end - 1);
                const auto divider = tag.find_first_of(" \t\r\n/");
                const auto type = tag.substr(0, divider);
                if (equals_ascii(type, "icon")) {
                    if (!result.empty() && !space(result.back())) result += " ";
                    result += divider == tag.npos ? "Unidentified control" : glyph(tag.substr(divider));
                    result += " ";
                } else if (equals_ascii(type, "br")) {
                    result += " ";
                }
                rendered_text.remove_prefix(end + 1);
                continue;
            }
        }

        const auto known = std::find_if(std::begin(entities), std::end(entities),
            [&](const Entity& entity) { return rendered_text.starts_with(entity.markup); });
        if (known != std::end(entities)) {
            result += known->symbol;
            rendered_text.remove_prefix(known->markup.size());
        } else {
            result += rendered_text.front();
            rendered_text.remove_prefix(1);
        }
    }
    return trim_spaces(result);
}

void UiReader::reset()
{
    control_panel_ = {};
    control_focus_ = {};
    observed_controls_ = {};
    previous_controls_ = {};
    control_samples_ = 0;
}

UiReader::ControlDetails UiReader::settled_controls(ObjectRef panel, ObjectRef focus,
                                                    ControlDetails observed)
{
    if (panel != control_panel_) {
        reset();
        control_panel_ = panel;
    }
    if (focus != control_focus_) {
        previous_controls_ = observed_controls_;
        control_focus_ = focus;
        control_samples_ = 0;
    }
    if (observed != observed_controls_) {
        observed_controls_ = observed;
        control_samples_ = 0;
    }
    if (control_samples_ < 3) ++control_samples_;

    // The layout is reused and its old description survives the focus event.
    // During a transition wait for a different detail signature to settle.
    if (control_samples_ < 2 || observed == previous_controls_) return {};
    return observed;
}

ScreenReading UiReader::sample(std::span<const ObjectRef> roots, bool cinematic, bool diagnostics)
{
    WidgetTree tree(game_);
    tree.capture(roots, cinematic);
    ScreenReading result;
    const auto modal = modal_surface(game_, tree);
    auto focus = focused_widget(game_, tree, modal);
    const auto surface = main_surface(game_, tree, focus, modal);
    const bool decision = modal != none && episode_decision(tree[modal].type);
    const bool paused_story = modal != none && story_pause(tree[modal].type);
    const bool overview_surface_selected = surface != none && (episode_chart(tree[surface].type) ||
        episode_map(tree[surface].type) || tree[surface].type == "WBP_OBJ_AI_Map_Outline_C");
    const auto overview = overview_surface_selected ? episode_overview(game_, tree) : none;
    const auto outline = overview == none ? none : associated_panel(game_, tree, overview,
        "SSDragonAdventureIFChartController", "StoryMapWidget", "MapOutlineWidget");
    const bool episode_overview_open = outline != none && tree[outline].type == "WBP_OBJ_AI_Map_Outline_C";
    const bool journey_map = surface != none && tree[surface].type == "WBP_NAD_Map_C";
    if (decision) focus = episode_decision_focus(game_, tree, modal, focus);
    auto map_spot = none;
    if (journey_map && focus != none) {
        map_spot = tree.ancestor(focus, [](const Node& node) { return node.type == "WBP_OBJ_NAD_Map_Spot_C"; });
        if (map_spot != none && tree.within(map_spot, surface)) focus = map_spot;
        else map_spot = none;
    }
    if (episode_overview_open) {
        focus = episode_overview_focus(game_, tree, overview);
    } else if (surface != none && (episode_chart(tree[surface].type) || episode_details(tree[surface].type)) &&
               focus != none && !tree.within(focus, surface)) {
        focus = none;
    }
    if (diagnostics && surface != none)
        result.visible_surfaces.push_back(tree[surface].type + " (" + tree[surface].name + ")");
    for (NodeIndex i = 0; diagnostics && i < tree.nodes().size() && result.visible_surfaces.size() < 28; ++i) {
        const auto& node = tree[i];
        if (i == surface || (node.parent != none && !overview_surface(node.type) && !raised_dialog(node.type))) continue;
        if (node.visible) result.visible_surfaces.push_back(node.type + " (" + node.name + ")");
    }
    if (surface != none) result.root = tree[episode_overview_open ? overview : surface].object;
    if (focus != none) result.focus = tree[focus].object;
    const auto side_changer = game_.engine().find_class("SSMenuButtonSideChanger");
    std::unordered_set<NodeIndex> consumed;
    collect_subtitle(game_, tree, result, consumed, modal);
    collect_tutorial(game_, tree, result, consumed, modal);
    if (episode_overview_open) {
        const auto chart = episode_chart(tree[surface].type) ? surface : none;
        result.announcement = episode_overview_label(game_, tree, outline, focus, chart, consumed);
    } else if (decision) result.announcement = episode_decision_label(game_, tree, modal, focus);
    else if (map_spot != none) result.announcement = journey_spot_label(tree, map_spot, consumed);
    else if (!journey_map && focus != none && !consumed.contains(focus))
        result.announcement = focused_label(game_, tree, focus, side_changer);
    if (result.announcement.empty() && surface != none && tree[surface].type == "WBP_Title_C") {
        for (auto i = surface + 1; i < tree[surface].end; ++i)
            if (tree[i].type == "WBP_Title_Button_PC_C")
                append(result.announcement, named_text(tree, i, "Caption_00"));
    }
    if (surface != none && stage_list(tree[surface].type) &&
        (result.announcement.empty() || focus == surface)) {
        std::string title;
        bool ambiguous = false;
        for (const auto& node : tree.nodes()) {
            if (node.parent != none || node.type != "WBP_GRP_BS_StageName_DP_C") continue;
            const auto current = named_text(tree, node.root, "Text_StageTitle");
            if (current.empty()) continue;
            if (!title.empty() && current != title) {
                ambiguous = true;
                break;
            }
            title = current;
        }
        if (!title.empty() && !ambiguous) result.announcement = std::move(title);
    }
    auto review_scope = none;
    if (surface != none && !episode_overview_open) {
        const auto& current = tree[surface];
        if (paused_story) {
            if (result.announcement.empty()) result.announcement = visible_lines(tree, modal, consumed);
        } else if (modal != none && !decision) {
            const auto selection = std::move(result.announcement);
            result.announcement = visible_lines(tree, modal, consumed, selection);
            append(result.announcement, selection);
        } else if (episode_chart(current.type)) {
            result.announcement = episode_chart_text(tree, surface);
        } else if (episode_details(current.type)) {
            result.announcement = visible_lines(tree, surface, consumed);
        } else if (episode_roster(current.type)) {
            auto roster = named_text(tree, surface, "CaracterText_0");
            append(roster, named_text(tree, surface, "TXT_ChapterTitle"));
            append(roster, result.announcement);
            append(roster, named_text(tree, surface, "TXT_main"));
            result.announcement = std::move(roster);
        } else if (focus != none) {
            review_scope = tournament_viewer(game_, tree, focus);
            if (review_scope != none && !result.announcement.empty())
                append(result.announcement, tournament_rules(game_, tree, review_scope, consumed));
        }

        if (modal == none && review_scope == none) {
            const auto details = other_menu_panel(game_, tree, surface);
            if (details != none)
                append(result.announcement, visible_lines(tree, details, consumed, result.announcement));
            else if (help_panel(current.type) || one_of(current.type, {
                     "WBP_CMN_GuideMessage_C", "WBP_GRP_HUN_MissionDetail_C", "WBP_GRP_HUN_Details_C",
                     "WBP_GRP_SH_Detail_00_C", "WBP_GRP_DB_ExDetails_C", "WBP_GRP_DB_ExDetails_2_C"}))
                append(result.announcement, visible_lines(tree, surface, consumed, result.announcement));
            else if (result.announcement.empty() && overview_surface(current.type) && !journey_map &&
                     !character_grid(current.type) && !stage_list(current.type))
                result.announcement = visible_lines(tree, surface, consumed);
        }

        if (result.announcement.empty() && character_grid(current.type) &&
            (focus != none || active_menu(game_, current))) {
            const auto plate = associated_panel(game_, tree, surface, "SSBattleSetupController",
                                                "BattleSetupCharaListWidget", "CharaNameWidget");
            if (plate != none) {
                result.announcement = ui_detail::compose_portrait_name(
                    named_text(tree, plate, "Text_Name"), named_text(tree, plate, "Text_Name_Plus"));
            }
        }
    }
    const auto panel = focus == none ? none : tree.ancestor(focus, [](const Node& node) {
        return control_panel(node.name, node.type);
    });
    const auto skill_row = focus == none ? none : tree.ancestor(focus, [](const Node& node) {
        return node.type == "WBP_OBJ_SL_Content_Skill_C" || node.type == "WBP_OBJ_SL_Content_Other_C";
    });
    if (panel != none && skill_row != none) {
        ControlDetails observed{};
        observed.lines[0] = named_text(tree, panel, "Text_BtnGuide");
        observed.lines[2] = named_text(tree, panel, "Text_Explain");
        consume_text(tree, panel, consumed, {"Text_BtnGuide", "Text_Explain"});
        for (auto i = panel + 1; i < tree[panel].end; ++i) {
            const auto& node = tree[i];
            if (node.name == "WBP_OBJ_SL_Cell_S_1") {
                observed.lines[1] = named_text(tree, i, "caption");
                consume_text(tree, i, consumed, {"caption"});
            } else if (node.name == "WBP_OBJ_SL_Cell_XL") {
                observed.lines[3] = named_text(tree, i, "caption");
                consume_text(tree, i, consumed, {"caption"});
            }
        }
        auto details = settled_controls(tree[panel].object, tree[skill_row].object, std::move(observed));
        for (const auto& line : details.lines) append(result.control_details, line);
    } else {
        reset();
    }

    if (!cinematic && modal == none) collect_free_training_help(game_, tree, result);

    std::vector<Item> items;
    NodeIndex reviewed_focus = none;
    auto focus_review = result.announcement;
    append(focus_review, result.control_details);
    const bool panel_review = surface != none && (episode_overview_open || decision || paused_story || journey_map ||
        technique_panel(tree[surface].type) || episode_chart(tree[surface].type) || episode_details(tree[surface].type));
    if (!focus_review.empty() && focus != none && !panel_review) {
        const auto visual = related_hit_label(game_, tree, focus);
        auto focus_position = visual.value_or(focus);
        const auto setting = tree.ancestor(focus_position, [&](const Node& node) {
            return option_row(game_, node, side_changer);
        });
        if (setting != none) focus_position = setting;
        put_item(items, tree, focus_position, focus_review);
        if (!items.empty()) reviewed_focus = focus_position;
        if (reviewed_focus != none && panel != none && skill_row != none && !result.announcement.empty())
            consume_text(tree, skill_row, consumed, {"caption"});
    }

    for (NodeIndex i = 0; i < tree.nodes().size() && items.size() < maximum_items; ++i) {
        const auto& node = tree[i];
        if ((modal != none && !tree.within(i, modal)) ||
            (reviewed_focus != none && review_scope != none && tree.within(i, review_scope))) continue;
        if (episode_overview_open && !tree.within(i, overview) && !tree.within(i, outline) &&
            !operation_guide(tree[node.root].type)) continue;
        if (surface != none && (journey_map || technique_panel(tree[surface].type) || episode_details(tree[surface].type)) &&
            !tree.within(i, surface) && !operation_guide(tree[node.root].type)) {
            if (!journey_map || tree.ancestor(i, [](const Node& parent) {
                    return journey_map_hud(parent.type);
                }) == none) continue;
        }
        if (journey_map && node.type == "WBP_OBJ_NAD_Map_Spot_C" && tree.within(i, surface)) {
            if (tree.painted(i)) put_item(items, tree, i,
                i == map_spot ? result.announcement : journey_spot_label(tree, i, consumed));
        } else if (auto tile = shop_tile_label(game_, tree, i); !tile.empty()) {
            if (i != reviewed_focus) put_item(items, tree, i, std::move(tile));
            consume_text(tree, i, consumed, {"caption"});
        } else if (bonus_battle_card(tree, i)) {
            if (i != reviewed_focus) put_item(items, tree, i, bonus_card_label(tree, i, false));
            consume_text(tree, i, consumed, {"Txt_TitleText", "Txt_MainTag", "Txt_MainTag_1"});
        } else if (option_row(game_, node, side_changer)) {
            const auto value = option_label(game_, tree, i);
            if (value.empty() || !tree.painted(i)) continue;
            if (i != reviewed_focus) put_item(items, tree, i, value);
            consume_text(tree, i, consumed, {"Title", "Text_Title", "caption", "Text_Disable"});
            for (const auto member : {"Label", "Description", "Caption"}) {
                const auto text = tree.locate(game_.object(node.object, member));
                if (text && tree.within(*text, i)) consumed.insert(*text);
            }
        } else if (guide_group(node.type)) {
            auto prompt = help_prompt(tree, i);
            if (!prompt.empty() && tree.painted(i)) {
                put_item(items, tree, i, std::move(prompt));
                consume_text(tree, i, consumed, {"RTEXT_Help_0", "RTEXT_Help_1", "RTEXT_Help",
                                                 "RichText_Button_1", "RTEXT_Button"});
            } else {
                const auto keys = binding(tree, i);
                const auto caption = named_text(tree, i, "caption");
                if (!keys.empty() && !caption.empty() && tree.painted(i)) {
                    put_item(items, tree, i, keys + ", " + caption);
                    consume_text(tree, i, consumed, {"caption", "RichText_PadButton_0", "RichText_PadButton_1"});
                }
            }
        } else if (node.type.starts_with("WBP_OBJ_BS_CharaIcon_") &&
                   tree.ancestor(i, [](const Node& ancestor) { return character_grid(ancestor.type); }) != none) {
            const auto face = unique_widget(tree, i, [](const Node& leaf) {
                return leaf.name == "IMG_Face_Main" && leaf.painted;
            });
            put_item(items, tree, face.value_or(i), portrait_label(game_, tree, i));
        }
        if (!node.text.empty() && !consumed.contains(i)) put_item(items, tree, i, node.text);
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return reading_order(a.position, b.position);
    });
    std::unordered_set<std::string> seen;
    const auto remember = [&](const std::string& text) {
        if (!text.empty() && seen.insert(text).second) result.items.push_back(text);
    };
    if (episode_overview_open || (reviewed_focus == none && !panel_review)) remember(focus_review);
    for (const auto& item : items) remember(item.text);
    remember(result.tutorial);
    for (const auto& subtitle : result.subtitles) remember(subtitle.text);
    return result;
}

} // namespace sparking
