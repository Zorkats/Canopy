using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

public sealed class VanillaAssetCacheTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy_studio_vanilla_" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    // Two entries a kind; items and villagers share icon 7, whistles have
    // none. Each icon is written as a one byte file, named as the DLL names
    // them.
    private sealed class FakeSource : IVanillaSource
    {
        public int NameCalls { get; private set; }
        public List<uint> IconIds { get; } = [];
        public string? FailingLanguage { get; init; }
        public bool NamesFail { get; init; }
        // Called before each names read, with how many came before.
        public Action<int>? BeforeNames { get; init; }

        public NameList Names(VanillaKind kind, string language)
        {
            BeforeNames?.Invoke(NameCalls);
            ++NameCalls;
            if (NamesFail || language == FailingLanguage)
            {
                return new NameList { Ok = false, Error = "The table could not be read." };
            }
            ulong? shared = kind is VanillaKind.Item or VanillaKind.Villager ? 7 : null;
            ulong? own = kind == VanillaKind.Whistle ? null : 100 + (ulong)kind;
            return new NameList
            {
                Ok = true,
                Kind = VanillaKinds.Code(kind),
                Language = language,
                Entries =
                [
                    new GameName(1, "FIRST", $"{kind} one {language}", shared, 3),
                    new GameName(2, null, "", own, null),
                ],
            };
        }

        public IconBatch Icons(IReadOnlyList<uint> ids, string outFolder)
        {
            Directory.CreateDirectory(outFolder);
            foreach (uint id in ids)
            {
                IconIds.Add(id);
                File.WriteAllBytes(Path.Combine(outFolder, $"{id}.png"), [1]);
            }
            return new IconBatch { Ok = true, Written = ids.Count, Problems = ids.Contains(103u) ? ["Icon 103 has no cell."] : [] };
        }
    }

    private VanillaAssetCache NewCache() => new(Path.Combine(root, "cache"));

    private static void Built(AssetPrepareResult result) => Assert.True(result.Ok, result.Error);

    [Fact]
    public void BuildWritesEveryNamesFileEveryIconAndAManifest()
    {
        VanillaAssetCache cache = NewCache();
        FakeSource source = new();
        List<string> steps = [];

        AssetPrepareResult result = cache.Build(source, 0x1234, new SyncProgress(steps.Add));

        Assert.True(result.Ok, result.Error);
        foreach (VanillaKind kind in VanillaKinds.All)
        {
            foreach (GameLanguage language in VanillaKinds.Languages)
            {
                Assert.True(File.Exists(cache.NamesPath(0x1234, kind, language.Code)));
            }
        }
        // Icon 7 is shared by items and villagers and cut once.
        Assert.Equal([7u, 100u, 101u, 103u, 104u, 105u], source.IconIds.Order());
        Assert.All(source.IconIds, id => Assert.NotNull(cache.IconPath(0x1234, id)));
        Assert.Null(cache.IconPath(0x1234, 999));

        VanillaManifest? manifest = cache.Load(0x1234);
        Assert.NotNull(manifest);
        Assert.Equal(VanillaAssetCache.Recipe, manifest.Recipe);
        Assert.Equal(VanillaKinds.All.Count * VanillaKinds.Languages.Count, manifest.Names.Count);
        Assert.Equal(6, manifest.Icons);
        Assert.Single(manifest.Problems);
        Assert.Equal(manifest.Problems, result.Problems);
        Assert.True(cache.IsCurrent(0x1234));
        Assert.False(cache.IsCurrent(0x1235));
        Assert.Equal(Path.Combine(root, "cache", "00001234"), cache.BuildFolder(0x1234));
        Assert.False(Directory.Exists(cache.BuildFolder(0x1234) + ".new"));

        Assert.Contains("Reading item names (3 of 7 languages)", steps);
        Assert.Contains("Cutting item icons", steps);
        Assert.Contains("Cutting crafting recipe icons", steps);
        Assert.DoesNotContain("Cutting whistle icons", steps);
    }

    [Fact]
    public void BuildIsSkippedWhenTheCacheIsCurrent()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 9));
        FakeSource second = new();

        AssetPrepareResult again = cache.Build(second, 9);

        Assert.True(again.Ok, again.Error);
        Assert.Single(again.Problems);
        Assert.Equal(0, second.NameCalls);
        Assert.Empty(second.IconIds);
    }

    [Fact]
    public void BuildCancelledHalfwayKeepsTheOldCache()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 5));
        using CancellationTokenSource cancel = new();
        FakeSource source = new() { BeforeNames = count => { if (count == 10) cancel.Cancel(); } };

        AssetPrepareResult result = cache.Build(source, 6, cancel: cancel.Token);

        Assert.False(result.Ok);
        Assert.False(string.IsNullOrEmpty(result.Error));
        Assert.Equal(11, source.NameCalls);
        Assert.True(cache.IsCurrent(5));
        Assert.False(cache.IsCurrent(6));
        Assert.False(Directory.Exists(cache.BuildFolder(6) + ".new"));
    }

    [Fact]
    public void EntriesBeforeTheCacheComeFromTheSourceAndAreKept()
    {
        VanillaAssetCache cache = NewCache();
        FakeSource source = new();

        VanillaRead first = cache.Entries(source, 3, VanillaKind.Villager, "fr");

        Assert.True(first.Ok, first.Error);
        Assert.Equal(2, first.Entries.Count);
        Assert.All(first.Entries, entry => Assert.Equal(VanillaKind.Villager, entry.Kind));
        Assert.Equal(1, source.NameCalls);
        Assert.True(File.Exists(cache.NamesPath(3, VanillaKind.Villager, "fr")));
        Assert.Null(cache.Load(3));
        Assert.False(cache.IsCurrent(3));

        VanillaRead second = cache.Entries(source, 3, VanillaKind.Villager, "fr");

        Assert.Equal(1, source.NameCalls);
        Assert.Equal(first.Entries, second.Entries);
    }

    [Fact]
    public void EntriesThatCannotBeReadSayWhy()
    {
        VanillaAssetCache cache = NewCache();

        VanillaRead read = cache.Entries(new FakeSource { NamesFail = true }, 3, VanillaKind.Item, "en");

        Assert.False(read.Ok);
        Assert.Empty(read.Entries);
        Assert.False(File.Exists(cache.NamesPath(3, VanillaKind.Item, "en")));
    }

    [Fact]
    public void DamagedManifestReadsAsNoCache()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 4));
        File.WriteAllText(Path.Combine(cache.BuildFolder(4), "manifest.json"), "{not json");

        Assert.Null(cache.Load(4));
        Assert.False(cache.IsCurrent(4));
        // The files the cache holds are still used.
        FakeSource source = new();
        Assert.Equal(2, cache.Entries(source, 4, VanillaKind.Item, "en").Entries.Count);
        Assert.Equal(0, source.NameCalls);
        Assert.NotNull(cache.IconPath(4, 7));

        // A damaged names file is read again from the source.
        File.WriteAllText(cache.NamesPath(4, VanillaKind.Item, "en"), "[");
        Assert.Equal(2, cache.Entries(source, 4, VanillaKind.Item, "en").Entries.Count);
        Assert.Equal(1, source.NameCalls);

        Built(cache.Build(source, 4));
        Assert.True(cache.IsCurrent(4));
    }

    [Fact]
    public void ManifestOfAnotherBuildReadsAsNoCache()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 4));
        Directory.Move(cache.BuildFolder(4), cache.BuildFolder(8));

        Assert.Null(cache.Load(8));
        Assert.False(cache.IsCurrent(8));
    }

    [Fact]
    public void NewBuildRemovesOlderBuildFolders()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 1));
        cache.Entries(new FakeSource(), 2, VanillaKind.Item, "en");
        Directory.CreateDirectory(cache.BuildFolder(3) + ".new");
        string other = Path.Combine(cache.Root, "not-a-build");
        Directory.CreateDirectory(other);

        Built(cache.Build(new FakeSource(), 4));

        Assert.False(Directory.Exists(cache.BuildFolder(1)));
        Assert.False(Directory.Exists(cache.BuildFolder(2)));
        Assert.False(Directory.Exists(cache.BuildFolder(3) + ".new"));
        Assert.True(Directory.Exists(other));
        Assert.True(cache.IsCurrent(4));
    }

    [Fact]
    public void LanguageThatFailsStillBuildsWithAProblem()
    {
        VanillaAssetCache cache = NewCache();

        AssetPrepareResult result = cache.Build(new FakeSource { FailingLanguage = "ko" }, 2);

        Assert.True(result.Ok, result.Error);
        // One problem per kind for Korean, plus the icon batch's.
        Assert.Equal(VanillaKinds.All.Count + 1, result.Problems.Count);
        VanillaManifest manifest = cache.Load(2)!;
        Assert.Equal(VanillaKinds.All.Count * (VanillaKinds.Languages.Count - 1), manifest.Names.Count);
        Assert.Equal(result.Problems, manifest.Problems);
        Assert.False(File.Exists(cache.NamesPath(2, VanillaKind.Item, "ko")));
        Assert.True(cache.IsCurrent(2));
    }

    [Fact]
    public void SourceWithNoNamesFails()
    {
        VanillaAssetCache cache = NewCache();
        Built(cache.Build(new FakeSource(), 1));

        AssetPrepareResult result = cache.Build(new FakeSource { NamesFail = true }, 2);

        Assert.False(result.Ok);
        Assert.False(string.IsNullOrEmpty(result.Error));
        Assert.Equal(VanillaKinds.All.Count * VanillaKinds.Languages.Count, result.Problems.Count);
        Assert.False(Directory.Exists(cache.BuildFolder(2)));
        Assert.False(Directory.Exists(cache.BuildFolder(2) + ".new"));
        Assert.True(cache.IsCurrent(1));
    }

    [Fact]
    public void SavedIconIsCached()
    {
        VanillaAssetCache cache = NewCache();
        Assert.Null(cache.IconPath(1, 42));

        string? path = cache.SaveIcon(1, 42, new TextureImage(2, 2, new byte[16]));

        Assert.NotNull(path);
        Assert.Equal(path, cache.IconPath(1, 42));
        Assert.Equal(new byte[] { 0x89, 0x50, 0x4E, 0x47 }, File.ReadAllBytes(path)[..4]);
    }

    // Progress<T> posts to the thread pool; this reports in line, so the
    // steps are all there when Build returns.
    private sealed class SyncProgress(Action<string> report) : IProgress<string>
    {
        public void Report(string value) => report(value);
    }
}

// The cache built from the installed game. Skips like NativeTests; checks
// counts only.
public sealed class VanillaAssetCacheNativeTests(ITestOutputHelper output) : IDisposable
{
    private const string DefaultGameFolder = @"D:\SteamLibrary\steamapps\common\Village in the Shade";

    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy_studio_vanilla_" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private bool Ready(out string folder)
    {
        folder = "";
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        string found = Environment.GetEnvironmentVariable("CANOPY_GAME_DIR") is { Length: > 0 } dir
            ? dir
            : Environment.GetEnvironmentVariable("CANOPY_GAME_FOLDER") is { Length: > 0 } set
                ? set
                : DefaultGameFolder;
        if (!Directory.Exists(found))
        {
            output.WriteLine("Skipped: the game is not installed here (set CANOPY_GAME_DIR).");
            return false;
        }
        folder = found;
        return true;
    }

    [Fact]
    public void InstalledGameGivesNamesInEveryLanguageAndIcons()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        GameInfo info = CanopyCore.Game(folder);
        Assert.True(info.Ok, info.Error);
        VanillaAssetCache cache = new(Path.Combine(root, "cache"));

        AssetPrepareResult result = cache.Build(new NativeVanillaSource(folder), info.Build);

        Assert.True(result.Ok, result.Error);
        VanillaManifest manifest = cache.Load(info.Build)!;
        Assert.Equal(VanillaKinds.All.Count * VanillaKinds.Languages.Count, manifest.Names.Count);
        Assert.True(manifest.Icons > 0);
        Assert.Equal(manifest.Icons, Directory.GetFiles(Path.Combine(cache.BuildFolder(info.Build), "icons"), "*.png").Length);
        Assert.True(cache.IsCurrent(info.Build));
        VanillaRead items = cache.Entries(new NativeVanillaSource(folder), info.Build, VanillaKind.Item, VanillaKinds.DefaultLanguage);
        Assert.True(items.Ok, items.Error);
        Assert.NotEmpty(items.Entries);
        output.WriteLine($"{manifest.Names.Count} names files, {items.Entries.Count} items, {manifest.Icons} icons, {manifest.Problems.Count} problems");
    }
}
