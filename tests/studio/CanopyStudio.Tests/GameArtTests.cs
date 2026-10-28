using System.Buffers.Binary;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The Game area's art: texture keys, the ini edits of a replacement, the PNG
// check and the texture list. The game test reads the installed game only;
// every mod it makes is in a temporary folder.
public sealed class GameArtTests(ITestOutputHelper output) : IDisposable
{
    private const string DefaultGame = @"D:\SteamLibrary\steamapps\common\Village in the Shade";

    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-art-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, true);
        }
    }

    private EditsMod Make(string folderName, string ini)
    {
        string folder = Path.Combine(root, folderName);
        Assert.True(ModProject.Create(folder, ini).Ok);
        (EditsMod? mod, string? error) = EditsMod.Open(folder);
        Assert.Null(error);
        return mod!;
    }

    // ---- Keys --------------------------------------------------------------

    [Theory]
    [InlineData("TEX_ID_UI_TITLE", 5, "UI_TITLE")]
    [InlineData("tex_id_lower", 5, "lower")]
    [InlineData("Plain-Name_2", 5, "Plain-Name_2")]
    [InlineData("TEX_ID_A.B C/D", 5, "A_B_C_D")]
    [InlineData("TEX_ID_ÉTÉ", 5, "T")]
    [InlineData(null, 1076, "tex1076")]
    [InlineData("", 7, "tex7")]
    [InlineData("TEX_ID_", 8, "tex8")]
    [InlineData("TEX_ID_...", 9, "tex9")]
    public void KeysComeFromTheEnumName(string? enumName, ulong id, string key)
    {
        Assert.Equal(key, ArtKeys.FromRow(enumName, id));
        Assert.True(ArtKeys.Valid(key));
    }

    [Fact]
    public void LongEnumNamesAreCutTo32()
    {
        string key = ArtKeys.FromRow("TEX_ID_" + new string('A', 20) + "_" + new string('B', 20), 1);

        Assert.Equal(32, key.Length);
        Assert.Equal(new string('A', 20) + "_" + new string('B', 11), key);
        Assert.True(ArtKeys.Valid(key));
        Assert.False(ArtKeys.Valid(key + "C"));
        Assert.False(ArtKeys.Valid(""));
        Assert.False(ArtKeys.Valid("a b"));
    }

    [Fact]
    public void KeysTakenInTheModGetANumber()
    {
        string x = new('X', 32);
        IReadOnlySet<string> taken = ArtKeys.Taken(
            ["mod", "texture.Lamp", "row.texture.LAMP_2", "icon.Lamp_3", "patch.texture.5", "texture.", "texture." + x]);

        Assert.Equal(["Lamp", "LAMP_2", x], taken.Order(StringComparer.OrdinalIgnoreCase));
        // Without case, and icon keys do not count.
        Assert.Equal("lamp_3", ArtKeys.Unique("lamp", taken.Contains));
        Assert.Equal("Other", ArtKeys.Unique("Other", taken.Contains));
        // A cut key that collides keeps 32 characters with its number.
        string unique = ArtKeys.Unique(x, taken.Contains);
        Assert.Equal(new string('X', 30) + "_2", unique);
        Assert.True(ArtKeys.Valid(unique));
    }

    [Fact]
    public void TwoLongEnumsThatCutAlikeGetTwoKeys()
    {
        string common = "TEX_ID_" + new string('Q', 40);
        HashSet<string> keys = new(StringComparer.OrdinalIgnoreCase);
        foreach (string name in (string[])[common + "_ONE", common + "_TWO"])
        {
            keys.Add(ArtKeys.Unique(ArtKeys.FromRow(name, 1), keys.Contains));
        }

        Assert.Equal(2, keys.Count);
        Assert.All(keys, key => Assert.True(ArtKeys.Valid(key)));
    }

    [Fact]
    public void GamePathsAreLowercase()
    {
        Assert.Equal("data/texture/canopy/myedits/ui_title.nltx", ArtKeys.GamePath("MyEdits", "UI_Title"));
        Assert.Equal("art/UI_Title.png", ArtKeys.ArtFile("UI_Title"));
    }

    [Theory]
    [InlineData("TEX_ID_UI", 1, "TEX_ID_UI.png")]
    [InlineData(null, 42, "42.png")]
    [InlineData("A:B/C", 1, "A_B_C.png")]
    [InlineData("...", 3, "3.png")]
    public void ExportNamesAreFileNames(string? enumName, ulong id, string name) =>
        Assert.Equal(name, ArtKeys.ExportName(enumName, id));

    [Fact]
    public void TheModKeyIsTheKeyInModOrTheFolder()
    {
        Assert.Equal("Edits", ArtKeys.ModKey(Make("Edits", "[mod]\r\nname = Edits\r\n").Project));
        Assert.Equal("Mine", ArtKeys.ModKey(Make("Other", "[mod]\r\nname = Edits\r\nkey = Mine\r\n").Project));
    }

    // ---- Replacing ---------------------------------------------------------

    [Fact]
    public void AReplacementWritesTheTextureAndThePatch()
    {
        EditsMod mod = Make("MyEdits", "[mod]\r\nname = My Edits\r\n");

        Assert.Null(ArtReplace.Apply(mod, 1076, "UI_Title"));

        string path = "data/texture/canopy/myedits/ui_title.nltx";
        Assert.Equal("art/UI_Title.png", mod.Project.Value("texture.UI_Title", "file"));
        foreach (string code in LangKeys.Codes)
        {
            Assert.Equal(path, mod.Value("texture.dat", 1076, "fileName." + code));
        }
        // The game reads the size from the file: width and height stay 0.
        Assert.Null(mod.Value("texture.dat", 1076, "width"));
        Assert.Null(mod.Value("texture.dat", 1076, "height"));
        Assert.Equal(7, mod.Patches.Count);
        Assert.Contains("[texture.UI_Title]", mod.Project.Text, StringComparison.Ordinal);
        Assert.Contains("[patch.texture.1076]", mod.Project.Text, StringComparison.Ordinal);

        Assert.Null(mod.Save());
        (EditsMod? again, _) = EditsMod.Open(mod.Folder);
        ArtReplacement found = Assert.Single(ArtReplace.Read(again!));
        Assert.Equal(new ArtReplacement("1076", 1076, "UI_Title", "art/UI_Title.png"), found);
    }

    [Fact]
    public void ReplacingAgainUpdatesTheSameSections()
    {
        EditsMod mod = Make("MyEdits", "[mod]\r\nname = My Edits\r\n[Texture.UI_Title]\r\nfile = old.png\r\n");

        Assert.Null(ArtReplace.Apply(mod, 1076, "UI_Title"));
        Assert.Null(ArtReplace.Apply(mod, 1076, "UI_Title"));

        Assert.Single(mod.Project.SectionNames, name => name.StartsWith("texture.", StringComparison.OrdinalIgnoreCase));
        Assert.Equal("art/UI_Title.png", mod.Project.Value("texture.UI_Title", "file"));
        Assert.Equal(7, mod.Patches.Count);
    }

    [Fact]
    public void RevertTakesOutTheReplacementAndKeepsOtherPatches()
    {
        EditsMod mod = Make("MyEdits", "[mod]\r\nname = My Edits\r\n");
        Assert.Null(mod.Set("texture.dat", 1076, "isNoCompress", "1"));
        Assert.Null(ArtReplace.Apply(mod, 1076, "UI_Title"));
        Assert.Null(ArtReplace.Apply(mod, 2000, "Other"));

        Assert.Null(ArtReplace.Revert(mod, 1076, "UI_Title"));

        Assert.False(mod.Project.HasSection("texture.UI_Title"));
        Assert.Equal("1", mod.Value("texture.dat", 1076, "isNoCompress"));
        Assert.Null(mod.Value("texture.dat", 1076, "fileName.en"));
        Assert.Null(mod.Value("texture.dat", 1076, "width"));
        Assert.Equal(["2000"], ArtReplace.Read(mod).Select(found => found.Row));

        Assert.Null(ArtReplace.Revert(mod, 2000, "Other"));
        Assert.False(mod.Project.HasSection("patch.texture.2000"));
        Assert.Empty(ArtReplace.Read(mod));
        // Reverting what is not there does nothing.
        Assert.Null(ArtReplace.Revert(mod, 2000, "Other"));
    }

    [Fact]
    public void AModKeyTheLoaderRefusesStopsAReplacement()
    {
        EditsMod mod = Make("My Edits", "[mod]\r\nname = My Edits\r\n");

        Assert.NotNull(ArtReplace.ModKeyProblem(mod));
        Assert.NotNull(ArtReplace.Apply(mod, 1076, "UI_Title"));
        Assert.Empty(mod.Patches);
        Assert.NotNull(ArtReplace.Apply(Make("Fine", "[mod]\r\n"), 1, "bad key"));
    }

    [Fact]
    public void ReadingFindsOnlyPatchesThatPointAtTheModsTextures()
    {
        EditsMod mod = Make("MyEdits", """
            [mod]
            key = Edits

            [texture.Sky]
            file = art/Sky.png

            [patch.texture.10]
            fileName = data/texture/canopy/edits/sky.nltx

            [patch.texture.TEX_ID_SEA]
            fileName.en = DATA/TEXTURE/CANOPY/EDITS/SKY.NLTX

            [patch.texture.11]
            fileName.en = data/texture/canopy/othermod/sky.nltx

            [patch.texture.12]
            isNoCompress = 1

            [patch.icon.13]
            drawInfo.0.x = 1
            """);

        IReadOnlyList<ArtReplacement> found = ArtReplace.Read(mod);

        Assert.Equal(2, found.Count);
        Assert.Equal(new ArtReplacement("10", 10, "Sky", "art/Sky.png"), found[0]);
        Assert.Equal(("TEX_ID_SEA", (ulong?)null), (found[1].Row, found[1].Id));
    }

    // ---- The PNG check -----------------------------------------------------

    private static byte[] Header(uint width, uint height, byte depth = 8, byte color = 6)
    {
        byte[] png = PngWriter.Encode(new TextureImage(1, 1, new byte[4]));
        BinaryPrimitives.WriteUInt32BigEndian(png.AsSpan(16), width);
        BinaryPrimitives.WriteUInt32BigEndian(png.AsSpan(20), height);
        png[24] = depth;
        png[25] = color;
        return png;
    }

    [Fact]
    public void APngCanopyReadsPasses()
    {
        string file = Path.Combine(root, "ok.png");
        Directory.CreateDirectory(root);
        File.WriteAllBytes(file, PngWriter.Encode(new TextureImage(3, 2, new byte[24])));

        ArtPngInfo info = ArtPng.Read(file, "ok.png");

        Assert.True(info.Ok, info.Error);
        Assert.Equal((3, 2), (info.Width, info.Height));
        Assert.True(ArtPng.Check(Header(8192, 8192), "big.png").Ok);
        Assert.True(ArtPng.Check(Header(4, 4, 16, 2), "deep.png").Ok);
    }

    [Fact]
    public void WhatIsNotAPngIsRefused()
    {
        string file = Path.Combine(root, "fake.png");
        Directory.CreateDirectory(root);
        File.WriteAllText(file, "BM this is a bitmap");

        ArtPngInfo info = ArtPng.Read(file, "fake.png");

        Assert.False(info.Ok);
        Assert.Contains("not a PNG", info.Error, StringComparison.Ordinal);
        Assert.False(ArtPng.Check(Header(4, 4).AsSpan(0, 20), "cut.png").Ok);
        Assert.False(ArtPng.Read(Path.Combine(root, "missing.png"), "missing.png").Ok);
    }

    [Theory]
    [InlineData(8193u, 16u)]
    [InlineData(16u, 9000u)]
    public void APngLargerThanCanopyTakesIsRefused(uint width, uint height)
    {
        ArtPngInfo info = ArtPng.Check(Header(width, height), "huge.png");

        Assert.False(info.Ok);
        Assert.Contains("8192x8192", info.Error, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(0u, 4u, 8, 6)]
    [InlineData(4u, 4u, 4, 6)]
    [InlineData(4u, 4u, 8, 5)]
    [InlineData(4u, 4u, 16, 3)]
    public void APngHeaderCanopyCannotReadIsRefused(uint width, uint height, byte depth, byte color) =>
        Assert.False(ArtPng.Check(Header(width, height, depth, color), "odd.png").Ok);

    // ---- The list ----------------------------------------------------------

    [Fact]
    public void ShrinkingKeepsTheShapeAndIgnoresClearPixels()
    {
        byte[] pixels = new byte[4 * 2 * 4];
        // Left half opaque red, right half clear black.
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                int at = (y * 4 + x) * 4;
                pixels[at + 2] = 255;
                pixels[at + 3] = 255;
            }
        }
        TextureImage image = new(4, 2, pixels);

        TextureImage small = ArtImages.Shrink(image, 2);

        Assert.Equal((2, 1), (small.Width, small.Height));
        Assert.Equal([0, 0, 255, 255, 0, 0, 0, 0], small.Pixels);
        Assert.Same(image, ArtImages.Shrink(image, 8));
        TextureImage tall = ArtImages.Shrink(new TextureImage(10, 1000, new byte[10 * 1000 * 4]), 50);
        Assert.Equal((1, 50), (tall.Width, tall.Height));
    }

    [Fact]
    public void TheListReadsTheGridColumns()
    {
        TableGrid grid = CanopyCore.Parse<TableGrid>("""
            {"abi":1,"ok":true,"error":null,"file":"texture.dat","struct":"TextureData",
             "columns":[{"path":"id","type":"id","table":null,"editable":false},
                        {"path":"fileName.ja","type":"path","table":null,"editable":true},
                        {"path":"fileName.en","type":"path","table":null,"editable":true},
                        {"path":"fileName.fr","type":"path","table":null,"editable":true},
                        {"path":"width","type":"u32","table":null,"editable":true},
                        {"path":"height","type":"u32","table":null,"editable":true}],
             "rows":[{"index":0,"id":5,"enum":"TEX_ID_A","fits":true,"values":["5","x/ja.nltx","x/a.nltx","x/a.nltx","0","0"]},
                     {"index":1,"id":6,"enum":null,"fits":true,"values":["6","x/b.nltx","","x/b.nltx","8","4"]},
                     {"index":2,"id":7,"enum":null,"fits":false,"values":[null,null,null,null,null,null]}]}
            """);

        IReadOnlyList<ArtTexture> list = ArtTextures.FromGrid(grid);

        Assert.Equal(3, list.Count);
        Assert.Equal("x/a.nltx", list[0].File);
        Assert.Equal([("ja", "x/ja.nltx")], list[0].OtherLanguages());
        Assert.Equal("x/b.nltx", list[1].File);
        Assert.Empty(list[1].OtherLanguages());
        Assert.Equal((8, 4), (list[1].Width, list[1].Height));
        Assert.Equal("", list[2].File);
        Assert.True(list[0].Matches("tex_id_a"));
        Assert.True(list[0].Matches("A.NLTX"));
        Assert.True(list[1].Matches("6"));
        Assert.True(list[1].Matches("  "));
        Assert.False(list[1].Matches("zzz"));
    }

    // ---- The installed game, read only ---------------------------------------

    [Fact]
    public void FirstAndLastTexturesDecodeAtTheirSize()
    {
        string game = Environment.GetEnvironmentVariable("CANOPY_GAME_DIR") is { Length: > 0 } set ? set : DefaultGame;
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Note: skipped, {problem}");
            return;
        }
        if (!Directory.Exists(game))
        {
            output.WriteLine($"Note: skipped, the game is not at {game} (set CANOPY_GAME_DIR).");
            return;
        }
        TableGrid grid = CanopyCore.Grid(game, ArtReplace.TableFile);
        Assert.True(grid.Ok, grid.Error);
        IReadOnlyList<ArtTexture> list = ArtTextures.FromGrid(grid);
        output.WriteLine($"texture.dat: {list.Count} rows, {list.Count(row => row.File.Length == 0)} name no file, " +
            $"{list.Count(row => row.Width > 0)} record a size, " +
            $"{list.Count(row => row.OtherLanguages().Count > 0)} have a file of their own in some language.");

        // Some rows name no file; the first and last that do.
        List<(ArtTexture Row, TextureImage Image)> decoded = [];
        foreach (ArtTexture row in (ArtTexture[])[list.First(row => row.File.Length > 0), list.Last(row => row.File.Length > 0)])
        {
            TextureImage? image = CanopyCore.Texture(game, row.File, out string? error);
            Assert.True(image is not null, error);
            Assert.NotNull(image);
            output.WriteLine($"Row {row.Index} (id {row.Id}): decoded {image.Width}x{image.Height}, row records {row.Width}x{row.Height}.");
            // Vanilla rows record 0 and leave the size to the file; where a row
            // records one, it is the file's.
            if (row.Width > 0 || row.Height > 0)
            {
                Assert.Equal((row.Width, row.Height), (image.Width, image.Height));
            }
            Assert.Equal(image.Width * image.Height * 4, image.Pixels.Length);
            decoded.Add((row, image));
        }

        // One replacement of the smaller texture, in a temporary mods
        // folder, checked the way Canopy reads it at startup.
        (ArtTexture small, TextureImage picture) = decoded.MinBy(entry => entry.Image.Width * entry.Image.Height);
        string mods = Environment.GetEnvironmentVariable("CANOPY_ART_SAMPLE") is { Length: > 0 } keep
            ? keep
            : Path.Combine(root, "mods");
        Directory.CreateDirectory(mods);
        Assert.True(ModProject.Create(Path.Combine(mods, "ArtEdits"), "[mod]\r\nname = Art Edits\r\n").Ok);
        (EditsMod? mod, _) = EditsMod.Open(Path.Combine(mods, "ArtEdits"));
        string key = ArtKeys.Unique(ArtKeys.FromRow(small.Enum, small.Id), ArtKeys.Taken(mod!.Project.SectionNames).Contains);
        TextureImage shrunk = ArtImages.Shrink(picture, 16);
        Directory.CreateDirectory(Path.Combine(mod.Folder, "art"));
        File.WriteAllBytes(Path.Combine(mod.Folder, ArtKeys.ArtFile(key)), PngWriter.Encode(shrunk));
        Assert.Null(ArtReplace.Apply(mod, small.Id, key));
        Assert.Null(mod.Save());

        ValidationReport report = CanopyCore.Validate(game, mods);
        Assert.True(report.Ok, report.Error);
        output.WriteLine($"Validate: {report.Folders.Count} folders, {report.Errors} errors, {report.Warnings} warnings, {report.Notes} notes.");
        foreach (ModDiagnostic diagnostic in report.Diagnostics)
        {
            output.WriteLine($"{diagnostic.Severity} {diagnostic.Folder}:{diagnostic.Line} {diagnostic.Message}");
        }
        Assert.Equal(0, report.Errors);
        Assert.Equal(0, report.Warnings);
    }
}
