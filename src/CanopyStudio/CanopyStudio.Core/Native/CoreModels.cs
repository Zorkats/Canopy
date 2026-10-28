using System.Text.Json.Serialization;

namespace CanopyStudio.Core.Native;

// The JSON canopy_core.dll hands out, field for field. Every result carries
// abi, ok and error; ok is false, with error saying why, when nothing was
// read.

public abstract record CoreResult
{
    public int Abi { get; init; }
    public bool Ok { get; init; }
    public string? Error { get; init; }
}

public sealed record ArchiveInfo(string Name, uint Version, int Entries);

public sealed record GameInfo : CoreResult
{
    public string Folder { get; init; } = "";
    public bool Exe { get; init; }
    public uint Build { get; init; }
    public bool Supported { get; init; }
    public bool Mods { get; init; }
    public bool Canopy { get; init; }
    public bool Log { get; init; }
    public IReadOnlyList<ArchiveInfo> Archives { get; init; } = [];
    public int Files { get; init; }
    public int Tables { get; init; }
}

public sealed record GameFile(string Path, long Size, long Time, string Archive, int Copies);

public sealed record FileList : CoreResult
{
    public IReadOnlyList<GameFile> Files { get; init; } = [];
}

public sealed record TableRow(int Index, ulong Id, string? Enum, int Size);

public sealed record TableInfo : CoreResult
{
    public string File { get; init; } = "";
    [JsonPropertyName("struct")]
    public string? StructName { get; init; }
    public bool Schema { get; init; }
    public long PoolBytes { get; init; }
    public IReadOnlyList<TableRow> Rows { get; init; } = [];
}

// One place of a row: Value is the text a [patch.] section takes there;
// Editable is true where a patch can set it.
public sealed record RowField(string Path, string Type, string Value, string? Table)
{
    public bool Editable { get; init; }
}

public sealed record RowText(int Offset, string Text);

public sealed record RowInfo : CoreResult
{
    public string File { get; init; } = "";
    public int Index { get; init; }
    public ulong Id { get; init; }
    public string? Enum { get; init; }
    public int Size { get; init; }
    public IReadOnlyList<RowField> Fields { get; init; } = [];
    public IReadOnlyList<RowText> Texts { get; init; } = [];
    public string Hex { get; init; } = "";
}

// One column of a table grid: a place of a row, named and typed as RowField
// names it. Editable is true when a patch can set it in every row that has
// it.
public sealed record GridColumn(string Path, string Type, string? Table, bool Editable);

// One row of a table grid: a value per column, null where the row has no
// such place. Fits is false, with every value null, for a row the schema
// does not describe.
public sealed record GridRow(int Index, ulong Id, string? Enum, bool Fits, IReadOnlyList<string?> Values);

// A whole table under its schema's columns, for a grid.
public sealed record TableGrid : CoreResult
{
    public string File { get; init; } = "";
    [JsonPropertyName("struct")]
    public string? StructName { get; init; }
    public IReadOnlyList<GridColumn> Columns { get; init; } = [];
    public IReadOnlyList<GridRow> Rows { get; init; } = [];
}

public sealed record TextureInfo : CoreResult
{
    public int Width { get; init; }
    public int Height { get; init; }
}

public sealed record FileReadInfo : CoreResult
{
    public int Size { get; init; }
}

// A decoded texture: width x height pixels, 4 bytes each in B, G, R, A
// order, straight alpha, rows top first.
public sealed record TextureImage(int Width, int Height, byte[] Pixels);

// One thing a picker lists: the row's id and enum name, its name in the
// asked language (empty when the game has none), its icon.dat id and its
// group (an item's itemcategory.dat id), each null when it has none.
public sealed record GameName(ulong Id, string? Enum, string Name, ulong? Icon, ulong? Group);

// What a batch of icons saved: the PNG files written, and a sentence for
// each icon that could not be.
public sealed record IconBatch : CoreResult
{
    public int Written { get; init; }
    public IReadOnlyList<string> Problems { get; init; } = [];
}

public sealed record NameList : CoreResult
{
    public string Kind { get; init; } = "";
    public string Language { get; init; } = "";
    public IReadOnlyList<GameName> Entries { get; init; } = [];
}

// One line of canopy.ini text. Kind is "section", "value", "comment",
// "blank" or "other"; Section is the one the line belongs to, empty before
// the first.
public sealed record IniLine(int Line, string Kind, string Section, string Key, string Value);

public sealed record IniStructure : CoreResult
{
    public string Eol { get; init; } = "";
    public IReadOnlyList<IniLine> Lines { get; init; } = [];
}

// The text after an edit; the text as it was when Ok is false.
public sealed record IniEditResult : CoreResult
{
    public string Text { get; init; } = "";
}
