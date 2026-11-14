Sparking Instinct is a screen reader mod for **DRAGON BALL: Sparking! ZERO**. This beta includes the native Reloaded II reader, Prism, and Buu420's audio descriptions for the opening and post-tutorial movie.

Version 0.2.2 improves menu speech and adds a voiced-dialogue filter. The coverage checklist below shows where tester feedback is needed.

The beta supports **Windows x64, Steam build 24953175 (game version 3022.021.003.012.015)**.

- **Amethyst**. Select **Sparking Instinct (Beta)** from Buu420. Close the game before installing, updating, or uninstalling. [Buu420 catalog](https://github.com/buu420/buu-s-mods)
- **Windows ZIP**. Close the game and extract the ZIP. Run `Install.cmd` to install or `Uninstall.cmd` to remove the mod. If you have a standalone preview, use its uninstaller first.

Start your screen reader and launch Sparking! ZERO from Steam or its desktop shortcut.

F6 reads the screen. F7 and F8 review items. F9 switches between reading all visible dialogue and skipping lines whose matching game voice is playing. Text-only story lines and lines whose voice cannot be confirmed still read automatically. F6 reviews visible dialogue in either mode. Visible battle tutorial instructions repeat every 20 seconds.

## Screen coverage checklist

Each item below was reported in the previous beta. The reader changes were tested offline. Please use this list to check the screens in the game.

| Area | What to check in 0.2.2 |
| --- | --- |
| First-visit mode tutorials | Visible help and guide windows announce their text and page changes. Report the mode if its introduction is still silent. |
| Episode Battle | The character name, chapter, and selected action read on the roster. The episode map announces the currently displayed title and chapter as you move. |
| Mission 100 | The selected mission and its visible detail panel read. Check the mission list and the screen opened after confirming. |
| Online Battle | Room Search Settings reads the setting name with its current value. |
| Battle Training | After selecting a lesson with A, check the practice screen. Press Y and listen for the explanation. |
| World Tournament | Tournament selection reads the name followed by its own rules. Check that Edit reads the setting name and value. Try the one-player roster in World Tournament, Cell Games, and Other World Tournament. |
| Zeno's Orders and Whis's Stamp Book | Check the selected order or stamp and the visible mission details. |
| Shop and Customize | Check the selected item, visible price and details, and the Customize lists. |
| My Data and Theater | Check the selected record, title, movie, music track, or replay and its visible details. |
| Move list and Explanation of Controls | Move quickly through several choices. The new move name should interrupt the previous one. Its controls and details should follow. |
| Episode Battle story text | Use F9 to skip voiced dialogue. Text-only summaries between battles should keep reading. |

For a bug report, press F4 on the affected screen and send `SparkingZeroAccess.Reloaded.log` from `SparkingZeroAccess-Reloaded\Logs` inside the game folder. Include the mode, input, and what you expected to hear. Use F5 if you need to capture a sequence of announcements.

Jessica Tegner and the Sparking Zero Access contributors built the earlier reader that inspired this work. [Earlier project](https://github.com/AccessForge/SparkingZeroAccess)
