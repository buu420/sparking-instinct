Sparking Instinct is a screen reader mod for **DRAGON BALL: Sparking! ZERO**. This beta includes the native Reloaded II reader, Prism, and Buu420's audio descriptions for the opening and post-tutorial movie.

Version 0.2.5 reads character selection from the game's roster cursor and painted character portraits. Single Battle, DP Battle, and single-character tournaments use the same roster reader, including after Random picks a fighter.

This beta captures visible menu changes, selections, and reader speech automatically. F4 is optional. The reader keeps recent diagnostics across restarts.

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**.

- **Amethyst**. Select **Sparking Instinct (Beta)** from Buu420. Close the game before installing, updating, or uninstalling. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Windows ZIP**. Close the game and extract the ZIP. Run `Install.cmd` to install or `Uninstall.cmd` to remove the mod. If you have a standalone preview, use its uninstaller first.

Start your screen reader and launch Sparking! ZERO from Steam or its desktop shortcut.

F6 reads the screen. F7 and F8 review items. F9 switches between reading all visible dialogue and skipping lines whose matching game voice is playing. Text-only story lines and lines whose voice cannot be confirmed still read automatically. F6 reviews visible dialogue in either mode. Visible battle tutorial instructions repeat every 20 seconds.

## What to test

Try Single Battle, DP Battle, and a one-character tournament. Move through a few rows, then use Random from several positions. Check that the landed fighter reads after the image updates. Test a transformation and return to the roster if you can.

For a report, send `SparkingZeroAccess.Reloaded.log` from `SparkingZeroAccess-Reloaded\Logs` inside the game folder. If the report happened before the file last rotated, include `SparkingZeroAccess.Reloaded.previous-1.log` from the same folder. Include the game mode and the action you took. For a crash, include the matching Unreal crash report or Windows Application Error.

Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired this work. [Earlier project](https://github.com/AccessForge/SparkingZeroAccess)
