using System.Globalization;
using CanopyStudio.Core.Game;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Game;

// A language the Text page can show: its index in GameTextIndex.Codes, -1
// for none.
public sealed record TextLanguage(int Index, string Code, string Name)
{
    public static TextLanguage None { get; } = new(-1, "", "None");

    public override string ToString() => Name;
}

// The table filter: one table, or every table when File is null.
public sealed record TextTableOption(string? File, string Title)
{
    public override string ToString() => Title;
}

// One entry of the Text list, made once per entry when the index is built.
// Its shown texts follow the languages picked and the edits mod, so a list
// of tens of thousands keeps its items and only their texts change.
public sealed partial class TextRowItem : ObservableObject
{
    private const string Missing = "(none)";
    private const string Blank = "(empty)";

    [ObservableProperty]
    private string firstText = "";

    [ObservableProperty]
    private bool firstEmpty;

    [ObservableProperty]
    private string secondText = "";

    [ObservableProperty]
    private bool secondEmpty;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(FirstSpan))]
    private bool hasSecond;

    [ObservableProperty]
    private bool isChanged;

    public TextRowItem(GameTextEntry entry, int position)
    {
        Entry = entry;
        Position = position;
        IdText = "#" + entry.Id.ToString(CultureInfo.InvariantCulture);
    }

    public GameTextEntry Entry { get; }

    // The entry's place in the index.
    public int Position { get; }

    public string Table => Entry.File;
    public string IdText { get; }
    public string Enum => Entry.Enum ?? "";
    public string Field => Entry.Field;

    // The first text takes the second's column when there is none.
    public int FirstSpan => HasSecond ? 1 : 2;

    // Shows the texts in first and second (-1 for none) as they are with the
    // edits mod: its text where changed is true and it sets one.
    public void Show(int first, int second, bool changed, GameTextPatches patches)
    {
        IsChanged = changed;
        (FirstText, FirstEmpty) = Text(first, changed, patches);
        HasSecond = second >= 0;
        (SecondText, SecondEmpty) = HasSecond ? Text(second, changed, patches) : ("", false);
    }

    // A text on one line, with a dimmed word where there is none.
    private (string Text, bool Empty) Text(int language, bool changed, GameTextPatches patches)
    {
        string? text = changed ? patches.Shown(Entry, language) : Entry.Values[language];
        return text switch
        {
            null => (Missing, true),
            "" => (Blank, true),
            _ => (text.Replace("\r\n", " ", StringComparison.Ordinal).Replace('\n', ' ').Replace('\r', ' '), false),
        };
    }
}
