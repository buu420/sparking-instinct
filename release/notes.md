Sparking Instinct is a screen reader mod for **DRAGON BALL: Sparking! ZERO**. The release includes the native Reloaded II reader, Prism, and Buu420's audio descriptions for the opening and post-tutorial movie.

Version 0.2.7 fixes the F9 dialogue filter. In filtered mode, a caption whose matching game voice is playing is skipped automatically. The reader briefly waits for a matching voice to start and reads the caption if playback fails. Text-only story lines and captions without a confirmed voice still read.

The reader captures visible menu changes, selections, and reader speech automatically. The log records subtitle filter decisions and silent roster states. F4 is optional. The reader keeps recent diagnostics across restarts.

The release supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**.

- **Amethyst**. Select **Sparking Instinct** from Buu420 on the Stable channel. Close the game before installing, updating, or uninstalling. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Windows ZIP**. Close the game and extract the ZIP. Run `Install.cmd` to install or `Uninstall.cmd` to remove the mod. If you have a standalone preview, use its uninstaller first.

Start your screen reader and launch Sparking! ZERO from Steam or its desktop shortcut.

F6 reads the screen. F7 and F8 review items. F9 switches between reading all visible dialogue and skipping lines whose matching game voice is playing. Text-only story lines and lines whose voice cannot be confirmed still read automatically. F6 reviews visible dialogue in either mode. Visible battle tutorial instructions repeat every 20 seconds.

## Report a problem

For a report, send `SparkingZeroAccess.Reloaded.log` from `SparkingZeroAccess-Reloaded\Logs` inside the game folder. If the report happened before the file last rotated, include `SparkingZeroAccess.Reloaded.previous-1.log` from the same folder. Include the game mode and the action you took. For a crash, include the matching Unreal crash report or Windows Application Error.

Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired this work. [Earlier project](https://github.com/AccessForge/SparkingZeroAccess)
