using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using CanopyStudio.ViewModels.Create;

namespace CanopyStudio.Views.Create;

// The view of a field editor, matched the way FieldEditors made it:
// reference views first, then plain ones, else a text box. A form uses it
// as the item template of its fields.
public sealed class FieldViewLocator : IDataTemplate
{
    public Control? Build(object? data) => data is FieldViewModel field
        ? ReferenceFieldViews.Build(field) ?? SimpleFieldViews.Build(field) ?? Raw(field)
        : null;

    public bool Match(object? data) => data is FieldViewModel;

    private static TextBox Raw(FieldViewModel field) => new()
    {
        DataContext = field,
        [!TextBox.TextProperty] = new Binding(nameof(FieldViewModel.Value)) { Mode = BindingMode.TwoWay },
    };
}
