using System.Diagnostics;
using System.IO.Compression;
using System.Text;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Platform;
using CanopyStudio.Core.Setup;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// What Canopy Studio does differently on Linux, run on any platform: the
// Linux paths take the platform as an argument, so Windows runs them too.
// Every folder is a throwaway one; nothing here reads Steam, the game or a
// settings folder of this PC.
public sealed class PlatformTests(ITestOutputHelper output) : IDisposable
{
    private readonly string root = Directory.CreateDirectory(
        Path.Combine(Path.GetTempPath(), "canopy-platform-" + Guid.NewGuid().ToString("N"))).FullName;

    public void Dispose()
    {
        // Links go first, on their own, so nothing follows them.
        foreach (string link in Directory.EnumerateFileSystemEntries(root, "*", SearchOption.AllDirectories)
            .Where(path => new FileInfo(path).LinkTarget is not null).ToList())
        {
            Directory.Delete(link);
        }
        Directory.Delete(root, recursive: true);
    }

    private string Folder(string relative) => Directory.CreateDirectory(Path.Combine(root, relative)).FullName;

    private string Write(string relative, string text = "")
    {
        string path = Path.Combine(root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
        return path;
    }

    // A link at path to target: a symbolic link where it may be made, else
    // on Windows a junction, which needs no rights. False with a note when
    // neither can be made.
    private bool Link(string path, string target)
    {
        try
        {
            Directory.CreateSymbolicLink(path, target);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            if (OperatingSystem.IsWindows())
            {
                using (Process? mklink = Process.Start(new ProcessStartInfo("cmd.exe", ["/c", "mklink", "/J", path, target])
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                }))
                {
                    mklink?.WaitForExit();
                }
                if (new DirectoryInfo(path).LinkTarget is not null)
                {
                    output.WriteLine("Note: symbolic links need rights here, so a junction stands in.");
                    return true;
                }
            }
            output.WriteLine($"Note: links cannot be made here ({exception.Message}), so the link case was not run.");
            return false;
        }
    }

    // ---- Steam roots and libraries ----

    [Fact]
    public void LinuxSteamRootsAreTheFoldersThatExistInTheirOrder()
    {
        string home = Folder("home");
        string native = Folder(Path.Combine("home", ".local", "share", "Steam"));
        string flatpak = Folder(Path.Combine("home", ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam"));

        Assert.Equal([native, flatpak], GameLocator.LinuxSteamRoots(home));
    }

    [Fact]
    public void LinuxSteamRootsAreEmptyWithoutSteam()
    {
        Assert.Empty(GameLocator.LinuxSteamRoots(Folder("home")));
    }

    [Fact]
    public void LinuxSteamRootsCountAFolderReachedThroughLinksOnce()
    {
        string home = Folder("home");
        string native = Folder(Path.Combine("home", ".local", "share", "Steam"));
        Folder(Path.Combine("home", ".steam"));
        if (!Link(Path.Combine(home, ".steam", "steam"), native) || !Link(Path.Combine(home, ".steam", "root"), native))
        {
            return;
        }

        Assert.Equal([native], GameLocator.LinuxSteamRoots(home));
    }

    [Fact]
    public void RealPathFollowsALinkInTheMiddleOfAPath()
    {
        string real = Folder(Path.Combine("real", "inner"));
        if (!Link(Path.Combine(root, "via"), Path.Combine(root, "real")))
        {
            return;
        }

        Assert.Equal(real, GameFiles.RealPath(Path.Combine(root, "via", "inner")));
        Assert.Equal(Path.Combine(real, "missing"), GameFiles.RealPath(Path.Combine(root, "via", "inner", "missing")));
    }

    [Fact]
    public void ASteamRootFindsTheGameWhateverTheCaseOfItsFolders()
    {
        string steam = Folder("Steam");
        Write(Path.Combine("Steam", "SteamApps", "Common", "Village in the Shade", "VILLAGE.EXE"));

        string found = Assert.Single(GameLocator.Candidates([steam], caseBlind: true));

        // The names on disk, not the ones asked for.
        Assert.Equal(Path.Combine(steam, "SteamApps", "Common", "Village in the Shade"), found);
    }

    [Fact]
    public void TheManifestsInstallDirNamesTheGameFolder()
    {
        string steam = Folder("Steam");
        Write(Path.Combine("Steam", "steamapps", "appmanifest_3934250.acf"),
            "\"AppState\"\n{\n\t\"appid\"\t\t\"3934250\"\n\t\"installdir\"\t\t\"Village Shade Test\"\n}\n");
        Write(Path.Combine("Steam", "steamapps", "common", "Village Shade Test", "village.exe"));
        Write(Path.Combine("Steam", "steamapps", "common", "Village in the Shade", "village.exe"));

        string found = Assert.Single(GameLocator.Candidates([steam], caseBlind: true));
        Assert.Equal(Path.Combine(steam, "steamapps", "common", "Village Shade Test"), found);
    }

    [Fact]
    public void TheManifestIsFoundWhateverItsCase()
    {
        string steam = Folder("Steam");
        Write(Path.Combine("Steam", "steamapps", "APPMANIFEST_3934250.ACF"), "\"installdir\" \"Other Place\"");
        Write(Path.Combine("Steam", "steamapps", "common", "other place", "village.exe"));

        Assert.Equal([Path.Combine(steam, "steamapps", "common", "other place")],
            GameLocator.Candidates([steam], caseBlind: true));
    }

    [Fact]
    public void EveryLibraryInLibraryFoldersIsSearchedOnce()
    {
        string steam = Folder("Steam");
        string library = Folder("Games");
        string escaped = library.Replace("\\", "\\\\");
        string steamEscaped = steam.Replace("\\", "\\\\");
        Write(Path.Combine("Steam", "steamapps", "libraryfolders.vdf"),
            $"\"libraryfolders\"\n{{\n\t\"0\"\n\t{{\n\t\t\"path\"\t\t\"{steamEscaped}\"\n\t}}\n\t\"1\"\n\t{{\n\t\t\"path\"\t\t\"{escaped}\"\n\t}}\n}}\n");
        Write(Path.Combine("Games", "steamapps", "common", "Village in the Shade", "village.exe"));

        Assert.Equal([Path.Combine(library, "steamapps", "common", "Village in the Shade")],
            GameLocator.Candidates([steam], caseBlind: true));
    }

    [Fact]
    public void ALibraryWithoutTheGameGivesNothing()
    {
        Folder(Path.Combine("Steam", "steamapps", "common", "Village in the Shade"));

        Assert.Empty(GameLocator.Candidates([Path.Combine(root, "Steam")], caseBlind: true));
        Assert.Empty(GameLocator.Candidates([Path.Combine(root, "Nowhere")], caseBlind: true));
    }

    // ---- Game folder files without case ----

    [Theory]
    [InlineData("dinput8.dll", "DINPUT8.DLL")]
    [InlineData("mods", "Mods")]
    [InlineData("mods/CanopyData/disabled.txt", "Mods/canopydata/Disabled.TXT")]
    [InlineData("Canopy.log", "canopy.LOG")]
    [InlineData("mods/Some Mod/canopy.ini", "MODS/Some Mod/CANOPY.INI")]
    [InlineData("mods/Some Mod/lua/main.lua", "mods/Some Mod/Lua/Main.lua")]
    [InlineData("mods/Some Mod/data", "mods/Some Mod/DATA")]
    public void CaseBlindLookupFindsTheNameOnDisk(string asked, string onDisk)
    {
        string game = Folder("game");
        string disk = Path.Combine([game, .. onDisk.Split('/')]);
        if (asked.EndsWith("data", StringComparison.Ordinal) || asked == "mods")
        {
            Directory.CreateDirectory(disk);
        }
        else
        {
            Directory.CreateDirectory(Path.GetDirectoryName(disk)!);
            File.WriteAllText(disk, "");
        }

        Assert.Equal(disk, GameFiles.Find(game, asked, caseBlind: true));
        // Backslashes split the same way.
        Assert.Equal(disk, GameFiles.Find(game, asked.Replace('/', '\\'), caseBlind: true));
    }

    [Fact]
    public void APartThatIsNotThereKeepsTheNameCanopyExpects()
    {
        string game = Folder("game");
        Folder(Path.Combine("game", "MODS"));

        Assert.Equal(Path.Combine(game, "MODS", "CanopyData", "disabled.txt"),
            GameFiles.Find(game, "mods/CanopyData/disabled.txt", caseBlind: true));
        Assert.Equal(Path.Combine(game, "lua", "main.lua"), GameFiles.Find(game, "lua/main.lua", caseBlind: true));
    }

    [Fact]
    public void WithoutCaseBlindLookupThePartsAreOnlyJoined()
    {
        string game = Folder("game");
        Folder(Path.Combine("game", "MODS"));

        Assert.Equal(Path.Combine(game, "mods", "x.dll"), GameFiles.Find(game, "mods\\x.dll", caseBlind: false));
        Assert.Equal(Path.Combine(game, "mods", "x.dll"), GameFiles.Find(game, "mods/x.dll", caseBlind: false));
    }

    [Fact]
    public void ShownUsesThePlatformsSeparator()
    {
        string expected = string.Join(Path.DirectorySeparatorChar, "mods", "A", "lua", "main.lua");
        Assert.Equal(expected, GameFiles.Shown("mods/A/lua/main.lua"));
        Assert.Equal(expected, GameFiles.Shown("mods\\A\\lua\\main.lua"));
        Assert.Equal(expected, GameFiles.Shown("mods/A\\lua/main.lua"));
    }

    // ---- Zip entries with either separator ----

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

    [Fact]
    public void AZipEntryLandsInOnePlaceWithEitherSeparator()
    {
        string game = Folder("game");
        File.WriteAllBytes(Path.Combine(game, "village.exe"), [0x4D, 0x5A]);

        ModInstallPlan slashes = ModZip.Plan(game,
            Zip(("mods/Sep/canopy.ini", "[mod]\n"), ("mods/Sep/lua/main.lua", "-- x\n")), "Sep.zip");
        ModInstallPlan backslashes = ModZip.Plan(game,
            Zip(("mods\\Sep\\canopy.ini", "[mod]\n"), ("mods\\Sep\\lua\\main.lua", "-- x\n")), "Sep.zip");
        ModInstallPlan mixed = ModZip.Plan(game,
            Zip(("mods/Sep\\canopy.ini", "[mod]\n"), ("mods\\Sep/lua\\main.lua", "-- x\n")), "Sep.zip");

        Assert.True(slashes.Ok, string.Join(" ", slashes.Problems));
        string[] expected =
        [
            Path.Combine("mods", "Sep", "canopy.ini"),
            Path.Combine("mods", "Sep", "lua", "main.lua"),
        ];
        foreach (ModInstallPlan plan in new[] { slashes, backslashes, mixed })
        {
            Assert.True(plan.Ok, string.Join(" ", plan.Problems));
            Assert.Equal("Sep", plan.Mod);
            Assert.Equal(expected, plan.Writes.Select(file => file.Path).Order(StringComparer.Ordinal));
        }
    }

    [Fact]
    public void AZipEntryEscapingTheModIsRefusedWithEitherSeparator()
    {
        string game = Folder("game");
        File.WriteAllBytes(Path.Combine(game, "village.exe"), [0x4D, 0x5A]);

        foreach (string evil in new[] { "mods/Sep/../../evil.txt", "mods\\Sep\\..\\..\\evil.txt" })
        {
            ModInstallPlan plan = ModZip.Plan(game, Zip(("mods/Sep/canopy.ini", "[mod]\n"), (evil, "x")), "Sep.zip");
            Assert.False(plan.Ok);
        }
    }

    // ---- The version reader ----

    // Canopy's DLLs from a CMake build in this tree or the one it is a copy
    // of, found by walking up from the test's folder.
    private static IEnumerable<string> BuiltDlls()
    {
        string[] wanted =
        [
            Path.Combine("build", "src", "Canopy", "dinput8.dll"),
            Path.Combine("build", "src", "CanopyTools", "canopy_core.dll"),
            Path.Combine("build", "src", "VillageQoL", "VillageQoL.dll"),
            Path.Combine("build", "src", "VillageModLoader", "dinput8.dll"),
        ];
        HashSet<string> seen = new(StringComparer.OrdinalIgnoreCase);
        for (DirectoryInfo? folder = new(AppContext.BaseDirectory); folder is not null; folder = folder.Parent)
        {
            foreach (string relative in wanted)
            {
                string path = Path.Combine(folder.FullName, relative);
                if (File.Exists(path) && seen.Add(path))
                {
                    yield return path;
                }
            }
        }
    }

    [Fact]
    public void TheVersionReaderAgreesWithFileVersionInfo()
    {
        List<string> dlls = BuiltDlls().ToList();
        if (dlls.Count == 0)
        {
            output.WriteLine("Note: no CMake build of Canopy is here, so no DLL was compared.");
            return;
        }
        foreach (string dll in dlls)
        {
            DllVersionResult read = DllVersions.Read(dll);
            Assert.True(read.Ok, read.Error);
            DllVersion version = read.Version!;
            output.WriteLine($"{dll}: {version.ProductName} {version.ProductVersionText}, file {version.FileVersion}");
            if (!OperatingSystem.IsWindows())
            {
                // FileVersionInfo reads no PE resources here; it is the
                // reference only on Windows.
                continue;
            }
            // FileVersionInfo gives "" for a string the table lacks, the
            // reader null; every caller treats both as no value.
            FileVersionInfo info = FileVersionInfo.GetVersionInfo(dll);
            Assert.Equal(info.ProductName ?? "", version.ProductName ?? "");
            Assert.Equal(info.ProductVersion ?? "", version.ProductVersionText ?? "");
            Assert.Equal(info.FileVersion ?? "", version.FileVersionText ?? "");
            Assert.Equal(info.CompanyName ?? "", version.Strings.GetValueOrDefault("CompanyName") ?? "");
            Assert.Equal(info.FileDescription ?? "", version.Strings.GetValueOrDefault("FileDescription") ?? "");
            Assert.Equal(info.LegalCopyright ?? "", version.Strings.GetValueOrDefault("LegalCopyright") ?? "");
            Assert.Equal(info.OriginalFilename ?? "", version.Strings.GetValueOrDefault("OriginalFilename") ?? "");
            Assert.Equal(info.InternalName ?? "", version.Strings.GetValueOrDefault("InternalName") ?? "");
            if (version.FileVersion is { } file)
            {
                Assert.Equal(new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart, info.FilePrivatePart), file);
            }
            if (version.ProductVersion is { } product)
            {
                Assert.Equal(new Version(info.ProductMajorPart, info.ProductMinorPart, info.ProductBuildPart, info.ProductPrivatePart), product);
            }
        }
    }

    [Fact]
    public void ACutDllGivesAnErrorNeverAnException()
    {
        // One that has a version resource, so cuts land inside it too.
        if (BuiltDlls().FirstOrDefault(path => DllVersions.Read(path).Version?.FileVersion is not null) is not { } dll)
        {
            output.WriteLine("Note: no CMake build of Canopy with a version resource is here, so no DLL was cut.");
            return;
        }
        byte[] whole = File.ReadAllBytes(dll);
        DllVersion full = DllVersions.Read(dll).Version!;
        int failed = 0;
        string cut = Path.Combine(root, "cut.dll");
        foreach (int length in new[] { 1, 2, 63, 64, 0x80, 0x100, 0x200 })
        {
            File.WriteAllBytes(cut, whole[..Math.Min(length, whole.Length)]);
            DllVersionResult read = DllVersions.Read(cut);
            Assert.False(read.Ok);
            Assert.False(string.IsNullOrWhiteSpace(read.Error));
        }
        // Anywhere else a cut either still holds the whole resource or says
        // what is wrong; it never throws.
        for (int step = 1; step < 64; ++step)
        {
            File.WriteAllBytes(cut, whole[..(int)((long)whole.Length * step / 64)]);
            DllVersionResult read = DllVersions.Read(cut);
            Assert.True(read.Ok || !string.IsNullOrWhiteSpace(read.Error));
            if (read.Ok && read.Version!.FileVersion is not null)
            {
                Assert.Equal(full.FileVersion, read.Version.FileVersion);
                Assert.Equal(full.ProductVersionText, read.Version.ProductVersionText);
            }
            failed += read.Ok ? 0 : 1;
        }
        // Most cuts lose the section table or the resource itself.
        Assert.True(failed > 0);
    }

    [Theory]
    [InlineData("")]
    [InlineData("This is a text file, not a program.")]
    [InlineData("MZ")]
    public void AFileThatIsNotAPeGivesAnError(string text)
    {
        string path = Write("not-a-dll.dll", text);

        DllVersionResult read = DllVersions.Read(path);

        Assert.False(read.Ok);
        Assert.False(string.IsNullOrWhiteSpace(read.Error));
    }

    [Fact]
    public void ADosHeaderPointingNowhereGivesAnError()
    {
        byte[] bytes = new byte[0x100];
        bytes[0] = (byte)'M';
        bytes[1] = (byte)'Z';
        // e_lfanew far past the end of the file.
        BitConverter.GetBytes(0x7FFFFFF0).CopyTo(bytes, 0x3C);
        string path = Path.Combine(root, "nowhere.dll");
        File.WriteAllBytes(path, bytes);

        Assert.False(DllVersions.Read(path).Ok);
    }

    [Fact]
    public void AMissingFileGivesAnError()
    {
        DllVersionResult read = DllVersions.Read(Path.Combine(root, "absent.dll"));

        Assert.False(read.Ok);
        Assert.Contains("absent.dll", read.Error);
    }

    // ---- Process names ----

    [Fact]
    public void WindowsNamesTheProcessWithoutExe()
    {
        Assert.Equal(["village"], GameProcesses.Names("village.exe", windows: true));
        Assert.True(GameProcesses.Matches("Village", "village.exe", windows: true));
        Assert.False(GameProcesses.Matches("village.exe", "village.exe", windows: true));
    }

    [Theory]
    [InlineData("village.exe")]
    [InlineData("village")]
    [InlineData("VILLAGE.EXE")]
    public void ProtonNamesTheProcessWithOrWithoutExe(string processName)
    {
        Assert.True(GameProcesses.Matches(processName, "village.exe", windows: false));
        Assert.True(GameProcesses.Matches(processName, "village", windows: false));
    }

    [Fact]
    public void ALongNameIsMatchedCutAtFifteenCharacters()
    {
        const string exe = "VillageInTheShadeGame.exe";

        Assert.True(GameProcesses.Matches("VillageInTheSha", exe, windows: false));
        Assert.True(GameProcesses.Matches(exe, exe, windows: false));
        Assert.False(GameProcesses.Matches("VillageInTheSh", exe, windows: false));
        Assert.All(GameProcesses.Names(exe, windows: false), name => Assert.True(name.Length <= exe.Length));
        Assert.Contains(GameProcesses.Names(exe, windows: false), name => name.Length == GameProcesses.LinuxNameLength);
    }

    [Fact]
    public void OtherProcessesAreNotTheGame()
    {
        Assert.False(GameProcesses.Matches("wineserver", "village.exe", windows: false));
        Assert.False(GameProcesses.Matches("village2.exe", "village.exe", windows: false));
        Assert.False(GameProcesses.Matches("explorer.exe", "village.exe", windows: false));
    }

    [Fact]
    public void IsRunningAnswersForANameNoProcessHas()
    {
        Assert.False(GameProcesses.IsRunning("canopy-no-such-process-" + Guid.NewGuid().ToString("N")[..8] + ".exe"));
    }

    // ---- Proton launch option and Canopy Studio's own folders ----

    [Fact]
    public void TheLaunchOptionTellsWineToLoadCanopysDinput8()
    {
        Assert.Equal("WINEDLLOVERRIDES=\"dinput8=n,b\" %command%", ProtonLaunch.LaunchOption);
        Assert.Equal(OperatingSystem.IsLinux(), ProtonLaunch.Needed);
        Assert.EndsWith(".", ProtonLaunch.Explanation);
    }

    [Fact]
    public void StudioFoldersAreNeverEmpty()
    {
        // Only worked out, never created or written.
        Assert.True(Path.IsPathRooted(StudioFolders.UserData()));
        Assert.True(Path.IsPathRooted(StudioFolders.Documents()));
        Assert.Equal("CanopyStudio", Path.GetFileName(StudioFolders.Root()));
    }
}
