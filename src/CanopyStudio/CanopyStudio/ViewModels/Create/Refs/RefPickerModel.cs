using System.Globalization;
using CanopyStudio.Controls.Pickers;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Pickers;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Create.Refs;

// One row of a reference picker: the value a pick writes, what it is shown
// as, and its game icon when it has one.
public sealed record RefChoice(string Value, string Title, string Detail, ulong? Icon, bool HasName = true);

// Picks one reference value: a key of the project or another mod, or what
// the rules take from the game (an item from the game's list, an icon some
// game item uses, or a typed id or name). It shows the value it was given as
// it is, with a warning when it names nothing, and writes only when the user
// picks or clears.
public sealed partial class RefPickerModel : ObservableObject
{
    // write sets the value (null removes it) and returns the value now in
    // the project, which stays the old one when the change was refused.
    private readonly Func<string?, string?> write;
    private ProjectKeys keys;
    private string? value;
    private RefResolution resolution = new(RefStatus.Empty);
    // The icons game items use, each with the first item that uses it.
    private Dictionary<ulong, PickerItem>? icons;
    private PickerQuery? iconsOf;

    [ObservableProperty]
    private string search = "";

    [ObservableProperty]
    private IReadOnlyList<RefChoice> choices = [];

    [ObservableProperty]
    private RefChoice? highlighted;

    public RefPickerModel(RefRules rules, ProjectKeys keys, IGameAssets assets, string? value,
        Func<string?, string?> write)
    {
        Rules = rules;
        Assets = assets;
        this.keys = keys;
        this.value = value;
        this.write = write;
        if (rules is { Kind: RefKind.Item } or { Kind: RefKind.Icon, VanillaIds: true })
        {
            Entries = new PickerEntries();
            Entries.Updated += (_, _) => Refresh();
            Entries.Use(assets, VanillaKind.Item);
        }
        Refresh();
    }

    public RefRules Rules { get; }
    public IGameAssets Assets { get; }

    // The game's items, for item and icon pickers; null for the others.
    internal PickerEntries? Entries { get; }

    public string? Value => value;
    public bool HasValue => !string.IsNullOrEmpty(value);

    // The field: the value's name, where it comes from, its icon, and why it
    // names nothing when it does not.
    public string Title { get; private set; } = "";
    public string Detail { get; private set; } = "";
    public ulong? IconId { get; private set; }
    public bool HasIcon => IconId is not null;
    public bool IsTitleMuted { get; private set; }
    public string? Warning { get; private set; }
    public bool HasWarning => Warning is not null;

    // A line above the list about what the game offers.
    public string? Note => Rules switch
    {
        { Kind: RefKind.Flag } =>
            "Game flags have no names, so a game flag is picked by typing its id, 1 to 94999. docs/FLAGS.md explains flag ids.",
        { Kind: RefKind.Texture } =>
            "A game texture is picked by typing its id or its TEX_ID_ name. docs/ART.md explains texture names.",
        { Kind: RefKind.Icon, VanillaIds: true } => "Game icons are the ones game items use. Search by an item's name or type an icon id.",
        { Kind: RefKind.Icon } => "Only a mod icon can go here. The whistle's score picks a game icon.",
        { Groups: true } => "Type group:<number> for any item of a group a vanilla cooking recipe uses, such as group:11.",
        _ => null,
    };

    public bool HasNote => Note is not null;

    public string Watermark => Rules.VanillaIds ? "Search, or type a key, Mod.Key or id" : "Search, or type a key or Mod.Key";

    public bool NoChoices => Choices.Count == 0;

    public string EmptyText => Entries is { IsLoading: true }
        ? "Reading the game's list..."
        : Rules.VanillaIds
            ? "Nothing matches. Type a key, a Mod.Key or an id."
            : $"Nothing matches. Add {Rules.Noun} to this mod first, or type another mod's Mod.Key.";

    public void Attach() => Entries?.Attach();

    public void Detach() => Entries?.Detach();

    // The value changed in the project: show it, never write it.
    public void Show(string? newValue)
    {
        value = newValue;
        Resolve();
    }

    // The project's keys changed, or other mods were read again.
    public void UseKeys(ProjectKeys newKeys)
    {
        keys = newKeys;
        Refresh();
    }

    public void Pick(RefChoice choice) => Show(write(choice.Value));

    public void Clear() => Show(write(null));

    // Moves the highlight by delta rows, stopping at either end.
    public void Move(int delta)
    {
        if (Choices.Count == 0)
        {
            return;
        }
        int index = Highlighted is null ? -1 : IndexOf(Highlighted);
        Highlighted = Choices[index < 0 ? (delta > 0 ? 0 : Choices.Count - 1) : Math.Clamp(index + delta, 0, Choices.Count - 1)];
    }

    partial void OnSearchChanged(string value) => RefreshChoices();

    partial void OnChoicesChanged(IReadOnlyList<RefChoice> value)
    {
        OnPropertyChanged(nameof(NoChoices));
        OnPropertyChanged(nameof(EmptyText));
    }

    private void Refresh()
    {
        RefreshChoices();
        Resolve();
    }

    private void RefreshChoices()
    {
        string text = Search.Trim();
        string[] words = PickerQuery.Fold(text).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        List<RefChoice> list = [];
        foreach (ModPart part in keys.Of(Rules.Kind))
        {
            if (words.All(word => PickerQuery.Fold(part.Value).Contains(word, StringComparison.Ordinal)))
            {
                list.Add(new RefChoice(part.Value, part.Value, part.Own ? "This mod" : "Mod " + part.Mod, null));
            }
        }

        // What was typed, when the rules take it as it is.
        List<RefChoice> game = GameChoices(text);
        if (text.Length > 0 && !list.Concat(game).Any(choice => string.Equals(choice.Value, text, StringComparison.OrdinalIgnoreCase))
            && Typed(text) is { } typed)
        {
            list.Add(typed);
        }
        list.AddRange(game);
        Choices = list;
        Highlighted = list.FirstOrDefault(choice => string.Equals(choice.Value, value, StringComparison.OrdinalIgnoreCase))
            is { } current && text.Length == 0 ? current : list.FirstOrDefault();
    }

    private List<RefChoice> GameChoices(string text)
    {
        if (Entries?.Query is not { } query)
        {
            return [];
        }
        if (Rules.Kind == RefKind.Item)
        {
            return query.Find(text).Select(item =>
                new RefChoice(Id(item.Id), item.Name, item.IdText, item.Entry.Icon, item.HasName)).ToList();
        }
        // An icon: one row per icon among the matching items.
        Dictionary<ulong, PickerItem> uses = IconUses(query);
        HashSet<ulong> seen = [];
        List<RefChoice> list = [];
        if (References.Digits(text) is { } typed && uses.TryGetValue(typed, out PickerItem? exact) && seen.Add(typed))
        {
            list.Add(IconChoice(typed, exact));
        }
        foreach (PickerItem item in query.Find(text))
        {
            if (item.Entry.Icon is { } icon && seen.Add(icon))
            {
                list.Add(IconChoice(icon, item));
            }
        }
        return list;
    }

    private RefChoice? Typed(string text)
    {
        RefResolution typed = References.Resolve(Rules, keys, text);
        return typed.Status switch
        {
            RefStatus.Vanilla when Rules.Kind == RefKind.Flag => new(text, $"Game flag {text}", "Typed id", null, false),
            RefStatus.Vanilla when Rules.Kind == RefKind.Texture => new(text, text, "Game texture", null, false),
            RefStatus.Vanilla => new(text, PickerQuery.IdText(typed.Id ?? 0), "Typed id", Rules.Kind == RefKind.Icon ? typed.Id : null, false),
            RefStatus.Group => new(text, text, "Cooking group", null, false),
            RefStatus.Unchecked => new(text, text, "Another mod", null, false),
            _ => null,
        };
    }

    private void Resolve()
    {
        resolution = References.Resolve(Rules, keys, value);
        string shown = value ?? "";
        IconId = null;
        IsTitleMuted = false;
        Warning = null;
        switch (resolution.Status)
        {
            case RefStatus.Empty:
                (Title, Detail, IsTitleMuted) = ("None", "", true);
                break;
            case RefStatus.Own:
                (Title, Detail) = (resolution.Part!.Key, "This mod");
                break;
            case RefStatus.OtherMod:
                (Title, Detail) = (resolution.Part!.Value, "Mod " + resolution.Part.Mod);
                break;
            case RefStatus.Unchecked:
                (Title, Detail) = (shown, "Another mod, checked once the game folder is open");
                break;
            case RefStatus.Group:
                (Title, Detail) = (shown, "Cooking group");
                break;
            case RefStatus.Vanilla:
                ResolveVanilla(shown);
                break;
            default:
                (Title, Detail, Warning) = (shown, "", resolution.Problem);
                break;
        }
        foreach (string name in (string[])[nameof(Value), nameof(HasValue), nameof(Title), nameof(Detail), nameof(IconId),
                     nameof(HasIcon), nameof(IsTitleMuted), nameof(Warning), nameof(HasWarning)])
        {
            OnPropertyChanged(name);
        }
    }

    // A game id or name: checked against the game's list when the field has
    // one and it is loaded; else shown as it is.
    private void ResolveVanilla(string shown)
    {
        ulong id = resolution.Id ?? 0;
        PickerQuery? query = Entries?.Query is { Count: > 0 } loaded ? loaded : null;
        switch (Rules.Kind)
        {
            case RefKind.Flag:
                (Title, Detail) = ($"Game flag {shown}", "Game flags have no names");
                break;
            case RefKind.Texture:
                (Title, Detail) = (shown, "Game texture");
                break;
            case RefKind.Item when query?.ById(id) is { } item:
                (Title, Detail, IconId, IsTitleMuted) = (item.Name, item.IdText, item.Entry.Icon, !item.HasName);
                break;
            case RefKind.Icon when query is not null && IconUses(query).ContainsKey(id):
                (Title, Detail, IconId) = (PickerQuery.IdText(id), "Game icon", id);
                break;
            case RefKind.Icon when query is null:
                (Title, Detail, IconId) = (PickerQuery.IdText(id), "Game icon", id);
                break;
            case RefKind.Icon:
                (Title, Detail, Warning) = (shown, "", $"No game item uses icon {shown}, so the base item's icon stays.");
                break;
            case RefKind.Item when query is null:
                (Title, Detail) = (PickerQuery.IdText(id), "Game item");
                break;
            default:
                (Title, Detail, Warning) = (shown, "", Rules.NotFound);
                break;
        }
    }

    private Dictionary<ulong, PickerItem> IconUses(PickerQuery query)
    {
        if (icons is null || !ReferenceEquals(iconsOf, query))
        {
            icons = [];
            foreach (PickerItem item in query.Find(null))
            {
                if (item.Entry.Icon is { } icon)
                {
                    icons.TryAdd(icon, item);
                }
            }
            iconsOf = query;
        }
        return icons;
    }

    private static RefChoice IconChoice(ulong icon, PickerItem item) =>
        new(Id(icon), PickerQuery.IdText(icon), "Used by " + item.Name, icon);

    private static string Id(ulong id) => id.ToString(CultureInfo.InvariantCulture);

    private int IndexOf(RefChoice choice)
    {
        for (int index = 0; index < Choices.Count; ++index)
        {
            if (Choices[index] == choice)
            {
                return index;
            }
        }
        return -1;
    }
}
