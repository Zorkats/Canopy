using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Tests;

public sealed class CreateTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-create-" + Guid.NewGuid().ToString("N"));

    public CreateTests() => Directory.CreateDirectory(root);

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

    [Fact]
    public void EveryTemplateValidatesWithoutErrors()
    {
        // One mods folder per template, so no two templates meet.
        foreach (ProjectTemplate template in ProjectTemplates.All)
        {
            string mods = Path.Combine(root, "mods-" + template.Id);
            Directory.CreateDirectory(mods);
            ProjectOpenResult made = ProjectTemplates.Create(mods, "Example", template);
            Assert.True(made.Ok, made.Error);
            Assert.Null(made.Error);
            Assert.False(made.Project!.IsDirty);
            Assert.StartsWith("; ", made.Project.Text);
            Assert.Contains(CanopyDocs.Url(template.Doc), made.Project.Text);
            Assert.Equal("Example", made.Project.Value("mod", "name"));
            foreach (string file in template.Files.Keys)
            {
                Assert.True(File.Exists(Path.Combine(made.Project.Folder, file)), file);
            }

            ValidationReport report = CanopyCore.Validate(null, mods);
            Assert.True(report.Ok, report.Error);
            Assert.False(report.Vanilla);
            Assert.Equal(["Example"], report.Folders);
            Assert.True(report.Errors == 0,
                $"{template.Id}: {string.Join(" | ", report.Diagnostics.Select(diagnostic => diagnostic.Line + " " + diagnostic.Message))}");
            Assert.True(report.Warnings == 0, $"{template.Id}: {report.Warnings} warnings");
        }
    }

    [Fact]
    public void TemplatesUseEverySectionKindTheyName()
    {
        IReadOnlyList<SectionSchema> kinds = Kinds();
        foreach (ProjectTemplate template in ProjectTemplates.All)
        {
            IniStructure structure = CanopyCore.IniParse(template.Ini("Example"));
            Assert.True(structure.Ok, structure.Error);
            foreach (IniLine line in structure.Lines.Where(line => line.Kind == "section"))
            {
                Assert.NotNull(SectionNames.KindOf(line.Section, kinds));
            }
            int sections = structure.Lines.Count(line => line.Kind == "section");
            Assert.True(template.Id == "empty" ? sections == 1 : sections >= 2, template.Id);
        }
    }

    [Fact]
    public void ATemplateRefusesABadNameAndAFolderInUse()
    {
        ProjectOpenResult spaced = ProjectTemplates.Create(root, "My Mod", ProjectTemplates.Empty);
        Assert.False(spaced.Ok);
        Assert.Contains("1 to 32", spaced.Error);

        Directory.CreateDirectory(Path.Combine(root, "Taken"));
        File.WriteAllText(Path.Combine(root, "Taken", "note.txt"), "x");
        ProjectOpenResult taken = ProjectTemplates.Create(root, "Taken", ProjectTemplates.Empty);
        Assert.False(taken.Ok);
        Assert.Contains("not empty", taken.Error);
    }

    [Theory]
    [InlineData("Apple", true)]
    [InlineData("a_b-9", true)]
    [InlineData("12345678901234567890123456789012", true)]
    [InlineData("123456789012345678901234567890123", false)]
    [InlineData("", false)]
    [InlineData("two words", false)]
    [InlineData("dot.ted", false)]
    [InlineData("caf\u00e9", false)]
    public void KeysFollowTheLoadersRule(string key, bool valid) => Assert.Equal(valid, SectionNames.KeyValid(key));

    [Fact]
    public void SectionKindsMatchTheWayTheLoaderDoes()
    {
        IReadOnlyList<SectionSchema> kinds = Kinds();
        Assert.Equal("mod", SectionNames.KindOf("MOD", kinds)?.Prefix);
        Assert.Equal("item.", SectionNames.KindOf("Item.Seed", kinds)?.Prefix);
        Assert.Equal("setting.", SectionNames.KindOf("setting.Speed", kinds)?.Prefix);
        Assert.Null(SectionNames.KindOf("flag.", kinds));
        Assert.Null(SectionNames.KindOf("mods", kinds));
        Assert.Null(SectionNames.KindOf("notes", kinds));

        SectionSchema item = SectionNames.KindOf("item.Seed", kinds)!;
        Assert.Equal("Seed", SectionNames.KeyOf("item.Seed", item));
        Assert.Equal("item.Pebble", SectionNames.Name(item, "Pebble"));
        SectionSchema mod = SectionNames.KindOf("mod", kinds)!;
        Assert.Equal("", SectionNames.KeyOf("mod", mod));
        Assert.Equal("mod", SectionNames.Name(mod, "ignored"));
    }

    [Fact]
    public void AddingAndRenamingFollowTheKeyRules()
    {
        IReadOnlyList<SectionSchema> kinds = Kinds();
        SectionSchema mod = kinds.First(kind => kind.Prefix == "mod");
        SectionSchema flag = kinds.First(kind => kind.Prefix == "flag.");
        string[] sections = ["Mod", "flag.Ready", "notes"];

        Assert.NotNull(SectionNames.AddProblem(sections, mod, ""));
        Assert.Null(SectionNames.AddProblem(["flag.Ready"], mod, ""));
        Assert.NotNull(SectionNames.AddProblem(sections, flag, "READY"));
        Assert.NotNull(SectionNames.AddProblem(sections, flag, "not ok"));
        Assert.Null(SectionNames.AddProblem(sections, flag, "Done"));

        Assert.Null(SectionNames.RenameProblem(sections, "flag.Ready", flag, "ready"));
        Assert.NotNull(SectionNames.RenameProblem(["flag.Ready", "flag.Done"], "flag.Ready", flag, "done"));
        Assert.NotNull(SectionNames.RenameProblem(sections, "flag.Ready", flag, ""));
        Assert.NotNull(SectionNames.RenameProblem(sections, "Mod", mod, "x"));
    }

    [Fact]
    public void SectionsGroupInSchemaOrderWithOtherLast()
    {
        IReadOnlyList<SectionSchema> kinds = Kinds();
        string[] sections = ["notes", "flag.B", "item.A", "mod", "flag.A", "flag."];
        IReadOnlyList<SectionGroup> groups = SectionNames.Group(sections, kinds);

        Assert.Equal(["Mod", "Item", "Flag", SectionNames.OtherTitle], groups.Select(group => group.Title));
        Assert.Equal(["flag.B", "flag.A"], groups[2].Sections);
        Assert.Null(groups[3].Kind);
        Assert.Equal(["notes", "flag."], groups[3].Sections);
        Assert.Empty(SectionNames.Group([], kinds));
    }

    [Fact]
    public void LanguageKeysFollowTheGamesOrder()
    {
        Assert.Equal(VanillaKinds.Languages.Select(language => language.Code), LangKeys.Codes);
        Assert.Equal(7, LangKeys.Codes.Count);
        Assert.Equal(["name.ja", "name.en", "name.fr", "name.es", "name.zh-tw", "name.ko", "name.zh-cn"],
            LangKeys.Keys("name"));
        Assert.Equal("name", LangKeys.BaseOf("name.zh-tw"));
        Assert.Equal("drawInfo.x", LangKeys.BaseOf("drawInfo.x"));
        Assert.Equal("English", LangKeys.LanguageName("en"));
    }

    [Fact]
    public void ALanguageKeyIsReadByTheRules()
    {
        // An item with only name.en and the base name: the rules take both
        // without a message, so the keys the editor writes are the ones the
        // loader reads.
        string mods = Path.Combine(root, "mods");
        string folder = Path.Combine(mods, "Lang");
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, ModProject.IniName),
            "[item.Stone]\nbase = 230070\nname = Stone\nname.en = Stone EN\nname.zh-cn = Stone CN\n");
        ValidationReport report = CanopyCore.Validate(null, mods);
        Assert.True(report.Ok, report.Error);
        Assert.Equal(0, report.Errors + report.Warnings);
    }

    [Fact]
    public void SettingsShowOnlyTheKeysTheirTypeUses()
    {
        SectionSchema setting = Kinds().First(kind => kind.Prefix == "setting.");
        Assert.True(SettingFields.IsSetting(setting));

        Assert.Equal(["type", "label", "help", "default"], SettingFields.Visible(setting, null).Select(field => field.Key));
        Assert.Equal(["type", "label", "help", "default", "min", "max", "step"],
            SettingFields.Visible(setting, "Slider").Select(field => field.Key));
        Assert.Equal(["type", "label", "help", "default", "choices"],
            SettingFields.Visible(setting, "choice").Select(field => field.Key));
        Assert.Equal(setting.Fields.Count, SettingFields.Visible(setting, "dial").Count);

        // Every key a type names is a field of the schema.
        foreach (string type in new[] { "toggle", "slider", "choice" })
        {
            Assert.All(SettingFields.Keys(type)!, key => Assert.Contains(setting.Fields, field => field.Key == key));
        }
    }

    [Fact]
    public void SwitchesAreWrittenTheWayTheirDefaultIsSpelled()
    {
        Assert.Equal(("true", "false"), SwitchText.Words("false"));
        Assert.Equal(("true", "false"), SwitchText.Words("TRUE"));
        Assert.Equal(("on", "off"), SwitchText.Words("off"));
        Assert.Equal(("on", "off"), SwitchText.Words(null));

        Assert.True(SwitchText.Read(" Yes "));
        Assert.True(SwitchText.Read("1"));
        Assert.False(SwitchText.Read("off"));
        Assert.False(SwitchText.Read(null));
        Assert.Null(SwitchText.Read("maybe"));

        // Each schema switch's default reads as on or off.
        foreach (SectionSchema kind in Kinds())
        {
            foreach (FieldSchema field in kind.Fields.Where(field => field.Kind == FieldKinds.Switch))
            {
                Assert.NotNull(SwitchText.Read(field.Default));
            }
        }
    }

    [Fact]
    public void ListsAndNumbersReadTheWayTheRulesDo()
    {
        Assert.Equal(["230000 * 2", "230020"], ListText.Split(" 230000 * 2 ,, 230020 ,"));
        Assert.Empty(ListText.Split(null));
        Assert.Equal("a, b", ListText.Join(["a ", "", " b"]));

        Assert.Null(NumberText.WholeProblem("42", null, null));
        Assert.Null(NumberText.WholeProblem("+7", 1, 99));
        Assert.Null(NumberText.WholeProblem("-3", null, null));
        Assert.NotNull(NumberText.WholeProblem("1.5", null, null));
        Assert.NotNull(NumberText.WholeProblem("100", 1, 99));
        Assert.Null(NumberText.WholeProblem("100", 0, 0));
        Assert.Equal("1 to 99", NumberText.Range(1, 99));
        Assert.Null(NumberText.Range(0, 0));
        Assert.Null(NumberText.DecimalProblem("1.5"));
        Assert.NotNull(NumberText.DecimalProblem("1,5"));
    }

    [Fact]
    public void PathsAreRelativeInsideTheModFolder()
    {
        string folder = Path.Combine(root, "Art");
        Assert.Equal("art/lantern.png", ProjectPaths.Relative(folder, Path.Combine(folder, "art", "lantern.png")));
        Assert.Equal("main.lua", ProjectPaths.Relative(folder + "\\", Path.Combine(folder, "main.lua")));
        Assert.Null(ProjectPaths.Relative(folder, Path.Combine(root, "elsewhere.png")));
        Assert.Null(ProjectPaths.Relative(folder, Path.Combine(root, "Art2", "x.png")));
        Assert.Null(ProjectPaths.Relative(folder, folder));

        Assert.Equal(Path.Combine(folder, "art", "lantern.png"), ProjectPaths.Full(folder, "art/lantern.png"));
        Assert.Null(ProjectPaths.Full(folder, "../x.png"));
        Assert.Null(ProjectPaths.Full(folder, "C:\\x.png"));
        Assert.Null(ProjectPaths.Full(folder, ""));
    }

    [Fact]
    public void RecentProjectsKeepTheNewestEightOnce()
    {
        string Folder(int index) => Path.Combine(root, "mod" + index);
        IReadOnlyList<string> list = [];
        for (int index = 0; index < 10; ++index)
        {
            list = RecentProjects.Add(list, Folder(index));
        }
        Assert.Equal(RecentProjects.Limit, list.Count);
        Assert.Equal(Folder(9), list[0]);
        Assert.DoesNotContain(Folder(1), list);

        list = RecentProjects.Add(list, Folder(5).ToUpperInvariant() + "\\");
        Assert.Equal(RecentProjects.Limit, list.Count);
        Assert.True(RecentProjects.Same(Folder(5), list[0]));
        Assert.Single(list, folder => RecentProjects.Same(folder, Folder(5)));

        list = RecentProjects.Remove(list, Folder(9));
        Assert.DoesNotContain(list, folder => RecentProjects.Same(folder, Folder(9)));
        Assert.Equal([Folder(3)], RecentProjects.Add(null, Folder(3)));
    }

    [Fact]
    public void DocsLinksPointAtTheCanopyRepository()
    {
        Assert.Equal("https://github.com/Zorkats/Canopy/blob/main/docs/ITEMS.md", CanopyDocs.Url("docs/ITEMS.md"));
        Assert.All(Kinds(), kind => Assert.StartsWith("docs/", kind.Doc));
    }
}
