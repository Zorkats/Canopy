using Avalonia.Controls;
using Avalonia.Input;
using CanopyStudio.ViewModels.Create;

namespace CanopyStudio.Views.Create;

public sealed partial class ProblemsView : UserControl
{
    public ProblemsView()
    {
        InitializeComponent();
        // A double tap or Enter on a message shows it, like its Show button.
        List.DoubleTapped += (_, _) => ShowSelected();
        List.KeyDown += (_, key) =>
        {
            if (key.Key == Key.Enter)
            {
                key.Handled = ShowSelected();
            }
        };
    }

    private bool ShowSelected()
    {
        if (DataContext is not ProblemsViewModel problems || problems.Selected is not { CanShow: true } row)
        {
            return false;
        }
        problems.ShowCommand.Execute(row);
        return true;
    }
}
