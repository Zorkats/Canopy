using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Tests;

public sealed class CoreJsonTests
{
    [Fact]
    public void GameJsonParses()
    {
        GameInfo info = CanopyCore.Parse<GameInfo>("""
            {"abi":1,"ok":true,"error":null,"folder":"D:\\Game","exe":true,"build":1790787597,"supported":true,
             "mods":false,"canopy":true,"log":false,"archives":[{"name":"data.dat","version":3,"entries":42}],
             "files":42,"tables":7}
            """);

        Assert.True(info.Ok);
        Assert.Equal(@"D:\Game", info.Folder);
        Assert.Equal(0x6ABD400Du, info.Build);
        Assert.Equal(new ArchiveInfo("data.dat", 3, 42), Assert.Single(info.Archives));
        Assert.Equal(7, info.Tables);
    }

    [Fact]
    public void TableJsonReadsTheStructKey()
    {
        TableInfo info = CanopyCore.Parse<TableInfo>("""
            {"abi":1,"ok":true,"error":null,"file":"texture.dat","struct":"TextureData","schema":true,"poolBytes":12,
             "rows":[{"index":0,"id":18446744073709551615,"enum":null,"size":8}]}
            """);

        Assert.Equal("TextureData", info.StructName);
        Assert.Equal(new TableRow(0, ulong.MaxValue, null, 8), Assert.Single(info.Rows));
    }

    [Fact]
    public void RowJsonParses()
    {
        RowInfo info = CanopyCore.Parse<RowInfo>("""
            {"abi":1,"ok":true,"error":null,"file":"skin.dat","index":1,"id":5,"enum":"A","size":4,
             "fields":[{"path":"entries.1.texture","type":"ref","value":"900","table":"texture.dat"}],
             "texts":[{"offset":8,"text":"x"}],"hex":"00ff"}
            """);

        Assert.Equal(new RowField("entries.1.texture", "ref", "900", "texture.dat"), Assert.Single(info.Fields));
        Assert.Equal(new RowText(8, "x"), Assert.Single(info.Texts));
        Assert.Equal("00ff", info.Hex);
    }

    [Fact]
    public void RowJsonReadsEditable()
    {
        RowInfo info = CanopyCore.Parse<RowInfo>("""
            {"abi":1,"ok":true,"error":null,"file":"item.dat","index":0,"id":5,"enum":null,"size":4,
             "fields":[{"path":"buyingPrice","type":"u32","value":"80","table":null,"editable":true}],
             "texts":[],"hex":"00ff"}
            """);

        Assert.True(Assert.Single(info.Fields).Editable);
    }

    [Fact]
    public void GridJsonParses()
    {
        TableGrid grid = CanopyCore.Parse<TableGrid>("""
            {"abi":1,"ok":true,"error":null,"file":"item.dat","struct":"ItemData",
             "columns":[{"path":"id","type":"id","table":null,"editable":false},
                        {"path":"categoryID","type":"ref","table":"itemcategory.dat","editable":true}],
             "rows":[{"index":0,"id":5,"enum":"ITEM_A","fits":true,"values":["5","20"]},
                     {"index":1,"id":6,"enum":null,"fits":false,"values":[null,null]}]}
            """);

        Assert.True(grid.Ok);
        Assert.Equal("ItemData", grid.StructName);
        Assert.Equal(new GridColumn("categoryID", "ref", "itemcategory.dat", true), grid.Columns[1]);
        Assert.Equal(["5", "20"], grid.Rows[0].Values);
        Assert.False(grid.Rows[1].Fits);
        Assert.All(grid.Rows[1].Values, Assert.Null);
    }

    [Fact]
    public void FailureJsonKeepsTheError()
    {
        RowInfo info = CanopyCore.Parse<RowInfo>("""{"abi":1,"ok":false,"error":"no row 9"}""");

        Assert.False(info.Ok);
        Assert.Equal("no row 9", info.Error);
        Assert.Empty(info.Fields);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("null")]
    public void UnreadableJsonIsAFailure(string json)
    {
        FileList list = CanopyCore.Parse<FileList>(json);

        Assert.False(list.Ok);
        Assert.NotNull(list.Error);
        Assert.Empty(list.Files);
    }
}

public sealed class GamePathsTests
{
    [Theory]
    [InlineData("data/database/item.dat", true)]
    [InlineData("data/database/sub/item.dat", false)]
    [InlineData("data/database/.dat", false)]
    [InlineData("data/database/item.lub", false)]
    [InlineData("data/item.dat", false)]
    public void IsTableMeansADatRightInTheDatabaseFolder(string path, bool table)
    {
        Assert.Equal(table, GamePaths.IsTable(path));
    }

    [Fact]
    public void NamesAndFolders()
    {
        Assert.Equal("a.nltx", GamePaths.FileName("x/y/a.nltx"));
        Assert.Equal("top.bin", GamePaths.FileName("top.bin"));
        Assert.Equal("x/y/", GamePaths.FolderOf("x/y/a.nltx"));
        Assert.Equal("", GamePaths.FolderOf("top.bin"));
        Assert.True(GamePaths.IsTexture("x/A.NLTX"));
    }

    [Fact]
    public void TablesAreSortedFileNames()
    {
        GameFile[] files =
        [
            new("data/database/b.dat", 1, 0, "a", 1),
            new("data/database/a.dat", 1, 0, "a", 1),
            new("data/other/c.dat", 1, 0, "a", 1),
        ];

        Assert.Equal(["a.dat", "b.dat"], GamePaths.Tables(files));
    }
}

public sealed class FileTreeTests
{
    [Fact]
    public void BuildsSortedFoldersWithTotals()
    {
        GameFile[] files =
        [
            new("z.bin", 1, 0, "a", 1),
            new("data/b/two.nltx", 1, 0, "a", 1),
            new("data/a/one.nltx", 1, 0, "a", 1),
            new("data/a/deep/three.nltx", 1, 0, "a", 1),
        ];

        FolderNode top = FileTree.Build(files, "Game files");

        Assert.Equal("Game files", top.Name);
        Assert.Equal(4, top.TotalFiles);
        Assert.Equal("z.bin", Assert.Single(top.Files).Path);
        FolderNode data = Assert.Single(top.Children);
        Assert.Equal("data/", data.Path);
        Assert.Equal(["a", "b"], data.Children.Select(child => child.Name));
        FolderNode a = data.Children[0];
        Assert.Equal(2, a.TotalFiles);
        Assert.Equal("data/a/deep/", Assert.Single(a.Children).Path);
    }
}

public sealed class HexDumpTests
{
    [Fact]
    public void SplitsLinesOfEightWithOffsets()
    {
        string hex = string.Concat(Enumerable.Range(0, 10).Select(value => value.ToString("x2")));

        Assert.Equal(
            "0000  00 01 02 03 04 05 06 07\n0008  08 09",
            HexDump.Format(hex));
    }

    [Fact]
    public void EmptyRowIsEmpty()
    {
        Assert.Equal("", HexDump.Format(""));
    }
}
