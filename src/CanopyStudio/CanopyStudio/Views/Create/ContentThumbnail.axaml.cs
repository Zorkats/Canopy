using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.VisualTree;
using CanopyStudio.Core.Create;
using CanopyStudio.Services;

namespace CanopyStudio.Views.Create;

// A tile's picture in the Create gallery. It asks for its picture only while on
// screen, and drops a load it no longer needs when the tile is reused or
// leaves the screen. Until a picture shows, it shows the kind's glyph.
public sealed partial class ContentThumbnail : UserControl
{
    public static readonly StyledProperty<TileArt?> ArtProperty =
        AvaloniaProperty.Register<ContentThumbnail, TileArt?>(nameof(Art));

    public static readonly StyledProperty<ContentImages?> ImagesProperty =
        AvaloniaProperty.Register<ContentThumbnail, ContentImages?>(nameof(Images));

    private CancellationTokenSource? cancel;
    private TileArt? shown;
    private bool attached;

    public ContentThumbnail() => InitializeComponent();

    public TileArt? Art
    {
        get => GetValue(ArtProperty);
        set => SetValue(ArtProperty, value);
    }

    public ContentImages? Images
    {
        get => GetValue(ImagesProperty);
        set => SetValue(ImagesProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == ArtProperty || change.Property == ImagesProperty)
        {
            Load();
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        attached = true;
        Load();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        attached = false;
        cancel?.Cancel();
        cancel = null;
    }

    private void Load()
    {
        if (Picture.Source is not null && ReferenceEquals(shown, Art))
        {
            return;
        }
        cancel?.Cancel();
        cancel = null;
        Show(null, null);
        GlyphText.Text = Art is { } glyph ? Capitalised(glyph.Glyph) : "";
        if (!attached || Images is not { } images || Art is not { Kind: not TileArtKind.Glyph } art)
        {
            return;
        }
        cancel = new CancellationTokenSource();
        _ = LoadAsync(images, art, cancel.Token);
    }

    private async Task LoadAsync(ContentImages images, TileArt art, CancellationToken token)
    {
        Bitmap? bitmap = await images.GetAsync(art, token);
        if (token.IsCancellationRequested || !ReferenceEquals(Art, art))
        {
            return;
        }
        Show(bitmap, art);
    }

    private void Show(Bitmap? bitmap, TileArt? art)
    {
        Picture.Source = bitmap;
        shown = bitmap is null ? null : art;
        Frame.IsVisible = bitmap is not null;
        Placeholder.IsVisible = bitmap is null;
    }

    // "item" becomes "Item", the word the placeholder shows.
    private static string Capitalised(string glyph) =>
        glyph.Length == 0 ? "" : char.ToUpperInvariant(glyph[0]) + glyph[1..];
}
