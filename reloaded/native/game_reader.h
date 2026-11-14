#pragma once

#include "unreal.h"

#include <span>

namespace sparking {

struct WidgetBounds {
    double x{}, y{}, width{}, height{};
};

class GameReader {
public:
    explicit GameReader(Unreal& engine) : engine_(engine) {}
    Unreal& engine() { return engine_; }
    void reset();

    ObjectRef object(ObjectRef owner, std::string_view path);
    std::optional<double> number(ObjectRef owner, std::string_view path);
    std::optional<std::int64_t> integer(ObjectRef owner, std::string_view path);
    std::optional<bool> boolean(ObjectRef owner, std::string_view path);
    std::string text(ObjectRef owner, std::string_view path);
    std::vector<ObjectRef> objects(ObjectRef owner, std::string_view path, std::size_t limit);
    std::optional<double> array_number(ObjectRef owner, std::string_view path,
                                       std::size_t index, std::string_view member);

    ObjectRef call_object(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument = {});
    std::optional<double> call_number(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument = {});
    std::optional<bool> call_bool(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument = {});
    std::string call_text(ObjectRef owner, std::string_view method, std::optional<std::int32_t> argument = {});
    bool visible(ObjectRef widget);
    std::optional<WidgetBounds> geometry(ObjectRef widget);
    std::vector<ObjectRef> widget_children(ObjectRef widget);
    std::vector<ObjectRef> viewport_roots();

private:
    struct Value {
        Address address{};
        Field field{};
    };

    struct Signature {
        ObjectRef function{};
        std::vector<Field> inputs;
        std::optional<Field> result;
        std::optional<Field> output;
    };

    struct Return;
    std::optional<Value> property(ObjectRef type, Address storage, std::size_t size, std::string_view path);
    std::optional<Value> property(ObjectRef owner, std::string_view path);
    std::optional<Value> array_element(Value value, std::size_t index);
    std::optional<ArrayHeader> array(Value value);
    ObjectRef object(Value value);
    std::optional<std::int64_t> integer(Value value);
    std::optional<double> number(Value value);
    std::optional<bool> boolean(Value value);
    std::string text(Value value);
    const Signature* signature(ObjectRef target, std::string_view method);
    std::optional<Return> invoke(ObjectRef target, std::string_view method,
                                 std::span<const std::byte> first = {},
                                 std::span<const std::byte> second = {});

    Unreal& engine_;
    std::unordered_map<Address, Signature> signatures_;
    std::optional<ObjectRef> widget_switcher_;
    std::optional<ObjectRef> panel_widget_;
};

}
