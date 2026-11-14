# Sparking Instinct

Sparking Instinct brings screen reader speech to DRAGON BALL: Sparking! ZERO on Steam. It reads supported menus, character selection, training, controls, battle gauges, and story text. It includes timed audio description for the main opening and the post-tutorial movie.

Buu420 maintains Sparking Instinct and made the audio descriptions for both movies. Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired the port. [Sparking Zero Access](https://github.com/AccessForge/SparkingZeroAccess) · [Earlier upstream repair](https://github.com/AccessForge/SparkingZeroAccess/pull/7)

## Install

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**, and requires the x64 **.NET 9 Desktop Runtime**, version 9.0.8 or a later .NET 9 patch. The ZIP includes the native reader, game-local Reloaded II loader, Prism, and both audio descriptions. Amethyst setup reuses a compatible runtime or offers Microsoft's installer if one is missing.

- **Amethyst:** Close the game, find **Sparking Instinct (Beta)** under **DRAGON BALL: Sparking! ZERO** in the Buu420 source, and choose Install. Amethyst will ask before running the script that checks your game build and looks for a conflicting loader. Use Amethyst to uninstall. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Standalone:** Close the game, extract the Windows ZIP, and run `Install.cmd`. It finds the Steam install or opens a folder picker. Use `Uninstall.cmd` to remove a standalone install. [GitHub releases](https://github.com/buu420/sparking-instinct/releases)

If you installed a standalone Reloaded II preview, use its uninstaller before installing this beta. Either installer stops when an unrelated loader occupies the startup file. For the Amethyst package, remove an active UE4SS installation through the tool that installed it before installing Sparking Instinct.

Start your screen reader, then open the game normally from Steam or its desktop shortcut.

## Play

- `F6` reads the current screen.
- `F7` and `F8` review the previous and next item.
- `F9` switches between reading all visible dialogue and skipping lines whose matching game voice is playing. Text-only lines still read. `F6` can review visible dialogue in either mode.
- A visible tutorial instruction repeats every 20 seconds.

The native reader is a beta. Please include the game mode and the screen where a line is missed or unclear.

For a report, include the screen name, action, and whether `F6` still works. The startup and reader logs are in `SparkingZeroAccess-Reloaded\Logs` inside the game folder.

## Source

The C++ reader in reloaded/native reads game objects on the game thread and sends announcements to Prism. The loader starts it through the normal Steam launch. The mod keeps the existing install identifier and subtitle preference so upgrades can preserve them.

The Amethyst and Windows packages include the reader and Buu420's movie descriptions. [Third-party notices](reloaded/package/THIRD-PARTY-NOTICES.md) · [Build instructions](release/build.md)
