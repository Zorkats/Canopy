using CanopyStudio.Core.Pickers;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.Controls.Pickers;

// What a VanillaPickerButton shows for its id: the entry's icon, name and
// id; "None" when nothing is picked; and an id the game's list does not
// have (a modded or removed row) as itself, never dropped.
public sealed class VanillaPickerButtonViewModel : ObservableObject
{
    private ulong? selectedId;
    private PickerItem? entry;

    public VanillaPickerButtonViewModel() => Entries.Updated += (_, _) => Resolve();

    internal PickerEntries Entries { get; } = new();

    public IGameAssets? Assets { get; private set; }

    public ulong? SelectedId
    {
        get => selectedId;
        set
        {
            selectedId = value;
            Resolve();
        }
    }

    public bool HasEntry => entry is not null;

    // An id is picked, so Clear has something to remove.
    public bool HasSelection => selectedId is not null;
    public ulong? IconId => entry?.Entry.Icon;

    public string NameText => (selectedId, entry) switch
    {
        (null, _) => "None",
        (_, { } found) => found.Name,
        // Only a loaded, non-empty list can say the game has no such row;
        // with no game, or while loading, the id is all there is to show.
        ({ } id, null) when Entries.Query is { Count: > 0 } => $"{PickerQuery.IdText(id)}, not in the game",
        ({ } id, null) => PickerQuery.IdText(id),
    };

    // Grey for "None", an entry the game names nothing, and an id the list lacks.
    public bool IsNameMuted => entry is not { HasName: true };

    public string IdText => entry?.IdText ?? "";

    public void UseAssets(IGameAssets? assets)
    {
        Assets = assets;
        OnPropertyChanged(nameof(Assets));
    }

    private void Resolve()
    {
        entry = selectedId is { } id ? Entries.Query?.ById(id) : null;
        foreach (string name in (string[])[nameof(HasEntry), nameof(HasSelection), nameof(IconId), nameof(NameText),
                     nameof(IsNameMuted), nameof(IdText)])
        {
            OnPropertyChanged(name);
        }
    }
}
