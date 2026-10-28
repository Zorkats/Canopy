using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using CanopyStudio.Core.Mods;

namespace CanopyStudio.Tests;

// The mod inventory and its changes on throwaway game and backup folders.
// Nothing here touches a real game folder or %APPDATA%.
public sealed class ModsTests : IDisposable
{
    private static readonly DateTime Now = new(2026, 10, 15, 14, 30, 15);

    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-mods-" + Guid.NewGuid().ToString("N"));
    private readonly string game;
    private readonly string backups;

    public ModsTests()
    {
        game = Path.Combine(root, "game");
        backups = Path.Combine(root, "backups");
        Directory.CreateDirectory(game);
        File.WriteAllBytes(Path.Combine(game, "village.exe"), [0x4D, 0x5A]);
    }

    public void Dispose()
    {
        // A junction goes first, on its own, so nothing follows it.
        string link = Path.Combine(game, "mods", "Linked");
        if (Directory.Exists(link))
        {
            Directory.Delete(link);
        }
        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(file, FileAttributes.Normal);
        }
        Directory.Delete(root, recursive: true);
    }

    private string Put(string path, string text) => Put(path, Encoding.UTF8.GetBytes(text));

    private string Put(string path, byte[] bytes)
    {
        string full = Path.Combine(game, path);
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllBytes(full, bytes);
        return full;
    }

    private static byte[] Zip(params (string Name, string Text)[] entries)
    {
        using MemoryStream memory = new();
        using (ZipArchive archive = new(memory, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach ((string name, string text) in entries)
            {
                using Stream stream = archive.CreateEntry(name).Open();
                stream.Write(Encoding.UTF8.GetBytes(text));
            }
        }
        return memory.ToArray();
    }

    // Every file and folder below folder: a file by its SHA-256, a folder
    // as "folder".
    private static SortedDictionary<string, string> Tree(string folder)
    {
        SortedDictionary<string, string> tree = new(StringComparer.OrdinalIgnoreCase);
        if (!Directory.Exists(folder))
        {
            return tree;
        }
        foreach (string path in Directory.EnumerateFileSystemEntries(folder, "*", SearchOption.AllDirectories))
        {
            tree[Path.GetRelativePath(folder, path)] = Directory.Exists(path)
                ? "folder"
                : Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));
        }
        return tree;
    }

    private static void OneSentence(string text)
    {
        Assert.EndsWith(".", text);
        Assert.DoesNotContain(". ", text.TrimEnd('.'));
    }

    private ModInstallPlan Plan(string zipName, params (string Name, string Text)[] entries) =>
        ModZip.Plan(game, Zip(entries), zipName);

    // ---------------------------------------------------------------
    // S1 scan
    // ---------------------------------------------------------------

    [Fact]
    public void ScanFindsEachModShape()
    {
        Put(@"mods\VillageQoL.dll", "MZ");
        Put(@"mods\VillageQoL.ini", "[QoL]");
        Put(@"mods\readme.txt", "notes");
        Put(@"mods\CanopyData\disabled.txt", "filemod\r\n");
        Put(@"mods\LuaMod\canopy.ini", "\uFEFF[mod]\r\nkey = HelloLua\r\nName = Hello\r\n; comment\r\nversion = 1.2.0\r\n[setting.Greeting]\r\ntype = toggle\r\n");
        Put(@"mods\LuaMod\lua\main.lua", "-- lua");
        Put(@"mods\ItemMod\canopy.ini", "[mod]\nname = Items\n[item.Pebble]\nbase = 230070\n");
        Put(@"mods\FileMod\data\Database\String.dat", "x");
        Put(@"mods\FileMod\data\texture\a.tex", "y");
        File.SetAttributes(Put(@"mods\FileMod\data\Thumbs.db", "z"), FileAttributes.Hidden);
        Put("mods\\FileMod\\data\\caf\u00e9.dat", "w");
        Put(@"mods\SettingsMod\canopy.ini", "[mod]\n[setting.A]\n");
        Put(@"mods\DllFolder\Thing.dll", "MZ");
        Directory.CreateDirectory(Path.Combine(game, "mods", "Empty"));

        IReadOnlyList<ModEntry> mods = ModInventory.Scan(game);

        Assert.Equal(["DllFolder", "Empty", "FileMod", "ItemMod", "LuaMod", "SettingsMod", "VillageQoL.dll"], mods.Select(mod => mod.Name));
        ModEntry Find(string name) => mods.Single(mod => mod.Name == name);

        ModEntry qol = Find("VillageQoL.dll");
        Assert.True(qol.IsDllFile);
        Assert.Equal(ModKind.Dll, qol.Kind);
        // Canopy loads only DLL files directly in mods.
        Assert.Equal(ModKind.FolderDll, Find("DllFolder").Kind);
        Assert.False(Find("DllFolder").IsDllFile);
        Assert.Equal(ModKind.None, Find("Empty").Kind);
        Assert.Equal(ModKind.SettingsOnly, Find("SettingsMod").Kind);
        Assert.Equal(ModKind.Content, Find("ItemMod").Kind);
        Assert.Equal("Items", Find("ItemMod").Title);
        Assert.Null(Find("ItemMod").Key);

        ModEntry lua = Find("LuaMod");
        Assert.Equal(ModKind.Lua, lua.Kind);
        Assert.Equal(("HelloLua", "Hello", "1.2.0"), (lua.Key, lua.Title, lua.Version));
        Assert.False(lua.Disabled);

        // As Canopy's index: lowercase keys, hidden files and names outside
        // ASCII left out.
        ModEntry files = Find("FileMod");
        Assert.Equal(ModKind.Files, files.Kind);
        Assert.True(files.Disabled);
        Assert.Equal(
            [new ModOverride("data/database/string.dat", @"data\Database\String.dat"), new ModOverride("data/texture/a.tex", @"data\texture\a.tex")],
            files.Overrides);
    }

    [Fact]
    public void ScanReadsADllsVersionResourceAndWorksWithoutAModsFolder()
    {
        Assert.Empty(ModInventory.Scan(game));

        // Canopy Studio's own DLL carries the Product of Directory.Build.props.
        File.Copy(typeof(ModInventory).Assembly.Location, Put(@"mods\Studio.dll", "MZ"), overwrite: true);
        Put(@"mods\Both\Both.dll", "MZ");
        Put(@"mods\Both\lua\main.lua", "-- lua");
        Put(@"mods\Both\data\x.dat", "x");
        Put(@"mods\Both\canopy.ini", "[mod]\n[flag.Seen]\n");

        IReadOnlyList<ModEntry> mods = ModInventory.Scan(game);

        ModEntry studio = mods.Single(mod => mod.Name == "Studio.dll");
        Assert.Equal("Canopy Studio", studio.ProductName);
        Assert.False(string.IsNullOrWhiteSpace(studio.ProductVersion));
        Assert.Equal(ModKind.FolderDll | ModKind.Lua | ModKind.Content | ModKind.Files, mods.Single(mod => mod.Name == "Both").Kind);
    }

    [Fact]
    public void ScanUsesTheIniReaderItIsGiven()
    {
        Put(@"mods\Swapped\canopy.ini", "anything");

        ModEntry mod = Assert.Single(ModInventory.Scan(game, new FixedIni()));

        Assert.Equal(("K", "N", "9"), (mod.Key, mod.Title, mod.Version));
        Assert.Equal(ModKind.Content, mod.Kind);
    }

    private sealed class FixedIni : IModIniReader
    {
        public ModIniInfo? Read(string path) => new("K", "N", "9", ["mod", "villager.X"]);
    }

    // ---------------------------------------------------------------
    // S2 conflicts
    // ---------------------------------------------------------------

    [Fact]
    public void ConflictsNameTheWinnerTheWayTheLoaderPicksIt()
    {
        // Canopy folds to lowercase, so '_' sorts before letters: Mod_B
        // loads before ModA, and ModA wins. Folding to uppercase would
        // pick Mod_B.
        Put(@"mods\Mod_B\data\database\string.dat", "b");
        Put(@"mods\ModA\data\Database\STRING.dat", "a");
        Put(@"mods\aaa\data\database\string.dat", "c");
        Put(@"mods\aaa\data\only.dat", "c");
        Put(@"mods\Off\data\database\string.dat", "d");
        Put(@"mods\Off\data\only.dat", "d");
        Put(@"mods\CanopyData\disabled.txt", "# off\nOFF\n");

        IReadOnlyList<OverrideConflict> conflicts = ModInventory.Conflicts(ModInventory.Scan(game));

        OverrideConflict conflict = Assert.Single(conflicts);
        Assert.Equal("data/database/string.dat", conflict.Key);
        Assert.Equal(["aaa", "Mod_B", "ModA"], conflict.Mods);
        Assert.Equal("ModA", conflict.Winner);
    }

    // ---------------------------------------------------------------
    // S3 disabled.txt
    // ---------------------------------------------------------------

    [Fact]
    public void TurningOffAndOnKeepsCommentsAndOtherEntries()
    {
        const string start = "# Turned off by hand\r\nOldMod\r\n\r\n# keep this\r\nVillageQoL.dll\r\n";
        string path = Put(@"mods\CanopyData\disabled.txt", start);

        ModResult off = DisabledMods.SetEnabled(game, "FileMod", enabled: false);
        Assert.True(off.Ok, off.Message);
        Assert.Equal(start + "FileMod\r\n", File.ReadAllText(path));
        Assert.Equal(["OldMod", "VillageQoL.dll", "FileMod"], DisabledMods.Read(game));

        Assert.True(DisabledMods.SetEnabled(game, "FILEMOD", enabled: false).Ok);
        Assert.Equal(start + "FileMod\r\n", File.ReadAllText(path));

        ModResult on = DisabledMods.SetEnabled(game, "oldmod", enabled: true);
        Assert.True(on.Ok, on.Message);
        Assert.Equal("# Turned off by hand\r\n\r\n# keep this\r\nVillageQoL.dll\r\nFileMod\r\n", File.ReadAllText(path));
        Assert.True(DisabledMods.SetEnabled(game, "FileMod", enabled: true).Ok);
        Assert.True(DisabledMods.SetEnabled(game, "villageqol.dll", enabled: true).Ok);
        Assert.Equal("# Turned off by hand\r\n\r\n# keep this\r\n", File.ReadAllText(path));
        Assert.Empty(DisabledMods.Read(game));

        // No temporary file stays behind.
        Assert.Equal(["disabled.txt"], Directory.GetFiles(Path.GetDirectoryName(path)!).Select(file => Path.GetFileName(file)));
    }

    [Fact]
    public void TheListIsMadeWhenMissingAndKeepsItsLineEndingsAndBom()
    {
        Assert.True(DisabledMods.SetEnabled(game, "First", enabled: false).Ok);
        Assert.Equal("First\r\n", File.ReadAllText(DisabledMods.PathIn(game)));

        File.WriteAllBytes(DisabledMods.PathIn(game), [0xEF, 0xBB, 0xBF, .. "# unix\nOne"u8]);
        Assert.True(DisabledMods.SetEnabled(game, "Two", enabled: false).Ok);
        byte[] expected = [0xEF, 0xBB, 0xBF, .. "# unix\nOne\nTwo\n"u8];
        Assert.Equal(expected, File.ReadAllBytes(DisabledMods.PathIn(game)));
        Assert.Equal(["One", "Two"], DisabledMods.Read(game));
    }

    [Theory]
    [InlineData("CanopyData")]
    [InlineData("#Hidden")]
    [InlineData(" Spaced")]
    [InlineData("a\\b")]
    [InlineData("")]
    public void NamesTheListCannotHoldAreRefused(string name)
    {
        ModResult result = DisabledMods.SetEnabled(game, name, enabled: false);

        Assert.False(result.Ok);
        OneSentence(result.Message);
        Assert.False(File.Exists(DisabledMods.PathIn(game)));
    }

    // ---------------------------------------------------------------
    // S4 install plans
    // ---------------------------------------------------------------

    [Fact]
    public void AZipHoldingModsGoesWhereItSays()
    {
        Put(@"mods\Foo\canopy.ini", "old");
        Put(@"mods\Foo\old.lua", "old");

        ModInstallPlan plan = Plan("Download.zip",
            ("mods/Foo/canopy.ini", "[mod]"), (@"mods\Foo\lua\main.lua", "-- lua"), ("README.md", "readme"));

        Assert.True(plan.Ok, string.Join(" ", plan.Problems));
        Assert.Equal(("Foo", false), (plan.Mod, plan.IsDllFile));
        Assert.Equal([@"mods\Foo\canopy.ini", @"mods\Foo\lua\main.lua"], plan.Writes.Select(file => file.Path));
        Assert.Equal([@"mods\Foo\canopy.ini"], plan.Replaces);
        Assert.Equal([@"mods\Foo\old.lua"], plan.Removes);
        Assert.Equal(["README.md"], plan.Skipped);

        ModInstallPlan dll = Plan("Bar-1.0.zip", ("mods/Bar.dll", "MZ"), ("LICENSE", "licence"));
        Assert.True(dll.Ok);
        Assert.Equal(("Bar.dll", true), (dll.Mod, dll.IsDllFile));
        Assert.Equal([@"mods\Bar.dll"], dll.Writes.Select(file => file.Path));
    }

    [Fact]
    public void AZipWithOneTopFolderInstallsThatFolder()
    {
        ModInstallPlan ini = Plan("Pack.zip", ("Pack/canopy.ini", "[mod]"), ("Pack/data/x.dat", "x"));
        ModInstallPlan data = Plan("x.zip", ("Textures/data/texture/a.tex", "a"));
        ModInstallPlan dll = Plan("x.zip", ("Wrap/Wrap.dll", "MZ"), ("Wrap/readme.txt", "r"));

        Assert.Equal("Pack", ini.Mod);
        Assert.Equal([@"mods\Pack\canopy.ini", @"mods\Pack\data\x.dat"], ini.Writes.Select(file => file.Path));
        Assert.Equal("Textures", data.Mod);
        // Canopy loads only DLL files directly in mods, so the DLL leaves its folder.
        Assert.Equal(("Wrap.dll", true), (dll.Mod, dll.IsDllFile));
        Assert.Equal([@"mods\Wrap.dll"], dll.Writes.Select(file => file.Path));
        Assert.Equal([@"Wrap\readme.txt"], dll.Skipped);
        Assert.False(Plan("x.zip", ("Two/A.dll", "MZ"), ("Two/B.dll", "MZ")).Ok);
    }

    [Fact]
    public void AZipWhoseRootIsTheModIsNamedAfterTheZip()
    {
        ModInstallPlan plan = Plan("Cool Mod.zip", ("canopy.ini", "[mod]"), ("lua/main.lua", "-- lua"));

        Assert.True(plan.Ok);
        Assert.Equal("Cool Mod", plan.Mod);
        Assert.Equal([@"mods\Cool Mod\canopy.ini", @"mods\Cool Mod\lua\main.lua"], plan.Writes.Select(file => file.Path));
        Assert.Empty(plan.Replaces);
    }

    [Fact]
    public void ASingleDllInstallsDirectlyInMods()
    {
        ModInstallPlan zipped = Plan("whatever.zip", ("Thing.dll", "MZ"), ("readme.txt", "r"));
        ModInstallPlan lone = ModZip.Plan(game, "MZ lone"u8.ToArray(), @"C:\Downloads\Lone.dll");

        Assert.Equal(("Thing.dll", true), (zipped.Mod, zipped.IsDllFile));
        Assert.Equal(["readme.txt"], zipped.Skipped);
        Assert.True(lone.Ok);
        Assert.Equal(("Lone.dll", true), (lone.Mod, lone.IsDllFile));
        Assert.Equal("MZ lone"u8.ToArray(), Assert.Single(lone.Writes).Bytes);
        Assert.Equal(@"mods\Lone.dll", lone.Writes[0].Path);
    }

    [Theory]
    [InlineData("/abs/canopy.ini", "absolute path", true)]
    [InlineData("C:/abs/canopy.ini", "absolute path", true)]
    [InlineData("Foo/../../evil.dll", "climbs out", true)]
    [InlineData("Foo/con.txt", "reserves for a device", true)]
    [InlineData("Foo/LPT1", "reserves for a device", true)]
    [InlineData("Foo/a?b.txt", "character Windows does not allow", true)]
    [InlineData("Foo/trailing ", "ends with a dot or a space", true)]
    [InlineData("mods/CanopyData/disabled.txt", "Canopy's own folder", true)]
    [InlineData("CanopyData/canopy.ini", "Canopy's own folder", false)]
    [InlineData("mods/notes.txt", "not a DLL", true)]
    [InlineData("notes.txt", "does not look like a mod", false)]
    public void UnsafeOrUnplaceableZipsAreRefused(string entry, string reason, bool besideAMod)
    {
        ModInstallPlan plan = besideAMod
            ? Plan("Mod.zip", ("Foo/canopy.ini", "[mod]"), (entry, "x"))
            : Plan("Mod.zip", (entry, "x"));

        Assert.False(plan.Ok);
        string problem = Assert.Single(plan.Problems);
        Assert.Contains(reason, problem);
        OneSentence(problem);
        Assert.Empty(plan.Writes);
    }

    [Fact]
    public void OtherRefusalsAreOneSentenceEach()
    {
        Put(@"mods\Taken", "a file");
        List<ModInstallPlan> plans =
        [
            Plan("Two.zip", ("mods/A/canopy.ini", "a"), ("mods/B/canopy.ini", "b")),
            Plan("Dup.zip", ("Foo/canopy.ini", "a"), ("foo/CANOPY.INI", "b")),
            Plan("CanopyData.zip", ("canopy.ini", "a")),
            Plan("Taken.zip", ("canopy.ini", "a")),
            Plan("Many.zip", ("A.dll", "MZ"), ("B.dll", "MZ")),
            Plan("Empty.zip"),
            ModZip.Plan(game, [1, 2, 3], "Broken.zip"),
            ModZip.Plan(game, Zip(("Big/canopy.ini", new string('x', 100))), "Big.zip", new ModZipLimits(MaxFileBytes: 10)),
            ModZip.Plan(game, Zip(("Big/a.txt", "123456"), ("Big/b.txt", "123456")), "Big.zip", new ModZipLimits(MaxTotalBytes: 10)),
            ModZip.Plan(game, Zip(("Big/a.txt", "1"), ("Big/b.txt", "2"), ("Big/c.txt", "3")), "Big.zip", new ModZipLimits(MaxFiles: 2)),
            ModZip.Plan(game, new byte[100], "Huge.dll", new ModZipLimits(MaxFileBytes: 10)),
        ];
        string[] reasons =
        [
            "install them one at a time", "in the zip twice", "Canopy's own folder", "is a file",
            "several DLLs", "holds no files", "not a zip", "larger than", "unpacks to more than", "more than 2 files", "larger than",
        ];

        for (int index = 0; index < plans.Count; ++index)
        {
            Assert.False(plans[index].Ok, reasons[index]);
            Assert.Contains(reasons[index], plans[index].Problems[0]);
            Assert.All(plans[index].Problems, OneSentence);
        }
    }

    // ---------------------------------------------------------------
    // S5 apply, remove, undo
    // ---------------------------------------------------------------

    [Fact]
    public void ApplyThenUndoRestoresTheFolderByteForByte()
    {
        Put(@"mods\Foo\canopy.ini", "old ini");
        Put(@"mods\Foo\old.lua", "old lua");
        Put(@"mods\Foo\data\keep.dat", "same");
        Directory.CreateDirectory(Path.Combine(game, "mods", "Foo", "empty"));
        Put(@"mods\Other\canopy.ini", "other");
        SortedDictionary<string, string> before = Tree(game);

        ModInstallPlan plan = Plan("Foo.zip", ("Foo/canopy.ini", "new ini"), ("Foo/lua/main.lua", "-- lua"), ("Foo/data/keep.dat", "same"));
        ModResult applied = ModChanges.Apply(game, plan, backups, gameRunning: false, Now);

        Assert.True(applied.Ok, applied.Message);
        Assert.Equal("new ini", File.ReadAllText(Path.Combine(game, @"mods\Foo\canopy.ini")));
        Assert.False(File.Exists(Path.Combine(game, @"mods\Foo\old.lua")));
        Assert.False(Directory.Exists(Path.Combine(game, @"mods\Foo\empty")));
        Assert.Equal(ModChangeKind.Install, ModChanges.Last(game, backups)!.Kind);

        ModResult undone = ModChanges.Undo(game, backups, gameRunning: false);

        Assert.True(undone.Ok, undone.Message);
        Assert.Equal(before, Tree(game));
        Assert.Null(ModChanges.Last(game, backups));
        Assert.Equal([ModChanges.JournalName], Tree(backups).Keys);
        Assert.False(ModChanges.Undo(game, backups, gameRunning: false).Ok);
    }

    [Fact]
    public void AFreshInstallIsUndoneDownToTheModsFolder()
    {
        SortedDictionary<string, string> before = Tree(game);

        Assert.True(ModChanges.Apply(game, Plan("New.zip", ("canopy.ini", "[mod]")), backups, false, Now).Ok);
        Assert.True(File.Exists(Path.Combine(game, @"mods\New\canopy.ini")));
        Assert.True(ModChanges.Undo(game, backups, false).Ok);

        Assert.Equal(before, Tree(game));
    }

    [Fact]
    public void RemoveThenUndoBringsTheModBack()
    {
        Put(@"mods\Foo\canopy.ini", "ini");
        Put(@"mods\Foo\data\database\string.dat", "dat");
        Put(@"mods\Solo.dll", "MZ solo");
        SortedDictionary<string, string> before = Tree(game);

        Assert.True(ModChanges.Remove(game, "Foo", backups, false, Now).Ok);
        ModResult removed = ModChanges.Remove(game, "Solo.dll", backups, false, Now.AddMinutes(1));
        Assert.True(removed.Ok, removed.Message);
        Assert.Equal(["mods", @"village.exe"], Tree(game).Keys);
        Assert.False(ModChanges.Remove(game, "Foo", backups, false, Now).Ok);

        // Undo takes back the last change first.
        Assert.True(ModChanges.Undo(game, backups, false).Ok);
        Assert.True(File.Exists(Path.Combine(game, @"mods\Solo.dll")));
        Assert.False(Directory.Exists(Path.Combine(game, @"mods\Foo")));
        Assert.True(ModChanges.Undo(game, backups, false).Ok);
        Assert.Equal(before, Tree(game));
    }

    [Fact]
    public void UndoIsRefusedAfterTheFolderChanged()
    {
        Put(@"mods\Foo\canopy.ini", "old");
        Assert.True(ModChanges.Apply(game, Plan("Foo.zip", ("Foo/canopy.ini", "new")), backups, false, Now).Ok);
        Put(@"mods\Foo\notes.txt", "the player's own file");
        SortedDictionary<string, string> changed = Tree(game);

        ModResult undo = ModChanges.Undo(game, backups, false);

        Assert.False(undo.Ok);
        Assert.Contains("changed after the install of Foo", undo.Message);
        Assert.Equal(changed, Tree(game));
        Assert.NotNull(ModChanges.Last(game, backups));

        // An edited file counts too.
        File.Delete(Path.Combine(game, @"mods\Foo\notes.txt"));
        Put(@"mods\Foo\canopy.ini", "edited");
        Assert.False(ModChanges.Undo(game, backups, false).Ok);
        Assert.Equal("edited", File.ReadAllText(Path.Combine(game, @"mods\Foo\canopy.ini")));
    }

    [Fact]
    public void APlanIsRefusedWhenTheFolderChangedSince()
    {
        Put(@"mods\Foo\canopy.ini", "old");
        ModInstallPlan plan = Plan("Foo.zip", ("Foo/canopy.ini", "new"));
        Put(@"mods\Foo\extra.txt", "added after the plan");

        ModResult result = ModChanges.Apply(game, plan, backups, false, Now);

        Assert.False(result.Ok);
        Assert.Contains("Plan the install again", result.Message);
        Assert.Equal("old", File.ReadAllText(Path.Combine(game, @"mods\Foo\canopy.ini")));
        Assert.False(Directory.Exists(backups));
    }

    [Fact]
    public void AFailureHalfwayPutsTheModBack()
    {
        Put(@"mods\Foo\a.txt", "old a");
        Put(@"mods\Foo\b.txt", "old b");
        string last = Put(@"mods\Foo\z.txt", "old z");
        SortedDictionary<string, string> before = Tree(game);
        ModInstallPlan plan = Plan("Foo.zip",
            ("Foo/canopy.ini", "[mod]"), ("Foo/a.txt", "new a"), ("Foo/b.txt", "new b"), ("Foo/z.txt", "new z"));
        Assert.True(plan.Ok);

        ModResult applied;
        ModResult removed;
        // Readable but locked against writes and deletes: a.txt and b.txt
        // change first, then z.txt fails.
        using (new FileStream(last, FileMode.Open, FileAccess.Read, FileShare.Read))
        {
            applied = ModChanges.Apply(game, plan, backups, false, Now);
            removed = ModChanges.Remove(game, "Foo", backups, false, Now);
        }

        Assert.False(applied.Ok);
        Assert.Contains("is as it was", applied.Message);
        Assert.False(removed.Ok);
        Assert.Contains("is as it was", removed.Message);
        Assert.Equal(before, Tree(game));
        Assert.Null(ModChanges.Last(game, backups));
        Assert.DoesNotContain(Tree(backups).Keys, path => path.EndsWith(".txt", StringComparison.Ordinal));
    }

    [Fact]
    public void NothingIsWrittenWhileTheGameRuns()
    {
        Put(@"mods\Foo\canopy.ini", "old");
        ModInstallPlan plan = Plan("Foo.zip", ("Foo/canopy.ini", "new"));
        SortedDictionary<string, string> before = Tree(game);

        Assert.False(ModChanges.Apply(game, plan, backups, gameRunning: true, Now).Ok);
        Assert.False(ModChanges.Remove(game, "Foo", backups, gameRunning: true, Now).Ok);
        Assert.Equal(before, Tree(game));
        Assert.False(Directory.Exists(backups));

        Assert.True(ModChanges.Apply(game, plan, backups, gameRunning: false, Now).Ok);
        SortedDictionary<string, string> installed = Tree(game);
        ModResult undo = ModChanges.Undo(game, backups, gameRunning: true);
        Assert.False(undo.Ok);
        Assert.StartsWith("Close the game first", undo.Message);
        Assert.Equal(installed, Tree(game));
    }

    [Fact]
    public void ChangesNeedTheGameAndABackupFolderOutsideIt()
    {
        Put(@"mods\Foo\canopy.ini", "old");

        Assert.Contains("inside the game folder", ModChanges.Remove(game, "Foo", Path.Combine(game, "backups"), false, Now).Message);
        Assert.Contains("is not a mod", ModChanges.Remove(game, "CanopyData", backups, false, Now).Message);
        Assert.Contains("is not a mod", ModChanges.Remove(game, @"..\..", backups, false, Now).Message);
        File.Delete(Path.Combine(game, "village.exe"));
        Assert.Contains("village.exe is not in this folder", ModChanges.Remove(game, "Foo", backups, false, Now).Message);
        Assert.True(File.Exists(Path.Combine(game, @"mods\Foo\canopy.ini")));
        Assert.False(Directory.Exists(backups));
    }

    [Fact]
    public void ADamagedJournalStopsChanges()
    {
        Put(@"mods\Foo\canopy.ini", "old");
        Directory.CreateDirectory(backups);
        File.WriteAllText(Path.Combine(backups, ModChanges.JournalName), "{ not json");

        Assert.Contains("cannot be read", ModChanges.Remove(game, "Foo", backups, false, Now).Message);
        Assert.Contains("cannot be read", ModChanges.Undo(game, backups, false).Message);
        Assert.True(File.Exists(Path.Combine(game, @"mods\Foo\canopy.ini")));
    }

    [Fact]
    public void ALinkedModIsLeftAlone()
    {
        string project = Path.Combine(root, "project");
        Directory.CreateDirectory(project);
        File.WriteAllText(Path.Combine(project, "canopy.ini"), "[mod]");
        Directory.CreateDirectory(Path.Combine(game, "mods"));
        string link = Path.Combine(game, "mods", "Linked");
        // A junction, as a playtest makes; it needs no rights.
        using (Process? mklink = Process.Start(new ProcessStartInfo("cmd.exe", ["/c", "mklink", "/J", link, project])
        {
            CreateNoWindow = true,
            UseShellExecute = false,
            RedirectStandardOutput = true,
        }))
        {
            mklink?.WaitForExit();
        }
        if (!File.Exists(Path.Combine(link, "canopy.ini")))
        {
            return;
        }
        Assert.Equal("Linked", Assert.Single(ModInventory.Scan(game)).Name);

        ModResult result = ModChanges.Remove(game, "Linked", backups, false, Now);

        Assert.False(result.Ok);
        Assert.Contains("link", result.Message);
        Assert.True(File.Exists(Path.Combine(project, "canopy.ini")));
        Assert.False(Plan("Linked.zip", ("canopy.ini", "x")).Ok);
    }
}
