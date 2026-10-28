using System.Text;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Mods;

// mods\CanopyData\disabled.txt, the list of mods turned off without being
// removed, in the format of src/CanopyCore/disabled_mods.h: one folder or
// DLL file name in mods per line, matched without case; blank lines and
// lines starting with # are ignored. Canopy reads it as the game starts.
// A change keeps every other line as it was, goes through a temporary file
// in the same folder and is read back before it counts.
public static class DisabledMods
{
    public const string FileName = "disabled.txt";

    public static string PathIn(string gameFolder) =>
        GameFiles.Find(gameFolder, Path.Combine(ModNames.ModsFolder, ModNames.DataFolder, FileName));

    // The names in the list, trimmed. Empty when there is no list.
    public static IReadOnlyList<string> Read(string gameFolder)
    {
        string path = PathIn(gameFolder);
        try
        {
            return File.Exists(path) ? Names(File.ReadAllText(path, Encoding.UTF8)) : [];
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return [];
        }
    }

    public static bool IsDisabled(IReadOnlyList<string> list, string name) => list.Any(entry => ModNames.Same(entry, name));

    // Turns the mod named name, a folder or DLL file name in mods, on or
    // off. Canopy sees the change at the next game start.
    public static ModResult SetEnabled(string gameFolder, string name, bool enabled)
    {
        string? problem = ModNames.NameProblem(name);
        if (problem is not null || name.Trim() != name || name.StartsWith('#'))
        {
            return new(false, $"\"{name}\" cannot be listed as a mod name, so nothing was changed{(problem is null ? "" : $": it {problem}")}.");
        }
        if (ModNames.Same(name, ModNames.DataFolder))
        {
            return new(false, "CanopyData is Canopy's own folder, not a mod, so it cannot be turned off.");
        }
        string path = PathIn(gameFolder);
        string temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
        try
        {
            bool existed = File.Exists(path);
            byte[] old = existed ? File.ReadAllBytes(path) : [];
            bool bom = old.AsSpan().StartsWith(Encoding.UTF8.Preamble);
            string text = new UTF8Encoding(false).GetString(old, bom ? 3 : 0, old.Length - (bom ? 3 : 0));
            if (IsDisabled(Names(text), name) != enabled)
            {
                return new(true, enabled ? $"{name} is already on." : $"{name} is already off.");
            }
            string next = enabled ? Without(text, name) : With(text, name);
            byte[] bytes = [.. (bom ? Encoding.UTF8.Preamble : []), .. new UTF8Encoding(false).GetBytes(next)];

            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(temporary, bytes);
            BusyFiles.Replace(temporary, path);
            if (!File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes) || IsDisabled(Read(gameFolder), name) == enabled)
            {
                if (existed)
                {
                    File.WriteAllBytes(path, old);
                }
                else
                {
                    File.Delete(path);
                }
                return new(false, $"{FileName} did not read back as it was written, so it was put back. Check that nothing else is writing the mods folder.");
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            TryDelete(temporary);
            return new(false, $"{name} could not be turned {(enabled ? "on" : "off")}: {exception.Message}");
        }
        return new(true, enabled
            ? $"{name} is on. It loads the next time the game starts."
            : $"{name} is off. It stays in the mods folder and is skipped the next time the game starts.");
    }

    private static List<string> Names(string text) => text.Split('\n')
        .Select(line => line.Trim(' ', '\t', '\r', '\n'))
        .Where(line => line.Length > 0 && line[0] != '#')
        .ToList();

    // text with name added on a line of its own, in the file's line ending.
    private static string With(string text, string name)
    {
        string newline = text.Contains("\r\n") || !text.Contains('\n') ? "\r\n" : "\n";
        bool ended = text.Length == 0 || text.EndsWith('\n');
        return text + (ended ? "" : newline) + name + newline;
    }

    // text without the lines that name name; every other line, comments and
    // blank lines too, is kept with its own line ending.
    private static string Without(string text, string name)
    {
        StringBuilder kept = new();
        int start = 0;
        while (start < text.Length)
        {
            int end = text.IndexOf('\n', start);
            end = end < 0 ? text.Length : end + 1;
            string line = text[start..end];
            string trimmed = line.Trim(' ', '\t', '\r', '\n');
            if (trimmed.StartsWith('#') || !ModNames.Same(trimmed, name))
            {
                kept.Append(line);
            }
            start = end;
        }
        return kept.ToString();
    }

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }
}
