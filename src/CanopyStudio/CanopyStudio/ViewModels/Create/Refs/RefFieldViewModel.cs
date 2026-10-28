using CanopyStudio.Core.Create;

namespace CanopyStudio.ViewModels.Create.Refs;

// A field that names one item, flag, icon, texture or skin, edited through a
// RefPickerModel.
public sealed class RefFieldViewModel : FieldViewModel
{
    private ProjectKeys keys;

    public RefFieldViewModel(FieldContext context, RefRules rules) : base(context)
    {
        keys = ReferenceFields.KeysFor(context);
        Picker = new RefPickerModel(rules, keys, context.Assets, Value, Write);
    }

    public RefPickerModel Picker { get; }

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        keys = keys.WithProject(Context.Project.Structure, Context.Project.Name);
        Picker.UseKeys(keys);
        Picker.Show(Value);
    }

    private string? Write(string? value)
    {
        SetValue(Key, value);
        return Value;
    }
}
