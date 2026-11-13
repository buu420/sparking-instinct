#pragma once

#include <filesystem>

struct lua_State;

namespace sparking::speech {

// Starts the worker and returns before Prism loads. The window starts unfocused.
// Pass an absolute DLL path and, optionally, a dedicated Prism diagnostic log.
bool start(const std::filesystem::path& dll, const std::filesystem::path& log = {}) noexcept;
void set_foreground(bool foreground) noexcept;

// Call only during normal teardown, outside DllMain and outside the game tick.
void shutdown() noexcept;

}

// Register under the existing Lua module name, speech_bridge.
extern "C" int luaopen_sparking_prism(lua_State* state);
