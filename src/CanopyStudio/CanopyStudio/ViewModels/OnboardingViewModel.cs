using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Install;
using CanopyStudio.Core.Setup;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

// First-time setup: Welcome, the game folder, the game's assets, Canopy, a
// theme and an area to start in. OnboardingFlow keeps the steps; this does
// each step's work through the same services Setup uses. Nothing is written
// to the game folder unless the user presses Install. A new one is made for
// every run, and Finished tells the shell where to go.
public sealed partial class OnboardingViewModel : ObservableObject
{
    // At most this many problems are listed after copying the game's assets.
    private const int ProblemsShown = 3;

    private readonly GameSession session;
    private readonly AppearanceService appearance;
    private readonly CanopyService canopy;
    private readonly IGameAssets assets;
    private readonly IStudioDialogs dialogs;
    private readonly OnboardingFlow flow;
    private CancellationTokenSource? copying;
    private bool copyingArt;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(FolderVerdict))]
    [NotifyPropertyChangedFor(nameof(FolderVerdictFailed))]
    [NotifyPropertyChangedFor(nameof(NextIsMain))]
    [NotifyCanExecuteChangedFor(nameof(NextCommand))]
    private OnboardingFolder? selectedFolder;

    // A folder typed in, for when Steam does not list the game and the
    // folder picker is no help.
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(AddFolderCommand))]
    private string folderText = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(NoFolders))]
    private bool isSearching;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CopyButtonText))]
    [NotifyPropertyChangedFor(nameof(ShowCopy))]
    [NotifyPropertyChangedFor(nameof(ShowCopyOutcome))]
    [NotifyCanExecuteChangedFor(nameof(CopyCommand))]
    [NotifyCanExecuteChangedFor(nameof(CancelCopyCommand))]
    private bool isCopying;

    [ObservableProperty]
    private string copyText = "";

    // What the last copy came to, or why it stopped.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowCopyOutcome))]
    private string? copyOutcome;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CopyButtonText))]
    private bool copyFailed;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanopyActionText))]
    [NotifyPropertyChangedFor(nameof(ShowCanopyAction))]
    [NotifyCanExecuteChangedFor(nameof(InstallCommand))]
    private bool wantQoL;

    public OnboardingViewModel(GameSession session, AppearanceService appearance, CanopyService canopy,
        IGameAssets assets, IStudioDialogs dialogs)
    {
        this.session = session;
        this.appearance = appearance;
        this.canopy = canopy;
        this.assets = assets;
        this.dialogs = dialogs;
        Proton = new ProtonLaunchViewModel(dialogs);
        flow = new OnboardingFlow(session.Settings.SetupDone, session.HasGame);
        Steps = [.. OnboardingFlow.Steps.Select(step => new OnboardingStepItem(step, StepName(step)))];
        session.PropertyChanged += OnSessionChanged;
        canopy.PropertyChanged += OnCanopyChanged;
        appearance.PropertyChanged += OnAppearanceChanged;
        appearance.Choices.CollectionChanged += OnChoicesChanged;
        RefreshSteps();
        _ = SearchAsync();
    }

    // Asks the shell to close setup and open PageId.
    public event EventHandler<OnboardingFinished>? Finished;

    public ObservableCollection<OnboardingStepItem> Steps { get; }

    // Canopy and VillageQoL in the folder in use, as on Setup.
    public CanopyService Canopy => canopy;

    // The Steam launch option Canopy needs under Proton, on Linux only.
    public ProtonLaunchViewModel Proton { get; }

    // The themes the window can wear now.
    public AppearanceService Appearance => appearance;

    public OnboardingStep Current => flow.Current;
    public bool IsWelcome => Current == OnboardingStep.Welcome;
    public bool IsFolderStep => Current == OnboardingStep.GameFolder;
    public bool IsAssetsStep => Current == OnboardingStep.GameAssets;
    public bool IsCanopyStep => Current == OnboardingStep.Canopy;
    public bool IsThemeStep => Current == OnboardingStep.Theme;
    public bool IsStartStep => Current == OnboardingStep.Start;
    public bool ShowNext => !flow.IsLast;

    // Next is the filled button once it can be pressed; until then the
    // step's own action is.
    public bool NextIsMain => CanNext();

    public string StepCaption => $"STEP {flow.CurrentIndex + 1} OF {OnboardingFlow.Steps.Count}";

    // The steps after the folder work on the folder the user chose in this
    // run or a setup before it, never on one only remembered from older
    // settings.
    public bool HasChosenFolder => flow.HasFolder && session.HasGame;
    public bool NeedsFolder => !HasChosenFolder;

    // Game folder.

    public ObservableCollection<OnboardingFolder> Folders { get; } = [];

    public bool NoFolders => !IsSearching && Folders.Count == 0;

    public string FolderVerdict => SelectedFolder switch
    {
        null => Folders.Count > 0 ? "Pick one of the folders above, or browse for another." : "",
        { IsChecking: true } or { Info: null } => "Checking the folder...",
        { CanUse: false } => "This folder cannot be used. Fix the red check, or pick another folder.",
        _ => "This folder is ready. Next uses it, and nothing in it changes.",
    };

    public bool FolderVerdictFailed => SelectedFolder is { Info: not null, CanUse: false };

    // Game assets.

    public string CopyButtonText => CopyFailed ? "Try again" : "Copy the game's assets";

    public bool ShowCopy => !IsCopying && !AssetsDone;

    public bool AssetsDone => flow.State(OnboardingStep.GameAssets) == OnboardingState.Done;

    public bool ShowCopyOutcome => CopyOutcome is not null && !IsCopying;

    public ObservableCollection<string> CopyProblems { get; } = [];

    public bool HasCopyProblems => CopyProblems.Count > 0;

    // Canopy.

    public bool CanopyReady => HasChosenFolder && canopy.Installed is { Loader: LoaderKind.Canopy };

    public bool ShowQoLChoice => HasChosenFolder && canopy.ShowQoLInstall;

    public string? CanopyActionText
    {
        get
        {
            if (!HasChosenFolder)
            {
                return null;
            }
            if (canopy.ShowInstall)
            {
                return WantQoL && canopy.ShowQoLInstall ? $"{canopy.InstallText} with VillageQoL" : canopy.InstallText;
            }
            if (canopy.ShowUpdate)
            {
                return canopy.UpdateText;
            }
            return WantQoL && canopy.ShowQoLInstall ? "Install VillageQoL" : null;
        }
    }

    public bool ShowCanopyAction => CanopyActionText is not null;

    // GitHub could not be asked, or has not been yet.
    public bool ShowCheckAgain => HasChosenFolder && !canopy.IsChecking && canopy.Latest is not { Release: not null };

    // Theme.

    public bool OnlyForest => appearance.Choices.Count <= 1;

    // Moving between steps.

    [RelayCommand(CanExecute = nameof(CanNext))]
    private void Next()
    {
        if (flow.Current == OnboardingStep.GameFolder)
        {
            if (SelectedFolder is not { CanUse: true, Info: { } info })
            {
                return;
            }
            // As Setup's Use this folder, but setup is marked done only when
            // the whole flow finishes.
            if (!string.Equals(session.Folder, info.Folder, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(session.Settings.GameFolder, info.Folder, StringComparison.OrdinalIgnoreCase))
            {
                session.Open(info, remember: true);
            }
            flow.ChooseFolder();
        }
        flow.Next();
        OnStepEntered();
    }

    private bool CanNext() => flow.Current == OnboardingStep.GameFolder
        ? SelectedFolder is { CanUse: true } && !flow.IsFinished
        : flow.CanNext;

    [RelayCommand(CanExecute = nameof(CanBack))]
    private void Back()
    {
        StopCopy();
        flow.Back();
        OnStepEntered();
    }

    private bool CanBack() => flow.CanBack;

    [RelayCommand(CanExecute = nameof(CanSkip))]
    private void Skip()
    {
        StopCopy();
        flow.Skip();
        if (flow.IsFinished)
        {
            Finish();
            return;
        }
        OnStepEntered();
    }

    private bool CanSkip() => flow.CanSkip;

    // Leaves setup now; what was done stays done.
    [RelayCommand]
    private void Leave()
    {
        if (!flow.IsFinished)
        {
            flow.Leave();
            Finish();
        }
    }

    // Game folder.

    public string FolderExample => GameLocator.FolderExample;

    [RelayCommand]
    private async Task Browse()
    {
        string? picked = await dialogs.PickFolderAsync("Choose the Village in the Shade folder");
        if (picked is not null)
        {
            await OfferAsync(picked, select: true);
        }
    }

    // Offers the typed folder as a card and checks it, like a browsed one.
    [RelayCommand(CanExecute = nameof(CanAddFolder))]
    private async Task AddFolder()
    {
        string folder = GameLocator.TypedFolder(FolderText);
        if (folder.Length > 0)
        {
            FolderText = "";
            await OfferAsync(folder, select: true);
        }
    }

    private bool CanAddFolder() => GameLocator.TypedFolder(FolderText).Length > 0;

    // Game assets: the fonts and art for the themes first, then the names and
    // icons the pickers show, each read from the game folder only.

    [RelayCommand(CanExecute = nameof(CanCopy))]
    private async Task Copy()
    {
        using CancellationTokenSource cancel = new();
        copying = cancel;
        IsCopying = true;
        CopyFailed = false;
        CopyOutcome = null;
        CopyProblems.Clear();
        OnPropertyChanged(nameof(HasCopyProblems));
        List<string> problems = [];
        bool failed = false;
        try
        {
            copyingArt = true;
            CopyText = "Copying the game's fonts and art...";
            await appearance.PrepareAsync(force: false).WaitAsync(cancel.Token);
            copyingArt = false;
            if (appearance.ArtProblem is { } artProblem)
            {
                failed = true;
                problems.Add(artProblem);
            }

            CopyText = "Reading the game's names and icons...";
            Progress<string> progress = new(text =>
            {
                if (copying == cancel && !cancel.IsCancellationRequested)
                {
                    CopyText = text;
                }
            });
            AssetPrepareResult result = await assets.PrepareAsync(progress, cancel.Token);
            cancel.Token.ThrowIfCancellationRequested();
            if (!result.Ok)
            {
                failed = true;
                problems.Add(result.Error ?? "The game's names and icons could not be read.");
            }
            problems.AddRange(result.Problems);

            CopyFailed = failed;
            if (failed)
            {
                CopyOutcome = "Not everything could be copied. Try again, or skip this step and run first-time setup again later from Setup.";
            }
            else
            {
                flow.Complete(OnboardingStep.GameAssets);
                CopyOutcome = problems.Count == 0
                    ? "The game's fonts, art, names and icons are copied. Press Next."
                    : "The game's fonts, art, names and icons are copied, apart from what is listed below. Press Next.";
            }
            ShowProblems(problems);
        }
        catch (OperationCanceledException)
        {
            // The art copy may still finish in the background; that is
            // harmless, as the next try finds it done.
            CopyOutcome = "Copying stopped. Copy again whenever you like, or skip this step.";
        }
        finally
        {
            copyingArt = false;
            copying = null;
            IsCopying = false;
            RefreshSteps();
        }
    }

    private bool CanCopy() => HasChosenFolder && !IsCopying;

    [RelayCommand(CanExecute = nameof(IsCopying))]
    private void CancelCopy()
    {
        CopyText = "Stopping...";
        copying?.Cancel();
    }

    // Canopy: one button installs or updates it, and VillageQoL too when the
    // user asked for it.

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private async Task Install()
    {
        if (canopy.ShowInstall)
        {
            await canopy.InstallCommand.ExecuteAsync(null);
        }
        else if (canopy.ShowUpdate)
        {
            await canopy.UpdateCommand.ExecuteAsync(null);
        }
        // VillageQoL needs Canopy, so it waits for Canopy's install to work.
        if (WantQoL && canopy.ShowQoLInstall && !canopy.OutcomeFailed && canopy.InstallQoLCommand.CanExecute(null))
        {
            await canopy.InstallQoLCommand.ExecuteAsync(null);
        }
        RefreshCanopy();
    }

    private bool CanInstall()
    {
        if (!HasChosenFolder || canopy.IsWorking)
        {
            return false;
        }
        if (canopy.ShowInstall)
        {
            return canopy.InstallCommand.CanExecute(null);
        }
        if (canopy.ShowUpdate)
        {
            return canopy.UpdateCommand.CanExecute(null);
        }
        return WantQoL && canopy.InstallQoLCommand.CanExecute(null);
    }

    // Start: each choice finishes setup and opens its area.

    [RelayCommand]
    private void OpenPlay() => Choose(OnboardingDestination.Play);

    [RelayCommand]
    private void OpenCreate() => Choose(OnboardingDestination.Create);

    [RelayCommand]
    private void OpenGame() => Choose(OnboardingDestination.Game);

    private void Choose(OnboardingDestination destination)
    {
        if (flow.Choose(destination))
        {
            Finish();
        }
    }

    private void Finish()
    {
        StopCopy();
        session.PropertyChanged -= OnSessionChanged;
        canopy.PropertyChanged -= OnCanopyChanged;
        appearance.PropertyChanged -= OnAppearanceChanged;
        appearance.Choices.CollectionChanged -= OnChoicesChanged;
        // Never clears a setup done before.
        if (flow.SetupDone && !session.Settings.SetupDone)
        {
            session.Remember(current => current with { SetupDone = true });
        }
        string page = flow.Destination switch
        {
            OnboardingDestination.Play => PageIds.Play,
            OnboardingDestination.Create => PageIds.Create,
            OnboardingDestination.Game => PageIds.Game,
            // Left without picking an area: back to Setup, where a rerun
            // starts and where a setup without a folder is finished.
            _ => flow.SetupDone && !flow.WasSetupDone ? PageIds.Home : PageIds.Setup,
        };
        RefreshSteps();
        Finished?.Invoke(this, new OnboardingFinished(page, flow.SetupDone));
    }

    private void OnStepEntered()
    {
        if (flow.Current == OnboardingStep.Canopy)
        {
            RefreshCanopy();
        }
        RefreshSteps();
    }

    // Steam's libraries first, after the folder already in use, each checked
    // as it is found.
    private async Task SearchAsync()
    {
        IsSearching = true;
        IReadOnlyList<string> found = await Task.Run(GameLocator.Candidates);
        IsSearching = false;
        List<string> offered = [];
        if (session.Folder is { } inUse)
        {
            offered.Add(inUse);
        }
        offered.AddRange(found);
        // All shown at once, then checked one at a time, as Setup checks.
        List<OnboardingFolder> added = [.. offered.Select(Add).OfType<OnboardingFolder>()];
        OnPropertyChanged(nameof(FolderVerdict));
        foreach (OnboardingFolder item in added)
        {
            await item.CheckAsync();
        }
        // The folder in use when it passes, else the first that does: still
        // only a suggestion until the user presses Next.
        SelectedFolder ??= Folders.FirstOrDefault(folder => folder.CanUse);
    }

    private async Task OfferAsync(string folder, bool select)
    {
        OnboardingFolder? item = Add(folder);
        if (select)
        {
            SelectedFolder = item ?? Find(folder);
        }
        if (item is not null)
        {
            await item.CheckAsync();
        }
    }

    // A new card for folder, or null when it is already offered.
    private OnboardingFolder? Add(string folder)
    {
        if (Find(folder) is not null)
        {
            return null;
        }
        OnboardingFolder item = new(folder);
        item.PropertyChanged += OnFolderChanged;
        Folders.Add(item);
        OnPropertyChanged(nameof(NoFolders));
        return item;
    }

    private OnboardingFolder? Find(string folder) => Folders.FirstOrDefault(known =>
        string.Equals(known.Folder, folder, StringComparison.OrdinalIgnoreCase));

    private void OnFolderChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender == SelectedFolder)
        {
            OnPropertyChanged(nameof(FolderVerdict));
            OnPropertyChanged(nameof(FolderVerdictFailed));
            OnPropertyChanged(nameof(NextIsMain));
            NextCommand.NotifyCanExecuteChanged();
        }
    }

    private void ShowProblems(List<string> problems)
    {
        CopyProblems.Clear();
        foreach (string problem in problems.Take(ProblemsShown))
        {
            CopyProblems.Add(problem);
        }
        if (problems.Count > ProblemsShown)
        {
            int more = problems.Count - ProblemsShown;
            CopyProblems.Add(more == 1 ? "And 1 more problem." : $"And {more} more problems.");
        }
        OnPropertyChanged(nameof(HasCopyProblems));
    }

    private void StopCopy() => copying?.Cancel();

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs e)
    {
        OnPropertyChanged(nameof(HasChosenFolder));
        OnPropertyChanged(nameof(NeedsFolder));
        CopyCommand.NotifyCanExecuteChanged();
        RefreshCanopy();
    }

    private void OnCanopyChanged(object? sender, PropertyChangedEventArgs e) => RefreshCanopy();

    private void OnAppearanceChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (copyingArt && e.PropertyName == nameof(AppearanceService.ArtSummary) && appearance.IsPreparing)
        {
            CopyText = appearance.ArtSummary;
        }
    }

    private void OnChoicesChanged(object? sender, NotifyCollectionChangedEventArgs e) =>
        OnPropertyChanged(nameof(OnlyForest));

    // Canopy installed in the chosen folder completes its step.
    private void RefreshCanopy()
    {
        if (CanopyReady && flow.State(OnboardingStep.Canopy) != OnboardingState.Done)
        {
            flow.Complete(OnboardingStep.Canopy);
            RefreshSteps();
        }
        OnPropertyChanged(nameof(CanopyReady));
        OnPropertyChanged(nameof(ShowQoLChoice));
        OnPropertyChanged(nameof(CanopyActionText));
        OnPropertyChanged(nameof(ShowCanopyAction));
        OnPropertyChanged(nameof(ShowCheckAgain));
        InstallCommand.NotifyCanExecuteChanged();
    }

    private void RefreshSteps()
    {
        foreach (OnboardingStepItem item in Steps)
        {
            item.State = flow.State(item.Step);
            item.IsCurrent = item.Step == flow.Current && !flow.IsFinished;
        }
        foreach (string name in (string[])[nameof(Current), nameof(IsWelcome), nameof(IsFolderStep),
                     nameof(IsAssetsStep), nameof(IsCanopyStep), nameof(IsThemeStep), nameof(IsStartStep),
                     nameof(ShowNext), nameof(NextIsMain), nameof(StepCaption), nameof(HasChosenFolder), nameof(NeedsFolder),
                     nameof(AssetsDone), nameof(ShowCopy), nameof(FolderVerdict)])
        {
            OnPropertyChanged(name);
        }
        NextCommand.NotifyCanExecuteChanged();
        BackCommand.NotifyCanExecuteChanged();
        SkipCommand.NotifyCanExecuteChanged();
        CopyCommand.NotifyCanExecuteChanged();
    }

    private static string StepName(OnboardingStep step) => step switch
    {
        OnboardingStep.Welcome => "Welcome",
        OnboardingStep.GameFolder => "Game folder",
        OnboardingStep.GameAssets => "Game assets",
        OnboardingStep.Canopy => "Canopy",
        OnboardingStep.Theme => "Theme",
        OnboardingStep.Start => "Start",
        _ => step.ToString(),
    };
}
