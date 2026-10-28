using System.Text.Json;

namespace CanopyStudio.Core.Lua;

// What a name in a Lua mod is: Canopy's API (Table, Function, Field), a
// canopy.on event, a name of the sandbox (Global, Library), a Lua keyword, a
// game command a safe mod may call (Command), or one only an unsafe mod may
// (Guarded).
public enum LuaEntryKind
{
    Table,
    Function,
    Field,
    Event,
    Global,
    Library,
    Keyword,
    Command,
    Guarded,
}

// One name the Lua editor completes. Name is the full dotted name, such as
// canopy.items.give; Signature is how it is called, null when not a function
// Canopy documents.
public sealed record LuaEntry(string Name, LuaEntryKind Kind, string? Signature, string Doc)
{
    // The part after the last dot: give for canopy.items.give.
    public string Leaf => Name[(Name.LastIndexOf('.') + 1)..];

    // The dotted name it is a member of, "" at the top level.
    public string Owner => Name.LastIndexOf('.') is var dot and >= 0 ? Name[..dot] : "";
}

// The names a Lua mod can use, from lua_api_catalog.json, which
// _work/scripts/lua_api_catalog.py writes from docs/LUA.md and
// src/Canopy/lua_commands.h. Read once; any thread may call.
public sealed class LuaCatalog
{
    private const string ResourceName = "lua_api_catalog.json";

    private static readonly Lazy<LuaCatalog> Shared = new(() => Parse(ReadResource()));

    private readonly Dictionary<string, List<LuaEntry>> byOwner;

    private LuaCatalog(IReadOnlyList<LuaEntry> entries)
    {
        Entries = entries;
        byOwner = entries.Where(entry => entry.Kind != LuaEntryKind.Event)
            .GroupBy(entry => entry.Owner, StringComparer.Ordinal)
            .ToDictionary(group => group.Key, group => group.ToList(), StringComparer.Ordinal);
        Events = entries.Where(entry => entry.Kind == LuaEntryKind.Event).ToList();
    }

    public static LuaCatalog Default => Shared.Value;

    public IReadOnlyList<LuaEntry> Entries { get; }

    // The canopy.on events.
    public IReadOnlyList<LuaEntry> Events { get; }

    // The members of owner ("" for the top level), events aside, in catalog
    // order. Empty for a name with no members.
    public IReadOnlyList<LuaEntry> Members(string owner) =>
        byOwner.TryGetValue(owner, out List<LuaEntry>? members) ? members : [];

    // The entry named name, events aside, or null.
    public LuaEntry? Find(string name) =>
        Members(name.LastIndexOf('.') is var dot and >= 0 ? name[..dot] : "")
            .FirstOrDefault(entry => string.Equals(entry.Name, name, StringComparison.Ordinal));

    public static LuaCatalog Parse(string json)
    {
        CatalogFile file = JsonSerializer.Deserialize<CatalogFile>(json, new JsonSerializerOptions(JsonSerializerDefaults.Web))
            ?? throw new InvalidDataException($"{ResourceName} is empty.");
        List<LuaEntry> entries = [];
        foreach (CatalogEntry entry in file.Entries ?? [])
        {
            if (string.IsNullOrEmpty(entry.Name) || KindOf(entry.Kind) is not { } kind)
            {
                throw new InvalidDataException($"{ResourceName} has an entry with no name or an unknown kind: {entry.Name} {entry.Kind}.");
            }
            entries.Add(new LuaEntry(entry.Name, kind, entry.Signature, entry.Doc ?? ""));
        }
        return new LuaCatalog(entries);
    }

    private static LuaEntryKind? KindOf(string? kind) => kind switch
    {
        "table" => LuaEntryKind.Table,
        "function" => LuaEntryKind.Function,
        "field" => LuaEntryKind.Field,
        "event" => LuaEntryKind.Event,
        "global" => LuaEntryKind.Global,
        "library" => LuaEntryKind.Library,
        "keyword" => LuaEntryKind.Keyword,
        "command" => LuaEntryKind.Command,
        "guarded" => LuaEntryKind.Guarded,
        _ => null,
    };

    private static string ReadResource()
    {
        using Stream stream = typeof(LuaCatalog).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidDataException($"{ResourceName} is not built into Canopy Studio.");
        using StreamReader reader = new(stream);
        return reader.ReadToEnd();
    }

    private sealed record CatalogFile(int Version, List<CatalogEntry>? Entries);

    private sealed record CatalogEntry(string? Name, string? Kind, string? Signature, string? Doc);
}
