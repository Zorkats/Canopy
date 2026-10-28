using System.Text;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Create;

// Who made a change to a project: a form, the raw ini view, or a load. A
// view skips the changes it made itself.
public sealed class ProjectChangedEventArgs(object? source) : EventArgs
{
    public object? Source { get; } = source;
}

public sealed record ProjectOpenResult(ModProject? Project, string? Error)
{
    public bool Ok => Project is not null;
}

// A mod being made: a folder with a canopy.ini. Every change goes through
// the ini text with canopy_core's editor (src/CanopyCore/ini_document.h), so
// comments, key order and keys no form knows about stay as they were. Forms
// and the raw view share one instance; Changed tells each about the other's
// edits. Use it from one thread.
public sealed class ModProject
{
    public const string IniName = "canopy.ini";

    // canopy.ini as last read or written, null before the first save.
    private string? savedText;

    private ModProject(string folder, string text, bool saved)
    {
        Folder = folder;
        Text = text;
        savedText = saved ? text : null;
        Structure = Parse(text);
    }

    // The mod folder, full path.
    public string Folder { get; }

    // The folder's name, which is the mod key unless [mod] names another.
    public string Name => System.IO.Path.GetFileName(Folder.TrimEnd('\\', '/'));

    // canopy.ini, or the CANOPY.INI already there on Linux.
    public string IniPath => GameFiles.Find(Folder, IniName);

    // canopy.ini as it is now, saved or not.
    public string Text { get; private set; }

    // Text read the way the loader reads it. Lines is empty when it could
    // not be read.
    public IniStructure Structure { get; private set; }

    // True when Text differs from the file.
    public bool IsDirty => Text != savedText;

    // Text changed. Structure is already updated.
    public event EventHandler<ProjectChangedEventArgs>? Changed;

    // The project in folder, which must exist. A folder without canopy.ini
    // opens as an empty project, not yet saved.
    public static ProjectOpenResult Open(string folder)
    {
        string full = System.IO.Path.GetFullPath(folder);
        if (!Directory.Exists(full))
        {
            return new(null, $"The folder {full} does not exist.");
        }
        string ini = GameFiles.Find(full, IniName);
        try
        {
            return File.Exists(ini)
                ? new(new ModProject(full, File.ReadAllText(ini, Encoding.UTF8), saved: true), null)
                : new(new ModProject(full, "", saved: false), null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(null, $"{ini} could not be read: {exception.Message}");
        }
    }

    // A new project in folder, which must be missing or empty, with text as
    // its canopy.ini, saved at once.
    public static ProjectOpenResult Create(string folder, string text)
    {
        string full = System.IO.Path.GetFullPath(folder);
        try
        {
            if (Directory.Exists(full) && Directory.EnumerateFileSystemEntries(full).Any())
            {
                return new(null, $"The folder {full} is not empty. Pick an empty folder or a new name.");
            }
            Directory.CreateDirectory(full);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(null, $"The folder {full} could not be made: {exception.Message}");
        }
        ModProject project = new(full, text, saved: false);
        string? error = project.Save();
        return error is null ? new(project, null) : new(null, error);
    }

    // The sections in file order, each once, as written.
    public IReadOnlyList<string> SectionNames =>
        Structure.Lines.Where(line => line.Kind == "section").Select(line => line.Section)
            .DistinctBy(ModNames.Lower).ToList();

    public bool HasSection(string section) =>
        Structure.Lines.Any(line => line.Kind == "section" && ModNames.Same(line.Section, section));

    // The value of the first key in section, as the loader reads it; null
    // when the key is not there. Names match without case.
    public string? Value(string section, string key) =>
        Line(section, key)?.Value;

    // The 1-based line of the first key in section, or of the section
    // header when key is empty; 0 when there is none.
    public int LineOf(string section, string key = "")
    {
        IniLine? line = key.Length == 0
            ? Structure.Lines.FirstOrDefault(entry => entry.Kind == "section" && ModNames.Same(entry.Section, section))
            : Line(section, key);
        return line?.Line ?? 0;
    }

    // One edit, as ini_document.h describes op: "set", "remove",
    // "addSection", "removeSection" or "renameSection". Null when made,
    // else why not; Text is then unchanged. source is passed to Changed.
    public string? Edit(string op, string section, string key = "", string value = "", object? source = null)
    {
        IniEditResult result = CanopyCore.IniEdit(Text, op, section, key, value);
        if (!result.Ok)
        {
            return result.Error ?? "The change could not be made.";
        }
        Apply(result.Text, source);
        return null;
    }

    // Replaces the whole text, as the raw view does. source is passed to
    // Changed.
    public void SetText(string text, object? source = null) => Apply(text, source);

    // Writes Text to canopy.ini through a temporary file. Null when saved,
    // else why not.
    public string? Save()
    {
        string temporary = IniPath + ".new";
        try
        {
            File.WriteAllText(temporary, Text, new UTF8Encoding(false));
            BusyFiles.Replace(temporary, IniPath);
            savedText = Text;
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{IniPath} could not be saved: {exception.Message}";
        }
    }

    private IniLine? Line(string section, string key) =>
        Structure.Lines.FirstOrDefault(line =>
            line.Kind == "value" && ModNames.Same(line.Section, section) && ModNames.Same(line.Key, key));

    private void Apply(string text, object? source)
    {
        if (text == Text)
        {
            return;
        }
        Text = text;
        Structure = Parse(text);
        Changed?.Invoke(this, new ProjectChangedEventArgs(source));
    }

    private static IniStructure Parse(string text)
    {
        IniStructure structure = CanopyCore.IniParse(text);
        return structure.Ok ? structure : structure with { Lines = [] };
    }
}
