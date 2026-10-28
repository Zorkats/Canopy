using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The pieces of Create's problems panel and raw view that need no window:
// where a line of canopy.ini is, how a line is coloured, and the project
// check through canopy_core.dll on a temporary project. Without the DLL the
// check test says so and passes.
public sealed class ProblemsTests(ITestOutputHelper output) : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "canopy-problems-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try
        {
            Directory.Delete(root, recursive: true);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static IniStructure Lines(params (int Line, string Kind, string Section, string Key)[] lines) =>
        new() { Ok = true, Lines = lines.Select(line => new IniLine(line.Line, line.Kind, line.Section, line.Key, "")).ToList() };

    [Fact]
    public void LinesMapToTheirSectionAndField()
    {
        ProblemPlaces places = ProblemPlaces.From(Lines(
            (1, "comment", "", ""),
            (2, "section", "mod", ""),
            (3, "value", "mod", "key"),
            (4, "blank", "mod", ""),
            (5, "section", "item.Seed", ""),
            (6, "value", "item.Seed", "name"),
            (7, "value", "item.Seed", "name.en"),
            (8, "value", "item.Seed", "help.zh-tw"),
            (9, "comment", "item.Seed", ""),
            (10, "other", "item.Seed", ""),
            (11, "value", "item.Seed", "Name.JA")));

        Assert.Null(places.At(1));
        Assert.Equal(new ProblemPlace("mod", ""), places.At(2));
        Assert.Equal(new ProblemPlace("mod", "key"), places.At(3));
        Assert.Equal(new ProblemPlace("mod", ""), places.At(4));
        Assert.Equal(new ProblemPlace("item.Seed", ""), places.At(5));
        Assert.Equal(new ProblemPlace("item.Seed", "name"), places.At(6));
        Assert.Equal(new ProblemPlace("item.Seed", "name"), places.At(7));
        Assert.Equal(new ProblemPlace("item.Seed", "help"), places.At(8));
        Assert.Equal(new ProblemPlace("item.Seed", ""), places.At(9));
        Assert.Equal(new ProblemPlace("item.Seed", ""), places.At(10));
        Assert.Equal(new ProblemPlace("item.Seed", "name"), places.At(11));
        Assert.Null(places.At(12));
        Assert.True(places.At(7)!.Is("ITEM.seed", "NAME"));
        Assert.False(places.At(7)!.Is("item.Seed", "name.en"));
    }

    [Theory]
    [InlineData("name.en", "name")]
    [InlineData("job.zh-cn", "job")]
    [InlineData("Help.KO", "help")]
    [InlineData("name", "name")]
    // Not a language: the key stays whole.
    [InlineData("row.id", "row.id")]
    [InlineData("name.de", "name.de")]
    [InlineData(".en", ".en")]
    public void FieldKeysDropOnlyALanguage(string key, string field) =>
        Assert.Equal(field, ProblemPlaces.FieldKey(key));

    private static string Show(string line) =>
        string.Join(" ", IniTokens.Line(line).Select(token => $"{token.Kind}:{line.Substring(token.Start, token.Length)}"));

    [Theory]
    [InlineData("", "")]
    [InlineData("   \t\r", "")]
    [InlineData("[mod]", "Section:[mod]")]
    [InlineData("  [item.Seed]  \r", "Section:[item.Seed]")]
    [InlineData("; a note", "Comment:; a note")]
    [InlineData("  # also = a note", "Comment:# also = a note")]
    [InlineData("key = Apple", "Key:key Value:Apple")]
    [InlineData("  name.en=Seed  \r", "Key:name.en Value:Seed")]
    // The first '=' splits; the loader has no comment after a value.
    [InlineData("notes = S=L ; not a comment", "Key:notes Value:S=L ; not a comment")]
    [InlineData("help =", "Key:help")]
    [InlineData("= orphan", "Value:orphan")]
    // Lines Canopy skips: no '=' and no brackets on both ends.
    [InlineData("just words", "Other:just words")]
    [InlineData("[unclosed", "Other:[unclosed")]
    [InlineData("\uFEFF[mod]", "Section:[mod]")]
    public void TokensFollowTheLoadersRules(string line, string expected) =>
        Assert.Equal(expected, Show(line));

    private bool DllReady()
    {
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        return true;
    }

    private string Write(string relative, string text)
    {
        string path = Path.Combine(root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
        return path;
    }

    [Fact]
    public void AProjectIsCheckedWithItsUnsavedTextAgainstTheOtherMods()
    {
        if (!DllReady())
        {
            return;
        }
        string mods = Path.Combine(root, "mods");
        Write(@"mods\Beta\canopy.ini", "[mod]\nkey = Beta\n[flag.Seen]\n[whistle.X]\nname = X\n");
        // On disk the project is broken; the editor's text is what counts.
        Write(@"projects\Apple\canopy.ini", "[mod]\nkey = bad key\n");
        Write(@"projects\Apple\whistles\rise.lua", "function main() end\n");
        string project = Path.Combine(root, "projects", "Apple");
        string text = "[mod]\nkey = Apple\n[whistle.Rise]\nname = Rise\nnotes = SSLL\nlearn = Beta.Seen\n" +
                      "script = whistles/rise.lua\n";

        ValidationReport fine = CanopyCore.ValidateProject(null, mods, project, text);
        Assert.True(fine.Ok, fine.Error);
        Assert.False(fine.Vanilla);
        Assert.Equal(["Apple", "Beta"], fine.Folders);
        // Beta's own error is not the project's.
        Assert.Empty(fine.Diagnostics);
        Assert.Equal(0, fine.Errors);

        string broken = text + "score = 11\nname.en = Rise\n";
        ValidationReport warned = CanopyCore.ValidateProject(null, mods, project, broken);
        Assert.True(warned.Ok, warned.Error);
        ModDiagnostic score = Assert.Single(warned.Diagnostics);
        Assert.Equal("warning", score.Severity);
        Assert.Equal("Apple", score.Folder);
        Assert.Equal(ModProject.IniName, score.File);
        Assert.Equal(8, score.Line);
        Assert.Equal(1, warned.Warnings);
        // The line points at its field.
        ProblemPlaces places = ProblemPlaces.From(CanopyCore.IniParse(broken));
        Assert.Equal(new ProblemPlace("whistle.Rise", "score"), places.At(score.Line));
        Assert.Equal(new ProblemPlace("whistle.Rise", "name"), places.At(9));

        // No game and no mods folder: the project alone.
        ValidationReport alone = CanopyCore.ValidateProject(null, null, project, "[mod]\nkey = Apple\n");
        Assert.True(alone.Ok, alone.Error);
        Assert.Equal(["Apple"], alone.Folders);

        ValidationReport missing = CanopyCore.ValidateProject(null, null, Path.Combine(root, "nope"), "");
        Assert.False(missing.Ok);
        Assert.EndsWith("is not a folder.", missing.Error);
    }
}
