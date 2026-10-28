using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// The sections of one kind in a project, in file order. Kind is null for the
// sections no kind takes, listed last under "Other".
public sealed record SectionGroup(SectionSchema? Kind, string Title, IReadOnlyList<string> Sections);

// Section names and keys the way Canopy reads them (src/CanopyCore/mod_rules.h:
// KeyValid, SectionOf, SectionKey), so a name the Create area makes is one
// the loader takes. Names match without case, ASCII letters only.
public static class SectionNames
{
    public const int MaxKeyLength = 32;
    public const string OtherTitle = "Other";

    // 1 to 32 letters, digits, '_' or '-': keys of mods, sections and settings.
    public static bool KeyValid(string key) =>
        key.Length is > 0 and <= MaxKeyLength && key.All(c => c is (>= 'a' and <= 'z') or (>= 'A' and <= 'Z')
            or (>= '0' and <= '9') or '_' or '-');

    public static bool Same(string left, string right) => ModNames.Same(left, right);

    // The kind of a section name, null for one no kind takes. A prefixed
    // kind needs a key after its prefix, so a bare [flag.] has no kind.
    public static SectionSchema? KindOf(string section, IReadOnlyList<SectionSchema> kinds) =>
        kinds.FirstOrDefault(kind => kind.IsSingle
            ? Same(section, kind.Prefix)
            : section.Length > kind.Prefix.Length && Same(section[..kind.Prefix.Length], kind.Prefix));

    // The part of section after kind's prefix; empty for a single kind.
    public static string KeyOf(string section, SectionSchema kind) =>
        kind.IsSingle || section.Length <= kind.Prefix.Length ? "" : section[kind.Prefix.Length..];

    // The section name of kind with key; a single kind ignores key.
    public static string Name(SectionSchema kind, string key) => kind.IsSingle ? kind.Prefix : kind.Prefix + key;

    // Why a section of kind with key cannot be added next to sections, or
    // null when it can.
    public static string? AddProblem(IReadOnlyList<string> sections, SectionSchema kind, string key)
    {
        if (kind.IsSingle)
        {
            return sections.Any(name => Same(name, kind.Prefix))
                ? $"The mod already has a [{kind.Prefix}] section, and it can have only one."
                : null;
        }
        if (!KeyValid(key))
        {
            return KeyRule;
        }
        string name = Name(kind, key);
        return sections.Any(other => Same(other, name))
            ? $"The mod already has a [{name}] section. Pick another key."
            : null;
    }

    // Why section, of kind, cannot take newKey, or null when it can. A
    // change of case only is allowed.
    public static string? RenameProblem(IReadOnlyList<string> sections, string section, SectionSchema kind,
        string newKey)
    {
        if (kind.IsSingle)
        {
            return $"[{kind.Prefix}] has no key to change.";
        }
        if (!KeyValid(newKey))
        {
            return KeyRule;
        }
        string name = Name(kind, newKey);
        return sections.Any(other => Same(other, name) && !Same(other, section))
            ? $"The mod already has a [{name}] section. Pick another key."
            : null;
    }

    // The sections grouped by kind: kinds in schema order, each with its
    // sections in file order, then "Other". Groups without sections are left
    // out.
    public static IReadOnlyList<SectionGroup> Group(IReadOnlyList<string> sections, IReadOnlyList<SectionSchema> kinds)
    {
        List<SectionGroup> groups = [];
        foreach (SectionSchema kind in kinds)
        {
            List<string> mine = sections.Where(section => ReferenceEquals(KindOf(section, kinds), kind)).ToList();
            if (mine.Count > 0)
            {
                groups.Add(new SectionGroup(kind, kind.Title, mine));
            }
        }
        List<string> other = sections.Where(section => KindOf(section, kinds) is null).ToList();
        if (other.Count > 0)
        {
            groups.Add(new SectionGroup(null, OtherTitle, other));
        }
        return groups;
    }

    private const string KeyRule = "A key is 1 to 32 letters, digits, '_' or '-', with no spaces.";
}
