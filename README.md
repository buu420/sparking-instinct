# Sparking Instinct

Sparking Instinct brings screen reader speech to DRAGON BALL: Sparking! ZERO on Steam. It reads supported menus, character selection, training, controls, battle gauges, and story text. It includes timed audio description for the main opening and the post-tutorial movie.

**Jessica Tegner and the Sparking Zero Access contributors created the original Lua reader.** Buu420 ported the mod to Reloaded II and Prism and made the audio descriptions for both movies. [Sparking Zero Access](https://github.com/AccessForge/SparkingZeroAccess) · [Earlier upstream repair](https://github.com/AccessForge/SparkingZeroAccess/pull/7)

## Install

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**, and requires the x64 **.NET 9 Desktop Runtime**. You can install and play without the AccessForge app. During installation, setup automatically downloads and verifies the original reader and audio descriptions from Buu420's GitHub fork. Reloaded II loads the prepared files locally when you start the game.

- **Amethyst:** Close the game, find **Sparking Instinct (Beta)** under **DRAGON BALL: Sparking! ZERO** in the Buu420 source, and choose Install. Amethyst will ask before running the script that verifies and prepares the reader. Use Amethyst to uninstall. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Standalone:** Close the game, extract the Windows ZIP, and run `Install.cmd`. It finds the Steam install or opens a folder picker. Use `Uninstall.cmd` to remove a standalone install. [GitHub releases](https://github.com/buu420/sparking-instinct/releases)

If you installed a standalone Reloaded II preview, use its uninstaller before installing this beta. Either installer stops when an unrelated loader occupies the startup file. For the Amethyst package, remove an active UE4SS installation through the tool that installed it before installing Sparking Instinct.

Start your screen reader, then open the game normally from Steam or its desktop shortcut.

## Play

- `F6` reads the current screen.
- `F7` and `F8` review the previous and next item.
- `F9` toggles automatic subtitle speech. With it off, text-only narration in subtitle windows can also be muted. Use `F6` to review what is on screen.
- A visible tutorial instruction repeats every 20 seconds.

The Windows beta was tested with NVDA on the normal Steam launch, menus, offline training, and the controls screen. Story coverage, player-two HUD readings, controller glyphs, and movie synchronization need more live testing.

For a report, include the screen name, action, and whether `F6` still works. The startup and reader logs are in `SparkingZeroAccess-Reloaded\Logs` inside the game folder.

## Source

`reloaded/` contains the native host, Prism integration, and game-local Reloaded II build. It keeps the old internal mod identity so the tested startup, preferences, and rollback paths still work. `release/` contains the Amethyst packaging and reader preparation tools. The installer verifies SHA-256 before using the pinned reader source from Buu420's fork, and verifies the reconstructed files before the game can load them.

Reloaded II and Reloaded.Memory source and notices ship with the packages. The included Prism binary comes from its tagged upstream release. The AccessForge reader remains under its upstream terms. [Third-party notices](reloaded/package/THIRD-PARTY-NOTICES.md) · [Build instructions](release/build.md)
