using System.ComponentModel;
using System.Globalization;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// A value of the edits mod the grid cannot show at a cell.
public sealed record PatchNoteItem(PatchNote Note)
{
    public string LineText => $"Line {Note.Patch.Line}";
    public string Where => $"[patch.{Note.Patch.Table}.{Note.Patch.Row}] {Note.Patch.Path} = {Note.Patch.Value}";
    public string Reason => Note.Reason;
}

// A table with a schema as an editable grid: one row per table row, one
// column per place the schema names. A cell edit is checked, then written
// to the edits mod as a [patch.] value; the mod's values show over the
// game's. The patches are read again each time the edits mod changes,
// whichever page changed it.
public sealed partial class TableGridViewModel : ObservableObject
{
    private readonly GameSession session;
    // The row ids of each table references point into, per game folder.
    private readonly Dictionary<string, Task<IReadOnlyList<ulong>?>> targets = new(StringComparer.OrdinalIgnoreCase);
    // The values each reference column of the open grid takes.
    private readonly Dictionary<int, IReadOnlySet<ulong>?> refIds = [];
    private TableGrid? grid;
    private List<TableRowItem> items = [];
    private int version;

    [ObservableProperty]
    private string file = "";

    [ObservableProperty]
    private bool isLoading;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    private string? error;

    [ObservableProperty]
    private IReadOnlyList<TableRowItem> visibleRows = [];

    [ObservableProperty]
    private TableRowItem? selectedRow;

    [ObservableProperty]
    private string rowFilter = "";

    [ObservableProperty]
    private bool changedOnly;

    [ObservableProperty]
    private string columnFilter = "";

    // Per grid column, whether the column filter shows it.
    [ObservableProperty]
    private IReadOnlyList<bool> shownColumns = [];

    // What became of the last edit, when it needs saying.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStatus))]
    private string? status;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasUnplaced), nameof(UnplacedTitle))]
    private IReadOnlyList<PatchNoteItem> unplaced = [];

    public TableGridViewModel(GameSession session, GameEdits edits)
    {
        this.session = session;
        Edits = edits;
        session.GameChanged += (_, _) => targets.Clear();
        edits.Edited += (_, _) => Refresh();
        edits.PropertyChanged += OnEditsChanged;
    }

    public GameEdits Edits { get; }

    // The open grid, null while none is.
    public TableGrid? Grid => grid;
    public IReadOnlyList<GridColumn> Columns => grid?.Columns ?? [];
    public TablePatches Patches { get; private set; } = TablePatches.None;

    // The grid columns shown as columns: all but the row's id and enum
    // name, which the grid always shows first.
    public IReadOnlyList<int> GridColumns { get; private set; } = [];

    public bool HasGrid => grid is not null;
    public bool HasError => Error is not null;
    public bool HasStatus => Status is not null;
    public bool HasUnplaced => Unplaced.Count > 0;
    public bool CanEdit => Edits.Current is not null;

    public string Title => File;
    public string Kind => grid?.StructName is { Length: > 0 } name ? $"Schema: {name}" : "Schema";

    public string Summary
    {
        get
        {
            if (grid is null)
            {
                return "";
            }
            CultureInfo culture = CultureInfo.CurrentCulture;
            int editable = grid.Columns.Count(column => column.Editable);
            string shape = string.Format(culture, "{0:N0} rows, {1:N0} fields, {2:N0} editable.", grid.Rows.Count,
                grid.Columns.Count, editable);
            if (Edits.Current is null)
            {
                return $"{shape} Choose an edits mod above to change them.";
            }
            int rows = items.Count(item => item.IsPatched);
            return Patches.Count == 0
                ? $"{shape} {Edits.Current.Name} changes nothing here yet."
                : string.Format(culture, "{0} {1} changes {2:N0} {3} in {4:N0} {5}.", shape, Edits.Current.Name, Patches.Count,
                    Patches.Count == 1 ? "field" : "fields", rows, rows == 1 ? "row" : "rows");
        }
    }

    public string RowsCaption => grid is null || VisibleRows.Count == items.Count
        ? ""
        : string.Format(CultureInfo.CurrentCulture, "{0:N0} of {1:N0} rows match.", VisibleRows.Count, items.Count);

    public string UnplacedTitle => Unplaced.Count == 1
        ? "1 value of the edits mod is not in the grid"
        : $"{Unplaced.Count} values of the edits mod are not in the grid";

    // Raised when a new grid is open, before its rows show, so the view
    // makes its columns once per table.
    public event EventHandler? GridChanged;

    // Opens file's grid, with the row whose id is rowId selected.
    public async Task LoadAsync(string table, ulong? rowId)
    {
        int load = ++version;
        Show(null);
        File = table;
        IsLoading = true;
        Error = null;
        TableGrid read = await session.GridAsync(table);
        if (load != version)
        {
            return;
        }
        IsLoading = false;
        if (!read.Ok)
        {
            Error = read.Error ?? $"{table} could not be read as a grid.";
            return;
        }
        Show(read);
        if (rowId is { } id)
        {
            SelectRow(id);
        }
    }

    // Closes the grid.
    public void Clear()
    {
        ++version;
        Show(null);
        File = "";
        IsLoading = false;
        Error = null;
    }

    // Selects the row with id, clearing filters that hide it.
    public void SelectRow(ulong id)
    {
        TableRowItem? found = items.FirstOrDefault(item => item.Row.Id == id);
        if (found is null)
        {
            Status = $"{File} has no row with the id {id}.";
            return;
        }
        if (!VisibleRows.Contains(found))
        {
            RowFilter = "";
            ChangedOnly = false;
        }
        SelectedRow = found;
    }

    // Asked when an edit starts with no edits mod chosen.
    public void NeedMod() =>
        Status = "Choose an edits mod in the bar above first. Cell edits are saved there, as patches.";

    // Checks text for a cell and writes it to the edits mod. False when it
    // was refused; the cell then keeps it, marked, and nothing is written.
    public async Task<bool> CommitAsync(TableRowItem row, int column, string text)
    {
        if (grid is not { } open || Edits.Current is not { } mod)
        {
            NeedMod();
            return false;
        }
        if (!row.IsRefused(column) && text == row.EditText(column))
        {
            return true;
        }
        IReadOnlySet<ulong>? ids = await RefIdsAsync(open, column);
        if (!ReferenceEquals(open, grid) || !ReferenceEquals(mod, Edits.Current))
        {
            return false;
        }
        if (TableEdits.Commit(mod, open, row.Position, column, text, ids) is { } refused)
        {
            row.Refuse(column, text, refused);
            Status = $"{open.Columns[column].Path} of row {row.IdText} was not changed. {refused}";
            return false;
        }
        row.Accept(column);
        Status = null;
        Edits.NotifyEdited();
        return true;
    }

    // Puts the game's value back in a cell.
    public void Revert(TableRowItem row, int column)
    {
        row.Accept(column);
        if (grid is not { } open || Edits.Current is not { } mod || Patches.At(row.Position, column) is null)
        {
            return;
        }
        Report(TableEdits.Revert(mod, open, row.Position, column), $"{open.Columns[column].Path} of row {row.IdText}");
    }

    // Puts the game's values back in every cell of a row.
    public void RevertRow(TableRowItem row)
    {
        if (grid is not { } open || Edits.Current is not { } mod || !row.IsPatched)
        {
            return;
        }
        Report(TableEdits.RevertRow(mod, open, row.Position), $"Row {row.IdText}");
    }

    [RelayCommand]
    private void ClearFilters()
    {
        RowFilter = "";
        ChangedOnly = false;
        ColumnFilter = "";
    }

    private void Report(string? problem, string what)
    {
        if (problem is not null)
        {
            Status = $"{what} could not be reverted. {problem}";
            return;
        }
        Status = null;
        Edits.NotifyEdited();
    }

    private void Show(TableGrid? next)
    {
        grid = next;
        refIds.Clear();
        SelectedRow = null;
        Status = null;
        VisibleRows = [];
        items = next is null ? [] : next.Rows.Select((row, index) => new TableRowItem(this, index, row)).ToList();
        GridColumns = next is null ? [] : Enumerable.Range(0, next.Columns.Count).Where(index => !IsRowKey(next, index)).ToList();
        Patches = TablePatches.Of(Edits.Current, next ?? new TableGrid());
        Unplaced = Patches.Unplaced.Select(note => new PatchNoteItem(note)).ToList();
        ShownColumns = TableFilter.Columns(Columns, ColumnFilter);
        foreach (string name in (string[])[nameof(Grid), nameof(Columns), nameof(GridColumns), nameof(HasGrid), nameof(Title),
                     nameof(Kind), nameof(Summary)])
        {
            OnPropertyChanged(name);
        }
        GridChanged?.Invoke(this, EventArgs.Empty);
        if (next is not null)
        {
            ApplyRowFilter();
        }
    }

    // The edits mod changed, here or on another page: the cells show its
    // values as they are now.
    private void Refresh()
    {
        if (grid is null)
        {
            OnPropertyChanged(nameof(Summary));
            return;
        }
        Patches = TablePatches.Of(Edits.Current, grid);
        Unplaced = Patches.Unplaced.Select(note => new PatchNoteItem(note)).ToList();
        foreach (TableRowItem item in items)
        {
            item.Refresh();
        }
        if (ChangedOnly)
        {
            ApplyRowFilter();
        }
        OnPropertyChanged(nameof(Summary));
    }

    private void OnEditsChanged(object? sender, PropertyChangedEventArgs change)
    {
        if (change.PropertyName == nameof(GameEdits.Current))
        {
            OnPropertyChanged(nameof(CanEdit));
            Refresh();
        }
    }

    partial void OnRowFilterChanged(string value) => ApplyRowFilter();

    partial void OnChangedOnlyChanged(bool value) => ApplyRowFilter();

    partial void OnColumnFilterChanged(string value) => ShownColumns = TableFilter.Columns(Columns, value);

    partial void OnVisibleRowsChanged(IReadOnlyList<TableRowItem> value) => OnPropertyChanged(nameof(RowsCaption));

    private void ApplyRowFilter()
    {
        if (grid is null)
        {
            return;
        }
        TableRowItem? selected = SelectedRow;
        VisibleRows = RowFilter.Trim().Length == 0 && !ChangedOnly
            ? items
            : TableFilter.Rows(grid, Patches, RowFilter, ChangedOnly).Select(index => items[index]).ToList();
        if (selected is not null && VisibleRows.Contains(selected))
        {
            SelectedRow = selected;
        }
    }

    // The values a reference column takes, null for any other column or
    // when the table it points into has no grid.
    private async Task<IReadOnlySet<ulong>?> RefIdsAsync(TableGrid open, int column)
    {
        if (open.Columns[column] is not { Type: "ref", Table: { } target })
        {
            return null;
        }
        if (refIds.TryGetValue(column, out IReadOnlySet<ulong>? known))
        {
            return known;
        }
        if (!targets.TryGetValue(target, out Task<IReadOnlyList<ulong>?>? ids))
        {
            ids = TargetIdsAsync(target);
            targets[target] = ids;
        }
        IReadOnlyList<ulong>? found = await ids;
        IReadOnlySet<ulong>? allowed = found is null ? null : TableEdits.RefIds(open, column, found);
        if (ReferenceEquals(open, grid))
        {
            refIds[column] = allowed;
        }
        return allowed;
    }

    // The row ids of table and of its framework_ twin, null when the
    // table has no grid.
    private async Task<IReadOnlyList<ulong>?> TargetIdsAsync(string table)
    {
        TableGrid target = await session.GridAsync(table);
        if (!target.Ok)
        {
            return null;
        }
        List<ulong> ids = target.Rows.Select(row => row.Id).ToList();
        if (!table.StartsWith("framework_", StringComparison.OrdinalIgnoreCase))
        {
            TableGrid twin = await session.GridAsync("framework_" + table);
            if (twin.Ok)
            {
                ids.AddRange(twin.Rows.Select(row => row.Id));
            }
        }
        return ids;
    }

    // True for the column of the row's own id or enum name, which the
    // grid shows as its fixed Id and Enum name columns.
    private static bool IsRowKey(TableGrid table, int column)
    {
        string type = table.Columns[column].Type;
        if (type is not ("id" or "enum"))
        {
            return false;
        }
        return table.Rows.Where(row => row.Fits).All(row => row.Values[column] is not { } value ||
            value == (type == "id" ? row.Id.ToString(CultureInfo.InvariantCulture) : row.Enum ?? ""));
    }
}
