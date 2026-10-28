using System.ComponentModel;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CanopyStudio.ViewModels.Create;
using CanopyStudio.ViewModels.Game;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels;

public sealed partial class ShellViewModel : ObservableObject, IStudioNavigator
{
    private readonly GameSession session;
    private readonly CanopyService canopy;
    private readonly IGameAssets assets;
    private readonly IStudioDialogs dialogs;

    [ObservableProperty]
    private PageViewModel? currentPage;

    // First-time setup while it shows over the window, else null.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOnboarding))]
    [NotifyPropertyChangedFor(nameof(ShowMain))]
    private OnboardingViewModel? onboarding;

    public ShellViewModel(GameSession session, AppearanceService appearance, CanopyService canopy, IGameAssets assets,
        IStudioDialogs dialogs)
    {
        this.session = session;
        this.canopy = canopy;
        this.assets = assets;
        this.dialogs = dialogs;
        Appearance = appearance;
        Home = new HomeViewModel(session, this, canopy);
        Play = new PlayViewModel(session, dialogs, this);
        Create = new CreateAreaViewModel(session, assets, dialogs);
        Edits = new GameEdits(session);
        Game = new GameAreaViewModel(new FilesViewModel(session, this), new TablesViewModel(session, Edits, this),
            new GameTextViewModel(session, Edits, this), new GameArtViewModel(session, Edits, dialogs),
            new GameSoundViewModel(session, Edits, dialogs), new ListsViewModel(assets, this),
            new EditsModBarViewModel(Edits, dialogs));
        Setup = new SetupViewModel(session, this, dialogs, appearance, canopy);
        Pages = [Home, Play, Create, Game, Setup];
        CoreProblem = CanopyCore.Check();
        session.PropertyChanged += OnSessionChanged;
    }

    public AppearanceService Appearance { get; }
    public HomeViewModel Home { get; }
    public PlayViewModel Play { get; }
    public CreateAreaViewModel Create { get; }
    public GameAreaViewModel Game { get; }
    // The edits mod the Game area saves to.
    public GameEdits Edits { get; }
    public SetupViewModel Setup { get; }
    public IReadOnlyList<PageViewModel> Pages { get; }

    // Before first-time setup is done, only Setup can be opened.
    public bool IsSetupDone => session.Settings.SetupDone;

    // The page to open next time: inside the Game area, its section.
    public string? CurrentPageId => CurrentPage is GameAreaViewModel area ? area.CurrentSection.Id : CurrentPage?.Id;

    // Why canopy_core.dll cannot be used, or null when it can. Nothing
    // works without it, so the window shows only this.
    public string? CoreProblem { get; }
    public bool HasCoreProblem => CoreProblem is not null;

    public bool IsOnboarding => Onboarding is not null;

    // The rail, pages and status bar; first-time setup and a missing
    // canopy_core.dll each take the whole window instead.
    public bool ShowMain => !HasCoreProblem && !IsOnboarding;

    public bool IsStarting { get; private set; } = true;

    public string StatusFolder => session.Folder ?? "No game folder chosen";

    public string StatusBuild => session.Game is { Ok: true } game
        ? game.Supported ? "Checked game version" : $"Unchecked game version {game.Build:X8}"
        : "";

    public bool IsUnsupportedBuild => session.Game is { Ok: true, Supported: false };

    // Opens the folder from the settings, or sends the user to Setup.
    public async Task StartAsync()
    {
        if (HasCoreProblem)
        {
            return;
        }
        string? folder = session.Settings.GameFolder;
        GameInfo? info = folder is null ? null : await GameSession.InspectAsync(folder);
        IsStarting = false;
        OnPropertyChanged(nameof(IsStarting));
        if (info is not { Ok: true })
        {
            // The remembered folder stays in the settings until the user
            // picks another: it may only be on a drive that is not plugged in.
            if (folder is not null)
            {
                Setup.Prefill(folder);
            }
            Navigate(PageIds.Setup);
            if (!IsSetupDone)
            {
                RunOnboarding();
            }
            return;
        }
        // Before first-time setup, so it offers the remembered folder.
        session.Open(info, remember: false);
        if (!IsSetupDone)
        {
            Navigate(PageIds.Setup);
            RunOnboarding();
            return;
        }
        string last = session.Settings.LastPage ?? PageIds.Home;
        Navigate(last == PageIds.Setup ? PageIds.Home : last);
    }

    public void Navigate(string pageId)
    {
        if (!IsSetupDone)
        {
            pageId = PageIds.Setup;
        }
        if (Game.Select(pageId))
        {
            pageId = PageIds.Game;
        }
        CurrentPage = Pages.FirstOrDefault(page => page.Id == pageId) ?? Home;
    }

    public void OpenTable(string file, ulong? rowId = null)
    {
        Game.Tables.Open(file, rowId);
        Navigate(PageIds.Tables);
    }

    // Shows Create first, so the raw view is laid out when it scrolls to
    // line. Create asks before it leaves a project with unsaved changes.
    public void OpenProject(string folder, int line = 0)
    {
        Navigate(PageIds.Create);
        _ = Create.OpenProject(folder, line);
    }

    // A new run each time; finishing it opens the area the user picked, or
    // Setup when setup is still not done.
    public void RunOnboarding()
    {
        if (HasCoreProblem || Onboarding is not null)
        {
            return;
        }
        OnboardingViewModel run = new(session, Appearance, canopy, assets, dialogs);
        run.Finished += (_, finished) =>
        {
            Onboarding = null;
            Navigate(finished.PageId);
        };
        Onboarding = run;
    }

    partial void OnCurrentPageChanged(PageViewModel? oldValue, PageViewModel? newValue)
    {
        if (oldValue is GameAreaViewModel area)
        {
            area.OnHidden();
        }
        newValue?.OnShown();
    }

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs e)
    {
        OnPropertyChanged(nameof(StatusFolder));
        OnPropertyChanged(nameof(StatusBuild));
        OnPropertyChanged(nameof(IsUnsupportedBuild));
        OnPropertyChanged(nameof(IsSetupDone));
    }
}
