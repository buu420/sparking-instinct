#include "runtime.h"
#include "../lua/owned_files.h"
#include "../speech/prism_speech.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fstream>

namespace sparking {
namespace {
constexpr auto no_reference = LUA_NOREF;
constexpr std::size_t max_script = 1024 * 1024;

bool module_name(std::string_view name)
{
    if (name.empty() || name.size() > 96) return false;
    for (const char ch : name) {
        if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') &&
            (ch < '0' || ch > '9') && ch != '_') return false;
    }
    return true;
}

int audio_unavailable(lua_State* state)
{
    return luaL_error(state, "The audio description bridge could not be loaded");
}

void preload(lua_State* state, const char* name, lua_CFunction function)
{
    luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(state, function);
    lua_setfield(state, -2, name);
    lua_pop(state, 1);
}

void set_callback(lua_State* state, int argument, int& reference)
{
    luaL_checktype(state, argument, LUA_TFUNCTION);
    luaL_unref(state, LUA_REGISTRYINDEX, reference);
    lua_pushvalue(state, argument);
    reference = luaL_ref(state, LUA_REGISTRYINDEX);
}
}

Runtime::Runtime(Unreal& engine, std::filesystem::path scripts,
                 std::filesystem::path game_bin, Report report)
    : engine_(engine), native_{&engine, 0}, scripts_(std::move(scripts)),
      game_bin_(std::move(game_bin)), report_(std::move(report))
{
    keys_.fill(no_reference);
}

Runtime::~Runtime()
{
    if (state_) lua_close(state_);
}

Runtime& Runtime::from(lua_State* state)
{
    return **static_cast<Runtime**>(lua_getextraspace(state));
}

int Runtime::log(lua_State* state)
{
    std::string message;
    for (int i = 1, count = lua_gettop(state); i <= count; ++i) {
        std::size_t length{};
        const auto* value = luaL_tolstring(state, i, &length);
        if (i > 1) message += ' ';
        message.append(value, (std::min)(length, std::size_t{4096}));
        lua_pop(state, 1);
        if (message.size() >= 4096) break;
    }
    from(state).report_(message);
    return 0;
}

int Runtime::wall_clock(lua_State* state)
{
    const auto elapsed = Clock::now() - from(state).origin_;
    lua_pushnumber(state, std::chrono::duration<double>(elapsed).count());
    return 1;
}

bool Runtime::read_script(const std::filesystem::path& file, const char* chunk_name)
{
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto end = input.tellg();
    if (!input || end <= 0 || static_cast<std::uintmax_t>(end) > max_script) {
        lua_pushfstring(state_, "Reader module unavailable: %s", chunk_name);
        return false;
    }
    std::string content(static_cast<std::size_t>(end), '\0');
    input.seekg(0);
    if (!input.read(content.data(), static_cast<std::streamsize>(content.size()))) {
        lua_pushfstring(state_, "Reader module could not be read: %s", chunk_name);
        return false;
    }
    return luaL_loadbufferx(state_, content.data(), content.size(), chunk_name, "t") == LUA_OK;
}

int Runtime::find_module(lua_State* state)
{
    std::size_t size{};
    const auto* module = luaL_checklstring(state, 1, &size);
    if (!module_name({module, size})) {
        lua_pushliteral(state, "Invalid reader module name");
        return 1;
    }
    auto& self = from(state);
    const std::string file = std::string(module, size) + ".lua";
    self.read_script(self.scripts_ / file, file.c_str());
    return 1;
}

int Runtime::add_timer(lua_State* state)
{
    auto& self = from(state);
    const auto milliseconds = luaL_checkinteger(state, 1);
    if (milliseconds < 1 || milliseconds > 60000 || self.timer_ != no_reference)
        return luaL_error(state, "Invalid reader timer registration");
    set_callback(state, 2, self.timer_);
    self.interval_ = std::chrono::milliseconds(milliseconds);
    return 0;
}

int Runtime::add_key(lua_State* state)
{
    auto& self = from(state);
    const auto key = luaL_checkinteger(state, 1);
    if (key < VK_F3 || key > VK_F9) return luaL_error(state, "Invalid reader hotkey");
    set_callback(state, 2, self.keys_[static_cast<std::size_t>(key - VK_F3)]);
    return 0;
}

int Runtime::has_key(lua_State* state)
{
    const auto key = luaL_checkinteger(state, 1);
    lua_pushboolean(state, key >= VK_F3 && key <= VK_F9 &&
        from(state).keys_[static_cast<std::size_t>(key - VK_F3)] != no_reference);
    return 1;
}

int Runtime::add_map_pre(lua_State* state)
{
    set_callback(state, 1, from(state).map_pre_);
    return 0;
}

int Runtime::add_map_post(lua_State* state)
{
    set_callback(state, 1, from(state).map_post_);
    return 0;
}

bool Runtime::run(int arguments, int results, std::string_view label)
{
    if (lua_pcall(state_, arguments, results, 0) == LUA_OK) return true;
    const auto* error = lua_tostring(state_, -1);
    report_(std::string(label) + ": " + (error ? error : "Lua error"));
    lua_pop(state_, 1);
    return false;
}

bool Runtime::start()
{
    state_ = luaL_newstate();
    if (!state_) return false;
    *static_cast<Runtime**>(lua_getextraspace(state_)) = this;
    luaL_openlibs(state_);
    install_unreal(state_, native_);
    if (!InstallOwnedFiles(state_, game_bin_))
        report_("The AE_debug directory is unavailable.");

    const luaL_Reg globals[] = {
        {"print", log}, {"LoopInGameThreadWithDelay", add_timer}, {"RegisterKeyBind", add_key},
        {"IsKeyBindRegistered", has_key}, {"RegisterLoadMapPreHook", add_map_pre},
        {"RegisterLoadMapPostHook", add_map_post}, {nullptr, nullptr}
    };
    lua_pushglobaltable(state_);
    luaL_setfuncs(state_, globals, 0);
    lua_pop(state_, 1);
    lua_pushboolean(state_, true);
    lua_setglobal(state_, "EngineTickAvailable");
    lua_getglobal(state_, "os");
    lua_pushcfunction(state_, wall_clock);
    lua_setfield(state_, -2, "clock");
    lua_pop(state_, 1);
    lua_newtable(state_);
    for (int key = VK_F3; key <= VK_F9; ++key) {
        const auto name = "F" + std::to_string(key - VK_F1 + 1);
        lua_pushinteger(state_, key);
        lua_setfield(state_, -2, name.c_str());
    }
    lua_setglobal(state_, "Key");

    lua_getglobal(state_, "package");
    lua_getfield(state_, -1, "searchers");
    lua_newtable(state_);
    lua_rawgeti(state_, -2, 1);
    lua_rawseti(state_, -2, 1);
    lua_pushcfunction(state_, find_module);
    lua_rawseti(state_, -2, 2);
    lua_setfield(state_, -3, "searchers");
    lua_pop(state_, 2);

    preload(state_, "speech_bridge", luaopen_sparking_prism);
    const auto audio = LoadLibraryExW((scripts_ / L"audio_bridge.dll").c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto audio_open = audio ? GetProcAddress(audio, "luaopen_audio_bridge") : nullptr;
    preload(state_, "audio_bridge", audio_open ? reinterpret_cast<lua_CFunction>(audio_open)
                                              : audio_unavailable);

    if (!read_script(scripts_ / L"main.lua", "main.lua")) {
        const auto* error = lua_tostring(state_, -1);
        report_(error ? error : "Unable to open main.lua");
        return false;
    }
    if (!run(0, 0, "Reader startup")) return false;

    lua_getglobal(state_, "require");
    lua_pushliteral(state_, "screen_reader");
    if (run(1, 1, "Screen reader reset lookup")) {
        if (lua_istable(state_, -1)) {
            lua_getfield(state_, -1, "Reset");
            if (lua_isfunction(state_, -1)) screen_reset_ = luaL_ref(state_, LUA_REGISTRYINDEX);
            else lua_pop(state_, 1);
        }
        lua_pop(state_, 1);
    }
    return timer_ != no_reference && map_pre_ != no_reference && map_post_ != no_reference;
}

bool Runtime::callback(int reference, std::string_view label, bool stop_on_true)
{
    if (reference == no_reference) return false;
    lua_rawgeti(state_, LUA_REGISTRYINDEX, reference);
    if (!run(0, stop_on_true ? 1 : 0, label)) return false;
    if (!stop_on_true) return false;
    const bool done = lua_isboolean(state_, -1) && lua_toboolean(state_, -1);
    lua_pop(state_, 1);
    return done;
}

void Runtime::sample_keys(bool dispatch)
{
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        const auto held = (GetAsyncKeyState(VK_F3 + static_cast<int>(i)) & 0x8000) != 0;
        if (dispatch && held && !down_[i]) callback(keys_[i], "Reader hotkey");
        down_[i] = held;
    }
}

void Runtime::reset_screen()
{
    callback(screen_reset_, "Screen reader reset");
}

void Runtime::foreground_lost()
{
    if (!foreground_) return;
    foreground_ = false;
    speech::set_foreground(false);
    reset_screen();
}

void Runtime::frame(bool foreground)
{
    ++native_.frame;
    if (!foreground) {
        foreground_lost();
        return;
    }
    if (!foreground_) {
        foreground_ = true;
        speech::set_foreground(true);
        sample_keys(false);
    }
    if (map_depth_ || !engine_.readable()) return;

    sample_keys(true);
    const auto now = Clock::now();
    if (timer_ == no_reference || now < due_) return;
    due_ = now + interval_;
    if (callback(timer_, "Reader polling", true)) {
        luaL_unref(state_, LUA_REGISTRYINDEX, timer_);
        timer_ = no_reference;
    }
}

void Runtime::map_start()
{
    ++native_.frame;
    if (map_depth_++ == 0) callback(map_pre_, "Before map load");
}

void Runtime::map_end()
{
    if (!map_depth_ || --map_depth_) return;
    ++native_.frame;
    engine_.clear_layouts();
    callback(map_post_, "After map load");
}

}

