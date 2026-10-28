using System.Runtime.Versioning;
using System.Text.RegularExpressions;
using CanopyStudio.Core.Platform;
using Microsoft.Win32;

namespace CanopyStudio.Core.Setup;

// Where Village in the Shade may be installed: each Steam library on this
// PC that has the game. Only reads Steam's own files and registry key. On
// Linux Steam is found in the home folder, as installed natively or as a
// Flatpak, and the game's files are looked up without case.
public static partial class GameLocator
{
    public const string SteamAppId = "3934250";
    public const string DefaultInstallDir = "Village in the Shade";
    public const string GameExe = "village.exe";

    // Where Steam keeps itself below the home folder on Linux: the native
    // package's two links and its folder, then the Flatpak's folder.
    public static readonly IReadOnlyList<string> LinuxSteamFolders =
    [
        ".steam/steam",
        ".steam/root",
        ".local/share/Steam",
        ".var/app/com.valvesoftware.Steam/.local/share/Steam",
    ];

    // A game folder as this platform writes one, for an empty folder box.
    public static string FolderExample => OperatingSystem.IsWindows()
        ? @"For example C:\Program Files (x86)\Steam\steamapps\common\Village in the Shade"
        : "For example ~/.local/share/Steam/steamapps/common/Village in the Shade";

    // A folder as typed into a folder box: spaces and the quotes a copied
    // path brings trimmed, and with home given a leading ~ is that folder,
    // as a Linux shell reads it. Empty when nothing is left.
    public static string TypedFolder(string text, string? home)
    {
        string folder = text.Trim().Trim('"', '\'').Trim();
        if (home is not null && (folder == "~" || folder.StartsWith("~/", StringComparison.Ordinal)))
        {
            folder = home.TrimEnd('/') + folder[1..];
        }
        return folder;
    }

    // TypedFolder with this computer's home folder off Windows, where ~ is
    // just a name.
    public static string TypedFolder(string text) => TypedFolder(text, OperatingSystem.IsWindows()
        ? null
        : Environment.GetFolderPath(Environment.SpecialFolder.UserProfile, Environment.SpecialFolderOption.DoNotVerify));

    // Folders holding village.exe, best first, without duplicates.
    public static IReadOnlyList<string> Candidates()
    {
        if (OperatingSystem.IsWindows())
        {
            return Candidates(WindowsSteamRoot() is { } root ? [root] : [], caseBlind: false);
        }
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile, Environment.SpecialFolderOption.DoNotVerify);
        return Candidates(LinuxSteamRoots(home), caseBlind: true);
    }

    // The game folders in the Steam roots given, each root first, then the
    // libraries its libraryfolders.vdf lists. caseBlind is for a Linux file
    // system: steamapps, the manifest, common, the install folder and
    // village.exe are found whatever their case, and folders reached through
    // links count once.
    public static IReadOnlyList<string> Candidates(IReadOnlyList<string> steamRoots, bool caseBlind)
    {
        StringComparer same = caseBlind ? StringComparer.Ordinal : StringComparer.OrdinalIgnoreCase;
        List<string> found = [];
        foreach (string library in steamRoots.SelectMany(root => SteamLibraries(root, caseBlind)))
        {
            string apps = GameFiles.Find(library, "steamapps", caseBlind);
            string installDir = InstallDir(GameFiles.Find(apps, $"appmanifest_{SteamAppId}.acf", caseBlind)) ?? DefaultInstallDir;
            string folder = GameFiles.Find(GameFiles.Find(apps, "common", caseBlind), installDir, caseBlind);
            if (caseBlind)
            {
                folder = GameFiles.RealPath(folder);
            }
            if (File.Exists(GameFiles.Find(folder, GameExe, caseBlind)) && !found.Contains(folder, same))
            {
                found.Add(folder);
            }
        }
        return found;
    }

    // The Steam roots below home that exist, links followed, each once.
    public static IReadOnlyList<string> LinuxSteamRoots(string home)
    {
        List<string> roots = [];
        foreach (string folder in LinuxSteamFolders)
        {
            string path = GameFiles.Find(home, folder, caseBlind: false);
            if (Directory.Exists(path) && GameFiles.RealPath(path) is var real && !roots.Contains(real, StringComparer.Ordinal))
            {
                roots.Add(real);
            }
        }
        return roots;
    }

    [SupportedOSPlatform("windows")]
    private static string? WindowsSteamRoot()
    {
        try
        {
            using RegistryKey? key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            if (key?.GetValue("SteamPath") is string path && path.Length > 0)
            {
                return path.Replace('/', Path.DirectorySeparatorChar);
            }
        }
        catch (Exception exception) when (exception is System.Security.SecurityException or UnauthorizedAccessException or IOException)
        {
        }
        string fallback = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");
        return Directory.Exists(fallback) ? fallback : null;
    }

    // The Steam root and every library libraryfolders.vdf lists.
    private static IEnumerable<string> SteamLibraries(string steamRoot, bool caseBlind)
    {
        yield return steamRoot;
        string vdf = GameFiles.Find(steamRoot, Path.Combine("steamapps", "libraryfolders.vdf"), caseBlind);
        string text;
        try
        {
            text = File.ReadAllText(vdf);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            yield break;
        }
        foreach (string path in VdfValues(text, "path"))
        {
            yield return path;
        }
    }

    private static string? InstallDir(string manifest)
    {
        try
        {
            return VdfValues(File.ReadAllText(manifest), "installdir").FirstOrDefault();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    // Every value of key in Valve's KeyValues text, unescaped: "key" "value".
    public static IEnumerable<string> VdfValues(string text, string key)
    {
        foreach (Match match in VdfPair().Matches(text))
        {
            if (string.Equals(match.Groups["key"].Value, key, StringComparison.OrdinalIgnoreCase))
            {
                yield return Regex.Unescape(match.Groups["value"].Value);
            }
        }
    }

    [GeneratedRegex("\"(?<key>[^\"\\\\]*)\"[ \\t]+\"(?<value>(?:[^\"\\\\]|\\\\.)*)\"")]
    private static partial Regex VdfPair();
}
