using System.Globalization;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// What a tile's picture comes from. Glyph is always set, so the view has
// something to draw while an image loads or when it cannot.
public enum TileArtKind
{
    Glyph,
    // An image file inside the mod folder: File, cropped to Rect when set.
    ModImage,
    // A game icon by icon id: Id.
    VanillaIcon,
    // A game item's icon, by item id: Id.
    VanillaItem,
    // A game villager's icon, by villager id: Id.
    VanillaVillager,
}

// x, y, w, h in pixels.
public sealed record TileRect(int X, int Y, int W, int H);

public sealed record TileArt(TileArtKind Kind, string Glyph, string? File = null, TileRect? Rect = null,
    ulong? Id = null);

// One tile: a section of canopy.ini. Kind is null for a section no kind
// takes.
public sealed record ContentTile(string Section, SectionSchema? Kind, string Title, string Subtitle, TileArt Art);

// One group of tiles. Kind is what "+ New" adds, null when the group cannot
// add (Game edits). Advanced groups show only while the Advanced switch is
// on.
public sealed record ContentGroup(string Id, string Title, SectionSchema? Kind, bool CanAdd, bool Advanced,
    IReadOnlyList<ContentTile> Tiles);

// The gallery of a project: which sections it shows, in which groups, and
// what each tile says and draws. A picture may follow the references its
// section makes (an item's icon, an icon's texture), at most MaxSteps of
// them, so a broken file cannot loop.
public static class ContentCatalog
{
    private const int MaxSteps = 3;
    private const string ItemPrefix = "item.";
    private const string ObjectPrefix = "object.";
    private const string IconPrefix = "icon.";
    private const string TexturePrefix = "texture.";
    private const string RowPrefix = "row.";
    private const string PatchPrefix = "patch.";

    private static readonly string[] ImageExtensions = [".png", ".jpg", ".jpeg", ".bmp"];

    // The groups after the mod group, in order: Id, Title and the prefix of
    // their kind.
    private static readonly (string Id, string Title, string Prefix)[] KindGroups =
    [
        ("items", "Items", ItemPrefix),
        ("objects", "Objects", ObjectPrefix),
        ("villagers", "Villagers", "villager."),
        ("whistles", "Whistles", "whistle."),
        ("craft", "Crafting recipes", "craft."),
        ("cooking", "Cooking recipes", "cooking."),
        ("sales", "Sales", "sale."),
        ("upgrades", "Upgrades", "upgrade."),
        ("offerings", "Offerings", "offering."),
        ("catches", "Catches", "catch."),
        ("forage", "Forage", "forage."),
        ("crops", "Crops", "crop."),
        ("flags", "Flags", "flag."),
        ("settings", "Settings", "setting."),
        ("textures", "Textures", TexturePrefix),
        ("icons", "Icons", IconPrefix),
        ("skins", "Skins", "skin."),
    ];

    // The gallery of project, in order: the mod, the KindGroups, any kind no
    // group names, Game edits when there are any, then the raw sections.
    // Tiles keep their file order.
    public static IReadOnlyList<ContentGroup> Groups(ModProject project, IReadOnlyList<SectionSchema> kinds)
    {
        List<ContentGroup> groups = [];
        SectionSchema? mod = KindWithPrefix(kinds, "mod");
        groups.Add(new ContentGroup("mod", "Mod", mod, mod is not null && !project.HasSection("mod"), false,
            TilesOf(project, kinds, mod)));
        foreach ((string id, string title, string prefix) in KindGroups)
        {
            SectionSchema? kind = KindWithPrefix(kinds, prefix);
            if (kind is not null)
            {
                groups.Add(new ContentGroup(id, title, kind, true, false, TilesOf(project, kinds, kind)));
            }
        }
        foreach (SectionSchema extra in kinds.Where(kind => !IsNamed(kind.Prefix)))
        {
            groups.Add(new ContentGroup(extra.Prefix.TrimEnd('.'), extra.Title, extra, true, false,
                TilesOf(project, kinds, extra)));
        }
        List<ContentTile> edits = TilesOf(project, kinds, KindWithPrefix(kinds, PatchPrefix));
        if (edits.Count > 0)
        {
            groups.Add(new ContentGroup("edits", "Game edits", null, false, false, edits));
        }
        SectionSchema? row = KindWithPrefix(kinds, RowPrefix);
        List<ContentTile> raw = [];
        foreach (string section in project.SectionNames)
        {
            SectionSchema? kind = SectionNames.KindOf(section, kinds);
            if (kind is null || SectionNames.Same(kind.Prefix, RowPrefix))
            {
                raw.Add(TileOf(project, section, kind));
            }
        }
        groups.Add(new ContentGroup("raw", "Raw sections", row, row is not null, true, raw));
        return groups;
    }

    // The tile of one section, null when the project has no such section.
    public static ContentTile? Tile(ModProject project, string section, IReadOnlyList<SectionSchema> kinds)
    {
        string? found = project.SectionNames.FirstOrDefault(name => SectionNames.Same(name, section));
        return found is null ? null : TileOf(project, found, SectionNames.KindOf(found, kinds));
    }

    // The tiles of the sections of kind, in file order.
    private static List<ContentTile> TilesOf(ModProject project, IReadOnlyList<SectionSchema> kinds,
        SectionSchema? kind)
    {
        if (kind is null)
        {
            return [];
        }
        return project.SectionNames
            .Where(section => ReferenceEquals(SectionNames.KindOf(section, kinds), kind))
            .Select(section => TileOf(project, section, kind))
            .ToList();
    }

    // The tile of a section of kind, where kind is null for a section no
    // kind takes.
    private static ContentTile TileOf(ModProject project, string section, SectionSchema? kind)
    {
        string key = kind is null ? "" : SectionNames.KeyOf(section, kind);
        return new ContentTile(section, kind, TitleOf(project, section, kind, key),
            SubtitleOf(project, section, kind, key), ArtOf(project, kind, section, 0));
    }

    private static string TitleOf(ModProject project, string section, SectionSchema? kind, string key) =>
        kind?.Prefix switch
        {
            "mod" => Text(project.Value(section, "name")) ?? project.Name,
            "item." or "object." or "villager." or "whistle." or "crop." => NameOf(project, section) ?? key,
            "craft." or "cooking." => ProductTitle(project, section, "output", key),
            "sale." => ProductTitle(project, section, "item", key),
            "setting." => Text(project.Value(section, "label")) ?? key,
            "row." or null => $"[{section}]",
            _ => key,
        };

    private static string SubtitleOf(ModProject project, string section, SectionSchema? kind, string key) =>
        kind?.Prefix switch
        {
            "mod" => Text(project.Value(section, "author")) ?? Text(project.Value(section, "version")) ?? "No author",
            "object." => Text(project.Value(section, "clone")) is { } clone ? "Copy of " + clone : "No clone",
            "flag." => OnByDefault(project.Value(section, "default")) ? "On in a new game" : "Off in a new game",
            "setting." => SettingType(project.Value(section, "type")),
            "texture." => FileName(project.Value(section, "file")) ?? "No file",
            "icon." => Text(project.Value(section, "texture")) ?? "No texture",
            "skin." => SwapCount(project.Value(section, "replace")),
            "sale." => Text(project.Value(section, "shop")) is { } shop ? "Shop " + shop : "No shop",
            "upgrade." => Text(project.Value(section, "to")) is { } tool ? "Makes " + tool : "No tool",
            "crop." => Text(project.Value(section, "base")) is { } crop ? "Copy of crop " + crop : "No base",
            "patch." => "Game edit",
            "row." => "Raw row",
            null => "Not read by Canopy",
            _ => key,
        };

    // The picture of a section of kind. steps counts the references already
    // followed to reach it.
    private static TileArt ArtOf(ModProject project, SectionSchema? kind, string section, int steps) =>
        kind?.Prefix switch
        {
            "mod" => GlyphArt("mod"),
            "item." => ItemArt(project, section, steps),
            "villager." => VillagerArt(project, section),
            "whistle." => WhistleArt(project, section, steps),
            "craft." => ProductArt(project, section, "output", "craft", steps),
            "cooking." => ProductArt(project, section, "output", "cooking", steps),
            "sale." => ProductArt(project, section, "item", "sale", steps),
            "upgrade." => ProductArt(project, section, "to", "upgrade", steps),
            "crop." => ProductArt(project, section, "seed", "crop", steps),
            "texture." => TextureArt(project, section),
            "icon." => IconArt(project, section, steps),
            "skin." => SkinArt(project, section, steps),
            "object." => GlyphArt("object"),
            "flag." => GlyphArt("flag"),
            "setting." => GlyphArt("setting"),
            "patch." => GlyphArt("patch"),
            _ => GlyphArt("raw"),
        };

    private static TileArt ItemArt(ModProject project, string section, int steps)
    {
        string? icon = Text(project.Value(section, "icon"));
        if (IdOf(icon) is { } iconId)
        {
            return new TileArt(TileArtKind.VanillaIcon, "item", Id: iconId);
        }
        string? iconSection = OwnSection(project, IconPrefix, icon);
        if (iconSection is not null)
        {
            return steps < MaxSteps ? WithGlyph(IconArt(project, iconSection, steps + 1), "item") : GlyphArt("item");
        }
        if (IdOf(Text(project.Value(section, "base"))) is { } baseId)
        {
            return new TileArt(TileArtKind.VanillaItem, "item", Id: baseId);
        }
        return GlyphArt("item");
    }

    private static TileArt VillagerArt(ModProject project, string section) =>
        IdOf(Text(project.Value(section, "base"))) is { } baseId
            ? new TileArt(TileArtKind.VanillaVillager, "villager", Id: baseId)
            : GlyphArt("villager");

    private static TileArt WhistleArt(ModProject project, string section, int steps)
    {
        string? iconSection = OwnSection(project, IconPrefix, Text(project.Value(section, "icon")));
        return iconSection is not null && steps < MaxSteps
            ? WithGlyph(IconArt(project, iconSection, steps + 1), "whistle")
            : GlyphArt("whistle");
    }

    // The picture of the item that field names: a recipe's output, a sale's
    // item or an upgrade's tool.
    private static TileArt ProductArt(ModProject project, string section, string field, string glyph, int steps)
    {
        string? output = Text(project.Value(section, field));
        string? item = OwnSection(project, ItemPrefix, output);
        if (item is not null)
        {
            return steps < MaxSteps ? WithGlyph(ItemArt(project, item, steps + 1), glyph) : GlyphArt(glyph);
        }
        if (IdOf(output) is { } outputId)
        {
            return new TileArt(TileArtKind.VanillaItem, glyph, Id: outputId);
        }
        return GlyphArt(glyph);
    }

    private static TileArt TextureArt(ModProject project, string section)
    {
        string? file = Text(project.Value(section, "file"));
        string? image = file is null ? null : ModImage(project, file);
        return image is null ? GlyphArt("texture") : new TileArt(TileArtKind.ModImage, "texture", File: image);
    }

    private static TileArt IconArt(ModProject project, string section, int steps)
    {
        string? textureSection = steps < MaxSteps
            ? OwnSection(project, TexturePrefix, Text(project.Value(section, "texture")))
            : null;
        if (textureSection is null)
        {
            return GlyphArt("icon");
        }
        TileArt picture = TextureArt(project, textureSection);
        return picture.Kind == TileArtKind.ModImage
            ? (picture with { Glyph = "icon", Rect = ParseRect(project.Value(section, "rect")) })
            : GlyphArt("icon");
    }

    private static TileArt SkinArt(ModProject project, string section, int steps)
    {
        if (steps < MaxSteps)
        {
            foreach (string swap in ListText.Split(project.Value(section, "replace")))
            {
                // A swap is model: texture, split at the first colon as the loader splits it.
                int colon = swap.IndexOf(':');
                string? textureSection = colon < 0
                    ? null
                    : OwnSection(project, TexturePrefix, Text(swap[(colon + 1)..]));
                if (textureSection is null)
                {
                    continue;
                }
                TileArt picture = TextureArt(project, textureSection);
                if (picture.Kind == TileArtKind.ModImage)
                {
                    return WithGlyph(picture, "skin");
                }
            }
        }
        return GlyphArt("skin");
    }

    // The full path of file when it names an image inside the mod folder,
    // else null. A backslash counts as a slash on every platform, as the
    // game reads it.
    private static string? ModImage(ModProject project, string file)
    {
        string? full = ProjectPaths.Full(project.Folder, file.Replace('\\', '/'));
        string extension = full is null ? "" : Path.GetExtension(full) ?? "";
        return ImageExtensions.Contains(extension, StringComparer.OrdinalIgnoreCase) ? full : null;
    }

    // The section of prefix that value names: the bare key, or Mod.Key with
    // the project's mod key, matched without case. Null when there is none.
    private static string? OwnSection(ModProject project, string prefix, string? value)
    {
        if (value is null)
        {
            return null;
        }
        string? bare = Find(project, prefix + value);
        if (bare is not null)
        {
            return bare;
        }
        string mod = ProjectKeys.ModKeyOf(project.Structure, project.Name);
        bool qualified = mod.Length > 0 && value.Length > mod.Length + 1 && value[mod.Length] == '.'
            && SectionNames.Same(value[..mod.Length], mod);
        return qualified ? Find(project, prefix + value[(mod.Length + 1)..]) : null;
    }

    // The section named name, matched without case, or null.
    private static string? Find(ModProject project, string name) =>
        project.SectionNames.FirstOrDefault(section => SectionNames.Same(section, name));

    // The title of an item: its name, else its key.
    private static string ItemTitle(ModProject project, string item) =>
        NameOf(project, item) ?? item[ItemPrefix.Length..];

    // The title of a recipe or a sale: the title of the item field names when
    // it is an own item, else the key.
    private static string ProductTitle(ModProject project, string section, string field, string key)
    {
        string? item = OwnSection(project, ItemPrefix, Text(project.Value(section, field)));
        return item is null ? key : ItemTitle(project, item);
    }

    // The name of an item, villager or whistle: name, else the first
    // language name in LangKeys order.
    private static string? NameOf(ModProject project, string section)
    {
        string? name = Text(project.Value(section, "name"));
        if (name is not null)
        {
            return name;
        }
        foreach (string key in LangKeys.Keys("name"))
        {
            string? translated = Text(project.Value(section, key));
            if (translated is not null)
            {
                return translated;
            }
        }
        return null;
    }

    private static SectionSchema? KindWithPrefix(IReadOnlyList<SectionSchema> kinds, string prefix) =>
        kinds.FirstOrDefault(kind => SectionNames.Same(kind.Prefix, prefix));

    // Whether a kind with prefix has a group of its own: the mod, the
    // KindGroups, and the row and patch kinds, which have their own groups.
    private static bool IsNamed(string prefix) =>
        SectionNames.Same(prefix, "mod") || SectionNames.Same(prefix, RowPrefix)
        || SectionNames.Same(prefix, PatchPrefix) || KindGroups.Any(group => SectionNames.Same(group.Prefix, prefix));

    // The value without its ends, or null when it is blank.
    private static string? Text(string? value) => string.IsNullOrWhiteSpace(value) ? null : value.Trim();

    // The vanilla id a value names, when it is digits only.
    private static ulong? IdOf(string? value) => References.Digits(value ?? "");

    // Whether a flag starts on: its default is on, true or 1, in any case.
    private static bool OnByDefault(string? value)
    {
        string text = Text(value) ?? "";
        return SectionNames.Same(text, "on") || SectionNames.Same(text, "true") || text == "1";
    }

    // The subtitle of a setting: Slider or Choice from its type, else Toggle.
    private static string SettingType(string? type)
    {
        string text = Text(type) ?? "";
        if (SectionNames.Same(text, "slider"))
        {
            return "Slider";
        }
        return SectionNames.Same(text, "choice") ? "Choice" : "Toggle";
    }

    // "1 swap", or "N swaps" for any other count of the entries of replace.
    private static string SwapCount(string? replace)
    {
        int count = ListText.Split(replace).Count;
        return count == 1 ? "1 swap" : $"{count} swaps";
    }

    // The file name at the end of a path written with either slash, or null
    // when there is none.
    private static string? FileName(string? file)
    {
        string? path = Text(file);
        string name = path is null ? "" : GamePaths.FileName(path.Replace('\\', '/'));
        return name.Length == 0 ? null : name;
    }

    // rect as x, y, w, h: four whole numbers separated by commas. A w or h of
    // 0 or less gives null, and so does anything else.
    private static TileRect? ParseRect(string? text)
    {
        string[] parts = (text ?? "").Split(',');
        if (parts.Length != 4)
        {
            return null;
        }
        int[] numbers = new int[4];
        for (int i = 0; i < numbers.Length; i++)
        {
            if (!int.TryParse(parts[i], NumberStyles.Integer, CultureInfo.InvariantCulture, out numbers[i]))
            {
                return null;
            }
        }
        return numbers[2] > 0 && numbers[3] > 0
            ? new TileRect(numbers[0], numbers[1], numbers[2], numbers[3])
            : null;
    }

    private static TileArt GlyphArt(string glyph) => new(TileArtKind.Glyph, glyph);

    // The picture of another tile, drawn under this tile's glyph.
    private static TileArt WithGlyph(TileArt picture, string glyph) => picture with { Glyph = glyph };
}
