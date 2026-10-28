using System.Globalization;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// One row of texture.dat as the Art section lists it: its id and enum name,
// the size the row records (0 in vanilla rows, which leave it to the file),
// and the file it names per language, in LangKeys.Codes order.
public sealed record ArtTexture(int Index, ulong Id, string? Enum, int Width, int Height, IReadOnlyList<string> Files)
{
    public const string English = "en";

    // The English file, or the first language's that has one.
    public string File => FileIn(English) is { Length: > 0 } english
        ? english
        : Files.FirstOrDefault(file => file.Length > 0) ?? "";

    public string FileIn(string code)
    {
        int at = IndexOf(code);
        return at >= 0 && at < Files.Count ? Files[at] : "";
    }

    // The languages whose file differs from File, with that file.
    public IReadOnlyList<(string Code, string File)> OtherLanguages()
    {
        string main = File;
        List<(string, string)> others = [];
        for (int at = 0; at < LangKeys.Codes.Count && at < Files.Count; ++at)
        {
            if (Files[at].Length > 0 && !string.Equals(Files[at], main, StringComparison.OrdinalIgnoreCase))
            {
                others.Add((LangKeys.Codes[at], Files[at]));
            }
        }
        return others;
    }

    // True when filter is empty or the id, enum name or file holds it.
    public bool Matches(string filter)
    {
        string text = filter.Trim();
        return text.Length == 0 ||
            Id.ToString(CultureInfo.InvariantCulture).Contains(text, StringComparison.Ordinal) ||
            (Enum?.Contains(text, StringComparison.OrdinalIgnoreCase) ?? false) ||
            File.Contains(text, StringComparison.OrdinalIgnoreCase);
    }

    private static int IndexOf(string code)
    {
        for (int at = 0; at < LangKeys.Codes.Count; ++at)
        {
            if (string.Equals(LangKeys.Codes[at], code, StringComparison.OrdinalIgnoreCase))
            {
                return at;
            }
        }
        return -1;
    }
}

public static class ArtTextures
{
    // Every row of texture.dat's grid, in table order. A row the schema
    // does not fit is listed with no files.
    public static IReadOnlyList<ArtTexture> FromGrid(TableGrid grid)
    {
        int width = Column(grid, "width");
        int height = Column(grid, "height");
        int[] files = [.. LangKeys.Keys(ArtReplace.FileField).Select(path => Column(grid, path))];
        List<ArtTexture> textures = new(grid.Rows.Count);
        foreach (GridRow row in grid.Rows)
        {
            textures.Add(new ArtTexture(row.Index, row.Id, row.Enum, Number(row, width), Number(row, height),
                [.. files.Select(column => Text(row, column))]));
        }
        return textures;
    }

    private static int Column(TableGrid grid, string path)
    {
        for (int at = 0; at < grid.Columns.Count; ++at)
        {
            if (string.Equals(grid.Columns[at].Path, path, StringComparison.OrdinalIgnoreCase))
            {
                return at;
            }
        }
        return -1;
    }

    private static string Text(GridRow row, int column) =>
        column >= 0 && column < row.Values.Count ? row.Values[column] ?? "" : "";

    private static int Number(GridRow row, int column) =>
        int.TryParse(Text(row, column), NumberStyles.None, CultureInfo.InvariantCulture, out int value) ? value : 0;
}
