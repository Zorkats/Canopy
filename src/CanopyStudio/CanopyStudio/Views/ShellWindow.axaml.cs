using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Platform.Storage;
using CanopyStudio.Services;

namespace CanopyStudio.Views;

public sealed partial class ShellWindow : Window, IStudioDialogs
{
    public ShellWindow() => InitializeComponent();

    public async Task<string?> PickFolderAsync(string title)
    {
        IReadOnlyList<IStorageFolder> picked = await StorageProvider.OpenFolderPickerAsync(
            new FolderPickerOpenOptions { Title = title, AllowMultiple = false });
        return picked.Count > 0 ? picked[0].TryGetLocalPath() : null;
    }

    public async Task<IReadOnlyList<string>> PickFilesAsync(string title, string kind, IReadOnlyList<string> patterns,
        bool many = false)
    {
        IReadOnlyList<IStorageFile> picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = title,
            AllowMultiple = many,
            FileTypeFilter = [new FilePickerFileType(kind) { Patterns = [.. patterns] }],
        });
        return picked.Select(file => file.TryGetLocalPath()).OfType<string>().ToList();
    }

    // A small modal window in the app's own styles: the message, then
    // Cancel and the confirm button. Escape and closing count as Cancel.
    public async Task<bool> ConfirmAsync(string title, string message, string confirm)
    {
        Window dialog = new()
        {
            Title = title,
            Width = 460,
            SizeToContent = SizeToContent.Height,
            CanResize = false,
            ShowInTaskbar = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
        };
        Button yes = new() { Content = confirm, IsDefault = true, Classes = { "accent" } };
        Button no = new() { Content = "Cancel", IsCancel = true };
        yes.Click += (_, _) => dialog.Close(true);
        no.Click += (_, _) => dialog.Close(false);
        dialog.Content = new StackPanel
        {
            Margin = new(20),
            Spacing = 16,
            Children =
            {
                new TextBlock { Text = message, TextWrapping = Avalonia.Media.TextWrapping.Wrap },
                new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    Spacing = 8,
                    Children = { no, yes },
                },
            },
        };
        return await dialog.ShowDialog<bool?>(this) == true;
    }

    public async Task<bool> CopyTextAsync(string text)
    {
        if (Clipboard is not { } clipboard)
        {
            return false;
        }
        try
        {
            await clipboard.SetTextAsync(text);
            return true;
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException
            or System.Runtime.InteropServices.COMException)
        {
            return false;
        }
    }
}
