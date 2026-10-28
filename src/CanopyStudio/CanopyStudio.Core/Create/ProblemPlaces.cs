using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// Where in a project a line of canopy.ini is: a key of a section, or the
// section itself (Key empty) for its header, comments and anything else in
// it. A per-language key such as name.en is the field name.
public sealed record ProblemPlace(string Section, string Key)
{
    public bool Is(string section, string key) => ModNames.Same(Section, section) && ModNames.Same(Key, key);
}

// The place of every line of one text, from the structure the loader reads,
// so a check's line numbers can point at the form field they are about.
public sealed class ProblemPlaces
{
    private readonly Dictionary<int, ProblemPlace> places = [];

    private ProblemPlaces()
    {
    }

    public static ProblemPlaces Empty { get; } = new();

    public static ProblemPlaces From(IniStructure structure)
    {
        ProblemPlaces result = new();
        foreach (IniLine line in structure.Lines)
        {
            // Lines before the first section belong to no section.
            if (line.Section.Length == 0)
            {
                continue;
            }
            string key = line.Kind == "value" ? FieldKey(line.Key) : "";
            result.places[line.Line] = new ProblemPlace(line.Section, key);
        }
        return result;
    }

    // The place of line (from 1), null when it is in no section.
    public ProblemPlace? At(int line) => places.GetValueOrDefault(line);

    // The field a key belongs to: name for name.en and the other language
    // keys, key itself otherwise. Lowercase, as the loader reads keys.
    public static string FieldKey(string key)
    {
        string lower = ModNames.Lower(key.Trim());
        int dot = lower.LastIndexOf('.');
        return dot > 0 && VanillaKinds.Languages.Any(language => language.Code == lower[(dot + 1)..])
            ? lower[..dot]
            : lower;
    }
}
