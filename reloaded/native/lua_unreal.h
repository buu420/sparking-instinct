#pragma once

#include <cstdint>

struct lua_State;

namespace sparking {
class Unreal;

struct LuaUnreal {
    Unreal* engine{};
    std::uint64_t frame{};
};

void install_unreal(lua_State* state, LuaUnreal& context);
}

