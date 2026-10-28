using System.Globalization;
using CanopyStudio.Core.Pickers;

namespace CanopyStudio.Core.Game;

// The index's entries, searchable over the languages shown, the enum name
// and the id. Search folds case and Latin accents the way the pickers do
// (PickerQuery.Fold). A text is folded the first time a search needs it,
// then kept, so the language shown costs nothing until it is searched.
// Find may run off the UI thread; two searches at once only fold a text
// twice.
public sealed class GameTextQuery
{
    private readonly IReadOnlyList<GameTextEntry> entries;
    private readonly string[] enums;
    private readonly string[] ids;
    private readonly string?[] folded;
    private readonly int languages;

    public GameTextQuery(IReadOnlyList<GameTextEntry> entries)
    {
        this.entries = entries;
        languages = GameTextIndex.Codes.Count;
        enums = entries.Select(entry => PickerQuery.Fold(entry.Enum ?? "")).ToArray();
        ids = entries.Select(entry => entry.Id.ToString(CultureInfo.InvariantCulture)).ToArray();
        folded = new string?[entries.Count * languages];
    }

    public IReadOnlyList<GameTextEntry> Entries => entries;

    // The positions in Entries, in order, of the entries of file (any table
    // when null) that keep accepts (all when null) and that match search:
    // every word of it must be found in a shown language's text, the enum
    // name or the id. "#12" looks for the id 12. edited gives the edits
    // mod's text of an entry in a language, searched beside the vanilla one;
    // null when it sets none.
    public IReadOnlyList<int> Find(string? search, IReadOnlyList<int> shown, string? file = null,
        Func<int, bool>? keep = null, Func<int, int, string?>? edited = null)
    {
        string[] words = PickerQuery.Fold((search ?? "").Trim())
            .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Select(word => word.Length > 1 && word[0] == '#' && char.IsAsciiDigit(word[1]) ? word[1..] : word)
            .ToArray();
        List<int> found = [];
        for (int position = 0; position < entries.Count; ++position)
        {
            GameTextEntry entry = entries[position];
            if (file is not null && !string.Equals(entry.File, file, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            if (keep is not null && !keep(position))
            {
                continue;
            }
            if (words.Length == 0 || words.All(word => Matches(position, word, shown, edited)))
            {
                found.Add(position);
            }
        }
        return found;
    }

    private bool Matches(int position, string word, IReadOnlyList<int> shown, Func<int, int, string?>? edited)
    {
        if (enums[position].Contains(word, StringComparison.Ordinal) || ids[position].Contains(word, StringComparison.Ordinal))
        {
            return true;
        }
        foreach (int language in shown)
        {
            if (Folded(position, language).Contains(word, StringComparison.Ordinal))
            {
                return true;
            }
            if (edited?.Invoke(position, language) is { } text && PickerQuery.Fold(text).Contains(word, StringComparison.Ordinal))
            {
                return true;
            }
        }
        return false;
    }

    private string Folded(int position, int language)
    {
        if (language < 0 || language >= languages)
        {
            return "";
        }
        int slot = position * languages + language;
        return folded[slot] ??= PickerQuery.Fold(entries[position].Values[language] ?? "");
    }
}
