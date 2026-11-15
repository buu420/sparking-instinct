#include "profile.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace sparking {
namespace {
constexpr Address process_event_rva = 0x2e82e90;
constexpr Address find_rva = 0x2eaa760;
constexpr Address get_objects_rva = 0x2eb3980;
constexpr Address weak_serial_rva = 0x2e95690;
constexpr Address text_string_rva = 0x2c3c040;
constexpr Address text_key_rva = 0x2c3d590;
constexpr Address voice_component_rva = 0x2485480;
constexpr Address destroy_text_rva = 0x2e99710;
constexpr Address free_rva = 0x2c07d10;

template<std::size_t N>
bool bytes_match(Address base, Address rva, const std::uint8_t (&expected)[N])
{
    std::array<std::uint8_t, N> actual{};
    return guarded_copy(actual.data(), base + rva, actual.size()) &&
           std::equal(actual.begin(), actual.end(), std::begin(expected));
}
}

bool bound_image(Address base)
{
    const auto dos = read<IMAGE_DOS_HEADER>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) return false;
    const auto pe = read<IMAGE_NT_HEADERS64>(base + dos->e_lfanew);
    if (!pe || pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        pe->OptionalHeader.SizeOfImage != 0x957f000) return false;

    return bytes_match(base, tick_rva,
                       {0x48,0x8b,0xc4,0x44,0x88,0x40,0x18,0x53,0x57,0x41,0x54,0x41,0x56,0x48,0x81,0xec}) &&
           bytes_match(base, load_map_rva,
                       {0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}) &&
           bytes_match(base, process_event_rva,
                       {0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec}) &&
           bytes_match(base, find_rva,
                       {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20}) &&
           bytes_match(base, get_objects_rva,
                       {0x48,0x83,0xec,0x48,0x48,0x8d,0x44,0x24,0x58,0x48,0x89,0x54,0x24,0x58}) &&
           bytes_match(base, weak_serial_rva,
                       {0x40,0x57,0x48,0x83,0xec,0x20,0x4c,0x8b,0xc9,0x3b,0x51,0x24}) &&
           bytes_match(base, text_string_rva,
                       {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xa2,0x9a,0x00,0x00}) &&
           bytes_match(base, destroy_text_rva,
                       {0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20}) &&
           bytes_match(base, free_rva,
                       {0x48,0x85,0xc9,0x74,0x2e,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9});
}

EngineApi engine_api(Address base)
{
    EngineApi api{};
    api.object_array = base + 0x88191a0;
    api.name_pool = base + 0x8779ac0;
    api.name_ready = base + 0x877983d;
    api.object_class_slot = base + 0x87f50f8;
    api.gc_active = base + 0x88162ab;
    api.saving_package = base + 0x8818cd8;
    api.text_vtable = base + 0x6e2b5e0;
    api.code_begin = base + 0x1000;
    api.code_end = base + 0x6234997;
    // The chart and RT overview use different keys in this build's selection context.
    api.episode_selection_slot = base + 0x870d9e0;
    api.find = reinterpret_cast<decltype(api.find)>(base + find_rva);
    api.objects = reinterpret_cast<decltype(api.objects)>(base + get_objects_rva);
    api.serial = reinterpret_cast<decltype(api.serial)>(base + weak_serial_rva);
    api.process_event = reinterpret_cast<decltype(api.process_event)>(base + process_event_rva);
    api.text_string = reinterpret_cast<decltype(api.text_string)>(base + text_string_rva);
    // Optional voice inspection is bound independently so caption speech
    // still works if either native helper cannot be confirmed.
    if (bytes_match(base, text_key_rva,
                    {0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x8b,0xd9,0x48,0x8b,0x0a,0x48,0x8b,0x01,0xff}))
        api.text_key = reinterpret_cast<decltype(api.text_key)>(base + text_key_rva);
    if (bytes_match(base, voice_component_rva,
                    {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0x41,0x38,0x33,0xff,0x48}))
        api.voice_component = reinterpret_cast<decltype(api.voice_component)>(base + voice_component_rva);
    api.destroy_text = reinterpret_cast<decltype(api.destroy_text)>(base + destroy_text_rva);
    api.free = reinterpret_cast<decltype(api.free)>(base + free_rva);
    return api;
}

}

