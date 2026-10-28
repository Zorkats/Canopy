using System.Collections.ObjectModel;
using CanopyStudio.Core.Install;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Platform;
using CanopyStudio.Core.Setup;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

public sealed record CandidateItem(string Folder, IRelayCommand Use);

public sealed record CheckItem(SetupCheck Check)
{
    public string Title => Check.Title;
    public string Detail => Check.Detail;
    public bool IsPass => Check.State == CheckState.Pass;
    public bool IsWarn => Check.State == CheckState.Warn;
    public bool IsFail => Check.State == CheckState.Fail;
}

// Choosing the game folder. Steam's libraries are searched for the game, but
// only a folder the user accepts is used and remembered.
public sealed partial class SetupViewModel : PageViewModel
{
    private readonly GameSession session;
    private readonly IStudioNavigator navigator;
    private readonly IStudioDialogs dialogs;
    private GameInfo? checkedInfo;
    private int checkVersion;
    private bool searched;

    [ObservableProperty]
    private string folderText = "";

    [ObservableProperty]
    private bool isChecking;

    [ObservableProperty]
    private bool isSearching;

    public SetupViewModel(GameSession session, IStudioNavigator navigator, IStudioDialogs dialogs,
        AppearanceService appearance, CanopyService canopy)
    {
        this.session = session;
        this.navigator = navigator;
        this.dialogs = dialogs;
        Appearance = appearance;
        Canopy = canopy;
        Proton = new ProtonLaunchViewModel(dialogs);
        session.PropertyChanged += (_, _) => RaiseVerdict();
        // An install changes what the checks say about the folder in use.
        canopy.Changed += (_, _) =>
        {
            if (IsCurrent)
            {
                _ = CheckFolderAsync();
            }
        };
    }

    public override string Id => PageIds.Setup;
    public override string Title => "Setup";

    // The game's fonts and art, copied for the seasonal themes.
    public AppearanceService Appearance { get; }

    // Installing, updating and removing Canopy in the folder in use.
    public CanopyService Canopy { get; }

    // The Steam launch option Canopy needs under Proton, on Linux only.
    public ProtonLaunchViewModel Proton { get; }

    // A game folder as this platform writes one, for the empty folder box.
    public string FolderExample => GameLocator.FolderExample;

    public string CanopyIntro { get; } =
        $"Canopy is the mod loader, dinput8.dll next to village.exe. VillageQoL is a mod for it, {GameFiles.Shown("mods/VillageQoL.dll")}, installed on its own. Canopy Studio downloads each from the latest release on GitHub, checks it against the SHA-256 GitHub lists for it, and writes only that file. Another program's dinput8.dll is kept under a new name, never deleted.";

    public ObservableCollection<CandidateItem> Candidates { get; } = [];
    public ObservableCollection<CheckItem> Checks { get; } = [];

    public bool HasCandidates => Candidates.Count > 0;
    public bool NoCandidates => searched && !IsSearching && Candidates.Count == 0;
    public bool HasChecks => Checks.Count > 0;

    // The checked folder is the one in use.
    public bool IsCurrent => checkedInfo is { Ok: true } &&
                             string.Equals(session.Folder, checkedInfo.Folder, StringComparison.OrdinalIgnoreCase);

    // In use and confirmed: before first-time setup is done, the folder
    // opened from older settings still needs its Use this folder.
    public bool IsInUse => IsCurrent && session.Settings.SetupDone;

    public bool CanUse => checkedInfo is { Ok: true } && !IsInUse;

    public string Verdict => checkedInfo switch
    {
        null => "",
        { Ok: false } => "This folder cannot be used yet. Fix the red check above, or pick another folder.",
        _ when IsInUse => "This is the folder Canopy Studio is using.",
        _ => "This folder is ready to use.",
    };

    public override void OnShown()
    {
        if (!searched && !IsSearching)
        {
            _ = SearchAsync();
        }
        else if (FolderText.Length == 0 && session.Folder is { } folder)
        {
            FolderText = folder;
            _ = CheckFolderAsync();
        }
    }

    // Fills in a remembered folder that no longer opens, so its checks say why.
    public void Prefill(string folder)
    {
        FolderText = folder;
        _ = CheckFolderAsync();
    }

    [RelayCommand]
    private async Task Browse()
    {
        string? folder = await dialogs.PickFolderAsync("Choose the Village in the Shade folder");
        if (folder is not null)
        {
            FolderText = folder;
            await CheckFolderAsync();
        }
    }

    [RelayCommand]
    private Task Check() => CheckFolderAsync();

    [RelayCommand]
    private void RunOnboarding() => navigator.RunOnboarding();

    [RelayCommand]
    private void Use()
    {
        if (checkedInfo is { Ok: true } info)
        {
            session.Open(info, remember: true);
            session.Remember(current => current with { SetupDone = true });
            RaiseVerdict();
            navigator.Navigate(PageIds.Home);
        }
    }

    partial void OnFolderTextChanged(string value)
    {
        // Edited text has not been checked yet.
        ++checkVersion;
        checkedInfo = null;
        Checks.Clear();
        IsChecking = false;
        RaiseVerdict();
    }

    private async Task SearchAsync()
    {
        IsSearching = true;
        IReadOnlyList<string> found = await Task.Run(GameLocator.Candidates);
        IsSearching = false;
        searched = true;
        Candidates.Clear();
        foreach (string folder in found)
        {
            Candidates.Add(new CandidateItem(folder, new AsyncRelayCommand(async () =>
            {
                FolderText = folder;
                await CheckFolderAsync();
            })));
        }
        OnPropertyChanged(nameof(HasCandidates));
        OnPropertyChanged(nameof(NoCandidates));
        if (FolderText.Length == 0)
        {
            // The folder in use first; else what Steam has, as a suggestion
            // the user still has to accept.
            string? start = session.Folder ?? (found.Count > 0 ? found[0] : null);
            if (start is not null)
            {
                FolderText = start;
                await CheckFolderAsync();
            }
        }
    }

    private async Task CheckFolderAsync()
    {
        string folder = GameLocator.TypedFolder(FolderText);
        if (folder.Length == 0)
        {
            return;
        }
        int version = ++checkVersion;
        IsChecking = true;
        bool exists = Directory.Exists(folder);
        GameInfo info = exists ? await GameSession.InspectAsync(folder) : new GameInfo { Folder = folder };
        LoaderKind? loader = exists ? await Task.Run(() => CanopyInstaller.Inspect(folder).Loader) : null;
        if (version != checkVersion)
        {
            return;
        }
        IsChecking = false;
        checkedInfo = info;
        Checks.Clear();
        foreach (SetupCheck check in SetupChecks.For(info, exists, loader))
        {
            Checks.Add(new CheckItem(check));
        }
        RaiseVerdict();
    }

    private void RaiseVerdict()
    {
        OnPropertyChanged(nameof(HasChecks));
        OnPropertyChanged(nameof(IsCurrent));
        OnPropertyChanged(nameof(IsInUse));
        OnPropertyChanged(nameof(CanUse));
        OnPropertyChanged(nameof(Verdict));
    }
}
