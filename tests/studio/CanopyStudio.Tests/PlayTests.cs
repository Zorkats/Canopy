using System.Diagnostics;
using System.Globalization;
using System.Text;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Play;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The Play area's Core pieces on throwaway folders with made-up mods.
// Nothing here touches a real game folder or %APPDATA%.
public sealed class PlayTests : IDisposable
{
    private readonly ITestOutputHelper output;
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-play-" + Guid.NewGuid().ToString("N"));
    private readonly string game;

    public PlayTests(ITestOutputHelper output)
    {
        this.output = output;
        game = Path.Combine(root, "game");
        Directory.CreateDirectory(game);
    }

    public void Dispose() => Directory.Delete(root, recursive: true);

    private string Put(string path, string text)
    {
        string full = Path.Combine(game, path);
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllText(full, text, new UTF8Encoding(false));
        return full;
    }

    private static ModEntry Mod(string name, ModKind kind, bool dll = false, string? title = null, string? version = null,
        string? product = null, string? productVersion = null) =>
        new(name, dll, kind, null, title, version, product, productVersion, false, []);

    private static IReadOnlyList<string> Texts(IReadOnlyList<ModBadge> badges) => badges.Select(badge => badge.Text).ToList();

    [Fact]
    public void EachKindHasItsBadge()
    {
        Assert.Equal(["DLL"], Texts(ModBadges.For(Mod("Alpha.dll", ModKind.Dll, dll: true), false, false)));
        Assert.Equal(["Lua"], Texts(ModBadges.For(Mod("Beta", ModKind.Lua), false, false)));
        Assert.Equal(["Content"], Texts(ModBadges.For(Mod("Gamma", ModKind.Content), false, false)));
        Assert.Equal(["Files"], Texts(ModBadges.For(Mod("Delta", ModKind.Files), false, false)));
        Assert.Equal(["Settings only"], Texts(ModBadges.For(Mod("Eps", ModKind.SettingsOnly), false, false)));
        Assert.Equal(["Lua", "Content", "Files"],
            Texts(ModBadges.For(Mod("Zeta", ModKind.Files | ModKind.Content | ModKind.Lua), false, false)));
        Assert.All(ModBadges.For(Mod("Zeta", ModKind.Files | ModKind.Content | ModKind.Lua), false, false),
            badge => Assert.False(badge.Warning));
    }

    [Fact]
    public void RisksHaveWarningBadges()
    {
        ModBadge folderDll = Assert.Single(ModBadges.For(Mod("Eta", ModKind.FolderDll), false, false));
        IReadOnlyList<ModBadge> nothing = ModBadges.For(Mod("Theta", ModKind.None), false, false);
        IReadOnlyList<ModBadge> all = ModBadges.For(Mod("Iota", ModKind.Lua | ModKind.FolderDll), true, true);

        Assert.True(folderDll.Warning);
        Assert.Equal("This DLL is inside a folder, so Canopy never loads it.", folderDll.Help);
        Assert.True(Assert.Single(nothing).Warning);
        Assert.Equal(["Lua", "DLL never loads", "Unsafe Lua", "Conflict"], Texts(all));
        Assert.Equal([false, true, true, true], all.Select(badge => badge.Warning));
        Assert.All(all, badge => Assert.EndsWith(".", badge.Help));
    }

    private static IniStructure Ini(params (string Kind, string Section, string Key, string Value)[] lines) => new()
    {
        Ok = true,
        Lines = lines.Select((line, index) => new IniLine(index + 1, line.Kind, line.Section, line.Key, line.Value)).ToList(),
    };

    [Theory]
    [InlineData("true", true)]
    [InlineData("TRUE", true)]
    [InlineData("1", true)]
    [InlineData("yes", true)]
    [InlineData("On", true)]
    [InlineData("false", false)]
    [InlineData("0", false)]
    [InlineData("", false)]
    [InlineData("maybe", false)]
    public void UnsafeReadsAsTheLoaderDoes(string value, bool expected)
    {
        IniStructure ini = Ini(("section", "Mod", "", ""), ("value", "Mod", "Unsafe", value));

        Assert.Equal(expected, ModBadges.UnsafeLua(ini));
    }

    [Fact]
    public void UnsafeCountsOnlyTheFirstKeyOfTheFirstModSection()
    {
        IniStructure firstWins = Ini(("section", "mod", "", ""), ("value", "mod", "unsafe", "false"),
            ("value", "mod", "unsafe", "true"));
        IniStructure secondMod = Ini(("section", "mod", "", ""), ("value", "mod", "name", "Kappa"),
            ("section", "item.Lambda", "", ""), ("value", "item.Lambda", "unsafe", "true"),
            ("section", "mod", "", ""), ("value", "mod", "unsafe", "true"));
        IniStructure none = Ini(("comment", "", "", ""), ("value", "", "unsafe", "true"));

        Assert.False(ModBadges.UnsafeLua(firstWins));
        Assert.False(ModBadges.UnsafeLua(secondMod));
        Assert.False(ModBadges.UnsafeLua(none));
    }

    [Fact]
    public void UnsafeIsReadFromTheModsCanopyIni()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return;
        }
        Put(@"mods\Mu\canopy.ini", "[mod]\r\nname = Mu\r\nunsafe = yes\r\n");
        Put(@"mods\Nu\canopy.ini", "; nothing risky\n[mod]\nname = Nu\n");

        Assert.True(ModBadges.ReadUnsafeLua(Path.Combine(game, "mods", "Mu")));
        Assert.False(ModBadges.ReadUnsafeLua(Path.Combine(game, "mods", "Nu")));
        Assert.False(ModBadges.ReadUnsafeLua(Path.Combine(game, "mods", "Missing")));
    }

    [Fact]
    public void ModsInAConflictAreFound()
    {
        Put(@"mods\Omicron\data\database\a.dat", "1");
        Put(@"mods\Pi\data\database\A.dat", "2");
        Put(@"mods\Rho\data\database\b.dat", "3");
        IReadOnlyList<ModEntry> mods = ModInventory.Scan(game);
        IReadOnlyList<OverrideConflict> conflicts = ModInventory.Conflicts(mods);

        Assert.True(ModBadges.InConflict(conflicts, "Omicron"));
        Assert.True(ModBadges.InConflict(conflicts, "pi"));
        Assert.False(ModBadges.InConflict(conflicts, "Rho"));
        Assert.Contains("Conflict", Texts(ModBadges.For(mods[0], false, ModBadges.InConflict(conflicts, mods[0].Name))));
    }

    [Fact]
    public void LastRunStartsAtTheLastStart()
    {
        const string log = "[t1] Canopy starting, API version 3.\r\n[t1] Loading 2 mods.\r\n"
            + "[t2] Canopy starting, API version 3.\r\n[t2] Loading 1 mod.\r\n[t2] Done.\r\n";

        Assert.Equal(["[t2] Canopy starting, API version 3.", "[t2] Loading 1 mod.", "[t2] Done."], CanopyLog.LastRun(log));
        Assert.Equal(["[t1] Canopy starting.", "[t1] One."], CanopyLog.LastRun("[t1] Canopy starting.\n[t1] One."));
    }

    [Fact]
    public void LastRunOfALogWithNoStartIsTheWholeLog()
    {
        Assert.Equal(["first", "second", "", "third"], CanopyLog.LastRun("first\r\nsecond\r\n\r\nthird\r\n"));
        Assert.Empty(CanopyLog.LastRun(""));
    }

    [Fact]
    public void ProblemLinesAreFoundInAnyCase()
    {
        string[] problems =
        [
            "[t] Warning: a value was ignored.",
            "[t] Mod Sigma was SKIPPED.",
            "[t] The hook failed.",
            "[t] The file Could Not be read.",
            "[t] Refused a path.",
            "[t] 2 errors in canopy.ini.",
        ];
        string[] fine = ["[t] Canopy starting, API version 3.", "[t] Loading 2 mods.", "[t] Tau loaded."];

        Assert.All(problems, line => Assert.True(CanopyLog.IsProblem(line), line));
        Assert.All(fine, line => Assert.False(CanopyLog.IsProblem(line), line));
        Assert.Equal(problems, CanopyLog.Problems([.. fine, .. problems]));
        Assert.Equal(6, CanopyLog.ProblemWords.Count);
    }

    [Fact]
    public void LogIsReadWhileTheGameHoldsIt()
    {
        Assert.False(CanopyLog.Read(game).Exists);

        string path = PlayPaths.Log(game);
        // As Canopy opens it: appending, shared for reading, writing and delete.
        using (FileStream held = new(path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete))
        {
            held.Write(Encoding.UTF8.GetBytes("[t1] Canopy starting.\r\n[t1] Old.\r\n[t2] Canopy starting.\r\n[t2] New.\r\n"));
            held.Flush();

            CanopyLogRun run = CanopyLog.Read(game);

            Assert.True(run.Exists);
            Assert.Null(run.Error);
            Assert.Equal(["[t2] Canopy starting.", "[t2] New."], run.Lines);
        }
    }

    [Fact]
    public void BackupsLiveOutsideTheGameFolder()
    {
        string appData = Path.Combine(root, "appdata");

        Assert.Equal(Path.Combine(appData, "CanopyStudio", "mod-backups"), PlayPaths.BackupsIn(appData));
        Assert.EndsWith(Path.Combine("CanopyStudio", "mod-backups"), PlayPaths.Backups());
        Assert.False(PlayPaths.Backups().StartsWith(game, StringComparison.OrdinalIgnoreCase));
        Assert.Equal(Path.Combine(game, "mods", "Upsilon"), PlayPaths.Mod(game, "Upsilon"));
        Assert.Equal(Path.Combine(game, "Canopy.log"), PlayPaths.Log(game));
        // Nothing is made just by asking.
        Assert.False(Directory.Exists(appData));
    }

    [Fact]
    public void GameProcessIsFoundByName()
    {
        using Process self = Process.GetCurrentProcess();

        Assert.False(GameProcess.IsRunning("canopy-no-such-game-" + Guid.NewGuid().ToString("N") + ".exe"));
        Assert.True(GameProcess.IsRunning(self.ProcessName + ".exe"));
        Assert.True(GameProcess.IsRunning(self.ProcessName));
    }

    [Fact]
    public void ProblemsAreSortedByMod()
    {
        IReadOnlyList<ModEntry> mods = [Mod("Phi", ModKind.Content), Mod("Chi.dll", ModKind.Dll, dll: true)];
        IReadOnlyList<ModDiagnostic> diagnostics =
        [
            new("error", "phi", "canopy.ini", 4, "m1"),
            new("warning", "", "", 0, "m2"),
            new("note", "Phi", "canopy.ini", 0, "m3"),
            new("warning", "Psi", "canopy.ini", 2, "m4"),
        ];

        Assert.Equal(["m1", "m3"], PlayMods.Of(diagnostics, "Phi").Select(diagnostic => diagnostic.Message));
        Assert.Empty(PlayMods.Of(diagnostics, "Chi.dll"));
        Assert.Equal(["m2", "m4"], PlayMods.General(diagnostics, mods).Select(diagnostic => diagnostic.Message));
        Assert.Equal([true, false, false, true], diagnostics.Select(PlayMods.CanShow));
    }

    [Fact]
    public void ModsAreNamedAndChangesDescribed()
    {
        ModChange install = new("1", game, ModChangeKind.Install, "Omega", new DateTime(2026, 10, 15, 9, 5, 0), [], [], []);

        Assert.Equal("Omega Title", PlayMods.Title(Mod("Omega", ModKind.Lua, title: "Omega Title", product: "Other")));
        Assert.Equal("Product", PlayMods.Title(Mod("Omega.dll", ModKind.Dll, dll: true, product: "Product")));
        Assert.Equal("Omega", PlayMods.Title(Mod("Omega", ModKind.Lua)));
        Assert.Equal("1.2", PlayMods.Version(Mod("Omega", ModKind.Lua, version: "1.2", productVersion: "9")));
        Assert.Equal("9", PlayMods.Version(Mod("Omega", ModKind.FolderDll, productVersion: "9")));
        Assert.Null(PlayMods.Version(Mod("Omega", ModKind.Lua)));
        Assert.Equal("the install of Omega, made Oct 15 at 09:05", PlayMods.Describe(install, CultureInfo.InvariantCulture));
        Assert.StartsWith("the removal of Omega", PlayMods.Describe(install with { Kind = ModChangeKind.Remove }, CultureInfo.InvariantCulture));
    }
}
