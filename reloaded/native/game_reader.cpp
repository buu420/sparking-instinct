#include "game_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace sparking {
namespace {
constexpr std::uint64_t parm = 0x80;
constexpr std::uint64_t out_parm = 0x100;
constexpr std::uint64_t return_parm = 0x400;
constexpr std::uint64_t const_parm = 0x2;
constexpr std::uint32_t native_function = 0x400;
constexpr std::size_t parameter_limit = 256;
constexpr std::size_t max_array = 8192;
constexpr std::size_t max_roots = 96;
constexpr std::size_t max_panel_children = 160;

struct Method {
    std::string_view owner;
    std::string_view name;
};

constexpr Method getters[] = {
    {"Widget", "GetVisibility"}, {"Widget", "IsVisible"}, {"Widget", "IsInViewport"},
    {"Widget", "HasKeyboardFocus"}, {"Widget", "HasFocusedDescendants"}, {"Widget", "HasAnyUserFocus"},
    {"Widget", "GetParent"}, {"Widget", "GetRenderOpacity"}, {"Widget", "GetCachedGeometry"},
    {"PanelWidget", "GetChildrenCount"}, {"PanelWidget", "GetChildAt"},
    {"WidgetSwitcher", "GetActiveWidget"}, {"WidgetSwitcher", "GetActiveWidgetIndex"},
    {"TextBlock", "GetText"}, {"RichTextBlock", "GetText"}, {"SpTextBlock", "GetText"},
    {"EditableText", "GetText"}, {"EditableTextBox", "GetText"},
    {"MultiLineEditableText", "GetText"}, {"MultiLineEditableTextBox", "GetText"},
    {"WidgetBlueprintLibrary", "GetAllWidgetsOfClass"},
    {"SlateBlueprintLibrary", "GetAbsoluteSize"}, {"SlateBlueprintLibrary", "LocalToAbsolute"},
    {"Controller", "GetViewTarget"}, {"Controller", "IsLocalController"},
    {"Controller", "IsLocalPlayerController"}, {"Pawn", "IsLocallyControlled"},
    {"Pawn", "IsPlayerControlled"}, {"Pawn", "IsLocallyViewed"},
    {"SSBattlePlayerController", "GetPlaySide"}, {"SSBattleGameStateBase", "GetPlayerCharacter"},
    {"SSCharacter", "GetMaxHPGaugeValue"}, {"SSCharacter", "GetMaxSPGaugeValue"},
    {"SSCharacter", "IsSparkingModeState"},
    {"SSCharacter", "IsBattleKeyPressed"}, {"SSCharacter", "GetBattleKeyHoldTime"},
    {"SSMainGameHUDWidget", "GetSkillGuideSetOfPlayer"},
    {"SSMainGameHUDWidgetRoot", "GetSkillGuideSetOfPlayer"},
    {"SSCriMediaPlayer", "IsPlaying"}, {"SSCriMediaPlayer", "IsPaused"}, {"SSCriMediaPlayer", "IsCrossFading"},
    {"AtomComponent", "GetCueName"}, {"AtomComponent", "GetStatus"},
    {"AtomComponent", "GetVolume"}, {"AtomComponent", "IsPaused"},
    {"ManaComponent", "GetSource"}, {"ManaComponent", "GetFrameNumber"},
    {"ManaComponent", "GetStatus"}, {"ManaComponent", "IsPlaying"},
    {"ManaComponent", "IsPaused"}, {"ManaMovie", "GetUrl"}
};

template<class T>
std::optional<std::int64_t> integer_at(Address address, std::uint32_t size)
{
    if (size != sizeof(T)) return {};
    const auto value = read<T>(address);
    if (!value || !std::in_range<std::int64_t>(*value)) return {};
    return static_cast<std::int64_t>(*value);
}

template<class T> std::span<const std::byte> bytes_of(const T& value)
{
    return std::as_bytes(std::span{&value, std::size_t{1}});
}

bool math_result(const Field& field, std::size_t bytes)
{
    return field.kind == Kind::Struct && field.count == 1 && field.size == bytes;
}

struct Vector {
    double x{}, y{};
};

std::optional<Vector> vector_at(Address address)
{
    const auto vector = read<Vector>(address);
    if (!vector || !std::isfinite(vector->x) || !std::isfinite(vector->y)) return {};
    return vector;
}
}

struct GameReader::Return {
    alignas(16) std::array<std::byte, parameter_limit> data{};
    Field field{};

    Address address() const { return reinterpret_cast<Address>(data.data() + field.offset); }
    Value value() const { return {address(), field}; }
};

void GameReader::reset()
{
    signatures_.clear();
    widget_switcher_.reset();
    panel_widget_.reset();
}

std::optional<GameReader::Value> GameReader::property(ObjectRef type, Address storage,
                                                       std::size_t size, std::string_view path)
{
    for (unsigned depth = 0; type && !path.empty() && depth < 8; ++depth) {
        const auto dot = path.find('.');
        const auto field = engine_.property(type, path.substr(0, dot));
        if (!field || field->count != 1 || field->offset > size || field->size > size - field->offset)
            return {};
        storage += field->offset;
        if (dot == path.npos) return Value{storage, *field};
        if (field->kind == Kind::Struct) {
            size = field->size;
            type = engine_.capture(field->child);
        } else {
            const auto next = object({storage, *field});
            const auto header = engine_.inspect(next);
            if (!header) return {};
            storage = next.address;
            type = engine_.capture(header->type);
            size = read<std::uint32_t>(type.address + 0x58).value_or(0);
        }
        path.remove_prefix(dot + 1);
    }
    return {};
}

std::optional<GameReader::Value> GameReader::property(ObjectRef owner, std::string_view path)
{
    const auto header = engine_.inspect(owner);
    if (!header) return {};
    const auto type = engine_.capture(header->type);
    const auto size = read<std::uint32_t>(type.address + 0x58).value_or(0);
    return property(type, owner.address, size, path);
}

ObjectRef GameReader::object(Value value)
{
    const auto& field = value.field;
    if (field.kind == Kind::Object || field.kind == Kind::Class) {
        if (field.size != sizeof(Address)) return {};
        return engine_.capture(read<Address>(value.address).value_or(0));
    }
    if (field.kind == Kind::WeakObject || field.kind == Kind::SoftObject) {
        if (field.size < 8) return {};
        const auto weak = read<std::array<std::int32_t, 2>>(value.address);
        if (weak) return engine_.weak((*weak)[0], (*weak)[1]);
    }
    return {};
}

ObjectRef GameReader::object(ObjectRef owner, std::string_view path)
{
    const auto value = property(owner, path);
    return value ? object(*value) : ObjectRef{};
}

bool GameReader::matches_object(ObjectRef owner, std::string_view path, ObjectRef target)
{
    if (!target) return false;
    const auto value = property(owner, path);
    if (!value) return false;
    if (const auto resolved = object(*value)) return resolved == target;
    if (value->field.kind != Kind::SoftObject || value->field.size != 0x30) return false;

    // The game may resolve a temporary copy of this soft pointer when it paints a texture.
    // Its original weak pointer can stay empty while the package and asset names still match.
    const auto asset_path = read<std::array<NameId, 2>>(value->address + 0x10);
    const auto subpath = read<ArrayHeader>(value->address + 0x20);
    if (!asset_path || !(*asset_path)[0].index || !(*asset_path)[1].index || !subpath || subpath->count > 1 ||
        subpath->count < 0 || subpath->capacity < subpath->count) return false;
    if (subpath->count == 1 && read<wchar_t>(subpath->data) != L'\0') return false;
    const auto asset = engine_.inspect(target);
    const auto package = engine_.inspect(engine_.outer(target));
    return asset && package && !package->outer &&
           package->name == (*asset_path)[0] && asset->name == (*asset_path)[1];
}

std::optional<std::int64_t> GameReader::integer(Value value)
{
    const auto& field = value.field;
    const auto address = value.address;
    switch (field.kind) {
    case Kind::Int8: return integer_at<std::int8_t>(address, field.size);
    case Kind::Int16: return integer_at<std::int16_t>(address, field.size);
    case Kind::Int32: return integer_at<std::int32_t>(address, field.size);
    case Kind::Int64: return integer_at<std::int64_t>(address, field.size);
    case Kind::Byte: return integer_at<std::uint8_t>(address, field.size);
    case Kind::UInt16: return integer_at<std::uint16_t>(address, field.size);
    case Kind::UInt32: return integer_at<std::uint32_t>(address, field.size);
    case Kind::UInt64: return integer_at<std::uint64_t>(address, field.size);
    case Kind::Enum: {
        const auto inner = engine_.describe(field.child);
        if (inner && inner->size == field.size && inner->kind != Kind::Enum)
            return integer({address, *inner});
        break;
    }
    default: break;
    }
    return {};
}

std::optional<std::int64_t> GameReader::integer(ObjectRef owner, std::string_view path)
{
    const auto value = property(owner, path);
    return value ? integer(*value) : std::nullopt;
}

std::optional<double> GameReader::number(Value value)
{
    if (value.field.kind == Kind::Float && value.field.size == sizeof(float)) {
        const auto result = read<float>(value.address);
        if (result && std::isfinite(*result)) return *result;
    } else if (value.field.kind == Kind::Double && value.field.size == sizeof(double)) {
        const auto result = read<double>(value.address);
        if (result && std::isfinite(*result)) return *result;
    } else if (const auto result = integer(value)) {
        return static_cast<double>(*result);
    }
    return {};
}

std::optional<double> GameReader::number(ObjectRef owner, std::string_view path)
{
    const auto value = property(owner, path);
    return value ? number(*value) : std::nullopt;
}

std::optional<bool> GameReader::boolean(Value value)
{
    const auto& field = value.field;
    if (field.kind != Kind::Bool || field.byte_offset >= field.size || !field.mask) return {};
    const auto byte = read<std::uint8_t>(value.address + field.byte_offset);
    if (!byte) return {};
    return (*byte & field.mask) != 0;
}

std::optional<bool> GameReader::boolean(ObjectRef owner, std::string_view path)
{
    const auto value = property(owner, path);
    return value ? boolean(*value) : std::nullopt;
}

std::string GameReader::text(Value value)
{
    const auto& field = value.field;
    if (field.kind == Kind::Text && field.size == 0x18)
        return engine_.text(value.address).value_or("");
    if (field.kind == Kind::String && field.size == sizeof(ArrayHeader))
        return engine_.string(value.address).value_or("");
    if (field.kind == Kind::Name && field.size == sizeof(NameId)) {
        const auto id = read<NameId>(value.address);
        if (id) return engine_.name(*id);
    }
    return {};
}

std::string GameReader::text(ObjectRef owner, std::string_view path)
{
    const auto value = property(owner, path);
    return value ? text(*value) : std::string{};
}

std::string GameReader::text_key(ObjectRef text_widget)
{
    const auto value = property(text_widget, "Text");
    if (!value || value->field.kind != Kind::Text || value->field.size != 0x18) return {};
    return engine_.text_key(value->address);
}

std::optional<ArrayHeader> GameReader::array(Value value)
{
    if (value.field.kind != Kind::Array || value.field.size != sizeof(ArrayHeader)) return {};
    const auto header = read<ArrayHeader>(value.address);
    if (!header || header->count < 0 || header->capacity < header->count ||
        static_cast<std::size_t>(header->count) > max_array ||
        (header->count && !header->data)) return {};
    return header;
}

std::optional<GameReader::Value> GameReader::array_element(Value value, std::size_t index)
{
    const auto header = array(value);
    if (!header || index >= static_cast<std::size_t>(header->count)) return {};
    const auto item = engine_.describe(value.field.child);
    if (!item || item->count != 1) return {};
    return Value{header->data + index * item->size, *item};
}

std::vector<ObjectRef> GameReader::objects(ObjectRef owner, std::string_view path, std::size_t limit)
{
    const auto value = property(owner, path);
    if (!value || value->field.kind != Kind::Array) return {};
    const auto header = array(*value);
    const auto item = engine_.describe(value->field.child);
    if (!header || !item || item->count != 1) return {};
    const auto count = (std::min)(limit, static_cast<std::size_t>(header->count));
    std::vector<ObjectRef> result;
    result.reserve(count);
    // Keep array indices when a slot is empty.
    for (std::size_t i = 0; i < count; ++i)
        result.push_back(object({header->data + i * item->size, *item}));
    return result;
}

std::optional<double> GameReader::array_number(ObjectRef owner, std::string_view path,
                                               std::size_t index, std::string_view member)
{
    const auto value = property(owner, path);
    const auto item = value ? array_element(*value, index) : std::nullopt;
    if (!item || item->field.kind != Kind::Struct) return {};
    const auto field = property(engine_.capture(item->field.child), item->address, item->field.size, member);
    return field ? number(*field) : std::nullopt;
}

const GameReader::Signature* GameReader::signature(ObjectRef target, std::string_view method)
{
    const auto header = engine_.inspect(target);
    if (!header || !engine_.readable()) return nullptr;
    const auto function = engine_.function(engine_.capture(header->type), method);
    if (!function) return nullptr;
    const auto previous = signatures_.find(function.address);
    if (previous != signatures_.end() && previous->second.function == function) return &previous->second;

    const auto owner = engine_.name_of(engine_.outer(function));
    if (!(read<std::uint32_t>(function.address + 0xb0).value_or(0) & native_function) ||
        !std::ranges::any_of(getters, [&](auto getter) { return getter.owner == owner && getter.name == method; }))
        return nullptr;
    const auto size = read<std::uint16_t>(function.address + 0xb6);
    const auto count = read<std::uint8_t>(function.address + 0xb4);
    if (!size || *size > parameter_limit || !count) return nullptr;
    auto fields = engine_.fields(function);
    std::erase_if(fields, [](const Field& field) { return !(field.flags & parm); });
    if (fields.size() != *count) return nullptr;
    std::ranges::sort(fields, {}, &Field::offset);

    Signature signature{function};
    std::size_t next{};
    for (const auto& field : fields) {
        if (field.count != 1 || field.offset < next || field.offset + field.size > *size)
            return nullptr;
        next = static_cast<std::size_t>(field.offset) + field.size;
        if (field.flags & return_parm) {
            if (signature.result || field.offset != read<std::uint16_t>(function.address + 0xb8).value_or(0xffff))
                return nullptr;
            signature.result = field;
        } else if ((field.flags & out_parm) && !(field.flags & const_parm)) {
            if (signature.output || method != "GetAllWidgetsOfClass" ||
                field.kind != Kind::Array || field.size != sizeof(ArrayHeader)) return nullptr;
            signature.output = field;
        } else {
            signature.inputs.push_back(field);
        }
    }
    return &signatures_.insert_or_assign(function.address, std::move(signature)).first->second;
}

std::optional<GameReader::Return> GameReader::invoke(ObjectRef target, std::string_view method,
                                                    std::span<const std::byte> first,
                                                    std::span<const std::byte> second)
{
    const auto* layout = signature(target, method);
    if (!layout || !layout->result || layout->output) return {};
    const auto result_field = *layout->result;
    if (result_field.kind == Kind::Text && !engine_.can_destroy_text(result_field)) return {};
    const std::array input = {first, second};
    const std::size_t arguments = second.empty() ? (first.empty() ? 0 : 1) : 2;
    if (arguments != layout->inputs.size()) return {};
    Return result;
    result.field = result_field;
    for (std::size_t i = 0; i < arguments; ++i) {
        const auto& field = layout->inputs[i];
        const auto value = input[i];
        if (field.kind == Kind::Struct) {
            if (field.size != value.size()) return {};
        } else {
            if (value.size() != sizeof(std::int32_t)) return {};
            std::int32_t argument{};
            std::memcpy(&argument, value.data(), sizeof argument);
            const auto member = field.kind == Kind::Enum ? engine_.describe(field.child) : std::optional{field};
            if (!member || field.size != member->size) return {};
            if (member->kind == Kind::Byte && field.size == 1 && std::in_range<std::uint8_t>(argument)) {
                const auto byte = static_cast<std::uint8_t>(argument);
                std::memcpy(result.data.data() + field.offset, &byte, sizeof byte);
                continue;
            }
            if (member->kind != Kind::Int32 || field.size != sizeof(argument)) return {};
        }
        std::memcpy(result.data.data() + field.offset, value.data(), value.size());
    }
    const bool success = engine_.call(target, layout->function, result.data.data());
    if (!success) {
        if (result_field.kind == Kind::Text) engine_.destroy_text(result_field, result.address());
        if (result_field.kind == Kind::String) {
            const auto string = read<ArrayHeader>(result.address());
            if (string) engine_.release(string->data);
        }
        return {};
    }
    return result;
}

ObjectRef GameReader::call_object(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument)
{
    const auto result = invoke(owner, method, argument ? bytes_of(*argument) : std::span<const std::byte>{});
    return result ? object(result->value()) : ObjectRef{};
}

std::optional<double> GameReader::call_number(ObjectRef owner, std::string_view method,
                                             std::optional<std::int32_t> argument)
{
    const auto result = invoke(owner, method, argument ? bytes_of(*argument) : std::span<const std::byte>{});
    return result ? number(result->value()) : std::nullopt;
}

std::optional<bool> GameReader::call_bool(ObjectRef owner, std::string_view method,
                                         std::optional<std::int32_t> argument)
{
    const auto result = invoke(owner, method, argument ? bytes_of(*argument) : std::span<const std::byte>{});
    return result ? boolean(result->value()) : std::nullopt;
}

std::string GameReader::call_text(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument)
{
    const auto result = invoke(owner, method, argument ? bytes_of(*argument) : std::span<const std::byte>{});
    if (!result) return {};
    const auto release = [&] {
        if (result->field.kind == Kind::Text) engine_.destroy_text(result->field, result->address());
        if (result->field.kind == Kind::String) {
            const auto string = read<ArrayHeader>(result->address());
            if (string) engine_.release(string->data);
        }
    };
    try {
        auto value = text(result->value());
        release();
        return value;
    } catch (...) {
        release();
        throw;
    }
}

bool GameReader::visible(ObjectRef widget)
{
    if (!widget || !engine_.live(widget)) return false;
    auto visibility = integer(widget, "Visibility");
    if (!visibility) {
        const auto native = call_number(widget, "GetVisibility");
        if (native && *native >= 0 && *native <= 4) visibility = static_cast<std::int64_t>(*native);
    }
    if (!visibility || (*visibility != 0 && *visibility != 3 && *visibility != 4)) return false;
    auto opacity = number(widget, "RenderOpacity");
    if (!opacity) opacity = call_number(widget, "GetRenderOpacity");
    return opacity && *opacity > 0.001;
}

std::optional<WidgetBounds> GameReader::geometry(ObjectRef widget)
{
    const auto geometry = invoke(widget, "GetCachedGeometry");
    if (!geometry || !math_result(geometry->field, 64)) return {};
    const auto library = engine_.find("/Script/UMG.Default__SlateBlueprintLibrary");
    if (!library) return {};
    const auto bytes = std::span{geometry->data}.subspan(geometry->field.offset, 64);
    const auto size = invoke(library, "GetAbsoluteSize", bytes);
    const auto origin = invoke(library, "LocalToAbsolute", bytes, bytes_of(Vector{}));
    if (!size || !origin || !math_result(size->field, sizeof(Vector)) ||
        !math_result(origin->field, sizeof(Vector))) return {};
    const auto dimensions = vector_at(size->address());
    const auto point = vector_at(origin->address());
    if (!point || !dimensions || dimensions->x < 1 || dimensions->y < 1) return {};
    return WidgetBounds{point->x, point->y, dimensions->x, dimensions->y};
}

std::vector<ObjectRef> GameReader::widget_children(ObjectRef widget)
{
    if (!widget) return {};
    if (!widget_switcher_ || !*widget_switcher_) widget_switcher_ = engine_.find_class("WidgetSwitcher");
    if (engine_.is_a(widget, *widget_switcher_)) {
        const auto active = call_object(widget, "GetActiveWidget");
        return active ? std::vector{active} : std::vector<ObjectRef>{};
    }
    if (const auto root = object(widget, "WidgetTree.RootWidget"); root && root != widget)
        return {root};
    if (!panel_widget_ || !*panel_widget_) panel_widget_ = engine_.find_class("PanelWidget");
    if (!engine_.is_a(widget, *panel_widget_)) return {};
    if (const auto field = property(widget, "Slots")) {
        const auto slots = objects(widget, "Slots", max_panel_children);
        std::vector<ObjectRef> children;
        children.reserve(slots.size());
        for (const auto slot : slots) {
            if (slot && object(slot, "Parent") == widget) {
                if (const auto child = object(slot, "Content"); child && child != widget)
                    children.push_back(child);
            }
        }
        return children;
    }
    const auto count = call_number(widget, "GetChildrenCount");
    if (!count || *count <= 0 || *count > 2048) return {};
    const auto capped = (std::min)(static_cast<std::size_t>(*count), max_panel_children);
    std::vector<ObjectRef> result;
    result.reserve(capped);
    for (std::size_t i = 0; i < capped; ++i) {
        if (auto child = call_object(widget, "GetChildAt", static_cast<std::int32_t>(i)))
            result.push_back(child);
    }
    return result;
}

std::vector<ObjectRef> GameReader::viewport_roots()
{
    const auto world = engine_.game_instance();
    const auto library = engine_.find("/Script/UMG.Default__WidgetBlueprintLibrary");
    const auto type = engine_.find_class("UserWidget");
    if (!world || !library || !type || !engine_.world_context(world)) return {};
    const auto* layout = signature(library, "GetAllWidgetsOfClass");
    if (!layout || !layout->output || layout->result || layout->inputs.size() != 3) return {};
    const auto& context = layout->inputs[0];
    const auto& widget = layout->inputs[1];
    const auto& top = layout->inputs[2];
    if (context.kind != Kind::Object || context.size != sizeof(Address) ||
        widget.kind != Kind::Class || widget.size != sizeof(Address) ||
        top.kind != Kind::Bool || !top.byte_mask || top.byte_offset >= top.size) return {};
    alignas(16) std::array<std::byte, parameter_limit> data{};
    std::memcpy(data.data() + context.offset, &world.address, sizeof(Address));
    std::memcpy(data.data() + widget.offset, &type.address, sizeof(Address));
    data[top.offset + top.byte_offset] = static_cast<std::byte>(top.byte_mask);
    const auto output = reinterpret_cast<Address>(data.data() + layout->output->offset);
    if (const auto item = engine_.describe(layout->output->child);
        !item || item->kind != Kind::Object || item->size != sizeof(Address)) return {};
    const bool called = engine_.call(library, layout->function, data.data());
    const auto raw = read<ArrayHeader>(output);
    struct Allocation {
        Unreal& engine;
        Address data;
        ~Allocation() { engine.release(data); }
    } allocation{engine_, raw ? raw->data : 0};
    if (!called || !raw || raw->count < 0 || raw->capacity < raw->count ||
        static_cast<std::size_t>(raw->count) > max_array || (raw->count && !raw->data)) return {};

    std::vector<ObjectRef> roots;
    for (int i = 0; i < raw->count && roots.size() < max_roots; ++i) {
        const auto root = engine_.capture(read<Address>(raw->data + i * sizeof(Address)).value_or(0));
        if (root && engine_.is_a(root, type) && visible(root) &&
            call_bool(root, "IsInViewport").value_or(false)) roots.push_back(root);
    }
    return roots;
}

}
