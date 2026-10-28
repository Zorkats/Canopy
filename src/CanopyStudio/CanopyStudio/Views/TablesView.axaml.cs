using Avalonia.Controls;

namespace CanopyStudio.Views;

public sealed partial class TablesView : UserControl
{
    public TablesView()
    {
        InitializeComponent();
        // A row picked from elsewhere (a link from another table) may be far
        // down the list.
        RowsGrid.SelectionChanged += (_, _) =>
        {
            if (RowsGrid.SelectedItem is { } row)
            {
                RowsGrid.ScrollIntoView(row, null);
            }
        };
    }
}
