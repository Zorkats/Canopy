using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Pickers;
using CanopyStudio.Services;

namespace CanopyStudio.Controls.Pickers;

// A searchable list of one kind of vanilla thing, with the game's icons and
// names, for choosing one by more than its number. Typing anywhere in it
// searches; Up and Down move through the list, Enter picks, Escape clears
// the search. A click picks too.
public sealed partial class VanillaPicker : UserControl
{
    public static readonly StyledProperty<VanillaKind> KindProperty =
        AvaloniaProperty.Register<VanillaPicker, VanillaKind>(nameof(Kind));

    public static readonly StyledProperty<IGameAssets?> AssetsProperty =
        AvaloniaProperty.Register<VanillaPicker, IGameAssets?>(nameof(Assets));

    public static readonly StyledProperty<ulong?> SelectedIdProperty =
        AvaloniaProperty.Register<VanillaPicker, ulong?>(nameof(SelectedId), defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<Func<ulong, string?>?> GroupLabelsProperty =
        AvaloniaProperty.Register<VanillaPicker, Func<ulong, string?>?>(nameof(GroupLabels));

    // Rows PageUp and PageDown move by.
    private const int PageStep = 10;

    private readonly VanillaPickerViewModel model = new();

    public VanillaPicker()
    {
        InitializeComponent();
        Root.DataContext = model;
        model.Picked += OnPicked;
        model.PropertyChanged += OnModelChanged;
        // Before the search box sees them, which would keep Up and Down.
        AddHandler(KeyDownEvent, OnPreviewKeyDown, RoutingStrategies.Tunnel);
        AddHandler(TextInputEvent, OnPreviewTextInput, RoutingStrategies.Tunnel);
        Rows.AddHandler(TappedEvent, OnRowTapped, RoutingStrategies.Bubble, handledEventsToo: true);
    }

    public VanillaKind Kind
    {
        get => GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    public IGameAssets? Assets
    {
        get => GetValue(AssetsProperty);
        set => SetValue(AssetsProperty, value);
    }

    // The picked entry's id. Kept when the list reloads, even when the new
    // list has no such entry.
    public ulong? SelectedId
    {
        get => GetValue(SelectedIdProperty);
        set => SetValue(SelectedIdProperty, value);
    }

    // The label of an item group, by itemcategory.dat id; null, or a null
    // answer, shows "Group <n>".
    public Func<ulong, string?>? GroupLabels
    {
        get => GetValue(GroupLabelsProperty);
        set => SetValue(GroupLabelsProperty, value);
    }

    // An entry was picked, by Enter or a click.
    public event EventHandler<PickerItem>? Picked;

    // Escape with an empty search: the host may close the picker.
    public event EventHandler? Cancelled;

    // Puts the caret in the search box with its text selected, so typing
    // starts a new search.
    public void FocusSearch() => Dispatcher.UIThread.Post(() =>
    {
        SearchBox.Focus();
        SearchBox.SelectAll();
    }, DispatcherPriority.Loaded);

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == KindProperty || change.Property == AssetsProperty)
        {
            model.Entries.Use(Assets, Kind);
        }
        else if (change.Property == SelectedIdProperty)
        {
            model.SelectedId = SelectedId;
        }
        else if (change.Property == GroupLabelsProperty)
        {
            model.GroupLabels = GroupLabels;
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        model.Entries.Attach();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        model.Entries.Detach();
    }

    private void OnPicked(object? sender, PickerItem item)
    {
        SetCurrentValue(SelectedIdProperty, item.Id);
        Picked?.Invoke(this, item);
    }

    private void OnModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(VanillaPickerViewModel.Highlighted) && model.Highlighted is { } item)
        {
            Rows.ScrollIntoView(item);
        }
    }

    private void OnPreviewKeyDown(object? sender, KeyEventArgs e)
    {
        // The group list keeps its own keys.
        if (InGroupBox(e.Source))
        {
            return;
        }
        switch (e.Key)
        {
            case Key.Down:
                model.Move(1);
                break;
            case Key.Up:
                model.Move(-1);
                break;
            case Key.PageDown:
                model.Move(PageStep);
                break;
            case Key.PageUp:
                model.Move(-PageStep);
                break;
            case Key.Home when e.Source is not TextBox:
                model.MoveToEnd(last: false);
                break;
            case Key.End when e.Source is not TextBox:
                model.MoveToEnd(last: true);
                break;
            case Key.Enter:
                model.Pick();
                break;
            case Key.Escape:
                if (string.IsNullOrEmpty(model.Search))
                {
                    Cancelled?.Invoke(this, EventArgs.Empty);
                    return;
                }
                model.Search = "";
                break;
            default:
                return;
        }
        e.Handled = true;
    }

    // Typing on the list goes to the search box.
    private void OnPreviewTextInput(object? sender, TextInputEventArgs e)
    {
        if (e.Source is TextBox || string.IsNullOrEmpty(e.Text) || InGroupBox(e.Source))
        {
            return;
        }
        model.Search = (model.Search ?? "") + e.Text;
        SearchBox.Focus();
        SearchBox.CaretIndex = model.Search.Length;
        e.Handled = true;
    }

    private void OnRowTapped(object? sender, TappedEventArgs e)
    {
        if (e.Source is Visual source && source.FindAncestorOfType<ListBoxItem>(includeSelf: true) is { DataContext: PickerItem item })
        {
            model.Highlighted = item;
            model.Pick();
        }
    }

    private bool InGroupBox(object? source) =>
        source is StyledElement element && (element == GroupBox || element.FindLogicalAncestorOfType<ComboBox>() == GroupBox);
}
