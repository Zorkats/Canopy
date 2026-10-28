namespace CanopyStudio.Core.Assets;

// The kinds of vanilla thing a picker lists, as canopy_core_names knows them.
public enum VanillaKind
{
    Item,
    Villager,
    Whistle,
    Craft,
    Cooking,
    Gimmick,
}

// One vanilla thing: its table row id and enum name, its name in the
// language asked for (empty when the game has none), its icon.dat id and its
// group (an item's itemcategory.dat id), each null when it has none. A craft
// or cooking recipe carries its output item's name, icon and group, as the
// game's menus show it. A gimmick is a furniture object, one its own item
// places; it carries that item's icon and group, and the item's name when it
// has none of its own.
public sealed record VanillaEntry(VanillaKind Kind, ulong Id, string? Enum, string Name, ulong? Icon, ulong? Group);

// A language the game has names in: its canopy.ini code and its name in
// English.
public sealed record GameLanguage(string Code, string Name)
{
    public override string ToString() => Name;
}

// What building the asset cache did. Ok is false when nothing usable was
// made, with Error saying why; Problems lists, a sentence each, what could
// not be made while the rest was.
public sealed record AssetPrepareResult(bool Ok, string? Error, IReadOnlyList<string> Problems);

public static class VanillaKinds
{
    public const string DefaultLanguage = "en";

    public static IReadOnlyList<VanillaKind> All { get; } =
        [VanillaKind.Item, VanillaKind.Villager, VanillaKind.Whistle, VanillaKind.Craft, VanillaKind.Cooking,
         VanillaKind.Gimmick];

    // The game's seven languages, in the order its tables hold them.
    public static IReadOnlyList<GameLanguage> Languages { get; } =
    [
        new("ja", "Japanese"),
        new("en", "English"),
        new("fr", "French"),
        new("es", "Spanish"),
        new("zh-tw", "Chinese (Traditional)"),
        new("ko", "Korean"),
        new("zh-cn", "Chinese (Simplified)"),
    ];

    // The code canopy_core_names takes for kind.
    public static string Code(VanillaKind kind) => kind switch
    {
        VanillaKind.Item => "item",
        VanillaKind.Villager => "villager",
        VanillaKind.Whistle => "whistle",
        VanillaKind.Craft => "craft",
        VanillaKind.Cooking => "cooking",
        VanillaKind.Gimmick => "gimmick",
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null),
    };
}
