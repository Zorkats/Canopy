using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The reference editors' logic: the keys a project and other mods define,
// how a value resolves, melodies, and the needs and replace lists. Ini text
// is read through canopy_core.dll; without it those tests say so and pass.
public sealed class ReferencesTests : IDisposable
{
    private readonly ITestOutputHelper output;
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-refs-" + Guid.NewGuid().ToString("N"));

    public ReferencesTests(ITestOutputHelper output)
    {
        this.output = output;
        Directory.CreateDirectory(root);
    }

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

    private bool Ready()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        return true;
    }

    private static IniStructure Parsed(string text)
    {
        IniStructure structure = CanopyCore.IniParse(text);
        Assert.True(structure.Ok, structure.Error);
        return structure;
    }

    private static ProjectKeys Keys(string text, string folderName = "Pebble") =>
        ProjectKeys.Read(Parsed(text), folderName);

    private static string[] Values(ProjectKeys keys, RefKind kind) => keys.Of(kind).Select(part => part.Value).ToArray();

    private void WriteMod(string modsFolder, string folder, string text)
    {
        string path = Path.Combine(modsFolder, folder);
        Directory.CreateDirectory(path);
        File.WriteAllText(Path.Combine(path, ModProject.IniName), text);
    }

    // A mods folder: two mods, one turned off in disabled.txt, Canopy's own
    // data folder, the project itself, and a mod with the project's key.
    private string ModsFolder()
    {
        string mods = Path.Combine(root, "mods");
        WriteMod(mods, "Beta", "[item.Nut]\n");
        WriteMod(mods, "Alpha", "[mod]\nkey = Ally\n\n[item.Berry]\n\n[flag.Moon]\n");
        WriteMod(mods, "Off", "[item.Gone]\n");
        WriteMod(mods, "CanopyData", "[item.Data]\n");
        WriteMod(mods, "Project", "[mod]\nkey = Pebble\n\n[item.Seed]\n");
        WriteMod(mods, "Twin", "[mod]\nkey = Pebble\n\n[item.Clone]\n");
        File.WriteAllText(Path.Combine(mods, "CanopyData", "disabled.txt"), "# turned off by hand\n  Off  \r\n");
        return mods;
    }

    [Fact]
    public void OwnKeysComeFromSectionsUnderTheModKey()
    {
        if (!Ready())
        {
            return;
        }
        ProjectKeys keys = Keys("[mod]\nkey = Pebble\n\n[item.Seed]\n\n[item.Bad Key]\n\n[flag.Rain]\n\n"
            + "[icon.Leaf]\n\n[row.icon.Leaf]\n\n[row.icon.Stem]\n\n[row.texture.Bark]\n\n[row.animereplacetexture.Coat]\n\n"
            + "[skin.Fur]\n\n[craft.Bread]\n\n[cooking.Soup]\n\n[object.Lamp]\n", folderName: "Folder");

        Assert.Equal("Pebble", keys.ModKey);
        Assert.False(keys.HasOthers);
        Assert.Equal(["Seed"], Values(keys, RefKind.Item));
        Assert.Equal(["Rain"], Values(keys, RefKind.Flag));
        Assert.Equal(["Leaf", "Stem"], Values(keys, RefKind.Icon));
        Assert.Equal(["Bark"], Values(keys, RefKind.Texture));
        Assert.Equal(["Coat", "Fur"], Values(keys, RefKind.Skin));
        Assert.Equal(["Bread", "Soup"], Values(keys, RefKind.Recipe));
        Assert.Equal(["Lamp"], Values(keys, RefKind.Object));
        Assert.All(keys.Own, part => Assert.True(part.Own));
    }

    [Fact]
    public void TheModKeyIsTheFolderNameWithoutAModSection()
    {
        if (!Ready())
        {
            return;
        }
        ProjectKeys keys = Keys("[item.Seed]\n", folderName: "Apple");

        Assert.Equal("Apple", keys.ModKey);
        Assert.Equal(["Seed"], Values(keys, RefKind.Item));
    }

    [Fact]
    public void AModKeyTheLoaderRefusesDefinesNothing()
    {
        if (!Ready())
        {
            return;
        }
        Assert.Empty(Keys("[mod]\nkey = not a key\n\n[item.Seed]\n").Own);
        Assert.Empty(Keys("[item.Seed]\n", folderName: "Folder With Spaces").Own);
    }

    [Fact]
    public void OtherModsAreReadInLoadOrderAndSkipWhatTheLoaderSkips()
    {
        if (!Ready())
        {
            return;
        }
        string mods = ModsFolder();
        ProjectKeys keys = ProjectKeys.Read(Parsed("[mod]\nkey = Pebble\n\n[item.Seed]\n"), "Project", mods,
            Path.Combine(mods, "Project"));

        Assert.True(keys.HasOthers);
        Assert.Equal(["Seed", "Ally.Berry", "Beta.Nut"], Values(keys, RefKind.Item));
        Assert.Equal(["Ally.Moon"], Values(keys, RefKind.Flag));
        // The mod with the project's key is read, but its keys would be the
        // project's own, so it is never offered.
        Assert.Contains(keys.Others, part => part.Key == "Clone");
        Assert.DoesNotContain(keys.Others, part => part.Key is "Gone" or "Data" or "Seed");
    }

    [Fact]
    public void FindReadsKeysTheWayTheLoaderDoes()
    {
        if (!Ready())
        {
            return;
        }
        string mods = ModsFolder();
        ProjectKeys keys = ProjectKeys.Read(Parsed("[mod]\nkey = Pebble\n\n[item.Seed]\n"), "Project", mods,
            Path.Combine(mods, "Project"));

        Assert.Equal(new ModPart(RefKind.Item, "Pebble", "Seed", true), keys.Find(RefKind.Item, "Seed"));
        Assert.Equal(new ModPart(RefKind.Item, "Pebble", "Seed", true), keys.Find(RefKind.Item, "pebble.SEED"));
        Assert.Equal(new ModPart(RefKind.Item, "Ally", "Berry", false), keys.Find(RefKind.Item, "ally.berry"));
        Assert.Null(keys.Find(RefKind.Flag, "Ally.Berry"));
        Assert.Null(keys.Find(RefKind.Item, "Alpha.Berry"));
        Assert.Null(keys.Find(RefKind.Item, "Off.Gone"));
        Assert.Null(keys.Find(RefKind.Item, "Pebble.Clone"));
        Assert.Equal(RefStatus.Unresolved, References.Resolve(References.Need(false), keys, "Off.Gone").Status);
    }

    [Fact]
    public void FlagsTakeGameIdsBelowTheModBand()
    {
        if (!Ready())
        {
            return;
        }
        RefRules rules = References.For(FieldKinds.FlagRef, "whistle.")!;
        ProjectKeys keys = Keys("[mod]\nkey = Pebble\n\n[flag.Rain]\n");

        Assert.Equal(RefStatus.Own, References.Resolve(rules, keys, "Rain").Status);
        Assert.Equal(new RefResolution(RefStatus.Vanilla, Id: 12), References.Resolve(rules, keys, "12"));
        Assert.Equal(RefStatus.Vanilla, References.Resolve(rules, keys, "94999").Status);
        foreach (string id in (string[])["0", "95000", "99998", "99999", "123456"])
        {
            Assert.Equal(RefStatus.Unresolved, References.Resolve(rules, keys, id).Status);
        }
    }

    [Fact]
    public void ItemsAndIconsFollowTheirRules()
    {
        if (!Ready())
        {
            return;
        }
        ProjectKeys keys = Keys("[mod]\nkey = Pebble\n\n[item.Seed]\n\n[icon.Leaf]\n");
        RefRules item = References.For(FieldKinds.ItemRef, "craft.")!;
        RefRules itemIcon = References.For(FieldKinds.IconRef, "item.")!;
        RefRules whistleIcon = References.For(FieldKinds.IconRef, "whistle.")!;

        Assert.Equal(RefStatus.Vanilla, References.Resolve(item, keys, "1499999999").Status);
        Assert.Equal(RefStatus.Unresolved, References.Resolve(item, keys, "1500000000").Status);
        Assert.Equal(RefStatus.Unchecked, References.Resolve(item, keys, "Other.Thing").Status);
        Assert.Equal(RefStatus.Unresolved, References.Resolve(item, keys, "Pebble.Missing").Status);
        Assert.Equal("Not an item of this mod, another mod or the game.", References.Resolve(item, keys, "Nope").Problem);
        Assert.Equal(RefStatus.Vanilla, References.Resolve(itemIcon, keys, "5").Status);
        Assert.Equal(RefStatus.Own, References.Resolve(whistleIcon, keys, "Leaf").Status);
        RefResolution typed = References.Resolve(whistleIcon, keys, "5");
        Assert.Equal(RefStatus.Unresolved, typed.Status);
        Assert.Equal("Not an icon of this mod or another mod.", typed.Problem);
    }

    [Fact]
    public void TexturesTakeEnumNamesAndCookingTakesGroups()
    {
        if (!Ready())
        {
            return;
        }
        ProjectKeys keys = Keys("[mod]\nkey = Pebble\n\n[texture.Bark]\n");
        RefRules texture = References.For(FieldKinds.TextureRef, "skin.")!;

        Assert.Equal(new RefResolution(RefStatus.Vanilla, Name: "TEX_ID_GRASS_01"),
            References.Resolve(texture, keys, "TEX_ID_GRASS_01"));
        Assert.Equal(RefStatus.Own, References.Resolve(texture, keys, "Bark").Status);
        Assert.Equal(RefStatus.Unresolved, References.Resolve(texture, keys, "lower_case").Status);
        Assert.Equal(new RefResolution(RefStatus.Group, Id: 11), References.Resolve(References.Need(true), keys, "group:11"));
        Assert.Equal(RefStatus.Unresolved, References.Resolve(References.Need(true), keys, "group:0").Status);
        Assert.Equal(RefStatus.Unresolved, References.Resolve(References.Need(true), keys, "group:1001").Status);
        Assert.Equal(RefStatus.Unresolved, References.Resolve(References.Need(false), keys, "group:11").Status);
    }

    [Fact]
    public void RulesExistOnlyForReferenceKinds()
    {
        Assert.Null(References.For(FieldKinds.Text, "item."));
        Assert.Null(References.For(FieldKinds.VanillaId, "item."));
        Assert.Null(References.For(FieldKinds.List, "craft."));
        Assert.False(References.For(FieldKinds.SkinRef, "villager.")!.VanillaIds);
        RefRules placed = References.For(FieldKinds.ObjectRef, "item.")!;
        Assert.Equal(RefKind.Object, placed.Kind);
        Assert.False(placed.VanillaIds);
        Assert.Equal("Not an object of this mod or another mod.", placed.NotFound);
    }

    [Fact]
    public void MelodiesReadAsTheLoaderReadsThem()
    {
        Assert.Equal([false, false, false, true, true, true], Melody.Read("S s 0 L l 1"));
        Assert.Equal([], Melody.Read(""));
        Assert.Null(Melody.Read("S,L"));
        Assert.Null(Melody.Read("S\tL"));
        Assert.Equal("S S L", Melody.Format([false, false, true]));
        Assert.Equal(Melody.ShortSymbol + " " + Melody.LongSymbol, Melody.Symbols([false, true]));
    }

    [Fact]
    public void MelodiesHaveTwoToTenNotes()
    {
        Assert.Null(Melody.Problem("SL"));
        Assert.Null(Melody.Problem("S S S S S L L L L L"));
        Assert.NotNull(Melody.Problem("S"));
        Assert.NotNull(Melody.Problem("SSSSSLLLLLL"));
        Assert.NotNull(Melody.Problem("S X"));
        Assert.NotNull(Melody.Problem(""));
    }

    [Theory]
    [InlineData("Seed")]
    [InlineData("Seed * 2")]
    [InlineData("Seed*2,Other.Nut * 3, 105")]
    [InlineData("group:11 * 2,  1500 *1 ,Seed")]
    [InlineData("a,,b")]
    public void NeedsKeepTheirTextOnARoundTrip(string text) =>
        Assert.Equal(text, ItemList.FormatNeeds(ItemList.ParseNeeds(text)));

    [Fact]
    public void NeedsSplitAsTheLoaderSplitsThem()
    {
        IReadOnlyList<Need> needs = ItemList.ParseNeeds("Seed*2, 105, group:11 * 3");

        Assert.Equal(["Seed", "105", "group:11"], needs.Select(need => need.Item));
        Assert.Equal(["2", null, "3"], needs.Select(need => need.Count));
        Assert.Empty(ItemList.ParseNeeds(""));
        Assert.Empty(ItemList.ParseNeeds(null));
    }

    [Fact]
    public void ChangingOneNeedKeepsTheOthersAsTheyWere()
    {
        List<Need> needs = [.. ItemList.ParseNeeds("Seed*2,  105")];

        needs[1] = needs[1] with { Count = "4", Raw = null };
        Assert.Equal("Seed*2, 105 * 4", ItemList.FormatNeeds(needs));
        needs.Add(new Need("Nut", null));
        Assert.Equal("Seed*2, 105 * 4, Nut", ItemList.FormatNeeds(needs));
        needs.RemoveAt(0);
        Assert.Equal("105 * 4, Nut", ItemList.FormatNeeds(needs));
        Assert.Equal("", ItemList.FormatNeeds([]));
    }

    [Theory]
    [InlineData("body: Bark")]
    [InlineData("body:Pebble.Bark,  hair : TEX_ID_HAIR_01")]
    [InlineData("odd")]
    public void SwapsKeepTheirTextOnARoundTrip(string text) =>
        Assert.Equal(text, ItemList.FormatSwaps(ItemList.ParseSwaps(text)));

    [Fact]
    public void SwapsSplitOnTheFirstColon()
    {
        IReadOnlyList<Swap> swaps = ItemList.ParseSwaps("body:Pebble.Bark, hair : TEX_ID_HAIR_01");

        Assert.Equal(["body", "hair"], swaps.Select(swap => swap.Name));
        Assert.Equal(["Pebble.Bark", "TEX_ID_HAIR_01"], swaps.Select(swap => swap.Texture));
        Assert.Equal("body:Pebble.Bark, hair: Coat",
            ItemList.FormatSwaps([swaps[0], swaps[1] with { Texture = "Coat", Raw = null }]));
    }

    [Fact]
    public void CountsAreOneToNinetyNine()
    {
        Assert.Null(ItemList.CountProblem(null));
        Assert.Null(ItemList.CountProblem("1"));
        Assert.Null(ItemList.CountProblem("99"));
        foreach (string count in (string[])["0", "100", "", "2x", "-1"])
        {
            Assert.NotNull(ItemList.CountProblem(count));
        }
    }

    [Fact]
    public void AValueThatDoesNotResolveStaysAsWritten()
    {
        if (!Ready())
        {
            return;
        }
        const string text = "[mod]\nkey = Pebble\n\n[craft.Bread]\nneeds = Nope * 2, 1500000001\nresult = Pebble.Missing\n";
        string folder = Path.Combine(root, "Pebble");
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, ModProject.IniName), text);
        ProjectOpenResult opened = ModProject.Open(folder);
        Assert.True(opened.Ok, opened.Error);
        ModProject project = opened.Project!;
        ProjectKeys keys = ProjectKeys.Read(project.Structure, project.Name);

        string needs = project.Value("craft.Bread", "needs")!;
        IReadOnlyList<Need> parsed = ItemList.ParseNeeds(needs);
        RefRules item = References.For(FieldKinds.ItemRef, "craft.")!;

        Assert.All(parsed, need => Assert.Equal(RefStatus.Unresolved, References.Resolve(References.Need(false), keys, need.Item).Status));
        Assert.Equal(RefStatus.Unresolved, References.Resolve(item, keys, project.Value("craft.Bread", "result")).Status);
        Assert.Equal(needs, ItemList.FormatNeeds(parsed));
        Assert.Equal(text, project.Text);
        Assert.False(project.IsDirty);
        Assert.Equal(text, File.ReadAllText(Path.Combine(folder, ModProject.IniName)));
    }
}
