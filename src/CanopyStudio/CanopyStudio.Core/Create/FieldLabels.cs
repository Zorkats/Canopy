using System.Text;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// Where a form field goes in the Create drawer: the main stack, the
// "More options" fold, or the one-line note about keys the loader ignores.
public enum FieldPlace
{
    Main,
    More,
    Ignored,
}

// The words a person reads for each field of each section kind, and the
// place each field goes. A key the tables do not know is spelled out in
// words, so a key the loader gains later still reads as something.
public static class FieldLabels
{
    // Every known key of every kind, matched without case. Prefixes are
    // spelled as the schema spells them: "item." or "mod".
    private static readonly (string Prefix, string Key, string Label)[] Labels =
    [
        ("mod", "key", "Mod key"),
        ("mod", "name", "Name"),
        ("mod", "author", "Author"),
        ("mod", "version", "Version"),
        ("mod", "unsafe", "Full game access"),
        ("setting.", "type", "Type"),
        ("setting.", "label", "Label"),
        ("setting.", "help", "Help line"),
        ("setting.", "default", "Starting value"),
        ("setting.", "min", "Lowest value"),
        ("setting.", "max", "Highest value"),
        ("setting.", "step", "Step"),
        ("setting.", "choices", "Choices"),
        ("item.", "name", "Name"),
        ("item.", "help", "Description"),
        ("item.", "base", "Based on"),
        ("item.", "icon", "Icon"),
        ("item.", "use", "When used"),
        ("item.", "stamina", "Stamina restored"),
        ("item.", "hp", "Health restored"),
        ("item.", "consume", "Used up when used"),
        ("item.", "object", "Places object"),
        ("object.", "name", "Name"),
        ("object.", "clone", "Copy of"),
        ("object.", "skin", "Skin"),
        ("item.", "price", "Price"),
        ("item.", "buy", "Buy price"),
        ("item.", "sell", "Sell price"),
        ("item.", "energy", "Energy"),
        ("item.", "count", "Count"),
        ("item.", "category", "Category"),
        ("item.", "type", "Type"),
        ("flag.", "default", "On in a new game"),
        ("flag.", "id", "Id"),
        ("villager.", "base", "Based on"),
        ("villager.", "name", "Name"),
        ("villager.", "job", "Job"),
        ("villager.", "help", "Description"),
        ("villager.", "area", "Starting area"),
        ("villager.", "x", "Starting x"),
        ("villager.", "y", "Starting y"),
        ("villager.", "enabled", "Starts enabled"),
        ("villager.", "routine", "Routine"),
        ("villager.", "script", "Script"),
        ("villager.", "birthday", "Birthday"),
        ("villager.", "likes", "Likes"),
        ("villager.", "dislikes", "Dislikes"),
        ("villager.", "portrait", "Portrait"),
        ("villager.", "anime", "Animation"),
        ("villager.", "icon", "Icon"),
        ("whistle.", "name", "Name"),
        ("whistle.", "notes", "Melody"),
        ("whistle.", "learn", "Learned with flag"),
        ("whistle.", "base", "Based on"),
        ("whistle.", "score", "Score icon"),
        ("whistle.", "icon", "Icon"),
        ("whistle.", "script", "Script"),
        ("whistle.", "entry", "Script function"),
        ("craft.", "base", "Based on"),
        ("craft.", "output", "Makes"),
        ("craft.", "count", "How many"),
        ("craft.", "needs", "Ingredients"),
        ("craft.", "unlock", "Unlocked by flag"),
        ("craft.", "recipe", "Recipe item"),
        ("cooking.", "base", "Based on"),
        ("cooking.", "output", "Makes"),
        ("cooking.", "count", "How many"),
        ("cooking.", "needs", "Ingredients"),
        ("cooking.", "unlock", "Unlocked by flag"),
        ("cooking.", "recipe", "Recipe item"),
        ("texture.", "file", "Image file"),
        ("icon.", "texture", "Texture"),
        ("icon.", "rect", "Part of the texture"),
        ("icon.", "size", "Drawn size"),
        ("icon.", "offset", "Offset"),
        ("skin.", "base", "Based on"),
        ("skin.", "replace", "Texture swaps"),
        ("row.", "base", "Based on"),
        ("crop.", "base", "Based on"),
        ("crop.", "seed", "Seed item"),
        ("crop.", "skins", "Growth skins"),
        ("crop.", "swap", "Swapped drops"),
        ("sale.", "item", "Item sold"),
        ("sale.", "shop", "Shop"),
        ("sale.", "stock", "Stock"),
        ("sale.", "restock", "Restock"),
        ("sale.", "seasons", "Seasons"),
        ("sale.", "flag", "Listed when flags are on"),
        ("sale.", "unless", "Hidden when flags are on"),
        ("sale.", "discount", "Discount percent"),
        ("sale.", "priority", "Place in the list"),
        ("sale.", "silhouette", "Shown as a silhouette"),
        ("upgrade.", "from", "Tool brought"),
        ("upgrade.", "to", "Tool made"),
        ("upgrade.", "needs", "Materials"),
        ("upgrade.", "time", "Work time"),
        ("offering.", "pools", "Offering pools"),
        ("offering.", "item", "Item"),
        ("offering.", "rate", "Drop rate"),
        ("offering.", "count", "How many"),
        ("catch.", "fish", "Fish"),
        ("catch.", "item", "Item"),
        ("catch.", "creature", "Creature"),
        ("catch.", "spots", "Fishing spots"),
        ("catch.", "traps", "Traps"),
        ("catch.", "rate", "Catch rate"),
        ("catch.", "seasons", "Seasons"),
        ("catch.", "times", "Times of day"),
        ("catch.", "bait", "Bait"),
        ("forage.", "item", "Item"),
        ("forage.", "crop", "Wild crop"),
        ("forage.", "spots", "Forage spots"),
        ("forage.", "areas", "Areas"),
        ("forage.", "weight", "Weight"),
        ("forage.", "weather", "Weather"),
        ("forage.", "flag", "Needs flag on"),
        ("forage.", "unless", "Needs flag off"),
        ("forage.", "seasons", "Seasons"),
        ("forage.", "daily", "Grow each day"),
        ("forage.", "most", "Most at once"),
    ];

    // Fields the loader ignores. Each one's help starts with "Ignored.",
    // "Not supported yet.", "Not used." or "Craft only.", which a test checks.
    private static readonly (string Prefix, string Key)[] IgnoredFields =
    [
        ("item.", "price"), ("item.", "sell"), ("item.", "energy"), ("item.", "count"),
        ("item.", "type"),
        ("villager.", "birthday"), ("villager.", "likes"), ("villager.", "dislikes"), ("villager.", "portrait"),
        ("villager.", "anime"), ("villager.", "icon"),
        ("flag.", "id"),
        ("cooking.", "recipe"),
    ];

    // Fields kept in the "More options" fold. The cooking recipe is ignored
    // rather than kept here, since IgnoredFields is checked first.
    private static readonly (string Prefix, string Key)[] MoreFields =
    [
        ("mod", "key"), ("mod", "unsafe"),
        ("villager.", "area"), ("villager.", "x"), ("villager.", "y"), ("villager.", "enabled"),
        ("villager.", "routine"),
        ("whistle.", "base"), ("whistle.", "score"), ("whistle.", "entry"),
        ("craft.", "base"), ("craft.", "recipe"), ("cooking.", "base"),
        ("icon.", "size"), ("icon.", "offset"),
        ("skin.", "base"),
        ("crop.", "skins"), ("crop.", "swap"),
        ("sale.", "priority"), ("sale.", "silhouette"), ("upgrade.", "time"),
        ("forage.", "weather"), ("forage.", "unless"),
    ];

    // Where the field of a section of kind prefix goes. A required field is
    // always in the main stack, whatever the tables say.
    public static FieldPlace Place(string prefix, FieldSchema field)
    {
        if (field.Required)
        {
            return FieldPlace.Main;
        }
        if (Listed(IgnoredFields, prefix, field.Key))
        {
            return FieldPlace.Ignored;
        }
        return Listed(MoreFields, prefix, field.Key) ? FieldPlace.More : FieldPlace.Main;
    }

    // The label a person reads for key in a section of kind prefix ("item.",
    // "mod"). The raw key is only shown when no label is known.
    public static string Label(string prefix, string key)
    {
        string name = key.Trim();
        foreach ((string labelPrefix, string labelKey, string label) in Labels)
        {
            if (SectionNames.Same(labelPrefix, prefix) && SectionNames.Same(labelKey, name))
            {
                return label;
            }
        }
        return Spelled(name);
    }

    // The title of a lang-text field's fold: "Translations (2 of 7)".
    public static string TranslationsTitle(int custom) => $"Translations ({custom} of {LangKeys.Codes.Count})";

    // How many languages have their own non-blank value for baseKey in
    // section, read with project.Value(section, LangKeys.Key(baseKey, code)).
    public static int Translated(ModProject project, string section, string baseKey) =>
        LangKeys.Codes.Count(code =>
            !string.IsNullOrWhiteSpace(project.Value(section, LangKeys.Key(baseKey, code))));

    private static bool Listed((string Prefix, string Key)[] table, string prefix, string key) =>
        table.Any(entry => SectionNames.Same(entry.Prefix, prefix) && SectionNames.Same(entry.Key, key));

    // The key as words: "_" and "." become spaces, a space goes before each
    // capital that follows a lower-case letter, and the first letter is a
    // capital. "drawInfo.x" gives "Draw Info x".
    private static string Spelled(string key)
    {
        string text = key.Replace('_', ' ').Replace('.', ' ').Trim();
        StringBuilder words = new();
        for (int i = 0; i < text.Length; i++)
        {
            if (i > 0 && char.IsAsciiLetterUpper(text[i]) && char.IsAsciiLetterLower(text[i - 1]))
            {
                words.Append(' ');
            }
            words.Append(text[i]);
        }
        return words.Length == 0 ? "" : char.ToUpperInvariant(words[0]) + words.ToString(1, words.Length - 1);
    }
}
