using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Tests;

public sealed class ContentCatalogTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-content-" + Guid.NewGuid().ToString("N"));

    public ContentCatalogTests() => Directory.CreateDirectory(root);

    public void Dispose()
    {
        try
        {
            Directory.Delete(root, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static IReadOnlyList<SectionSchema> Kinds()
    {
        SchemaInfo schema = CanopyCore.Sections();
        Assert.True(schema.Ok, schema.Error);
        return schema.Sections;
    }

    // A project made from text in its own folder under the test root.
    private ModProject Made(string text, string folder = "Apple")
    {
        ProjectOpenResult result = ModProject.Create(Path.Combine(root, folder), text);
        Assert.True(result.Ok, result.Error);
        return result.Project!;
    }

    private static ContentTile TileAt(ModProject project, string section) =>
        ContentCatalog.Tile(project, section, Kinds())!;

    [Fact]
    public void EveryFieldOfEveryKindHasAFriendlyLabel()
    {
        foreach (SectionSchema kind in Kinds())
        {
            foreach (FieldSchema field in kind.Fields)
            {
                string label = FieldLabels.Label(kind.Prefix, field.Key);
                Assert.False(string.IsNullOrWhiteSpace(label), kind.Prefix + field.Key);
                Assert.NotEqual(field.Key, label);
            }
        }
    }

    [Fact]
    public void IgnoredFieldsAreExactlyTheOnesWhoseHelpSaysSo()
    {
        string[] ignoredHelp = ["Ignored.", "Not supported yet.", "Not used.", "Craft only."];
        int ignored = 0;
        foreach (SectionSchema kind in Kinds())
        {
            foreach (FieldSchema field in kind.Fields)
            {
                bool helpSaysIgnored = ignoredHelp.Any(start => field.Help.StartsWith(start, StringComparison.Ordinal));
                bool placedIgnored = FieldLabels.Place(kind.Prefix, field) == FieldPlace.Ignored;
                Assert.Equal(helpSaysIgnored, placedIgnored);
                ignored += placedIgnored ? 1 : 0;
            }
        }
        // The five item fields, the six villager fields, the flag id and the cooking recipe.
        Assert.Equal(13, ignored);
    }

    [Fact]
    public void ARequiredFieldIsAlwaysInTheMainStack()
    {
        FieldSchema key = new("key", FieldKinds.Text, true, "The mod key.", null, null, null, null, []);
        FieldSchema price = new("price", FieldKinds.Integer, true,
            "Ignored. Required here, so it stays in the main stack.", null, null, null, null, []);
        Assert.Equal(FieldPlace.Main, FieldLabels.Place("mod", key));
        Assert.Equal(FieldPlace.Main, FieldLabels.Place("item.", price));

        FieldSchema optional = key with { Required = false };
        Assert.Equal(FieldPlace.More, FieldLabels.Place("mod", optional));
    }

    [Fact]
    public void AnUnknownKeyIsSpelledOutAsWords()
    {
        Assert.Equal("Draw Info x", FieldLabels.Label("item.", "drawInfo.x"));
        Assert.Equal("Some key", FieldLabels.Label("item.", "some_key"));
    }

    [Fact]
    public void TranslationsCountOnlyTheLanguagesWithAValue()
    {
        ModProject project = Made("[item.Seed]\nname = Alpha\nname.fr = Beta\nname.es =\nname.zh-cn = Gamma\n");
        Assert.Equal(2, FieldLabels.Translated(project, "item.Seed", "name"));
        Assert.Equal(0, FieldLabels.Translated(project, "item.Seed", "help"));
        Assert.Equal("Translations (0 of 7)", FieldLabels.TranslationsTitle(0));
        Assert.Equal("Translations (2 of 7)", FieldLabels.TranslationsTitle(2));
    }

    [Fact]
    public void AModOnlyProjectGivesTheFixedGroupsWithTheModTileOnly()
    {
        IReadOnlyList<ContentGroup> groups = ContentCatalog.Groups(Made("[mod]\nkey = Apple\n"), Kinds());
        Assert.Equal(["mod", "items", "objects", "villagers", "whistles", "craft", "cooking", "sales",
            "upgrades", "offerings",
            "catches", "forage", "crops", "flags", "settings", "textures", "icons", "skins", "raw"], groups.Select(group => group.Id));
        Assert.Equal(["mod"], groups[0].Tiles.Select(tile => tile.Section));
        Assert.All(groups.Skip(1).SkipLast(1), group => Assert.Empty(group.Tiles));
        Assert.Empty(groups[^1].Tiles);
        Assert.False(groups[0].CanAdd);
        Assert.True(groups[1].CanAdd);
        Assert.False(groups[0].Advanced);
        Assert.True(groups[^1].Advanced);
        Assert.Equal("row.", groups[^1].Kind?.Prefix);
    }

    [Fact]
    public void AProjectWithoutAModLetsTheModGroupAddOne()
    {
        IReadOnlyList<ContentGroup> groups = ContentCatalog.Groups(Made(""), Kinds());
        Assert.True(groups[0].CanAdd);
        Assert.Empty(groups[0].Tiles);
    }

    [Fact]
    public void SectionsFallIntoTheirGroupsInFileOrder()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[item.Seed]\nname = Alpha\n"
            + "[flag.Ready]\ndefault = on\n"
            + "[texture.Art]\nfile = art.png\n"
            + "[row.Raw]\nbase = 5\n"
            + "[foo]\nx = 1\n"
            + "[patch.item.5]\nprice = 3\n"
            + "[item.Pebble]\nname = Beta\n"
            + "[object.Lamp]\nname = Lamp\nclone = 242000004\n"
            + "[catch.Bass]\nspots = all\nfish = 600020\n");
        IReadOnlyList<ContentGroup> groups = ContentCatalog.Groups(project, Kinds());
        ContentGroup Group(string id) => groups.Single(group => group.Id == id);

        Assert.Equal(["mod", "items", "objects", "villagers", "whistles", "craft", "cooking", "sales",
            "upgrades", "offerings",
            "catches", "forage", "crops", "flags", "settings", "textures", "icons", "skins", "edits", "raw"],
            groups.Select(group => group.Id));
        Assert.Equal(["item.Seed", "item.Pebble"], groups[1].Tiles.Select(tile => tile.Section));
        ContentTile lamp = Assert.Single(groups[2].Tiles);
        Assert.Equal(("Lamp", "Copy of 242000004", "object"), (lamp.Title, lamp.Subtitle, lamp.Art.Glyph));
        Assert.Equal(["flag.Ready"], Group("flags").Tiles.Select(tile => tile.Section));
        Assert.Equal(["texture.Art"], Group("textures").Tiles.Select(tile => tile.Section));
        Assert.Equal(["catch.Bass"], Group("catches").Tiles.Select(tile => tile.Section));
        Assert.Equal("Bass", Group("catches").Tiles[0].Title);
        Assert.Empty(Group("offerings").Tiles);
        Assert.Equal(["patch.item.5"], Group("edits").Tiles.Select(tile => tile.Section));
        Assert.Null(Group("edits").Kind);
        Assert.False(Group("edits").CanAdd);
        Assert.Equal(["row.Raw", "foo"], Group("raw").Tiles.Select(tile => tile.Section));
        Assert.Equal("row.", Group("raw").Tiles[0].Kind?.Prefix);
        Assert.Null(Group("raw").Tiles[1].Kind);
        Assert.Equal("[foo]", Group("raw").Tiles[1].Title);
    }

    [Fact]
    public void SalesAndUpgradesShowTheirItemAndWhereTheyGo()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[item.Pie]\nname = Apple Pie\nbase = 300\n"
            + "[sale.Pie]\nitem = Apple.Pie\nshop = 2\n"
            + "[sale.Rope]\nitem = 220100\n"
            + "[upgrade.Axe]\nfrom = 1000\nto = 1001\nneeds = 1, 2\n"
            + "[upgrade.Odd]\nfrom = 1000\n");
        IReadOnlyList<ContentGroup> groups = ContentCatalog.Groups(project, Kinds());
        ContentGroup Group(string id) => groups.Single(group => group.Id == id);

        Assert.Equal(["sale.Pie", "sale.Rope"], Group("sales").Tiles.Select(tile => tile.Section));
        Assert.Equal(["upgrade.Axe", "upgrade.Odd"], Group("upgrades").Tiles.Select(tile => tile.Section));
        ContentTile pie = TileAt(project, "sale.Pie");
        Assert.Equal(("Apple Pie", "Shop 2", "sale"), (pie.Title, pie.Subtitle, pie.Art.Glyph));
        Assert.Equal((TileArtKind.VanillaItem, (ulong?)300), (pie.Art.Kind, pie.Art.Id));
        ContentTile rope = TileAt(project, "sale.Rope");
        Assert.Equal(("Rope", "No shop"), (rope.Title, rope.Subtitle));
        Assert.Equal((TileArtKind.VanillaItem, (ulong?)220100), (rope.Art.Kind, rope.Art.Id));
        ContentTile axe = TileAt(project, "upgrade.Axe");
        Assert.Equal(("Axe", "Makes 1001", "upgrade"), (axe.Title, axe.Subtitle, axe.Art.Glyph));
        Assert.Equal((TileArtKind.VanillaItem, (ulong?)1001), (axe.Art.Kind, axe.Art.Id));
        ContentTile odd = TileAt(project, "upgrade.Odd");
        Assert.Equal(("No tool", TileArtKind.Glyph), (odd.Subtitle, odd.Art.Kind));
    }

    [Fact]
    public void CropsShowTheirNameBaseAndSeed()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[item.Pip]\nname = Apple Pip\nbase = 300\n"
            + "[crop.Tree]\nbase = 1\nname = Apple Tree\nseed = Pip\n"
            + "[crop.Bare]\n");
        IReadOnlyList<ContentGroup> groups = ContentCatalog.Groups(project, Kinds());

        Assert.Equal(["crop.Tree", "crop.Bare"],
            groups.Single(group => group.Id == "crops").Tiles.Select(tile => tile.Section));
        ContentTile tree = TileAt(project, "crop.Tree");
        Assert.Equal(("Apple Tree", "Copy of crop 1", "crop"), (tree.Title, tree.Subtitle, tree.Art.Glyph));
        Assert.Equal((TileArtKind.VanillaItem, (ulong?)300), (tree.Art.Kind, tree.Art.Id));
        ContentTile bare = TileAt(project, "crop.Bare");
        Assert.Equal(("Bare", "No base", TileArtKind.Glyph), (bare.Title, bare.Subtitle, bare.Art.Kind));
    }

    [Fact]
    public void ItemPicturesFollowTheIconAndTheBaseItem()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[texture.Tex]\nfile = art/tex.png\n"
            + "[icon.Ic]\ntexture = Tex\nrect = 1, 2, 16, 16\n"
            + "[item.Own]\nname = Own\nicon = Ic\n"
            + "[item.Qualified]\nicon = Apple.Ic\n"
            + "[item.Vanilla]\nicon = 123\n"
            + "[item.Base]\nbase = 77\n"
            + "[item.Plain]\nname = Plain\n");
        TileArt modImage = new(TileArtKind.ModImage, "item", Path.Combine(project.Folder, "art", "tex.png"),
            new TileRect(1, 2, 16, 16));

        Assert.Equal(modImage, TileAt(project, "item.Own").Art);
        Assert.Equal(modImage, TileAt(project, "item.Qualified").Art);
        Assert.Equal(new TileArt(TileArtKind.VanillaIcon, "item", Id: 123), TileAt(project, "item.Vanilla").Art);
        Assert.Equal(new TileArt(TileArtKind.VanillaItem, "item", Id: 77), TileAt(project, "item.Base").Art);
        Assert.Equal(new TileArt(TileArtKind.Glyph, "item"), TileAt(project, "item.Plain").Art);
    }

    [Fact]
    public void ACraftRecipeFollowsItsItemIconAndTextureToThePicture()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[texture.Tex]\nfile = tex.png\n"
            + "[icon.Ic]\ntexture = Tex\n"
            + "[item.Own]\nicon = Ic\n"
            + "[craft.Make]\noutput = Own\n");
        Assert.Equal(new TileArt(TileArtKind.ModImage, "craft", Path.Combine(project.Folder, "tex.png")),
            TileAt(project, "craft.Make").Art);
    }

    [Fact]
    public void ASkinShowsItsFirstSwapWithAnImage()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[texture.Tex]\nfile = tex.png\n"
            + "[skin.Missing]\nreplace = Model: Nope\n"
            + "[skin.Own]\nreplace = Model: Nope, Other: Tex\n");
        Assert.Equal(new TileArt(TileArtKind.Glyph, "skin"), TileAt(project, "skin.Missing").Art);
        Assert.Equal(new TileArt(TileArtKind.ModImage, "skin", Path.Combine(project.Folder, "tex.png")),
            TileAt(project, "skin.Own").Art);
    }

    [Fact]
    public void ATextureOutsideTheModFolderGetsAGlyph()
    {
        string outside = Path.Combine(root, "outside.png");
        ModProject project = Made(
            "[texture.Up]\nfile = ..\\outside.png\n"
            + "[texture.Absolute]\nfile = " + outside + "\n"
            + "[texture.Inside]\nfile = ok.png\n");
        Assert.Equal(new TileArt(TileArtKind.Glyph, "texture"), TileAt(project, "texture.Up").Art);
        Assert.Equal(new TileArt(TileArtKind.Glyph, "texture"), TileAt(project, "texture.Absolute").Art);
        Assert.Equal(new TileArt(TileArtKind.ModImage, "texture", Path.Combine(project.Folder, "ok.png")),
            TileAt(project, "texture.Inside").Art);
    }

    [Fact]
    public void TitlesAndSubtitlesFollowTheirRules()
    {
        ModProject project = Made(
            "[mod]\nkey = Apple\n"
            + "[item.Named]\nname = Alpha\n"
            + "[item.Blank]\nname =\nname.fr = Beta\n"
            + "[item.Bare]\nhelp = Gamma\n"
            + "[craft.Make]\noutput = Apple.Named\n"
            + "[cooking.Cook]\noutput = 5\n"
            + "[setting.Speed]\ntype = Slider\nlabel = Speed label\n"
            + "[setting.Mode]\ntype = dial\n"
            + "[skin.Two]\nreplace = a: t1, b: t2\n"
            + "[skin.One]\nreplace = a: t1\n"
            + "[flag.Ready]\ndefault = TRUE\n"
            + "[icon.Ic]\n");
        Assert.Equal("Alpha", TileAt(project, "item.Named").Title);
        Assert.Equal("Beta", TileAt(project, "item.Blank").Title);
        Assert.Equal("Bare", TileAt(project, "item.Bare").Title);
        Assert.Equal("Alpha", TileAt(project, "craft.Make").Title);
        Assert.Equal("Make", TileAt(project, "craft.Make").Subtitle);
        Assert.Equal("Cook", TileAt(project, "cooking.Cook").Title);
        Assert.Equal("Speed label", TileAt(project, "setting.Speed").Title);
        Assert.Equal("Slider", TileAt(project, "setting.Speed").Subtitle);
        Assert.Equal("Mode", TileAt(project, "setting.Mode").Title);
        Assert.Equal("Toggle", TileAt(project, "setting.Mode").Subtitle);
        Assert.Equal("2 swaps", TileAt(project, "skin.Two").Subtitle);
        Assert.Equal("1 swap", TileAt(project, "skin.One").Subtitle);
        Assert.Equal("On in a new game", TileAt(project, "flag.Ready").Subtitle);
        Assert.Equal("No texture", TileAt(project, "icon.Ic").Subtitle);
    }

    [Fact]
    public void ModTitleAndSubtitleFallBack()
    {
        ModProject plain = Made("[mod]\nkey = Plain\n", "Plain");
        Assert.Equal("Plain", TileAt(plain, "mod").Title);
        Assert.Equal("No author", TileAt(plain, "mod").Subtitle);

        ModProject versioned = Made("[mod]\nname = Shiny\nversion = 2.0\n", "Versioned");
        Assert.Equal("Shiny", TileAt(versioned, "mod").Title);
        Assert.Equal("2.0", TileAt(versioned, "mod").Subtitle);
        Assert.Equal("Someone", TileAt(Made("[mod]\nauthor = Someone\nversion = 2.0\n", "Authored"), "mod").Subtitle);
    }

    [Fact]
    public void AMissingReferenceEndsInAGlyphWithoutThrowing()
    {
        ModProject project = Made("[mod]\nkey = Apple\n[icon.A]\ntexture = A\n[item.B]\nicon = Missing\n");
        Assert.Equal(new TileArt(TileArtKind.Glyph, "icon"), TileAt(project, "icon.A").Art);
        Assert.Equal(new TileArt(TileArtKind.Glyph, "item"), TileAt(project, "item.B").Art);
    }

    [Fact]
    public void ATileIsFoundBySectionNameWithoutCase()
    {
        ModProject project = Made("[mod]\nkey = Apple\n[item.Seed]\nname = Alpha\n");
        Assert.Equal("item.Seed", TileAt(project, "ITEM.seed").Section);
        Assert.Null(ContentCatalog.Tile(project, "item.Missing", Kinds()));
    }
}
