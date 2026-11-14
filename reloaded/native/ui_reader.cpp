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

bool root_is_cinematic(std::string_view type)
{
    return one_of(type, {"WBP_GRP_AI_EventSkip_C", "WBP_TEST_Tutorial_MoviePlayer_C",
                         "WBP_OBJ_Common_TextWindow_C", "WBP_GRP_Common_EventText_C", "WBP_GRP_AI_EndCredit_C"}) ||
           type.starts_with("WBP_GRP_AI_Event") || type.find("Subtitle") != type.npos;
}

bool root_is_dialog(std::string_view type)
{
    return type.starts_with("WBP_Dialog_") || type.starts_with("WBP_GRP_AI_DecisionBranch");
}

bool operation_guide(std::string_view type)
{
    return type == "WBP_GRP_GuideSet_C";
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

bool character_grid(std::string_view type)
{
    return type.starts_with("WBP_GRP_BS_CharaList_") ||
           type.starts_with("WBP_GRP_BS_Top_00_") ||
           type.starts_with("WBP_GRP_BS_ChList_TeamList_");
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
    return setup_surface(type) || type == "WBP_GRP_CSG_Top_00_C";
}

bool episode_screen(std::string_view type)
{
    return type.starts_with("WBP_GRP_AI_CharacterSelect") || type.starts_with("WBP_GRP_AI_Map_") ||
           type.starts_with("WBP_GRP_AI_Chart") || type == "WBP_GRP_AI_Outline_C" ||
           type.starts_with("WBP_GRP_AI_DecisionBranch");
}

struct Node {
    ObjectRef object{};
    NodeIndex parent{none}, root{none}, end{};
    std::string name, type, text;
    std::optional<ViewRect> painted;
    ViewRect clip{};
    bool keyboard{}, controller{};
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
            if (candidates.size() >= maximum_roots) break;
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
                if (r.focus_path || root_is_dialog(r.type) || split_focus_surface(r.type) || controls_screen(r.type)) return 0;
                if (tutorial_root(r.type) || root_is_cinematic(r.type) || operation_guide(r.type) ||
                    r.type == "WBP_MainMenu_Base_TextSub_C" || r.type == "WBP_GRP_BS_StageName_DP_C") return 1;
                return 2;
            };
            return priority(a) < priority(b);
        });
        for (const auto& root : candidates) {
            if (attempts_ >= maximum_widgets) break;
            const auto extent = rect(game_.geometry(root.object));
            if (!extent) continue;
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
        if (type.ends_with("ScrollBox") || (clipping >= 1 && clipping <= 3)) {
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
        if ((focus_path || proxy) && !is_image) {
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
            const bool child_focus = focus_path &&
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
    auto name = read_ui_text(game.text(asset, "CharacterDataAssetRecord.NameInfo.FullName"));
    append(name, read_ui_text(game.text(asset, "CharacterDataAssetRecord.NameInfo.TitleName")));
    return name;
}

std::string option_label(GameReader& game, const WidgetTree& tree, NodeIndex scope)
{
    auto title = named_text(tree, scope, "Title");
    if (title.empty()) title = named_text(tree, scope, "Text_Title");
    auto value = named_text(tree, scope, "caption");
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
    if (side_changer && game.engine().is_a(tree[scope].object, side_changer))
        return option_label(game, tree, scope);

    for (const auto name : {"caption", "Title", "Text_Title", "Text_Name", "Text_CharaName",
                            "TXT_Title", "TXT_Name", "Text_StageName", "Text_BattleName"}) {
        if (auto value = named_text(tree, scope, name); !value.empty()) return value;
    }

    // Some Blueprint buttons carry just one painted text leaf with a custom name.
    const auto candidate = unique_widget(tree, scope, [](const Node& node) {
        return !node.text.empty() && !node.name.starts_with("RichText_Pad") &&
               !node.name.starts_with("RTEXT_Help");
    });
    return candidate ? tree[*candidate].text : std::string{};
}



std::optional<NodeIndex> related_hit_label(GameReader& game, const WidgetTree& tree,
                                           NodeIndex scope)
{
    const auto& hit = tree[scope];
    const auto& root = tree[hit.root];
    const auto remote = tree.locate(game.object(hit.object, "RemoteButton"));
    if (remote && tree[*remote].root == hit.root) return remote;

    return unique_widget(tree, hit.root, [&](const Node& visual) {
        return ui_detail::matches_menu_visual(root.type, hit.name, visual.name);
    });
}

bool focus_has_visual(GameReader& game, const WidgetTree& tree, NodeIndex index)
{
    const auto& node = tree[index];
    if (!hit_target(node.type) || !split_focus_surface(tree[node.root].type)) return tree.painted(index).has_value();
    const auto visual = related_hit_label(game, tree, index);
    if (!visual) return false;
    if (tree.painted(*visual)) return true;
    for (auto i = *visual + 1; i < tree[*visual].end; ++i)
        if (!tree[i].text.empty() || (tree[i].name == "IMG_Face_Main" && tree[i].painted)) return true;
    return false;
}

NodeIndex focused_widget(GameReader& game, const WidgetTree& tree)
{
    NodeIndex chosen = none;
    bool ambiguous = false;
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if ((!node.keyboard && !node.controller) || !focus_has_visual(game, tree, i)) continue;
        if (chosen == none || (node.keyboard && !tree[chosen].keyboard)) {
            chosen = i;
            ambiguous = false;
        } else if (node.keyboard == tree[chosen].keyboard) {
            if (i < tree[chosen].end) chosen = i;
            else ambiguous = true;
        }
    }
    if (ambiguous || chosen != none) return ambiguous ? none : chosen;

    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& root = tree[i];
        if (root.parent != none) continue;
        if (root_is_dialog(root.type)) return none;
        if (!split_focus_surface(root.type) || stage_list(root.type) ||
            game.boolean(root.object, "bIsActive") != true ||
            game.boolean(root.object, "bIsDisableKeyInput") != false) continue;
        const auto target = tree.locate(game.object(root.object, root.type == "WBP_GRP_CSG_Top_00_C" ?
                                                    "BuiltInMenu.LastFocusedWidget" : "LastFocusedWidget"));
        if (!target || tree[*target].root != i || !hit_target(tree[*target].type) ||
            !focus_has_visual(game, tree, *target)) continue;
        if (chosen != none) return none;
        chosen = *target;
    }
    return chosen;
}

std::string focused_label(GameReader& game, const WidgetTree& tree, NodeIndex focused,
                          ObjectRef side_changer)
{
    const auto& initial = tree[focused];
    if (!initial.text.empty()) return initial.text;
    if (initial.type == "WBP_MainMenu_Base_C") return named_text(tree, focused, "TXT_GuideMessage");
    for (auto scope = focused; scope != none; scope = tree[scope].parent) {
        const auto& node = tree[scope];
        if (node.type.starts_with("WBP_OBJ_BS_CharaIcon_")) return portrait_label(game, tree, scope);
        if (const auto remote = related_hit_label(game, tree, scope)) {
            if (tree[*remote].type.starts_with("WBP_OBJ_BS_CharaIcon_"))
                return portrait_label(game, tree, *remote);
            if (auto caption = label(game, tree, *remote, side_changer); !caption.empty()) return caption;
        }

        // Keep an unlabeled hit target local. A broad ancestor label can belong to another button.
        if (hit_target(node.type)) return {};
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
           type.starts_with("WBP_GRP_BS_CharaNameSet") || tutorial_root(type) ||
           root_is_cinematic(type) || type.starts_with("WBP_CMN_Waiting");
}

std::string episode_context(const WidgetTree& tree, NodeIndex root, bool choice_dialog)
{
    std::string context;
    const bool map = tree[root].type.starts_with("WBP_GRP_AI_Map_");
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (node.text.empty()) continue;
        const auto& surface = tree[node.root].type;
        const bool chart = map && (surface.starts_with("WBP_GRP_AI_Chart") ||
                                   surface == "WBP_GRP_AI_Outline_C");
        const bool choice_text = choice_dialog && node.root == root &&
            one_of(node.name, {"Title", "Text_Title", "Text_Explain"});
        if (chart || choice_text) append(context, node.text);
        if (context.size() >= 1300) break;
    }
    return context;
}

void collect_subtitle(GameReader& game, const WidgetTree& tree, ScreenReading& result,
                      std::unordered_set<NodeIndex>& consumed, NodeIndex modal_root)
{
    const auto text_ui = game.engine().find_class("SSMenuTextUi");
    const auto captions = game.engine().find_class("SSWCTSubtitle");
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (modal_root != none && node.root != modal_root) continue;
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
        if (result.subtitle.empty()) result.subtitle = std::move(line);
    }
}

void collect_tutorial(GameReader& game, const WidgetTree& tree, ScreenReading& result,
                      std::unordered_set<NodeIndex>& consumed, NodeIndex modal_root)
{
    for (NodeIndex i = 0; i < tree.nodes().size(); ++i) {
        const auto& node = tree[i];
        if (modal_root != none && node.root != modal_root) continue;
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

ObjectRef training_level_guide(GameReader& game, const WidgetTree& tree, ObjectRef world)
{
    ObjectRef result{};
    for (const auto level : game.objects(world, "Levels", 96)) {
        if (!level || game.object(level, "OwningWorld") != world ||
            game.boolean(level, "bIsVisible") != true) continue;
        const auto script = game.object(level, "LevelScriptActor");
        if (!native_type(game, script, "SSTrainingBattleUiLevelScriptActor")) continue;
        const auto menu = game.object(script, "MenuController");
        if (!native_type(game, menu, "SSTrainingBattleUiController")) continue;
        const auto guide = game.object(menu, "GuideWidget");
        if (!tree.locate(guide)) continue;
        if (result && result != guide) return {};
        result = guide;
    }
    return result;
}

ObjectRef free_training_guide(GameReader& game, const WidgetTree& tree)
{
    bool has_guide = false;
    for (const auto& root : tree.nodes()) {
        if (root.parent != none) continue;
        if (root_is_dialog(root.type) || controls_screen(root.type) || training_settings(root.type) ||
            split_focus_surface(root.type) || root.type == "WBP_Pause_C" ||
            root.type.starts_with("WBP_Option_")) return {};
        if (operation_guide(root.type)) has_guide = true;
    }
    if (!has_guide) return {};

    const auto instance = game.engine().game_instance();
    // EKoratBattlePauseMode::Training is Free Training. Battle Training, practice
    // demos, and the opening tutorial each have their own mode.
    if (!native_type(game, instance, "SSGameInstance") || game.integer(instance, "BattlePauseMode") != 1) return {};
    const auto players = game.objects(instance, "LocalPlayers", 1);
    if (players.empty()) return {};
    const auto controller = game.object(players.front(), "PlayerController");
    if (!native_type(game, controller, "SSBattlePlayerController") ||
        !game.call_bool(controller, "IsLocalPlayerController").value_or(false)) return {};
    const auto hud = game.object(controller, "MyHUD");
    if (!native_type(game, hud, "SSMainGameHUD") || game.object(hud, "PlayerOwner") != controller ||
        game.boolean(hud, "bShowHUD") == false) return {};

    const auto gameplay = game.object(controller, "GuideWidget");
    if (tree.locate(gameplay)) return gameplay;
    const auto world = game.object(game.engine().outer(controller), "OwningWorld");
    if (world && game.object(world, "OwningGameInstance") == instance)
        return training_level_guide(game, tree, world);
    return {};
}

void collect_free_training_help(GameReader& game, const WidgetTree& tree, ScreenReading& result)
{
    const auto owner = free_training_guide(game, tree);
    const auto guide = tree.locate(owner);
    if (!guide || !operation_guide(tree[*guide].type) || !native_type(game, owner, "SSOperationGuideSet")) return;
    const auto group = tree.locate(game.object(owner, "ButtonGuideWidget"));
    const auto scope = group && *group > *guide && *group < tree[*guide].end ? *group : *guide;
    auto prompt = unique_widget(tree, scope, [](const Node& node) {
        return node.type == "WBP_OBJ_Guide_Btn_Present_C";
    });
    if (!prompt && scope != *guide) {
        prompt = unique_widget(tree, *guide, [](const Node& node) {
            return node.type == "WBP_OBJ_Guide_Btn_Present_C";
        });
    }
    if (!prompt) return;
    auto text = help_prompt(tree, *prompt);
    if (text.empty()) return;
    result.root = owner;
    result.focus = {};
    result.announcement = std::move(text);
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
        if (!split_focus_surface(root.type) ||
            game.boolean(root.object, "bIsActive") != true ||
            game.boolean(root.object, "bIsDisableKeyInput") != false) continue;
        if (!stage_list(root.type) || stage != none) return none;
        stage = root.root;
    }
    return has_title ? stage : none;
}

NodeIndex main_surface(GameReader& game, const WidgetTree& tree, NodeIndex focus)
{
    if (focus != none) return tree[focus].root;
    if (const auto stage = stage_surface(game, tree); stage != none) return stage;
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
    return dialog != none ? dialog : most_lines;
}

} // namespace

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

ScreenReading UiReader::sample(std::span<const ObjectRef> roots, bool cinematic)
{
    WidgetTree tree(game_);
    tree.capture(roots, cinematic);
    ScreenReading result;
    const auto focus = focused_widget(game_, tree);
    const auto surface = main_surface(game_, tree, focus);
    if (surface != none) result.root = tree[surface].object;
    if (focus != none) result.focus = tree[focus].object;
    const auto side_changer = game_.engine().find_class("SSMenuButtonSideChanger");
    const auto modal = surface != none && root_is_dialog(tree[surface].type) ? surface : none;

    if (focus != none) result.announcement = focused_label(game_, tree, focus, side_changer);
    if (result.announcement.empty() && surface != none && tree[surface].type == "WBP_Title_C") {
        for (auto i = surface + 1; i < tree[surface].end; ++i)
            if (tree[i].type == "WBP_Title_Button_PC_C")
                append(result.announcement, named_text(tree, i, "Caption_00"));
    }
    if (result.announcement.empty() && modal != none && !episode_screen(tree[modal].type)) {
        for (auto i = modal + 1; i < tree[modal].end; ++i) append(result.announcement, tree[i].text);
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
    if (surface != none && episode_screen(tree[surface].type)) {
        auto context = episode_context(tree, surface, modal == surface);
        // Wire the currently rendered chart or decision context to the active view only.
        append(context, result.announcement);
        result.announcement = std::move(context);
    }

    std::unordered_set<NodeIndex> consumed;
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
        for (const auto& line : details.lines) append(result.announcement, line);
    } else {
        reset();
    }

    collect_subtitle(game_, tree, result, consumed, modal);
    collect_tutorial(game_, tree, result, consumed, modal);
    if (!cinematic && result.announcement.empty() && result.tutorial.empty())
        collect_free_training_help(game_, tree, result);

    std::vector<Item> items;
    bool shown_focus = false;
    if (!result.announcement.empty() && focus != none) {
        put_item(items, tree, focus, result.announcement);
        shown_focus = !items.empty();
        if (shown_focus && panel != none && skill_row != none)
            consume_text(tree, skill_row, consumed, {"caption"});
    }

    for (NodeIndex i = 0; i < tree.nodes().size() && items.size() < maximum_items; ++i) {
        const auto& node = tree[i];
        if (modal != none && node.root != modal) continue;
        if (side_changer && node.type.starts_with("WBP_") && game_.engine().is_a(node.object, side_changer)) {
            const auto value = option_label(game_, tree, i);
            if (value.empty() || !tree.painted(i)) continue;
            put_item(items, tree, i, value);
            consume_text(tree, i, consumed, {"Title", "Text_Title", "caption", "Text_Disable"});
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
        } else if (node.type.starts_with("WBP_OBJ_BS_CharaIcon_") && character_grid(tree[node.root].type)) {
            const auto face = unique_widget(tree, i, [](const Node& leaf) {
                return leaf.name == "IMG_Face_Main" && leaf.painted;
            });
            put_item(items, tree, face.value_or(i), portrait_label(game_, tree, i));
        }
        if (!node.text.empty() && !consumed.contains(i)) put_item(items, tree, i, node.text);
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const auto a_line = std::floor(a.position.t / 12.0);
        const auto b_line = std::floor(b.position.t / 12.0);
        if (a_line != b_line) return a_line < b_line;
        return a.position.l < b.position.l;
    });
    std::unordered_set<std::string> seen;
    const auto remember = [&](const std::string& text) {
        if (!text.empty() && seen.insert(text).second) result.items.push_back(text);
    };
    if (!shown_focus) remember(result.announcement);
    for (const auto& item : items) remember(item.text);
    remember(result.tutorial);
    remember(result.subtitle);
    return result;
}

} // namespace sparking
