using System.ComponentModel;
using CanopyStudio.Core.Create;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create.Refs;

// A whistle's notes, built with S and L buttons. A value the loader cannot
// read is shown as it is; only Clear changes it then.
public sealed partial class MelodyFieldViewModel : FieldViewModel
{
    public MelodyFieldViewModel(FieldContext context) : base(context) => PropertyChanged += OnChanged;

    // The notes, or null when the value has something that is not a note.
    private IReadOnlyList<bool>? Notes => Melody.Read(Value);

    public bool IsReadable => Notes is not null;
    public bool HasNotes => Notes is { Count: > 0 };

    public string Symbols => Notes is { } notes ? Melody.Symbols(notes) : "";

    public string CountText => Notes is { } notes
        ? $"{notes.Count} of {Melody.MaxNotes} notes"
        : "";

    // The value as written, shown under the symbols.
    public string ValueText => Value ?? "";

    public string? Warning => string.IsNullOrEmpty(Value)
        ? Required ? $"A whistle needs {Melody.MinNotes} to {Melody.MaxNotes} notes. Press S or L to add them." : null
        : Melody.Problem(Value);

    public bool HasWarning => Warning is not null;

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private void AddShort() => Add(false);

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private void AddLong() => Add(true);

    [RelayCommand(CanExecute = nameof(HasNotes))]
    private void Backspace()
    {
        if (Notes is { Count: > 0 } notes)
        {
            SetValue(Key, Melody.Format(notes.Take(notes.Count - 1)));
        }
    }

    [RelayCommand(CanExecute = nameof(CanClear))]
    private void Clear() => SetValue(Key, null);

    private bool CanAdd() => Notes is { } notes && notes.Count < Melody.MaxNotes;

    private bool CanClear() => !string.IsNullOrEmpty(Value);

    private void Add(bool longNote)
    {
        if (Notes is { } notes && notes.Count < Melody.MaxNotes)
        {
            SetValue(Key, Melody.Format(notes.Append(longNote)));
        }
    }

    private void OnChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(Value))
        {
            return;
        }
        foreach (string name in (string[])[nameof(IsReadable), nameof(HasNotes), nameof(Symbols), nameof(CountText),
                     nameof(ValueText), nameof(Warning), nameof(HasWarning)])
        {
            OnPropertyChanged(name);
        }
        AddShortCommand.NotifyCanExecuteChanged();
        AddLongCommand.NotifyCanExecuteChanged();
        BackspaceCommand.NotifyCanExecuteChanged();
        ClearCommand.NotifyCanExecuteChanged();
    }
}
