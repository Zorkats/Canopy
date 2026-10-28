using System.Globalization;
using System.Text;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Mods;

namespace CanopyStudio.Core.Game;

// The names a texture replacement goes by in the edits mod: the
// [texture.<key>] key, the mod key, and the path the game reads the texture
// at. The rules are the loader's (src/CanopyCore/rules_art.cpp, KeyValid in
// mod_rules.cpp): a key is 1 to 32 letters, digits, '_' or '-', unique
// among the mod's textures without case.
public static class ArtKeys
{
    public const int MaxLength = 32;

    // Every vanilla texture's enum name starts with this, so a key drops it
    // and keeps the 32 characters for the part that tells textures apart.
    private const string EnumPrefix = "TEX_ID_";
    private const string GameFolder = "data/texture/canopy/";

    public static bool Valid(string key) =>
        key.Length is > 0 and <= MaxLength && key.All(Allowed);

    // The key for a texture row: its enum name without TEX_ID_, each
    // character the rules refuse made '_', cut to 32; tex<id> when nothing
    // is left of it.
    public static string FromRow(string? enumName, ulong id)
    {
        string name = (enumName ?? "").Trim();
        if (name.StartsWith(EnumPrefix, StringComparison.OrdinalIgnoreCase))
        {
            name = name[EnumPrefix.Length..];
        }
        StringBuilder key = new(Math.Min(name.Length, MaxLength));
        foreach (char c in name)
        {
            if (key.Length == MaxLength)
            {
                break;
            }
            key.Append(Allowed(c) ? c : '_');
        }
        string made = key.ToString().Trim('_');
        return made.Length > 0 ? made : string.Create(CultureInfo.InvariantCulture, $"tex{id}");
    }

    // key, or key with _2, _3 and on after it, cut so it still fits, when
    // taken says the mod already has it.
    public static string Unique(string key, Func<string, bool> taken)
    {
        if (!taken(key))
        {
            return key;
        }
        for (int number = 2; ; ++number)
        {
            string suffix = string.Create(CultureInfo.InvariantCulture, $"_{number}");
            string candidate = key[..Math.Min(key.Length, MaxLength - suffix.Length)] + suffix;
            if (!taken(candidate))
            {
                return candidate;
            }
        }
    }

    // The texture keys the mod's sections use, [texture.<key>] and
    // [row.texture.<key>] alike: the loader keeps both in one set of keys.
    public static IReadOnlySet<string> Taken(IEnumerable<string> sectionNames)
    {
        HashSet<string> keys = new(StringComparer.OrdinalIgnoreCase);
        foreach (string section in sectionNames)
        {
            if (TextureKey(section) is { } key)
            {
                keys.Add(key);
            }
        }
        return keys;
    }

    // The key of a [texture.<key>] or [row.texture.<key>] section, null for
    // any other section.
    public static string? TextureKey(string section)
    {
        foreach (string prefix in (string[])["texture.", "row.texture."])
        {
            if (section.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && section.Length > prefix.Length)
            {
                return section[prefix.Length..];
            }
        }
        return null;
    }

    // The mod key the loader files the mod's art under: key in [mod], or
    // the folder name. A key that is there but empty stays empty, as the
    // loader reads it, and is not Valid.
    public static string ModKey(ModProject project) => project.Value("mod", "key") ?? project.Name;

    // Where the game reads the mod's texture key: lowercase, in every
    // language (docs/ART.md).
    public static string GamePath(string modKey, string key) =>
        GameFolder + ModNames.Lower(modKey) + "/" + ModNames.Lower(key) + ".nltx";

    // The replacement PNG inside the mod folder.
    public static string ArtFile(string key) => "art/" + key + ".png";

    // A file name for an exported texture: its enum name, or its id when it
    // has none, with anything a file name cannot hold made '_'.
    public static string ExportName(string? enumName, ulong id)
    {
        string name = string.IsNullOrWhiteSpace(enumName)
            ? id.ToString(CultureInfo.InvariantCulture)
            : enumName.Trim();
        char[] invalid = ['<', '>', ':', '"', '/', '\\', '|', '?', '*'];
        StringBuilder safe = new(name.Length);
        foreach (char c in name)
        {
            safe.Append(c < ' ' || invalid.Contains(c) ? '_' : c);
        }
        return safe.ToString().TrimEnd('.', ' ') is { Length: > 0 } made
            ? made + ".png"
            : id.ToString(CultureInfo.InvariantCulture) + ".png";
    }

    private static bool Allowed(char c) =>
        c is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or >= '0' and <= '9' or '_' or '-';
}
