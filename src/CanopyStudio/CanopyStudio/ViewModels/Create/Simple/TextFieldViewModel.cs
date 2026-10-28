using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.ViewModels.Create.Simple;

// A value typed as text: text and other fields, and the base of numbers and
// paths. The box keeps what the user typed, spaces at the ends included,
// while the project gets it trimmed, as the loader reads it; a change from
// elsewhere replaces the box's text.
public class TextFieldViewModel : FieldViewModel
{
    private string text;

    public TextFieldViewModel(FieldContext context) : base(context)
    {
        text = Value ?? "";
        Note = Check(text);
    }

    public string Text
    {
        get => text;
        set
        {
            if (SetProperty(ref text, value))
            {
                SetValue(Key, value.Trim());
                Note = Check(value);
                OnPropertyChanged(nameof(Note));
                OnPropertyChanged(nameof(HasNote));
            }
        }
    }

    // What the box shows while empty: the value the loader uses then.
    public virtual string Watermark => Default ?? "";

    // Advice about the value before the rules see it, null for none. The
    // value is written either way: the rules decide.
    public string? Note { get; private set; }
    public bool HasNote => Note is not null;

    protected virtual string? Check(string value) => null;

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        string now = Value ?? "";
        if (now != text.Trim())
        {
            text = now;
            OnPropertyChanged(nameof(Text));
        }
        Note = Check(text);
        OnPropertyChanged(nameof(Note));
        OnPropertyChanged(nameof(HasNote));
    }
}

// A whole number (integer) or any number (number), with its range when the
// schema gives one.
public sealed class NumberFieldViewModel(FieldContext context) : TextFieldViewModel(context)
{
    public bool Whole => Context.Field.Kind == FieldKinds.Integer;

    // "1 to 99", or null without a range.
    public string? Range => NumberText.Range(Context.Field.Min, Context.Field.Max);
    public bool HasRange => Range is not null;

    public override string Watermark => (Default, Range) switch
    {
        ({ } value, { } range) => $"{value} ({range})",
        (null, { } range) => range,
        ({ } value, null) => value,
        _ => Whole ? "A whole number" : "A number",
    };

    protected override string? Check(string value) => value.Trim().Length == 0
        ? null
        : Context.Field.Kind == FieldKinds.Integer
            ? NumberText.WholeProblem(value, Context.Field.Min, Context.Field.Max)
            : NumberText.DecimalProblem(value);
}
