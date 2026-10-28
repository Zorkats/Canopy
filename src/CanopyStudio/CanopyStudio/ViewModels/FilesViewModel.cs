using System.Collections.ObjectModel;
using Avalonia.Media.Imaging;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

public sealed record FileItem(GameFile File, string Display)
{
    public string Name => GamePaths.FileName(File.Path);
    public string SizeText => GamePaths.SizeText(File.Size);
}

// Every file in the game's archives as a folder tree, with a search over all
// paths and a preview of the file picked.
public sealed partial class FilesViewModel : PageViewModel
{
    // More matches than this are not listed: nobody scrolls through them,
    // and the grid stays quick.
    public const int SearchCap = 2000;

    private readonly GameSession session;
    private readonly IStudioNavigator navigator;
    private IReadOnlyList<GameFile> all = [];
    private bool loaded;
    private int previewVersion;

    [ObservableProperty]
    private bool isLoading;

    [ObservableProperty]
    private string? loadError;

    [ObservableProperty]
    private FolderNode? selectedFolder;

    [ObservableProperty]
    private string searchText = "";

    [ObservableProperty]
    private IReadOnlyList<FileItem> visibleFiles = [];

    [ObservableProperty]
    private string listCaption = "";

    [ObservableProperty]
    private FileItem? selectedFile;

    [ObservableProperty]
    private Bitmap? previewImage;

    [ObservableProperty]
    private string previewCaption = "";

    [ObservableProperty]
    private bool isPreviewLoading;

    [ObservableProperty]
    private string? previewNote;

    public FilesViewModel(GameSession session, IStudioNavigator navigator)
    {
        this.session = session;
        this.navigator = navigator;
        session.GameChanged += (_, _) => Reset();
        session.PropertyChanged += (_, _) => OnPropertyChanged(nameof(NeedsGame));
    }

    public override string Id => PageIds.Files;
    public override string Title => "Files";

    public bool NeedsGame => !session.HasGame;
    public bool IsReady => loaded && LoadError is null;
    public ObservableCollection<FolderNode> Roots { get; } = [];

    public bool HasSelection => SelectedFile is not null;
    public bool NoSelection => SelectedFile is null;
    public bool IsTableFile => SelectedFile is { } item && GamePaths.IsTable(item.File.Path);
    public bool HasPreviewImage => PreviewImage is not null;
    public string SelectedSize => SelectedFile is { } item ? $"{GamePaths.SizeText(item.File.Size)} ({item.File.Size:N0} bytes)" : "";
    public string SelectedTime => SelectedFile is { File.Time: > 0 } item
        ? DateTimeOffset.FromUnixTimeSeconds(item.File.Time).ToLocalTime().ToString("g")
        : "Unknown";
    public string SelectedCopies => SelectedFile is { File.Copies: > 1 } item
        ? $"{item.File.Copies} archives hold this file; the game reads the one in {item.File.Archive}."
        : "";
    public bool HasCopies => SelectedFile is { File.Copies: > 1 };

    public override void OnShown()
    {
        if (!loaded && !IsLoading && session.HasGame)
        {
            _ = LoadAsync();
        }
    }

    [RelayCommand]
    private void OpenSetup() => navigator.Navigate(PageIds.Setup);

    [RelayCommand]
    private void OpenInTables()
    {
        if (SelectedFile is { } item && GamePaths.IsTable(item.File.Path))
        {
            navigator.OpenTable(GamePaths.FileName(item.File.Path));
        }
    }

    [RelayCommand]
    private void ClearSearch() => SearchText = "";

    private async Task LoadAsync()
    {
        IsLoading = true;
        LoadError = null;
        FileList list = await session.FilesAsync();
        FolderNode? top = list.Ok ? await Task.Run(() => FileTree.Build(list.Files, "Game files")) : null;
        IsLoading = false;
        if (top is null)
        {
            LoadError = list.Error ?? "The file list could not be read.";
            return;
        }
        all = list.Files;
        loaded = true;
        // Open the top and any folder that is the only one in its parent:
        // there is nothing to choose between until the tree branches.
        for (FolderNode? open = top; open is not null; open = open.Children.Count == 1 ? open.Children[0] : null)
        {
            open.IsExpanded = true;
        }
        Roots.Clear();
        Roots.Add(top);
        OnPropertyChanged(nameof(IsReady));
        SelectedFolder = top;
        RefreshList();
    }

    private void Reset()
    {
        loaded = false;
        all = [];
        Roots.Clear();
        SelectedFolder = null;
        SearchText = "";
        VisibleFiles = [];
        ListCaption = "";
        LoadError = null;
        OnPropertyChanged(nameof(IsReady));
    }

    partial void OnSelectedFolderChanged(FolderNode? value)
    {
        if (SearchText.Length == 0)
        {
            RefreshList();
        }
    }

    partial void OnSearchTextChanged(string value) => RefreshList();

    partial void OnSelectedFileChanged(FileItem? value)
    {
        foreach (string name in (string[])[nameof(HasSelection), nameof(NoSelection), nameof(IsTableFile),
                     nameof(SelectedSize), nameof(SelectedTime), nameof(SelectedCopies), nameof(HasCopies)])
        {
            OnPropertyChanged(name);
        }
        _ = PreviewAsync(value);
    }

    partial void OnPreviewImageChanged(Bitmap? value) => OnPropertyChanged(nameof(HasPreviewImage));

    private void RefreshList()
    {
        string search = SearchText.Trim();
        if (search.Length > 0)
        {
            List<FileItem> found = [];
            int matches = 0;
            foreach (GameFile file in all)
            {
                if (file.Path.Contains(search, StringComparison.OrdinalIgnoreCase) && ++matches <= SearchCap)
                {
                    found.Add(new FileItem(file, file.Path));
                }
            }
            VisibleFiles = found;
            ListCaption = matches switch
            {
                0 => $"No file path holds \"{search}\".",
                <= SearchCap => $"{matches:N0} files match \"{search}\".",
                _ => $"{matches:N0} files match \"{search}\". The first {SearchCap:N0} are listed; type more to narrow it down.",
            };
            return;
        }
        if (SelectedFolder is not { } folder)
        {
            VisibleFiles = [];
            ListCaption = "";
            return;
        }
        VisibleFiles = folder.Files.Select(file => new FileItem(file, GamePaths.FileName(file.Path))).ToList();
        string where = folder.Path.Length == 0 ? "The top folder" : folder.Path;
        ListCaption = folder.Files.Count == 0
            ? $"{where} holds only folders ({folder.TotalFiles:N0} files below)."
            : $"{where}: {folder.Files.Count:N0} files here, {folder.TotalFiles:N0} with its folders.";
    }

    private async Task PreviewAsync(FileItem? item)
    {
        int version = ++previewVersion;
        PreviewImage = null;
        PreviewCaption = "";
        PreviewNote = null;
        IsPreviewLoading = false;
        if (item is null)
        {
            return;
        }
        string path = item.File.Path;
        if (GamePaths.IsTable(path))
        {
            PreviewNote = "This is one of the game's tables. Open it to see its rows.";
            return;
        }
        if (!GamePaths.IsTexture(path))
        {
            PreviewNote = "Canopy Studio has no preview for this kind of file yet.";
            return;
        }
        IsPreviewLoading = true;
        (TextureImage? image, string? error) = await session.TextureAsync(path);
        if (version != previewVersion)
        {
            return;
        }
        IsPreviewLoading = false;
        if (image is null)
        {
            PreviewNote = error ?? "The texture could not be decoded.";
            return;
        }
        PreviewImage = TextureBitmaps.ToBitmap(image);
        PreviewCaption = $"{image.Width} x {image.Height} pixels";
    }
}
