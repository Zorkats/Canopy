using System.Text;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Lua;

// One Lua file of a mod: its path below the mod's lua folder with '/'
// between folders (util/text.lua), and the name require loads it by
// (util.text).
public sealed record LuaFileName(string Relative, string Module);

// A mod's Lua files, read and written the way the Lua host loads them
// (src/Canopy/lua_host.cpp): lua\main.lua runs first, require(name) loads
// lua\<name>.lua with dots as folders, a name is letters, digits and '_'
// with single dots between folders, up to 64 characters, and a file is at
// most 1 MB.
public static class LuaFiles
{
    public const string Folder = "lua";
    public const string MainModule = "main";
    public const int MaxModuleLength = 64;
    public const int MaxBytes = 1024 * 1024;

    // The Lua files below the mod's lua folder, main.lua first, then by
    // path. Files whose path is no valid module name are left out, since
    // require cannot load them.
    public static IReadOnlyList<LuaFileName> List(string modFolder)
    {
        string root = GameFiles.Find(modFolder, Folder);
        if (!Directory.Exists(root))
        {
            return [];
        }
        List<LuaFileName> files = [];
        foreach (string path in Directory.EnumerateFiles(root, "*.lua", SearchOption.AllDirectories))
        {
            string relative = Path.GetRelativePath(root, path).Replace('\\', '/');
            string module = relative[..^".lua".Length].Replace('/', '.');
            if (ModuleProblem(module) is null)
            {
                files.Add(new LuaFileName(relative, module));
            }
        }
        return files
            .OrderBy(file => file.Module == MainModule ? 0 : 1)
            .ThenBy(file => file.Relative, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    // Why require cannot load module, null when it can.
    public static string? ModuleProblem(string module)
    {
        if (module.Length == 0)
        {
            return "Type a name, such as main or util.text.";
        }
        if (module.Length > MaxModuleLength)
        {
            return $"A name is at most {MaxModuleLength} characters.";
        }
        if (module[0] == '.' || module[^1] == '.' || module.Contains("..", StringComparison.Ordinal))
        {
            return "Dots go only between folder names, one at a time.";
        }
        if (module.Any(c => c != '.' && !LuaTokens.IsNamePart(c)))
        {
            return "Use letters, digits and _, with dots between folders.";
        }
        return null;
    }

    // The name typed in a "new file" box as a module: util/text.lua and
    // util.text are both util.text.
    public static string ModuleOf(string typed)
    {
        string name = typed.Trim().Replace('\\', '/');
        if (name.EndsWith(".lua", StringComparison.OrdinalIgnoreCase))
        {
            name = name[..^".lua".Length];
        }
        return name.Replace('/', '.');
    }

    public static LuaFileName NameOf(string module) => new(module.Replace('.', '/') + ".lua", module);

    // The full path of file in modFolder.
    public static string PathOf(string modFolder, LuaFileName file) =>
        GameFiles.Find(modFolder, Folder + "/" + file.Relative);

    // The text of file, a UTF-8 byte order mark dropped as the host drops
    // it. Null, with error set, when it cannot be read.
    public static string? Read(string modFolder, LuaFileName file, out string? error)
    {
        string path = PathOf(modFolder, file);
        try
        {
            error = null;
            return File.ReadAllText(path, new UTF8Encoding(false));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            error = $"{GameFiles.Shown(Folder + "/" + file.Relative)} could not be read: {exception.Message}";
            return null;
        }
    }

    // Writes text to file through a temporary file, making its folders.
    // Null when saved, else why not.
    public static string? Write(string modFolder, LuaFileName file, string text)
    {
        string path = PathOf(modFolder, file);
        string temporary = path + ".new";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(temporary, text, new UTF8Encoding(false));
            BusyFiles.Replace(temporary, path);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{GameFiles.Shown(Folder + "/" + file.Relative)} could not be saved: {exception.Message}";
        }
    }

    // Why the game would refuse text, null when it would not.
    public static string? SizeProblem(string text) =>
        Encoding.UTF8.GetByteCount(text) > MaxBytes
            ? "This file is larger than 1 MB, so the game will not load it."
            : null;

    // What a new main.lua starts with.
    public static string MainTemplate { get; } =
        "-- Runs once as the game starts. How Lua mods work: " + CanopyDocs.Url("docs/LUA.md") + "\n"
        + "canopy.on(\"control\", function()\n"
        + "  canopy.log(\"The player is on the map.\")\n"
        + "end)\n";
}
