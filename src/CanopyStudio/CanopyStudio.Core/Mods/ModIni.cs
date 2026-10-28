using System.Text;

namespace CanopyStudio.Core.Mods;

// What the inventory needs from a canopy.ini: the [mod] key, name and
// version when set, and every section's name in file order.
public sealed record ModIniInfo(string? Key, string? Name, string? Version, IReadOnlyList<string> Sections);

// Reads a canopy.ini for the inventory. Null when the file cannot be read.
public interface IModIniReader
{
    ModIniInfo? Read(string path);
}

// A reader that follows src/CanopyCore/canopy_ini.cpp: UTF-8 text, a BOM
// skipped, [section] lines, key = value lines, ';' and '#' comments, keys
// without case, the first of a repeated key wins.
public sealed class SimpleModIniReader : IModIniReader
{
    private const char Bom = (char)0xFEFF;

    public ModIniInfo? Read(string path)
    {
        try
        {
            return Parse(File.ReadAllText(path, Encoding.UTF8));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    public static ModIniInfo Parse(string text)
    {
        List<string> sections = [];
        Dictionary<string, string> mod = [];
        bool inMod = false;
        foreach (string raw in text.TrimStart(Bom).Split('\n'))
        {
            string line = raw.Trim(' ', '\t', '\r', '\n');
            if (line.Length == 0 || line[0] is ';' or '#')
            {
                continue;
            }
            if (line[0] == '[' && line[^1] == ']')
            {
                string name = line[1..^1].Trim(' ', '\t');
                sections.Add(name);
                // FindSection takes the first [mod].
                inMod = ModNames.Same(name, "mod") && sections.Count(section => ModNames.Same(section, "mod")) == 1;
                continue;
            }
            int equals = line.IndexOf('=');
            if (equals < 0 || !inMod)
            {
                continue;
            }
            mod.TryAdd(ModNames.Lower(line[..equals].Trim(' ', '\t')), line[(equals + 1)..].Trim(' ', '\t'));
        }
        return new(Value(mod, "key"), Value(mod, "name"), Value(mod, "version"), sections);
    }

    private static string? Value(Dictionary<string, string> mod, string key) =>
        mod.TryGetValue(key, out string? value) && value.Length > 0 ? value : null;
}
