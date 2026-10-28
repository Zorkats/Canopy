using System.Globalization;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CanopyStudio.ViewModels.Game;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

public sealed record FieldItem(RowField Field, IRelayCommand? Go)
{
    public string Path => Field.Path;
    public string Type => Field.Type;
    public string Value => Field.Value;
    public bool IsEmpty => Field.Value.Length == 0;
    public string ShownValue => IsEmpty ? "(empty)" : Field.Value;
    public bool HasLink => Go is not null;
    public string LinkText => $"Open in {Field.Table}";
}

public sealed record TextItem(RowText Text)
{
    public string OffsetText => $"+0x{Text.Offset:x2}";
    public string Value => Text.Text;
}

// The game's tables (data/database/*.dat). A table with a schema is an
// editable grid (Grid), whose edits go to the edits mod as patches; any
// other shows row by row the texts its rows point at, marked as guessed,
// and the raw bytes.
public sealed partial class TablesViewModel : PageViewModel
{
    private readonly GameSession session;
    private readonly IStudioNavigator navigator;
    private IReadOnlyList<string> allTables = [];
    private TableInfo? table;
    private bool listLoaded;
    private bool listLoading;
    private string? pendingTable;
    private ulong? pendingRowId;
    private int tableVersion;
    private int rowVersion;

    [ObservableProperty]
    private string? listError;

    [ObservableProperty]
    private string tableFilter = "";

    [ObservableProperty]
    private IReadOnlyList<string> visibleTables = [];

    [ObservableProperty]
    private string? selectedTable;

    [ObservableProperty]
    private bool isTableLoading;

    [ObservableProperty]
    private string? tableError;

    [ObservableProperty]
    private string rowFilter = "";

    [ObservableProperty]
    private IReadOnlyList<TableRow> visibleRows = [];

    [ObservableProperty]
    private TableRow? selectedRow;

    [ObservableProperty]
    private bool isRowLoading;

    [ObservableProperty]
    private string? rowError;

    [ObservableProperty]
    private RowInfo? row;

    public TablesViewModel(GameSession session, GameEdits edits, IStudioNavigator navigator)
    {
        this.session = session;
        this.navigator = navigator;
        Edits = edits;
        Grid = new TableGridViewModel(session, edits);
        session.GameChanged += (_, _) => Reset();
        session.PropertyChanged += (_, _) => OnPropertyChanged(nameof(NeedsGame));
    }

    // Where cell edits are saved, as patches.
    public GameEdits Edits { get; }

    // The open table as a grid, when it has a schema.
    public TableGridViewModel Grid { get; }
    public bool ShowsGrid => table is { Ok: true, Schema: true };

    public override string Id => PageIds.Tables;
    public override string Title => "Tables";

    public bool NeedsGame => !session.HasGame;
    public bool IsListLoading => listLoading;
    public bool IsListReady => listLoaded;

    public bool HasTable => table is { Ok: true };
    public bool NoTableChosen => SelectedTable is null;
    public string TableTitle => table?.File ?? "";
    public string TableKind => table switch
    {
        { Schema: true, StructName: { } name } => $"Schema: {name}",
        { Schema: true } => "Schema",
        _ => "No schema",
    };
    public bool TableHasSchema => table is { Schema: true };
    public string TableSummary => table is null
        ? ""
        : string.Create(CultureInfo.CurrentCulture, $"{table.Rows.Count:N0} rows, {table.PoolBytes:N0} bytes of text");
    public string RowsCaption => table is null || VisibleRows.Count == table.Rows.Count
        ? ""
        : $"{VisibleRows.Count:N0} of {table.Rows.Count:N0} rows match.";

    public bool NoRowChosen => SelectedRow is null && HasTable;
    public bool HasRow => Row is { Ok: true };
    public string RowTitle => Row is { Ok: true } info ? $"Row {info.Index}" : "";
    public string RowSubtitle => Row is { Ok: true } info
        ? info.Enum is { Length: > 0 } name ? $"id {info.Id}, {name}, {info.Size} bytes" : $"id {info.Id}, {info.Size} bytes"
        : "";
    public IReadOnlyList<FieldItem> Fields { get; private set; } = [];
    public IReadOnlyList<TextItem> Texts { get; private set; } = [];
    public bool HasFields => Fields.Count > 0;
    public bool HasTexts => Texts.Count > 0;
    public bool NoTexts => HasRow && !HasFields && Texts.Count == 0;
    public string HexText { get; private set; } = "";

    public override void OnShown() => _ = LoadListAsync();

    // Shows file, and its row with id rowId when there is one.
    public void Open(string file, ulong? rowId)
    {
        pendingTable = file;
        pendingRowId = rowId;
        _ = LoadListAsync();
    }

    [RelayCommand]
    private void OpenSetup() => navigator.Navigate(PageIds.Setup);

    [RelayCommand]
    private void ClearRowFilter() => RowFilter = "";

    private async Task LoadListAsync()
    {
        if (!session.HasGame)
        {
            return;
        }
        if (listLoaded)
        {
            ApplyPending();
            return;
        }
        if (listLoading)
        {
            // The load under way applies the pending table when it ends.
            return;
        }
        listLoading = true;
        ListError = null;
        OnPropertyChanged(nameof(IsListLoading));
        FileList list = await session.FilesAsync();
        listLoading = false;
        OnPropertyChanged(nameof(IsListLoading));
        if (!list.Ok)
        {
            ListError = list.Error ?? "The table list could not be read.";
            return;
        }
        allTables = GamePaths.Tables(list.Files);
        listLoaded = true;
        OnPropertyChanged(nameof(IsListReady));
        RefreshTables();
        ApplyPending();
    }

    private void ApplyPending()
    {
        if (pendingTable is not { } file)
        {
            return;
        }
        pendingTable = null;
        TableFilter = "";
        if (SelectedTable == file && HasTable)
        {
            SelectPendingRow();
        }
        else
        {
            SelectedTable = file;
        }
    }

    private void Reset()
    {
        listLoaded = false;
        allTables = [];
        TableFilter = "";
        VisibleTables = [];
        SelectedTable = null;
        ListError = null;
        OnPropertyChanged(nameof(IsListReady));
    }

    partial void OnTableFilterChanged(string value) => RefreshTables();

    private void RefreshTables()
    {
        string filter = TableFilter.Trim();
        VisibleTables = filter.Length == 0
            ? allTables
            : allTables.Where(name => name.Contains(filter, StringComparison.OrdinalIgnoreCase)).ToList();
    }

    partial void OnSelectedTableChanged(string? value)
    {
        OnPropertyChanged(nameof(NoTableChosen));
        _ = LoadTableAsync(value);
    }

    private async Task LoadTableAsync(string? file)
    {
        int version = ++tableVersion;
        SetTable(null);
        TableError = null;
        IsTableLoading = file is not null;
        if (file is null)
        {
            return;
        }
        TableInfo info = await session.TableAsync(file);
        if (version != tableVersion)
        {
            return;
        }
        IsTableLoading = false;
        if (!info.Ok)
        {
            TableError = info.Error ?? "The table could not be read.";
            return;
        }
        SetTable(info);
        if (info.Schema)
        {
            ulong? rowId = pendingRowId;
            pendingRowId = null;
            await Grid.LoadAsync(info.File, rowId);
            return;
        }
        SelectPendingRow();
    }

    private void SetTable(TableInfo? info)
    {
        table = info;
        SelectedRow = null;
        RowFilter = "";
        VisibleRows = info?.Rows ?? [];
        if (info is not { Schema: true })
        {
            Grid.Clear();
        }
        foreach (string name in (string[])[nameof(HasTable), nameof(TableTitle), nameof(TableKind), nameof(TableHasSchema),
                     nameof(TableSummary), nameof(RowsCaption), nameof(NoRowChosen), nameof(ShowsGrid)])
        {
            OnPropertyChanged(name);
        }
    }

    private void SelectPendingRow()
    {
        if (pendingRowId is not { } id || table is null)
        {
            return;
        }
        pendingRowId = null;
        if (ShowsGrid)
        {
            Grid.SelectRow(id);
            return;
        }
        RowFilter = "";
        SelectedRow = table.Rows.FirstOrDefault(candidate => candidate.Id == id);
        if (SelectedRow is null)
        {
            RowError = $"{table.File} has no row with id {id}.";
        }
    }

    partial void OnRowFilterChanged(string value)
    {
        if (table is null)
        {
            return;
        }
        string filter = value.Trim();
        VisibleRows = filter.Length == 0
            ? table.Rows
            : table.Rows.Where(candidate =>
                candidate.Index.ToString(CultureInfo.InvariantCulture).Equals(filter, StringComparison.Ordinal) ||
                candidate.Id.ToString(CultureInfo.InvariantCulture).Contains(filter, StringComparison.Ordinal) ||
                (candidate.Enum?.Contains(filter, StringComparison.OrdinalIgnoreCase) ?? false)).ToList();
        OnPropertyChanged(nameof(RowsCaption));
    }

    partial void OnSelectedRowChanged(TableRow? value)
    {
        OnPropertyChanged(nameof(NoRowChosen));
        _ = LoadRowAsync(value);
    }

    private async Task LoadRowAsync(TableRow? chosen)
    {
        int version = ++rowVersion;
        Row = null;
        RowError = null;
        IsRowLoading = chosen is not null;
        if (chosen is null || table is null)
        {
            return;
        }
        RowInfo info = await session.RowAsync(table.File, chosen.Index);
        if (version != rowVersion)
        {
            return;
        }
        IsRowLoading = false;
        if (!info.Ok)
        {
            RowError = info.Error ?? "The row could not be read.";
            return;
        }
        Row = info;
    }

    partial void OnRowChanged(RowInfo? value)
    {
        Fields = value?.Fields.Select(field => new FieldItem(field, LinkOf(field))).ToList() ?? [];
        Texts = value?.Texts.Select(text => new TextItem(text)).ToList() ?? [];
        HexText = value is null ? "" : HexDump.Format(value.Hex);
        foreach (string name in (string[])[nameof(HasRow), nameof(RowTitle), nameof(RowSubtitle), nameof(Fields), nameof(Texts),
                     nameof(HasFields), nameof(HasTexts), nameof(NoTexts), nameof(HexText)])
        {
            OnPropertyChanged(name);
        }
    }

    // A ref field names the table its id points into.
    private RelayCommand? LinkOf(RowField field)
    {
        if (field.Table is not { } file ||
            !ulong.TryParse(field.Value, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id))
        {
            return null;
        }
        return new RelayCommand(() => Open(file, id));
    }
}
