using System.ComponentModel;
using CanopyStudio.Core.Assets;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

// One kind a list shows, and the table its rows live in.
public sealed record ListKind(VanillaKind Kind, string Title, string Table)
{
    public override string ToString() => Title;
}

// The Game area's lists: the game's own items, villagers, whistles and
// recipes with their names and icons, searchable, each a way into its row
// in Tables. Read only.
public sealed partial class ListsViewModel : PageViewModel
{
    private readonly IStudioNavigator navigator;

    [ObservableProperty]
    private ListKind selectedKind;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasPick))]
    [NotifyPropertyChangedFor(nameof(PickText))]
    [NotifyCanExecuteChangedFor(nameof(OpenRowCommand))]
    private ulong? pickedId;

    public ListsViewModel(IGameAssets assets, IStudioNavigator navigator)
    {
        Assets = assets;
        this.navigator = navigator;
        Kinds =
        [
            new(VanillaKind.Item, "Items", "item.dat"),
            new(VanillaKind.Villager, "Villagers", "character.dat"),
            new(VanillaKind.Whistle, "Whistles", "whistle.dat"),
            new(VanillaKind.Craft, "Crafting", "craft.dat"),
            new(VanillaKind.Cooking, "Cooking", "cooking.dat"),
            new(VanillaKind.Gimmick, "Furniture", "gimmick.dat"),
        ];
        selectedKind = Kinds[0];
        assets.PropertyChanged += OnAssetsChanged;
    }

    public override string Id => PageIds.Lists;
    public override string Title => "Lists";

    public IGameAssets Assets { get; }
    public IReadOnlyList<ListKind> Kinds { get; }
    public IReadOnlyList<GameLanguage> Languages => VanillaKinds.Languages;

    // The language names are shown in; it is kept in the settings.
    public GameLanguage? Language
    {
        get => Languages.FirstOrDefault(language => language.Code == Assets.Language);
        set
        {
            if (value is not null)
            {
                Assets.Language = value.Code;
            }
        }
    }

    public bool HasPick => PickedId is not null;

    public string PickText => PickedId is { } id ? $"Row #{id} in {SelectedKind.Table}" : "";

    [RelayCommand(CanExecute = nameof(HasPick))]
    private void OpenRow() => navigator.OpenTable(SelectedKind.Table, PickedId);

    partial void OnSelectedKindChanged(ListKind value) => PickedId = null;

    private void OnAssetsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(IGameAssets.Language))
        {
            OnPropertyChanged(nameof(Language));
        }
    }
}
