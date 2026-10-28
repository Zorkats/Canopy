using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.Services;

// The open game folder and what has been read from it. canopy_core.dll keeps
// nothing between calls, so the file list and the table grids are cached
// here, once per folder. Every DLL call runs off the UI thread.
public sealed class GameSession(SettingsStore store) : ObservableObject
{
    private readonly Dictionary<string, Task<TableGrid>> grids = new(StringComparer.OrdinalIgnoreCase);
    private GameInfo? game;
    private Task<FileList>? files;

    public StudioSettings Settings { get; private set; } = store.Load();

    public GameInfo? Game
    {
        get => game;
        private set
        {
            if (SetProperty(ref game, value))
            {
                OnPropertyChanged(nameof(HasGame));
                OnPropertyChanged(nameof(Folder));
            }
        }
    }

    // True once a folder whose archives open is in use.
    public bool HasGame => game is { Ok: true };

    public string? Folder => HasGame ? game!.Folder : null;

    // After Open switched folders: anything read from the old one is stale.
    public event EventHandler? GameChanged;

    public static Task<GameInfo> InspectAsync(string folder) => Task.Run(() => CanopyCore.Game(folder));

    // Uses info's folder from now on. remember stores it as the user's
    // choice; a folder opened from the settings is already stored.
    public void Open(GameInfo info, bool remember)
    {
        Game = info;
        files = null;
        grids.Clear();
        if (remember)
        {
            Remember(current => current with { GameFolder = info.Folder });
        }
        GameChanged?.Invoke(this, EventArgs.Empty);
    }

    public Task<FileList> FilesAsync()
    {
        string? folder = Folder;
        if (folder is null)
        {
            return Task.FromResult(new FileList { Error = NoGame });
        }
        return files ??= Task.Run(() => CanopyCore.Files(folder));
    }

    public Task<TableInfo> TableAsync(string file)
    {
        string? folder = Folder;
        return folder is null
            ? Task.FromResult(new TableInfo { Error = NoGame })
            : Task.Run(() => CanopyCore.Table(folder, file));
    }

    public Task<RowInfo> RowAsync(string file, int index)
    {
        string? folder = Folder;
        return folder is null
            ? Task.FromResult(new RowInfo { Error = NoGame })
            : Task.Run(() => CanopyCore.Row(folder, file, index));
    }

    // The grid of a table with a schema, read once per folder: the Tables,
    // Text and Art pages share it. Called on the UI thread.
    public Task<TableGrid> GridAsync(string file)
    {
        string? folder = Folder;
        if (folder is null)
        {
            return Task.FromResult(new TableGrid { Error = NoGame });
        }
        if (!grids.TryGetValue(file, out Task<TableGrid>? grid))
        {
            grid = Task.Run(() => CanopyCore.Grid(folder, file));
            grids[file] = grid;
        }
        return grid;
    }

    public Task<(TextureImage? Image, string? Error)> TextureAsync(string gamePath)
    {
        string? folder = Folder;
        if (folder is null)
        {
            return Task.FromResult<(TextureImage?, string?)>((null, NoGame));
        }
        return Task.Run(() =>
        {
            TextureImage? image = CanopyCore.Texture(folder, gamePath, out string? error);
            return (image, error);
        });
    }

    // Changes the settings and writes them out. False when the file could
    // not be written; the change still holds until Canopy Studio closes.
    public bool Remember(Func<StudioSettings, StudioSettings> change)
    {
        Settings = change(Settings);
        OnPropertyChanged(nameof(Settings));
        return store.Save(Settings);
    }

    private const string NoGame = "No game folder is open.";
}
