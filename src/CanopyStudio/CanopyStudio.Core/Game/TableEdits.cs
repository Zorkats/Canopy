using System.Globalization;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// Edits of one cell or row of a table grid, made in the edits mod as
// [patch.] values. A value is checked first and nothing is written when it
// is refused; a value equal to the game's own takes the patch away. Rows
// and columns are positions in the grid's lists. Each returns null when
// done, else why not. Nothing is saved: the caller saves the mod.
public static class TableEdits
{
    // Sets the cell at row, column to text. ids, when given, are the
    // values a reference column takes (see RefIds).
    public static string? Commit(EditsMod mod, TableGrid grid, int row, int column, string text,
        IReadOnlySet<ulong>? ids = null)
    {
        if (Problem(grid, row, column) is { } problem)
        {
            return problem;
        }
        GridColumn place = grid.Columns[column];
        // The game's own value is taken back as it is, unchecked: some
        // game texts hold what canopy.ini cannot, such as end spaces, and
        // leaving such a cell unchanged must not refuse it.
        if (string.Equals(text, grid.Rows[row].Values[column], StringComparison.Ordinal))
        {
            return Spread(mod, grid, row, column) ?? Clear(mod, grid, row, column);
        }
        FieldValue value = FieldValueCheck.Check(place.Type, text, ids, place.Table);
        if (!value.Ok)
        {
            return value.Error;
        }
        if (Spread(mod, grid, row, column) is { } spread)
        {
            return spread;
        }
        if (FieldValueCheck.Same(place.Type, value.Text, grid.Rows[row].Values[column]))
        {
            return Clear(mod, grid, row, column);
        }
        // A line that already sets the cell keeps its key, which may name
        // a language by index; a new one goes in the row's section.
        TablePatches patches = TablePatches.Of(mod, grid);
        TablePatches.Placed? placed = patches.PlacedAt(row, column);
        string section = placed?.Section ?? patches.SectionOf(row) ?? EditsMod.Section(grid.File, grid.Rows[row].Id);
        return mod.Project.Edit("set", section, placed?.Value.Path ?? place.Path, value.Text!);
    }

    // Takes the patch of one cell away, so it shows the game's value.
    public static string? Revert(EditsMod mod, TableGrid grid, int row, int column)
    {
        if (row < 0 || row >= grid.Rows.Count || column < 0 || column >= grid.Columns.Count)
        {
            return "That cell is not in the table.";
        }
        if (TablePatches.Of(mod, grid).At(row, column) is null)
        {
            return null;
        }
        return Clear(mod, grid, row, column);
    }

    // Takes every patch of a row away: the sections that name it.
    public static string? RevertRow(EditsMod mod, TableGrid grid, int row)
    {
        if (row < 0 || row >= grid.Rows.Count)
        {
            return "That row is not in the table.";
        }
        string table = EditsMod.TableName(grid.File);
        GridRow data = grid.Rows[row];
        string id = data.Id.ToString(CultureInfo.InvariantCulture);
        List<string> named = mod.Project.SectionNames.Where(section =>
            PatchSections.Split(section) is { } split && split.Table == table &&
            (split.Row == id || (data.Enum is { Length: > 0 } name && split.Row == name))).ToList();
        foreach (string section in named)
        {
            if (mod.Project.Edit("removeSection", section) is { } error)
            {
                return error;
            }
        }
        return null;
    }

    // The values a reference column takes: the ids of the rows of the
    // table it points into (with its framework_ twin's), and every value
    // the column already holds in the game, such as 0 for none.
    public static IReadOnlySet<ulong> RefIds(TableGrid grid, int column, IEnumerable<ulong> targetIds)
    {
        HashSet<ulong> ids = [.. targetIds];
        foreach (GridRow row in grid.Rows)
        {
            if (column < row.Values.Count && row.Values[column] is { } value &&
                ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id))
            {
                ids.Add(id);
            }
        }
        return ids;
    }

    // Why the cell cannot be edited, null when it can.
    public static string? Problem(TableGrid grid, int row, int column)
    {
        if (row < 0 || row >= grid.Rows.Count || column < 0 || column >= grid.Columns.Count)
        {
            return "That cell is not in the table.";
        }
        GridColumn place = grid.Columns[column];
        GridRow data = grid.Rows[row];
        if (!data.Fits)
        {
            return "The table's schema does not describe this row, so its fields cannot be edited.";
        }
        if (data.Values[column] is null)
        {
            return $"This row has no {place.Path}.";
        }
        if (!place.Editable || !FieldValueCheck.IsWritable(place.Type))
        {
            return $"{place.Path} is read only: a patch cannot change it.";
        }
        return null;
    }

    // When a line that sets several places of the row (every language, or
    // a field of every element) sets this cell, writes each of those places
    // as its own line instead, so the one cell can change alone.
    private static string? Spread(EditsMod mod, TableGrid grid, int row, int column)
    {
        TablePatches patches = TablePatches.Of(mod, grid);
        if (patches.PlacedAt(row, column) is not { Single: false } placed)
        {
            return null;
        }
        foreach (int other in patches.ColumnsOf(row, placed).ToList())
        {
            if (mod.Project.Edit("set", placed.Section, grid.Columns[other].Path, placed.Value.Value) is { } error)
            {
                return error;
            }
        }
        return mod.Project.Edit("remove", placed.Section, placed.Value.Path);
    }

    // Removes the line that sets the cell, and the row's section when
    // nothing is left in it. A line the loader skipped, because an earlier
    // one set the same place, would count once that one is gone, so it goes
    // too.
    private static string? Clear(EditsMod mod, TableGrid grid, int row, int column)
    {
        for (int round = 0; round < MaxRounds; round++)
        {
            if (Spread(mod, grid, row, column) is { } spread)
            {
                return spread;
            }
            if (TablePatches.Of(mod, grid).PlacedAt(row, column) is not { } placed)
            {
                return null;
            }
            if (mod.Project.Edit("remove", placed.Section, placed.Value.Path) is { } error)
            {
                return error;
            }
            bool empty = !mod.Project.Structure.Lines.Any(line =>
                line.Kind == "value" && string.Equals(line.Section, placed.Section, StringComparison.OrdinalIgnoreCase));
            if (empty && mod.Project.Edit("removeSection", placed.Section) is { } removed)
            {
                return removed;
            }
        }
        return "The patch could not be taken away. Remove its lines in Create.";
    }

    // How many lines that set one place Clear removes at most.
    private const int MaxRounds = 16;
}
