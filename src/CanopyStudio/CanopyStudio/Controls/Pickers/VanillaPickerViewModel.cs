using CanopyStudio.Core.Pickers;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.Controls.Pickers;

// A choice in the group filter: one group, or every group when Group is null.
public sealed record PickerGroupOption(ulong? Group, string Label, int Count)
{
    public string Text => $"{Label} ({Count:N0})";
}

// What a VanillaPicker shows: the search, the group filter, the matching
// rows and the highlighted one. Highlighting is moving through the list;
// picking (Pick) is choosing, and raises Picked.
public sealed partial class VanillaPickerViewModel : ObservableObject
{
    private Func<ulong, string?>? groupLabels;
    private ulong? selectedId;

    [ObservableProperty]
    private string search = "";

    [ObservableProperty]
    private IReadOnlyList<PickerItem> items = [];

    [ObservableProperty]
    private PickerItem? highlighted;

    [ObservableProperty]
    private IReadOnlyList<PickerGroupOption> groups = [];

    [ObservableProperty]
    private PickerGroupOption? selectedGroup;

    public VanillaPickerViewModel() => Entries.Updated += (_, _) => OnEntriesUpdated();

    internal PickerEntries Entries { get; } = new();

    // The loading line shows only when there is nothing older to show.
    public bool IsLoading => Entries.IsLoading && Entries.Query is null;
    public bool NeedsGame => Entries.IsEmpty;
    public bool HasItems => Items.Count > 0;
    public bool NoMatches => Entries.Query is { Count: > 0 } && Items.Count == 0;
    // "All groups" plus at least one real group.
    public bool HasGroups => Groups.Count > 1;

    // The id picked so far; highlighted when the list shows it.
    public ulong? SelectedId
    {
        get => selectedId;
        set
        {
            selectedId = value;
            HighlightBest();
        }
    }

    public Func<ulong, string?>? GroupLabels
    {
        get => groupLabels;
        set
        {
            groupLabels = value;
            RebuildGroups();
        }
    }

    public event EventHandler<PickerItem>? Picked;

    // Moves the highlight by delta rows, stopping at either end.
    public void Move(int delta)
    {
        if (Items.Count == 0)
        {
            return;
        }
        int index = IndexOf(Highlighted);
        int next = index < 0 ? (delta > 0 ? 0 : Items.Count - 1) : Math.Clamp(index + delta, 0, Items.Count - 1);
        Highlighted = Items[next];
    }

    public void MoveToEnd(bool last)
    {
        if (Items.Count > 0)
        {
            Highlighted = Items[last ? Items.Count - 1 : 0];
        }
    }

    // Picks the highlighted row.
    public void Pick()
    {
        if (Highlighted is not { } item)
        {
            return;
        }
        selectedId = item.Id;
        Picked?.Invoke(this, item);
    }

    partial void OnSearchChanged(string value) => Refresh();

    partial void OnSelectedGroupChanged(PickerGroupOption? value) => Refresh();

    private void OnEntriesUpdated()
    {
        RebuildGroups();
        Refresh();
        OnPropertyChanged(nameof(IsLoading));
        OnPropertyChanged(nameof(NeedsGame));
    }

    private void RebuildGroups()
    {
        ulong? kept = SelectedGroup?.Group;
        PickerQuery? query = Entries.Query;
        List<PickerGroupOption> options = [new(null, "All groups", query?.Count ?? 0)];
        if (query is not null)
        {
            options.AddRange(query.Groups.Select(group =>
                new PickerGroupOption(group.Group, PickerQuery.GroupLabel(group.Group, groupLabels), group.Count)));
        }
        Groups = options;
        SelectedGroup = options.Find(option => option.Group == kept) ?? options[0];
        OnPropertyChanged(nameof(HasGroups));
    }

    private void Refresh()
    {
        Items = Entries.Query?.Find(Search, SelectedGroup?.Group) ?? [];
        HighlightBest();
        OnPropertyChanged(nameof(HasItems));
        OnPropertyChanged(nameof(NoMatches));
    }

    // With no search, the picked row; while searching, the best match.
    private void HighlightBest()
    {
        PickerItem? picked = null;
        if (string.IsNullOrWhiteSpace(Search) && selectedId is { } id)
        {
            picked = Items.FirstOrDefault(item => item.Id == id);
        }
        Highlighted = picked ?? Items.FirstOrDefault();
    }

    private int IndexOf(PickerItem? item)
    {
        if (item is null)
        {
            return -1;
        }
        for (int index = 0; index < Items.Count; ++index)
        {
            if (Items[index].Id == item.Id)
            {
                return index;
            }
        }
        return -1;
    }
}
