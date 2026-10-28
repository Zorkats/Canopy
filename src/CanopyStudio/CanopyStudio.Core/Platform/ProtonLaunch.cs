namespace CanopyStudio.Core.Platform;

// What the game needs from Steam on Linux. The game runs through Proton,
// whose Wine loads its own dinput8.dll unless told to prefer the one next
// to village.exe, so Canopy loads only with this launch option. Canopy
// Studio shows it and copies it; it never edits Steam's files.
public static class ProtonLaunch
{
    public const string LaunchOption = "WINEDLLOVERRIDES=\"dinput8=n,b\" %command%";

    public const string Explanation =
        "On Linux the game runs through Proton, which loads its own dinput8.dll unless told otherwise. For Canopy to load, open the game's Properties in Steam and put this line in its Launch Options. Canopy Studio never changes Steam's files, so this is the one step you do yourself.";

    // True on Linux, where the game runs under Proton.
    public static bool Needed { get; } = OperatingSystem.IsLinux();
}
