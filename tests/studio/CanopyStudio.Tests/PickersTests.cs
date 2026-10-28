using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Pickers;

namespace CanopyStudio.Tests;

// The picker's search, order and groups, on made-up entries.
public sealed class PickersTests
{
    private static VanillaEntry Item(ulong id, string name, string? enumName = null, ulong? group = null) =>
        new(VanillaKind.Item, id, enumName, name, null, group);

    private static readonly VanillaEntry[] Entries =
    [
        Item(10, "Pebble", "ITEM_PEBBLE", 1),
        Item(3, "Apple", "ITEM_APPLE", 2),
        Item(12, "Crème Brûlée", "ITEM_CREME", 2),
        Item(120, "Red Apple", "ITEM_RED_APPLE", 2),
        Item(7, "Œuvre Stone", "ITEM_STONE_ART", 1),
        Item(44, "Twig", "ITEM_TWIG"),
    ];

    private static List<ulong> Ids(IEnumerable<PickerItem> items) => items.Select(item => item.Id).ToList();

    [Fact]
    public void EmptySearchListsEveryEntryByName()
    {
        PickerQuery query = new(Entries);
        Assert.Equal(6, query.Count);
        // Œuvre sorts as "oeuvre", between Crème and Pebble.
        Assert.Equal([3UL, 12, 7, 10, 120, 44], Ids(query.Find("")));
        Assert.Equal(Ids(query.Find("")), Ids(query.Find(null)));
        Assert.Equal(Ids(query.Find("")), Ids(query.Find("   ")));
    }

    [Fact]
    public void OrderDoesNotDependOnInputOrder()
    {
        PickerQuery forward = new(Entries);
        PickerQuery backward = new(Entries.Reverse());
        PickerQuery shuffled = new([Entries[4], Entries[1], Entries[5], Entries[0], Entries[3], Entries[2]]);
        foreach (string search in (string[])["", "apple", "1", "item", "stone"])
        {
            Assert.Equal(Ids(forward.Find(search)), Ids(backward.Find(search)));
            Assert.Equal(Ids(forward.Find(search)), Ids(shuffled.Find(search)));
        }
    }

    [Fact]
    public void SearchMatchesNameWithoutCase()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([3UL, 120], Ids(query.Find("APPLE")));
        Assert.Equal([10UL], Ids(query.Find("pEbB")));
        Assert.Empty(query.Find("banana"));
    }

    [Fact]
    public void SearchIgnoresAccentsBothWays()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([12UL], Ids(query.Find("creme brulee")));
        Assert.Equal([12UL], Ids(query.Find("BRÛLÉE")));
        Assert.Equal([7UL], Ids(query.Find("oeuvre")));
        Assert.Equal("creme", PickerQuery.Fold("Crème"));
        Assert.Equal("strasse", PickerQuery.Fold("Straße"));
    }

    [Fact]
    public void SearchMatchesEnumNames()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([7UL], Ids(query.Find("STONE_ART")));
        Assert.Equal([7UL], Ids(query.Find("stone art")));
        Assert.Equal([120UL], Ids(query.Find("item_red")));
        Assert.Equal(6, query.Find("item").Count);
    }

    [Fact]
    public void EveryWordMustMatch()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([120UL], Ids(query.Find("apple red")));
        Assert.Empty(query.Find("apple pebble"));
    }

    [Fact]
    public void SearchMatchesIdsWithTheExactIdFirst()
    {
        PickerQuery query = new(Entries);
        // 12 is exact; 120 contains it; Pebble is 10, Apple 3: neither.
        Assert.Equal([12UL, 120], Ids(query.Find("12")));
        Assert.Equal([12UL, 120], Ids(query.Find("#12")));
        Assert.Equal([120UL], Ids(query.Find("120")));
        Assert.Equal([44UL], Ids(query.Find("44")));
        // No exact match: every id with a 1 in it, by name.
        Assert.Equal([12UL, 10, 120], Ids(query.Find("1")));
    }

    [Fact]
    public void AnExactIdBeatsANameThatStartsWithTheSearch()
    {
        VanillaEntry[] entries = [Item(5, "Zebra Stone"), Item(55, "5 Pebbles"), Item(500, "Five")];
        PickerQuery query = new(entries);
        Assert.Equal([5UL, 55, 500], Ids(query.Find("5")));
        Assert.Equal([500UL], Ids(query.Find("500")));
    }

    [Fact]
    public void NamesThatStartWithTheSearchComeBeforeOtherMatches()
    {
        VanillaEntry[] entries = [Item(1, "Big Apple"), Item(2, "Apple Pie"), Item(3, "Apple")];
        PickerQuery query = new(entries);
        Assert.Equal([3UL, 2, 1], Ids(query.Find("apple")));
    }

    [Fact]
    public void EmptyNamesShowTheEnumNameOrTheId()
    {
        VanillaEntry[] entries =
        [
            new(VanillaKind.Villager, 4, "VILLAGER_GUEST", "", null, null),
            new(VanillaKind.Villager, 9, null, " ", null, null),
            new(VanillaKind.Villager, 2, "VILLAGER_HOST", "Hazel", null, null),
        ];
        PickerQuery query = new(entries);
        PickerItem guest = query.ById(4)!;
        Assert.Equal("VILLAGER_GUEST", guest.Name);
        Assert.False(guest.HasName);
        PickerItem nameless = query.ById(9)!;
        Assert.Equal("#9", nameless.Name);
        Assert.False(nameless.HasName);
        Assert.True(query.ById(2)!.HasName);
        Assert.Equal("#9", nameless.IdText);
        // The shown names are searchable too.
        Assert.Equal([4UL], Ids(query.Find("guest")));
        Assert.Equal([9UL], Ids(query.Find("#9")));
    }

    [Fact]
    public void DuplicateNamesStayApartAndOrderedById()
    {
        VanillaEntry[] entries = [Item(30, "Pebble"), Item(8, "Pebble"), Item(19, "Pebble"), Item(2, "Apple")];
        PickerQuery query = new(entries);
        Assert.Equal([2UL, 8, 19, 30], Ids(query.Find("")));
        Assert.Equal([8UL, 19, 30], Ids(query.Find("pebble")));
        Assert.Equal([19UL], Ids(query.Find("19")));
    }

    [Fact]
    public void GroupsAreTheDistinctGroupsWithCounts()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([new PickerGroupCount(1, 2), new PickerGroupCount(2, 3)], query.Groups);
        Assert.Empty(new PickerQuery([Item(1, "Twig")]).Groups);
        Assert.Empty(new PickerQuery([]).Groups);
    }

    [Fact]
    public void GroupFilterNarrowsListAndSearch()
    {
        PickerQuery query = new(Entries);
        Assert.Equal([3UL, 12, 120], Ids(query.Find("", 2)));
        Assert.Equal([7UL, 10], Ids(query.Find(null, 1)));
        Assert.Equal([3UL, 120], Ids(query.Find("apple", 2)));
        Assert.Empty(query.Find("apple", 1));
        Assert.Empty(query.Find("", 99));
    }

    [Fact]
    public void GroupLabelsComeFromTheHostOrFallBack()
    {
        Assert.Equal("Group 3", PickerQuery.GroupLabel(3, null));
        Assert.Equal("Group 3", PickerQuery.GroupLabel(3, _ => null));
        Assert.Equal("Group 3", PickerQuery.GroupLabel(3, _ => ""));
        Assert.Equal("Tools", PickerQuery.GroupLabel(3, group => group == 3 ? "Tools" : null));
    }

    [Fact]
    public void ByIdFindsEntriesAndMissesUnknownIds()
    {
        PickerQuery query = new(Entries);
        Assert.Equal("Apple", query.ById(3)!.Name);
        Assert.Null(query.ById(999));
    }

    [Fact]
    public void FoldKeepsKanaVoicingAndFoldsFullWidth()
    {
        // A voiced kana stays apart from its plain form; full width Latin
        // letters and digits read as plain ones.
        Assert.NotEqual(PickerQuery.Fold("が"), PickerQuery.Fold("か"));
        Assert.Equal("が", PickerQuery.Fold("が"));
        Assert.Equal("ab12", PickerQuery.Fold("Ａｂ１２"));
    }
}
