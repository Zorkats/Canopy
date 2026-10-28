using Avalonia.Media.Imaging;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.Services;

// IGameAssets over the vanilla cache, for the game GameSession has open.
// Names and icons are read from the cache when it has them and from the game
// otherwise, always off the UI thread; each list and icon is read once per
// game while Canopy Studio runs. A newly opened game is not cached by itself:
// first-time setup and the Setup page call PrepareAsync.
public sealed class GameAssetsService : ObservableObject, IGameAssets
{
    private const string NoGame = "No game folder is open. Choose the game folder on the Setup page first.";

    private readonly GameSession session;
    private readonly VanillaAssetCache cache;
    private readonly SemaphoreSlim building = new(1, 1);
    // The native fallback decodes a whole texture per icon, so only a few
    // run at once and the rest of the pool stays free.
    private readonly SemaphoreSlim cutting = new(2, 2);
    private readonly object gate = new();
    private readonly Dictionary<ulong, Task<Bitmap?>> icons = [];
    private readonly Dictionary<(VanillaKind Kind, string Language), Task<IReadOnlyList<VanillaEntry>>> entries = [];
    private bool isReady;
    private string language;

    public GameAssetsService(GameSession session, VanillaAssetCache cache)
    {
        this.session = session;
        this.cache = cache;
        language = Known(session.Settings.NamesLanguage) ?? VanillaKinds.DefaultLanguage;
        session.GameChanged += (_, _) => OnGameChanged();
        _ = RefreshReadyAsync();
    }

    public bool IsReady
    {
        get => isReady;
        private set => SetProperty(ref isReady, value);
    }

    // A code not in VanillaKinds.Languages is ignored.
    public string Language
    {
        get => language;
        set
        {
            if (Known(value) is not { } code || code == language)
            {
                return;
            }
            language = code;
            session.Remember(current => current with { NamesLanguage = code });
            OnPropertyChanged();
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public event EventHandler? Changed;

    // A cancelled read returns an empty list rather than throwing.
    public async Task<IReadOnlyList<VanillaEntry>> EntriesAsync(VanillaKind kind, CancellationToken cancel = default)
    {
        if (session.Game is not { Ok: true } game)
        {
            return [];
        }
        string folder = game.Folder;
        uint build = game.Build;
        (VanillaKind, string) key = (kind, language);
        Task<IReadOnlyList<VanillaEntry>> read;
        lock (gate)
        {
            if (!entries.TryGetValue(key, out read!))
            {
                read = Task.Run(() => ReadEntries(folder, build, key.Item1, key.Item2));
                entries[key] = read;
            }
        }
        try
        {
            return await read.WaitAsync(cancel);
        }
        catch (OperationCanceledException)
        {
            return [];
        }
    }

    // A cancelled wait returns null; the load goes on for the next caller.
    public async Task<Bitmap?> IconAsync(ulong iconId, CancellationToken cancel = default)
    {
        if (session.Game is not { Ok: true } game || iconId > uint.MaxValue)
        {
            return null;
        }
        string folder = game.Folder;
        uint build = game.Build;
        Task<Bitmap?> load;
        lock (gate)
        {
            if (!icons.TryGetValue(iconId, out load!))
            {
                load = Task.Run(() => LoadIconAsync(folder, build, iconId));
                icons[iconId] = load;
            }
        }
        try
        {
            return await load.WaitAsync(cancel);
        }
        catch (OperationCanceledException)
        {
            return null;
        }
    }

    // A cancel comes back as a result with Ok false; the old cache stays.
    public async Task<AssetPrepareResult> PrepareAsync(IProgress<string>? progress = null, CancellationToken cancel = default)
    {
        if (session.Game is not { Ok: true } game)
        {
            return new AssetPrepareResult(false, NoGame, []);
        }
        if (CanopyCore.Check() is { } problem)
        {
            return new AssetPrepareResult(false, problem, []);
        }
        string folder = game.Folder;
        uint build = game.Build;
        await building.WaitAsync(CancellationToken.None);
        try
        {
            AssetPrepareResult result = await Task.Run(() =>
                cache.Build(new NativeVanillaSource(folder), build, progress, cancel));
            if (result.Ok && IsOpen(folder, build))
            {
                // Icons that could not be made before may be in the cache now.
                int evicted = 0;
                lock (gate)
                {
                    foreach (ulong id in icons.Where(pair => pair.Value.IsCompletedSuccessfully && pair.Value.Result is null)
                                 .Select(pair => pair.Key).ToList())
                    {
                        icons.Remove(id);
                        ++evicted;
                    }
                }
                IsReady = true;
                if (evicted > 0)
                {
                    // Views showing a placeholder ask again.
                    Changed?.Invoke(this, EventArgs.Empty);
                }
            }
            return result;
        }
        finally
        {
            building.Release();
        }
    }

    private void OnGameChanged()
    {
        lock (gate)
        {
            // Not disposed: views may still be drawing the old game's icons.
            icons.Clear();
            entries.Clear();
        }
        IsReady = false;
        Changed?.Invoke(this, EventArgs.Empty);
        _ = RefreshReadyAsync();
    }

    private async Task RefreshReadyAsync()
    {
        if (session.Game is not { Ok: true } game)
        {
            IsReady = false;
            return;
        }
        string folder = game.Folder;
        uint build = game.Build;
        bool current = await Task.Run(() => cache.IsCurrent(build));
        if (IsOpen(folder, build))
        {
            IsReady = current;
        }
    }

    // A failed read is not kept, so the next call tries again.
    private IReadOnlyList<VanillaEntry> ReadEntries(string folder, uint build, VanillaKind kind, string code)
    {
        VanillaRead read = CanopyCore.Check() is null
            ? cache.Entries(new NativeVanillaSource(folder), build, kind, code)
            : new VanillaRead([], $"{CanopyCore.LibraryFile} cannot be used.");
        if (!read.Ok)
        {
            lock (gate)
            {
                entries.Remove((kind, code));
            }
        }
        return read.Entries;
    }

    // The cached PNG as a bitmap; when it is not cached yet, the icon cut
    // from the game, saved to the cache and returned. Null when it cannot be
    // made.
    private async Task<Bitmap?> LoadIconAsync(string folder, uint build, ulong id)
    {
        byte[]? png = Read(cache.IconPath(build, id));
        if (png is null)
        {
            if (CanopyCore.Check() is not null)
            {
                return null;
            }
            await cutting.WaitAsync();
            try
            {
                // Another caller may have cut it while this one waited.
                png = Read(cache.IconPath(build, id));
                if (png is null)
                {
                    TextureImage? image = CanopyCore.Icon(folder, (uint)id, out _);
                    if (image is null)
                    {
                        return null;
                    }
                    png = Read(cache.SaveIcon(build, id, image)) ?? PngWriter.Encode(image);
                }
            }
            finally
            {
                cutting.Release();
            }
        }
        try
        {
            return new Bitmap(new MemoryStream(png));
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException or IOException)
        {
            return null;
        }
    }

    // The file's bytes, read into memory so it is not held open; null when
    // there is no file or it cannot be read.
    private static byte[]? Read(string? path)
    {
        if (path is null)
        {
            return null;
        }
        try
        {
            return File.ReadAllBytes(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private bool IsOpen(string folder, uint build) =>
        session.Game is { Ok: true } game && game.Folder == folder && game.Build == build;

    private static string? Known(string? code) =>
        VanillaKinds.Languages.FirstOrDefault(language => string.Equals(language.Code, code, StringComparison.OrdinalIgnoreCase))?.Code;
}
