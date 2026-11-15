#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sparking {

using Address = std::uintptr_t;

struct NameId {
    std::uint32_t index{};
    std::uint32_t number{};
    bool operator==(const NameId&) const = default;
};

struct ArrayHeader {
    Address data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct TextKey {
    ArrayHeader value{};
    std::uint8_t present{};
};

struct ObjectHeader {
    Address vtable{};
    std::uint32_t flags{};
    std::int32_t index{};
    Address type{};
    NameId name{};
    Address outer{};
};

struct ObjectRef {
    Address address{};
    std::int32_t index{-1};
    std::int32_t serial{};
    explicit operator bool() const { return address != 0; }
    bool operator==(const ObjectRef&) const = default;
};

enum class Kind {
    Unknown, Object, Class, WeakObject, SoftObject, Bool, Byte, Int8, Int16,
    Int32, Int64, UInt16, UInt32, UInt64, Float, Double, Enum, Name, String,
    Text, Array, Struct
};

struct Field {
    Address address{};
    Address child{};
    Kind kind{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t count{};
    std::uint64_t flags{};
    std::uint8_t byte_offset{};
    std::uint8_t byte_mask{};
    std::uint8_t mask{};
    NameId name{};
};

struct EngineApi {
    Address object_array{};
    Address name_pool{};
    Address name_ready{};
    Address object_class_slot{};
    Address gc_active{};
    Address saving_package{};
    Address text_vtable{};
    Address code_begin{};
    Address code_end{};
    Address episode_selection_slot{};
    Address (*find)(Address type, Address outer, const wchar_t* name, bool exact){};
    void (*objects)(Address type, ArrayHeader*, bool include_derived,
                    std::uint32_t excluded, std::uint32_t excluded_internal){};
    std::int32_t (*serial)(Address array, std::int32_t index){};
    void (*process_event)(Address object, Address function, void* params){};
    Address (*text_string)(Address text){};
    TextKey* (*text_key)(TextKey* result, Address text){};
    std::array<std::int32_t, 2>* (*voice_component)(Address voice_manager,
        std::array<std::int32_t, 2>* result, const std::uint32_t* handle){};
    void (*destroy_text)(Address property, Address storage){};
    void (*free)(Address allocation){};
};

bool guarded_copy(void* destination, Address source, std::size_t bytes) noexcept;

template<class T> std::optional<T> read(Address address) noexcept
{
    T value{};
    if (!guarded_copy(&value, address, sizeof value)) return std::nullopt;
    return value;
}

std::string utf8(std::wstring_view value);
std::wstring wide(std::string_view value);

class Unreal {
public:
    explicit Unreal(EngineApi api) : api_(api) {}
    bool readable() const;
    bool faulted() const { return faulted_; }
    void set_engine(Address address) { engine_ = address; }
    void clear_layouts();

    ObjectRef capture(Address address);
    ObjectRef weak(std::int32_t index, std::int32_t serial) const;
    bool live(ObjectRef object) const;
    std::optional<ObjectHeader> inspect(ObjectRef object) const;
    std::string name(NameId id);
    std::string name_of(ObjectRef object);
    std::string class_name(ObjectRef object);
    std::string path_of(ObjectRef object);
    ObjectRef outer(ObjectRef object);
    ObjectRef super(ObjectRef type);
    bool is_a(ObjectRef object, ObjectRef type) const;
    bool is_class(ObjectRef object) const;
    ObjectRef game_instance();
    ObjectRef find(std::string_view path);
    ObjectRef find_class(std::string_view name);
    std::vector<ObjectRef> objects(std::string_view type, std::string_view short_name,
                                   std::size_t limit, std::uint32_t required,
                                   std::uint32_t excluded, bool exact);

    std::optional<Field> describe(Address address);
    std::optional<Field> property(ObjectRef type, std::string_view name);
    std::vector<Field> fields(ObjectRef owner);
    ObjectRef function(ObjectRef type, std::string_view name);
    bool call(ObjectRef target, ObjectRef function, void* params);
    bool world_context(ObjectRef object) const;
    bool can_destroy_text(const Field& field) const;
    void destroy_text(const Field& field, Address value) noexcept;
    std::optional<std::string> text(Address value);
    std::string text_key(Address value);
    ObjectRef voice_component(ObjectRef manager, std::uint32_t handle);
    bool can_inspect_voice() const { return api_.text_key && api_.voice_component; }
    std::optional<std::string> string(Address value) const;
    void release(Address allocation) noexcept;
    bool is_code(Address address) const;
    bool episode_cursors_match(NameId painted_episode) const;

private:
    struct ObjectArray {
        Address chunks{};
        Address preallocated{};
        std::int32_t capacity{};
        std::int32_t count{};
        std::int32_t chunk_capacity{};
        std::int32_t chunk_count{};
    };
    struct Slot {
        Address object{};
        std::uint32_t flags{};
        std::int32_t cluster{};
        std::int32_t serial{};
        std::int32_t padding{};
    };
    struct Layout {
        ObjectRef owner{};
        bool properties_ready{};
        bool functions_ready{};
        std::unordered_map<std::string, Field> properties;
        std::unordered_map<std::string, ObjectRef> functions;
    };

    std::optional<ObjectArray> array() const;
    std::optional<Slot> slot(std::int32_t index) const;
    Address meta_class() const;
    ObjectRef lookup(Address type, Address outer, std::string_view value, bool exact = false);
    bool derives(Address from, Address target) const;
    Layout& layout(ObjectRef owner);
    Kind kind(Address field_class);

    EngineApi api_;
    Address engine_{};
    bool faulted_{};
    std::unordered_map<std::uint32_t, std::string> names_;
    std::unordered_map<Address, Kind> kinds_;
    std::unordered_map<std::string, ObjectRef> classes_;
    std::unordered_map<std::string, ObjectRef> paths_;
    std::unordered_map<Address, Layout> layouts_;
};

}

