using System.Buffers.Binary;
using System.IO.Compression;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

public sealed class PngWriterTests
{
    [Fact]
    public void EncodesBgraAsRgbaThatDecodesBack()
    {
        // 3 x 2 pixels, BGRA.
        byte[] pixels =
        [
            1, 2, 3, 255, 40, 50, 60, 128, 200, 100, 0, 0,
            9, 8, 7, 6, 255, 255, 255, 255, 0, 0, 0, 255,
        ];
        byte[] png = PngWriter.Encode(new TextureImage(3, 2, pixels));

        Assert.Equal(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, png[..8]);
        Assert.Equal(3, BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(16)));
        Assert.Equal(2, BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(20)));

        byte[] rgba = Decode(png, 3, 2);
        for (int i = 0; i < pixels.Length; i += 4)
        {
            Assert.Equal(pixels[i + 2], rgba[i]);
            Assert.Equal(pixels[i + 1], rgba[i + 1]);
            Assert.Equal(pixels[i], rgba[i + 2]);
            Assert.Equal(pixels[i + 3], rgba[i + 3]);
        }
    }

    // Enough of a PNG reader for what PngWriter writes: one IDAT, Sub rows.
    private static byte[] Decode(byte[] png, int width, int height)
    {
        int at = 8;
        byte[]? idat = null;
        while (at < png.Length)
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(at));
            string type = System.Text.Encoding.ASCII.GetString(png, at + 4, 4);
            if (type == "IDAT")
            {
                idat = png[(at + 8)..(at + 8 + length)];
            }
            at += 12 + length;
        }
        Assert.NotNull(idat);
        using ZLibStream zlib = new(new MemoryStream(idat), CompressionMode.Decompress);
        using MemoryStream raw = new();
        zlib.CopyTo(raw);
        byte[] rows = raw.ToArray();
        int stride = width * 4;
        Assert.Equal((stride + 1) * height, rows.Length);
        byte[] rgba = new byte[stride * height];
        for (int y = 0; y < height; ++y)
        {
            Assert.Equal(1, rows[y * (stride + 1)]);
            for (int x = 0; x < stride; ++x)
            {
                byte left = x >= 4 ? rgba[y * stride + x - 4] : (byte)0;
                rgba[y * stride + x] = unchecked((byte)(rows[y * (stride + 1) + 1 + x] + left));
            }
        }
        return rgba;
    }
}

public sealed class BackdropTests
{
    private static TextureImage Solid(int width, int height, byte b, byte g, byte r, byte a)
    {
        byte[] pixels = new byte[width * height * 4];
        for (int i = 0; i < pixels.Length; i += 4)
        {
            pixels[i] = b;
            pixels[i + 1] = g;
            pixels[i + 2] = r;
            pixels[i + 3] = a;
        }
        return new TextureImage(width, height, pixels);
    }

    [Fact]
    public void OpaqueSkyUnderClearLayersFillsThePicture()
    {
        Season spring = Backdrops.Seasons[0];
        TextureImage? picture = Backdrops.Compose(spring, path =>
            path.Contains("_02_") ? (Solid(2048, 2048, 10, 20, 30, 255), null) : (Solid(2048, 1024, 0, 0, 0, 0), null),
            out string? error);

        Assert.Null(error);
        Assert.NotNull(picture);
        Assert.Equal(Backdrops.Width * Backdrops.Height * 4, picture.Pixels.Length);
        Assert.Equal(new byte[] { 10, 20, 30, 255 }, picture.Pixels[..4]);
        Assert.Equal(new byte[] { 10, 20, 30, 255 }, picture.Pixels[^4..]);
    }

    [Fact]
    public void HalfAlphaBlendsOverTheLayerBelow()
    {
        Season winter = Backdrops.Seasons[2];
        TextureImage? picture = Backdrops.Compose(winter, path =>
            path.Contains("_02_") ? (Solid(2048, 2048, 0, 0, 0, 255), null)
            : path.Contains("_03_") ? (Solid(2048, 1024, 200, 200, 200, 128), null)
            : (Solid(2048, 1024, 0, 0, 0, 0), null),
            out _);

        // The foliage layer starts at row 160 and reaches the bottom.
        int last = (Backdrops.Width * Backdrops.Height - 1) * 4;
        Assert.Equal(100, picture!.Pixels[last]);
        Assert.Equal(0, picture.Pixels[0]);
    }

    [Fact]
    public void MissingLayerFailsNamingIt()
    {
        TextureImage? picture = Backdrops.Compose(Backdrops.Seasons[1], path =>
            path.Contains("_06_") ? (null, "gone") : (Solid(2048, 1024, 0, 0, 0, 0), null),
            out string? error);

        Assert.Null(picture);
        Assert.Contains("bg_8100_06_aut", error);
        Assert.Contains("gone", error);
    }
}

public sealed class GameAssetCacheTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy_studio_assets_" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private sealed class FakeSource(bool texturesWork, bool filesWork) : IGameAssetSource
    {
        public IReadOnlyList<string> Paths { get; } =
        [
            "data/misc/lora-bold.ttf", "data/misc/kiwimaru-regular.otf", "data/misc/lora-notes.txt",
            "data/misc/resourcehanroundedk-regular.ttf", "data/misc/deeper/lora-medium.ttf",
            "data/texture/lora-medium.ttf",
        ];

        public byte[]? File(string gamePath, out string? error)
        {
            error = filesWork ? null : "unreadable";
            return filesWork ? [1, 2, 3] : null;
        }

        public (TextureImage? Image, string? Error) Texture(string gamePath) =>
            texturesWork ? (new TextureImage(2048, 1024, new byte[2048 * 1024 * 4]), null) : (null, "undecodable");
    }

    [Fact]
    public void FontPathsAreTheThemeFamiliesRightInDataMisc()
    {
        Assert.Equal(["data/misc/kiwimaru-regular.otf", "data/misc/lora-bold.ttf"], GameAssetCache.FontPaths(new FakeSource(true, true).Paths));
    }

    [Fact]
    public void BuildWritesEveryAssetAndIsCurrentForThatBuildOnly()
    {
        GameAssetCache cache = new(Path.Combine(root, "game-assets"));

        AssetBuild result = cache.Build(new FakeSource(true, true), build: 77);

        Assert.True(result.Ok, result.Error);
        Assert.Equal(["kiwimaru-regular.otf", "lora-bold.ttf"], result.Manifest!.Fonts);
        Assert.Equal(Backdrops.Seasons.Select(season => season.Id), result.Manifest.Backdrops);
        Assert.Empty(result.Manifest.Problems);
        Assert.True(File.Exists(cache.BackdropPath("winter")));
        Assert.True(cache.IsCurrent(77));
        Assert.False(cache.IsCurrent(78));

        File.Delete(cache.FontPath("lora-bold.ttf"));
        Assert.False(cache.IsCurrent(77));
    }

    [Fact]
    public void PartialBuildListsItsProblems()
    {
        GameAssetCache cache = new(Path.Combine(root, "game-assets"));

        AssetBuild result = cache.Build(new FakeSource(texturesWork: false, filesWork: true), build: 1);

        Assert.True(result.Ok, result.Error);
        Assert.Empty(result.Manifest!.Backdrops);
        Assert.Equal(Backdrops.Seasons.Count, result.Manifest.Problems.Count);
    }

    [Fact]
    public void FailedBuildKeepsTheOldCache()
    {
        GameAssetCache cache = new(Path.Combine(root, "game-assets"));
        Assert.True(cache.Build(new FakeSource(true, true), build: 5).Ok);

        AssetBuild failed = cache.Build(new FakeSource(false, false), build: 6);

        Assert.False(failed.Ok);
        Assert.False(string.IsNullOrEmpty(failed.Error));
        Assert.True(cache.IsCurrent(5));
        Assert.False(Directory.Exists(cache.Folder + ".new"));
    }

    [Fact]
    public void DamagedManifestReadsAsNoCache()
    {
        GameAssetCache cache = new(Path.Combine(root, "game-assets"));
        Directory.CreateDirectory(cache.Folder);
        File.WriteAllText(Path.Combine(cache.Folder, "manifest.json"), "{not json");

        Assert.Null(cache.Load());
        Assert.False(cache.IsCurrent(0));
    }
}

// The cache built from the installed game. Skips like NativeTests.
public sealed class GameAssetCacheNativeTests(ITestOutputHelper output) : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy_studio_assets_" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void InstalledGameGivesFontsAndEveryBackdrop()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return;
        }
        string? folder = Environment.GetEnvironmentVariable("CANOPY_GAME_FOLDER") is { Length: > 0 } set
            ? set
            : GameLocator.Candidates().FirstOrDefault();
        if (folder is null)
        {
            output.WriteLine("Skipped: the game is not installed here (set CANOPY_GAME_FOLDER).");
            return;
        }
        GameInfo info = CanopyCore.Game(folder);
        FileList files = CanopyCore.Files(folder);
        GameAssetCache cache = new(Path.Combine(root, "game-assets"));

        AssetBuild result = cache.Build(new NativeAssetSource(folder, files.Files.Select(file => file.Path).ToList()), info.Build);

        Assert.True(result.Ok, result.Error);
        Assert.Empty(result.Manifest!.Problems);
        Assert.NotEmpty(result.Manifest.Fonts);
        Assert.Equal(Backdrops.Seasons.Count, result.Manifest.Backdrops.Count);
        Assert.True(cache.IsCurrent(info.Build));
        output.WriteLine($"{result.Manifest.Fonts.Count} fonts, {result.Manifest.Backdrops.Count} backdrops");
    }
}
