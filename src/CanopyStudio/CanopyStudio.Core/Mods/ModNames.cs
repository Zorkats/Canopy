using System.Security.Cryptography;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Mods;

// Names in mods compared the way Canopy compares them: _stricmp and
// _wcsicmp in the C locale, which fold only the ASCII letters. Also the
// small file helpers every part of the inventory shares.
internal static class ModNames
{
    public const string ModsFolder = "mods";
    // The loader's own folder, never a mod.
    public const string DataFolder = "CanopyData";

    // The game folder's mods folder by its name on disk; mods when there is
    // none yet, the name Canopy expects.
    public static string ModsIn(string gameFolder) => GameFiles.Find(gameFolder, ModsFolder);

    // That folder's own name, mods unless Linux has it in another case.
    public static string ModsName(string gameFolder) => Path.GetFileName(ModsIn(gameFolder));

    // mods\name as a message shows it, with the platform's separator.
    public static string Shown(string name) => GameFiles.Shown(ModsFolder + "/" + name);

    // Load order: by name, ASCII letters folded to lowercase, last wins.
    public static readonly IComparer<string> LoadOrder = Comparer<string>.Create(Compare);

    public static int Compare(string left, string right)
    {
        int length = Math.Min(left.Length, right.Length);
        for (int index = 0; index < length; ++index)
        {
            int difference = Lower(left[index]) - Lower(right[index]);
            if (difference != 0)
            {
                return difference;
            }
        }
        return left.Length - right.Length;
    }

    public static bool Same(string left, string right) => left.Length == right.Length && Compare(left, right) == 0;

    public static char Lower(char c) => c is >= 'A' and <= 'Z' ? (char)(c - 'A' + 'a') : c;

    public static string Lower(string text) => string.Create(text.Length, text, (span, source) =>
    {
        for (int index = 0; index < source.Length; ++index)
        {
            span[index] = Lower(source[index]);
        }
    });

    public static bool IsDll(string name) => name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase);

    // Why Windows cannot hold name as one file or folder name, or null when
    // it can.
    public static string? NameProblem(string name)
    {
        if (name.Length == 0)
        {
            return "has an empty name";
        }
        if (name is "." or "..")
        {
            return "steps out of its folder";
        }
        foreach (char c in name)
        {
            if (c < ' ' || "<>:\"/\\|?*".Contains(c))
            {
                return "has a character Windows does not allow in a name";
            }
        }
        if (name.EndsWith('.') || name.EndsWith(' '))
        {
            return "ends with a dot or a space";
        }
        string stem = name.Split('.')[0].TrimEnd(' ');
        if (stem.Length is 3 or 4 && (stem.ToUpperInvariant() is "CON" or "PRN" or "AUX" or "NUL"
            || (stem.Length == 4 && stem[3] is >= '0' and <= '9' && stem[..3].ToUpperInvariant() is "COM" or "LPT")))
        {
            return "uses a name Windows reserves for a device";
        }
        return null;
    }

    public static string Sha256(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));

    public static string Sha256File(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }

    // True when path is root or below it, after both are made full.
    public static bool Inside(string path, string root)
    {
        string full = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        string top = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        return full.Equals(top, StringComparison.OrdinalIgnoreCase)
            || full.StartsWith(top + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsLink(FileSystemInfo info) => (info.Attributes & FileAttributes.ReparsePoint) != 0;
}
