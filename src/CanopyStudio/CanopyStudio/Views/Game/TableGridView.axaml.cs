using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.VisualTree;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.ViewModels;
using CanopyStudio.ViewModels.Game;

namespace CanopyStudio.Views.Game;

// The grid of a table with a schema. Its columns depend on the table, so
// they are made here, once per table, after the three fixed ones (index,
// id, enum name); each cell is a TableCellText that reads its row.
public sealed partial class TableGridView : UserControl
{
    private const int FixedColumns = 3;

    // A padlock, drawn on the header of a column a patch cannot change.
    private static readonly Geometry Lock = Geometry.Parse(
        "M3,7 H13 V15 H3 Z M4.5,7 V5 A3.5,3.5 0 0 1 11.5,5 V7 H10 V5 A2,2 0 0 0 6,5 V7 Z");

    // The fields that pick which builds a row is for, read only in every
    // table (gen_table_schemas.py BUILD_FIELDS).
    private static readonly HashSet<string> BuildFields =
        new(["version", "disableVersion", "region", "product", "platform"], StringComparer.OrdinalIgnoreCase);

    private TableGridViewModel? model;
    private TableRowItem? menuRow;
    private int? menuColumn;

    public TableGridView()
    {
        InitializeComponent();
        Cells.BeginningEdit += OnBeginningEdit;
        Cells.CellEditEnding += OnCellEditEnding;
        Cells.AddHandler(ContextRequestedEvent, OnContextRequested, RoutingStrategies.Tunnel);
        RevertCellItem.Click += (_, _) =>
        {
            if (model is not null && menuRow is not null && menuColumn is { } column)
            {
                model.Revert(menuRow, column);
            }
        };
        RevertRowItem.Click += (_, _) =>
        {
            if (model is not null && menuRow is not null)
            {
                model.RevertRow(menuRow);
            }
        };
        // A row picked from elsewhere (a link from another table) may be far
        // down the grid.
        Cells.SelectionChanged += (_, _) =>
        {
            if (Cells.SelectedItem is { } row)
            {
                Cells.ScrollIntoView(row, null);
            }
        };
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (model is not null)
        {
            model.GridChanged -= OnGridChanged;
            model.PropertyChanged -= OnModelChanged;
        }
        model = DataContext as TableGridViewModel;
        if (model is not null)
        {
            model.GridChanged += OnGridChanged;
            model.PropertyChanged += OnModelChanged;
        }
        MakeColumns();
    }

    private void OnGridChanged(object? sender, EventArgs e) => MakeColumns();

    private void OnModelChanged(object? sender, PropertyChangedEventArgs change)
    {
        if (change.PropertyName == nameof(TableGridViewModel.ShownColumns))
        {
            ShowColumns();
        }
    }

    private void MakeColumns()
    {
        while (Cells.Columns.Count > FixedColumns)
        {
            Cells.Columns.RemoveAt(Cells.Columns.Count - 1);
        }
        if (model?.Grid is not { } grid)
        {
            return;
        }
        foreach (int index in model.GridColumns)
        {
            GridColumn place = grid.Columns[index];
            bool number = FieldValueCheck.IsNumber(place.Type);
            bool editable = place.Editable && FieldValueCheck.IsWritable(place.Type);
            Cells.Columns.Add(new DataGridTemplateColumn
            {
                Header = Header(place, editable),
                Width = new DataGridLength(ColumnWidth(place)),
                IsReadOnly = !editable,
                Tag = index,
                CellTemplate = new FuncDataTemplate<TableRowItem>((_, _) => new TableCellText(index, number), true),
                CellEditingTemplate = new FuncDataTemplate<TableRowItem>((_, _) => new TableCellEditor(index, number)),
            });
        }
        ShowColumns();
    }

    private void ShowColumns()
    {
        if (model is null)
        {
            return;
        }
        IReadOnlyList<bool> shown = model.ShownColumns;
        foreach (DataGridColumn column in Cells.Columns.Skip(FixedColumns))
        {
            column.IsVisible = column.Tag is int index && index < shown.Count && shown[index];
        }
    }

    private void OnBeginningEdit(object? sender, DataGridBeginningEditEventArgs e)
    {
        if (model?.Grid is not { } grid || e.Row.DataContext is not TableRowItem row || e.Column.Tag is not int column)
        {
            e.Cancel = true;
            return;
        }
        if (!model.CanEdit)
        {
            e.Cancel = true;
            model.NeedMod();
            AskForMod();
            return;
        }
        if (TableEdits.Problem(grid, row.Position, column) is { } problem)
        {
            e.Cancel = true;
            model.Status = problem;
        }
    }

    private void OnCellEditEnding(object? sender, DataGridCellEditEndingEventArgs e)
    {
        if (e.EditAction != DataGridEditAction.Commit || model is null ||
            e.Row.DataContext is not TableRowItem row || e.EditingElement is not TableCellEditor editor)
        {
            return;
        }
        _ = model.CommitAsync(row, editor.Column, editor.Text ?? "");
    }

    // Right-click or the menu key: the menu acts on the cell under the
    // pointer, else on the current cell.
    private void OnContextRequested(object? sender, ContextRequestedEventArgs e)
    {
        DataGridCell? cell = (e.Source as Visual)?.FindAncestorOfType<DataGridCell>(includeSelf: true);
        menuRow = (cell?.DataContext ?? Cells.SelectedItem) as TableRowItem;
        menuColumn = cell is not null
            ? (cell.Content as TableCellText)?.Column
            : Cells.CurrentColumn?.Tag as int?;
        if (menuRow is not null && !ReferenceEquals(Cells.SelectedItem, menuRow))
        {
            Cells.SelectedItem = menuRow;
        }
        bool canEdit = model?.CanEdit ?? false;
        RevertCellItem.IsEnabled = canEdit && menuRow is not null && menuColumn is { } column &&
            model!.Patches.At(menuRow.Position, column) is not null;
        RevertRowItem.IsEnabled = canEdit && (menuRow?.IsPatched ?? false);
    }

    // The edits mod bar over the Game area asks the user to choose a mod.
    private void AskForMod()
    {
        if (this.FindAncestorOfType<GameAreaView>()?.DataContext is GameAreaViewModel area)
        {
            area.EditsBar.Ask();
        }
    }

    private static Control Header(GridColumn place, bool editable)
    {
        TextBlock name = new() { Text = place.Path, VerticalAlignment = VerticalAlignment.Center };
        StackPanel header = new() { Orientation = Orientation.Horizontal, Spacing = 5 };
        if (!editable)
        {
            PathIcon icon = new() { Data = Lock, VerticalAlignment = VerticalAlignment.Center };
            icon.Classes.Add("lock");
            header.Children.Add(icon);
        }
        header.Children.Add(name);
        ToolTip.SetTip(header, HeaderTip(place, editable));
        return header;
    }

    private static string HeaderTip(GridColumn place, bool editable)
    {
        string kind = place switch
        {
            { Type: "ref", Table: { } table } => $"the id of a row of {table}",
            { Type: "i32" or "i64" } => "a whole number",
            { Type: "u32" or "u64" } => "a whole number of at least 0",
            { Type: "f32" } => "a number",
            { Type: "bool" } => "0 or 1",
            { Type: "flag" } => "a flag id",
            { Type: "path" } => "a file path",
            { Type: "text" } => "a text",
            _ => place.Type,
        };
        string what = $"{place.Path}: {kind}.";
        if (editable)
        {
            return $"{what} Double-click or press F2 to edit.";
        }
        string why = place switch
        {
            { Type: "id" or "enum" } => "A patch never changes a row's id or enum name.",
            _ when place.Path.EndsWith(".count", StringComparison.OrdinalIgnoreCase) =>
                "The number of elements. A patch never adds or removes elements.",
            _ when place.Path.StartsWith("trailer.", StringComparison.OrdinalIgnoreCase) =>
                "The row's trailer, which Canopy leaves as the game has it.",
            _ when BuildFields.Contains(place.Path) =>
                "It tells the game which builds the row is for, so a patch never changes it.",
            _ => "What this field means is only guessed so far, so it is shown but not edited.",
        };
        return $"{what} Read only. {why}";
    }

    // Wide enough for the header, the lock and a typical value.
    private static double ColumnWidth(GridColumn place)
    {
        double value = place.Type switch
        {
            "text" or "path" => 180,
            "bool" => 56,
            "f32" => 80,
            _ => 90,
        };
        double header = place.Path.Length * 7.0 + 34;
        return Math.Clamp(Math.Max(value, header), 56, 240);
    }
}
