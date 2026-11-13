#include "owned_files.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void Require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void Script(lua_State* state, const char* code)
{
    if (luaL_dostring(state, code) != LUA_OK)
        throw std::runtime_error(lua_tostring(state, -1));
}

int main()
{
    try {
        const auto original_directory = fs::current_path();
        const auto scratch = fs::temp_directory_path() /
            (L"SparkingZeroAccess-file-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()));
        const auto game = scratch / L"Game 測試";
        const auto blocked = scratch / L"Blocked 測試";
        Require(fs::create_directories(game) && fs::create_directory(blocked), "fixture directories failed");

        const auto preference = game / L"SparkingZeroAccess-subtitles.ini";
        const auto debug = game / L"AE_debug";
        const auto log = debug / L"debug_dump.txt";
        const auto coverage = debug / L"screen-coverage-20261113-123456-1.log";
        std::ofstream(preference) << "AutomaticSubtitles=false\n";

        const std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
        Require(state != nullptr, "Lua state failed");
        luaL_openlibs(state.get());
        Require(sparking::InstallOwnedFiles(state.get(), game), "diagnostic directory unavailable");
        Require(fs::current_path() == original_directory, "process working directory changed");

        Script(state.get(), R"lua(
            local path = 'SparkingZeroAccess-subtitles.ini'
            local preference = assert(SparkingOpenDataFile(path, 'r'))
            assert(io.type(preference) == 'file')
            assert(preference:read(4097) == 'AutomaticSubtitles=false\n')
            assert(preference:close() and io.type(preference) == 'closed file')
            local output = assert(SparkingOpenDataFile(path, 'w'))
            assert(output:write('AutomaticSubtitles=true\n') and output:close())
            local diag = 'AE_debug/debug_dump.txt'
            local first = assert(SparkingOpenDataFile(diag, 'w'))
            assert(first:write('new\n') and first:close())
            local second = assert(SparkingOpenDataFile(diag, 'a'))
            assert(second:write('append\n') and second:close())
            local capture = assert(SparkingOpenDataFile('AE_debug/screen-coverage-20261113-123456-1.log', 'a'))
            assert(capture:write('visible\n') and capture:close())
            for _, rejected in ipairs({
                '../SparkingZeroAccess-subtitles.ini', '/outside', 'C:/outside', 'AE_debug/../outside',
                'AE_debug/debug_dump.txt:stream', 'AE_debug/debug_dump.txt\0suffix', 'AE_debug/NUL',
                'AE_debug/screen-coverage-nondates-123456-1.log', 'AE_debug/other-mod.log',
            }) do
                local file, err, code = SparkingOpenDataFile(rejected, 'a')
                assert(not file and err and code)
            end
            assert(not SparkingOpenDataFile(path, 'w\0append'))
            assert(not SparkingOpenDataFile(path, 'a'))
            assert(not SparkingOpenDataFile('AE_debug/debug_dump.txt', 'r'))
        )lua");

        std::string setting, first_line, second_line, caption;
        std::getline(std::ifstream(preference), setting);
        {
            std::ifstream diagnostics(log);
            std::getline(diagnostics, first_line);
            std::getline(diagnostics, second_line);
        }
        std::getline(std::ifstream(coverage), caption);
        Require(setting == "AutomaticSubtitles=true", "preference write went to the wrong directory");
        Require(first_line == "new" && second_line == "append", "diagnostic append did not preserve its first line");
        Require(caption == "visible", "coverage write did not land in AE_debug");
        std::cout << "PASS: standard Lua streams work with Unicode game paths and reject unrelated names and modes\n";

        const auto blocked_debug = blocked / L"AE_debug";
        std::ofstream(blocked_debug) << "original\n";
        Require(!sparking::InstallOwnedFiles(state.get(), blocked), "a file was accepted as the diagnostics directory");
        Script(state.get(), R"lua(
            local file, _, code = SparkingOpenDataFile('SparkingZeroAccess-subtitles.ini', 'r')
            assert(not file and code == 2)
            assert(not SparkingOpenDataFile('AE_debug/debug_dump.txt', 'w'))
            local preference = assert(SparkingOpenDataFile('SparkingZeroAccess-subtitles.ini', 'w'))
            assert(preference:write('AutomaticSubtitles=false\n') and preference:close())
        )lua");
        std::getline(std::ifstream(blocked_debug), first_line);
        Require(first_line == "original", "diagnostic path conflict overwrote another file");
        std::cout << "PASS: a diagnostic path conflict preserves the file and leaves subtitle preferences available\n";

        for (const auto& path : {preference, coverage, log, debug, game,
                                 blocked / L"SparkingZeroAccess-subtitles.ini", blocked_debug, blocked, scratch})
            Require(fs::remove(path), "fixture cleanup failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
