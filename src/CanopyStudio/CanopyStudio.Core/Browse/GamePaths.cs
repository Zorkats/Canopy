using System.Globalization;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Browse;

// Facts about a game path as the archives spell it: lowercase, '/' between
// folders, no leading separator.
public static class GamePaths
{
    public const string DatabaseFolder = "data/database/";

    public static string FileName(string path) => path[(path.LastIndexOf('/') + 1)..];

    // The folder part with its trailing '/', or "" for a top-level file.
    public static string FolderOf(string path) => path[..(path.LastIndexOf('/') + 1)];

    public static bool IsTexture(string path) => path.EndsWith(".nltx", StringComparison.OrdinalIgnoreCase);

    // A table the database pages can open: a .dat right in data/database/.
    public static bool IsTable(string path) =>
        path.StartsWith(DatabaseFolder, StringComparison.Ordinal) &&
        path.Length > DatabaseFolder.Length + 4 &&
        path.IndexOf('/', DatabaseFolder.Length) < 0 &&
        path.EndsWith(".dat", StringComparison.Ordinal);

    // Table file names, sorted.
    public static IReadOnlyList<string> Tables(IEnumerable<GameFile> files) =>
        files.Select(file => file.Path).Where(IsTable).Select(FileName).Order(StringComparer.Ordinal).ToList();

    public static string SizeText(long bytes)
    {
        CultureInfo culture = CultureInfo.CurrentCulture;
        return bytes switch
        {
            < 1024 => string.Format(culture, "{0} B", bytes),
            < 1024 * 1024 => string.Format(culture, "{0:0.#} KB", bytes / 1024.0),
            _ => string.Format(culture, "{0:0.#} MB", bytes / (1024.0 * 1024.0)),
        };
    }
}
