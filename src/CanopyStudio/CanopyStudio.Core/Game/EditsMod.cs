using System.Globalization;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// One value a [patch.<table>.<row>] section of the edits mod sets. Table is
// the table's name without ".dat"; Row is the row's id, or its enum name
// when the section names the row so; Line counts from 1.
public sealed record PatchValue(string Table, string Row, string Path, string Value, int Line)
{
    // The row's id, null when the section names the row by its enum name.
    public ulong? Id => ulong.TryParse(Row, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) ? id : null;
}

// The mod the Game area saves its edits to: a Canopy mod whose
// [patch.<table>.<id>] sections change fields of the game's own rows when
// the game loads them (docs/PATCHES.md). Every edit is an ini edit of the
// project, so comments and every other section of canopy.ini stay as they
// are. Rows are named by id. A framework_ table's rows take its table's
// name, since the loader finds a patched row in either file. Nothing is
// written until Save.
public sealed class EditsMod
{
    private const string Prefix = "patch.";
    private const string Twin = "framework_";

    private EditsMod(ModProject project) => Project = project;

    public ModProject Project { get; }
    public string Folder => Project.Folder;
    public string Name => Project.Name;
    public bool IsDirty => Project.IsDirty;

    public static (EditsMod? Mod, string? Error) Open(string folder)
    {
        ProjectOpenResult opened = ModProject.Open(folder);
        return opened.Ok ? (new EditsMod(opened.Project!), null) : (null, opened.Error);
    }

    // A table's name in a patch section: lowercase, with no ".dat" and no
    // framework_ before it.
    public static string TableName(string file)
    {
        string name = file.Trim().ToLowerInvariant();
        if (name.EndsWith(".dat", StringComparison.Ordinal))
        {
            name = name[..^4];
        }
        return name.StartsWith(Twin, StringComparison.Ordinal) ? name[Twin.Length..] : name;
    }

    // The section that patches row id of file, as a new one is written.
    public static string Section(string file, ulong id) =>
        string.Create(CultureInfo.InvariantCulture, $"{Prefix}{TableName(file)}.{id}");

    // Null when canopy.ini can hold value as it is, else why not: a value
    // is one line, read back without the spaces around it.
    public static string? CannotHold(string value)
    {
        if (value.Contains('\n') || value.Contains('\r'))
        {
            return "A value in canopy.ini is one line, so it cannot hold a line break.";
        }
        if (value.Length > 0 && (char.IsWhiteSpace(value[0]) || char.IsWhiteSpace(value[^1])))
        {
            return "canopy.ini drops the spaces at the start and end of a value, so it cannot keep them.";
        }
        return null;
    }

    // Every value the mod's patch sections set, in file order.
    public IReadOnlyList<PatchValue> Patches
    {
        get
        {
            List<PatchValue> values = [];
            foreach (IniLine line in Project.Structure.Lines)
            {
                if (line.Kind == "value" && Split(line.Section) is { } row)
                {
                    values.Add(new PatchValue(row.Table, row.Row, line.Key, line.Value, line.Line));
                }
            }
            return values;
        }
    }

    // The value the mod sets at path of row id of file, null when it sets
    // none there.
    public string? Value(string file, ulong id, string path) =>
        SectionOf(file, id) is { } section ? Project.Value(section, path) : null;

    // Sets path of row id of file to value, adding the row's section when it
    // has none. Null when made, else why not.
    public string? Set(string file, ulong id, string path, string value)
    {
        if (CannotHold(value) is { } reason)
        {
            return reason;
        }
        return Project.Edit("set", SectionOf(file, id) ?? Section(file, id), path, value);
    }

    // Takes path of row id of file out of the mod, and the row's section
    // with it when nothing else is left there. Null when made or when the
    // mod did not set it, else why not.
    public string? Remove(string file, ulong id, string path)
    {
        if (SectionOf(file, id) is not { } section || Project.Value(section, path) is null)
        {
            return null;
        }
        if (Project.Edit("remove", section, path) is { } error)
        {
            return error;
        }
        bool empty = !Project.Structure.Lines.Any(line =>
            line.Kind == "value" && string.Equals(line.Section, section, StringComparison.OrdinalIgnoreCase));
        return empty ? Project.Edit("removeSection", section) : null;
    }

    // Writes canopy.ini. Null when saved, else why not.
    public string? Save() => Project.Save();

    // The section canopy.ini has for row id of file, as written there; null
    // when there is none.
    private string? SectionOf(string file, ulong id)
    {
        string table = TableName(file);
        foreach (string name in Project.SectionNames)
        {
            if (Split(name) is { } row && row.Table == table &&
                ulong.TryParse(row.Row, NumberStyles.None, CultureInfo.InvariantCulture, out ulong rowId) && rowId == id)
            {
                return name;
            }
        }
        return null;
    }

    // A [patch.<table>.<row>] section's table, as TableName gives it, and
    // row; null for any other section.
    private static (string Table, string Row)? Split(string section)
    {
        if (!section.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        string rest = section[Prefix.Length..];
        int dot = rest.LastIndexOf('.');
        if (dot <= 0 || dot == rest.Length - 1)
        {
            return null;
        }
        return (TableName(rest[..dot]), rest[(dot + 1)..]);
    }
}
