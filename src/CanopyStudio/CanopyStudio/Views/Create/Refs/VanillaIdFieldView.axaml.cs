using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using CanopyStudio.Services;
using CanopyStudio.ViewModels.Create.Refs;

namespace CanopyStudio.Views.Create.Refs;

// The view of a vanilla id field. "Open in Tables" goes through the window's
// navigator, which is the shell the window shows; with none, the button is
// hidden.
public sealed partial class VanillaIdFieldView : UserControl
{
    public VanillaIdFieldView() => InitializeComponent();

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        OpenButton.IsVisible = Navigator is not null;
    }

    private IStudioNavigator? Navigator => TopLevel.GetTopLevel(this)?.DataContext as IStudioNavigator;

    private void OnOpenInTables(object? sender, RoutedEventArgs e)
    {
        if (DataContext is VanillaIdFieldViewModel field && Navigator is { } navigator)
        {
            field.OpenInTables(navigator);
        }
    }
}
