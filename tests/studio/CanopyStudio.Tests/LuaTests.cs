using System.Text.RegularExpressions;
using CanopyStudio.Core.Lua;

namespace CanopyStudio.Tests;

public sealed class LuaTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-lua-" + Guid.NewGuid().ToString("N"));

    public LuaTests() => Directory.CreateDirectory(root);

    public void Dispose()
    {
        try
        {
            Directory.Delete(root, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static LuaCatalog Catalog => LuaCatalog.Default;

    // The repository's root, found from the test's folder, or null outside
    // a checkout.
    private static string? RepoRoot()
    {
        for (DirectoryInfo? folder = new(AppContext.BaseDirectory); folder is not null; folder = folder.Parent)
        {
            if (File.Exists(Path.Combine(folder.FullName, "src", "Canopy", "lua_commands.h")))
            {
                return folder.FullName;
            }
        }
        return null;
    }

    private static string[] Names(LuaCompletionResult? result) =>
        result?.Entries.Select(entry => entry.Name).ToArray() ?? [];

    // The completion at the | in text.
    private static LuaCompletionResult? At(string text, bool unsafeMod = false)
    {
        int caret = text.IndexOf('|');
        return LuaCompletion.At(text.Remove(caret, 1), caret, Catalog, unsafeMod);
    }

    [Fact]
    public void TheCatalogHasTheCanopyApiTheEventsAndTheSandbox()
    {
        LuaEntry onUse = Catalog.Find("canopy.items.on_use")!;
        Assert.Equal(LuaEntryKind.Function, onUse.Kind);
        Assert.Equal("canopy.items.on_use(key, handler)", onUse.Signature);
        Assert.Contains("use = script", onUse.Doc);
        Assert.Equal(LuaEntryKind.Field, Catalog.Find("canopy.key")!.Kind);
        Assert.Equal(LuaEntryKind.Table, Catalog.Find("canopy.items")!.Kind);
        Assert.Equal(["title", "save_loaded", "control", "no_control", "frame"], Catalog.Events.Select(entry => entry.Name));
        Assert.Equal(LuaEntryKind.Command, Catalog.Find("GameGetFlag")!.Kind);
        Assert.Equal(LuaEntryKind.Guarded, Catalog.Find("GameSetFlag")!.Kind);
        Assert.Equal(LuaEntryKind.Function, Catalog.Find("string.format")!.Kind);
        Assert.Null(Catalog.Find("io"));
        Assert.Null(Catalog.Find("math.randomseed"));
        Assert.All(Catalog.Entries, entry => Assert.False(string.IsNullOrWhiteSpace(entry.Doc), entry.Name));
    }

    // The catalog is generated; this fails while it is behind its sources.
    [Fact]
    public void TheCatalogFollowsLuaDocAndTheCommandLists()
    {
        if (RepoRoot() is not { } repo)
        {
            return;
        }
        string header = File.ReadAllText(Path.Combine(repo, "src", "Canopy", "lua_commands.h"));
        foreach ((string list, LuaEntryKind kind) in new[] { ("kAllowed", LuaEntryKind.Command), ("kGuarded", LuaEntryKind.Guarded) })
        {
            string body = Regex.Match(header, @"constexpr const char\* " + list + @"\[\] = \{(.*?)\};", RegexOptions.Singleline).Groups[1].Value;
            string[] commands = Regex.Matches(body, "\"(\\w+)\"").Select(match => match.Groups[1].Value).ToArray();
            Assert.NotEmpty(commands);
            Assert.Equal(commands, Catalog.Entries.Where(entry => entry.Kind == kind).Select(entry => entry.Name));
        }

        string[] documented = File.ReadAllLines(Path.Combine(repo, "docs", "LUA.md"))
            .Where(line => line.StartsWith("### ", StringComparison.Ordinal))
            .SelectMany(line => Regex.Matches(line, @"(canopy(?:\.\w+)+)").Select(match => match.Groups[1].Value))
            .ToArray();
        Assert.Contains("canopy.items.on_use", documented);
        Assert.All(documented, name => Assert.NotNull(Catalog.Find(name)));
        Assert.Equal(documented.Order(), Catalog.Entries
            .Where(entry => entry.Name.StartsWith("canopy.", StringComparison.Ordinal) && entry.Kind != LuaEntryKind.Table)
            .Select(entry => entry.Name).Order());
    }

    [Fact]
    public void TokensFollowLuasLexer()
    {
        const string text = "local s = \"a\\\"b\" -- note\nx = [==[long]]\n]==] --[[ block\n]] 0x1F 1.5e-3 'cut\nend";
        LuaToken[] tokens = LuaTokens.Scan(text).ToArray();
        string[] shown = tokens.Select(token => $"{token.Kind}:{text.Substring(token.Start, token.Length)}").ToArray();
        Assert.Equal([
            "Keyword:local", "Name:s", "String:\"a\\\"b\"", "Comment:-- note", "Name:x", "String:[==[long]]\n]==]",
            "Comment:--[[ block\n]]", "Number:0x1F", "Number:1.5e-3", "String:'cut", "Keyword:end",
        ], shown);
        Assert.False(tokens[9].Closed);
        Assert.True(tokens[2].Closed);

        LuaToken open = Assert.Single(LuaTokens.Scan("--[[ never closed"));
        Assert.Equal((LuaTokenKind.Comment, false), (open.Kind, open.Closed));
        Assert.False(Assert.Single(LuaTokens.Scan("-- to the end")).Closed);
        Assert.True(Assert.Single(LuaTokens.Scan("-- ended\n")).Closed);
    }

    [Fact]
    public void MembersFollowADottedName()
    {
        LuaCompletionResult items = At("canopy.items.|")!;
        Assert.Equal(["canopy.items.give", "canopy.items.count", "canopy.items.id", "canopy.items.on_use"], Names(items));
        Assert.Equal(("", 13), (items.Typed, items.Start));

        LuaCompletionResult typed = At("  canopy.items.ON|")!;
        Assert.Equal(["canopy.items.on_use"], Names(typed));
        Assert.Equal(("ON", 15), (typed.Typed, typed.Start));

        Assert.Contains("canopy.save", Names(At("canopy.|")));
        Assert.Contains("canopy.on", Names(At("canopy.o|")));
        Assert.Equal(["string.format"], Names(At("string.form|")));
        Assert.Null(At("canopy.items.zzz|"));
        Assert.Null(At("nothing.|"));
        Assert.Null(At("f().|"));
        Assert.Null(At("x = \"a\" ..|"));
    }

    [Fact]
    public void TopLevelNamesHideGuardedCommandsFromSafeMods()
    {
        string[] safe = Names(At("Game|"));
        Assert.Contains("GameGetFlag", safe);
        Assert.DoesNotContain("GameSetFlag", safe);
        Assert.Contains("GameSetFlag", Names(At("Game|", unsafeMod: true)));
        Assert.Equal(["canopy"], Names(At("x = can|")));
        Assert.Contains("function", Names(At("func|")));
        Assert.Null(At("x = 12|"));
    }

    [Fact]
    public void StringsAndCommentsOfferNothingButCanopyOnsEvents()
    {
        LuaCompletionResult events = At("canopy.on(\"co|")!;
        Assert.Equal(["control"], Names(events));
        Assert.Equal(("co", 11), (events.Typed, events.Start));
        Assert.Equal(5, At("canopy.on( '|")!.Entries.Count);
        Assert.Null(At("canopy.log(\"co|"));
        Assert.Null(At("-- canopy.|"));
        Assert.Null(At("--[[ canopy.\n items.|"));
        Assert.NotNull(At("-- note\ncanopy.|"));
        Assert.NotNull(At("x = \"done\" canopy.|"));
    }

    [Fact]
    public void FilesAreListedReadAndWrittenAsTheHostLoadsThem()
    {
        Assert.Empty(LuaFiles.List(root));
        LuaFileName util = LuaFiles.NameOf("util.text");
        Assert.Equal("util/text.lua", util.Relative);
        Assert.Null(LuaFiles.Write(root, util, "return 1\n"));
        Assert.Null(LuaFiles.Write(root, LuaFiles.NameOf("main"), LuaFiles.MainTemplate));
        Assert.Null(LuaFiles.Write(root, LuaFiles.NameOf("alpha"), ""));
        File.WriteAllText(Path.Combine(root, "lua", "bad name.lua"), "");
        File.WriteAllText(Path.Combine(root, "lua", "notes.txt"), "");

        Assert.Equal(["main.lua", "alpha.lua", "util/text.lua"], LuaFiles.List(root).Select(file => file.Relative));
        Assert.Equal(["main", "alpha", "util.text"], LuaFiles.List(root).Select(file => file.Module));
        Assert.Equal("return 1\n", LuaFiles.Read(root, util, out string? error));
        Assert.Null(error);
        Assert.False(File.Exists(LuaFiles.PathOf(root, util) + ".new"));

        // A byte order mark is dropped, as the host drops it.
        File.WriteAllBytes(LuaFiles.PathOf(root, util), [0xEF, 0xBB, 0xBF, (byte)'x']);
        Assert.Equal("x", LuaFiles.Read(root, util, out _));
        Assert.Null(LuaFiles.Read(root, LuaFiles.NameOf("gone"), out error));
        Assert.Contains("could not be read", error);
        Assert.StartsWith("-- Runs once", LuaFiles.MainTemplate);
    }

    [Fact]
    public void ModuleNamesFollowRequiresRules()
    {
        Assert.Null(LuaFiles.ModuleProblem("main"));
        Assert.Null(LuaFiles.ModuleProblem("util.text_2"));
        Assert.NotNull(LuaFiles.ModuleProblem(""));
        Assert.NotNull(LuaFiles.ModuleProblem(".x"));
        Assert.NotNull(LuaFiles.ModuleProblem("x."));
        Assert.NotNull(LuaFiles.ModuleProblem("a..b"));
        Assert.NotNull(LuaFiles.ModuleProblem("a-b"));
        Assert.NotNull(LuaFiles.ModuleProblem(new string('a', 65)));
        Assert.Null(LuaFiles.ModuleProblem(new string('a', 64)));
        Assert.Equal("util.text", LuaFiles.ModuleOf(" util/text.lua "));
        Assert.Equal("util.text", LuaFiles.ModuleOf("util\\text.LUA"));
        Assert.Equal("util.text", LuaFiles.ModuleOf("util.text"));
        Assert.Null(LuaFiles.SizeProblem("x"));
        Assert.NotNull(LuaFiles.SizeProblem(new string('x', LuaFiles.MaxBytes + 1)));
    }
}
