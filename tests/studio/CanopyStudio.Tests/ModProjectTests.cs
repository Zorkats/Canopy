using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Tests;

public sealed class ModProjectTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-project-" + Guid.NewGuid().ToString("N"));

    public ModProjectTests() => Directory.CreateDirectory(root);

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

    private ModProject Opened(string text)
    {
        string folder = Path.Combine(root, "Apple");
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, ModProject.IniName), text);
        ProjectOpenResult result = ModProject.Open(folder);
        Assert.True(result.Ok, result.Error);
        return result.Project!;
    }

    [Fact]
    public void AFolderWithoutIniOpensEmptyAndUnsaved()
    {
        ProjectOpenResult result = ModProject.Open(root);
        Assert.True(result.Ok, result.Error);
        Assert.Equal("", result.Project!.Text);
        Assert.True(result.Project.IsDirty);
        Assert.Empty(result.Project.SectionNames);
    }

    [Fact]
    public void AMissingFolderDoesNotOpen()
    {
        ProjectOpenResult result = ModProject.Open(Path.Combine(root, "nope"));
        Assert.False(result.Ok);
        Assert.Contains("does not exist", result.Error);
    }

    [Fact]
    public void CreateRefusesAFolderThatIsNotEmpty()
    {
        File.WriteAllText(Path.Combine(root, "note.txt"), "x");
        ProjectOpenResult result = ModProject.Create(root, "[mod]\n");
        Assert.False(result.Ok);
        Assert.Contains("not empty", result.Error);
    }

    [Fact]
    public void CreateWritesTheIniAtOnce()
    {
        string folder = Path.Combine(root, "Pebble");
        ProjectOpenResult result = ModProject.Create(folder, "[mod]\nkey = Pebble\n");
        Assert.True(result.Ok, result.Error);
        Assert.False(result.Project!.IsDirty);
        Assert.Equal("Pebble", result.Project.Name);
        Assert.Equal("[mod]\nkey = Pebble\n", File.ReadAllText(Path.Combine(folder, ModProject.IniName)));
    }

    [Fact]
    public void ValuesAndLinesMatchWithoutCase()
    {
        ModProject project = Opened("; made by hand\n[Mod]\nKey = Apple\n\n[item.Seed]\nname = Seed\n");
        Assert.Equal("Apple", project.Value("mod", "key"));
        Assert.Null(project.Value("mod", "name"));
        Assert.Equal(["Mod", "item.Seed"], project.SectionNames);
        Assert.True(project.HasSection("ITEM.seed"));
        Assert.Equal(2, project.LineOf("mod"));
        Assert.Equal(6, project.LineOf("item.seed", "name"));
        Assert.Equal(0, project.LineOf("item.seed", "help"));
    }

    [Fact]
    public void EditsKeepCommentsAndTellWhoMadeThem()
    {
        ModProject project = Opened("; made by hand\n[mod]\nkey = Apple\n");
        object form = new();
        List<object?> sources = [];
        project.Changed += (_, change) => sources.Add(change.Source);

        Assert.Null(project.Edit("set", "mod", "name", "Apple Mod", form));
        Assert.Equal("Apple Mod", project.Value("mod", "name"));
        Assert.StartsWith("; made by hand\n", project.Text);
        Assert.True(project.IsDirty);

        Assert.Null(project.Edit("remove", "mod", "name"));
        Assert.Null(project.Value("mod", "name"));
        Assert.Equal([form, null], sources);
    }

    [Fact]
    public void ARefusedEditLeavesTheTextAlone()
    {
        ModProject project = Opened("[mod]\nkey = Apple\n");
        string before = project.Text;
        int changes = 0;
        project.Changed += (_, _) => ++changes;

        Assert.NotNull(project.Edit("set", "mod", "key", "two\nlines"));
        Assert.Equal(before, project.Text);
        Assert.Equal(0, changes);
    }

    [Fact]
    public void SetTextReparsesAndSaveClearsDirty()
    {
        ModProject project = Opened("[mod]\n");
        project.SetText("[mod]\nkey = Apple\n[flag.Ready]\n");
        Assert.Equal(["mod", "flag.Ready"], project.SectionNames);
        Assert.True(project.IsDirty);

        Assert.Null(project.Save());
        Assert.False(project.IsDirty);
        Assert.Equal(project.Text, File.ReadAllText(project.IniPath));
        Assert.False(File.Exists(project.IniPath + ".new"));
    }

    [Fact]
    public void TheSchemaListsEveryKindWithFields()
    {
        SchemaInfo schema = CanopyCore.Sections();
        Assert.True(schema.Ok, schema.Error);
        SectionSchema mod = Assert.Single(schema.Sections, kind => kind.Prefix == "mod");
        Assert.True(mod.IsSingle);
        SectionSchema item = Assert.Single(schema.Sections, kind => kind.Prefix == "item.");
        Assert.False(item.IsSingle);
        FieldSchema name = Assert.Single(item.Fields, field => field.Key == "name");
        Assert.Equal(FieldKinds.LangText, name.Kind);
        Assert.True(name.Required);
        Assert.Equal("item.dat", Assert.Single(item.Fields, field => field.Key == "base").Table);
    }

    [Fact]
    public void ValidateChecksAModsFolderWithoutAGame()
    {
        string mods = Path.Combine(root, "mods");
        Directory.CreateDirectory(Path.Combine(mods, "Apple"));
        File.WriteAllText(Path.Combine(mods, "Apple", ModProject.IniName), "[flag.Ready]\ndefault = maybe\n");

        ValidationReport report = CanopyCore.Validate(null, mods);
        Assert.True(report.Ok, report.Error);
        Assert.False(report.Vanilla);
        Assert.Equal(["Apple"], report.Folders);
        Assert.Contains(report.Diagnostics, diagnostic => diagnostic.Folder == "Apple" && diagnostic.Line == 2);
    }
}
