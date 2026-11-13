# Prism speech

SparkingZeroAccess uses the official Windows x64 Prism 0.18.3 release. Verify the archive and its `dynamic/release/bin/prism.dll` against [the lock file](prism-lock.json). Place that DLL in the mod's `Scripts` directory and include the `licenses` directory with redistribution. [Release](https://github.com/ethindp/prism/releases/tag/v0.18.3) · [Corresponding source](https://github.com/ethindp/prism/tree/v0.18.3)

The bridge uses Prism's built-in screen reader selection and Windows fallback. It follows Blind Soldier's availability notification and reselection pattern. The `stuff.zip` reference includes an older Prism binary. Keep the release DLL and its headers paired because the configuration function returns a versioned structure by value.

Set `SPARKING_PRISM_LUA_TARGET` to the host's Lua 5.4 target, add this directory with CMake, and link `sparking_prism`. Start the worker with the absolute Prism DLL path, then register `luaopen_sparking_prism` as `speech_bridge`. Pass foreground transitions to `sparking::speech::set_foreground`. If the host has an orderly teardown, call `shutdown()` outside the engine tick and DLL entry point.

`speech.say` accepts UTF-8 and preserves the existing `interrupt` argument. It returns whether the mailbox accepted the announcement. A new interrupt replaces waiting speech, and a foreground loss discards waiting lines. The worker checks foreground ownership again before delivery. `speech.is_speaking()` reports pending speech or the latest available backend reading. Prism diagnostics can be recorded in a dedicated log by passing its absolute path to `start`.

Enable `BUILD_TESTING` for the mailbox and Lua module checks. Set `SPARKING_PRISM_TEST_DLL` to the absolute path to the pinned DLL to enable the ABI test.
