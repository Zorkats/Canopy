using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using CanopyStudio.Core.Install;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;

namespace CanopyStudio.Tests;

// The installer on throwaway game folders and a fake GitHub. Nothing here
// touches the network or a real game folder.
public sealed class InstallTests : IDisposable
{
    private static readonly byte[] CanopyDll = Encoding.ASCII.GetBytes("MZ fake loader. Canopy starting, API version 4.");
    private static readonly byte[] QoLDll = Encoding.ASCII.GetBytes("MZ fake VillageQoL");
    private static readonly byte[] OtherDll = Encoding.ASCII.GetBytes("MZ another proxy");
    private static readonly DateTime Now = new(2026, 10, 15, 14, 30, 15);

    private readonly string game = Path.Combine(Path.GetTempPath(), "canopy-install-" + Guid.NewGuid().ToString("N"));

    public InstallTests()
    {
        Directory.CreateDirectory(game);
        File.WriteAllBytes(Path.Combine(game, "village.exe"), [0x4D, 0x5A]);
    }

    public void Dispose()
    {
        foreach (string file in Directory.EnumerateFiles(game, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(file, FileAttributes.Normal);
        }
        Directory.Delete(game, recursive: true);
    }

    private static byte[] Zip(params (string Name, byte[] Bytes)[] entries)
    {
        using MemoryStream memory = new();
        using (ZipArchive archive = new(memory, ZipArchiveMode.Create, leaveOpen: true))
        {
            foreach ((string name, byte[] bytes) in entries)
            {
                using Stream stream = archive.CreateEntry(name).Open();
                stream.Write(bytes);
            }
        }
        return memory.ToArray();
    }

    // The packages as the release workflow lays them out.
    private static byte[] CanopyZip(byte[]? loader = null) => Zip(
        ("dinput8.dll", loader ?? CanopyDll),
        ("README.md", "readme"u8.ToArray()),
        ("LICENSE", "licence"u8.ToArray()),
        ("THIRD_PARTY_NOTICES.txt", "notices"u8.ToArray()));

    private static byte[] QoLZip() => Zip(
        ("mods\\VillageQoL.dll", QoLDll),
        ("LICENSE", "licence"u8.ToArray()));

    private static CanopyPackage Package(PackageKind kind, byte[]? loader = null)
    {
        byte[] zip = kind == PackageKind.Canopy ? CanopyZip(loader) : QoLZip();
        (CanopyPackage? package, string? error) = CanopyPackage.Read(zip, kind, "1.2.0");
        Assert.Null(error);
        return package!;
    }

    private string[] GameFiles() => Directory.EnumerateFileSystemEntries(game, "*", SearchOption.AllDirectories)
        .Select(path => Path.GetRelativePath(game, path))
        .Order(StringComparer.OrdinalIgnoreCase)
        .ToArray();

    private static string Sha(char digit) => "sha256:" + new string(digit, 64);

    private static string AssetJson(string name, string? digest) =>
        $$"""{ "name": "{{name}}", "browser_download_url": "https://example.test/{{name}}", "size": 100{{(digest is null ? "" : $", \"digest\": \"{digest}\"")}} }""";

    private static string ReleaseJson(params string[] assets) => $$"""
        {
          "tag_name": "v1.2.0",
          "assets": [ {{string.Join(", ", assets)}} ]
        }
        """;

    private sealed class FakeGitHub(Func<HttpRequestMessage, HttpResponseMessage> answer) : HttpMessageHandler
    {
        public List<string> Asked { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            Asked.Add(request.RequestUri!.AbsoluteUri);
            Assert.Contains("CanopyStudio", request.Headers.UserAgent.ToString());
            return Task.FromResult(answer(request));
        }
    }

    [Fact]
    public void ParsePicksEachPackageByItsExactName()
    {
        string hash = new('a', 64);
        ReleaseCheck check = ReleaseFeed.Parse(ReleaseJson(
            AssetJson("Canopy-SDK-1.2.0.zip", Sha('0')),
            AssetJson("Canopy-1.2.0.zip", "sha256:" + hash.ToUpperInvariant()),
            AssetJson("VillageQoL-1.2.0.zip", Sha('b'))));

        Assert.True(check.Ok, check.Error);
        Assert.Equal("v1.2.0", check.Release!.Tag);
        Assert.Equal("1.2.0", check.Release.Version);
        Assert.Equal(PackageKind.Canopy, check.Release.Canopy.Kind);
        Assert.Equal("Canopy-1.2.0.zip", check.Release.Canopy.Name);
        Assert.Equal(hash, check.Release.Canopy.Sha256);
        Assert.Equal("VillageQoL-1.2.0.zip", check.Release.VillageQoL!.Name);
        Assert.Equal(new string('b', 64), check.Release.VillageQoL.Sha256);
    }

    [Fact]
    public void ParseOffersCanopyWithoutAUsableVillageQoL()
    {
        ReleaseCheck missing = ReleaseFeed.Parse(ReleaseJson(AssetJson("Canopy-1.2.0.zip", Sha('a'))));
        ReleaseCheck unlisted = ReleaseFeed.Parse(ReleaseJson(
            AssetJson("Canopy-1.2.0.zip", Sha('a')),
            AssetJson("VillageQoL-1.2.0.zip", null)));

        Assert.True(missing.Ok, missing.Error);
        Assert.Null(missing.Release!.VillageQoL);
        Assert.True(unlisted.Ok, unlisted.Error);
        Assert.Null(unlisted.Release!.VillageQoL);
    }

    [Fact]
    public void ParseRefusesACanopyPackageWithoutSha256OrNoneAtAll()
    {
        Assert.Contains("SHA-256", ReleaseFeed.Parse(ReleaseJson(AssetJson("Canopy-1.2.0.zip", null))).Error);
        // The SDK package and the old combined name are not the loader.
        Assert.Contains("has no Canopy-1.2.0.zip", ReleaseFeed.Parse(ReleaseJson(
            AssetJson("Canopy-SDK-1.2.0.zip", Sha('0')),
            AssetJson("VillageQoL-1.2.0.zip", Sha('b')))).Error);
        Assert.False(ReleaseFeed.Parse("not json").Ok);
    }

    [Fact]
    public async Task NoPublishedReleaseIsSaidPlainly()
    {
        FakeGitHub github = new(_ => new HttpResponseMessage(HttpStatusCode.NotFound));
        ReleaseCheck check = await new ReleaseFeed(new HttpClient(github)).LatestAsync();

        Assert.False(check.Ok);
        Assert.Equal("No Canopy release is published yet.", check.Error);
        Assert.Equal(["https://api.github.com/repos/Zorkats/Canopy/releases/latest"], github.Asked);
    }

    [Fact]
    public async Task DownloadIsCheckedAgainstTheListedSha256()
    {
        byte[] zip = CanopyZip();
        string good = Convert.ToHexStringLower(SHA256.HashData(zip));
        FakeGitHub github = new(_ => new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(zip) });
        ReleaseFeed feed = new(new HttpClient(github));
        ReleaseAsset asset = new(PackageKind.Canopy, "Canopy-1.2.0.zip", new Uri("https://example.test/Canopy-1.2.0.zip"), good, zip.Length);

        ReleaseDownload download = await feed.DownloadAsync(asset, "1.2.0");
        ReleaseDownload tampered = await feed.DownloadAsync(asset with { Sha256 = new string('0', 64) }, "1.2.0");

        Assert.True(download.Ok, download.Error);
        Assert.Equal(CanopyDll, download.Package!.File.Bytes);
        Assert.Equal("1.2.0", download.Package.Version);
        Assert.False(tampered.Ok);
        Assert.Contains("does not match", tampered.Error);
    }

    [Fact]
    public void PackageHoldsItsOneFile()
    {
        Assert.Contains("lacks dinput8.dll", CanopyPackage.Read(QoLZip(), PackageKind.Canopy, "1").Error);
        Assert.Contains("lacks mods\\VillageQoL.dll", CanopyPackage.Read(CanopyZip(), PackageKind.VillageQoL, "1").Error);
        Assert.Contains("is not Canopy", CanopyPackage.Read(CanopyZip(OtherDll), PackageKind.Canopy, "1").Error);
        Assert.Contains("not a zip", CanopyPackage.Read([1, 2, 3], PackageKind.Canopy, "1").Error);

        // PowerShell 7 writes slashes.
        (CanopyPackage? qol, string? error) = CanopyPackage.Read(Zip(("mods/VillageQoL.dll", QoLDll)), PackageKind.VillageQoL, "1");
        Assert.Null(error);
        Assert.Equal(Path.Combine("mods", "VillageQoL.dll"), qol!.File.Path);
        Assert.Equal(QoLDll, qol.File.Bytes);
    }

    [Fact]
    public void CanopyInstallWritesOnlyTheLoader()
    {
        InstallResult result = CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now);

        Assert.True(result.Ok, result.Message);
        Assert.Null(result.Backup);
        Assert.Equal(["dinput8.dll", "village.exe"], GameFiles());
        Assert.Equal(CanopyDll, File.ReadAllBytes(Path.Combine(game, "dinput8.dll")));
        Assert.Equal(new InstalledCanopy(LoaderKind.Canopy, null, false, null), CanopyInstaller.Inspect(game));
    }

    [Fact]
    public void VillageQoLInstallsOnItsOwn()
    {
        InstallResult result = CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now);

        Assert.True(result.Ok, result.Message);
        Assert.Contains("loads with Canopy", result.Message);
        Assert.Equal(["mods", Path.Combine("mods", "VillageQoL.dll"), "village.exe"], GameFiles());
        Assert.Equal(new InstalledCanopy(LoaderKind.None, null, true, null), CanopyInstaller.Inspect(game));
    }

    [Fact]
    public void AnotherLoaderIsRenamedNeverDeleted()
    {
        File.WriteAllBytes(Path.Combine(game, "dinput8.dll"), OtherDll);
        Assert.Equal(LoaderKind.Other, CanopyInstaller.Inspect(game).Loader);

        // VillageQoL does not touch it.
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now).Ok);
        Assert.Equal(OtherDll, File.ReadAllBytes(Path.Combine(game, "dinput8.dll")));

        InstallResult result = CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now);

        Assert.True(result.Ok, result.Message);
        Assert.Equal("dinput8.dll.old-143015", result.Backup);
        Assert.Equal(OtherDll, File.ReadAllBytes(Path.Combine(game, "dinput8.dll.old-143015")));
        Assert.Equal(LoaderKind.Canopy, CanopyInstaller.Inspect(game).Loader);
    }

    [Fact]
    public void NothingIsWrittenWhileTheGameRuns()
    {
        Assert.False(CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: true, Now).Ok);
        Assert.False(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: true, Now).Ok);
        Assert.Equal(["village.exe"], GameFiles());

        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now).Ok);
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now).Ok);
        Assert.False(CanopyInstaller.Uninstall(game, null, gameRunning: true).Ok);
        Assert.False(CanopyInstaller.RemoveVillageQoL(game, gameRunning: true).Ok);
        Assert.Equal(new InstalledCanopy(LoaderKind.Canopy, null, true, null), CanopyInstaller.Inspect(game));
    }

    [Fact]
    public void AFolderWithoutTheGameIsRefused()
    {
        File.Delete(Path.Combine(game, "village.exe"));

        Assert.False(CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now).Ok);
        Assert.False(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now).Ok);
        Assert.Empty(GameFiles());
    }

    [Theory]
    [InlineData(PackageKind.Canopy)]
    [InlineData(PackageKind.VillageQoL)]
    public void AFailedUpdateLeavesTheOldFile(PackageKind kind)
    {
        byte[] old = Encoding.ASCII.GetBytes("MZ old build. Canopy starting, API version 3.");
        string path = Path.Combine(game, CanopyPackage.PathOf(kind));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, old);
        // A read-only file makes the write fail.
        File.SetAttributes(path, FileAttributes.ReadOnly);

        InstallResult result = CanopyInstaller.Install(game, Package(kind), gameRunning: false, Now);

        Assert.False(result.Ok);
        Assert.Contains("is as it was", result.Message);
        Assert.Equal(old, File.ReadAllBytes(path));
    }

    [Fact]
    public void UninstallLeavesVillageQoLAndOtherModsAndRestoresTheOldLoader()
    {
        File.WriteAllBytes(Path.Combine(game, "dinput8.dll"), OtherDll);
        InstallResult install = CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now);
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now).Ok);
        File.WriteAllText(Path.Combine(game, "mods", "VillageQoL.ini"), "[QoL]");
        Directory.CreateDirectory(Path.Combine(game, "mods", "SomeMod"));

        InstallResult result = CanopyInstaller.Uninstall(game, install.Backup, gameRunning: false);

        Assert.True(result.Ok, result.Message);
        Assert.Equal(["dinput8.dll", "mods", Path.Combine("mods", "SomeMod"), Path.Combine("mods", "VillageQoL.dll"),
            Path.Combine("mods", "VillageQoL.ini"), "village.exe"], GameFiles());
        Assert.Equal(OtherDll, File.ReadAllBytes(Path.Combine(game, "dinput8.dll")));
    }

    [Fact]
    public void RemovingVillageQoLKeepsItsSettingsAndCanopy()
    {
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now).Ok);
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.VillageQoL), gameRunning: false, Now).Ok);
        File.WriteAllText(Path.Combine(game, "mods", "VillageQoL.ini"), "[QoL]");

        InstallResult result = CanopyInstaller.RemoveVillageQoL(game, gameRunning: false);

        Assert.True(result.Ok, result.Message);
        Assert.Equal(["dinput8.dll", "mods", Path.Combine("mods", "VillageQoL.ini"), "village.exe"], GameFiles());
        Assert.False(CanopyInstaller.RemoveVillageQoL(game, gameRunning: false).Ok);
    }

    [Fact]
    public void UninstallLeavesAnotherLoaderAndOddBackupNamesAlone()
    {
        File.WriteAllBytes(Path.Combine(game, "dinput8.dll"), OtherDll);
        Assert.False(CanopyInstaller.Uninstall(game, null, gameRunning: false).Ok);
        Assert.Equal(OtherDll, File.ReadAllBytes(Path.Combine(game, "dinput8.dll")));

        File.Delete(Path.Combine(game, "dinput8.dll"));
        Assert.True(CanopyInstaller.Install(game, Package(PackageKind.Canopy), gameRunning: false, Now).Ok);
        Assert.True(CanopyInstaller.Uninstall(game, "village.exe", gameRunning: false).Ok);
        Assert.True(File.Exists(Path.Combine(game, "village.exe")));
        Assert.False(File.Exists(Path.Combine(game, "dinput8.dll")));
    }

    [Fact]
    public void SetupNamesAnotherProgramsLoader()
    {
        GameInfo info = new() { Exe = true, Supported = true, Ok = true, Canopy = true, Mods = true, Log = true };

        SetupCheck other = SetupChecks.For(info, true, LoaderKind.Other).Single(check => check.Title == "Canopy");
        SetupCheck canopy = SetupChecks.For(info, true, LoaderKind.Canopy).Single(check => check.Title == "Canopy");

        Assert.Equal(CheckState.Warn, other.State);
        Assert.Contains("another program's", other.Detail);
        Assert.Equal(CheckState.Pass, canopy.State);
    }

    // The DLLs from the CMake build carry their versions in a resource.
    // Passes with a note when they are not built.
    [Fact]
    public void BuiltDllsReportTheirVersions()
    {
        DirectoryInfo? root = new(AppContext.BaseDirectory);
        while (root is not null && !File.Exists(Path.Combine(root.FullName, "build", "src", "Canopy", "dinput8.dll")))
        {
            root = root.Parent;
        }
        if (root is null)
        {
            return;
        }
        File.Copy(Path.Combine(root.FullName, "build", "src", "Canopy", "dinput8.dll"), Path.Combine(game, "dinput8.dll"));
        Directory.CreateDirectory(Path.Combine(game, "mods"));
        File.Copy(Path.Combine(root.FullName, "build", "src", "VillageQoL", "VillageQoL.dll"),
            Path.Combine(game, "mods", "VillageQoL.dll"));

        InstalledCanopy installed = CanopyInstaller.Inspect(game);

        Assert.Equal(LoaderKind.Canopy, installed.Loader);
        Assert.False(string.IsNullOrWhiteSpace(installed.Version));
        Assert.True(installed.HasQoL);
        Assert.Equal(installed.Version, installed.QoLVersion);
    }

    [Theory]
    [InlineData("1.2.0", null, true)]
    [InlineData("1.2.0", "1.1.9", true)]
    [InlineData("1.2.0", "1.2.0", false)]
    [InlineData("1.2.0", "1.10.0", false)]
    [InlineData("1.2.0", "dev-abc1234", true)]
    [InlineData("dev-abc1234", "1.0.0", false)]
    public void NewerComparesReleaseNumbers(string latest, string? installed, bool newer)
    {
        Assert.Equal(newer, CanopyInstaller.IsNewer(latest, installed));
    }
}
