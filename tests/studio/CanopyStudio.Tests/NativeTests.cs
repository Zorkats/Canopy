using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// canopy_core.dll on the installed game. Without the DLL or the game each
// test says so in its output and passes: they check the wiring, and the
// C++ tests cover the reading itself. They assert shapes and counts only,
// never the game's own texts.
public sealed class NativeTests(ITestOutputHelper output)
{
    // CANOPY_GAME_FOLDER, else the first folder Steam lists.
    private static readonly Lazy<string?> GameFolder = new(() =>
        Environment.GetEnvironmentVariable("CANOPY_GAME_FOLDER") is { Length: > 0 } folder
            ? folder
            : GameLocator.Candidates().FirstOrDefault());

    private bool Ready(out string folder)
    {
        folder = "";
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        if (GameFolder.Value is not { } found)
        {
            output.WriteLine("Skipped: the game is not installed here (set CANOPY_GAME_FOLDER).");
            return false;
        }
        folder = found;
        return true;
    }

    [Fact]
    public void DllSpeaksTheExpectedLayout()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return;
        }
        Assert.Null(CanopyCore.Check());
    }

    [Fact]
    public void GameOpens()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        GameInfo info = CanopyCore.Game(folder);

        Assert.True(info.Ok, info.Error);
        Assert.Equal(CanopyCore.ExpectedAbi, info.Abi);
        Assert.True(info.Exe);
        Assert.NotEmpty(info.Archives);
        Assert.True(info.Files > 0);
        Assert.True(info.Tables > 0);
    }

    [Fact]
    public void FileListAgreesWithTheGameCounts()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        GameInfo info = CanopyCore.Game(folder);
        FileList list = CanopyCore.Files(folder);

        Assert.True(list.Ok, list.Error);
        Assert.Equal(info.Files, list.Files.Count);
        Assert.Equal(info.Tables, GamePaths.Tables(list.Files).Count);
    }

    [Fact]
    public void CuratedTableAndRowRead()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        TableInfo table = CanopyCore.Table(folder, "texture.dat");
        Assert.True(table.Ok, table.Error);
        Assert.True(table.Schema);
        Assert.NotEmpty(table.Rows);

        RowInfo row = CanopyCore.Row(folder, "texture.dat", 0);
        Assert.True(row.Ok, row.Error);
        Assert.NotEmpty(row.Fields);
        Assert.Equal(row.Size * 2, row.Hex.Length);
    }

    [Fact]
    public void GridOfAPatchOnlyTable()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        TableInfo table = CanopyCore.Table(folder, "item.dat");
        TableGrid grid = CanopyCore.Grid(folder, "item.dat");
        Assert.True(grid.Ok, grid.Error);
        Assert.Equal(table.Rows.Count, grid.Rows.Count);
        Assert.All(grid.Rows, row => Assert.True(row.Fits));
        Assert.All(grid.Rows, row => Assert.Equal(grid.Columns.Count, row.Values.Count));
        Assert.Contains(grid.Columns, column => column is { Path: "buyingPrice", Editable: true });
        Assert.Contains(grid.Columns, column => column is { Path: "id", Editable: false });

        TableGrid none = CanopyCore.Grid(folder, "no_such_table");
        Assert.False(none.Ok);
    }

    [Fact]
    public void BadRequestsFailWithAReason()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        TableInfo missing = CanopyCore.Table(folder, "no_such_table");
        Assert.False(missing.Ok);
        Assert.False(string.IsNullOrEmpty(missing.Error));

        RowInfo past = CanopyCore.Row(folder, "texture.dat", int.MaxValue);
        Assert.False(past.Ok);
        Assert.False(string.IsNullOrEmpty(past.Error));
    }

    [Fact]
    public void TextureDecodes()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        string path = CanopyCore.Files(folder).Files.First(file => GamePaths.IsTexture(file.Path)).Path;

        TextureImage? image = CanopyCore.Texture(folder, path, out string? error);

        Assert.True(image is not null, error);
        Assert.True(image.Width > 0 && image.Height > 0);
        Assert.Equal(image.Width * image.Height * 4, image.Pixels.Length);
    }

    [Fact]
    public void FileReadsAndMissingFileFails()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        GameFile file = CanopyCore.Files(folder).Files.First(entry => entry.Size > 0);

        byte[]? bytes = CanopyCore.File(folder, file.Path, out string? error);
        byte[]? missing = CanopyCore.File(folder, "data/no_such_file.bin", out string? missingError);

        Assert.True(bytes is not null, error);
        Assert.NotEmpty(bytes);
        Assert.Null(missing);
        Assert.False(string.IsNullOrEmpty(missingError));
    }

    [Fact]
    public void NamesListEveryItemAndItsIconDecodes()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        NameList items = CanopyCore.Names(folder, "item", "en");
        NameList area = CanopyCore.Names(folder, "area", "en");
        NameList badLanguage = CanopyCore.Names(folder, "item", "xx");

        Assert.True(items.Ok, items.Error);
        Assert.Equal(("item", "en"), (items.Kind, items.Language));
        Assert.NotEmpty(items.Entries);
        Assert.Equal(items.Entries.Count, items.Entries.Select(entry => entry.Id).Distinct().Count());
        Assert.False(area.Ok);
        Assert.False(badLanguage.Ok);
        output.WriteLine($"{items.Entries.Count} items, {items.Entries.Count(entry => entry.Icon is not null)} with an icon");

        uint icon = (uint)items.Entries.First(entry => entry.Icon is not null).Icon!.Value;
        TextureImage? cell = CanopyCore.Icon(folder, icon, out string? error);
        TextureImage? none = CanopyCore.Icon(folder, uint.MaxValue, out string? noneError);

        Assert.True(cell is not null, error);
        Assert.Equal(cell.Width * cell.Height * 4, cell.Pixels.Length);
        Assert.Null(none);
        Assert.False(string.IsNullOrEmpty(noneError));

        string folderOut = Path.Combine(Path.GetTempPath(), "CanopyStudioIcons-" + Guid.NewGuid().ToString("N"));
        try
        {
            uint[] ids = [.. items.Entries.Where(entry => entry.Icon is not null).Take(30).Select(entry => (uint)entry.Icon!.Value)];
            IconBatch batch = CanopyCore.Icons(folder, [.. ids, uint.MaxValue], folderOut);

            Assert.True(batch.Ok, batch.Error);
            Assert.Equal(ids.Distinct().Count(), batch.Written);
            Assert.Single(batch.Problems);
            Assert.True(System.IO.File.Exists(Path.Combine(folderOut, icon + ".png")));
        }
        finally
        {
            if (Directory.Exists(folderOut))
            {
                Directory.Delete(folderOut, recursive: true);
            }
        }
    }

    [Fact]
    public void BgmIsOggAndAWaveSoundFailsWithAReason()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        List<GameFile> sounds = CanopyCore.Files(folder).Files
            .Where(file => file.Path.EndsWith(".nlsd", StringComparison.OrdinalIgnoreCase)).ToList();
        GameFile bgm = sounds.First(file => file.Path.StartsWith("data/bgm/", StringComparison.OrdinalIgnoreCase));

        byte[]? ogg = CanopyCore.Sound(folder, bgm.Path, out string? error);
        int refused = sounds.Where(file => file.Path.StartsWith("data/se/", StringComparison.OrdinalIgnoreCase)).Take(40)
            .Count(file => CanopyCore.Sound(folder, file.Path, out string? why) is null && !string.IsNullOrEmpty(why));

        Assert.True(ogg is not null, error);
        Assert.Equal("OggS"u8.ToArray(), ogg.Take(4).ToArray());
        Assert.True(refused > 0);
        output.WriteLine($"{sounds.Count} .nlsd files, {refused} of the first 40 sound effects refused");
    }

    [Fact]
    public void IniEditKeepsEveryOtherByte()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return;
        }
        const string text = "; my mod\r\n[mod]\r\nname = Old\r\n\r\n[flag.A]\r\n";

        IniStructure parsed = CanopyCore.IniParse(text);
        IniEditResult changed = CanopyCore.IniEdit(text, "set", "mod", "name", "New");
        IniEditResult missing = CanopyCore.IniEdit(text, "remove", "mod", "version");

        Assert.True(parsed.Ok, parsed.Error);
        Assert.Equal("\r\n", parsed.Eol);
        Assert.Equal(["comment", "section", "value", "blank", "section"], parsed.Lines.Select(line => line.Kind));
        Assert.Equal(("mod", "name"), (parsed.Lines[2].Section, parsed.Lines[2].Key));
        Assert.True(changed.Ok, changed.Error);
        Assert.Equal(text.Replace("Old", "New"), changed.Text);
        Assert.False(missing.Ok);
        Assert.Equal(text, missing.Text);
    }
}
