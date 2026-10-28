namespace CanopyStudio.ViewModels.Create;

// The editor for a field, by the kind of its value. Reference kinds (ids and
// keys of other things) are tried first, then plain values; anything else
// is edited as raw text.
public static class FieldEditors
{
    public static FieldViewModel Create(FieldContext context) =>
        ReferenceFields.Create(context) ?? SimpleFields.Create(context) ?? new RawFieldViewModel(context);
}
