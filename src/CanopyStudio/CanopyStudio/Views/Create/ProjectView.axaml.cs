using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CanopyStudio.ViewModels.Create;

namespace CanopyStudio.Views.Create;

public sealed partial class ProjectView : UserControl
{
    public ProjectView() => InitializeComponent();

    // Scrolls to field's row in the form and focuses its first input. Posted
    // so a form that just opened has its rows laid out first. The row is in
    // the main fields or in "More options", whichever holds the field.
    public void FocusField(FieldViewModel field) =>
        Dispatcher.UIThread.Post(() =>
        {
            if (RowOf(field) is not { } row)
            {
                return;
            }
            row.BringIntoView();
            InputElement? input = row.GetVisualDescendants()
                .OfType<InputElement>()
                .FirstOrDefault(element => element is TextBox or ComboBox or Button or CheckBox
                    && element.Focusable && element.IsEffectivelyEnabled && element.IsEffectivelyVisible);
            input?.Focus(NavigationMethod.Tab);
        }, DispatcherPriority.Loaded);

    // The row of field in whichever field list holds it, null when neither does.
    private Control? RowOf(FieldViewModel field) =>
        new[] { "MainFields", "MoreFields" }
            .Select(name => this.GetControl<ItemsControl>(name).ContainerFromItem(field))
            .OfType<Control>()
            .FirstOrDefault();
}
