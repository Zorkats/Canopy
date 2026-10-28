using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;

namespace CanopyStudio.Tests;

public sealed class EditsModTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-edits-" + Guid.NewGuid().ToString("N"));

    public EditsModTests() => Directory.CreateDirectory(root);

    public void Dispose()
    {
        if (Directory.Exists(root))
        {
            Directory.Delete(root, true);
        }
    }

    private EditsMod Make(string ini)
    {
        string folder = Path.Combine(root, "Edits");
        Assert.True(ModProject.Create(folder, ini).Ok);
        (EditsMod? mod, string? error) = EditsMod.Open(folder);
        Assert.Null(error);
        return mod!;
    }

    [Theory]
    [InlineData("item.dat", "item")]
    [InlineData("ITEM", "item")]
    [InlineData("framework_icon.dat", "icon")]
    public void TableNamesDropDatAndTheTwinPrefix(string file, string name) =>
        Assert.Equal(name, EditsMod.TableName(file));

    [Fact]
    public void SetAddsTheSectionAndSaveWritesIt()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");

        Assert.Null(mod.Set("item.dat", 10000, "buyingPrice", "81"));
        Assert.Null(mod.Set("framework_icon.dat", 1000, "drawInfo.0.offsetX", "1.5"));

        Assert.Equal("81", mod.Value("item", 10000, "buyingPrice"));
        Assert.Equal("1.5", mod.Value("icon.dat", 1000, "drawInfo.0.offsetX"));
        Assert.True(mod.IsDirty);
        Assert.Null(mod.Save());
        string text = File.ReadAllText(Path.Combine(mod.Folder, "canopy.ini"));
        Assert.Contains("[patch.item.10000]", text, StringComparison.Ordinal);
        Assert.Contains("[patch.icon.1000]", text, StringComparison.Ordinal);
        Assert.Contains("\r\n", text, StringComparison.Ordinal);
    }

    [Fact]
    public void AnExistingSectionIsKeptWithItsComments()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n\r\n; prices\r\n[PATCH.Item.dat.10000]\r\nsellingPrice = 9\r\n");

        Assert.Null(mod.Set("item.dat", 10000, "buyingPrice", "81"));

        Assert.Contains("; prices", mod.Project.Text, StringComparison.Ordinal);
        Assert.DoesNotContain("[patch.item.10000]", mod.Project.Text, StringComparison.Ordinal);
        Assert.Equal("9", mod.Value("item.dat", 10000, "sellingPrice"));
        Assert.Equal(2, mod.Patches.Count);
        Assert.All(mod.Patches, patch => Assert.Equal(("item", 10000UL), (patch.Table, patch.Id!.Value)));
    }

    [Fact]
    public void RemovingTheLastValueRemovesTheSection()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.item.10000]\r\nbuyingPrice = 81\r\nsellingPrice = 9\r\n");

        Assert.Null(mod.Remove("item.dat", 10000, "buyingPrice"));
        Assert.Contains("[patch.item.10000]", mod.Project.Text, StringComparison.Ordinal);
        Assert.Null(mod.Remove("item.dat", 10000, "sellingPrice"));
        Assert.DoesNotContain("patch.item", mod.Project.Text, StringComparison.Ordinal);
        Assert.Null(mod.Remove("item.dat", 10000, "sellingPrice"));
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void ARowNamedByEnumNameIsListedButNotMatchedById()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n[patch.icon.ICON_SAMPLE]\r\ndrawInfo.0.x = 4\r\n");

        PatchValue patch = Assert.Single(mod.Patches);
        Assert.Equal("ICON_SAMPLE", patch.Row);
        Assert.Null(patch.Id);
        Assert.Equal(4, patch.Line);
        Assert.Null(mod.Value("icon.dat", 1000, "drawInfo.0.x"));
    }

    [Theory]
    [InlineData("two\nlines")]
    [InlineData(" lead")]
    [InlineData("trail ")]
    public void ValuesCanopyIniCannotHoldAreRefused(string value)
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");

        Assert.NotNull(mod.Set("item.dat", 1, "name.en", value));
        Assert.Empty(mod.Patches);
    }

    [Fact]
    public void TextWithIniCharactersIsKept()
    {
        EditsMod mod = Make("[mod]\r\nname = Edits\r\n");

        Assert.Null(mod.Set("item.dat", 1, "name.en", "A = B; #1 [x]"));

        Assert.Equal("A = B; #1 [x]", mod.Value("item.dat", 1, "name.en"));
    }
}
