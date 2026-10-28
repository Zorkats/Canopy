using System.Diagnostics;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The checks a grid cell runs before it writes, against the rules of
// table_schema::Check and the values tests/core/patch_tests.cpp refuses.
public sealed class FieldValueCheckTests
{
    [Theory]
    [InlineData("i32", "-2147483648", "-2147483648")]
    [InlineData("i32", "2147483647", "2147483647")]
    [InlineData("i32", "-0", "0")]
    [InlineData("i32", "007", "7")]
    [InlineData("flag", "-5", "-5")]
    [InlineData("u32", "4294967295", "4294967295")]
    [InlineData("u32", "0", "0")]
    [InlineData("u64", "18446744073709551615", "18446744073709551615")]
    [InlineData("i64", "-9223372036854775808", "-9223372036854775808")]
    [InlineData("i64", "9223372036854775807", "9223372036854775807")]
    [InlineData("i64", "-007", "-7")]
    [InlineData("ref", "1000", "1000")]
    [InlineData("f32", "1e3", "1000")]
    [InlineData("f32", "-0", "-0")]
    [InlineData("f32", "12.5", "12.5")]
    [InlineData("f32", "-2", "-2")]
    [InlineData("f32", ".5", "0.5")]
    [InlineData("bool", "1", "1")]
    [InlineData("bool", "0", "0")]
    [InlineData("bool", "TRUE", "1")]
    [InlineData("bool", "false", "0")]
    [InlineData("text", "A = B; #1 [x]", "A = B; #1 [x]")]
    [InlineData("text", "", "")]
    [InlineData("path", "ui/icon.png", "ui/icon.png")]
    public void ValidValuesAreWrittenInTheGridsForm(string type, string value, string written)
    {
        FieldValue checkedValue = FieldValueCheck.Check(type, value);

        Assert.True(checkedValue.Ok, checkedValue.Error);
        Assert.Equal(written, checkedValue.Text);
    }

    [Theory]
    [InlineData("i32", "-2147483649")]
    [InlineData("i32", "2147483648")]
    [InlineData("i32", "+5")]
    [InlineData("i32", " 5")]
    [InlineData("i32", "5 ")]
    [InlineData("i32", "1.0")]
    [InlineData("i32", "")]
    [InlineData("i32", "-")]
    [InlineData("u32", "4294967296")]
    [InlineData("u32", "-0")]
    [InlineData("u32", "-1")]
    [InlineData("u64", "18446744073709551616")]
    [InlineData("u64", "000000000000000000001")]
    [InlineData("i64", "9223372036854775808")]
    [InlineData("i64", "-9223372036854775809")]
    [InlineData("i64", "+1")]
    [InlineData("ref", "Other.Tex")]
    [InlineData("ref", "12.5")]
    [InlineData("ref", "-1")]
    [InlineData("f32", "wide")]
    [InlineData("f32", "1,5")]
    [InlineData("f32", "1e39")]
    [InlineData("f32", "-1e39")]
    [InlineData("f32", "1e-50")]
    [InlineData("f32", "1e-40")]
    [InlineData("f32", "nan")]
    [InlineData("f32", "Infinity")]
    [InlineData("f32", "")]
    [InlineData("f32", "1.5 ")]
    [InlineData("f32", "0x10")]
    [InlineData("f32", "123456789012345678901234567890123")]
    [InlineData("bool", "2")]
    [InlineData("bool", "yes")]
    [InlineData("bool", "")]
    [InlineData("text", "two\nlines")]
    [InlineData("text", "two\r\nlines")]
    [InlineData("text", "tab\there")]
    [InlineData("text", " lead")]
    [InlineData("text", "trail ")]
    [InlineData("path", "a\u007fb")]
    [InlineData("id", "5")]
    [InlineData("enum", "ICON_X")]
    [InlineData("bytes", "00")]
    public void ValuesTheLoaderRefusesAreRefused(string type, string value)
    {
        FieldValue checkedValue = FieldValueCheck.Check(type, value);

        Assert.False(checkedValue.Ok);
        Assert.Null(checkedValue.Text);
        Assert.False(string.IsNullOrWhiteSpace(checkedValue.Error));
    }

    [Fact]
    public void TextsAreLimitedTo1024BytesOfUtf8()
    {
        Assert.True(FieldValueCheck.Check("text", new string('a', 1024)).Ok);
        Assert.False(FieldValueCheck.Check("text", new string('a', 1025)).Ok);
        // Three bytes each in UTF-8.
        Assert.True(FieldValueCheck.Check("text", new string('\u3042', 341)).Ok);
        Assert.False(FieldValueCheck.Check("text", new string('\u3042', 342)).Ok);
    }

    [Fact]
    public void AReferenceMustBeAKnownId()
    {
        HashSet<ulong> ids = [0, 10, 20];

        Assert.True(FieldValueCheck.Check("ref", "10", ids, "item.dat").Ok);
        Assert.True(FieldValueCheck.Check("ref", "0", ids, "item.dat").Ok);
        FieldValue missing = FieldValueCheck.Check("ref", "11", ids, "item.dat");
        Assert.False(missing.Ok);
        Assert.Contains("item.dat", missing.Error, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("i32", "-0", "0", true)]
    [InlineData("i32", "007", "7", true)]
    [InlineData("f32", "1e3", "1000", true)]
    [InlineData("f32", "0.10", "0.1", true)]
    [InlineData("f32", "-0", "0", false)]
    [InlineData("bool", "true", "1", true)]
    [InlineData("text", "a", "A", false)]
    [InlineData("i32", "x", "0", false)]
    public void SameComparesWhatTheValuesReadAs(string type, string left, string right, bool same) =>
        Assert.Equal(same, FieldValueCheck.Same(type, left, right));

    [Theory]
    [InlineData("buyingPrice", "buyingPrice", true)]
    [InlineData("BuyingPrice", "buyingPrice", true)]
    [InlineData("name.en", "name.en", true)]
    [InlineData("name.1", "name.en", true)]
    [InlineData("name", "name.en", true)]
    [InlineData("drawInfo.w", "drawInfo.3.w", true)]
    [InlineData("drawInfo.3.w", "drawInfo.3.w", true)]
    [InlineData("drawInfo.2.w", "drawInfo.3.w", false)]
    [InlineData("texts.text", "texts.0.text.en", true)]
    [InlineData("name.ja", "name.en", false)]
    [InlineData("name.0", "name.en", false)]
    [InlineData("drawInfo", "drawInfo.0.w", false)]
    [InlineData("buyingPrice", "sellingPrice", false)]
    [InlineData("drawInfo.w.x", "drawInfo.0.w", false)]
    public void PatchPathsNameTheirPlaces(string path, string column, bool names) =>
        Assert.Equal(names, PatchPaths.Names(path, column));
}

// Cell commits, reverts, the patches a grid shows and its filters, on a
// hand-made grid and an edits mod in a temporary folder.
public sealed class GameTablesTests : IDisposable
{
    private const int Price = 2;
    private const int NameJa = 3;
    private const int NameEn = 4;
    private const int Priority = 5;
    private const int DrawW0 = 6;
    private const int DrawW1 = 7;
    private const int Icon = 8;

    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-tables-" + Guid.NewGuid().ToString("N"));

    public GameTablesTests() => Directory.CreateDirectory(root);

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, true);
        }
    }

    private static TableGrid Grid() => new()
    {
        Ok = true,
        File = "item.dat",
        Columns =
        [
            new GridColumn("id", "id", null, false),
            new GridColumn("enumName", "enum", null, false),
            new GridColumn("buyingPrice", "i32", null, true),
            new GridColumn("name.ja", "text", null, true),
            new GridColumn("name.en", "text", null, true),
            new GridColumn("priority", "i32", null, false),
            new GridColumn("drawInfo.0.w", "f32", null, true),
            new GridColumn("drawInfo.1.w", "f32", null, true),
            new GridColumn("iconId", "ref", "icon.dat", true),
        ],
        Rows =
        [
            new GridRow(0, 10000, "ITEM_APPLE", true, ["10000", "ITEM_APPLE", "50", "ja", "Apple", "3", "1.5", "1.5", "7"]),
            new GridRow(1, 10001, "ITEM_PEAR", true, ["10001", "ITEM_PEAR", "60", "ja", "Pear", "4", "2", "2", "8"]),
            new GridRow(2, 10002, null, false, [null, null, null, null, null, null, null, null, null]),
        ],
    };

    private EditsMod Make(string ini)
    {
        string folder = Path.Combine(root, "Edits");
        Assert.True(ModProject.Create(folder, ini).Ok);
        (EditsMod? mod, string? error) = EditsMod.Open(folder);
        Assert.Null(error);
        return mod!;
    }

    [Fact]
    public void ACommitSetsThePatch()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");
        TableGrid grid = Grid();

        Assert.Null(TableEdits.Commit(mod, grid, 0, Price, "81"));

        Assert.Equal("81", mod.Value("item.dat", 10000, "buyingPrice"));
        Assert.True(mod.IsDirty);
        TablePatches patches = TablePatches.Of(mod, grid);
        Assert.Equal("81", patches.At(0, Price)?.Value);
        Assert.True(patches.Patches(0));
        Assert.False(patches.Patches(1));
        Assert.Equal(1, patches.Count);
    }

    [Fact]
    public void AValueEqualToTheGamesRemovesThePatch()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\nbuyingPrice = 81\r\n");
        TableGrid grid = Grid();

        Assert.Null(TableEdits.Commit(mod, grid, 0, Price, "050"));

        Assert.Null(mod.Value("item.dat", 10000, "buyingPrice"));
        Assert.DoesNotContain("patch.item", mod.Project.Text, StringComparison.Ordinal);
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void ACommitEqualToTheGamesWithNoPatchWritesNothing()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");
        TableGrid grid = Grid();

        Assert.Null(TableEdits.Commit(mod, grid, 0, DrawW0, "1.50"));

        Assert.False(mod.IsDirty);
    }

    [Fact]
    public void AGameTextCanopyIniCannotHoldIsTakenBackUnchanged()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\nname.en = Red\r\n");
        TableGrid plain = Grid();
        TableGrid grid = plain with
        {
            Rows = [new GridRow(0, 10000, "ITEM_APPLE", true, ["10000", "ITEM_APPLE", "50", "ja", " Apple ", "3", "1.5", "1.5", "7"]), .. plain.Rows.Skip(1)],
        };
        Assert.NotNull(FieldValueCheck.Check("text", " Apple ").Error);

        Assert.Null(TableEdits.Commit(mod, grid, 0, NameEn, " Apple "));

        Assert.Null(mod.Value("item.dat", 10000, "name.en"));
        Assert.NotNull(TableEdits.Commit(mod, grid, 0, NameEn, " Pear "));
    }

    [Theory]
    [InlineData(Price, "lots")]
    [InlineData(Price, "2147483648")]
    [InlineData(NameEn, "two\nlines")]
    [InlineData(DrawW0, "1e39")]
    [InlineData(Icon, "9")]
    public void ARefusedValueWritesNothing(int column, string value)
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");
        string before = mod.Project.Text;

        string? error = TableEdits.Commit(mod, Grid(), 0, column, value, new HashSet<ulong> { 7, 8 });

        Assert.NotNull(error);
        Assert.Equal(before, mod.Project.Text);
        Assert.False(mod.IsDirty);
    }

    [Fact]
    public void AReferenceTakesAKnownId()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");
        TableGrid grid = Grid();
        IReadOnlySet<ulong> ids = TableEdits.RefIds(grid, Icon, [100, 101]);

        Assert.Contains(7UL, ids);
        Assert.Null(TableEdits.Commit(mod, grid, 0, Icon, "101", ids));
        Assert.Equal("101", mod.Value("item.dat", 10000, "iconId"));
    }

    [Theory]
    [InlineData(0, Priority)]
    [InlineData(0, 0)]
    [InlineData(2, Price)]
    public void ReadOnlyCellsAndRowsThatDoNotFitAreNotEdited(int row, int column)
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");
        TableGrid grid = Grid();

        Assert.NotNull(TableEdits.Problem(grid, row, column));
        Assert.NotNull(TableEdits.Commit(mod, grid, row, column, "1"));
        Assert.False(mod.IsDirty);
    }

    [Fact]
    public void ARowNamedByEnumNameShowsPatchedAndKeepsItsSection()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.ITEM_APPLE]\r\nbuyingPrice = 70\r\n");
        TableGrid grid = Grid();

        TablePatches patches = TablePatches.Of(mod, grid);
        Assert.Equal("70", patches.At(0, Price)?.Value);
        Assert.Equal("patch.item.ITEM_APPLE", patches.SectionOf(0));

        Assert.Null(TableEdits.Commit(mod, grid, 0, NameEn, "Green apple"));
        Assert.Null(TableEdits.Commit(mod, grid, 0, Price, "75"));

        Assert.DoesNotContain("[patch.item.10000]", mod.Project.Text, StringComparison.Ordinal);
        Assert.Equal("Green apple", mod.Project.Value("patch.item.ITEM_APPLE", "name.en"));
        Assert.Equal("75", mod.Project.Value("patch.item.ITEM_APPLE", "buyingPrice"));

        Assert.Null(TableEdits.Revert(mod, grid, 0, Price));
        Assert.Null(TableEdits.Revert(mod, grid, 0, NameEn));
        Assert.DoesNotContain("patch.item", mod.Project.Text, StringComparison.Ordinal);
    }

    [Fact]
    public void ALineForEveryElementIsSpreadBeforeOneCellChanges()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\ndrawInfo.w = 4\r\n");
        TableGrid grid = Grid();

        TablePatches patches = TablePatches.Of(mod, grid);
        Assert.Equal("4", patches.At(0, DrawW0)?.Value);
        Assert.Equal("4", patches.At(0, DrawW1)?.Value);

        Assert.Null(TableEdits.Revert(mod, grid, 0, DrawW0));

        patches = TablePatches.Of(mod, grid);
        Assert.Null(patches.At(0, DrawW0));
        Assert.Equal("4", patches.At(0, DrawW1)?.Value);
        Assert.Null(mod.Project.Value("patch.item.10000", "drawInfo.w"));
        Assert.Equal("4", mod.Project.Value("patch.item.10000", "drawInfo.1.w"));
    }

    [Fact]
    public void ALanguageByIndexKeepsItsKey()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\nname.1 = Red\r\n");
        TableGrid grid = Grid();

        Assert.Equal("Red", TablePatches.Of(mod, grid).At(0, NameEn)?.Value);
        Assert.Null(TableEdits.Commit(mod, grid, 0, NameEn, "Crimson"));

        Assert.Equal("Crimson", mod.Project.Value("patch.item.10000", "name.1"));
        Assert.Null(mod.Project.Value("patch.item.10000", "name.en"));
    }

    [Fact]
    public void ValuesTheGridCannotShowAreListedWithTheirLines()
    {
        EditsMod mod = Make(string.Join("\r\n",
            "[mod]",                        // 1
            "name = Edits",                 // 2
            "[patch.item.99]",              // 3
            "buyingPrice = 1",              // 4  no such row
            "[patch.item.10001]",           // 5
            "nosuch = 1",                   // 6  no such place
            "priority = 9",                 // 7  read only
            "buyingPrice = 61",             // 8  shown
            "buyingPrice = 62",             // 9  the same place again
            "[patch.item.ITEM_PEAR]",       // 10 the row again, by name
            "name.en = Other",              // 11
            "[patch.icon.10001]",           // 12 another table
            "x = 1",                        // 13
            ""));
        TableGrid grid = Grid();

        TablePatches patches = TablePatches.Of(mod, grid);

        Assert.Equal("61", patches.At(1, Price)?.Value);
        Assert.Null(patches.At(1, NameEn));
        Assert.Equal([4, 6, 7, 9, 11], patches.Unplaced.Select(note => note.Patch.Line));
        Assert.All(patches.Unplaced, note => Assert.False(string.IsNullOrWhiteSpace(note.Reason)));
    }

    [Fact]
    public void RevertingARowRemovesItsSections()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\nbuyingPrice = 1\r\nname.en = A\r\n[patch.item.10001]\r\nbuyingPrice = 2\r\n");
        TableGrid grid = Grid();

        Assert.Null(TableEdits.RevertRow(mod, grid, 0));

        TablePatches patches = TablePatches.Of(mod, grid);
        Assert.False(patches.Patches(0));
        Assert.True(patches.Patches(1));
        Assert.DoesNotContain("patch.item.10000", mod.Project.Text, StringComparison.Ordinal);
    }

    [Fact]
    public void TheTextFilterLooksAtIdsEnumNamesAndEveryValue()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10001]\r\nname.en = Quince\r\n");
        TableGrid grid = Grid();
        TablePatches patches = TablePatches.Of(mod, grid);

        Assert.Equal([0, 1, 2], TableFilter.Rows(grid, patches, "", false));
        Assert.Equal([1], TableFilter.Rows(grid, patches, "10001", false));
        Assert.Equal([0], TableFilter.Rows(grid, patches, "item_apple", false));
        Assert.Equal([0], TableFilter.Rows(grid, patches, "APPLE", false));
        Assert.Equal([1], TableFilter.Rows(grid, patches, "quince", false));
        Assert.Equal([1], TableFilter.Rows(grid, patches, " pear ", false));
        Assert.Empty(TableFilter.Rows(grid, patches, "nothing like it", false));
    }

    [Fact]
    public void ChangedOnlyKeepsTheRowsTheModPatches()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10001]\r\nbuyingPrice = 1\r\n");
        TableGrid grid = Grid();
        TablePatches patches = TablePatches.Of(mod, grid);

        Assert.Equal([1], TableFilter.Rows(grid, patches, "", true));
        Assert.Empty(TableFilter.Rows(grid, patches, "apple", true));
        Assert.Empty(TableFilter.Rows(grid, TablePatches.None, "", true));
    }

    [Fact]
    public void TheColumnFilterMatchesPaths()
    {
        IReadOnlyList<GridColumn> columns = Grid().Columns;

        Assert.All(TableFilter.Columns(columns, " "), Assert.True);
        Assert.Equal([6, 7], Shown(TableFilter.Columns(columns, "drawinfo")));
        Assert.Equal([3, 4], Shown(TableFilter.Columns(columns, "name.")));
        Assert.Equal([2, 8], Shown(TableFilter.Columns(columns, "price, icon")));
    }

    private static List<int> Shown(IReadOnlyList<bool> shown) =>
        Enumerable.Range(0, shown.Count).Where(index => shown[index]).ToList();
}

// The grids of the installed game, read only. Without canopy_core or the
// game each test says so and passes. Counts only, never the game's texts.
public sealed class GameTablesNativeTests(ITestOutputHelper output)
{
    private const string DefaultGameFolder = @"D:\SteamLibrary\steamapps\common\Village in the Shade";

    private bool Ready(out string folder)
    {
        folder = "";
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        string found = Environment.GetEnvironmentVariable("CANOPY_GAME_DIR") is { Length: > 0 } dir ? dir : DefaultGameFolder;
        if (!Directory.Exists(found))
        {
            output.WriteLine("Skipped: the game is not installed here (set CANOPY_GAME_DIR).");
            return false;
        }
        folder = found;
        return true;
    }

    [Fact]
    public void EveryPatchableTableLoadsAsAGridWhoseValuesCheck()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        FileList list = CanopyCore.Files(folder);
        Assert.True(list.Ok, list.Error);
        int grids = 0;
        foreach (string file in GamePaths.Tables(list.Files))
        {
            if (!CanopyCore.Table(folder, file).Schema)
            {
                continue;
            }
            Stopwatch watch = Stopwatch.StartNew();
            TableGrid grid = CanopyCore.Grid(folder, file);
            long read = watch.ElapsedMilliseconds;
            Assert.True(grid.Ok, $"{file}: {grid.Error}");
            grids++;
            watch.Restart();
            TablePatches.Of(null, grid);
            TableFilter.Rows(grid, TablePatches.None, "zzzz", false);
            output.WriteLine($"{file}: {grid.Rows.Count} rows, {grid.Columns.Count} columns, " +
                $"{grid.Columns.Count(column => column.Editable)} editable, {grid.Rows.Count(row => !row.Fits)} do not fit; " +
                $"read {read} ms, a filter pass {watch.ElapsedMilliseconds} ms");

            // Every number the game holds in an editable column passes the
            // check and reads as itself, so an untouched cell never writes.
            // Some game texts hold what canopy.ini cannot (end spaces), so
            // texts are only counted; a commit takes those back unchecked.
            int unwritable = 0;
            for (int column = 0; column < grid.Columns.Count; column++)
            {
                GridColumn place = grid.Columns[column];
                if (!place.Editable)
                {
                    continue;
                }
                foreach (GridRow row in grid.Rows.Where(row => row.Fits))
                {
                    if (row.Values[column] is not { } value)
                    {
                        continue;
                    }
                    FieldValue checkedValue = FieldValueCheck.Check(place.Type, value);
                    if (!checkedValue.Ok && place.Type is "text" or "path")
                    {
                        unwritable++;
                        continue;
                    }
                    Assert.True(checkedValue.Ok, $"{file} row {row.Index} {place.Path}: {checkedValue.Error}");
                    Assert.True(FieldValueCheck.Same(place.Type, value, checkedValue.Text), $"{file} row {row.Index} {place.Path}");
                }
            }
            output.WriteLine($"{file}: {unwritable} game texts canopy.ini cannot hold");
        }
        Assert.True(grids >= 17, $"{grids} grids");

        TableGrid items = CanopyCore.Grid(folder, "item.dat");
        Assert.Contains(items.Columns, column => column is { Path: "buyingPrice", Editable: true });
        Assert.Contains(items.Columns, column => column is { Path: "id", Editable: false });
        Assert.DoesNotContain(items.Columns, column => column.Path == "id" && column.Editable);
    }
}
