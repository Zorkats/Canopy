using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using CanopyStudio.ViewModels.Game;

namespace CanopyStudio.Views.Game;

// One cell of a table grid: the text of column in the row it is shown for.
// It reads the row when the grid hands it a row and when the row says its
// cells changed, so scrolling reuses cells without a view model per cell.
internal sealed class TableCellText : TextBlock
{
    private TableRowItem? row;
    private bool attached;

    public TableCellText(int column, bool number)
    {
        Column = column;
        Classes.Add("cell");
        Classes.Set("num", number);
    }

    // The grid column the cell shows.
    public int Column { get; }

    protected override Type StyleKeyOverride => typeof(TextBlock);

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        Watch(attached ? DataContext as TableRowItem : null);
        Show();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        attached = true;
        Watch(DataContext as TableRowItem);
        Show();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        attached = false;
        Watch(null);
    }

    private void Watch(TableRowItem? next)
    {
        if (ReferenceEquals(row, next))
        {
            return;
        }
        if (row is not null)
        {
            row.PropertyChanged -= OnRowChanged;
        }
        row = next;
        if (row is not null)
        {
            row.PropertyChanged += OnRowChanged;
        }
    }

    private void OnRowChanged(object? sender, PropertyChangedEventArgs change)
    {
        if (change.PropertyName == TableRowItem.CellsProperty)
        {
            Show();
        }
    }

    private void Show()
    {
        CellState state = (DataContext as TableRowItem)?.Cell(Column) ?? new CellState("", CellLook.Missing, null);
        Text = state.Text;
        Classes.Set("patched", state.Look == CellLook.Patched);
        Classes.Set("refused", state.Look == CellLook.Refused);
        Classes.Set("faint", state.Look == CellLook.Missing);
        ToolTip.SetTip(this, state.Tip);
    }
}

// The editor of one cell: a text box that starts with what the cell shows,
// focused with its text selected. The page reads its text when the edit is
// committed.
internal sealed class TableCellEditor : TextBox
{
    public TableCellEditor(int column, bool number)
    {
        Column = column;
        Classes.Add("cell");
        Classes.Set("num", number);
        AcceptsReturn = false;
    }

    public int Column { get; }

    protected override Type StyleKeyOverride => typeof(TextBox);

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        Text = (DataContext as TableRowItem)?.EditText(Column) ?? "";
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Dispatcher.UIThread.Post(() =>
        {
            Focus();
            SelectAll();
        });
    }
}
