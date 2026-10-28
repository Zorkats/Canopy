using Avalonia.Controls;
using CanopyStudio.ViewModels.Create;
using CanopyStudio.ViewModels.Create.Refs;
using CanopyStudio.Views.Create.Refs;

namespace CanopyStudio.Views.Create;

// The views of ReferenceFields' editors. Null for an editor it does not
// know.
public static class ReferenceFieldViews
{
    public static Control? Build(FieldViewModel field) => field switch
    {
        RefFieldViewModel reference => new RefPickerView { DataContext = reference.Picker },
        VanillaIdFieldViewModel => new VanillaIdFieldView { DataContext = field },
        MelodyFieldViewModel => new MelodyFieldView { DataContext = field },
        NeedsFieldViewModel => new NeedsFieldView { DataContext = field },
        SwapsFieldViewModel => new SwapsFieldView { DataContext = field },
        _ => null,
    };
}
