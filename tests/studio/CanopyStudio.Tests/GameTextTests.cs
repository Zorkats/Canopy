using System.Diagnostics;
using System.Globalization;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

public sealed class GameTextTests : IDisposable
{
    private static readonly string[] Codes = ["ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"];

    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-text-" + Guid.NewGuid().ToString("N"));

    public GameTextTests() => Directory.CreateDirectory(root);

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, true);
        }
    }

    // item.dat with a per-language name, a vector of per-language entry
    // names, a plain text, a per-language path and a number. The third row
    // has one entry fewer; the second does not fit.
    private static TableGrid ItemGrid()
    {
        List<GridColumn> columns =
        [
            new("id", "id", null, false),
            new("enumName", "enum", null, false),
            new("comment", "text", null, true),
        ];
        columns.AddRange(Codes.Select(code => new GridColumn("name." + code, "text", null, true)));
        columns.AddRange(Codes.Select(code => new GridColumn("icon." + code, "path", null, true)));
        columns.Add(new GridColumn("entries.0.name.ja", "text", null, false));
        columns.Add(new GridColumn("entries.0.name.en", "text", null, false));
        columns.Add(new GridColumn("entries.1.name.ja", "text", null, false));
        columns.Add(new GridColumn("entries.1.name.en", "text", null, false));
        columns.Add(new GridColumn("buyingPrice", "i32", null, true));

        static List<string?> Row(ulong id, string enumName, string[] names, string?[] entries) =>
        [
            id.ToString(CultureInfo.InvariantCulture), enumName, "note",
            .. names, .. Codes.Select(code => "icon_" + code + ".png"), .. entries, "10",
        ];

        string[] apple = ["Ringo", "Apple", "Pomme", "Manzana", "Pingguo", "Sagwa", "Pingguo2"];
        string[] cafe = ["Kohi", "Coffee", "Caf\u00e9", "", "Kafei", "Keopi", "Kafei2"];
        return new TableGrid
        {
            Ok = true,
            File = "item.dat",
            Columns = columns,
            Rows =
            [
                new GridRow(0, 42, "ITEM_APPLE", true, Row(42, "ITEM_APPLE", apple, ["E0ja", "Seed", "E1ja", "Core"])),
                new GridRow(1, 43, "ITEM_BROKEN", false, Enumerable.Repeat<string?>(null, columns.Count).ToList()),
                new GridRow(2, 1044, "ITEM_COFFEE", true, Row(1044, "ITEM_COFFEE", cafe, ["Mame", "Bean", null, null])),
            ],
        };
    }

    private static GameTextIndex Index() => GameTextIndex.Build([ItemGrid()]);

    private static GameTextEntry Entry(GameTextIndex index, ulong id, string field) =>
        index.Entries.Single(entry => entry.Id == id && entry.Field == field);

    private EditsMod Mod(string ini = "[mod]\r\nname = Edits\r\n")
    {
        string folder = Path.Combine(root, "Edits");
        Assert.True(ModProject.Create(folder, ini).Ok);
        (EditsMod? mod, string? error) = EditsMod.Open(folder);
        Assert.Null(error);
        return mod!;
    }

    [Fact]
    public void TheIndexHasOneEntryPerRowAndPerLanguageField()
    {
        GameTextIndex index = Index();

        // Row 42: name, entries.0.name, entries.1.name. Row 1044: name,
        // entries.0.name. Row 43 does not fit; comment and icon are not
        // per-language texts.
        Assert.Equal(5, index.Entries.Count);
        Assert.Equal(["name", "entries.0.name", "entries.1.name"],
            index.Entries.Where(entry => entry.Id == 42).Select(entry => entry.Field));
        Assert.Equal(["name", "entries.0.name"], index.Entries.Where(entry => entry.Id == 1044).Select(entry => entry.Field));
        Assert.DoesNotContain(index.Entries, entry => entry.Id == 43);

        GameTextEntry name = Entry(index, 42, "name");
        Assert.Equal(("item.dat", 0, "ITEM_APPLE"), (name.File, name.Index, name.Enum));
        Assert.Equal(7, name.Values.Count);
        Assert.Equal("Apple", name.Values[1]);
        Assert.Equal("Pingguo2", name.Values[6]);
        Assert.All(name.Editable, Assert.True);
        Assert.Equal("name.zh-cn", name.Path(6));

        GameTextEntry part = Entry(index, 42, "entries.1.name");
        Assert.Equal([true, true, false, false, false, false, false], part.Values.Select(value => value is not null));
        Assert.False(part.Editable[1]);
        Assert.Equal("entries.1.name.en", part.Path(1));

        Assert.Equal(Enumerable.Range(0, 7), index.Languages);
        Assert.Equal([new GameTextTable("item.dat", 5)], index.Tables);
    }

    [Fact]
    public void AGridThatWasNotReadGivesNoTexts() =>
        Assert.Empty(GameTextIndex.FromGrid(new TableGrid { Ok = false, Error = "no schema" }));

    [Theory]
    [InlineData("item.dat", true)]
    [InlineData("framework_item.dat", true)]
    [InlineData("ICON", true)]
    [InlineData("offeringitem.dat", true)]
    [InlineData("string.dat", true)]
    [InlineData("framework_networkdefine.dat", true)]
    [InlineData("talkdefine.dat", false)]
    [InlineData("framework_talkdefine.dat", false)]
    [InlineData("lostbook.dat", false)]
    public void OnlyPatchableTablesAreRead(string file, bool held) => Assert.Equal(held, GameTextIndex.Holds(file));

    [Fact]
    public void SearchIgnoresCaseAndAccentsAndFindsIdsAndEnums()
    {
        GameTextIndex index = Index();
        GameTextQuery query = new(index.Entries);
        int[] english = [1];
        int[] french = [2];

        IReadOnlyList<GameTextEntry> Found(string search, int[] shown, string? file = null, Func<int, bool>? keep = null,
            Func<int, int, string?>? edited = null) =>
            query.Find(search, shown, file, keep, edited).Select(position => query.Entries[position]).ToList();

        Assert.Equal(5, Found("", english).Count);
        Assert.Equal([(42UL, "entries.0.name")], Found("sEEd", english).Select(entry => (entry.Id, entry.Field)));
        // Accents: the French text is "Cafe" with an acute accent.
        Assert.Equal([(1044UL, "name")], Found("CAFE", french).Select(entry => (entry.Id, entry.Field)));
        Assert.Equal([(1044UL, "name")], Found("caf\u00c9", french).Select(entry => (entry.Id, entry.Field)));
        // Only the shown languages are searched.
        Assert.Empty(Found("cafe", english));
        Assert.Single(Found("cafe", [1, 2]));
        // Ids, with or without '#', and enum names, with or without '_'.
        Assert.Equal(2, Found("#1044", english).Count);
        Assert.Equal(2, Found("1044", english).Count);
        Assert.Equal(3, Found("item_apple", english).Count);
        Assert.Equal(3, Found("apple ITEM", english).Count);
        Assert.Single(Found("coffee bean", english));
        // The table filter, the keep test and the edits mod's texts.
        Assert.Empty(Found("", english, "craft.dat"));
        Assert.Equal(5, Found("", english, "ITEM.dat").Count);
        Assert.Equal(2, Found("", english, keep: position => query.Entries[position].Id == 1044).Count);
        Assert.Single(Found("granny", english, edited: (position, language) =>
            query.Entries[position].Field == "name" && query.Entries[position].Id == 42 && language == 1 ? "Granny Smith" : null));
    }

    [Fact]
    public void CommitSetsATextAndAVanillaTextTakesItOut()
    {
        EditsMod mod = Mod();
        GameTextEntry name = Entry(Index(), 42, "name");

        Assert.Null(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), name, 1, "Green apple"));
        Assert.Equal("Green apple", mod.Value("item.dat", 42, "name.en"));
        Assert.Contains("[patch.item.42]", mod.Project.Text, StringComparison.Ordinal);

        GameTextPatches patches = GameTextPatches.Of(mod);
        Assert.True(patches.Changes(name));
        Assert.Equal("Green apple", patches.Shown(name, 1));
        Assert.Equal("Pomme", patches.Shown(name, 2));

        Assert.Null(GameTextEdit.Commit(mod, patches, name, 1, "Apple"));
        Assert.Null(mod.Value("item.dat", 42, "name.en"));
        Assert.DoesNotContain("patch.item", mod.Project.Text, StringComparison.Ordinal);
        Assert.False(GameTextPatches.Of(mod).Changes(name));
    }

    [Theory]
    [InlineData("two\nlines")]
    [InlineData("two\r\nlines")]
    [InlineData(" leading")]
    [InlineData("trailing ")]
    public void ARefusedTextWritesNothing(string text)
    {
        EditsMod mod = Mod();
        string before = mod.Project.Text;
        GameTextEntry name = Entry(Index(), 42, "name");

        Assert.NotNull(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), name, 1, text));

        Assert.Equal(before, mod.Project.Text);
        Assert.False(mod.IsDirty);
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void WithoutAModOrOnAReadOnlyFieldNothingIsWritten()
    {
        GameTextIndex index = Index();
        GameTextEntry name = Entry(index, 42, "name");
        GameTextEntry part = Entry(index, 42, "entries.0.name");
        EditsMod mod = Mod();

        Assert.Equal(GameTextEdit.NoMod, GameTextEdit.Commit(null, GameTextPatches.Empty, name, 1, "Pear"));
        Assert.NotNull(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), part, 1, "Pip"));
        Assert.NotNull(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), part, 3, "Pip"));
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void RevertTakesOutOneLanguageOrEveryOne()
    {
        EditsMod mod = Mod();
        GameTextEntry name = Entry(Index(), 42, "name");
        Assert.Null(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), name, 1, "Pear"));
        Assert.Null(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), name, 2, "Poire"));
        Assert.Null(GameTextEdit.Commit(mod, GameTextPatches.Of(mod), name, 6, "Li"));

        Assert.Null(GameTextEdit.Revert(mod, GameTextPatches.Of(mod), name, 1));
        Assert.Null(mod.Value("item.dat", 42, "name.en"));
        Assert.Equal("Poire", mod.Value("item.dat", 42, "name.fr"));
        // Nothing set there: nothing to do.
        Assert.Null(GameTextEdit.Revert(mod, GameTextPatches.Of(mod), name, 3));

        Assert.Null(GameTextEdit.RevertAll(mod, GameTextPatches.Of(mod), name));
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void OtherFormsOfAPatchAreShownAndLeftToCreate()
    {
        EditsMod mod = Mod("[mod]\r\nname = Edits\r\n" +
                           "[patch.item.42]\r\nname.2 = Poire\r\n" +
                           "[patch.item.dat.1044]\r\nNAME.EN = Tea\r\nentries.0.name = Leaf\r\n" +
                           "[patch.item.ITEM_COFFEE]\r\nbuyingPrice = 5\r\n");
        GameTextIndex index = Index();
        GameTextPatches patches = GameTextPatches.Of(mod);
        GameTextEntry apple = Entry(index, 42, "name");
        GameTextEntry coffee = Entry(index, 1044, "name");
        GameTextEntry bean = Entry(index, 1044, "entries.0.name");

        // name.2 is French, by index.
        Assert.Equal("Poire", patches.Shown(apple, 2));
        Assert.Equal("Apple", patches.Shown(apple, 1));
        Assert.Contains("name.2", GameTextEdit.CannotEdit(mod, patches, apple, 2), StringComparison.Ordinal);
        Assert.Null(GameTextEdit.CannotEdit(mod, patches, apple, 1));

        // A whole field sets every language; the path's case does not count.
        Assert.Equal("Leaf", patches.Shown(bean, 0));
        Assert.Equal("Leaf", patches.Shown(bean, 1));
        Assert.Equal("Tea", patches.Shown(coffee, 1));
        Assert.True(GameTextPatches.IsPlain(patches.Find(coffee, 1)!, coffee, 1));

        // A section naming the row by enum name blocks every text of it.
        Assert.NotNull(patches.EnumSection(coffee));
        Assert.Contains("line 9", GameTextEdit.CannotEdit(mod, patches, coffee, 1), StringComparison.Ordinal);
        string before = mod.Project.Text;
        Assert.NotNull(GameTextEdit.Commit(mod, patches, coffee, 1, "Milk"));
        Assert.Equal(before, mod.Project.Text);
    }
}

// The full index from the installed game, read only. Counts only: never a
// game text.
public sealed class GameTextNativeTests(ITestOutputHelper output)
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
    public void TheWholeIndexBuildsFromTheGame()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        FileList list = CanopyCore.Files(folder);
        Assert.True(list.Ok, list.Error);
        List<string> files = GamePaths.Tables(list.Files).Where(GameTextIndex.Holds).ToList();
        Assert.Contains("item.dat", files);

        Stopwatch clock = Stopwatch.StartNew();
        List<TableGrid> grids = files.AsParallel().AsOrdered().Select(file => CanopyCore.Grid(folder, file)).ToList();
        long read = clock.ElapsedMilliseconds;
        GameTextIndex index = GameTextIndex.Build(grids);
        long built = clock.ElapsedMilliseconds - read;

        Assert.All(grids, grid => Assert.True(grid.Ok, grid.Error));
        Assert.NotEmpty(index.Entries);
        Assert.Equal(7, index.Languages.Count);
        Assert.Contains(index.Tables, table => table.File == "item.dat" && table.Count > 0);
        Assert.All(index.Entries, entry => Assert.Contains(entry.Values, value => value is not null));

        output.WriteLine($"{files.Count} tables read in {read} ms, index built in {built} ms.");
        output.WriteLine($"{index.Entries.Count} entries in {index.Tables.Count} tables, languages {string.Join(" ", index.Languages.Select(language => GameTextIndex.Codes[language]))}.");
        foreach (GameTextTable table in index.Tables)
        {
            IEnumerable<GameTextEntry> of = index.Entries.Where(entry => entry.File == table.File);
            output.WriteLine($"  {table.File}: {table.Count} entries, fields {string.Join(" ", of.Select(entry => entry.Field).Distinct().Take(6))}");
        }
        foreach (TableGrid grid in grids.Where(grid => !index.Tables.Any(table => table.File == grid.File)))
        {
            output.WriteLine($"  {grid.File}: no per-language texts ({grid.Rows.Count} rows)");
        }
        int empty = index.Entries.Sum(entry => entry.Values.Count(value => value is { Length: 0 }));
        // Texts that are not per language: comments, file names, scripts.
        IEnumerable<string> plain = grids.SelectMany(grid => grid.Columns
            .Where(column => column.Type == "text" && GameTextIndex.LanguageOf(column.Path[(column.Path.LastIndexOf('.') + 1)..]) < 0)
            .Select(column => grid.File + ":" + column.Path));
        output.WriteLine($"Empty texts: {empty}. Plain text columns left out: {string.Join(" ", plain)}");
        GameTextQuery query = new(index.Entries);
        clock.Restart();
        int hits = query.Find("a", [1, 0]).Count;
        output.WriteLine($"A one-letter search over two languages: {hits} hits in {clock.ElapsedMilliseconds} ms.");
    }

    [Fact]
    public void TablesTheIndexCannotReach()
    {
        if (!Ready(out string folder))
        {
            return;
        }
        FileList list = CanopyCore.Files(folder);
        Assert.True(list.Ok, list.Error);
        foreach (string file in GamePaths.Tables(list.Files).Where(file => !GameTextIndex.Holds(file)))
        {
            TableInfo table = CanopyCore.Table(folder, file);
            if (table.Ok && table.PoolBytes > 0)
            {
                output.WriteLine($"  {file}: {table.Rows.Count} rows, {table.PoolBytes} bytes of text");
            }
        }
    }
}
