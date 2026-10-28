using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CanopyStudio.ViewModels.Create.Refs;

namespace CanopyStudio.Views.Create.Refs;

// The list a reference field opens: search, the project's and other mods'
// keys, and what the game offers. Up and Down move, Enter or a click picks,
// Escape clears the search and then closes.
public sealed partial class RefPickerPanel : UserControl
{
    // Rows PageUp and PageDown move by.
    private const int PageStep = 10;

    public RefPickerPanel()
    {
        InitializeComponent();
        AddHandler(KeyDownEvent, OnPreviewKeyDown, RoutingStrategies.Tunnel);
        Rows.AddHandler(TappedEvent, OnRowTapped, RoutingStrategies.Bubble, handledEventsToo: true);
    }

    // A row was picked.
    public event EventHandler? Picked;

    // Escape with an empty search.
    public event EventHandler? Cancelled;

    private RefPickerModel? Model => DataContext as RefPickerModel;

    // Puts the caret in the search box with its text selected.
    public void FocusSearch() => Dispatcher.UIThread.Post(() =>
    {
        SearchBox.Focus();
        SearchBox.SelectAll();
    }, DispatcherPriority.Loaded);

    private void OnPreviewKeyDown(object? sender, KeyEventArgs e)
    {
        if (Model is not { } model)
        {
            return;
        }
        switch (e.Key)
        {
            case Key.Down:
                model.Move(1);
                break;
            case Key.Up:
                model.Move(-1);
                break;
            case Key.PageDown:
                model.Move(PageStep);
                break;
            case Key.PageUp:
                model.Move(-PageStep);
                break;
            case Key.Enter:
                Pick(model, model.Highlighted);
                break;
            case Key.Escape:
                if (string.IsNullOrEmpty(model.Search))
                {
                    Cancelled?.Invoke(this, EventArgs.Empty);
                    return;
                }
                model.Search = "";
                break;
            default:
                return;
        }
        if (model.Highlighted is { } highlighted)
        {
            Rows.ScrollIntoView(highlighted);
        }
        e.Handled = true;
    }

    private void OnRowTapped(object? sender, TappedEventArgs e)
    {
        if (Model is { } model && e.Source is Avalonia.Visual source
            && source.FindAncestorOfType<ListBoxItem>(includeSelf: true) is { DataContext: RefChoice choice })
        {
            Pick(model, choice);
        }
    }

    private void Pick(RefPickerModel model, RefChoice? choice)
    {
        if (choice is null)
        {
            return;
        }
        model.Pick(choice);
        Picked?.Invoke(this, EventArgs.Empty);
    }
}
