using CanopyStudio.Services;
using CanopyStudio.ViewModels.Game;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels;

// The Game area: the game's own files and tables, one section at a time.
// Each section keeps its own page id, so the rest of the Studio can still
// send the user straight to Files or Tables.
public sealed partial class GameAreaViewModel : PageViewModel
{
    private bool shown;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsEditsBar))]
    private PageViewModel currentSection;

    public GameAreaViewModel(FilesViewModel files, TablesViewModel tables, GameTextViewModel text, GameArtViewModel art,
        GameSoundViewModel sound, ListsViewModel lists, EditsModBarViewModel editsBar)
    {
        Files = files;
        Tables = tables;
        Text = text;
        Art = art;
        Sound = sound;
        Lists = lists;
        EditsBar = editsBar;
        Sections = [files, tables, text, art, sound, lists];
        currentSection = files;
    }

    public FilesViewModel Files { get; }
    public TablesViewModel Tables { get; }
    public GameTextViewModel Text { get; }
    public GameArtViewModel Art { get; }
    public GameSoundViewModel Sound { get; }
    public ListsViewModel Lists { get; }
    public IReadOnlyList<PageViewModel> Sections { get; }

    // The edits mod bar, over the sections that save edits.
    public EditsModBarViewModel EditsBar { get; }
    public bool ShowsEditsBar => CurrentSection is TablesViewModel or GameTextViewModel or GameArtViewModel or GameSoundViewModel;

    public override string Id => PageIds.Game;
    public override string Title => "Game";

    // The section with sectionId, or false when the area has none.
    public bool Select(string sectionId)
    {
        PageViewModel? section = Sections.FirstOrDefault(page => page.Id == sectionId);
        if (section is null)
        {
            return false;
        }
        CurrentSection = section;
        return true;
    }

    public override void OnShown()
    {
        shown = true;
        CurrentSection.OnShown();
    }

    // Sound stops when the Game area is left, as when its section is.
    public void OnHidden()
    {
        shown = false;
        Sound.StopCommand.Execute(null);
    }

    partial void OnCurrentSectionChanged(PageViewModel? oldValue, PageViewModel newValue)
    {
        if (oldValue is GameSoundViewModel sound)
        {
            sound.StopCommand.Execute(null);
        }
        // A section loads when the user sees it, not when it is picked
        // while another area is open.
        if (shown)
        {
            newValue.OnShown();
        }
    }
}
