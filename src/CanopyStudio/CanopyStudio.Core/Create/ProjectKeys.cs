using System.Collections.Concurrent;
using System.Text;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Create;

// The kinds of part a value can name by key, as RefKind in
// src/CanopyCore/mod_rules.h.
public enum RefKind
{
    Flag,
    Item,
    Icon,
    Texture,
    Skin,
    Villager,
    Whistle,
    Recipe,
    Object,
}

// One part a mod defines. Value is what a field of the project writes to name
// it: Key for the project's own part, Mod.Key for another mod's.
public sealed record ModPart(RefKind Kind, string Mod, string Key, bool Own)
{
    public string Value => Own ? Key : Mod + "." + Key;
}

// The keys a project defines, per kind, and those of the other mods in the
// game's mods folder, read the way the loader reads them: a mod's key is
// key in [mod] or its folder name, a part's key is its section name after
// the prefix, and a raw row ([row.icon.X]) shares its table's keys. Parts
// the loader would skip for a bad key are left out. Other mods' canopy.ini
// files are read once each and kept until the file changes.
public sealed class ProjectKeys
{
    private static readonly (string Prefix, RefKind Kind)[] Prefixes =
    [
        ("flag.", RefKind.Flag),
        ("item.", RefKind.Item),
        ("icon.", RefKind.Icon),
        ("texture.", RefKind.Texture),
        ("skin.", RefKind.Skin),
        ("villager.", RefKind.Villager),
        ("whistle.", RefKind.Whistle),
        ("craft.", RefKind.Recipe),
        ("cooking.", RefKind.Recipe),
        ("object.", RefKind.Object),
        ("row.icon.", RefKind.Icon),
        ("row.texture.", RefKind.Texture),
        ("row.animereplacetexture.", RefKind.Skin),
    ];

    private static readonly ConcurrentDictionary<string, CachedIni> Cache = new(StringComparer.OrdinalIgnoreCase);

    private ProjectKeys(string modKey, IReadOnlyList<ModPart> own, IReadOnlyList<ModPart> others, bool hasOthers)
    {
        ModKey = modKey;
        Own = own;
        Others = others;
        HasOthers = hasOthers;
    }

    // The project's mod key.
    public string ModKey { get; }

    public IReadOnlyList<ModPart> Own { get; }

    // Other mods' parts, in load order. A mod with the project's key is
    // left out when parts are looked up, since its keys would be the
    // project's own.
    public IReadOnlyList<ModPart> Others { get; }

    // True when a mods folder was read, so a Mod.Key that is not there is
    // known to be missing.
    public bool HasOthers { get; }

    // The keys of the project read from structure, with folderName as its
    // folder's name, and of the mods in modsFolder (none when null) except
    // the one in projectFolder.
    public static ProjectKeys Read(IniStructure structure, string folderName, string? modsFolder = null,
        string? projectFolder = null)
    {
        string modKey = ModKeyOf(structure, folderName);
        IReadOnlyList<ModPart> others = modsFolder is null ? [] : OtherMods(modsFolder, projectFolder);
        return new(modKey, PartsIn(structure, modKey, own: true), others, modsFolder is not null);
    }

    // The same other mods, with the project read again from structure.
    public ProjectKeys WithProject(IniStructure structure, string folderName)
    {
        string modKey = ModKeyOf(structure, folderName);
        return new(modKey, PartsIn(structure, modKey, own: true), Others, HasOthers);
    }

    // The parts of kind the project can name, its own first.
    public IEnumerable<ModPart> Of(RefKind kind) =>
        Own.Where(part => part.Kind == kind)
            .Concat(Others.Where(part => part.Kind == kind && !ModNames.Same(part.Mod, ModKey)));

    // The part value names, as mod_rules::FullKey reads it: Key of the
    // project, or Mod.Key, the project's own key included, without case.
    // Null when there is none.
    public ModPart? Find(RefKind kind, string value)
    {
        int dot = value.IndexOf('.');
        string mod = dot < 0 ? ModKey : value[..dot];
        string key = dot < 0 ? value : value[(dot + 1)..];
        IEnumerable<ModPart> parts = ModNames.Same(mod, ModKey) ? Own : Others;
        return parts.FirstOrDefault(part => part.Kind == kind && ModNames.Same(part.Mod, mod) && ModNames.Same(part.Key, key));
    }

    // key in the first [mod], else the folder name, as mod_rules::ModKey.
    public static string ModKeyOf(IniStructure structure, string folderName) =>
        structure.Lines.FirstOrDefault(line =>
            line.Kind == "value" && ModNames.Same(line.Section, "mod") && ModNames.Same(line.Key, "key"))?.Value
        ?? folderName;

    // Every part the sections of structure define, under modKey. Empty when
    // modKey is not a key the loader takes, since it then skips them all.
    public static IReadOnlyList<ModPart> PartsIn(IniStructure structure, string modKey, bool own)
    {
        List<ModPart> parts = [];
        if (!KeyValid(modKey))
        {
            return parts;
        }
        foreach (IniLine line in structure.Lines.Where(line => line.Kind == "section"))
        {
            foreach ((string prefix, RefKind kind) in Prefixes)
            {
                if (line.Section.Length <= prefix.Length
                    || !ModNames.Same(line.Section[..prefix.Length], prefix))
                {
                    continue;
                }
                string key = line.Section[prefix.Length..];
                if (KeyValid(key) && !parts.Any(part => part.Kind == kind && ModNames.Same(part.Key, key)))
                {
                    parts.Add(new ModPart(kind, modKey, key, own));
                }
            }
        }
        return parts;
    }

    // The parts of every mod folder in modsFolder with a canopy.ini, in load
    // order, skipping Canopy's own folder, folders turned off in
    // disabled.txt, and exceptFolder.
    public static IReadOnlyList<ModPart> OtherMods(string modsFolder, string? exceptFolder = null)
    {
        List<ModPart> parts = [];
        string[] folders;
        try
        {
            folders = Directory.GetDirectories(modsFolder);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return parts;
        }
        IReadOnlyList<string> turnedOff = TurnedOff(modsFolder);
        string? except = exceptFolder is null ? null : Path.TrimEndingDirectorySeparator(Path.GetFullPath(exceptFolder));
        foreach (string folder in folders.OrderBy(folder => Path.GetFileName(folder), ModNames.LoadOrder))
        {
            string name = Path.GetFileName(folder);
            if (ModNames.Same(name, ModNames.DataFolder) || turnedOff.Any(entry => ModNames.Same(entry, name))
                || (except is not null && string.Equals(Path.GetFullPath(folder), except, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }
            parts.AddRange(PartsOfFolder(folder, name));
        }
        return parts;
    }

    // 1 to 32 letters, digits, '_' or '-', as mod_rules::KeyValid.
    public static bool KeyValid(string key) =>
        key.Length is >= 1 and <= 32
        && key.All(c => c is (>= 'a' and <= 'z') or (>= 'A' and <= 'Z') or (>= '0' and <= '9') or '_' or '-');

    private static IReadOnlyList<ModPart> PartsOfFolder(string folder, string name)
    {
        string ini = GameFiles.Find(folder, ModProject.IniName);
        try
        {
            FileInfo info = new(ini);
            if (!info.Exists)
            {
                return [];
            }
            if (Cache.TryGetValue(ini, out CachedIni? cached) && cached.Time == info.LastWriteTimeUtc
                && cached.Size == info.Length && cached.Name == name)
            {
                return cached.Parts;
            }
            IniStructure structure = CanopyCore.IniParse(File.ReadAllText(ini, Encoding.UTF8));
            IReadOnlyList<ModPart> parts = structure.Ok
                ? PartsIn(structure, ModKeyOf(structure, name), own: false)
                : [];
            Cache[ini] = new CachedIni(info.LastWriteTimeUtc, info.Length, name, parts);
            return parts;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return [];
        }
    }

    // mods\CanopyData\disabled.txt as DisabledMods reads it, here from a
    // mods folder rather than a game folder.
    private static IReadOnlyList<string> TurnedOff(string modsFolder)
    {
        string path = GameFiles.Find(modsFolder, Path.Combine(ModNames.DataFolder, DisabledMods.FileName));
        try
        {
            return File.Exists(path)
                ? File.ReadAllText(path, Encoding.UTF8).Split('\n')
                    .Select(line => line.Trim(' ', '\t', '\r', '\n'))
                    .Where(line => line.Length > 0 && line[0] != '#')
                    .ToList()
                : [];
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return [];
        }
    }

    private sealed record CachedIni(DateTime Time, long Size, string Name, IReadOnlyList<ModPart> Parts);
}
