using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Create;

namespace CanopyStudio.Services;

// The pictures of the Create gallery's tiles: a picture file of the mod,
// cropped to its rect and shrunk, or a game icon. Each is decoded off the UI
// thread and kept until the game or the language changes. Tiles share a
// picture, so callers must not dispose one. A load runs to its end even when
// its caller stops waiting, so the next caller finds it done.
public sealed class ContentImages : IDisposable
{
    // The longer side of a picture, in pixels. A larger picture is shrunk to it.
    public const int MaxSide = 128;

    private readonly IGameAssets assets;
    private readonly object gate = new();
    private readonly Dictionary<TileArt, (DateTime Stamp, Task<Bitmap?> Load)> loads = new();
    private readonly Dictionary<VanillaKind, Task<IReadOnlyList<VanillaEntry>>> lists = new();
    private bool disposed;

    public ContentImages(IGameAssets assets)
    {
        this.assets = assets;
        assets.Changed += OnAssetsChanged;
    }

    // The picture of art, or null when it has none, cannot be read, or cancel
    // fires first. A picture file is decoded again once its write time changes.
    public async Task<Bitmap?> GetAsync(TileArt art, CancellationToken cancel)
    {
        DateTime stamp = StampOf(art);
        Task<Bitmap?> load;
        lock (gate)
        {
            if (disposed)
            {
                return null;
            }
            if (loads.TryGetValue(art, out (DateTime Stamp, Task<Bitmap?> Load) known) && known.Stamp == stamp)
            {
                load = known.Load;
            }
            else
            {
                load = Load(art);
                loads[art] = (stamp, load);
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

    public void Dispose()
    {
        assets.Changed -= OnAssetsChanged;
        lock (gate)
        {
            disposed = true;
            loads.Clear();
            lists.Clear();
        }
    }

    // Starts the load of art on the thread pool. It never faults: a picture
    // that cannot be read is null.
    private Task<Bitmap?> Load(TileArt art) => art switch
    {
        { Kind: TileArtKind.ModImage, File: { } file } => Task.Run(() => DecodeFile(file, art.Rect)),
        { Kind: TileArtKind.VanillaIcon, Id: { } id } => Task.Run(() => IconAsync(id)),
        { Kind: TileArtKind.VanillaItem, Id: { } id } => Task.Run(() => EntryIconAsync(VanillaKind.Item, id)),
        { Kind: TileArtKind.VanillaVillager, Id: { } id } => Task.Run(() => EntryIconAsync(VanillaKind.Villager, id)),
        _ => Task.FromResult<Bitmap?>(null),
    };

    // The game's icon with id, or null when the game has none.
    private async Task<Bitmap?> IconAsync(ulong id)
    {
        try
        {
            return await assets.IconAsync(id);
        }
        catch (Exception)
        {
            // A game icon that cannot be read shows as its glyph, so one bad
            // icon cannot break the gallery.
            return null;
        }
    }

    // The icon of the game's entry of kind with id, found in its list. Null
    // when there is no such entry or it has no icon.
    private async Task<Bitmap?> EntryIconAsync(VanillaKind kind, ulong id)
    {
        try
        {
            IReadOnlyList<VanillaEntry> entries = await EntriesOf(kind);
            VanillaEntry? entry = entries.FirstOrDefault(known => known.Id == id);
            if (entry?.Icon is not { } icon)
            {
                return null;
            }
            return await IconAsync(icon);
        }
        catch (Exception)
        {
            // An entry list or icon that cannot be read shows as its glyph, so
            // one bad entry cannot break the gallery.
            return null;
        }
    }

    // The game's entries of kind, read once until the game or the language changes.
    private Task<IReadOnlyList<VanillaEntry>> EntriesOf(VanillaKind kind)
    {
        lock (gate)
        {
            if (!lists.TryGetValue(kind, out Task<IReadOnlyList<VanillaEntry>>? list))
            {
                list = assets.EntriesAsync(kind);
                lists[kind] = list;
            }
            return list;
        }
    }

    private void OnAssetsChanged(object? sender, EventArgs e)
    {
        lock (gate)
        {
            loads.Clear();
            lists.Clear();
        }
    }

    // Decodes the picture at path, cropped to rect when it has one, and shrinks
    // it. Null when the file is missing, is not a picture, or rect is not
    // inside it.
    private static Bitmap? DecodeFile(string path, TileRect? rect)
    {
        try
        {
            using Bitmap source = new(path);
            PixelSize size = source.PixelSize;
            if (size.Width <= 0 || size.Height <= 0)
            {
                return null;
            }
            if (rect is null)
            {
                return Shrink(source);
            }
            // The edges are summed as wide numbers, so a rect that runs past the
            // picture is refused rather than cut short.
            if (rect.X < 0 || rect.Y < 0 || rect.W <= 0 || rect.H <= 0
                || (long)rect.X + rect.W > size.Width || (long)rect.Y + rect.H > size.Height)
            {
                return null;
            }
            // The crop is copied in the decoder's own pixel format, which
            // differs between platforms, so no conversion is needed and no
            // render target is used off the UI thread.
            if (source.Format is not { } format || source.AlphaFormat is not { } alpha)
            {
                return null;
            }
            using WriteableBitmap cropped = new(new PixelSize(rect.W, rect.H), source.Dpi, format, alpha);
            using (ILockedFramebuffer frame = cropped.Lock())
            {
                source.CopyPixels(new PixelRect(rect.X, rect.Y, rect.W, rect.H), frame.Address,
                    frame.RowBytes * rect.H, frame.RowBytes);
            }
            return Shrink(cropped);
        }
        catch (Exception)
        {
            // A picture the decoder cannot read shows as its glyph, so one bad
            // file cannot break the gallery.
            return null;
        }
    }

    // A copy of image with its longer side at most MaxSide, scaled with high
    // quality.
    private static Bitmap Shrink(Bitmap image)
    {
        PixelSize size = image.PixelSize;
        double scale = Math.Min(1.0, (double)MaxSide / Math.Max(size.Width, size.Height));
        PixelSize target = new(Math.Max(1, (int)Math.Round(size.Width * scale)),
            Math.Max(1, (int)Math.Round(size.Height * scale)));
        return image.CreateScaledBitmap(target, BitmapInterpolationMode.HighQuality);
    }

    // The write time of a picture file, so an edited picture is decoded again.
    // A tile with no file, or a file that cannot be read, has the minimum time.
    private static DateTime StampOf(TileArt art)
    {
        if (art.Kind != TileArtKind.ModImage || art.File is not { } file)
        {
            return DateTime.MinValue;
        }
        try
        {
            return File.GetLastWriteTimeUtc(file);
        }
        catch (Exception exception) when (exception is ArgumentException or IOException or NotSupportedException
            or UnauthorizedAccessException)
        {
            return DateTime.MinValue;
        }
    }
}
