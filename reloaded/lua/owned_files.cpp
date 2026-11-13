#include "owned_files.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cwchar>
#include <string_view>

namespace sparking {
namespace {

constexpr std::string_view preference = "SparkingZeroAccess-subtitles.ini";
constexpr std::string_view diagnostic_prefix = "AE_debug/";
constexpr std::array<std::string_view, 4> diagnostic_files{
    "debug_dump.txt", "chara_select.txt", "battle_gauges.txt", "battle_state.txt"
};

bool Digits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

bool ScreenCaptureName(std::string_view name)
{
    constexpr std::string_view prefix = "screen-coverage-", extension = ".log";
    if (!name.starts_with(prefix) || !name.ends_with(extension)) return false;
    name.remove_prefix(prefix.size());
    name.remove_suffix(extension.size());
    // YYYYMMDD-HHMMSS-session
    return name.size() >= 17 && name.size() <= 36 && name[8] == '-' && name[15] == '-' &&
           Digits(name.substr(0, 8)) && Digits(name.substr(9, 6)) && Digits(name.substr(16));
}

bool OwnedName(std::string_view path, std::string_view mode)
{
    if (path == preference) return mode == "r" || mode == "w";
    if (!path.starts_with(diagnostic_prefix)) return false;
    path.remove_prefix(diagnostic_prefix.size());
    if (std::find(diagnostic_files.begin(), diagnostic_files.end(), path) != diagnostic_files.end())
        return mode == "a" || mode == "w";
    return mode == "a" && ScreenCaptureName(path);
}

bool PlainDirectory(const wchar_t* path)
{
    const auto attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

int FileError(lua_State* state, const char* path, int code)
{
    errno = code;
    return luaL_fileresult(state, 0, path);
}

int CloseFile(lua_State* state)
{
    auto* stream = static_cast<luaL_Stream*>(luaL_checkudata(state, 1, LUA_FILEHANDLE));
    FILE* file = stream->f;
    stream->f = nullptr;
    return luaL_fileresult(state, std::fclose(file) == 0, nullptr);
}

int OpenDataFile(lua_State* state)
{
    std::size_t path_size{}, mode_size{};
    const char* relative = luaL_checklstring(state, 1, &path_size);
    const char* mode = luaL_optlstring(state, 2, "r", &mode_size);
    const std::string_view name(relative, path_size);
    if (!OwnedName(name, {mode, mode_size})) return FileError(state, nullptr, EINVAL);

    auto* stream = static_cast<luaL_Stream*>(lua_newuserdatauv(state, sizeof(luaL_Stream), 0));
    *stream = {nullptr, nullptr};
    luaL_setmetatable(state, LUA_FILEHANDLE);

    const auto* root = static_cast<const wchar_t*>(lua_touserdata(state, lua_upvalueindex(1)));
    const auto root_size = std::wcslen(root);
    auto* target = static_cast<wchar_t*>(
        lua_newuserdatauv(state, (root_size + path_size + 1) * sizeof(wchar_t), 0));
    std::copy_n(root, root_size, target);
    if (name.starts_with(diagnostic_prefix)) {
        constexpr std::wstring_view diagnostic_folder = L"AE_debug";
        std::copy(diagnostic_folder.begin(), diagnostic_folder.end(), target + root_size);
        target[root_size + diagnostic_folder.size()] = 0;
        if (!PlainDirectory(target)) return FileError(state, relative, EACCES);
    }
    for (std::size_t index = 0; index < path_size; ++index)
        target[root_size + index] = relative[index] == '/' ? L'\\' : relative[index];
    target[root_size + path_size] = 0;

    const auto attributes = GetFileAttributesW(target);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        return FileError(state, relative, EACCES);

    const wchar_t wide_mode[] = {static_cast<wchar_t>(mode[0]), 0};
    const int error = _wfopen_s(&stream->f, target, wide_mode);
    lua_pop(state, 1);
    if (error || !stream->f) return FileError(state, relative, error ? error : EIO);
    stream->closef = CloseFile;
    return 1;
}

}

bool InstallOwnedFiles(lua_State* state, const std::filesystem::path& game_bin)
{
    std::error_code error;
    if (!game_bin.is_absolute() || !std::filesystem::is_directory(game_bin, error)) return false;
    auto root = game_bin.lexically_normal().native();
    if (!root.ends_with(L'\\') && !root.ends_with(L'/')) root += L'\\';
    auto* storage = static_cast<wchar_t*>(
        lua_newuserdatauv(state, (root.size() + 1) * sizeof(wchar_t), 0));
    std::copy_n(root.c_str(), root.size() + 1, storage);
    lua_pushcclosure(state, OpenDataFile, 1);
    lua_setglobal(state, "SparkingOpenDataFile");

    const auto diagnostics = game_bin / L"AE_debug";
    std::filesystem::create_directory(diagnostics, error);
    return PlainDirectory(diagnostics.c_str());
}

}
