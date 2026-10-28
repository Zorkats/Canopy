using System.Globalization;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// A texture row the edits mod replaces: the row as its patch section names
// it, the row's id when named by id, the [texture.<key>] key and the file
// that section names.
public sealed record ArtReplacement(string Row, ulong? Id, string Key, string File);

// One value a replacement patches into the vanilla texture row.
public sealed record ArtPatch(string Path, string Value);

// A texture replaced through the edits mod: a [texture.<key>] section that
// declares the PNG, and a [patch.texture.<id>] section that points the
// vanilla row at the file the game reads for it (docs/ART.md,
// docs/PATCHES.md). The PNG is copied by the caller; these are the ini
// edits, which wait for the user's Save like every other edit.
public static class ArtReplace
{
    public const string TableFile = "texture.dat";
    public const string FileField = "fileName";

    // What the vanilla row gets: every language's file pointed at the mod
    // texture. Width and height stay as they are, 0 in every vanilla row:
    // the game reads the size from the file, as it does for mod textures.
    public static IReadOnlyList<ArtPatch> Patches(string gamePath) =>
        [.. LangKeys.Keys(FileField).Select(path => new ArtPatch(path, gamePath))];

    // The paths Revert takes out: the files, and width and height, which
    // replacements made before they were left alone patched too.
    public static IReadOnlyList<string> PatchedPaths { get; } = [.. LangKeys.Keys(FileField), "width", "height"];

    public static string SectionOf(string key) => "texture." + key;

    // Declares key as art/<key>.png and patches row id to it. Null when
    // made, else why not; a failure part way leaves what was made, which
    // Revert takes out.
    public static string? Apply(EditsMod mod, ulong id, string key)
    {
        string modKey = ArtKeys.ModKey(mod.Project);
        if (!ArtKeys.Valid(modKey))
        {
            return KeyProblem(mod, modKey);
        }
        if (!ArtKeys.Valid(key))
        {
            return $"The texture key {key} must be 1 to 32 letters, digits, '_' or '-'.";
        }
        string section = Written(mod.Project, SectionOf(key)) ?? SectionOf(key);
        if (mod.Project.Edit("set", section, "file", ArtKeys.ArtFile(key)) is { } error)
        {
            return error;
        }
        foreach (ArtPatch patch in Patches(ArtKeys.GamePath(modKey, key)))
        {
            if (mod.Set(TableFile, id, patch.Path, patch.Value) is { } failed)
            {
                return failed;
            }
        }
        return null;
    }

    // Takes the replacement of row id out: the patched values, the row's
    // section when nothing else is left in it, and [texture.<key>]. Other
    // fields the mod patches on the row stay. Null when done, else why not.
    public static string? Revert(EditsMod mod, ulong id, string key)
    {
        foreach (string path in PatchedPaths)
        {
            if (mod.Remove(TableFile, id, path) is { } error)
            {
                return error;
            }
        }
        // The plain path sets every language at once, as a hand edit may.
        if (mod.Remove(TableFile, id, FileField) is { } plain)
        {
            return plain;
        }
        return Written(mod.Project, SectionOf(key)) is { } section ? mod.Project.Edit("removeSection", section) : null;
    }

    // The texture rows mod replaces: each [patch.texture.<row>] whose
    // fileName points at the game path of one of the mod's [texture.<key>]
    // sections.
    public static IReadOnlyList<ArtReplacement> Read(EditsMod mod) =>
        Read(mod.Project.Structure.Lines, mod.Patches, ArtKeys.ModKey(mod.Project));

    public static IReadOnlyList<ArtReplacement> Read(IReadOnlyList<IniLine> lines, IReadOnlyList<PatchValue> patches,
        string modKey)
    {
        // The game path of each [texture.<key>], to its key and file.
        Dictionary<string, (string Key, string File)> textures = new(StringComparer.OrdinalIgnoreCase);
        foreach (IniLine line in lines)
        {
            if (line.Kind == "value" && string.Equals(line.Key, "file", StringComparison.OrdinalIgnoreCase) &&
                line.Section.StartsWith("texture.", StringComparison.OrdinalIgnoreCase) &&
                ArtKeys.TextureKey(line.Section) is { } key)
            {
                textures.TryAdd(ArtKeys.GamePath(modKey, key), (key, line.Value));
            }
        }
        List<ArtReplacement> found = [];
        HashSet<string> rows = new(StringComparer.Ordinal);
        foreach (PatchValue patch in patches)
        {
            if (patch.Table == "texture" && IsFileField(patch.Path) &&
                textures.TryGetValue(patch.Value.Trim(), out (string Key, string File) texture) && rows.Add(patch.Row))
            {
                found.Add(new ArtReplacement(patch.Row, patch.Id, texture.Key, texture.File));
            }
        }
        return found;
    }

    // Null when the mod's key can file art, else what to do about it.
    public static string? ModKeyProblem(EditsMod mod)
    {
        string modKey = ArtKeys.ModKey(mod.Project);
        return ArtKeys.Valid(modKey) ? null : KeyProblem(mod, modKey);
    }

    private static string KeyProblem(EditsMod mod, string modKey) =>
        $"Canopy files a mod's art under its key, and the key of {mod.Name} is \"{modKey}\", which is not 1 to 32 letters, digits, '_' or '-'. " +
        "Set key = <a short key> in its [mod] section, or pick another edits mod.";

    private static bool IsFileField(string path) =>
        string.Equals(path, FileField, StringComparison.OrdinalIgnoreCase) ||
        path.StartsWith(FileField + ".", StringComparison.OrdinalIgnoreCase);

    // The section as canopy.ini writes it, matched without case; null when
    // there is none.
    private static string? Written(ModProject project, string section) =>
        project.SectionNames.FirstOrDefault(name => string.Equals(name, section, StringComparison.OrdinalIgnoreCase));
}
