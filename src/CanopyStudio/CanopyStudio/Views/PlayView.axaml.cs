using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Platform.Storage;
using CanopyStudio.ViewModels;

namespace CanopyStudio.Views;

// Files dropped on the page are planned like files picked with Install.
public sealed partial class PlayView : UserControl
{
    public PlayView()
    {
        InitializeComponent();
        AddHandler(DragDrop.DragOverEvent, OnDragOver);
        AddHandler(DragDrop.DropEvent, OnDrop);
    }

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        e.DragEffects = DataContext is PlayViewModel { HasGame: true } && e.DataTransfer.Contains(DataFormat.File)
            ? DragDropEffects.Copy
            : DragDropEffects.None;
    }

    private async void OnDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not PlayViewModel { HasGame: true } page || e.DataTransfer.TryGetFiles() is not { } items)
        {
            return;
        }
        List<string> paths = items.Select(item => item.TryGetLocalPath()).OfType<string>().ToList();
        await page.PlanAsync(paths);
    }
}
