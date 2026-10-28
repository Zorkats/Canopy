using System.Globalization;
using System.Text;
using CanopyStudio.Core.Assets;

namespace CanopyStudio.Core.Pickers;

// One row of a picker: the entry and the name it is shown with. HasName is
// false when the game gives the entry no name, so Name is its enum name or
// its id instead.
public sealed record PickerItem(VanillaEntry Entry, string Name, bool HasName)
{
    public ulong Id => Entry.Id;
    public string IdText => PickerQuery.IdText(Entry.Id);
}

// A group the entries fall in and how many of them do.
public sealed record PickerGroupCount(ulong Group, int Count);

// The entries of one kind, searchable. Built once per list, so each search
// only filters: the names are folded and sorted up front.
//
// Order: by name, folded the way search folds it, then by id, so entries
// with the same name are told apart by their id and the order never depends
// on the order the entries came in. A search puts an exact id match first,
// then names and enum names that start with the search, then the rest.
public sealed class PickerQuery
{
    private readonly Key[] keys;
    private readonly Dictionary<ulong, PickerItem> byId = [];

    public PickerQuery(IEnumerable<VanillaEntry> entries)
    {
        keys = entries.Select(entry => new Key(entry)).ToArray();
        Array.Sort(keys, static (left, right) =>
        {
            int order = string.CompareOrdinal(left.Name, right.Name);
            if (order == 0)
            {
                order = string.CompareOrdinal(left.Item.Name, right.Item.Name);
            }
            return order != 0 ? order : left.Item.Id.CompareTo(right.Item.Id);
        });
        foreach (Key key in keys)
        {
            byId.TryAdd(key.Item.Id, key.Item);
        }
        Groups = keys
            .Where(key => key.Item.Entry.Group is not null)
            .GroupBy(key => key.Item.Entry.Group!.Value)
            .Select(group => new PickerGroupCount(group.Key, group.Count()))
            .OrderBy(group => group.Group)
            .ToList();
    }

    public int Count => keys.Length;

    // The distinct groups present, by group id, each with its count.
    public IReadOnlyList<PickerGroupCount> Groups { get; }

    // The entry with id, or null when the list has none.
    public PickerItem? ById(ulong id) => byId.GetValueOrDefault(id);

    // The entries in group (any group when null) that match search. Every
    // word of the search must be found in the name, the enum name or the id.
    public IReadOnlyList<PickerItem> Find(string? search, ulong? group = null)
    {
        string text = (search ?? "").Trim();
        IEnumerable<Key> inGroup = group is { } wanted ? keys.Where(key => key.Item.Entry.Group == wanted) : keys;
        if (text.Length == 0)
        {
            return inGroup.Select(key => key.Item).ToList();
        }

        string folded = Fold(text);
        // "#12" looks for the id 12.
        string[] words = folded.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .Select(word => ParseId(word) is not null ? word.TrimStart('#') : word)
            .ToArray();
        ulong? exactId = ParseId(text);
        List<PickerItem> first = [];
        List<PickerItem> starts = [];
        List<PickerItem> rest = [];
        foreach (Key key in inGroup)
        {
            if (!words.All(word => key.Haystack.Contains(word, StringComparison.Ordinal)))
            {
                continue;
            }
            if (key.Item.Id == exactId)
            {
                first.Add(key.Item);
            }
            else if (key.Name.StartsWith(folded, StringComparison.Ordinal) || key.Enum.StartsWith(folded, StringComparison.Ordinal))
            {
                starts.Add(key.Item);
            }
            else
            {
                rest.Add(key.Item);
            }
        }
        first.AddRange(starts);
        first.AddRange(rest);
        return first;
    }

    // The name entry is shown with: its name, else its enum name, else its id.
    public static string ShownName(VanillaEntry entry) =>
        !string.IsNullOrWhiteSpace(entry.Name) ? entry.Name
        : !string.IsNullOrWhiteSpace(entry.Enum) ? entry.Enum
        : IdText(entry.Id);

    public static string IdText(ulong id) => "#" + id.ToString(CultureInfo.InvariantCulture);

    // A group's label: what labels gives for it, or "Group <n>" when labels
    // is null or has none.
    public static string GroupLabel(ulong group, Func<ulong, string?>? labels) =>
        labels?.Invoke(group) is { Length: > 0 } label ? label : $"Group {group.ToString(CultureInfo.InvariantCulture)}";

    // text as search compares it: lower case, Latin accents dropped, full
    // width letters and digits made plain, the few letters that do not
    // decompose spelled out, and the enum separator read as a space. Marks
    // outside the Latin block, such as the kana voicing marks, are kept.
    public static string Fold(string text)
    {
        string decomposed = text.Normalize(NormalizationForm.FormKD);
        StringBuilder builder = new(decomposed.Length);
        foreach (char c in decomposed)
        {
            if (c is >= '̀' and <= 'ͯ')
            {
                continue;
            }
            char lower = char.ToLowerInvariant(c);
            switch (lower)
            {
                case 'ß':
                    builder.Append("ss");
                    break;
                case 'æ':
                    builder.Append("ae");
                    break;
                case 'œ':
                    builder.Append("oe");
                    break;
                case 'ø':
                    builder.Append('o');
                    break;
                case 'đ':
                    builder.Append('d');
                    break;
                case 'ł':
                    builder.Append('l');
                    break;
                case '_':
                    builder.Append(' ');
                    break;
                default:
                    builder.Append(lower);
                    break;
            }
        }
        return builder.ToString().Normalize(NormalizationForm.FormKC);
    }

    // The id a search names: digits, with or without a leading '#'.
    private static ulong? ParseId(string text)
    {
        string digits = text.StartsWith('#') ? text[1..] : text;
        return ulong.TryParse(digits, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) ? id : null;
    }

    private sealed class Key
    {
        public Key(VanillaEntry entry)
        {
            bool hasName = !string.IsNullOrWhiteSpace(entry.Name);
            Item = new PickerItem(entry, ShownName(entry), hasName);
            Name = Fold(Item.Name);
            Enum = Fold(entry.Enum ?? "");
            // One string per entry, so a word is looked for once. The
            // separator never appears in a folded search word.
            Haystack = string.Join('\u0001', Name, Enum, entry.Id.ToString(CultureInfo.InvariantCulture));
        }

        public PickerItem Item { get; }
        public string Name { get; }
        public string Enum { get; }
        public string Haystack { get; }
    }
}
