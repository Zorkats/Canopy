using Avalonia;
using Avalonia.Controls;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Pickers;
using CanopyStudio.Services;

namespace CanopyStudio.Controls.Pickers;

// A compact form field for a vanilla id: shows the picked entry and opens a
// VanillaPicker in a flyout. Picking closes the flyout and sets SelectedId;
// Clear sets it to null.
public sealed partial class VanillaPickerButton : UserControl
{
    public static readonly StyledProperty<VanillaKind> KindProperty =
        VanillaPicker.KindProperty.AddOwner<VanillaPickerButton>();

    public static readonly StyledProperty<IGameAssets?> AssetsProperty =
        VanillaPicker.AssetsProperty.AddOwner<VanillaPickerButton>();

    // Two-way by default, as the picker's is.
    public static readonly StyledProperty<ulong?> SelectedIdProperty =
        VanillaPicker.SelectedIdProperty.AddOwner<VanillaPickerButton>();

    public static readonly StyledProperty<Func<ulong, string?>?> GroupLabelsProperty =
        VanillaPicker.GroupLabelsProperty.AddOwner<VanillaPickerButton>();

    private readonly VanillaPickerButtonViewModel model = new();
    private readonly VanillaPicker picker;
    private readonly Flyout flyout;

    public VanillaPickerButton()
    {
        InitializeComponent();
        Field.DataContext = model;
        ClearButton.DataContext = model;
        ClearButton.Click += (_, _) => Clear();
        picker = new VanillaPicker { Width = 400, Height = 440 };
        flyout = new Flyout { Content = picker, Placement = PlacementMode.BottomEdgeAlignedLeft };
        flyout.Opened += (_, _) =>
        {
            picker.SelectedId = SelectedId;
            picker.FocusSearch();
        };
        picker.Picked += OnPicked;
        picker.Cancelled += (_, _) => Close();
        Field.Flyout = flyout;
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

    // The picked id, or null for none. An id the game's list lacks is kept
    // and shown as such.
    public ulong? SelectedId
    {
        get => GetValue(SelectedIdProperty);
        set => SetValue(SelectedIdProperty, value);
    }

    public Func<ulong, string?>? GroupLabels
    {
        get => GetValue(GroupLabelsProperty);
        set => SetValue(GroupLabelsProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == KindProperty || change.Property == AssetsProperty)
        {
            model.UseAssets(Assets);
            model.Entries.Use(Assets, Kind);
            picker.Kind = Kind;
            picker.Assets = Assets;
        }
        else if (change.Property == SelectedIdProperty)
        {
            model.SelectedId = SelectedId;
            picker.SelectedId = SelectedId;
        }
        else if (change.Property == GroupLabelsProperty)
        {
            picker.GroupLabels = GroupLabels;
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

    // Sets the id to none, as the Clear button does.
    public void Clear()
    {
        SetCurrentValue(SelectedIdProperty, null);
        Field.Focus();
    }

    private void OnPicked(object? sender, PickerItem item)
    {
        SetCurrentValue(SelectedIdProperty, item.Id);
        Close();
    }

    private void Close()
    {
        flyout.Hide();
        Field.Focus();
    }
}
