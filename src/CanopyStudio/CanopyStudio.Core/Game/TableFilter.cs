using System.Globalization;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// Narrowing a table grid: rows by a text and by what the edits mod
// changes, columns by their paths. Rows and columns are positions in the
// grid's lists.
public static class TableFilter
{
    // The rows whose id, enum name or any value, the game's or the edits
    // mod's, contains text without case; with changedOnly, only the rows
    // the mod patches.
    public static IReadOnlyList<int> Rows(TableGrid grid, TablePatches patches, string text, bool changedOnly)
    {
        string filter = text.Trim();
        List<int> rows = [];
        for (int index = 0; index < grid.Rows.Count; index++)
        {
            if (changedOnly && !patches.Patches(index))
            {
                continue;
            }
            if (filter.Length == 0 || RowHas(grid, patches, index, filter))
            {
                rows.Add(index);
            }
        }
        return rows;
    }

    // The columns to show for a filter over their paths: each word of it,
    // split at spaces or commas, is looked for without case, and a column
    // that has any of them shows. An empty filter shows every column.
    public static IReadOnlyList<bool> Columns(IReadOnlyList<GridColumn> columns, string filter)
    {
        string[] words = filter.Split([' ', ','], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return columns.Select(column => words.Length == 0 ||
            words.Any(word => column.Path.Contains(word, StringComparison.OrdinalIgnoreCase))).ToList();
    }

    private static bool RowHas(TableGrid grid, TablePatches patches, int index, string filter)
    {
        GridRow row = grid.Rows[index];
        if (row.Id.ToString(CultureInfo.InvariantCulture).Contains(filter, StringComparison.Ordinal) ||
            (row.Enum?.Contains(filter, StringComparison.OrdinalIgnoreCase) ?? false))
        {
            return true;
        }
        for (int column = 0; column < row.Values.Count; column++)
        {
            if ((row.Values[column]?.Contains(filter, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (patches.Patches(index) && (patches.At(index, column)?.Value.Contains(filter, StringComparison.OrdinalIgnoreCase) ?? false)))
            {
                return true;
            }
        }
        return false;
    }
}
