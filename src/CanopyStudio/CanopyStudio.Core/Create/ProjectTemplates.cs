using System.Text;

namespace CanopyStudio.Core.Create;

// Canopy's docs pages online, where a form and a template point for help.
public static class CanopyDocs
{
    public const string Base = "https://github.com/Zorkats/Canopy/blob/main/";

    // The page of doc, a repository path such as "docs/ITEMS.md".
    public static string Url(string doc) => Base + doc.TrimStart('/');
}

// A starting point for a new mod: a canopy.ini, and for some the files it
// needs beside it. Every name in a template is made up, never the game's.
public sealed class ProjectTemplate(string id, string title, string description, string doc, string body,
    IReadOnlyDictionary<string, string>? files = null)
{
    public string Id { get; } = id;
    public string Title { get; } = title;
    public string Description { get; } = description;
    // The docs page the template's first line points at.
    public string Doc { get; } = doc;
    // Other files by their path inside the mod folder, '/' between folders.
    public IReadOnlyDictionary<string, string> Files { get; } = files ?? new Dictionary<string, string>();

    // canopy.ini for a mod named name: a comment pointing at the docs,
    // [mod], then the template's sections. Lines end in CRLF, as Notepad
    // writes them.
    public string Ini(string name)
    {
        StringBuilder text = new();
        text.Append("; ").Append(Description).Append(" How it works: ").Append(CanopyDocs.Url(Doc)).Append("\r\n");
        text.Append("[mod]\r\nname = ").Append(name).Append("\r\n");
        if (body.Length > 0)
        {
            text.Append("\r\n").Append(body.ReplaceLineEndings("\r\n"));
        }
        return text.ToString();
    }

    public override string ToString() => Title;
}

public static class ProjectTemplates
{
    public static ProjectTemplate Empty { get; } = new("empty", "Empty",
        "A Canopy mod with only its [mod] section.", "docs/MODDING.md", "");

    public static IReadOnlyList<ProjectTemplate> All { get; } =
    [
        Empty,
        new("item", "Item", "A new item, a copy of a vanilla item with its own name and help.", "docs/ITEMS.md",
            """
            [item.ExampleStone]
            base = 230070
            name = Example Stone
            help = A made-up item. Change its name, its help and the item it copies.

            """),
        new("furniture", "Furniture", "A new furniture object, a copy of a vanilla one, and the item that places it.",
            "docs/OBJECTS.md",
            """
            [object.ExampleLamp]
            clone = 242000004
            name = Example Lamp

            [item.ExampleLamp]
            name = Example Lamp
            help = A made-up lamp. Use it to set it down.
            use = place
            object = ExampleLamp

            """),
        new("villager", "Villager","A new villager, a copy of a vanilla villager with its own name and job.",
            "docs/VILLAGERS.md",
            """
            [villager.ExampleWanderer]
            base = 1020
            name = Example Wanderer
            job = Example Job
            help = A made-up villager. Change its name, its job and the villager it copies.

            """),
        new("whistle", "Whistle", "A new whistle with its own melody, learned through a flag of the mod.",
            "docs/WHISTLES.md",
            """
            [flag.KnowsExampleTune]
            default = on

            [whistle.ExampleTune]
            name = Example Tune
            notes = S L S L
            learn = KnowsExampleTune

            """),
        new("recipe", "Recipe", "A new item and the craft recipe that makes it.", "docs/RECIPES.md",
            """
            [item.ExampleCharm]
            base = 230070
            name = Example Charm

            [craft.ExampleCharm]
            output = ExampleCharm
            needs = 230000 * 2, 230020

            """),
        new("flag-setting", "Flag and setting", "A flag the mod keeps per save, and a setting on the Mod Settings page.",
            "docs/LUA.md",
            """
            [flag.ExampleSeen]
            default = off

            [setting.ExampleTips]
            type = toggle
            label = Show example tips
            help = A made-up setting. Lua reads it with canopy.setting.
            default = true

            """,
            new Dictionary<string, string>
            {
                ["lua/main.lua"] = """
                    -- Runs once as the game starts. How Lua mods work: https://github.com/Zorkats/Canopy/blob/main/docs/LUA.md
                    if canopy.setting("ExampleTips") then
                      canopy.log("Example tips are on.")
                    end

                    """,
            }),
    ];

    // A new mod folder name in parent, made from template. Null error when
    // made; a project with an error when canopy.ini was written but a file
    // beside it was not.
    public static ProjectOpenResult Create(string parent, string name, ProjectTemplate template)
    {
        if (!SectionNames.KeyValid(name))
        {
            return new(null, "A mod name is 1 to 32 letters, digits, '_' or '-', with no spaces, because it is also the mod's key.");
        }
        ProjectOpenResult made = ModProject.Create(Path.Combine(parent, name), template.Ini(name));
        if (!made.Ok)
        {
            return made;
        }
        foreach ((string path, string text) in template.Files)
        {
            string full = Path.Combine(made.Project!.Folder, path.Replace('/', Path.DirectorySeparatorChar));
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(full)!);
                File.WriteAllText(full, text.ReplaceLineEndings("\r\n"), new UTF8Encoding(false));
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return new(made.Project, $"The mod was made, but {path} could not be written: {exception.Message}");
            }
        }
        return made;
    }
}
