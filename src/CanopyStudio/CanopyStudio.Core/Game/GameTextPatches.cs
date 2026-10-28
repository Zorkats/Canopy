using System.Globalization;

namespace CanopyStudio.Core.Game;

// The edits mod's patches, looked up by text: for an entry and a language,
// the line of the mod that sets that text. A line can name the row by id or
// by enum name, and the text as <field>.<language>, <field>.<index> or the
// whole <field> (every language). Only the first form, by id and by
// language code, is what the Text page writes; the others are shown and
// left to Create. Built from one read of EditsMod.Patches, so it is made
// again after every change.
public sealed class GameTextPatches
{
    private const int Every = -1;

    // (table, row, field) to the lines setting it, each with its language
    // (Every for the whole field). row is the id in decimal, or "=" and the
    // enum name.
    private readonly Dictionary<(string Table, string Row, string Field), List<(int Language, PatchValue Value)>> places = [];
    // (table, row) to the first line of a section that names the row by its
    // enum name.
    private readonly Dictionary<(string Table, string Row), PatchValue> enumRows = [];

    public GameTextPatches(IEnumerable<PatchValue> values)
    {
        foreach (PatchValue value in values)
        {
            string row = value.Id is { } id ? id.ToString(CultureInfo.InvariantCulture) : "=" + value.Row;
            if (value.Id is null)
            {
                enumRows.TryAdd((value.Table, row), value);
            }
            (string field, int language) = Split(value.Path);
            var key = (value.Table, row, field);
            if (!places.TryGetValue(key, out List<(int, PatchValue)>? lines))
            {
                lines = [];
                places[key] = lines;
            }
            lines.Add((language, value));
        }
    }

    public static GameTextPatches Empty { get; } = new([]);

    public static GameTextPatches Of(EditsMod? mod) => mod is null ? Empty : new GameTextPatches(mod.Patches);

    // True when the mod patches nothing.
    public bool IsEmpty => places.Count == 0;

    // The line that sets entry's text in language, null when none does. The
    // last line wins, as it does when the loader reads the section.
    public PatchValue? Find(GameTextEntry entry, int language)
    {
        string table = EditsMod.TableName(entry.File);
        string field = entry.Field.ToLowerInvariant();
        PatchValue? found = Last(table, entry.Id.ToString(CultureInfo.InvariantCulture), field, language);
        if (found is null && entry.Enum is { Length: > 0 } name)
        {
            found = Last(table, "=" + name, field, language);
        }
        return found;
    }

    // True when the mod sets entry's text in some language.
    public bool Changes(GameTextEntry entry)
    {
        for (int language = 0; language < entry.Values.Count; ++language)
        {
            if (entry.Has(language) && Find(entry, language) is not null)
            {
                return true;
            }
        }
        return false;
    }

    // The text entry shows in language: the mod's when it sets one, else
    // the vanilla text; null where the row has no such place.
    public string? Shown(GameTextEntry entry, int language) =>
        entry.Has(language) ? Find(entry, language)?.Value ?? entry.Values[language] : null;

    // True when value sets entry's text in language the way the Text page
    // writes it: by the row's id and the language's code.
    public static bool IsPlain(PatchValue value, GameTextEntry entry, int language) =>
        value.Id == entry.Id && string.Equals(value.Path, entry.Path(language), StringComparison.OrdinalIgnoreCase);

    // The first line of a section of the mod that names entry's row by its
    // enum name, null when there is none.
    public PatchValue? EnumSection(GameTextEntry entry) =>
        entry.Enum is { Length: > 0 } name &&
        enumRows.TryGetValue((EditsMod.TableName(entry.File), "=" + name), out PatchValue? value)
            ? value
            : null;

    private PatchValue? Last(string table, string row, string field, int language)
    {
        if (!places.TryGetValue((table, row, field), out List<(int Language, PatchValue Value)>? lines))
        {
            return null;
        }
        for (int index = lines.Count - 1; index >= 0; --index)
        {
            if (lines[index].Language == language || lines[index].Language == Every)
            {
                return lines[index].Value;
            }
        }
        return null;
    }

    // A path's field, lower case, and its language: name.en and name.1 are
    // name in English, name alone is name in every language.
    private static (string Field, int Language) Split(string path)
    {
        string lower = path.Trim().ToLowerInvariant();
        int dot = lower.LastIndexOf('.');
        if (dot > 0)
        {
            string last = lower[(dot + 1)..];
            int language = GameTextIndex.LanguageOf(last);
            if (language < 0 && last.Length == 1 && last[0] >= '0' && last[0] - '0' < GameTextIndex.Codes.Count)
            {
                language = last[0] - '0';
            }
            if (language >= 0)
            {
                return (lower[..dot], language);
            }
        }
        return (lower, Every);
    }
}
