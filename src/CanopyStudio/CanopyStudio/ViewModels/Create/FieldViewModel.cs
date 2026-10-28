using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Create;

// What a field editor works on: one key of one section of the project, the
// rules for that key, and the game's names and icons for pickers.
// GameFolder is the open game, null when there is none.
public sealed record FieldContext(
    ModProject Project,
    string Section,
    SectionSchema Kind,
    FieldSchema Field,
    IGameAssets Assets,
    string? GameFolder);

// One field of a section form. The value lives in the project's text, never
// here: reading asks the project, writing edits it, so the form and the raw
// view can never disagree. Editors for each kind of value derive from this
// (SimpleFields, ReferenceFields); the base edits the one key Field.Key.
public abstract partial class FieldViewModel : ObservableObject, IDisposable
{
    // The problems of the last check about this field, set by the problems
    // panel.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasProblems))]
    [NotifyPropertyChangedFor(nameof(HasFailure))]
    [NotifyPropertyChangedFor(nameof(HasWarningOnly))]
    private IReadOnlyList<ModDiagnostic> problems = [];

    // Why the last change was refused, null when it was made.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasFailure))]
    [NotifyPropertyChangedFor(nameof(HasWarningOnly))]
    private string? error;

    protected FieldViewModel(FieldContext context)
    {
        Context = context;
        context.Project.Changed += OnProjectChanged;
    }

    public FieldContext Context { get; }
    public string Key => Context.Field.Key;
    public string Help => Context.Field.Help;
    public bool Required => Context.Field.Required;
    // What the loader uses when the key is left out, null for nothing.
    public string? Default => Context.Field.Default;
    public bool HasProblems => Problems.Count > 0;

    // The words a person reads for the field, and where its editor goes: the
    // main stack, the "More options" fold, or nowhere.
    public string Label => FieldLabels.Label(Context.Kind.Prefix, Key);
    public FieldPlace Place => FieldLabels.Place(Context.Kind.Prefix, Context.Field);

    // The field's left rule: red when it has an error, the accent when it
    // only has warnings.
    public bool HasFailure => Error is not null || Problems.Any(problem => problem.Severity == "error");
    public bool HasWarningOnly => !HasFailure && Problems.Any(problem => problem.Severity == "warning");

    // The key's value in the project, null when the key is not there.
    public string? Value
    {
        get => Context.Project.Value(Context.Section, Key);
        set => SetValue(Key, value);
    }

    public void Dispose()
    {
        Context.Project.Changed -= OnProjectChanged;
        GC.SuppressFinalize(this);
    }

    // Sets key in this field's section; null or empty removes it. A change
    // that cannot be made leaves the text as it was and sets Error.
    protected void SetValue(string key, string? value)
    {
        Error = string.IsNullOrEmpty(value)
            ? Context.Project.Value(Context.Section, key) is null
                ? null
                : Context.Project.Edit("remove", Context.Section, key, source: this)
            : Context.Project.Edit("set", Context.Section, key, value, source: this);
        OnPropertyChanged(nameof(Value));
    }

    // The project changed: by another field, the raw view, or a load. The
    // base re-reads Value; editors with more state refresh it here too.
    protected virtual void OnProjectChanged()
    {
        OnPropertyChanged(nameof(Value));
    }

    private void OnProjectChanged(object? sender, ProjectChangedEventArgs change)
    {
        if (!ReferenceEquals(change.Source, this))
        {
            OnProjectChanged();
        }
    }
}

// The editor for a kind no other editor takes: the value as plain text.
public sealed class RawFieldViewModel(FieldContext context) : FieldViewModel(context);
