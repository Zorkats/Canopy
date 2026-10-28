using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using CanopyStudio.ViewModels.Game;

namespace CanopyStudio.Views.Game;

// A texture's thumbnail in the Art list. It asks for its row's thumbnail
// only while on screen, so the virtualized list decodes only the rows it
// shows; a row reused for another texture drops the load it had under way.
public sealed partial class ArtThumbnail : UserControl
{
    public static readonly StyledProperty<ArtRowViewModel?> RowProperty =
        AvaloniaProperty.Register<ArtThumbnail, ArtRowViewModel?>(nameof(Row));

    public static readonly StyledProperty<ArtThumbnails?> SourceProperty =
        AvaloniaProperty.Register<ArtThumbnail, ArtThumbnails?>(nameof(Source));

    private CancellationTokenSource? cancel;
    private ArtRowViewModel? shown;
    private bool attached;

    public ArtThumbnail() => InitializeComponent();

    public ArtRowViewModel? Row
    {
        get => GetValue(RowProperty);
        set => SetValue(RowProperty, value);
    }

    public ArtThumbnails? Source
    {
        get => GetValue(SourceProperty);
        set => SetValue(SourceProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == RowProperty || change.Property == SourceProperty)
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
        if (Picture.Source is not null && ReferenceEquals(shown, Row))
        {
            return;
        }
        cancel?.Cancel();
        cancel = null;
        Show(null, null);
        if (!attached || Source is not { } source || Row is not { HasFile: true } row)
        {
            return;
        }
        cancel = new CancellationTokenSource();
        _ = LoadAsync(source, row, cancel.Token);
    }

    private async Task LoadAsync(ArtThumbnails source, ArtRowViewModel row, CancellationToken token)
    {
        Bitmap? bitmap = await source.GetAsync(row, token);
        if (token.IsCancellationRequested || !ReferenceEquals(Row, row))
        {
            return;
        }
        Show(bitmap, row);
    }

    private void Show(Bitmap? bitmap, ArtRowViewModel? row)
    {
        Picture.Source = bitmap;
        shown = bitmap is null ? null : row;
        Frame.IsVisible = bitmap is not null;
        Placeholder.IsVisible = bitmap is null;
    }
}
