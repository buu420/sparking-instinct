# Build Sparking Instinct 0.1.2

Build on Windows x64 with CMake 3.23 or later, the Visual Studio 2022 C++ tools, and a .NET SDK that can target .NET 9. The installed mod uses the same native reader, game startup DLL, loader, and speech bridge as the verified Reloaded II preview. The public release changes the packaging and installation path.

Open PowerShell in this repository and set the paths for the x64 Microsoft.NETCore.App.Host.win-x64 9.0.15 NuGet package and the official Prism DLL. CMake downloads the pinned Lua and MinHook source archives. The Prism release and hashes are in the lock file. [Prism dependency lock](../reloaded/speech/prism-lock.json)

```powershell
cmake -S reloaded -B build/native -G 'Visual Studio 17 2022' -A x64 "-DSPARKING_NETHOST_ROOT=$hostPack" "-DSPARKING_PRISM_TEST_DLL=$prismDll"
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure

./reloaded/autostart/Build-LocalLoader.ps1 -BuildDirectory build/loader-work -OutputDirectory build/loader -SourceArchive build/Reloaded-LocalLoader-Source.zip
./audio_bridge/Build.ps1 -LuaSource build/native/_deps/sparking_lua_source-src/src
```

Keep the dependency notices with the managed loader. The tested loader uses Reloaded.Memory 9.4.3. Its corresponding source is the commit recorded in the NuGet package. [Reloaded.Memory source](https://github.com/Reloaded-Project/Reloaded.Memory/tree/1197add61ed5f93215df7cd4821d5d6df037ff3c)

Use `reloaded/package/Build-Package.ps1` with the x64 native DLL, startup DLL, Prism DLL, audio bridge, game-local loader directory, loader source ZIP, .NET host notices, and Reloaded.Memory source ZIP. The audio bridge and Prism checks in the builder pin the release inputs. Review and update the lock only when publishing a separately validated build. Build the manager variant with `release/Build-Amethyst.ps1` and pass `release/amethyst-game.json` as the game definition. Run `amm-author package validate` on the finished Amethyst ZIP.

`reloaded/package/ReaderPlan.json` records the hashes and changes that setup applies to the reader. `release/Build-ReaderPlan.py` takes the exact upstream archive, a local reference ZIP containing the tested reader and narration, and an output path. It emits references for unchanged and reused source lines. The setup file accepts an optional local copy of the same verified upstream archive. [Pinned AccessForge source](https://github.com/AccessForge/SparkingZeroAccess/tree/527125583d5d7ef36890c1c006ed187923e7e923)
