package.path = './SparkingZeroAccess/?.lua;' .. package.path

local paths, keys, jobs = {}, {}, {}
local preference = 'AutomaticSubtitles=false\n'
local shellRuns, legacyOpens = 0, 0
local realExecute, realOpen = os.execute, io.open
os.execute = function() shellRuns = shellRuns + 1 end
io.open = function() legacyOpens = legacyOpens + 1; return nil, 'unexpected legacy file access', 2 end
SparkingOpenDataFile = function(path, mode)
    local entry = {path = path, mode = mode}
    paths[#paths + 1] = entry
    return {
        read = function() return preference end,
        write = function(self, content)
            entry.content = content
            if path == 'SparkingZeroAccess-subtitles.ini' then preference = content end
            return self
        end,
        close = function() return true end,
    }
end
Key = {F3 = 114, F4 = 115, F5 = 116, F6 = 117, F7 = 118, F8 = 119, F9 = 120}
RegisterKeyBind = function(key, callback) keys[key] = callback end
package.preload.audio_bridge = function() return {} end
local pump = require('game_thread')
pump.After = function(_, _, callback) jobs[#jobs + 1] = callback end
pump.Every = function() return function() end end
local function dispatch(key)
    keys[key]()
    local pending = jobs
    jobs = {}
    for _, callback in ipairs(pending) do callback() end
end

local subtitles = require('subtitle_speech')
subtitles.Init(function() end)
assert(not subtitles.IsEnabled(), 'Reloaded did not read the installed subtitle preference')
assert(paths[1].path == 'SparkingZeroAccess-subtitles.ini' and paths[1].mode == 'r')
dispatch(Key.F9)
assert(subtitles.IsEnabled() and preference == 'AutomaticSubtitles=true\n')
assert(paths[2].path == paths[1].path and paths[2].mode == 'w')
print('PASS: Reloaded uses the installed subtitle preference for startup and F9')

local reader = require('screen_reader')
require('visible_ui').Capture = function()
    return {roots = {{path = 'screen'}}, nodes = {}, warnings = {},
        entries = {{path = 'instruction', text = 'Guard', ancestry = '/WBP_Tutorial_C'}}}
end
reader.Init(function() end, function() end)
local debugTools = require('debug_tools')
debugTools.Init(function() end)
assert(shellRuns == 0, 'Reloaded startup spawned a shell')
local ownedDebugNames = {'debug_dump.txt', 'chara_select.txt', 'battle_gauges.txt', 'battle_state.txt'}
for index, name in ipairs(ownedDebugNames) do
    local entry = paths[index + 2]
    assert(entry.path == 'AE_debug/' .. name and entry.mode == 'w')
end
dispatch(Key.F5)
reader.Poll()
assert(paths[7].path == 'AE_debug/debug_dump.txt' and paths[7].mode == 'a')
assert(paths[8].path:match('^AE_debug/screen%-coverage%-%d+%-%d+%-%d+%.log$') and paths[8].mode == 'a')
assert(paths[8].content:find('Guard', 1, true))
dispatch(Key.F5)
assert(paths[9].path == paths[7].path and paths[9].mode == 'a')
assert(legacyOpens == 0, 'Reloaded attempted a CWD-relative file operation')
print('PASS: Reloaded keeps its debug paths and F5 session without a startup shell')

os.execute, io.open, SparkingOpenDataFile = realExecute, realOpen, nil
