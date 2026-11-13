#pragma once

#include "executable.h"
#include "unreal.h"

namespace sparking {
constexpr Address tick_rva = 0x4a7d280;
constexpr Address load_map_rva = 0x50344a0;

bool bound_image(Address base);
EngineApi engine_api(Address base);
}

