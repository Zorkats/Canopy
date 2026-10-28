using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using CanopyStudio.ViewModels.Create.Refs;

namespace CanopyStudio.Views.Create.Refs;

// A reference value as a form field: the value with its icon and where it
// comes from, and the RefPickerPanel in a flyout on click. The game's list
// loads only while the field is on screen.
public sealed partial class RefPickerView : UserControl
{
    private readonly RefPickerPanel panel = new();
    private readonly Flyout flyout;
    private RefPickerModel? attachedModel;

    public RefPickerView()
    {
        InitializeComponent();
        flyout = new Flyout { Content = panel, Placement = PlacementMode.BottomEdgeAlignedLeft };
        flyout.Opened += (_, _) =>
        {
            panel.DataContext = DataContext;
            if (DataContext is RefPickerModel model)
            {
                model.Search = "";
            }
            panel.FocusSearch();
        };
        panel.Picked += (_, _) => Close();
        panel.Cancelled += (_, _) => Close();
        Field.Flyout = flyout;
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        panel.DataContext = DataContext;
        if (this.IsAttachedToVisualTree())
        {
            Use(DataContext as RefPickerModel);
        }
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Use(DataContext as RefPickerModel);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Use(null);
    }

    private void Use(RefPickerModel? model)
    {
        if (ReferenceEquals(model, attachedModel))
        {
            return;
        }
        attachedModel?.Detach();
        attachedModel = model;
        attachedModel?.Attach();
    }

    private void OnClear(object? sender, RoutedEventArgs e)
    {
        (DataContext as RefPickerModel)?.Clear();
        Field.Focus();
    }

    private void Close()
    {
        flyout.Hide();
        Field.Focus();
    }
}
