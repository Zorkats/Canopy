using System.Text;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// The drawer's create mode for one new section of kind: its key, and the name
// or label the new section starts with. The key follows the name until the
// user types a key of their own. Refresh asks again whether the key is free,
// since the project may have changed under the draft.
public sealed partial class NewSectionViewModel : ObservableObject
{
    private readonly Func<IReadOnlyList<string>> sections;
    private readonly Action<NewSectionViewModel> add;
    private readonly Action cancel;
    private bool keyTyped;
    private bool suggesting;

    public NewSectionViewModel(SectionSchema kind, Func<IReadOnlyList<string>> sections,
        Action<NewSectionViewModel> add, Action cancel)
    {
        Kind = kind;
        this.sections = sections;
        this.add = add;
        this.cancel = cancel;
    }

    public SectionSchema Kind { get; }
    public string KindTitle => Kind.Title;
    public string Title => $"New {Kind.Title.ToLowerInvariant()}";

    // A single kind, such as [mod], has one section and no key.
    public bool NeedsKey => !Kind.IsSingle;

    // The field the draft's second box writes: name, else label, else none.
    public string? NameKey => HasField("name") ? "name" : HasField("label") ? "label" : null;
    public bool HasNameField => NameKey is not null;
    public string NameLabel => NameKey is { } key ? FieldLabels.Label(Kind.Prefix, key) : "";

    public string DocUrl => CanopyDocs.Url(Kind.Doc);
    public Uri DocUri => new(DocUrl);
    public string DocText => $"Read {Path.GetFileName(Kind.Doc)} on GitHub";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Problem))]
    [NotifyPropertyChangedFor(nameof(HasProblem))]
    [NotifyPropertyChangedFor(nameof(SectionName))]
    [NotifyCanExecuteChangedFor(nameof(AddCommand))]
    private string key = "";

    [ObservableProperty]
    private string name = "";

    // The problem with adding the section, null when it can be added or the
    // key is still empty.
    public string? Problem => NeedsKey && Key.Trim().Length == 0
        ? null
        : SectionNames.AddProblem(sections(), Kind, NeedsKey ? Key.Trim() : "");

    public bool HasProblem => Problem is not null;

    public string SectionName => SectionNames.Name(Kind, NeedsKey ? Key.Trim() : "");

    public bool CanAdd => Problem is null && (!NeedsKey || Key.Trim().Length > 0);

    // The project changed: the key may now clash, or be free.
    public void Refresh()
    {
        OnPropertyChanged(nameof(Problem));
        OnPropertyChanged(nameof(HasProblem));
        AddCommand.NotifyCanExecuteChanged();
    }

    partial void OnKeyChanged(string value)
    {
        if (!suggesting)
        {
            keyTyped = true;
        }
    }

    partial void OnNameChanged(string value)
    {
        if (!keyTyped && NeedsKey)
        {
            suggesting = true;
            Key = SuggestKey(value);
            suggesting = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanAdd))]
    private void Add() => add(this);

    [RelayCommand]
    private void Cancel() => cancel();

    private bool HasField(string key) => Kind.Fields.Any(field => SectionNames.Same(field.Key, key));

    // Suggests a key from a name: the letters and digits of each word, each
    // word starting with a capital, cut to the longest key a section takes.
    // "Moss lamp" gives "MossLamp".
    private static string SuggestKey(string name)
    {
        StringBuilder key = new();
        foreach (string word in name.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            bool first = true;
            foreach (char letter in word.Where(char.IsAsciiLetterOrDigit))
            {
                key.Append(first ? char.ToUpperInvariant(letter) : letter);
                first = false;
            }
        }
        string text = key.ToString();
        return text.Length > SectionNames.MaxKeyLength ? text[..SectionNames.MaxKeyLength] : text;
    }
}
