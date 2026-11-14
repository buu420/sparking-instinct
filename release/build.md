# Build Sparking Instinct 0.2.1

Build on Windows x64 with CMake 3.23 or later, Visual Studio 2022 C++ tools, and a .NET SDK targeting .NET 9. Use the x64 Microsoft.NETCore.App.Host.win-x64 9.0.15 NuGet package and the official Prism DLL. CMake verifies the pinned MinHook source archive. [Prism dependency lock](../reloaded/speech/prism-lock.json)

```powershell
cmake -S reloaded -B build/native -G 'Visual Studio 17 2022' -A x64 "-DSPARKING_NETHOST_ROOT=$hostPack" "-DSPARKING_PRISM_TEST_DLL=$prismDll"
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure

./reloaded/autostart/Build-LocalLoader.ps1 -BuildDirectory build/loader-work -OutputDirectory build/loader -SourceArchive build/Reloaded-LocalLoader-Source.zip
```

Keep the dependency notices with the managed loader. The tested loader uses Reloaded.Memory 9.4.3. [Reloaded.Memory source](https://github.com/Reloaded-Project/Reloaded.Memory/tree/1197add61ed5f93215df7cd4821d5d6df037ff3c)

Use `reloaded/package/Build-Package.ps1` with the x64 native reader DLL, startup DLL, Prism DLL, game-local loader directory, loader source ZIP, .NET host notices, and Reloaded.Memory source ZIP. It bundles the 37 movie clips and cue sheet from `reloaded/narration`. Use `release/Build-Amethyst.ps1` with the resulting package directory and `release/amethyst-game.json`. Verify the finished Amethyst ZIP with `amm-author package validate`.
