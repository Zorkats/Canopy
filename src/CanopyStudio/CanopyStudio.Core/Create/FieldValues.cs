using System.Globalization;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Create;

// How the plain editors read and write values, the way the rules read them
// (src/CanopyCore/rules_*.cpp). The editors never rewrite a value just by
// showing it; these only matter once the user changes one.

// on and off. ParseSwitch (rules_flags.cpp) takes on, true, yes and 1, or
// off, false, no, 0 and nothing; Truthy (canopy_ini.cpp) the same ons.
public static class SwitchText
{
    // The words to write: true and false when the field's default is
    // spelled that way, as [mod] unsafe is, else on and off.
    public static (string On, string Off) Words(string? defaultValue) =>
        defaultValue is not null && (defaultValue.Equals("true", StringComparison.OrdinalIgnoreCase)
            || defaultValue.Equals("false", StringComparison.OrdinalIgnoreCase))
            ? ("true", "false")
            : ("on", "off");

    // True or false for a value the rules read as on or off, null for one
    // they do not.
    public static bool? Read(string? value)
    {
        string text = (value ?? "").Trim().ToLowerInvariant();
        return text switch
        {
            "on" or "true" or "yes" or "1" => true,
            "off" or "false" or "no" or "0" or "" => false,
            _ => null,
        };
    }
}

// Comma-separated lists, as SplitList (mod_rules.cpp) reads them.
public static class ListText
{
    // The parts of text, each trimmed, empty ones left out.
    public static IReadOnlyList<string> Split(string? text) =>
        (text ?? "").Split(',').Select(part => part.Trim()).Where(part => part.Length > 0).ToList();

    public static string Join(IEnumerable<string> parts) =>
        string.Join(", ", parts.Select(part => part.Trim()).Where(part => part.Length > 0));
}

// Numbers as the rules read them: whole numbers in the int32 or int64
// range, and decimals with a dot.
public static class NumberText
{
    // Why text is not a whole number within min and max (when min < max),
    // or null when it is. A leading '+' is taken, as rules_mod.cpp does.
    public static string? WholeProblem(string text, long? min, long? max)
    {
        string trimmed = text.Trim();
        if (trimmed.StartsWith('+'))
        {
            trimmed = trimmed[1..];
        }
        if (!long.TryParse(trimmed, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long value))
        {
            return "This is not a whole number.";
        }
        if (min is { } low && max is { } high && low < high && (value < low || value > high))
        {
            return $"This is outside {low} to {high}.";
        }
        return null;
    }

    public static string? DecimalProblem(string text) =>
        double.TryParse(text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out _)
            ? null
            : "This is not a number. Write decimals with a dot, such as 1.5.";

    // "min to max" for a field with a range, else null.
    public static string? Range(long? min, long? max) =>
        min is { } low && max is { } high && low < high ? $"{low} to {high}" : null;
}

// Files a value names inside the mod folder: relative, with '/' between
// folders, as the docs write them (art/lantern.png); the rules find each
// part as Canopy would, either separator and on Linux without case, and
// refuse a path that leaves the folder.
public static class ProjectPaths
{
    // file as a path inside folder, or null when it is not inside it.
    public static string? Relative(string folder, string file)
    {
        string full = Path.GetFullPath(file);
        if (!ModNames.Inside(full, folder))
        {
            return null;
        }
        string relative = Path.GetRelativePath(Path.GetFullPath(folder), full);
        return relative == "." ? null : relative.Replace('\\', '/');
    }

    // The full path of value inside folder, or null when value is not a
    // relative path that stays inside it.
    public static string? Full(string folder, string value)
    {
        string text = value.Trim();
        if (text.Length == 0 || Path.IsPathRooted(text) || text.Contains(':'))
        {
            return null;
        }
        try
        {
            string full = Path.GetFullPath(GameFiles.Find(folder, text));
            return ModNames.Inside(full, folder) ? full : null;
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException
            or PathTooLongException)
        {
            return null;
        }
    }
}
