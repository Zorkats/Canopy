using System.Globalization;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// What one reference field takes beyond the keys of mods, by the rules that
// read it: VanillaIds, a number naming a game row (an item, a flag, an icon
// some vanilla item uses, a texture); EnumNames, a game enum name such as
// TEX_ID_...; Groups, a cooking group:<number>.
public sealed record RefRules(RefKind Kind, bool VanillaIds, bool EnumNames = false, bool Groups = false)
{
    // The part's name in a sentence: "an item", "a flag".
    public string Noun => Kind switch
    {
        RefKind.Item => "an item",
        RefKind.Icon => "an icon",
        RefKind.Flag => "a flag",
        RefKind.Texture => "a texture",
        RefKind.Skin => "a skin",
        RefKind.Villager => "a villager",
        RefKind.Whistle => "a whistle",
        RefKind.Object => "an object",
        _ => "a recipe",
    };

    // The sentence for a value that names nothing it takes.
    public string NotFound => VanillaIds
        ? $"Not {Noun} of this mod, another mod or the game."
        : $"Not {Noun} of this mod or another mod.";
}

// How a value resolved. Own and OtherMod carry Part; Vanilla carries Id, or
// Name for an enum name; Group carries Id; Unchecked is another mod's key
// with no mods folder to look in; Unresolved carries Problem.
public enum RefStatus
{
    Empty,
    Own,
    OtherMod,
    Unchecked,
    Vanilla,
    Group,
    Unresolved,
}

public sealed record RefResolution(RefStatus Status, ModPart? Part = null, ulong? Id = null, string? Name = null,
    string? Problem = null);

// Reading reference values the way src/CanopyCore reads them: FlagRef,
// ItemRef and KeyRef in mod_rules.cpp, a recipe's needs in
// rules_recipes.cpp, an art reference in rules_art.cpp. Whether a vanilla id
// is a row the game has is the caller's check, against the game's lists.
public static class References
{
    // Canopy's ids start here; vanilla ids are below (mod_rules.h).
    public const ulong FirstModId = 1500000000;
    // Game flags are 1 to FlagCount - 1; mods get ModFlagFirst to ModFlagLast.
    public const ulong FlagCount = 99999;
    public const ulong ModFlagFirst = 95000;
    public const ulong ModFlagLast = 99998;
    public const ulong MaxGroup = 1000;
    public const string GroupPrefix = "group:";

    // The rules for a field of kind (FieldKinds) in a section of prefix,
    // null for a kind that names nothing by key.
    public static RefRules? For(string kind, string prefix) => kind switch
    {
        FieldKinds.ItemRef => new(RefKind.Item, VanillaIds: true),
        FieldKinds.FlagRef => new(RefKind.Flag, VanillaIds: true),
        // An item's icon may be a vanilla icon id; a whistle's must be a
        // mod icon (its score picks a vanilla one).
        FieldKinds.IconRef => new(RefKind.Icon, VanillaIds: ModNames.Same(prefix, "item.")),
        FieldKinds.TextureRef => new(RefKind.Texture, VanillaIds: true, EnumNames: true),
        FieldKinds.SkinRef => new(RefKind.Skin, VanillaIds: false),
        // A place item places a mod object; vanilla furniture has its own item.
        FieldKinds.ObjectRef => new(RefKind.Object, VanillaIds: false),
        _ => null,
    };

    // The rules for one ingredient of a recipe's needs.
    public static RefRules Need(bool cooking) => new(RefKind.Item, VanillaIds: true, Groups: cooking);

    // The rules for the texture of a skin's swap.
    public static RefRules SwapTexture { get; } = new(RefKind.Texture, VanillaIds: true, EnumNames: true);

    public static RefResolution Resolve(RefRules rules, ProjectKeys keys, string? value)
    {
        string text = (value ?? "").Trim();
        if (text.Length == 0)
        {
            return new(RefStatus.Empty);
        }
        if (rules.Groups && text.StartsWith(GroupPrefix, StringComparison.OrdinalIgnoreCase))
        {
            return Digits(text[GroupPrefix.Length..].Trim()) is { } group && group >= 1 && group <= MaxGroup
                ? new(RefStatus.Group, Id: group)
                : new(RefStatus.Unresolved, Problem: "A group is group:1 to group:1000, one a vanilla cooking recipe uses.");
        }
        if (rules.VanillaIds && Digits(text) is { } id)
        {
            return VanillaId(rules.Kind, id);
        }
        if (keys.Find(rules.Kind, text) is { } part)
        {
            return new(part.Own ? RefStatus.Own : RefStatus.OtherMod, Part: part);
        }
        int dot = text.IndexOf('.');
        if (dot >= 0 && !keys.HasOthers && !ModNames.Same(text[..dot], keys.ModKey))
        {
            return new(RefStatus.Unchecked);
        }
        if (rules.EnumNames && dot < 0 && LooksLikeEnum(text))
        {
            return new(RefStatus.Vanilla, Name: text);
        }
        return new(RefStatus.Unresolved, Problem: rules.NotFound);
    }

    // Decimal digits only, at most 19 of them, as mod_rules::ParseU64.
    public static ulong? Digits(string text) =>
        text.Length is >= 1 and <= 19 && text.All(char.IsAsciiDigit)
            ? ulong.Parse(text, NumberStyles.None, CultureInfo.InvariantCulture)
            : null;

    // An enum name as the game writes them: capitals, digits and '_', with
    // at least one '_' (rules_art.cpp).
    public static bool LooksLikeEnum(string text) =>
        text.Contains('_') && text.All(c => c is (>= 'A' and <= 'Z') or (>= '0' and <= '9') or '_');

    private static RefResolution VanillaId(RefKind kind, ulong id)
    {
        if (id == 0)
        {
            return new(RefStatus.Unresolved, Problem: "0 names nothing. Pick one, or clear the field.");
        }
        if (kind == RefKind.Flag)
        {
            if (id >= ModFlagFirst && id <= ModFlagLast)
            {
                return new(RefStatus.Unresolved,
                    Problem: "Ids 95000 to 99998 belong to mod flags. Name a mod flag by its key instead.");
            }
            return id < FlagCount
                ? new(RefStatus.Vanilla, Id: id)
                : new(RefStatus.Unresolved, Problem: "A game flag id is 1 to 94999.");
        }
        if (id >= FirstModId)
        {
            return new(RefStatus.Unresolved,
                Problem: "Ids from 1500000000 up are Canopy's own. Name a mod's part by its key instead.");
        }
        return new(RefStatus.Vanilla, Id: id);
    }
}
