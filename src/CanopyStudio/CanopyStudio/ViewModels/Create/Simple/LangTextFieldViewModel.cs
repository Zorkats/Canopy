using CanopyStudio.Core.Create;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Create.Simple;

// One language of a lang-text field: its own key, such as name.en. Left
// empty, the game shows the base key's text in that language.
public sealed class LangTextRowViewModel : ObservableObject
{
    private readonly LangTextFieldViewModel owner;
    private string text;

    public LangTextRowViewModel(LangTextFieldViewModel owner, string code)
    {
        this.owner = owner;
        Code = code;
        Language = LangKeys.LanguageName(code);
        Key = LangKeys.Key(owner.Key, code);
        text = owner.Read(Key) ?? "";
    }

    public string Code { get; }
    public string Language { get; }
    public string Key { get; }

    public string Text
    {
        get => text;
        set
        {
            if (SetProperty(ref text, value))
            {
                owner.Write(Key, value.Trim());
                OnPropertyChanged(nameof(FallsBack));
            }
        }
    }

    // True while the language uses the base key's text.
    public bool FallsBack => text.Trim().Length == 0;

    public string FallbackText => $"Uses {owner.Label}";

    internal void Refresh()
    {
        string now = owner.Read(Key) ?? "";
        if (now != text.Trim())
        {
            text = now;
            OnPropertyChanged(nameof(Text));
            OnPropertyChanged(nameof(FallsBack));
        }
    }
}

// A text with a key per language: the base key, read in every language
// without its own, then name.ja, name.en, ... in the game's language order.
public sealed class LangTextFieldViewModel : FieldViewModel
{
    private string text;

    public LangTextFieldViewModel(FieldContext context) : base(context)
    {
        text = Value ?? "";
        Rows = LangKeys.Codes.Select(code => new LangTextRowViewModel(this, code)).ToList();
    }

    public IReadOnlyList<LangTextRowViewModel> Rows { get; }

    // The base key's text, as typed.
    public string Text
    {
        get => text;
        set
        {
            if (SetProperty(ref text, value))
            {
                SetValue(Key, value.Trim());
            }
        }
    }

    public string Watermark => Default ?? (Required ? "Needed" : "");

    // The fold's title, such as "Translations (2 of 7)": how many languages
    // have their own text.
    public string Summary => FieldLabels.TranslationsTitle(Rows.Count(row => !row.FallsBack));

    internal string? Read(string key) => Context.Project.Value(Context.Section, key);

    internal void Write(string key, string value)
    {
        SetValue(key, value);
        OnPropertyChanged(nameof(Summary));
    }

    protected override void OnProjectChanged()
    {
        base.OnProjectChanged();
        string now = Value ?? "";
        if (now != text.Trim())
        {
            text = now;
            OnPropertyChanged(nameof(Text));
        }
        foreach (LangTextRowViewModel row in Rows)
        {
            row.Refresh();
        }
        OnPropertyChanged(nameof(Summary));
    }
}
