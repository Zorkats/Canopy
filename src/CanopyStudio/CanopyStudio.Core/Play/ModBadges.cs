using System.Text;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Play;

// One badge on a mod's row. Warning badges stand out; Help is the sentence
// that explains the badge.
public sealed record ModBadge(string Text, bool Warning, string Help);

// The badges a mod's row shows: what the mod is made of, then what to watch
// out for.
public static class ModBadges
{
    public const string FolderDllHelp = "This DLL is inside a folder, so Canopy never loads it.";
    public static readonly string NothingHelp = $"This folder has no canopy.ini, {GameFiles.Shown("lua/main.lua")}, data folder or DLL, so Canopy has nothing to load from it.";
    public const string UnsafeHelp = "This mod runs Lua with full game access: it can change story flags, quests and saves.";
    public const string ConflictHelp = "This mod ships a game file that another turned-on mod ships too. Only the last in load order is used.";

    public static IReadOnlyList<ModBadge> For(ModEntry mod, bool unsafeLua, bool inConflict)
    {
        List<ModBadge> badges = [];
        if (mod.Kind.HasFlag(ModKind.Dll))
        {
            badges.Add(new("DLL", false, "A DLL in mods, which Canopy loads as the game starts."));
        }
        if (mod.Kind.HasFlag(ModKind.Lua))
        {
            badges.Add(new("Lua", false, $"Runs {GameFiles.Shown("lua/main.lua")}."));
        }
        if (mod.Kind.HasFlag(ModKind.Content))
        {
            badges.Add(new("Content", false, "Adds or changes items, villagers, recipes, art or more through canopy.ini."));
        }
        if (mod.Kind.HasFlag(ModKind.Files))
        {
            badges.Add(new("Files", false, "Replaces game files with the ones in its data folder."));
        }
        if (mod.Kind.HasFlag(ModKind.SettingsOnly))
        {
            badges.Add(new("Settings only", false, "Has a canopy.ini with settings and nothing else."));
        }
        if (mod.Kind.HasFlag(ModKind.FolderDll))
        {
            badges.Add(new("DLL never loads", true, FolderDllHelp));
        }
        if (mod.Kind == ModKind.None)
        {
            badges.Add(new("Nothing to load", true, NothingHelp));
        }
        if (unsafeLua)
        {
            badges.Add(new("Unsafe Lua", true, UnsafeHelp));
        }
        if (inConflict)
        {
            badges.Add(new("Conflict", true, ConflictHelp));
        }
        return badges;
    }

    // True when name is one of the mods in any conflict.
    public static bool InConflict(IReadOnlyList<OverrideConflict> conflicts, string name) =>
        conflicts.Any(conflict => conflict.Mods.Any(mod => ModNames.Same(mod, name)));

    // True when the first [mod] section of ini sets unsafe to a true value,
    // read as the loader reads it: the first unsafe key wins, and true, 1,
    // yes and on are true, in any case.
    public static bool UnsafeLua(IniStructure ini)
    {
        bool seenMod = false;
        bool inMod = false;
        foreach (IniLine line in ini.Lines)
        {
            if (line.Kind == "section")
            {
                inMod = !seenMod && ModNames.Same(line.Section, "mod");
                seenMod |= inMod;
            }
            else if (inMod && line.Kind == "value" && ModNames.Same(line.Key, "unsafe"))
            {
                return ModNames.Lower(line.Value.Trim()) is "true" or "1" or "yes" or "on";
            }
        }
        return false;
    }

    // UnsafeLua of modFolder\canopy.ini, parsed by canopy_core.dll. False
    // when there is no canopy.ini or it cannot be read.
    public static bool ReadUnsafeLua(string modFolder)
    {
        string path = GameFiles.Find(modFolder, "canopy.ini");
        try
        {
            if (!File.Exists(path))
            {
                return false;
            }
            IniStructure ini = CanopyCore.IniParse(File.ReadAllText(path, Encoding.UTF8));
            return ini.Ok && UnsafeLua(ini);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }
}
