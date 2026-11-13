// SparkingZeroAccess game-local entry point for Reloaded II 1.31.0.
// SPDX-License-Identifier: GPL-3.0-only
namespace Reloaded.Mod.Loader;

public static class LocalEntryPoint
{
    private const string GameId = "sparkingzero-win64-shipping.exe";
    private const string ModId = "sparkingzero.accessibility.reloaded";
    private static int _attempted;
    private static Loader _loader;
    private static ReloadedMappedFile _status;

    // The host passes a length-delimited UTF-16 path, not a process-wide setting.
    public static int InitializeLocal(IntPtr argument, int argumentSize)
    {
        if (Interlocked.Exchange(ref _attempted, 1) != 0) return 1;
        if (argument == IntPtr.Zero || argumentSize <= 0 || argumentSize > 65534 || argumentSize % 2 != 0) return 2;

        string logFolder = null;
        try
        {
            var manager = Path.GetFullPath(Marshal.PtrToStringUni(argument, argumentSize / 2)!);
            var expectedExe = Path.GetFullPath(Path.Combine(manager, "..", "SparkingZERO", "Binaries", "Win64",
                "SparkingZERO-Win64-Shipping.exe"));
            if (!string.Equals(expectedExe, System.Environment.ProcessPath, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The game-local profile does not belong to this process.");

            logFolder = Path.Combine(manager, "Logs");
            var apps = Path.Combine(manager, "Apps");
            var mods = Path.Combine(manager, "Mods");
            var appPath = Path.Combine(apps, GameId, "AppConfig.json");
            var modPath = Path.Combine(mods, ModId, "ModConfig.json");
            var app = IConfig<ApplicationConfig>.FromPath(appPath);
            var mod = IConfig<ModConfig>.FromPath(modPath);
            if (app.AppId != GameId || !app.EnabledMods.SequenceEqual(new[] { ModId }) ||
                !string.Equals(ApplicationConfig.GetAbsoluteAppLocation(new(appPath, app)), expectedExe,
                    StringComparison.OrdinalIgnoreCase) ||
                mod.ModId != ModId || mod.ModNativeDll64 != "SparkingZeroAccess.Native.dll" ||
                !string.IsNullOrEmpty(mod.ModNativeDll32) || !string.IsNullOrEmpty(mod.ModDll) ||
                !string.IsNullOrEmpty(mod.ModR2RManagedDll64) || !string.IsNullOrEmpty(mod.ModR2RManagedDll32) ||
                mod.ModDependencies.Length != 0 || mod.OptionalDependencies.Length != 0)
                throw new InvalidDataException("The installed SparkingZeroAccess profile was changed.");

            var config = new LoaderConfig
            {
                ShowConsole = false,
                ApplicationConfigDirectory = apps,
                ModConfigDirectory = mods,
                ModUserConfigDirectory = Path.Combine(manager, "User", "Mods"),
                MiscConfigDirectory = Path.Combine(manager, "User", "Misc"),
                PluginConfigDirectory = Path.Combine(manager, "Plugins")
            };
            // Steam started this process itself. Use Reloaded's native mod API
            // directly and leave Steam's startup, overlay and input intact.
            const string initializing = "Initializing the game-local SparkingZeroAccess Reloaded II profile.";
            _status = new ReloadedMappedFile(System.Environment.ProcessId);
            if (_status.GetState().IsInitialized)
                throw new InvalidOperationException("Another Reloaded II loader is already active.");
            _loader = new Loader(config, logFolder);
            _loader.Logger.LogWriteLineAsync(initializing);
            _loader.LoadLocalMod(app, new(modPath, mod));
            _status.SetInitialized(true);
            return 0;
        }
        catch (Exception error)
        {
            try
            {
                if (logFolder != null)
                    File.AppendAllText(Path.Combine(logFolder, "SparkingZeroAccess.AutoStart.log"),
                        "Reloaded II: " + error + System.Environment.NewLine);
            }
            catch (Exception) { }

            return 3;
        }
    }
}
