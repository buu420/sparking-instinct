# Third-party components

- This package contains a game-local variant of Reloaded II 1.31.0, modified to use this game's own profile and log folder when the game starts normally. The GPL v3 text and complete source for the included Reloaded II assemblies are in this folder. [Upstream source and release](https://github.com/Reloaded-Project/Reloaded-II/releases/tag/1.31.0)
- Reloaded II runtime dependencies include Colorful.Console, Steam Apps Management and VDF API, DotNetCorePlugins, and Reloaded.Memory. The loader's Notices folder has their licenses. The source for the bundled Reloaded.Memory 9.4.3 build is in this folder. [Reloaded.Memory source](https://github.com/Reloaded-Project/Reloaded.Memory/tree/1197add61ed5f93215df7cd4821d5d6df037ff3c)
- The startup DLL uses Microsoft's .NET 9.0.15 native hosting library. The MIT license and Microsoft third-party notices are in DotNet. The .NET Desktop Runtime is installed separately. [Source](https://github.com/dotnet/runtime/tree/4250c8399aa851d2d6a95efbdcc5c4c12311e024/src/native/corehost/nethost)
- Prism 0.18.3 provides screen reader output. Its notices and MPL v2.0 text are in Prism. [Corresponding source and release](https://github.com/ethindp/prism/releases/tag/v0.18.3)
- MinHook 1.3.4 is used by the native runtime. Its BSD 2-Clause license is in MinHook-LICENSE.txt. [Source](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)
- Buu420 made the audio descriptions for the main opening and post-tutorial movie and contributed the clips and timings upstream in pull request #7. The cue sheet is installed beside the clips.
