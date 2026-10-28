using CanopyStudio.Core.Native;
using CanopyStudio.ViewModels.Create.Simple;

namespace CanopyStudio.ViewModels.Create;

// Editors for plain values: text, per-language text, numbers, switches,
// choices, lists and paths. Null for a kind it does not take.
public static class SimpleFields
{
    public static FieldViewModel? Create(FieldContext context) => context.Field.Kind switch
    {
        FieldKinds.Text or FieldKinds.Other => new TextFieldViewModel(context),
        FieldKinds.LangText => new LangTextFieldViewModel(context),
        FieldKinds.Integer or FieldKinds.Number => new NumberFieldViewModel(context),
        FieldKinds.Switch => new SwitchFieldViewModel(context),
        FieldKinds.Choice => new ChoiceFieldViewModel(context),
        FieldKinds.List => new ListFieldViewModel(context),
        FieldKinds.Path => new PathFieldViewModel(context),
        _ => null,
    };
}
