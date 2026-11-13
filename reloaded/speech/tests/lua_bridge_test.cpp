#include "prism_speech.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdlib>
#include <iostream>

int main()
{
    if (sparking::speech::start(L"relative/prism.dll")) {
        std::cerr << "Started a worker with a relative Prism path\n";
        return EXIT_FAILURE;
    }

    auto* state = luaL_newstate();
    if (!state) {
        return EXIT_FAILURE;
    }
    luaL_openlibs(state);
    luaL_requiref(state, "speech_bridge", luaopen_sparking_prism, 0);
    lua_pop(state, 1);

    const int result = luaL_dostring(state, R"lua(
        local bridge = require('speech_bridge')
        assert(bridge == require('speech_bridge'))
        assert(bridge.say('unavailable') == false)
        assert(bridge.say('unavailable', false) == false)
        assert(bridge.is_speaking() == false)
        assert(bridge.detect() == nil)
        assert(not pcall(bridge.say, nil))
        bridge.stop()
    )lua");
    if (result != LUA_OK) {
        std::cerr << lua_tostring(state, -1) << '\n';
    }
    lua_close(state);
    sparking::speech::shutdown();
    return result == LUA_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
