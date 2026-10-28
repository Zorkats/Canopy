namespace CanopyStudio.Core.Platform;

// Files inside the game folder by the names Canopy gives them. Windows, and
// Wine when the game runs under Proton, find a name whatever its case, so a
// folder named Mods or a file named CANOPY.INI is the one Canopy reads. A
// Linux file system tells them apart, so there each part of the path is
// looked up without case, here and nowhere else. A part that is not there
// keeps the name Canopy expects, so a new file is written under that name.
public static class GameFiles
{
    private static readonly char[] Separators = ['/', '\\'];

    // True where the file system tells names apart by case.
    public static bool CaseBlindLookup { get; } = !OperatingSystem.IsWindows();

    // relative below folder, its parts split at '/' or '\'.
    public static string Find(string folder, string relative) => Find(folder, relative, CaseBlindLookup);

    // As Find. With caseBlind false the parts are only joined, as on Windows;
    // with it true each one takes the name found on disk, an exact match
    // first, then the first without case in ordinal order.
    public static string Find(string folder, string relative, bool caseBlind)
    {
        string path = folder;
        bool lookUp = caseBlind;
        foreach (string part in relative.Split(Separators, StringSplitOptions.RemoveEmptyEntries))
        {
            if (lookUp && part is not ("." or ".."))
            {
                if (Match(path, part) is { } found)
                {
                    path = Path.Combine(path, found);
                    continue;
                }
                // Nothing below a part that is not there can be.
                lookUp = false;
            }
            path = Path.Combine(path, part);
        }
        return path;
    }

    // relative, written with '/', as this platform writes paths for the
    // user: mods\Name on Windows, mods/Name on Linux.
    public static string Shown(string relative) =>
        relative.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);

    // path with every link in it followed, as realpath does, so two routes to
    // one folder compare equal: ~/.steam/steam is a link to
    // ~/.local/share/Steam. Parts that are not there are kept as given.
    public static string RealPath(string path)
    {
        string full = Path.GetFullPath(path);
        for (int hop = 0; hop < MaxLinks; ++hop)
        {
            string root = Path.GetPathRoot(full) ?? "";
            string[] parts = full[root.Length..].Split(Path.DirectorySeparatorChar, StringSplitOptions.RemoveEmptyEntries);
            string current = root;
            string? next = null;
            for (int index = 0; index < parts.Length && next is null; ++index)
            {
                string step = Path.Combine(current, parts[index]);
                if (LinkTarget(step) is { } target)
                {
                    // A relative target is relative to the link's own folder.
                    next = Path.GetFullPath(Path.Combine([Path.GetFullPath(target, current), .. parts[(index + 1)..]]));
                }
                current = step;
            }
            if (next is null)
            {
                return current;
            }
            full = next;
        }
        return full;
    }

    // Past this many links a path is taken to loop, as Linux's ELOOP.
    private const int MaxLinks = 40;

    private static string? LinkTarget(string path)
    {
        try
        {
            return new DirectoryInfo(path).LinkTarget;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    // The name in folder that is name, or null when folder holds none.
    private static string? Match(string folder, string name)
    {
        try
        {
            if (!Directory.Exists(folder))
            {
                return null;
            }
            string? blind = null;
            foreach (string entry in Directory.EnumerateFileSystemEntries(folder, "*", new EnumerationOptions { AttributesToSkip = 0 }))
            {
                string candidate = Path.GetFileName(entry);
                if (candidate == name)
                {
                    return candidate;
                }
                if (candidate.Equals(name, StringComparison.OrdinalIgnoreCase)
                    && (blind is null || string.CompareOrdinal(candidate, blind) < 0))
                {
                    blind = candidate;
                }
            }
            return blind;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
