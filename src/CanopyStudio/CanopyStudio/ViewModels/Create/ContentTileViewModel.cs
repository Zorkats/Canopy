using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// One tile of the Create gallery: a section of canopy.ini, or the "+ New" tile
// of a group, which has no Tile. Errors and Warnings count the last check's
// messages about the section; the project view model sets them.
public sealed partial class ContentTileViewModel : ObservableObject
{
    public ContentTileViewModel(ContentTile tile, ContentImages images, Action<ContentTileViewModel> pick)
    {
        Tile = tile;
        Images = images;
        Section = tile.Section;
        Kind = tile.Kind;
        Title = tile.Title;
        Subtitle = tile.Subtitle;
        Art = tile.Art;
        OpenCommand = new RelayCommand(() => pick(this));
    }

    // The "+ New" tile of a group whose kind is kind.
    public ContentTileViewModel(SectionSchema kind, ContentImages images, Action<ContentTileViewModel> pick)
    {
        Images = images;
        Kind = kind;
        Title = $"New {kind.Title.ToLowerInvariant()}";
        Subtitle = "";
        OpenCommand = new RelayCommand(() => pick(this));
    }

    // The section's tile in the catalog; null for the "+ New" tile.
    public ContentTile? Tile { get; }
    // The section's name, such as item.Moss; null for the "+ New" tile.
    public string? Section { get; }
    // The kind of the section, or the kind "+ New" adds; null for a section no kind takes.
    public SectionSchema? Kind { get; }
    public string Title { get; }
    public string Subtitle { get; }
    // The picture the tile draws; null for the "+ New" tile.
    public TileArt? Art { get; }
    public ContentImages Images { get; }
    public bool IsNew => Tile is null;

    // The kind's title, or "Raw section" for a section no kind takes.
    public string KindTitle => Kind?.Title ?? "Raw section";

    // What a screen reader says: "Moss, Item", or "New item" for the "+ New" tile.
    public string AccessibleName => IsNew ? Title : $"{Title}, {KindTitle}";

    [ObservableProperty]
    private bool isSelected;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasErrors))]
    [NotifyPropertyChangedFor(nameof(HasWarningsOnly))]
    [NotifyPropertyChangedFor(nameof(HasProblems))]
    [NotifyPropertyChangedFor(nameof(ProblemText))]
    private int errors;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasWarningsOnly))]
    [NotifyPropertyChangedFor(nameof(HasProblems))]
    [NotifyPropertyChangedFor(nameof(ProblemText))]
    private int warnings;

    public bool HasErrors => Errors > 0;
    public bool HasWarningsOnly => Errors == 0 && Warnings > 0;
    public bool HasProblems => Errors > 0 || Warnings > 0;

    // The marker's tooltip, such as "2 errors, 1 warning".
    public string ProblemText => ProblemCount.Summary(Errors, Warnings);

    public IRelayCommand OpenCommand { get; }
}
