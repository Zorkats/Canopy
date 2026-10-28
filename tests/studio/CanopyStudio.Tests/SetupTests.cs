using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;

namespace CanopyStudio.Tests;

public sealed class GameLocatorTests
{
    [Fact]
    public void VdfValuesReadsEveryLibraryPathUnescaped()
    {
        const string vdf = """
            "libraryfolders"
            {
            	"0"
            	{
            		"path"		"C:\\Program Files (x86)\\Steam"
            		"label"		""
            		"apps"
            		{
            			"228980"		"123"
            		}
            	}
            	"1"
            	{
            		"path"		"D:\\SteamLibrary"
            	}
            }
            """;

        Assert.Equal([@"C:\Program Files (x86)\Steam", @"D:\SteamLibrary"], GameLocator.VdfValues(vdf, "path"));
    }

    [Fact]
    public void VdfValuesMatchesKeysWithoutCase()
    {
        const string acf = """
            "AppState"
            {
            	"appid"		"3934250"
            	"InstallDir"		"Village in the Shade"
            }
            """;

        Assert.Equal(["Village in the Shade"], GameLocator.VdfValues(acf, "installdir"));
    }

    [Fact]
    public void VdfValuesKeepsEscapedQuotes()
    {
        Assert.Equal(["say \"hi\""], GameLocator.VdfValues("\"path\" \"say \\\"hi\\\"\"", "path"));
    }

    [Fact]
    public void VdfValuesIgnoresSectionNames()
    {
        Assert.Empty(GameLocator.VdfValues("\"path\"\n{\n}", "path"));
    }

    [Theory]
    [InlineData("  \"D:\\Games\\Village in the Shade\"  ", null, @"D:\Games\Village in the Shade")]
    [InlineData("'/games/Village in the Shade'", "/home/deck", "/games/Village in the Shade")]
    [InlineData("~/Games/Village in the Shade", "/home/deck", "/home/deck/Games/Village in the Shade")]
    [InlineData("~", "/home/deck/", "/home/deck")]
    [InlineData("~/Games", null, "~/Games")]
    [InlineData("~deck/Games", "/home/deck", "~deck/Games")]
    [InlineData("  \"\"  ", "/home/deck", "")]
    public void TypedFolderTrimsQuotesAndReadsHome(string text, string? home, string expected)
    {
        Assert.Equal(expected, GameLocator.TypedFolder(text, home));
    }
}

public sealed class SettingsStoreTests : IDisposable
{
    private readonly string folder = Path.Combine(Path.GetTempPath(), "CanopyStudioTests", Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(folder))
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    [Fact]
    public void MissingFileReadsAsDefaults()
    {
        Assert.Equal(new StudioSettings(), new SettingsStore(folder).Load());
    }

    [Fact]
    public void SavedSettingsReadBack()
    {
        SettingsStore store = new(folder);
        StudioSettings settings = new()
        {
            GameFolder = @"D:\Games\Village in the Shade",
            SetupDone = true,
            LastPage = "tables",
            WindowWidth = 1500,
            WindowHeight = 900,
            WindowMaximized = true,
        };

        Assert.True(store.Save(settings));
        Assert.Equal(settings, new SettingsStore(folder).Load());
        Assert.False(File.Exists(store.FilePath + ".tmp"));
    }

    [Fact]
    public void DamagedFileReadsAsDefaults()
    {
        SettingsStore store = new(folder);
        Directory.CreateDirectory(folder);
        File.WriteAllText(store.FilePath, "{ \"gameFolder\": ");

        Assert.Equal(new StudioSettings(), store.Load());
    }
}

public sealed class SetupChecksTests
{
    private static GameInfo Healthy() => new()
    {
        Ok = true,
        Exe = true,
        Build = 0x6ABD400D,
        Supported = true,
        Mods = true,
        Canopy = true,
        Log = true,
        Archives = [new ArchiveInfo("data.dat", 1, 10)],
        Files = 10,
        Tables = 2,
    };

    [Fact]
    public void MissingFolderIsOneFailure()
    {
        SetupCheck check = Assert.Single(SetupChecks.For(new GameInfo(), folderExists: false));
        Assert.Equal(CheckState.Fail, check.State);
    }

    [Fact]
    public void HealthyFolderPassesEverything()
    {
        IReadOnlyList<SetupCheck> checks = SetupChecks.For(Healthy(), folderExists: true);

        Assert.Equal(6, checks.Count);
        Assert.All(checks, check => Assert.Equal(CheckState.Pass, check.State));
    }

    [Fact]
    public void OtherBuildWarnsWithItsStamp()
    {
        SetupCheck version = SetupChecks.For(Healthy() with { Build = 0x12345678, Supported = false }, true)
            .Single(check => check.Title == "Game version");

        Assert.Equal(CheckState.Warn, version.State);
        Assert.Contains("12345678", version.Detail);
    }

    [Fact]
    public void ArchivesThatDoNotOpenStopTheChecks()
    {
        IReadOnlyList<SetupCheck> checks = SetupChecks.For(
            Healthy() with { Ok = false, Error = "no archives here", Archives = [] }, true);

        SetupCheck last = checks[^1];
        Assert.Equal("Game archives", last.Title);
        Assert.Equal(CheckState.Fail, last.State);
        Assert.Equal("no archives here", last.Detail);
    }

    [Fact]
    public void NoCanopyIsOnlyAWarning()
    {
        IReadOnlyList<SetupCheck> checks = SetupChecks.For(Healthy() with { Canopy = false, Mods = false, Log = false }, true);

        Assert.DoesNotContain(checks, check => check.State == CheckState.Fail);
        Assert.Equal(CheckState.Warn, checks.Single(check => check.Title == "Canopy").State);
    }
}
