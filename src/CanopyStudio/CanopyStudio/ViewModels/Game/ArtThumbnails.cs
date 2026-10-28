using Avalonia.Media.Imaging;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;

namespace CanopyStudio.ViewModels.Game;

// Thumbnails of the Art list, decoded only for the rows on screen. A texture
// is decoded whole and shrunk off the UI thread, two at a time; a row that
// scrolls away before its turn is dropped. The last Capacity thumbnails are
// kept, the least recently shown going first, so scrolling back is free and
// memory stays small (a thumbnail is at most Side x Side, 6.4 KB).
public sealed class ArtThumbnails
{
    public const int Side = 40;
    public const int Capacity = 300;

    private sealed record Kept(string Path, Bitmap Bitmap, int Width, int Height);

    private readonly GameSession session;
    private readonly SemaphoreSlim decoding = new(2, 2);
    private readonly Dictionary<string, LinkedListNode<Kept>> kept = new(StringComparer.OrdinalIgnoreCase);
    private readonly LinkedList<Kept> order = new();

    public ArtThumbnails(GameSession session)
    {
        this.session = session;
        // The list is read again for a new game, with new rows.
        session.GameChanged += (_, _) => Clear();
    }

    // The thumbnail of row's file; null when it has none, it could not be
    // decoded, or token was cancelled first. Records the decoded size on
    // row. Call on the UI thread.
    public async Task<Bitmap?> GetAsync(ArtRowViewModel row, CancellationToken token)
    {
        string path = row.File;
        if (path.Length == 0 || session.Folder is not { } folder)
        {
            return null;
        }
        if (!kept.ContainsKey(path))
        {
            (TextureImage? small, int width, int height) = await DecodeAsync(folder, path, token);
            if (small is null || session.Folder != folder)
            {
                return null;
            }
            // Kept even when its row has scrolled away, since the work is
            // done; another row with the same file may have kept it meanwhile.
            if (!kept.ContainsKey(path))
            {
                kept[path] = order.AddFirst(new Kept(path, TextureBitmaps.ToBitmap(small), width, height));
                while (order.Count > Capacity && order.Last is { } oldest)
                {
                    // Not disposed: a row may still be drawing it.
                    order.RemoveLast();
                    kept.Remove(oldest.Value.Path);
                }
            }
        }
        LinkedListNode<Kept> node = kept[path];
        order.Remove(node);
        order.AddFirst(node);
        row.Saw(node.Value.Width, node.Value.Height);
        return token.IsCancellationRequested ? null : node.Value.Bitmap;
    }

    private async Task<(TextureImage?, int, int)> DecodeAsync(string folder, string path, CancellationToken token)
    {
        try
        {
            await decoding.WaitAsync(token);
        }
        catch (OperationCanceledException)
        {
            return (null, 0, 0);
        }
        try
        {
            return await Task.Run(() =>
            {
                if (token.IsCancellationRequested)
                {
                    return ((TextureImage?)null, 0, 0);
                }
                TextureImage? image = CanopyCore.Texture(folder, path, out _);
                return image is null ? (null, 0, 0) : (ArtImages.Shrink(image, Side), image.Width, image.Height);
            }, CancellationToken.None);
        }
        finally
        {
            decoding.Release();
        }
    }

    private void Clear()
    {
        kept.Clear();
        order.Clear();
    }
}
