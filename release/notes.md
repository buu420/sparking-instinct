Sparking Instinct is a screen reader mod for **DRAGON BALL: Sparking! ZERO**. This beta includes the native Reloaded II reader, Prism, and Buu420's audio descriptions for the opening and post-tutorial movie.

Version 0.2.1 fixes Amethyst installs that stopped at the .NET dependency check. Setup reuses the x64 .NET 9 Desktop Runtime starting at 9.0.8. If the runtime is missing, setup downloads the official Microsoft installer and asks for Windows administrator approval.

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**.

- **Amethyst**. Select **Sparking Instinct (Beta)** from Buu420. Close the game before installing, updating, or uninstalling. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Windows ZIP**. Close the game and extract the ZIP. Run `Install.cmd` to install or `Uninstall.cmd` to remove the mod. If you have a standalone preview, use its uninstaller first.

Start your screen reader and launch Sparking! ZERO from Steam or its desktop shortcut.

F6 reads the screen. F7 and F8 review items. F9 toggles automatic subtitle speech, including text-only lines shown in subtitle windows. Visible tutorial instructions repeat every 20 seconds. In Free Training the reader announces the Reset Positions shortcut. While charging Ki, it reads the visible Blasts and their buttons once per hold. When the Ultimate Blast button appears, the reader announces its binding and the name of the move assigned to it when that name is available.

For a bug report, include the screen and input. You can find the startup and reader logs in `SparkingZeroAccess-Reloaded\Logs` inside the game folder.

Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired this work. [Earlier project](https://github.com/AccessForge/SparkingZeroAccess)
