using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Play;

// Where the Play area reads and writes. Mod changes keep their backups in
// %APPDATA%\CanopyStudio\mod-backups (~/.config/CanopyStudio/mod-backups on
// Linux), outside the game folder, so a Steam file check never removes them;
// tests pass a folder of their own. Files in the game folder are found
// whatever their case on Linux, as Canopy finds them under Proton.
public static class PlayPaths
{
    public const string BackupsFolder = "mod-backups";

    public static string Backups() =>
        BackupsIn(StudioFolders.UserData());

    // The backup folder below appData, the folder %APPDATA% names.
    public static string BackupsIn(string appData) => Path.Combine(appData, StudioFolders.Name, BackupsFolder);

    public static string Mods(string gameFolder) => ModNames.ModsIn(gameFolder);

    // A mod's folder or DLL file in mods, by its name on disk.
    public static string Mod(string gameFolder, string name) => Path.Combine(Mods(gameFolder), name);

    public static string Log(string gameFolder) => GameFiles.Find(gameFolder, CanopyLog.FileName);

    // mods\name as a message shows it, with the platform's separator.
    public static string Shown(string name) => ModNames.Shown(name);
}
