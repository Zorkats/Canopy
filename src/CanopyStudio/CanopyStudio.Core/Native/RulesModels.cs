namespace CanopyStudio.Core.Native;

// The canopy.ini rules as canopy_core.dll hands them out: what each section
// kind accepts (src/CanopyCore/mod_rules.h, FieldInfo and SectionInfo) and
// the check of mods against those rules (rules_report.h).

// The kind of a field's value, as the JSON spells it.
public static class FieldKinds
{
    public const string Text = "text";
    // A text with optional per-language keys: name, name.en, ...
    public const string LangText = "lang-text";
    public const string Integer = "integer";
    public const string Number = "number";
    // on or off.
    public const string Switch = "switch";
    // One of FieldSchema.Choices.
    public const string Choice = "choice";
    // A comma-separated list.
    public const string List = "list";
    // A file inside the mod folder.
    public const string Path = "path";
    // An id of a row of FieldSchema.Table.
    public const string VanillaId = "vanilla-id";
    public const string FlagRef = "flag-ref";
    public const string ItemRef = "item-ref";
    public const string IconRef = "icon-ref";
    public const string TextureRef = "texture-ref";
    public const string SkinRef = "skin-ref";
    public const string ObjectRef = "object-ref";
    // S and L notes.
    public const string Melody = "melody";
    // Anything else, explained by Help.
    public const string Other = "other";
}

// One key a section kind accepts. Default is the value used when the key is
// left out; Table is the table of a vanilla-id; Min and Max bound an
// integer when Min < Max.
public sealed record FieldSchema(
    string Key,
    string Kind,
    bool Required,
    string Help,
    string? Default,
    string? Table,
    long? Min,
    long? Max,
    IReadOnlyList<string> Choices);

// One section kind. Prefix is the section name's start, such as
// "whistle.", or "mod" for [mod]; Doc is the docs page that explains it.
public sealed record SectionSchema(string Prefix, string Title, string Doc, IReadOnlyList<FieldSchema> Fields)
{
    // A section that is the whole name, [mod], rather than a prefix and a
    // key, [item.Apple].
    public bool IsSingle => !Prefix.EndsWith('.');
}

// Every section kind, in the order a tool lists them, and the table files
// a [patch.] section can name.
public sealed record SchemaInfo : CoreResult
{
    public IReadOnlyList<SectionSchema> Sections { get; init; } = [];
    public IReadOnlyList<string> Tables { get; init; } = [];
}

// One message of a check. Severity is "error" (the loader skips the
// section or value), "warning" (it uses a default or ignores the value) or
// "note" (advice from a desktop check only). Folder is the mod folder's
// name, empty for a message about no one folder; File is "canopy.ini" or
// the file the message is about; Line counts from 1, 0 when the message is
// not about one line. Message is the exact line Canopy writes to its log.
public sealed record ModDiagnostic(string Severity, string Folder, string File, int Line, string Message);

// A check of mods. Vanilla is true when vanilla ids were checked against
// the game's tables; Folders are the mod folders read.
public sealed record ValidationReport : CoreResult
{
    public string? Game { get; init; }
    public string? Mods { get; init; }
    public bool Vanilla { get; init; }
    public IReadOnlyList<string> Folders { get; init; } = [];
    public int Errors { get; init; }
    public int Warnings { get; init; }
    public int Notes { get; init; }
    public IReadOnlyList<ModDiagnostic> Diagnostics { get; init; } = [];
}
