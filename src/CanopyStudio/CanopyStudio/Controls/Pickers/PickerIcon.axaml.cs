using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using CanopyStudio.Services;

namespace CanopyStudio.Controls.Pickers;

// A game icon at 32 x 32, scaled without smoothing so pixel art stays sharp.
// It loads only while on screen, so a long virtualized list asks only for
// the rows it shows. A row reused for another entry drops the load it had
// under way, and a load that ends for an id no longer shown is thrown away.
public sealed partial class PickerIcon : UserControl
{
    public static readonly StyledProperty<ulong?> IconIdProperty =
        AvaloniaProperty.Register<PickerIcon, ulong?>(nameof(IconId));

    public static readonly StyledProperty<IGameAssets?> AssetsProperty =
        AvaloniaProperty.Register<PickerIcon, IGameAssets?>(nameof(Assets));

    private CancellationTokenSource? cancel;
    private ulong? shownId;
    private bool attached;

    public PickerIcon() => InitializeComponent();

    // The icon.dat id to show; null shows the placeholder.
    public ulong? IconId
    {
        get => GetValue(IconIdProperty);
        set => SetValue(IconIdProperty, value);
    }

    public IGameAssets? Assets
    {
        get => GetValue(AssetsProperty);
        set => SetValue(AssetsProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == AssetsProperty)
        {
            if (attached)
            {
                Listen(change.GetOldValue<IGameAssets?>(), false);
                Listen(change.GetNewValue<IGameAssets?>(), true);
            }
            Load(force: true);
        }
        else if (change.Property == IconIdProperty)
        {
            Load(force: false);
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        attached = true;
        Listen(Assets, true);
        Load(force: false);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        attached = false;
        Listen(Assets, false);
        cancel?.Cancel();
        cancel = null;
    }

    private void Listen(IGameAssets? assets, bool on)
    {
        if (assets is null)
        {
            return;
        }
        assets.Changed -= OnAssetsChanged;
        if (on)
        {
            assets.Changed += OnAssetsChanged;
        }
    }

    // A new game may number its icons differently.
    private void OnAssetsChanged(object? sender, EventArgs e) => Dispatcher.UIThread.Post(() => Load(force: true));

    private void Load(bool force)
    {
        if (!force && Picture.Source is not null && shownId == IconId)
        {
            return;
        }
        cancel?.Cancel();
        cancel = null;
        Show(null, null);
        if (!attached || Assets is not { } assets || IconId is not { } id)
        {
            return;
        }
        cancel = new CancellationTokenSource();
        _ = LoadAsync(assets, id, cancel.Token);
    }

    private async Task LoadAsync(IGameAssets assets, ulong id, CancellationToken token)
    {
        Bitmap? bitmap;
        try
        {
            bitmap = await assets.IconAsync(id, token);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (token.IsCancellationRequested || IconId != id || !ReferenceEquals(Assets, assets))
        {
            return;
        }
        Show(bitmap, id);
    }

    private void Show(Bitmap? bitmap, ulong? id)
    {
        Picture.Source = bitmap;
        shownId = bitmap is null ? null : id;
        Placeholder.IsVisible = bitmap is null;
    }
}
