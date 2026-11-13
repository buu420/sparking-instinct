#include "lua_unreal.h"
#include "unreal.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace sparking {
namespace {
constexpr auto object_type = "Sparking.Unreal.Object";
constexpr auto text_type = "Sparking.Unreal.Text";
constexpr auto array_type = "Sparking.Unreal.Array";
constexpr auto struct_type = "Sparking.Unreal.Struct";
constexpr auto geometry_type = "Sparking.Unreal.Geometry";
constexpr auto field_type = "Sparking.Unreal.Field";
constexpr auto field_class_type = "Sparking.Unreal.FieldClass";
constexpr char unreal_key{};
constexpr std::size_t max_array = 16384;

struct View {
    ObjectRef owner;
    Address storage{};
    Address descriptor{};
    std::uint32_t size{};
    std::uint64_t frame{};
};

struct Geometry {
    std::array<std::byte, 64> bytes{};
    std::uint64_t frame{};
};

struct FieldView {
    ObjectRef owner;
    Field field;
    std::uint64_t frame{};
};

struct Vector2D { double x{}, y{}; };

LuaUnreal& context(lua_State* state)
{
    lua_rawgetp(state, LUA_REGISTRYINDEX, &unreal_key);
    auto* result = static_cast<LuaUnreal*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return *result;
}

Unreal& engine(lua_State* state) { return *context(state).engine; }

template<class T> T* as(lua_State* state, int index, const char* type)
{
    return static_cast<T*>(luaL_testudata(state, index, type));
}

template<class T> void push(lua_State* state, const char* type, const T& value)
{
    auto* result = static_cast<T*>(lua_newuserdatauv(state, sizeof(T), 0));
    *result = value;
    luaL_setmetatable(state, type);
}

void string(lua_State* state, std::string_view value)
{
    lua_pushlstring(state, value.data(), value.size());
}

void string_or_nil(lua_State* state, const std::string& value)
{
    if (value.empty()) lua_pushnil(state);
    else string(state, value);
}

ObjectRef object(lua_State* state, int index = 1)
{
    const auto* value = as<ObjectRef>(state, index, object_type);
    return value && engine(state).live(*value) ? *value : ObjectRef{};
}

void push_object(lua_State* state, ObjectRef value)
{
    if (value) push(state, object_type, value);
    else lua_pushnil(state);
}

void push_text(lua_State* state, std::optional<std::string> text, const char* kind)
{
    if (!text) {
        lua_pushnil(state);
        return;
    }
    lua_newuserdatauv(state, 1, 2);
    luaL_setmetatable(state, text_type);
    string(state, *text);
    lua_setiuservalue(state, -2, 1);
    lua_pushstring(state, kind);
    lua_setiuservalue(state, -2, 2);
}

int text_value(lua_State* state) { lua_getiuservalue(state, 1, 1); return 1; }
int text_kind(lua_State* state) { lua_getiuservalue(state, 1, 2); return 1; }
int geometry_kind(lua_State* state) { lua_pushliteral(state, "UScriptStruct"); return 1; }

bool live_view(lua_State* state, const View* view)
{
    return view && view->frame == context(state).frame && engine(state).live(view->owner);
}

std::optional<ArrayHeader> array(lua_State* state, const View* view)
{
    if (!live_view(state, view)) return {};
    const auto value = read<ArrayHeader>(view->storage);
    if (!value || value->count < 0 || value->capacity < value->count ||
        static_cast<std::size_t>(value->count) > max_array ||
        (value->count && !value->data)) return {};
    return value;
}

template<class T> void number(lua_State* state, Address address, std::uint32_t bytes)
{
    const auto value = bytes == sizeof(T) ? read<T>(address) : std::nullopt;
    if (!value) lua_pushnil(state);
    else if constexpr (std::is_floating_point_v<T>) lua_pushnumber(state, *value);
    else if constexpr (std::is_same_v<T, std::uint64_t>) {
        if (std::in_range<lua_Integer>(*value))
            lua_pushinteger(state, static_cast<lua_Integer>(*value));
        else lua_pushnil(state);
    } else lua_pushinteger(state, static_cast<lua_Integer>(*value));
}

void push_value(lua_State* state, ObjectRef owner, const Field& field,
                Address address, unsigned depth = 0)
{
    auto& ue = engine(state);
    if (depth > 4 || field.count != 1 || !address) {
        lua_pushnil(state);
        return;
    }
    switch (field.kind) {
    case Kind::Object:
    case Kind::Class: {
        auto value = field.size == sizeof(Address)
            ? ue.capture(read<Address>(address).value_or(0)) : ObjectRef{};
        if (value && field.kind == Kind::Class && !ue.is_class(value)) value = {};
        push_object(state, value);
        return;
    }
    case Kind::WeakObject:
    case Kind::SoftObject: {
        const auto indices = field.size >= 8 ? read<std::array<std::int32_t, 2>>(address) : std::nullopt;
        push_object(state, indices ? ue.weak((*indices)[0], (*indices)[1]) : ObjectRef{});
        return;
    }
    case Kind::Bool: {
        const auto byte = read<std::uint8_t>(address + field.byte_offset);
        if (byte) lua_pushboolean(state, (*byte & field.mask) != 0);
        else lua_pushnil(state);
        return;
    }
    case Kind::Byte: number<std::uint8_t>(state, address, field.size); return;
    case Kind::Int8: number<std::int8_t>(state, address, field.size); return;
    case Kind::Int16: number<std::int16_t>(state, address, field.size); return;
    case Kind::Int32: number<std::int32_t>(state, address, field.size); return;
    case Kind::Int64: number<std::int64_t>(state, address, field.size); return;
    case Kind::UInt16: number<std::uint16_t>(state, address, field.size); return;
    case Kind::UInt32: number<std::uint32_t>(state, address, field.size); return;
    case Kind::UInt64: number<std::uint64_t>(state, address, field.size); return;
    case Kind::Float: number<float>(state, address, field.size); return;
    case Kind::Double: number<double>(state, address, field.size); return;
    case Kind::Enum: {
        const auto underlying = ue.describe(field.child);
        if (underlying && underlying->size == field.size) {
            push_value(state, owner, *underlying, address, depth + 1);
            return;
        }
        break;
    }
    case Kind::Name: {
        const auto value = field.size == sizeof(NameId) ? read<NameId>(address) : std::nullopt;
        push_text(state, value ? std::optional{ue.name(*value)} : std::nullopt, "FName");
        return;
    }
    case Kind::String:
        push_text(state, field.size == sizeof(ArrayHeader) ? ue.string(address) : std::nullopt,
                  "FString");
        return;
    case Kind::Text:
        push_text(state, field.size == 0x18 ? ue.text(address) : std::nullopt, "FText");
        return;
    case Kind::Array:
        if (field.child && field.size == sizeof(ArrayHeader)) {
            push(state, array_type, View{owner, address, field.child, field.size,
                                         context(state).frame});
            return;
        }
        break;
    case Kind::Struct:
        if (field.child) {
            push(state, struct_type, View{owner, address, field.child, field.size,
                                          context(state).frame});
            return;
        }
        break;
    default: break;
    }
    lua_pushnil(state);
}

void array_item(lua_State* state, const View& view, const ArrayHeader& header, lua_Integer index)
{
    auto inner = engine(state).describe(view.descriptor);
    if (index < 1 || index > header.count || !inner ||
        std::uint64_t(header.count) * inner->size > 16 * 1024 * 1024) {
        lua_pushnil(state);
        return;
    }
    push_value(state, view.owner, *inner, header.data + (index - 1) * inner->size);
}

int array_size(lua_State* state)
{
    const auto value = array(state, as<View>(state, 1, array_type));
    if (!value) return 0;
    lua_pushinteger(state, value->count);
    return 1;
}

int array_valid(lua_State* state)
{
    lua_pushboolean(state, array(state, as<View>(state, 1, array_type)).has_value());
    return 1;
}

int array_foreach(lua_State* state)
{
    const auto* view = as<View>(state, 1, array_type);
    const auto value = array(state, view);
    if (!value || !lua_isfunction(state, 2)) return 0;
    for (lua_Integer i = 1; i <= value->count; ++i) {
        lua_pushvalue(state, 2);
        lua_pushinteger(state, i);
        array_item(state, *view, *value, i);
        if (lua_pcall(state, 2, 1, 0) != LUA_OK) return lua_error(state);
        const bool done = lua_isboolean(state, -1) && lua_toboolean(state, -1);
        lua_pop(state, 1);
        if (done) break;
    }
    return 0;
}

int array_index(lua_State* state)
{
    auto* view = as<View>(state, 1, array_type);
    if (lua_isinteger(state, 2)) {
        const auto value = array(state, view);
        if (!value) return 0;
        array_item(state, *view, *value, lua_tointeger(state, 2));
        return 1;
    }
    const auto* key = lua_tostring(state, 2);
    if (!key) return 0;
    const std::string_view member(key);
    if (member == "GetArrayNum") lua_pushcfunction(state, array_size);
    else if (member == "IsValid") lua_pushcfunction(state, array_valid);
    else if (member == "ForEach") lua_pushcfunction(state, array_foreach);
    else return 0;
    return 1;
}

int struct_index(lua_State* state)
{
    const auto* view = as<View>(state, 1, struct_type);
    const auto* key = lua_tostring(state, 2);
    if (!key || !live_view(state, view)) return 0;
    auto& ue = engine(state);
    const auto type = ue.capture(view->descriptor);
    const auto field = ue.property(type, key);
    if (!field || field->offset + field->size > view->size) return 0;
    push_value(state, view->owner, *field, view->storage + field->offset);
    return 1;
}

int object_valid(lua_State* state) { lua_pushboolean(state, bool(object(state))); return 1; }

int object_equal(lua_State* state)
{
    const auto first = object(state);
    lua_pushboolean(state, first && first == object(state, 2));
    return 1;
}

int object_name(lua_State* state)
{
    string_or_nil(state, engine(state).name_of(object(state)));
    return 1;
}

int object_fname(lua_State* state)
{
    auto name = engine(state).name_of(object(state));
    push_text(state, name.empty() ? std::nullopt : std::optional{std::move(name)}, "FName");
    return 1;
}

int object_full_name(lua_State* state)
{
    auto& ue = engine(state);
    const auto value = object(state);
    const auto type = ue.class_name(value);
    const auto path = ue.path_of(value);
    if (type.empty() || path.empty()) return 0;
    string(state, type + " " + path);
    return 1;
}

int object_class(lua_State* state)
{
    auto& ue = engine(state);
    const auto header = ue.inspect(object(state));
    push_object(state, header ? ue.capture(header->type) : ObjectRef{});
    return 1;
}

int object_super(lua_State* state) { push_object(state, engine(state).super(object(state))); return 1; }
int object_outer(lua_State* state) { push_object(state, engine(state).outer(object(state))); return 1; }

int object_address(lua_State* state)
{
    const auto value = object(state);
    if (!value) return 0;
    lua_pushinteger(state, static_cast<lua_Integer>(value.address));
    return 1;
}

int object_is_a(lua_State* state)
{
    lua_pushboolean(state, engine(state).is_a(object(state), object(state, 2)));
    return 1;
}

int object_kind(lua_State* state) { lua_pushliteral(state, "UObject"); return 1; }

const FieldView* current_field(lua_State* state)
{
    const auto* view = as<FieldView>(state, 1, field_type);
    return view && view->frame == context(state).frame && engine(state).live(view->owner)
        ? view : nullptr;
}

int field_offset(lua_State* state)
{
    const auto* view = current_field(state);
    if (!view) return 0;
    lua_pushinteger(state, view->field.offset);
    return 1;
}

int field_name(lua_State* state)
{
    const auto* view = current_field(state);
    if (!view) return 0;
    push_text(state, engine(state).name(view->field.name), "FName");
    return 1;
}

int field_kind_name(lua_State* state)
{
    const auto* id = as<NameId>(state, 1, field_class_type);
    if (!id) return 0;
    push_text(state, engine(state).name(*id), "FName");
    return 1;
}

std::optional<NameId> field_class_name(const FieldView* view)
{
    const auto type = view ? read<Address>(view->field.address + 8) : std::nullopt;
    return type ? read<NameId>(*type) : std::nullopt;
}

int field_class(lua_State* state)
{
    const auto value = field_class_name(current_field(state));
    if (!value) return 0;
    push(state, field_class_type, *value);
    return 1;
}

int field_is_a(lua_State* state)
{
    const auto value = field_class_name(current_field(state));
    const auto* key = lua_tostring(state, 2);
    lua_pushboolean(state, value && key && engine(state).name(*value) == key);
    return 1;
}

int field_enum(lua_State* state)
{
    const auto* view = current_field(state);
    if (!view || view->field.kind != Kind::Enum) return 0;
    push_object(state, engine(state).capture(read<Address>(view->field.address + 0x80).value_or(0)));
    return 1;
}

int enum_name(lua_State* state)
{
    auto& ue = engine(state);
    const auto value = object(state);
    if (!value || ue.class_name(value) != "Enum" || !lua_isinteger(state, 2)) return 0;
    const auto names = read<ArrayHeader>(value.address + 0x40);
    if (!names || names->count < 0 || names->count > 4096 ||
        names->capacity < names->count || !names->data) return 0;
    struct EnumEntry { NameId name; std::int64_t value; };
    for (int i = 0; i < names->count; ++i) {
        const auto entry = read<EnumEntry>(names->data + i * sizeof(EnumEntry));
        if (entry && entry->value == lua_tointeger(state, 2)) {
            push_text(state, ue.name(entry->name), "FName");
            return 1;
        }
    }
    return 0;
}

int object_fields(lua_State* state)
{
    auto& ue = engine(state);
    auto owner = object(state);
    if (!owner || !lua_isfunction(state, 2)) return 0;
    if (!ue.is_class(owner)) {
        const auto header = ue.inspect(owner);
        owner = header ? ue.capture(header->type) : ObjectRef{};
    }
    for (const auto& field : ue.fields(owner)) {
        lua_pushvalue(state, 2);
        push(state, field_type, FieldView{owner, field, context(state).frame});
        if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
            lua_pop(state, 1);
            break;
        }
        const bool done = lua_isboolean(state, -1) && lua_toboolean(state, -1);
        lua_pop(state, 1);
        if (done) break;
    }
    return 0;
}

int object_property(lua_State* state)
{
    auto& ue = engine(state);
    const auto value = object(state);
    const auto* key = lua_tostring(state, 2);
    const auto header = ue.inspect(value);
    if (!header || !key) return 0;
    const auto field = ue.property(ue.capture(header->type), key);
    if (!field) return 0;
    push_value(state, value, *field, value.address + field->offset);
    return 1;
}

struct Method { std::string_view owner; std::string_view name; };

constexpr Method allowed[] = {
    {"Widget", "IsVisible"}, {"Widget", "IsInViewport"},
    {"Widget", "HasKeyboardFocus"}, {"Widget", "HasFocusedDescendants"},
    {"Widget", "GetParent"}, {"Widget", "GetCachedGeometry"},
    {"Widget", "GetVisibility"}, {"Widget", "GetRenderOpacity"},
    {"PanelWidget", "GetChildrenCount"}, {"PanelWidget", "GetChildAt"},
    {"PanelWidget", "GetChildIndex"}, {"WidgetSwitcher", "GetActiveWidget"},
    {"WidgetSwitcher", "GetActiveWidgetIndex"},
    {"TextBlock", "GetText"}, {"RichTextBlock", "GetText"}, {"SpTextBlock", "GetText"},
    {"EditableText", "GetText"}, {"EditableTextBox", "GetText"},
    {"MultiLineEditableText", "GetText"}, {"MultiLineEditableTextBox", "GetText"},
    {"WidgetBlueprintLibrary", "GetAllWidgetsOfClass"},
    {"SlateBlueprintLibrary", "GetAbsoluteSize"}, {"SlateBlueprintLibrary", "LocalToAbsolute"},
    {"Controller", "GetViewTarget"}, {"Controller", "IsLocalController"},
    {"Controller", "IsLocalPlayerController"}, {"Pawn", "IsLocallyControlled"},
    {"Pawn", "IsPlayerControlled"}, {"Pawn", "IsLocallyViewed"}, {"Actor", "HasAuthority"},
    {"SSCharacter", "IsBattleKeyPressed"}, {"SSCharacter", "GetBattleKeyHoldTime"},
    {"SSMainGameHUDWidget", "GetSkillGuideSetOfPlayer"},
    {"SSMainGameHUDWidgetRoot", "GetSkillGuideSetOfPlayer"},
    {"ManaComponent", "GetSource"}, {"ManaComponent", "GetFrameNumber"},
    {"ManaComponent", "GetStatus"}, {"ManaComponent", "IsPlaying"},
    {"ManaComponent", "IsPaused"}, {"ManaMovie", "GetUrl"}
};

bool allowed_name(std::string_view name)
{
    return std::ranges::any_of(allowed, [name](auto method) { return method.name == name; });
}

bool allowed_function(Unreal& ue, ObjectRef function)
{
    const auto name = ue.name_of(function);
    const auto owner = ue.name_of(ue.outer(function));
    constexpr std::uint32_t native_function = 0x400;
    return (read<std::uint32_t>(function.address + 0xb0).value_or(0) & native_function) &&
           std::ranges::any_of(allowed, [&](auto method) {
               return method.name == name && method.owner == owner;
           });
}

template<class T> bool write_int(lua_State* state, int arg, std::byte* dst, std::size_t size)
{
    if (size != sizeof(T) || !lua_isinteger(state, arg)) return false;
    const auto value = lua_tointeger(state, arg);
    if (!std::in_range<T>(value)) return false;
    const auto converted = static_cast<T>(value);
    std::memcpy(dst, &converted, sizeof converted);
    return true;
}

bool read_vector(lua_State* state, int index, Vector2D& value)
{
    if (!lua_istable(state, index)) return false;
    lua_pushliteral(state, "X");
    lua_rawget(state, index);
    int x{};
    value.x = lua_tonumberx(state, -1, &x);
    lua_pop(state, 1);
    lua_pushliteral(state, "Y");
    lua_rawget(state, index);
    int y{};
    value.y = lua_tonumberx(state, -1, &y);
    lua_pop(state, 1);
    return x && y && std::isfinite(value.x) && std::isfinite(value.y);
}

bool input(lua_State* state, int arg, const Field& field, std::byte* dst, unsigned depth = 0)
{
    if (depth > 2) return false;
    auto& ue = engine(state);
    switch (field.kind) {
    case Kind::Object:
    case Kind::Class: {
        const auto value = object(state, arg);
        const auto expected = ue.capture(field.child);
        if (field.size != sizeof(Address) || !value || !expected ||
            (field.kind == Kind::Class ? !ue.is_class(value) : !ue.is_a(value, expected)))
            return false;
        std::memcpy(dst, &value.address, sizeof value.address);
        return true;
    }
    case Kind::Bool:
        if (!lua_isboolean(state, arg) || field.byte_offset >= field.size) return false;
        dst[field.byte_offset] &= ~std::byte(field.mask);
        if (lua_toboolean(state, arg)) dst[field.byte_offset] |= std::byte(field.byte_mask);
        return true;
    case Kind::Byte: return write_int<std::uint8_t>(state, arg, dst, field.size);
    case Kind::Int32: return write_int<std::int32_t>(state, arg, dst, field.size);
    case Kind::Enum: {
        const auto inner = ue.describe(field.child);
        return inner && inner->size == field.size && input(state, arg, *inner, dst, depth + 1);
    }
    case Kind::Struct: {
        const auto type = ue.name_of(ue.capture(field.child));
        if (type == "Geometry" && field.size == 64) {
            const auto* geometry = as<Geometry>(state, arg, geometry_type);
            if (!geometry || geometry->frame != context(state).frame) return false;
            std::memcpy(dst, geometry->bytes.data(), geometry->bytes.size());
            return true;
        }
        if (type == "Vector2D" && field.size == sizeof(Vector2D)) {
            Vector2D value{};
            if (!read_vector(state, arg, value)) return false;
            std::memcpy(dst, &value, sizeof value);
            return true;
        }
        return false;
    }
    default: return false;
    }
}

bool result_supported(Unreal& ue, const Field& result)
{
    switch (result.kind) {
    case Kind::Object:
    case Kind::Class:
    case Kind::Bool:
    case Kind::Byte:
    case Kind::Int32:
    case Kind::Float:
    case Kind::Enum:
    case Kind::String:
        return true;
    case Kind::Text:
        return ue.can_destroy_text(result);
    case Kind::Struct: {
        const auto name = ue.name_of(ue.capture(result.child));
        return (name == "Geometry" && result.size == 64) ||
               (name == "Vector2D" && result.size == sizeof(Vector2D));
    }
    default: return false;
    }
}

struct ResultStorage {
    Unreal& engine;
    Field field{};
    Address data{};
    bool active{};

    ~ResultStorage()
    {
        if (!active) return;
        if (field.kind == Kind::Text) engine.destroy_text(field, data);
        if (field.kind == Kind::String || field.kind == Kind::Array) {
            const auto result = read<ArrayHeader>(data);
            if (result) engine.release(result->data);
        }
    }
};

bool widget_array(lua_State* state, const Field& field, Address data, int table)
{
    auto& ue = engine(state);
    const auto result = read<ArrayHeader>(data);
    if (!result || result->count < 0 || result->capacity < result->count ||
        static_cast<std::size_t>(result->count) > max_array ||
        (result->count && !result->data)) return false;
    const auto inner = ue.describe(field.child);
    if (!inner || inner->kind != Kind::Object || inner->size != sizeof(Address)) return false;
    const auto user_widget = ue.find_class("UserWidget");
    const auto requested = object(state, 4);
    if (!user_widget || !requested) return false;
    lua_Integer count = 0;
    for (int i = 0; i < result->count; ++i) {
        const auto address = read<Address>(result->data + i * sizeof(Address));
        const auto value = address ? ue.capture(*address) : ObjectRef{};
        if (ue.is_a(value, user_widget) && ue.is_a(value, requested)) {
            push_object(state, value);
            lua_rawseti(state, table, ++count);
        }
    }
    return true;
}

int invoke(lua_State* state, ObjectRef target, ObjectRef function)
{
    auto& ue = engine(state);
    if (!target || !function || !allowed_function(ue, function)) return -1;
    const auto member = ue.name_of(function);
    const bool widget_query = member == "GetAllWidgetsOfClass";
    const auto size = read<std::uint16_t>(function.address + 0xb6);
    const auto count = read<std::uint8_t>(function.address + 0xb4);
    if (!size || *size > 256 || !count || !ue.readable()) return -1;
    auto fields = ue.fields(function);
    std::erase_if(fields, [](const Field& field) { return !(field.flags & 0x80); });
    if (fields.size() != *count) return -1;
    std::ranges::sort(fields, {}, &Field::offset);
    alignas(16) std::array<std::byte, 256> params{};
    const auto base = reinterpret_cast<Address>(params.data());
    ResultStorage result{ue};
    ResultStorage output{ue};
    int arg = 2, output_index = 0;
    for (const auto& field : fields) {
        if (field.count != 1 || field.offset + field.size > *size) return -1;
        if (field.flags & 0x400) {
            if (result.data || !result_supported(ue, field) ||
                field.offset != read<std::uint16_t>(function.address + 0xb8).value_or(0xffff))
                return -1;
            result.field = field;
            result.data = base + field.offset;
        } else if ((field.flags & 0x100) && !(field.flags & 0x2)) {
            if (!widget_query || field.kind != Kind::Array || field.size != sizeof(ArrayHeader) ||
                output.data || !lua_istable(state, arg)) return -1;
            output.field = field;
            output.data = base + field.offset;
            output_index = arg++;
        } else {
            if (widget_query && arg == 2 && !ue.world_context(object(state, arg))) return -1;
            if (!input(state, arg++, field, params.data() + field.offset)) return -1;
        }
    }
    if (arg - 1 != lua_gettop(state) || (widget_query && (!output_index || result.data)))
        return -1;
    const bool invoked = ue.call(target, function, params.data());
    result.active = result.data != 0;
    output.active = output.data != 0;
    if (!invoked) return -1;
    if (output_index && !widget_array(state, output.field, output.data, output_index)) return -1;
    if (!result.data) return 0;
    if (result.field.kind == Kind::Struct) {
        if (result.field.size == sizeof(Vector2D)) {
            const auto vector = read<Vector2D>(result.data);
            if (!vector || !std::isfinite(vector->x) || !std::isfinite(vector->y)) return 0;
            lua_createtable(state, 0, 2);
            lua_pushnumber(state, vector->x);
            lua_setfield(state, -2, "X");
            lua_pushnumber(state, vector->y);
            lua_setfield(state, -2, "Y");
        } else {
            Geometry geometry{};
            geometry.frame = context(state).frame;
            if (!guarded_copy(geometry.bytes.data(), result.data, geometry.bytes.size())) return 0;
            push(state, geometry_type, geometry);
        }
    } else {
        push_value(state, target, result.field, result.data);
    }
    return 1;
}

int invoke_reflected(lua_State* state)
{
    const auto* function = as<ObjectRef>(state, lua_upvalueindex(1), object_type);
    const auto results = function ? invoke(state, object(state), *function) : -1;
    return results >= 0 ? results : luaL_error(state, "Unreal reader query unavailable");
}

int object_index(lua_State* state)
{
    const auto* key = lua_tostring(state, 2);
    if (!key) return 0;
    const std::string_view name(key);
    constexpr luaL_Reg methods[] = {
        {"IsValid", object_valid}, {"GetFullName", object_full_name}, {"GetFName", object_fname},
        {"GetName", object_name}, {"GetClass", object_class}, {"GetOuter", object_outer},
        {"GetSuperStruct", object_super}, {"IsA", object_is_a}, {"type", object_kind},
        {"GetAddress", object_address}, {"ForEachProperty", object_fields},
        {"GetPropertyValue", object_property}, {"GetNameByValue", enum_name}
    };
    for (const auto& method : methods) {
        if (name == method.name) {
            lua_pushcfunction(state, method.func);
            return 1;
        }
    }
    auto& ue = engine(state);
    const auto value = object(state);
    const auto header = ue.inspect(value);
    if (!header) return 0;
    const auto type = ue.capture(header->type);
    if (!type) return 0;
    if (allowed_name(name)) {
        const auto function = ue.function(type, name);
        if (function && allowed_function(ue, function)) {
            push(state, object_type, function);
            lua_pushcclosure(state, invoke_reflected, 1);
            return 1;
        }
    }
    return object_property(state);
}

void push_list(lua_State* state, const std::vector<ObjectRef>& values)
{
    lua_createtable(state, static_cast<int>(values.size()), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        push_object(state, values[i]);
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
}

int find_all(lua_State* state)
{
    const auto* name = luaL_checkstring(state, 1);
    push_list(state, engine(state).objects(name, {}, 0, 0, 0x10, false));
    return 1;
}

int find_first(lua_State* state)
{
    const std::string_view name(luaL_checkstring(state, 1));
    auto& ue = engine(state);
    if (name == "GameInstance") {
        push_object(state, ue.game_instance());
    } else {
        const auto values = ue.objects(name, {}, 1, 0, 0x10, false);
        push_object(state, values.empty() ? ObjectRef{} : values.front());
    }
    return 1;
}

int find_objects(lua_State* state)
{
    const auto limit = luaL_checkinteger(state, 1);
    const auto* type = luaL_checkstring(state, 2);
    const auto* name = luaL_optstring(state, 3, "");
    const auto required = luaL_optinteger(state, 4, 0);
    const auto excluded = luaL_optinteger(state, 5, 0);
    if (limit < 0 || limit > 32768 || !std::in_range<std::uint32_t>(required) ||
        !std::in_range<std::uint32_t>(excluded)) return luaL_error(state, "Invalid object query");
    push_list(state, engine(state).objects(type, name, static_cast<std::size_t>(limit),
        static_cast<std::uint32_t>(required), static_cast<std::uint32_t>(excluded),
        lua_toboolean(state, 6) != 0));
    return 1;
}

int static_find(lua_State* state)
{
    const auto* path = luaL_checkstring(state, 1);
    push_object(state, engine(state).find(path));
    return 1;
}

void method_table(lua_State* state, const char* name, const luaL_Reg* methods)
{
    luaL_newmetatable(state, name);
    luaL_setfuncs(state, methods, 0);
    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
    lua_pop(state, 1);
}

void indexed_type(lua_State* state, const char* name, lua_CFunction index, lua_CFunction equal = nullptr)
{
    luaL_newmetatable(state, name);
    lua_pushcfunction(state, index);
    lua_setfield(state, -2, "__index");
    if (equal) {
        lua_pushcfunction(state, equal);
        lua_setfield(state, -2, "__eq");
    }
    lua_pop(state, 1);
}
}

void install_unreal(lua_State* state, LuaUnreal& native)
{
    lua_pushlightuserdata(state, &native);
    lua_rawsetp(state, LUA_REGISTRYINDEX, &unreal_key);
    indexed_type(state, object_type, object_index, object_equal);
    indexed_type(state, array_type, array_index);
    indexed_type(state, struct_type, struct_index);
    const luaL_Reg texts[] = {{"ToString", text_value}, {"type", text_kind},
        {"__tostring", text_value}, {nullptr, nullptr}};
    method_table(state, text_type, texts);
    const luaL_Reg geometries[] = {{"type", geometry_kind}, {nullptr, nullptr}};
    method_table(state, geometry_type, geometries);
    const luaL_Reg fields[] = {{"GetFName", field_name}, {"GetClass", field_class},
        {"GetOffset_Internal", field_offset}, {"IsA", field_is_a}, {"GetEnum", field_enum},
        {nullptr, nullptr}};
    method_table(state, field_type, fields);
    const luaL_Reg field_classes[] = {{"GetFName", field_kind_name}, {nullptr, nullptr}};
    method_table(state, field_class_type, field_classes);
    const luaL_Reg globals[] = {
        {"FindAllOf", find_all}, {"FindFirstOf", find_first}, {"FindObjects", find_objects},
        {"StaticFindObject", static_find}, {nullptr, nullptr}
    };
    lua_pushglobaltable(state);
    luaL_setfuncs(state, globals, 0);
    lua_pop(state, 1);
    lua_newtable(state);
    for (auto* name : {"Int8Property", "Int16Property", "IntProperty", "Int64Property",
         "ByteProperty", "FloatProperty", "DoubleProperty", "BoolProperty", "NameProperty",
         "StrProperty", "TextProperty", "EnumProperty", "ObjectProperty", "StructProperty",
         "ArrayProperty"}) {
        lua_pushstring(state, name);
        lua_setfield(state, -2, name);
    }
    lua_setglobal(state, "PropertyTypes");
}

}

