using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;

namespace CanopyStudio.Views.Game;

// A texture at a zoom: 0 fits it to the space (shrinking only), 1, 2 or 4
// shows each pixel as that many screen pixels, without smoothing.
public sealed partial class ArtCanvas : UserControl
{
    public static readonly StyledProperty<Bitmap?> SourceProperty =
        AvaloniaProperty.Register<ArtCanvas, Bitmap?>(nameof(Source));

    public static readonly StyledProperty<int> ZoomProperty =
        AvaloniaProperty.Register<ArtCanvas, int>(nameof(Zoom));

    public ArtCanvas()
    {
        InitializeComponent();
        Update();
    }

    public Bitmap? Source
    {
        get => GetValue(SourceProperty);
        set => SetValue(SourceProperty, value);
    }

    public int Zoom
    {
        get => GetValue(ZoomProperty);
        set => SetValue(ZoomProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == SourceProperty || change.Property == ZoomProperty)
        {
            Update();
        }
    }

    private void Update()
    {
        if (FitPicture is null)
        {
            return;
        }
        Bitmap? source = Source;
        bool fit = Zoom <= 0;
        FitPicture.Source = fit ? source : null;
        FitFrame.IsVisible = fit && source is not null;
        ZoomPicture.Source = fit ? null : source;
        Scroller.IsVisible = !fit && source is not null;
        if (!fit && source is not null)
        {
            // Pixel size, not the bitmap's DPI size, so 1x is 1:1.
            ZoomPicture.Width = source.PixelSize.Width * Zoom;
            ZoomPicture.Height = source.PixelSize.Height * Zoom;
        }
    }
}
