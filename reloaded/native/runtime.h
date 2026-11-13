#pragma once

#include "lua_unreal.h"
#include "unreal.h"

#include <chrono>
#include <filesystem>
#include <functional>

namespace sparking {

class Runtime {
public:
    using Report = std::function<void(std::string_view)>;

    Runtime(Unreal& engine, std::filesystem::path scripts,
            std::filesystem::path game_bin, Report report);
    ~Runtime();

    bool start();
    void frame(bool foreground);
    void map_start();
    void map_end();
    void foreground_lost();

private:
    using Clock = std::chrono::steady_clock;

    static Runtime& from(lua_State* state);
    static int log(lua_State* state);
    static int wall_clock(lua_State* state);
    static int find_module(lua_State* state);
    static int add_timer(lua_State* state);
    static int add_key(lua_State* state);
    static int has_key(lua_State* state);
    static int add_map_pre(lua_State* state);
    static int add_map_post(lua_State* state);
    bool read_script(const std::filesystem::path& file, const char* chunk_name);
    bool run(int arguments, int results, std::string_view label);
    bool callback(int reference, std::string_view label, bool stop_on_true = false);
    void reset_screen();
    void sample_keys(bool dispatch);

    Unreal& engine_;
    LuaUnreal native_{};
    std::filesystem::path scripts_;
    std::filesystem::path game_bin_;
    Report report_;
    lua_State* state_{};
    Clock::time_point origin_{Clock::now()};
    Clock::time_point due_{};
    std::chrono::milliseconds interval_{};
    int timer_{-2};
    int map_pre_{-2};
    int map_post_{-2};
    int screen_reset_{-2};
    unsigned map_depth_{};
    bool foreground_{};
    std::array<int, 7> keys_{};
    std::array<bool, 7> down_{};
};

}

