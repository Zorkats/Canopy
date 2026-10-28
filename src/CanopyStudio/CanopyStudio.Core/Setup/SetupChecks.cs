using CanopyStudio.Core.Install;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Setup;

public enum CheckState
{
    Pass,
    Info,
    Warn,
    Fail,
}

public sealed record SetupCheck(string Title, string Detail, CheckState State);

// What the Setup page says about a game folder, check by check. Only a
// failed archive check stops the folder from being used: browsing needs the
// archives and nothing else.
public static class SetupChecks
{
    // loader is what the folder's dinput8.dll is, when known: canopy_core.dll
    // only says one is there.
    public static IReadOnlyList<SetupCheck> For(GameInfo info, bool folderExists, LoaderKind? loader = null)
    {
        if (!folderExists)
        {
            return [new("Folder", "This folder does not exist.", CheckState.Fail)];
        }
        List<SetupCheck> checks =
        [
            info.Exe
                ? new("Game program", "village.exe is in this folder.", CheckState.Pass)
                : new("Game program", "village.exe is not in this folder. Pick the folder that holds it.", CheckState.Fail),
        ];
        if (info.Exe)
        {
            checks.Add(info.Supported
                ? new("Game version", "This is a game version Canopy has checked.", CheckState.Pass)
                : new("Game version", $"This village.exe is build {info.Build:X8}, which Canopy has not checked yet. Canopy finds what it needs on its own, but features that write saves stay off and some mods may not load.", CheckState.Warn));
        }
        checks.Add(info.Ok
            ? new("Game archives", $"{info.Archives.Count} archives open, holding {info.Files:N0} files and {info.Tables} tables.", CheckState.Pass)
            : new("Game archives", info.Error ?? "The game archives could not be opened.", CheckState.Fail));
        if (!info.Ok)
        {
            return checks;
        }
        checks.Add(loader == LoaderKind.Other
            ? new("Canopy", "The dinput8.dll in this folder is another program's, not Canopy. Browsing works; mods need Canopy to load in game.", CheckState.Warn)
            : info.Canopy
                ? new("Canopy", "Canopy is installed (dinput8.dll).", CheckState.Pass)
                : new("Canopy", "Canopy is not installed in this folder. Browsing works; mods need Canopy to load in game.", CheckState.Warn));
        checks.Add(info.Mods
            ? new("Mods folder", "The mods folder is there.", CheckState.Pass)
            : new("Mods folder", "There is no mods folder yet. Mods go in a folder named mods next to village.exe.", CheckState.Info));
        checks.Add(info.Log
            ? new("Canopy log", "Canopy.log is there, so the game has run with Canopy.", CheckState.Pass)
            : new("Canopy log", "There is no Canopy.log yet. It appears once the game runs with Canopy.", CheckState.Info));
        return checks;
    }
}
