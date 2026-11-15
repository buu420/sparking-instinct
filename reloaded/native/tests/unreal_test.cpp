#include "unreal.h"
#include "game_reader.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>

namespace {
using namespace sparking;

template<class T> Address address(T& object)
{
    return reinterpret_cast<Address>(&object);
}

template<class T, class Buffer> void set(Buffer& bytes, std::size_t offset, T value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct TestSlot {
    Address object{};
    std::uint32_t flags{};
    std::int32_t cluster{};
    std::int32_t serial{};
    std::int32_t unused{};
};
static_assert(sizeof(TestSlot) == 24);

using ObjectBytes = std::array<std::byte, 0x300>;
using PropertyBytes = std::array<std::byte, 0x90>;
std::array<TestSlot, 18> slots{};
std::array<Address, 1> chunks{address(slots)};
std::array<std::byte, 0xc0> object_array{};
std::array<std::byte, 0x80> name_pool{};
std::array<std::byte, 0x800> names{};
std::array<ObjectBytes, 18> records{};
Address object_class{};
std::uint8_t name_ready = 1;
std::uint8_t collecting{};
std::uint8_t saving{};
int freed{};
int destroyed{};
int calls{};
bool query_invalid{};
std::array<std::byte, 0x1100> game_engine{};
std::array<Address, 38> text_vtable{};
const std::wstring current_text = L"\u00a1Listo! \u30d9\u30b8\u30fc\u30bf";
ArrayHeader native_text{reinterpret_cast<Address>(current_text.c_str()),
                        static_cast<int>(current_text.size() + 1),
                        static_cast<int>(current_text.size() + 1)};

std::int32_t assign_serial(Address, std::int32_t index)
{
    return slots[index].serial = 1000 + index;
}

Address find_object(Address type, Address outer, const wchar_t* name, bool)
{
    if (type == address(records[1]) && outer == Address(-1) && std::wstring_view(name) == L"Fixture")
        return address(records[4]);
    if (std::wstring_view(name) == L"/Engine/Transient.Fixture_6")
        return address(records[6]);
    if (std::wstring_view(name) == L"/Script/UMG.Default__WidgetBlueprintLibrary")
        return address(records[10]);
    if (std::wstring_view(name) == L"/Game/TextFixture.TextFixture")
        return address(records[14]);
    if (std::wstring_view(name) == L"TextBlock") return address(records[5]);
    if (std::wstring_view(name) == L"UserWidget") return address(records[8]);
    if (std::wstring_view(name) == L"GameEngine") return address(records[16]);
    if (std::wstring_view(name) == L"GameInstance") return address(records[17]);
    return 0;
}

void find_objects(Address type, ArrayHeader* output, bool derived,
                  std::uint32_t excluded, std::uint32_t)
{
    check(type == address(records[4]), "resolved the wrong class");
    check(derived && excluded == 0x10, "class hash filter did not match the reader contract");
    auto* data = new Address[2]{address(records[6]), address(records[7])};
    *output = {reinterpret_cast<Address>(data), 2, 2};
}

void release(Address data)
{
    ++freed;
    delete[] reinterpret_cast<Address*>(data);
}

Address text_string(Address text)
{
    return read<Address>(text).value_or(0);
}

void destroy_text(Address, Address storage)
{
    check(read<Address>(storage).value_or(0) == address(native_text), "FText result destroyed too early");
    ++destroyed;
}

void process(Address target, Address function, void* parameters)
{
    ++calls;
    const auto base = reinterpret_cast<Address>(parameters);
    if (function == address(records[13])) {
        check(target == address(records[14]), "text getter received wrong owner");
        *reinterpret_cast<Address*>(parameters) = address(native_text);
    } else if (function == address(records[11])) {
        check(target == address(records[10]) &&
            read<Address>(base) == address(records[6]) &&
            read<Address>(base + 24) == address(records[8]) &&
            read<std::uint8_t>(base + 32) == 1, "UMG call has incorrect parameter layout");
        auto* data = new Address[2]{address(records[15]), address(records[14])};
        *reinterpret_cast<ArrayHeader*>(base + 8) = {reinterpret_cast<Address>(data),
                                                     query_invalid ? -1 : 2, 2};
    } else if (function == address(records[7])) {
        check(target == address(records[15]), "viewport getter received wrong widget");
        *static_cast<std::uint8_t*>(parameters) = 1;
    } else check(false, "unexpected reflected method");
}

NameId name_at(std::uint32_t offset, std::string_view value)
{
    const auto header = static_cast<std::uint16_t>(value.size() << 6);
    set(names, offset * 2, header);
    std::memcpy(names.data() + offset * 2 + 2, value.data(), value.size());
    return {offset, 0};
}

void record(int index, int type, NameId name, int outer = -1)
{
    ObjectHeader value{1, 0, index, address(records[type]), name,
                       outer < 0 ? 0 : address(records[outer])};
    set(records[index], 0, value);
    slots[index].object = address(records[index]);
}

void property(PropertyBytes& buffer, Address type, NameId name, std::int32_t offset,
              std::int32_t bytes, std::uint64_t flags, Address child = 0)
{
    set(buffer, 8, type);
    set(buffer, 0x28, name);
    set(buffer, 0x38, std::int32_t{1});
    set(buffer, 0x3c, bytes);
    set(buffer, 0x40, flags);
    set(buffer, 0x4c, offset);
    set(buffer, 0x78, child);
}

void function(int index, int owner, NameId name, Address first, int count, int size, int ret)
{
    record(index, 12, name, owner);
    set(records[index], 0x50, first);
    set(records[index], 0x58, static_cast<std::int32_t>(size));
    set(records[index], 0xb0, std::uint32_t{0x400});
    set(records[index], 0xb4, static_cast<std::uint8_t>(count));
    set(records[index], 0xb6, static_cast<std::uint16_t>(size));
    set(records[index], 0xb8, static_cast<std::uint16_t>(ret));
    set(records[index], 0xd8, reinterpret_cast<Address>(process));
    set(records[owner], 0x48, address(records[index]));
}

}

int main()
{
    using namespace sparking;
    set(object_array, 0x10, address(chunks));
    set(object_array, 0x20, std::int32_t{65536});
    set(object_array, 0x24, std::int32_t{static_cast<int>(slots.size())});
    set(object_array, 0x28, std::int32_t{1});
    set(object_array, 0x2c, std::int32_t{1});
    set(name_pool, 0x0c, std::uint32_t{static_cast<unsigned>(names.size())});
    set(name_pool, 0x10, address(names));
    const auto class_name = name_at(0, "Class");
    const auto object_name = name_at(10, "Object");
    const auto fixture_name = name_at(20, "Fixture");
    const auto transient_name = name_at(30, "/Engine/Transient");
    const auto child_name = name_at(50, "Child");
    const auto cost_name = name_at(60, "Cost");
    const auto property_kind = name_at(70, "IntProperty");
    record(1, 1, class_name);
    record(2, 1, object_name);
    object_class = address(records[2]);
    record(3, 2, transient_name);
    record(4, 1, fixture_name);
    set(records[4], 0x40, address(records[2]));
    record(6, 4, {fixture_name.index, 7}, 3);
    record(7, 4, child_name, 6);
    set(records[7], 8, std::uint32_t{0x10});
    std::array<std::byte, 0x40> field_class{};
    set(field_class, 0, property_kind);
    PropertyBytes cost{};
    set(cost, 8, address(field_class));
    set(cost, 0x28, cost_name);
    set(cost, 0x38, std::int32_t{1});
    set(cost, 0x3c, std::int32_t{4});
    set(cost, 0x4c, std::int32_t{0x90});
    set(records[4], 0x50, address(cost));
    set(records[4], 0x58, std::int32_t{0x100});

    std::array<std::byte, 0x150> episode_selection{};
    const NameId episode{301, 2};
    set(episode_selection, 0x10c, episode);
    set(episode_selection, 0x13c, episode);
    auto selection_context = address(episode_selection);

    EngineApi api{};
    api.object_array = address(object_array);
    api.name_pool = address(name_pool);
    api.name_ready = address(name_ready);
    api.object_class_slot = address(object_class);
    api.gc_active = address(collecting);
    api.saving_package = address(saving);
    api.find = find_object;
    api.objects = find_objects;
    api.serial = assign_serial;
    api.free = release;
    api.process_event = process;
    api.code_begin = reinterpret_cast<Address>(process);
    api.code_end = api.code_begin + 1;
    api.text_string = text_string;
    api.destroy_text = destroy_text;
    api.text_vtable = address(text_vtable);
    api.episode_selection_slot = address(selection_context);
    Unreal engine(api);
    check(engine.episode_cursors_match(episode), "same live episode heading rejected");
    check(!engine.episode_cursors_match({301, 0}), "mismatched episode name instance accepted");
    set(episode_selection, 0x13c, NameId{302, 0});
    check(!engine.episode_cursors_match(episode), "ordinary chart chapter accepted for another RT episode");
    selection_context = 0;
    check(!engine.episode_cursors_match(episode), "chapter accepted without a native selection context");
    check(engine.readable(), "initial valid engine layout rejected");
    check(engine.name({fixture_name.index, 7}) == "Fixture_6", "FName number is off by one");
    auto object = engine.capture(address(records[6]));
    check(engine.live(object) && object.serial == 1006, "weak identity not registered");
    check(engine.weak(6, 1006) == object, "weak reference did not resolve");
    check(engine.path_of(engine.capture(address(records[7]))) ==
              "/Engine/Transient.Fixture_6:Child", "incorrect Unreal path separators");
    check(engine.find("/Engine/Transient.Fixture_6") == object, "full-path lookup failed");
    auto type = engine.find_class("Fixture");
    check(engine.is_class(type) && engine.is_a(object, type), "class or inheritance lookup failed");
    auto field = engine.property(type, "Cost");
    check(field && field->kind == Kind::Int32 && field->offset == 0x90,
          "FField property layout mismatch");
    check(engine.objects("Fixture", {}, 0, 0, 0x10, false).size() == 1 && freed == 1,
          "class result filtering or engine array release failed");
    slots[6].serial = 2006;
    check(!engine.live(object) && !engine.weak(6, 1006), "reused address accepted stale serial");
    object = engine.capture(address(records[6]));
    check(object.serial == 2006, "current serial was not captured");
    slots[6].flags = 0x10000000;
    check(!engine.live(object) && !engine.capture(address(records[6])),
          "unreachable object retained");
    collecting = 1;
    check(!engine.readable(), "reflection permitted during garbage collection");
    collecting = 0;
    saving = 1;
    check(!engine.readable(), "reflection permitted during package save");
    saving = 0;
    slots[6].flags = 0;

    record(5, 1, name_at(88, "TextBlock"));
    record(8, 1, name_at(99, "UserWidget"));
    record(9, 1, name_at(112, "WidgetBlueprintLibrary"));
    record(10, 9, name_at(130, "Default__WidgetBlueprintLibrary"));
    record(12, 1, name_at(155, "Function"));
    record(14, 5, name_at(166, "TextFixture"));
    record(15, 8, name_at(180, "WidgetFixture"));
    for (const int index : {5, 8, 9, 12}) set(records[index], 0x40, address(records[2]));
    set(game_engine, 0x1058, address(records[6]));
    engine.set_engine(address(game_engine));

    std::array<std::array<std::byte, 0x40>, 5> kinds{};
    set(kinds[0], 0, name_at(194, "TextProperty"));
    set(kinds[1], 0, name_at(207, "ObjectProperty"));
    set(kinds[2], 0, name_at(221, "ArrayProperty"));
    set(kinds[3], 0, name_at(234, "ClassProperty"));
    set(kinds[4], 0, name_at(247, "BoolProperty"));
    PropertyBytes text{}, context_field{}, output{}, widget_class{}, top{}, inner{};
    const auto result_name = name_at(260, "ReturnValue");
    text_vtable[0x128 / 8] = reinterpret_cast<Address>(destroy_text);
    property(text, address(kinds[0]), result_name, 0, 24, 0x580);
    set(text, 0, address(text_vtable));
    function(13, 5, name_at(273, "GetText"), address(text), 1, 24, 0);

    property(context_field, address(kinds[1]), name_at(282, "WorldContextObject"),
             0, 8, 0x80, address(records[2]));
    property(inner, address(kinds[1]), name_at(298, "FoundWidgets"), 0, 8, 0, address(records[8]));
    property(output, address(kinds[2]), name_at(298, "FoundWidgets"), 8, 16, 0x180, address(inner));
    property(widget_class, address(kinds[3]), name_at(312, "WidgetClass"),
             24, 8, 0x80, address(records[1]));
    property(top, address(kinds[4]), name_at(325, "TopLevelOnly"), 32, 1, 0x80);
    set(top, 0x7a, std::uint8_t{1});
    set(top, 0x7b, std::uint8_t{0xff});
    set(top, 0x20, address(widget_class));
    set(widget_class, 0x20, address(output));
    set(output, 0x20, address(context_field));
    function(11, 9, name_at(338, "GetAllWidgetsOfClass"), address(top), 4, 40, 0xffff);
    PropertyBytes display_array{};
    property(display_array, address(kinds[2]), name_at(360, "Children"), 0x90, 16, 0, address(inner));
    std::array<Address, 1> children{address(records[15])};
    set(records[15], 0x90, ArrayHeader{address(children), 1, 1});
    set(records[8], 0x50, address(display_array));
    set(records[8], 0x58, std::int32_t{0x100});

    record(16, 1, name_at(370, "GameEngine"));
    record(17, 1, name_at(385, "GameInstance"));
    set(records[16], 0x40, address(records[2]));
    set(records[17], 0x40, address(records[2]));
    set(records[4], 0x40, address(records[17]));
    set(game_engine, 0, ObjectHeader{1, 0, 0, address(records[16]), fixture_name, 0});
    slots[0].object = address(game_engine);
    PropertyBytes visibility{}, opacity{}, attached_result{};
    property(visibility, address(field_class), name_at(401, "Visibility"), 0xa0, 4, 0);
    set(visibility, 0x20, address(display_array));
    std::array<std::byte, 0x40> float_kind{};
    set(float_kind, 0, name_at(416, "FloatProperty"));
    property(opacity, address(float_kind), name_at(433, "RenderOpacity"), 0xa4, 4, 0);
    set(opacity, 0x20, address(visibility));
    set(records[8], 0x50, address(opacity));
    set(records[15], 0xa4, 1.0f);
    record(3, 1, name_at(449, "Widget"));
    set(records[3], 0x40, address(records[2]));
    set(records[8], 0x40, address(records[3]));
    property(attached_result, address(kinds[4]), result_name, 0, 1, 0x580);
    set(attached_result, 0x7a, std::uint8_t{1});
    set(attached_result, 0x7b, std::uint8_t{0xff});
    function(7, 3, name_at(460, "IsInViewport"), address(attached_result), 1, 1, 0);
    engine.clear_layouts();

    const auto game_ref = engine.capture(address(records[6]));
    check(engine.world_context(game_ref) && engine.game_instance() == game_ref, "world-context identity missing");
    GameReader reader(engine);
    const auto roots = reader.viewport_roots();
    check(roots.size() == 1 && roots.front().address == address(records[15]), "detached or unrelated widget leaked");
    check(freed == 2, "validated UMG call did not free exactly once");
    const auto ui_children = reader.objects(roots.front(), "Children", 3);
    check(ui_children.size() == 1 && ui_children.front() == roots.front(), "native object array mismatch");
    set(records[15], 0xa0, std::int32_t{2});
    check(reader.viewport_roots().empty() && freed == 3, "hidden widget exposed");
    set(records[15], 0xa0, std::int32_t{0});
    query_invalid = true;
    check(reader.viewport_roots().empty() && freed == 4, "failed query did not free its engine array");
    const auto shown = engine.capture(address(records[14]));
    const auto spoken = reader.call_text(shown, "GetText");
    check(spoken == utf8(current_text) && destroyed == 1, "rendered FText was not copied and released");
    const auto before = calls;
    check(reader.call_text(shown, "SetText").empty() && calls == before, "unsupported call was dispatched");
    std::cout << "UE5.1 name, object lifetime, class, path, property and GC checks passed\n";
}

