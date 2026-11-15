Sparking Instinct is a screen reader mod for **DRAGON BALL: Sparking! ZERO**. This beta includes the native Reloaded II reader, Prism, and Buu420's audio descriptions for the opening and post-tutorial movie.

Version 0.2.4 fixes character selection in single-character tournament modes and the RT episode menu in Episode Battle.

- **Tournament roster.** After you open the character grid from your team slot, the reader announces the highlighted character and form. F6 can still review the visible technique text.
- **Episode Battle.** The RT menu reads the selected episode and area. It includes the displayed chapter number when it matches the selected episode. F6 reviews the episode's visible description.

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**.

- **Amethyst**. Select **Sparking Instinct (Beta)** from Buu420. Close the game before installing, updating, or uninstalling. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Windows ZIP**. Close the game and extract the ZIP. Run `Install.cmd` to install or `Uninstall.cmd` to remove the mod. If you have a standalone preview, use its uninstaller first.

Start your screen reader and launch Sparking! ZERO from Steam or its desktop shortcut.

F6 reads the screen. F7 and F8 review items. F9 switches between reading all visible dialogue and skipping lines whose matching game voice is playing. Text-only story lines and lines whose voice cannot be confirmed still read automatically. F6 reviews visible dialogue in either mode. Visible battle tutorial instructions repeat every 20 seconds.

## What to test

These changes passed offline tests. In a one-character tournament, press A on your team slot and check several fighters. In Episode Battle, open the Episode Map, press RT, and check an episode while moving within an area and after switching areas with LB or RB. Try an unlocked Sparking path if available.

For a bug report, press F4 on the affected screen and send `SparkingZeroAccess.Reloaded.log` from `SparkingZeroAccess-Reloaded\Logs` inside the game folder. Include the mode, input, and what you expected to hear. Use F5 if you need to capture a sequence of announcements.

Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired this work. [Earlier project](https://github.com/AccessForge/SparkingZeroAccess)
