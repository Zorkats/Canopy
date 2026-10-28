using System.Collections.ObjectModel;
using System.Globalization;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// One group of the Create gallery: its header, and its tiles with the "+ New"
// tile last when the group can add. Update takes a fresh catalog group and
// keeps the tiles that did not change, so the gallery does not flicker.
public sealed partial class ContentGroupViewModel : ObservableObject
{
    private readonly ContentImages images;
    private readonly Action<ContentTileViewModel> pick;
    private readonly Action<ContentGroupViewModel> startNew;
    private ContentTileViewModel? newTile;

    public ContentGroupViewModel(string id, ContentImages images, Action<ContentTileViewModel> pick,
        Action<ContentGroupViewModel> startNew)
    {
        Id = id;
        this.images = images;
        this.pick = pick;
        this.startNew = startNew;
    }

    public string Id { get; }

    [ObservableProperty]
    private string title = "";

    // The number of real tiles, in the invariant culture.
    [ObservableProperty]
    private string count = "0";

    [ObservableProperty]
    private bool canAdd;

    [ObservableProperty]
    private bool advanced;

    // The kind "+ New" adds; null when the group cannot add.
    [ObservableProperty]
    private SectionSchema? kind;

    public ObservableCollection<ContentTileViewModel> Tiles { get; } = [];

    // Takes group's title, kind and tiles. A tile whose record is equal to one
    // already shown keeps its view model.
    public void Update(ContentGroup group)
    {
        Title = group.Title;
        Kind = group.Kind;
        CanAdd = group.CanAdd;
        Advanced = group.Advanced;
        Count = group.Tiles.Count.ToString(CultureInfo.InvariantCulture);
        List<ContentTileViewModel> next = [];
        foreach (ContentTile tile in group.Tiles)
        {
            next.Add(Tiles.FirstOrDefault(known => known.Tile == tile) ?? new ContentTileViewModel(tile, images, pick));
        }
        if (CanAdd && Kind is { } addKind)
        {
            if (newTile is null || !ReferenceEquals(newTile.Kind, addKind))
            {
                newTile = new ContentTileViewModel(addKind, images, _ => AddNew());
            }
            next.Add(newTile);
        }
        CollectionSync.To(Tiles, next);
    }

    [RelayCommand]
    private void AddNew()
    {
        if (Kind is not null)
        {
            startNew(this);
        }
    }
}
