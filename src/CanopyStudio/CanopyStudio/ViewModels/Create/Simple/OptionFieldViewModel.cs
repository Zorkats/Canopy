using CanopyStudio.Core.Create;

namespace CanopyStudio.ViewModels.Create.Simple;

// One entry of a switch or choice list. Value is what is written, null to
// leave the key out.
public sealed record FieldOption(string Label, string? Value)
{
    public override string ToString() => Label;
}

// A value picked from a short list, with "not set" first. A value in the
// file that is not on the list is shown as a note and kept until the user
// picks another.
public abstract class OptionFieldViewModel : FieldViewModel
{
    protected OptionFieldViewModel(FieldContext context) : base(context)
    {
    }

    public abstract IReadOnlyList<FieldOption> Options { get; }

    public FieldOption? Selected
    {
        get => Match(Value);
        set
        {
            if (value is not null && !ReferenceEquals(value, Match(Value)))
            {
                SetValue(Key, value.Value);
                Raise();
            }
        }
    }

    public string? Note => Value is { } value && Match(value) is null
        ? $"The file says \"{value}\", which is not one of these. Pick one to replace it."
        : null;

    public bool HasNote => Note is not null;

    // The option value stands for, null when none does.
    protected abstract FieldOption? Match(string? value);

    // "Not set", with the default the loader uses then.
    protected FieldOption Unset() => new(Default is null ? "Not set" : $"Not set ({Default})", null);

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        Raise();
    }

    private void Raise()
    {
        OnPropertyChanged(nameof(Selected));
        OnPropertyChanged(nameof(Note));
        OnPropertyChanged(nameof(HasNote));
    }
}

// on and off, written the way the field's default is spelled.
public sealed class SwitchFieldViewModel : OptionFieldViewModel
{
    public SwitchFieldViewModel(FieldContext context) : base(context)
    {
        (string on, string off) = SwitchText.Words(Default);
        Options = [Unset(), new("On", on), new("Off", off)];
    }

    public override IReadOnlyList<FieldOption> Options { get; }

    protected override FieldOption? Match(string? value) => value is null
        ? Options[0]
        : SwitchText.Read(value) switch
        {
            true => Options[1],
            false => Options[2],
            null => null,
        };
}

// One of the schema's choices. The rules compare without case.
public sealed class ChoiceFieldViewModel : OptionFieldViewModel
{
    public ChoiceFieldViewModel(FieldContext context) : base(context)
    {
        Options = [Unset(), .. context.Field.Choices.Select(choice => new FieldOption(choice, choice))];
    }

    public override IReadOnlyList<FieldOption> Options { get; }

    protected override FieldOption? Match(string? value) => value is null
        ? Options[0]
        : Options.Skip(1).FirstOrDefault(option =>
            string.Equals(option.Value, value.Trim(), StringComparison.OrdinalIgnoreCase));
}
