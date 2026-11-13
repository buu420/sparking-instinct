#pragma once

#include <filesystem>

struct lua_State;

namespace sparking {

// Install the wide-path opener after luaL_openlibs and before loading the reader.
// A false return reports an unavailable diagnostics folder. The preference
// opener is still registered if the absolute game directory is usable.
bool InstallOwnedFiles(lua_State* state, const std::filesystem::path& game_bin);

}
