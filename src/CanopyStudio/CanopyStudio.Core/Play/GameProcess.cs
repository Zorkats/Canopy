using CanopyStudio.Core.Platform;
using CanopyStudio.Core.Setup;

namespace CanopyStudio.Core.Play;

// Whether the game is running, by its process name. Canopy reads the mods
// folder and disabled.txt as the game starts, and Windows keeps mod DLLs in
// use while it runs, so the Play area changes nothing until it is closed.
public static class GameProcess
{
    public const string CloseFirstText =
        "Close the game first, then try again: Canopy reads the mods folder only as the game starts, and Windows keeps mod DLLs in use while it runs.";

    public static bool IsRunning() => IsRunning(GameLocator.GameExe);

    // True while a process named exeName, with or without ".exe", runs, on
    // Windows or under Proton.
    public static bool IsRunning(string exeName) => GameProcesses.IsRunning(exeName);
}
