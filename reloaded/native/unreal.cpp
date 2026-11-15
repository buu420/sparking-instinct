#include "unreal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace sparking {
namespace {
constexpr std::int32_t objects_per_chunk = 65536;
constexpr std::uint32_t unavailable_slot = 0xbc200000;
constexpr std::uint32_t unavailable_object = 0x6001b400;
constexpr std::uint32_t class_default_object = 0x10;
constexpr std::size_t max_classes = 4096;
constexpr std::size_t max_fields = 4096;

template<class Function, class Result, class... Args>
bool guarded_result(Function function, Result& result, Args... args) noexcept
{
    if (!function) return false;
    __try {
        result = function(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template<class Function, class... Args>
bool guarded_call(Function function, Args... args) noexcept
{
    if (!function) return false;
    __try {
        function(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct FieldHeader {
    Address vtable{};
    Address type{};
    Address owner{};
    std::uint64_t owner_flags{};
    Address next{};
    NameId name{};
    std::uint32_t object_flags{};
    std::uint32_t padding{};
    std::int32_t dimension{};
    std::int32_t size{};
    std::uint64_t flags{};
    std::uint16_t replication{};
    std::uint16_t replication_condition{};
    std::int32_t offset{};
};
static_assert(offsetof(FieldHeader, name) == 0x28);
static_assert(offsetof(FieldHeader, offset) == 0x4c);
static_assert(sizeof(ObjectHeader) == 0x28);
static_assert(sizeof(ArrayHeader) == 0x10);

constexpr std::pair<std::string_view, Kind> field_types[] = {
    {"ObjectProperty", Kind::Object}, {"ObjectPtrProperty", Kind::Object},
    {"ClassProperty", Kind::Class}, {"ClassPtrProperty", Kind::Class},
    {"WeakObjectProperty", Kind::WeakObject}, {"SoftObjectProperty", Kind::SoftObject},
    {"SoftClassProperty", Kind::SoftObject}, {"BoolProperty", Kind::Bool},
    {"ByteProperty", Kind::Byte}, {"Int8Property", Kind::Int8},
    {"Int16Property", Kind::Int16}, {"IntProperty", Kind::Int32},
    {"Int64Property", Kind::Int64}, {"UInt16Property", Kind::UInt16},
    {"UInt32Property", Kind::UInt32}, {"UInt64Property", Kind::UInt64},
    {"FloatProperty", Kind::Float}, {"DoubleProperty", Kind::Double},
    {"EnumProperty", Kind::Enum}, {"NameProperty", Kind::Name},
    {"StrProperty", Kind::String}, {"TextProperty", Kind::Text},
    {"ArrayProperty", Kind::Array}, {"StructProperty", Kind::Struct}
};
}

bool guarded_copy(void* destination, Address source, std::size_t bytes) noexcept
{
    if (!destination || source < 0x10000 || bytes > 8 * 1024 * 1024 ||
        source + bytes < source) return false;
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(source), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string utf8(std::wstring_view value)
{
    if (value.empty() || value.size() > INT_MAX) return {};
    const auto count = static_cast<int>(value.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), count, nullptr, 0, nullptr, nullptr);
    if (!bytes) return {};
    std::string result(bytes, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), count,
                            result.data(), bytes, nullptr, nullptr) != bytes) return {};
    return result;
}

std::wstring wide(std::string_view value)
{
    if (value.empty() || value.size() > INT_MAX) return {};
    const auto count = static_cast<int>(value.size());
    const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        value.data(), count, nullptr, 0);
    if (!units) return {};
    std::wstring result(units, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), count,
                            result.data(), units) != units) return {};
    return result;
}

std::optional<Unreal::ObjectArray> Unreal::array() const
{
    const auto value = read<ObjectArray>(api_.object_array + 0x10);
    if (!value || !value->chunks || value->count <= 0 ||
        value->count > value->capacity || value->capacity > 16'777'216 ||
        value->chunk_count <= 0 || value->chunk_count > value->chunk_capacity ||
        value->chunk_capacity > 256 ||
        value->count > value->chunk_count * objects_per_chunk) return {};
    return value;
}

std::optional<Unreal::Slot> Unreal::slot(std::int32_t index) const
{
    static_assert(sizeof(Slot) == 0x18);
    const auto objects = array();
    if (!objects || index < 0 || index >= objects->count) return {};
    const auto chunk = read<Address>(objects->chunks + (index / objects_per_chunk) * sizeof(Address));
    if (!chunk || !*chunk) return {};
    return read<Slot>(*chunk + (index % objects_per_chunk) * sizeof(Slot));
}

bool Unreal::episode_cursors_match(NameId painted_episode) const
{
    if (!api_.episode_selection_slot || !painted_episode.index) return false;
    const auto selection = read<Address>(api_.episode_selection_slot).value_or(0);
    if (!selection) return false;
    // Native chart and RT caption refresh use these separate FName keys.
    return read<NameId>(selection + 0x10c) == painted_episode &&
           read<NameId>(selection + 0x13c) == painted_episode;
}

bool Unreal::readable() const
{
    return !faulted_ && read<std::uint8_t>(api_.name_ready).value_or(0) == 1 &&
           read<std::uint8_t>(api_.gc_active).value_or(1) == 0 &&
           read<std::uint8_t>(api_.saving_package).value_or(1) == 0 &&
           array().has_value() && meta_class() != 0;
}

void Unreal::clear_layouts()
{
    layouts_.clear();
    classes_.clear();
    paths_.clear();
}

ObjectRef Unreal::capture(Address address)
{
    if (!address || (address & 7)) return {};
    const auto header = read<ObjectHeader>(address);
    if (!header || !header->type || (header->flags & unavailable_object)) return {};
    const auto item = slot(header->index);
    if (!item || item->object != address || (item->flags & unavailable_slot)) return {};
    auto serial = item->serial;
    if (!serial && !guarded_result(api_.serial, serial, api_.object_array, header->index)) {
        faulted_ = true;
        return {};
    }
    return serial > 0 ? weak(header->index, serial) : ObjectRef{};
}

ObjectRef Unreal::weak(std::int32_t index, std::int32_t serial) const
{
    if (serial <= 0) return {};
    const auto item = slot(index);
    if (!item || item->serial != serial || !item->object ||
        (item->flags & unavailable_slot)) return {};
    const auto header = read<ObjectHeader>(item->object);
    if (!header || header->index != index || !header->type ||
        (header->flags & unavailable_object)) return {};
    return {item->object, index, serial};
}

bool Unreal::live(ObjectRef object) const
{
    return object && weak(object.index, object.serial) == object;
}

std::optional<ObjectHeader> Unreal::inspect(ObjectRef object) const
{
    return live(object) ? read<ObjectHeader>(object.address) : std::nullopt;
}

std::string Unreal::name(NameId id)
{
    auto found = names_.find(id.index);
    if (found == names_.end()) {
        const auto block_index = id.index >> 16;
        const auto offset = (id.index & 0xffff) * 2u;
        const auto current = read<std::uint32_t>(api_.name_pool + 8);
        const auto cursor = read<std::uint32_t>(api_.name_pool + 0x0c);
        constexpr std::uint32_t block_size = 0x20000;
        if (!current || !cursor || block_index > *current || block_index >= 8192) return {};
        const auto limit = block_index == *current ? *cursor : block_size;
        if (limit > block_size || offset + 2 > limit) return {};
        const auto block = read<Address>(api_.name_pool + 0x10 + block_index * 8);
        if (!block || !*block) return {};
        const auto entry = read<std::uint16_t>(*block + offset);
        if (!entry) return {};
        const auto length = *entry >> 6;
        const bool is_wide = (*entry & 1) != 0;
        if (!length || offset + 2 + length * (is_wide ? 2u : 1u) > limit) return {};

        std::string value;
        if (is_wide) {
            std::wstring characters(length, L'\0');
            if (!guarded_copy(characters.data(), *block + offset + 2, length * sizeof(wchar_t)))
                return {};
            value = utf8(characters);
        } else {
            value.resize(length);
            if (!guarded_copy(value.data(), *block + offset + 2, length)) return {};
            if (std::ranges::any_of(value, [](unsigned char ch) { return ch >= 0x80; })) {
                std::wstring characters;
                for (const unsigned char ch : value) characters.push_back(ch);
                value = utf8(characters);
            }
        }
        if (value.empty() || value.find('\0') != value.npos) return {};
        if (names_.size() >= 65536) names_.clear();
        found = names_.emplace(id.index, std::move(value)).first;
    }
    return id.number ? found->second + "_" + std::to_string(id.number - 1) : found->second;
}

std::string Unreal::name_of(ObjectRef object)
{
    const auto header = inspect(object);
    return header ? name(header->name) : std::string{};
}

std::string Unreal::class_name(ObjectRef object)
{
    const auto header = inspect(object);
    return header ? name_of(capture(header->type)) : std::string{};
}

std::string Unreal::path_of(ObjectRef object)
{
    std::vector<std::string> names;
    for (auto current = object; current && names.size() < 40;) {
        const auto header = inspect(current);
        if (!header) return {};
        auto segment = name(header->name);
        if (segment.empty()) return {};
        names.push_back(std::move(segment));
        if (!header->outer) {
            std::string path = names.back();
            for (auto i = names.size() - 1; i > 0; --i) {
                path += names.size() - i == 2 && path.starts_with('/') ? ':' : '.';
                path += names[i - 1];
            }
            return path;
        }
        current = capture(header->outer);
    }
    return {};
}

ObjectRef Unreal::outer(ObjectRef object)
{
    const auto header = inspect(object);
    return header ? capture(header->outer) : ObjectRef{};
}

Address Unreal::meta_class() const
{
    const auto type = read<Address>(api_.object_class_slot).value_or(0);
    return type ? read<Address>(type + 0x10).value_or(0) : 0;
}

bool Unreal::derives(Address from, Address target) const
{
    if (!target) return false;
    for (unsigned i = 0; from && i < 96; ++i) {
        if (from == target) return true;
        const auto parent = read<Address>(from + 0x40).value_or(0);
        if (parent == from) break;
        from = parent;
    }
    return false;
}

bool Unreal::is_a(ObjectRef object, ObjectRef type) const
{
    const auto header = inspect(object);
    return header && is_class(type) && derives(header->type, type.address);
}

bool Unreal::is_class(ObjectRef object) const
{
    const auto header = inspect(object);
    return header && derives(header->type, meta_class());
}

ObjectRef Unreal::super(ObjectRef type)
{
    if (!is_class(type)) return {};
    return capture(read<Address>(type.address + 0x40).value_or(0));
}

ObjectRef Unreal::game_instance()
{
    const auto owner = capture(engine_);
    const auto type = find_class("GameEngine");
    if (!is_a(owner, type)) return {};
    const auto instance = capture(read<Address>(engine_ + 0x1058).value_or(0));
    return is_a(instance, find_class("GameInstance")) ? instance : ObjectRef{};
}

bool Unreal::world_context(ObjectRef object) const
{
    return object && live(object) && object.address ==
           read<Address>(engine_ + 0x1058).value_or(0);
}

ObjectRef Unreal::lookup(Address type, Address outer, std::string_view value, bool exact)
{
    if (!readable() || value.empty() || value.size() > 2048 || value.find('\0') != value.npos)
        return {};
    const auto query = wide(value);
    if (query.empty()) return {};
    Address result{};
    if (!guarded_result(api_.find, result, type, outer, query.c_str(), exact)) {
        faulted_ = true;
        return {};
    }
    return capture(result);
}

ObjectRef Unreal::find(std::string_view path)
{
    const auto key = std::string(path);
    const auto cached = paths_.find(key);
    if (cached != paths_.end() && live(cached->second)) return cached->second;
    const auto found = lookup(0, 0, path);
    if (found && paths_.size() < max_classes) paths_.insert_or_assign(key, found);
    return found;
}

ObjectRef Unreal::find_class(std::string_view short_name)
{
    const auto key = std::string(short_name);
    const auto cached = classes_.find(key);
    if (cached != classes_.end() && live(cached->second)) return cached->second;
    const auto meta = capture(meta_class());
    if (short_name == "Class") return meta;
    if (!meta || short_name.find_first_of("/.:") != short_name.npos) return {};
    const auto found = lookup(meta.address, Address(-1), short_name);
    if (!is_class(found) || name_of(found) != short_name) return {};
    if (classes_.size() < max_classes) classes_.insert_or_assign(key, found);
    return found;
}

std::vector<ObjectRef> Unreal::objects(std::string_view type, std::string_view short_name,
                                       std::size_t limit, std::uint32_t required,
                                       std::uint32_t excluded, bool exact)
{
    std::vector<ObjectRef> result;
    if (!readable()) return result;
    const auto class_object = find_class(type);
    if (!class_object) return result;
    const auto include = [&](ObjectRef ref) {
        const auto item = inspect(ref);
        if (item && (item->flags & required) == required && !(item->flags & excluded) &&
            (short_name.empty() || name_of(ref) == short_name) &&
            (exact ? item->type == class_object.address : is_a(ref, class_object)))
            result.push_back(ref);
    };
    if (limit == 1 && !short_name.empty()) {
        include(lookup(class_object.address, Address(-1), short_name, exact));
        return result;
    }

    ArrayHeader output{};
    const auto success = guarded_call(api_.objects, class_object.address, &output, !exact,
                                      excluded | class_default_object, unavailable_slot);
    const auto cleanup = [this](ArrayHeader* list) { release(list->data); };
    const std::unique_ptr<ArrayHeader, decltype(cleanup)> storage(&output, cleanup);
    if (!success) {
        faulted_ = true;
        return result;
    }
    if (output.count < 0 || output.count > 32768 || output.capacity < output.count ||
        (output.count && !output.data)) return result;
    result.reserve(std::min<std::size_t>(output.count, limit ? limit : 32768));
    for (int i = 0; i < output.count; ++i) {
        include(capture(read<Address>(output.data + i * sizeof(Address)).value_or(0)));
        if (limit && result.size() >= limit) break;
    }
    return result;
}

Kind Unreal::kind(Address field_class)
{
    const auto found = kinds_.find(field_class);
    if (found != kinds_.end()) return found->second;
    const auto id = read<NameId>(field_class);
    if (!id) return Kind::Unknown;
    const auto type = name(*id);
    const auto entry = std::ranges::find_if(field_types, [&](const auto& row) {
        return row.first == type;
    });
    const auto result = entry == std::end(field_types) ? Kind::Unknown : entry->second;
    if (result != Kind::Unknown) kinds_.emplace(field_class, result);
    return result;
}

std::optional<Field> Unreal::describe(Address address)
{
    const auto field = read<FieldHeader>(address);
    if (!field || !field->type || field->offset < 0 || field->offset > 0x100000 ||
        field->size <= 0 || field->size > 0x100000 ||
        field->dimension <= 0 || field->dimension > 4096) return {};
    Field result{address, 0, kind(field->type), static_cast<std::uint32_t>(field->offset),
                 static_cast<std::uint32_t>(field->size),
                 static_cast<std::uint32_t>(field->dimension), field->flags, 0, 0, 0, field->name};
    switch (result.kind) {
    case Kind::Array:
    case Kind::Struct:
    case Kind::Enum:
    case Kind::Object:
    case Kind::Class:
    case Kind::WeakObject:
    case Kind::SoftObject:
        result.child = read<Address>(address + 0x78).value_or(0);
        break;
    case Kind::Bool:
        result.byte_offset = read<std::uint8_t>(address + 0x79).value_or(0xff);
        result.byte_mask = read<std::uint8_t>(address + 0x7a).value_or(0);
        result.mask = read<std::uint8_t>(address + 0x7b).value_or(0);
        if (result.byte_offset >= result.size || !result.byte_mask ||
            (result.byte_mask & result.mask) != result.byte_mask) return {};
        break;
    default:
        break;
    }
    return result;
}

std::vector<Field> Unreal::fields(ObjectRef owner)
{
    std::vector<Field> result;
    if (!live(owner)) return result;
    auto current = read<Address>(owner.address + 0x50).value_or(0);
    const auto size = read<std::int32_t>(owner.address + 0x58).value_or(0);
    if (size < 0 || size > 0x100000) return result;
    for (std::size_t i = 0; current && i < max_fields; ++i) {
        const auto field = describe(current);
        if (field && field->offset + std::uint64_t(field->size) * field->count <=
                         static_cast<std::uint32_t>(size)) result.push_back(*field);
        const auto next = read<Address>(current + 0x20).value_or(0);
        if (next == current) break;
        current = next;
    }
    return result;
}

Unreal::Layout& Unreal::layout(ObjectRef owner)
{
    if (layouts_.size() >= max_classes && !layouts_.contains(owner.address)) layouts_.clear();
    auto& result = layouts_[owner.address];
    if (result.owner != owner) {
        result = {};
        result.owner = owner;
    }
    return result;
}

std::optional<Field> Unreal::property(ObjectRef type, std::string_view member)
{
    const std::string key(member);
    for (unsigned depth = 0; type && depth < 96; ++depth) {
        auto& table = layout(type);
        if (!table.properties_ready) {
            for (const auto& field : fields(type)) {
                auto field_name = name(field.name);
                if (!field_name.empty()) table.properties.try_emplace(std::move(field_name), field);
            }
            table.properties_ready = true;
        }
        const auto found = table.properties.find(key);
        if (found != table.properties.end()) return found->second;
        type = capture(read<Address>(type.address + 0x40).value_or(0));
    }
    return {};
}

ObjectRef Unreal::function(ObjectRef type, std::string_view member)
{
    const std::string key(member);
    for (unsigned depth = 0; type && depth < 96; ++depth) {
        auto& table = layout(type);
        if (!table.functions_ready) {
            auto child = read<Address>(type.address + 0x48).value_or(0);
            for (unsigned i = 0; child && i < max_fields; ++i) {
                auto value = capture(child);
                if (value && class_name(value) == "Function") {
                    auto child_name = name_of(value);
                    if (!child_name.empty()) table.functions.try_emplace(std::move(child_name), value);
                }
                const auto next = read<Address>(child + 0x28).value_or(0);
                if (next == child) break;
                child = next;
            }
            table.functions_ready = true;
        }
        const auto found = table.functions.find(key);
        if (found != table.functions.end() && live(found->second)) return found->second;
        type = super(type);
    }
    return {};
}

bool Unreal::is_code(Address address) const
{
    return address >= api_.code_begin && address < api_.code_end;
}

bool Unreal::call(ObjectRef target, ObjectRef function, void* params)
{
    if (!readable() || !live(target) || !live(function) ||
        !is_code(read<Address>(function.address + 0xd8).value_or(0)) ||
        !is_a(target, outer(function))) return false;
    if (guarded_call(api_.process_event, target.address, function.address, params)) return true;
    faulted_ = true;
    return false;
}

bool Unreal::can_destroy_text(const Field& field) const
{
    const auto vtable = read<Address>(field.address).value_or(0);
    return field.kind == Kind::Text && field.count == 1 && field.size == 0x18 &&
           vtable && vtable == api_.text_vtable &&
           read<Address>(vtable + 0x128).value_or(0) ==
               reinterpret_cast<Address>(api_.destroy_text);
}

void Unreal::destroy_text(const Field& field, Address value) noexcept
{
    if (!can_destroy_text(field) || !guarded_call(api_.destroy_text, field.address, value))
        faulted_ = true;
}

std::optional<std::string> Unreal::text(Address value)
{
    if (!read<Address>(value).value_or(0)) return std::string{};
    Address borrowed{};
    if (!guarded_result(api_.text_string, borrowed, value)) {
        faulted_ = true;
        return {};
    }
    return string(borrowed);
}

std::string Unreal::text_key(Address value)
{
    if (!readable() || !api_.text_key || !read<Address>(value).value_or(0)) return {};
    TextKey key{};
    static_assert(offsetof(TextKey, present) == 16 && sizeof(TextKey) == 24);
    TextKey* result{};
    if (!guarded_result(api_.text_key, result, &key, value) || result != &key || key.present != 1)
        return {};
    const auto cleanup = [this](TextKey* item) { release(item->value.data); };
    const std::unique_ptr<TextKey, decltype(cleanup)> storage(&key, cleanup);
    return string(reinterpret_cast<Address>(&key.value)).value_or("");
}

ObjectRef Unreal::voice_component(ObjectRef manager, std::uint32_t handle)
{
    if (!handle || !readable() || !is_a(manager, find_class("SSAtomComponentManager"))) return {};
    std::array<std::int32_t, 2> result{};
    decltype(&result) returned{};
    if (!guarded_result(api_.voice_component, returned, manager.address, &result, &handle) ||
        returned != &result) return {};
    const auto component = weak(result[0], result[1]);
    return is_a(component, find_class("AtomComponent")) ? component : ObjectRef{};
}

std::optional<std::string> Unreal::string(Address value) const
{
    const auto array = read<ArrayHeader>(value);
    if (!array || array->count < 0 || array->count > 32768 ||
        array->capacity < array->count || (array->count && !array->data)) return {};
    if (!array->count) return std::string{};
    std::wstring text(array->count, L'\0');
    if (!guarded_copy(text.data(), array->data, array->count * sizeof(wchar_t)) ||
        text.back() != L'\0') return {};
    text.pop_back();
    if (text.find(L'\0') != text.npos) return {};
    auto result = utf8(text);
    if (!text.empty() && result.empty()) return {};
    return result;
}

void Unreal::release(Address allocation) noexcept
{
    if (allocation && !guarded_call(api_.free, allocation)) faulted_ = true;
}

}

