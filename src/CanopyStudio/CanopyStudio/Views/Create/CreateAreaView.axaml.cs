using Avalonia;
using Avalonia.Controls;
using CanopyStudio.ViewModels.Create;

namespace CanopyStudio.Views.Create;

// Listens to the area's focus requests only while shown, so a view that is
// gone does not keep the page alive.
public sealed partial class CreateAreaView : UserControl
{
    private CreateAreaViewModel? area;

    public CreateAreaView() => InitializeComponent();

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Listen(DataContext as CreateAreaViewModel);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        Listen(null);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);
        if (VisualRoot is not null)
        {
            Listen(DataContext as CreateAreaViewModel);
        }
    }

    private void Listen(CreateAreaViewModel? next)
    {
        if (area is not null)
        {
            area.FocusRequested -= OnFocusRequested;
        }
        area = next;
        if (area is not null)
        {
            area.FocusRequested += OnFocusRequested;
        }
    }

    private void OnFocusRequested(object? sender, FieldViewModel field) =>
        this.GetControl<ProjectView>("ProjectPane").FocusField(field);
}
