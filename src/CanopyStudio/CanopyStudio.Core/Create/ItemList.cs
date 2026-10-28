using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// One ingredient of a recipe's needs: an item (or group:<number>) and its
// count text, null when it has no "* count". Raw is the part as it was read,
// null once changed, so unchanged parts are written back byte for byte.
public sealed record Need(string Item, string? Count, string? Raw = null)
{
    public string Text => Count is null ? Item : $"{Item} * {Count}";
}

// One swap of a skin's replace: a model texture name and the texture drawn
// instead. Raw as in Need.
public sealed record Swap(string Name, string Texture, string? Raw = null)
{
    public string Text => $"{Name}: {Texture}";
}

// The comma-separated lists of reference values: a recipe's needs
// (rules_recipes.cpp) and a skin's replace (rules_art.cpp). Each part is
// split as the loader splits it. Formatting keeps every unchanged part as it
// was read, so Format(Parse(text)) is text for any value canopy.ini can hold
// (the ini trims a value, so text never starts or ends with a space).
public static class ItemList
{
    public const int MaxNeeds = 5;
    public const int MaxCount = 99;
    // The game keeps a skin's swaps inside its record, 32 at most
    // (table_schema.cpp, the animereplacetexture entries).
    public const int MaxSwaps = 32;

    public static IReadOnlyList<Need> ParseNeeds(string? text) =>
        Split(text).Select(part =>
        {
            int star = part.IndexOf('*');
            return star < 0
                ? new Need(part.Trim(), null, part)
                : new Need(part[..star].Trim(), part[(star + 1)..].Trim(), part);
        }).ToList();

    public static string FormatNeeds(IEnumerable<Need> needs) => Join(needs.Select(need => (need.Raw, need.Text)));

    public static IReadOnlyList<Swap> ParseSwaps(string? text) =>
        Split(text).Select(part =>
        {
            int colon = part.IndexOf(':');
            return colon < 0
                ? new Swap(part.Trim(), "", part)
                : new Swap(part[..colon].Trim(), part[(colon + 1)..].Trim(), part);
        }).ToList();

    public static string FormatSwaps(IEnumerable<Swap> swaps) => Join(swaps.Select(swap => (swap.Raw, swap.Text)));

    // Why the loader would refuse count, or null when it takes it: 1 to 99,
    // or no count at all.
    public static string? CountProblem(string? count) =>
        count is null || References.Digits(count) is >= 1 and <= MaxCount
            ? null
            : "A count is a whole number from 1 to 99.";

    // How many lines of section set key. A skin may repeat replace, and the
    // form edits only the first.
    public static int ValueCount(IniStructure structure, string section, string key) =>
        structure.Lines.Count(line =>
            line.Kind == "value" && ModNames.Same(line.Section, section) && ModNames.Same(line.Key, key));

    // The parts between commas, untrimmed. None for an empty value.
    private static List<string> Split(string? text) => string.IsNullOrEmpty(text) ? [] : [.. text.Split(',')];

    // A changed part is written ", item * count"; the whole is trimmed, so
    // removing the first part does not leave a space in front.
    private static string Join(IEnumerable<(string? Raw, string Text)> parts) =>
        string.Join(',', parts.Select((part, index) => part.Raw ?? (index == 0 ? part.Text : " " + part.Text))).Trim();
}
