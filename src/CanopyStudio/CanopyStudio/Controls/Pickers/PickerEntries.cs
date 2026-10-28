using System.ComponentModel;
using Avalonia.Threading;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Pickers;
using CanopyStudio.Services;

namespace CanopyStudio.Controls.Pickers;

// One kind's entries from the asset service, kept current: they load again
// when the game or the language changes or the cache becomes ready. It
// loads and listens only while its control is on screen, so the service
// never keeps a closed picker alive.
internal sealed class PickerEntries
{
    private IGameAssets? assets;
    private VanillaKind kind;
    private bool attached;
    private bool stale = true;
    private CancellationTokenSource? cancel;
    private int version;

    // The entries as a searchable list; null before the first load ends and
    // when there is no asset service.
    public PickerQuery? Query { get; private set; }

    public bool IsLoading { get; private set; }

    // True when loading ended with nothing to pick: no game, or its tables
    // could not be read.
    public bool IsEmpty => !IsLoading && (Query?.Count ?? 0) == 0;

    // Query or IsLoading changed.
    public event EventHandler? Updated;

    public void Use(IGameAssets? newAssets, VanillaKind newKind)
    {
        if (ReferenceEquals(assets, newAssets) && kind == newKind)
        {
            return;
        }
        Listen(false);
        assets = newAssets;
        kind = newKind;
        Listen(attached);
        Reload(keep: false);
    }

    public void Attach()
    {
        attached = true;
        Listen(true);
        // What was read before may be out of date: refresh it, but keep
        // showing it meanwhile so reopening a picker does not flicker.
        Reload(keep: !stale);
    }

    public void Detach()
    {
        attached = false;
        Listen(false);
        Cancel();
        if (IsLoading)
        {
            IsLoading = false;
            stale = true;
        }
    }

    private void Listen(bool on)
    {
        if (assets is null)
        {
            return;
        }
        assets.Changed -= OnChanged;
        assets.PropertyChanged -= OnPropertyChanged;
        if (on)
        {
            assets.Changed += OnChanged;
            assets.PropertyChanged += OnPropertyChanged;
        }
    }

    // The service may raise these from any thread.
    private void OnChanged(object? sender, EventArgs e) => Dispatcher.UIThread.Post(() => Reload(keep: false));

    private void OnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is null or "" or nameof(IGameAssets.IsReady))
        {
            Dispatcher.UIThread.Post(() => Reload(keep: true));
        }
    }

    private void Cancel()
    {
        cancel?.Cancel();
        cancel = null;
        ++version;
    }

    private void Reload(bool keep)
    {
        Cancel();
        if (!keep)
        {
            Query = null;
        }
        if (assets is null)
        {
            Query = null;
            IsLoading = false;
            stale = false;
            Updated?.Invoke(this, EventArgs.Empty);
            return;
        }
        stale = true;
        if (!attached)
        {
            // Loads when it comes on screen.
            IsLoading = false;
            Updated?.Invoke(this, EventArgs.Empty);
            return;
        }
        cancel = new CancellationTokenSource();
        _ = LoadAsync(assets, kind, version, cancel.Token);
    }

    private async Task LoadAsync(IGameAssets source, VanillaKind wanted, int mine, CancellationToken token)
    {
        IsLoading = true;
        Updated?.Invoke(this, EventArgs.Empty);
        PickerQuery query;
        try
        {
            IReadOnlyList<VanillaEntry> entries = await source.EntriesAsync(wanted, token);
            // Folding and sorting a few thousand names is quick, but it is
            // not the UI thread's work.
            query = await Task.Run(() => new PickerQuery(entries), token);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (mine != version)
        {
            return;
        }
        cancel = null;
        stale = false;
        Query = query;
        IsLoading = false;
        Updated?.Invoke(this, EventArgs.Empty);
    }
}
