using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// One per-language text of one row: the field's path without its language
// (name, entries.0.name) and, for each of the game's languages in table
// order, the vanilla text, null where the row has no such place. Editable
// says, per language, whether a patch can set it. File is the table as the
// grid names it, Index the row's place in it.
public sealed record GameTextEntry(
    string File, int Index, ulong Id, string? Enum, string Field,
    IReadOnlyList<string?> Values, IReadOnlyList<bool> Editable)
{
    // The field path a patch sets for language, such as name.en.
    public string Path(int language) => Field + "." + GameTextIndex.Codes[language];

    public bool Has(int language) => Values[language] is not null;
}

// How many texts a table gave the index.
public sealed record GameTextTable(string File, int Count);

// Every per-language text the patchable tables hold, one entry per row and
// per text field, built from the tables' grids. Rows the schema does not
// describe are left out, and so are texts that are not per language (a
// texture's comment, file names).
public sealed class GameTextIndex
{
    private const string Twin = "framework_";

    private static readonly Lazy<HashSet<string>> Patchable = new(ReadPatchable);

    // The tables a [patch.] section can address (docs/PATCHES.md), as
    // canopy_core lists them; empty when canopy_core cannot be loaded. A
    // table can also come as framework_<table>.dat, patched the same way.
    public static IReadOnlySet<string> PatchableTables => Patchable.Value;

    // The language codes, in the order every per-language field holds them.
    public static IReadOnlyList<string> Codes { get; } = VanillaKinds.Languages.Select(language => language.Code).ToArray();

    public GameTextIndex(IReadOnlyList<GameTextEntry> entries)
    {
        Entries = entries;
        Tables = entries
            .GroupBy(entry => entry.File, StringComparer.OrdinalIgnoreCase)
            .Select(group => new GameTextTable(group.Key, group.Count()))
            .OrderBy(table => table.File, StringComparer.Ordinal)
            .ToList();
        Languages = Enumerable.Range(0, Codes.Count)
            .Where(language => entries.Any(entry => entry.Has(language)))
            .ToList();
    }

    public static GameTextIndex Empty { get; } = new([]);

    public IReadOnlyList<GameTextEntry> Entries { get; }

    // The tables with at least one text, by file name.
    public IReadOnlyList<GameTextTable> Tables { get; }

    // The languages some entry has a text in, as indexes into Codes.
    public IReadOnlyList<int> Languages { get; }

    // The index of the language code, -1 for none.
    public static int LanguageOf(string code)
    {
        for (int index = 0; index < Codes.Count; ++index)
        {
            if (string.Equals(Codes[index], code, StringComparison.OrdinalIgnoreCase))
            {
                return index;
            }
        }
        return -1;
    }

    // True for a table file the index reads: a patchable table or its
    // framework_ twin.
    public static bool Holds(string file)
    {
        string name = file.Trim().ToLowerInvariant();
        if (!name.EndsWith(".dat", StringComparison.Ordinal))
        {
            name += ".dat";
        }
        if (name.StartsWith(Twin, StringComparison.Ordinal))
        {
            name = name[Twin.Length..];
        }
        return PatchableTables.Contains(name);
    }

    private static HashSet<string> ReadPatchable()
    {
        try
        {
            return CanopyCore.Sections().Tables.ToHashSet(StringComparer.Ordinal);
        }
        catch (Exception error) when (error is DllNotFoundException or BadImageFormatException)
        {
            return [];
        }
    }

    public static GameTextIndex Build(IEnumerable<TableGrid> grids) => new(grids.SelectMany(FromGrid).ToList());

    // The texts of one grid, row by row and field by field in column order.
    // Empty for a grid that was not read.
    public static IReadOnlyList<GameTextEntry> FromGrid(TableGrid grid)
    {
        if (!grid.Ok)
        {
            return [];
        }
        // Each per-language text field once, with its column per language.
        List<(string Field, int[] Columns)> fields = [];
        Dictionary<string, int> byField = new(StringComparer.Ordinal);
        for (int column = 0; column < grid.Columns.Count; ++column)
        {
            GridColumn place = grid.Columns[column];
            int dot = place.Path.LastIndexOf('.');
            if (place.Type != "text" || dot <= 0)
            {
                continue;
            }
            int language = LanguageOf(place.Path[(dot + 1)..]);
            if (language < 0)
            {
                continue;
            }
            string field = place.Path[..dot];
            if (!byField.TryGetValue(field, out int at))
            {
                at = fields.Count;
                byField[field] = at;
                fields.Add((field, Enumerable.Repeat(-1, Codes.Count).ToArray()));
            }
            fields[at].Columns[language] = column;
        }

        List<GameTextEntry> entries = [];
        foreach (GridRow row in grid.Rows)
        {
            if (!row.Fits)
            {
                continue;
            }
            foreach ((string field, int[] columns) in fields)
            {
                string?[] values = new string?[Codes.Count];
                bool[] editable = new bool[Codes.Count];
                bool any = false;
                for (int language = 0; language < columns.Length; ++language)
                {
                    int column = columns[language];
                    if (column < 0 || column >= row.Values.Count || row.Values[column] is not { } value)
                    {
                        continue;
                    }
                    values[language] = value;
                    editable[language] = grid.Columns[column].Editable;
                    any = true;
                }
                if (any)
                {
                    entries.Add(new GameTextEntry(grid.File, row.Index, row.Id, row.Enum, field, values, editable));
                }
            }
        }
        return entries;
    }
}
