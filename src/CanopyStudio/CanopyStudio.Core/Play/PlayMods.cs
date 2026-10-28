using System.Globalization;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Play;

// How the Play area names mods, sorts the check's messages among them and
// says what undo would take back.
public static class PlayMods
{
    // The [mod] name, else the DLL's product name, else the name on disk.
    public static string Title(ModEntry mod) => mod.Title ?? mod.ProductName ?? mod.Name;

    // The [mod] version, else the DLL's product version; null when neither.
    public static string? Version(ModEntry mod) => mod.Version ?? mod.ProductVersion;

    // The check's messages about the mod named name, in the check's order.
    public static IReadOnlyList<ModDiagnostic> Of(IReadOnlyList<ModDiagnostic> diagnostics, string name) =>
        diagnostics.Where(diagnostic => diagnostic.Folder.Length > 0 && ModNames.Same(diagnostic.Folder, name)).ToList();

    // The messages about no one folder, and those about a folder that is not
    // one of mods, so none is lost.
    public static IReadOnlyList<ModDiagnostic> General(IReadOnlyList<ModDiagnostic> diagnostics, IReadOnlyList<ModEntry> mods) =>
        diagnostics.Where(diagnostic => diagnostic.Folder.Length == 0
            || !mods.Any(mod => ModNames.Same(mod.Name, diagnostic.Folder))).ToList();

    // True when "Show me" can open the message's line: it names a mod folder
    // and a line of its canopy.ini.
    public static bool CanShow(ModDiagnostic diagnostic) => diagnostic.Folder.Length > 0 && diagnostic.Line > 0;

    // What undoing change does, for a button's caption: "the install of X,
    // made Oct 15 at 14:30".
    public static string Describe(ModChange change, CultureInfo? culture = null)
    {
        culture ??= CultureInfo.CurrentCulture;
        string what = change.Kind == ModChangeKind.Install ? $"the install of {change.Mod}" : $"the removal of {change.Mod}";
        return string.Create(culture, $"{what}, made {change.When:MMM d} at {change.When:HH:mm}");
    }
}
