using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Mods;

// What a mod is made of. A mod can be several at once; SettingsOnly means a
// canopy.ini and none of the others.
[Flags]
public enum ModKind
{
    None = 0,
    // A DLL file directly in mods, which Canopy loads.
    Dll = 1,
    // lua\main.lua in the mod's folder.
    Lua = 2,
    // canopy.ini sections other than [mod] and [setting.*]: items,
    // villagers, recipes, art and the rest.
    Content = 4,
    // A data folder of replacement game files.
    Files = 8,
    SettingsOnly = 16,
    // A DLL in the mod's folder. Canopy loads only DLL files directly in
    // mods, so this one never loads.
    FolderDll = 32,
}

// One file a mod replaces: Key is the game path as Canopy's lookup sees it
// (data/database/string.dat), Path the file below the mod folder as it is
// on disk.
public sealed record ModOverride(string Key, string Path);

// One mod in mods. Name is the folder or DLL file name on disk; Key,
// Title and Version come from canopy.ini's [mod], ProductName and
// ProductVersion from the DLL's version resource, each null when not set.
public sealed record ModEntry(
    string Name,
    bool IsDllFile,
    ModKind Kind,
    string? Key,
    string? Title,
    string? Version,
    string? ProductName,
    string? ProductVersion,
    bool Disabled,
    IReadOnlyList<ModOverride> Overrides);

// Two or more turned-on mods that ship one game path. Mods are in load
// order; Canopy serves the last one's file, Winner.
public sealed record OverrideConflict(string Key, IReadOnlyList<string> Mods, string Winner);

// Lists the mods in a game folder without changing anything.
public static class ModInventory
{
    private const string IniFile = "canopy.ini";
    private const string DataFolder = "data";
    // As in src/Canopy/file_overrides.cpp: deeper folders are not indexed,
    // the game opens loose files without the long path prefix, and its
    // lookup copies a path into a 512-byte buffer.
    private const int MaxDepth = 16;
    private const int MaxPath = 260;
    private const int MaxKeyLength = 511;
    // EnumerateFiles's own options, blind to case on Linux too, where Wine
    // loads X.DLL as readily as x.dll.
    private static readonly EnumerationOptions DllFiles = new()
    {
        MatchType = MatchType.Win32,
        MatchCasing = MatchCasing.CaseInsensitive,
        AttributesToSkip = 0,
        IgnoreInaccessible = false,
    };

    // Every folder in mods but CanopyData and every DLL file directly in
    // mods, in load order. Empty when there is no mods folder.
    public static IReadOnlyList<ModEntry> Scan(string gameFolder, IModIniReader? ini = null)
    {
        ini ??= new SimpleModIniReader();
        DirectoryInfo mods = new(ModNames.ModsIn(gameFolder));
        if (!mods.Exists)
        {
            return [];
        }
        IReadOnlyList<string> disabled = DisabledMods.Read(gameFolder);
        List<ModEntry> entries = [];
        foreach (FileSystemInfo item in mods.EnumerateFileSystemInfos("*", new EnumerationOptions { AttributesToSkip = 0 }))
        {
            bool off = DisabledMods.IsDisabled(disabled, item.Name);
            if (item is DirectoryInfo folder && !ModNames.Same(folder.Name, ModNames.DataFolder))
            {
                entries.Add(ScanFolder(folder, off, ini));
            }
            else if (item is FileInfo file && ModNames.IsDll(file.Name))
            {
                (string? product, string? version) = Product(file.FullName);
                entries.Add(new(file.Name, true, ModKind.Dll, null, null, null, product, version, off, []));
            }
        }
        entries.Sort((left, right) => ModNames.Compare(left.Name, right.Name));
        return entries;
    }

    // Every game path shipped by more than one turned-on mod, in path order.
    public static IReadOnlyList<OverrideConflict> Conflicts(IReadOnlyList<ModEntry> mods)
    {
        Dictionary<string, List<string>> shippers = new(StringComparer.Ordinal);
        foreach (ModEntry mod in mods.Where(mod => !mod.Disabled).OrderBy(mod => mod.Name, ModNames.LoadOrder))
        {
            foreach (ModOverride file in mod.Overrides)
            {
                if (!shippers.TryGetValue(file.Key, out List<string>? names))
                {
                    shippers[file.Key] = names = [];
                }
                names.Add(mod.Name);
            }
        }
        return shippers.Where(pair => pair.Value.Count > 1)
            .OrderBy(pair => pair.Key, StringComparer.Ordinal)
            .Select(pair => new OverrideConflict(pair.Key, pair.Value, pair.Value[^1]))
            .ToList();
    }

    private static ModEntry ScanFolder(DirectoryInfo folder, bool disabled, IModIniReader ini)
    {
        ModKind kind = ModKind.None;
        string? product = null;
        string? productVersion = null;
        FileInfo? dll = folder.EnumerateFiles("*.dll", DllFiles).Where(file => ModNames.IsDll(file.Name))
            .OrderBy(file => file.Name, ModNames.LoadOrder).FirstOrDefault();
        if (dll is not null)
        {
            kind |= ModKind.FolderDll;
            (product, productVersion) = Product(dll.FullName);
        }
        if (File.Exists(GameFiles.Find(folder.FullName, Path.Combine("lua", "main.lua"))))
        {
            kind |= ModKind.Lua;
        }
        string iniPath = GameFiles.Find(folder.FullName, IniFile);
        ModIniInfo? info = File.Exists(iniPath) ? ini.Read(iniPath) : null;
        if (info is not null && info.Sections.Any(name => !ModNames.Same(name, "mod")
            && !ModNames.Lower(name).StartsWith("setting.", StringComparison.Ordinal)))
        {
            kind |= ModKind.Content;
        }
        List<ModOverride> overrides = [];
        DirectoryInfo data = new(GameFiles.Find(folder.FullName, DataFolder));
        if (data.Exists)
        {
            kind |= ModKind.Files;
            Walk(data, DataFolder, data.Name, 0, overrides);
            overrides.Sort((left, right) => string.CompareOrdinal(left.Key, right.Key));
        }
        if (kind == ModKind.None && File.Exists(iniPath))
        {
            kind = ModKind.SettingsOnly;
        }
        return new(folder.Name, false, kind, info?.Key, info?.Name, info?.Version, product, productVersion, disabled, overrides);
    }

    // Adds the files below folder that Canopy would serve, as its index
    // does: hidden and system files skipped, names outside ASCII skipped,
    // keys lowercase with forward slashes.
    private static void Walk(DirectoryInfo folder, string key, string path, int depth, List<ModOverride> files)
    {
        foreach (FileSystemInfo item in folder.EnumerateFileSystemInfos("*", new EnumerationOptions { AttributesToSkip = 0 }))
        {
            bool isFolder = item is DirectoryInfo;
            if ((!isFolder && (item.Attributes & (FileAttributes.Hidden | FileAttributes.System)) != 0)
                || item.Name.Any(c => c == 0 || c > 0x7F))
            {
                continue;
            }
            string child = key + "/" + ModNames.Lower(item.Name);
            string childPath = Path.Combine(path, item.Name);
            if (item is DirectoryInfo sub)
            {
                if (depth < MaxDepth)
                {
                    Walk(sub, child, childPath, depth + 1, files);
                }
            }
            else if (item.FullName.Length < MaxPath && child.Length <= MaxKeyLength)
            {
                files.Add(new(child, childPath));
            }
        }
    }

    // A DLL that cannot be read, or carries no version, has neither.
    private static (string? Name, string? Version) Product(string path)
    {
        DllVersion? info = DllVersions.Read(path).Version;
        return (Blank(info?.ProductName), Blank(info?.ProductVersionText));
    }

    private static string? Blank(string? text) => string.IsNullOrWhiteSpace(text) ? null : text.Trim();
}
