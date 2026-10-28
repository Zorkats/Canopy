using System.Globalization;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// A value the edits mod sets that a grid cannot show at a cell, and why.
public sealed record PatchNote(PatchValue Patch, string Reason);

// The edits mod's [patch.] values laid over one table's grid, as the
// loader applies them (docs/PATCHES.md): a section names its row by id or
// by enum name, a row has one section per mod, a path may name several
// places (every language of a text, a field of every element), and the
// first line that sets a place wins. Rows and columns are positions in
// the grid's lists. It is read once; build it again after every change to
// the mod.
public sealed class TablePatches
{
    private readonly Dictionary<(int Row, int Column), Placed> cells = [];
    private readonly Dictionary<int, string> sections = [];
    private readonly HashSet<int> rows = [];
    private readonly List<PatchNote> notes = [];

    private TablePatches()
    {
    }

    // No mod: nothing is patched.
    public static TablePatches None { get; } = new();

    // The values that could not be placed, in file order.
    public IReadOnlyList<PatchNote> Unplaced => notes;

    // The number of cells the mod patches.
    public int Count => cells.Count;

    public static TablePatches Of(EditsMod? mod, TableGrid grid)
    {
        if (mod is null || !grid.Ok)
        {
            return None;
        }
        TablePatches patches = new();
        patches.Read(mod, grid);
        return patches;
    }

    // The value the mod sets at a cell, null when it sets none.
    public PatchValue? At(int row, int column) => cells.TryGetValue((row, column), out Placed? placed) ? placed.Value : null;

    // True when the mod patches some cell of row.
    public bool Patches(int row) => rows.Contains(row);

    // The section of the mod that names row, as written; null when none
    // does.
    public string? SectionOf(int row) => sections.GetValueOrDefault(row);

    // The line that sets a cell, with every other cell of its row it sets.
    internal Placed? PlacedAt(int row, int column) => cells.GetValueOrDefault((row, column));

    // The cells of row that placed sets.
    internal IEnumerable<int> ColumnsOf(int row, Placed placed) =>
        cells.Where(cell => cell.Key.Row == row && ReferenceEquals(cell.Value, placed)).Select(cell => cell.Key.Column);

    private void Read(EditsMod mod, TableGrid grid)
    {
        string table = EditsMod.TableName(grid.File);
        Dictionary<ulong, int> byId = [];
        Dictionary<string, int> byName = new(StringComparer.Ordinal);
        for (int index = 0; index < grid.Rows.Count; index++)
        {
            GridRow row = grid.Rows[index];
            byId.TryAdd(row.Id, index);
            if (row.Enum is { Length: > 0 } name)
            {
                byName.TryAdd(name, index);
            }
        }

        foreach (IniLine line in mod.Project.Structure.Lines)
        {
            if (PatchSections.Split(line.Section) is not { } named || named.Table != table)
            {
                continue;
            }
            int row = Find(named.Row, byId, byName);
            if (line.Kind == "section")
            {
                if (row >= 0)
                {
                    sections.TryAdd(row, line.Section);
                }
                continue;
            }
            if (line.Kind != "value")
            {
                continue;
            }
            PatchValue value = new(named.Table, named.Row, line.Key, line.Value, line.Line);
            if (row < 0)
            {
                notes.Add(new PatchNote(value, value.Id is { } id
                    ? $"{grid.File} has no row with the id {id}."
                    : $"{grid.File} has no row with this enum name."));
                continue;
            }
            if (!string.Equals(sections.GetValueOrDefault(row), line.Section, StringComparison.OrdinalIgnoreCase))
            {
                notes.Add(new PatchNote(value,
                    "An earlier section of the mod patches this row, and Canopy reads one section per row, so it skips this one."));
                continue;
            }
            Place(grid, row, value, line.Section);
        }
    }

    private void Place(TableGrid grid, int row, PatchValue value, string section)
    {
        GridRow data = grid.Rows[row];
        if (!data.Fits)
        {
            notes.Add(new PatchNote(value, "The table's schema does not describe this row, so the field cannot be placed."));
            return;
        }
        List<int> columns = [];
        for (int column = 0; column < grid.Columns.Count; column++)
        {
            if (data.Values[column] is not null && PatchPaths.Names(value.Path, grid.Columns[column].Path))
            {
                columns.Add(column);
            }
        }
        if (columns.Count == 0)
        {
            notes.Add(new PatchNote(value, $"This row of {grid.File} has no place {value.Path}."));
            return;
        }
        if (columns.Any(column => !grid.Columns[column].Editable))
        {
            notes.Add(new PatchNote(value, $"{value.Path} names a field that cannot be patched, so Canopy skips the line."));
            return;
        }
        Placed placed = new(value, section, columns.Count == 1);
        int added = columns.Count(column => cells.TryAdd((row, column), placed));
        if (added == 0)
        {
            notes.Add(new PatchNote(value, "An earlier line of the section sets the same place, so Canopy skips this one."));
            return;
        }
        rows.Add(row);
    }

    private static int Find(string row, Dictionary<ulong, int> byId, Dictionary<string, int> byName)
    {
        if (ulong.TryParse(row, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id))
        {
            return byId.TryGetValue(id, out int at) ? at : -1;
        }
        return byName.TryGetValue(row, out int named) ? named : -1;
    }

    // A value line and the section it is in. Single is true when the
    // line sets only one cell of its row.
    internal sealed record Placed(PatchValue Value, string Section, bool Single);
}

// [patch.<table>.<row>] section names, read as EditsMod reads them.
internal static class PatchSections
{
    private const string Prefix = "patch.";

    // The table, as EditsMod.TableName gives it, and the row as written;
    // null for any other section.
    public static (string Table, string Row)? Split(string section)
    {
        if (!section.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        string rest = section[Prefix.Length..];
        int dot = rest.LastIndexOf('.');
        if (dot <= 0 || dot == rest.Length - 1)
        {
            return null;
        }
        return (EditsMod.TableName(rest[..dot]), rest[(dot + 1)..]);
    }
}

// Which grid columns a patch's field path names, as table_schema::Resolve
// reads a path: names without case, a language by key or by index, and a
// path that leaves out an element index or a language naming every one.
public static class PatchPaths
{
    private static readonly string[] Languages = ["ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"];

    // True when path names the place column (a grid column's path, with
    // every index and language spelled out).
    public static bool Names(string path, string column)
    {
        string[] given = path.Split('.');
        string[] place = column.Split('.');
        int i = 0;
        int j = 0;
        while (j < place.Length)
        {
            bool last = j == place.Length - 1;
            if (i < given.Length && string.Equals(given[i], place[j], StringComparison.OrdinalIgnoreCase))
            {
                i++;
                j++;
            }
            else if (i < given.Length && last && Language(place[j]) is { } language &&
                     given[i] == language.ToString(CultureInfo.InvariantCulture))
            {
                i++;
                j++;
            }
            else if (IsIndex(place[j]) && (i >= given.Length || !IsIndex(given[i])))
            {
                // Every element.
                j++;
            }
            else if (last && i == given.Length && Language(place[j]) is not null)
            {
                // Every language.
                j++;
            }
            else
            {
                return false;
            }
        }
        return i == given.Length;
    }

    private static int? Language(string key)
    {
        int index = Array.FindIndex(Languages, language => language.Equals(key, StringComparison.OrdinalIgnoreCase));
        return index < 0 ? null : index;
    }

    private static bool IsIndex(string part) => part.Length > 0 && part.All(char.IsAsciiDigit);
}
