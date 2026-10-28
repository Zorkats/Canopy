using CanopyStudio.Core.Install;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.Services;

// Canopy and VillageQoL in the game folder in use: what is installed there,
// the latest release on GitHub, and installing, updating and removing each
// on its own. GitHub is asked once per run, when a game folder first comes
// into use, and again only on Check again.
public sealed partial class CanopyService : ObservableObject
{
    private const string GameRunningText = "Close the game first: Windows keeps the game's DLLs in use while it runs.";

    private readonly GameSession session;
    private readonly ReleaseFeed feed;
    private bool asked;

    [ObservableProperty]
    private InstalledCanopy? installed;

    [ObservableProperty]
    private ReleaseCheck? latest;

    [ObservableProperty]
    private bool isChecking;

    [ObservableProperty]
    private bool isWorking;

    [ObservableProperty]
    private string workText = "";

    // What the last install, update or removal did, or why it did not.
    [ObservableProperty]
    private string? outcome;

    [ObservableProperty]
    private bool outcomeFailed;

    [ObservableProperty]
    private bool isConfirmingUninstall;

    public CanopyService(GameSession session, ReleaseFeed feed)
    {
        this.session = session;
        this.feed = feed;
        session.GameChanged += async (_, _) =>
        {
            IsConfirmingUninstall = false;
            Outcome = null;
            await RefreshAsync();
            if (!asked)
            {
                await CheckAsync();
            }
        };
    }

    // After Canopy Studio changed the game folder's files.
    public event EventHandler? Changed;

    public bool HasGame => session.HasGame;

    private InstalledCanopy? Here => HasGame ? Installed : null;

    private CanopyRelease? Release => Latest?.Release;

    // Canopy, the loader.

    public string InstalledText => Here switch
    {
        null => "",
        { Loader: LoaderKind.None } => "Not installed",
        { Loader: LoaderKind.Other } => "Not installed. Another program's dinput8.dll is here.",
        { Version: null } => "A Canopy build that does not carry its version",
        { Version: { } version } => version,
    };

    public bool UpdateAvailable => Here is { Loader: LoaderKind.Canopy } here && Release is { } release &&
                                   CanopyInstaller.IsNewer(release.Version, here.Version);

    public bool ShowInstall => Here is { Loader: LoaderKind.None or LoaderKind.Other };
    public bool ShowUpdate => UpdateAvailable;
    public bool ShowUninstall => Here is { Loader: LoaderKind.Canopy } && !IsConfirmingUninstall;
    public string InstallText => Release is { } release ? $"Install {release.Version}" : "Install";
    public string UpdateText => Release is { } release ? $"Update to {release.Version}" : "Update";

    // VillageQoL, the mod.

    public string QoLText => Here switch
    {
        null => "",
        { HasQoL: false } => "Not installed",
        { QoLVersion: null } => "A VillageQoL build that does not carry its version",
        { QoLVersion: { } version } => version,
    };

    private ReleaseAsset? QoLAsset => Release?.VillageQoL;

    public bool QoLUpdateAvailable => Here is { HasQoL: true } here && QoLAsset is not null &&
                                      CanopyInstaller.IsNewer(Release!.Version, here.QoLVersion);

    public bool ShowQoLInstall => Here is { HasQoL: false };
    public bool ShowQoLUpdate => QoLUpdateAvailable;
    public bool ShowQoLRemove => Here is { HasQoL: true };
    public string QoLInstallText => QoLAsset is not null ? $"Install {Release!.Version}" : "Install";
    public string QoLUpdateText => QoLAsset is not null ? $"Update to {Release!.Version}" : "Update";

    // The release, and what is being done.

    public string LatestText => IsChecking
        ? "Asking GitHub..."
        : Latest switch
        {
            null => "Not checked yet.",
            { Release: { VillageQoL: null } release } => $"{release.Version}, without a VillageQoL package",
            { Release: { } release } => release.Version,
            { Error: var error } => error ?? "",
        };

    public bool ShowWork => IsWorking;
    public bool ShowOutcome => Outcome is not null && !IsWorking;

    // The CANOPY figure on Home.
    public string HomeText => Here switch
    {
        { Loader: LoaderKind.Canopy } here => here.Version ?? "Installed",
        null => "",
        _ => "Not installed",
    };

    // Home's nudge, when Canopy is missing or out of date, or what is being
    // done now; null otherwise.
    public string? HomePrompt
    {
        get
        {
            if (IsWorking)
            {
                return WorkText;
            }
            if (Release is not { } release)
            {
                return null;
            }
            if (ShowInstall)
            {
                return $"Canopy is not installed in this game folder. Canopy {release.Version} loads mods in the game.";
            }
            return UpdateAvailable
                ? $"Canopy {release.Version} is out. This game folder has {(Here!.Version is { } version ? $"Canopy {version}" : "an older build")}."
                : null;
        }
    }

    public bool ShowHomePrompt => HomePrompt is not null;
    public bool ShowHomeCard => ShowHomePrompt || ShowOutcome;

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private Task Install() => InstallAsync(Release?.Canopy);

    private bool CanInstall => !IsWorking && ShowInstall && Release is not null;

    [RelayCommand(CanExecute = nameof(CanUpdate))]
    private Task Update() => InstallAsync(Release?.Canopy);

    private bool CanUpdate => !IsWorking && UpdateAvailable;

    [RelayCommand(CanExecute = nameof(CanUninstall))]
    private void AskUninstall()
    {
        Outcome = null;
        IsConfirmingUninstall = true;
    }

    [RelayCommand]
    private void CancelUninstall() => IsConfirmingUninstall = false;

    [RelayCommand(CanExecute = nameof(CanUninstall))]
    private async Task Uninstall()
    {
        IsConfirmingUninstall = false;
        if (session.Folder is not { } folder)
        {
            return;
        }
        string? restore = Backup(folder);
        await WorkAsync("Removing Canopy...", async () =>
        {
            InstallResult result = await Task.Run(() =>
                CanopyInstaller.Uninstall(folder, restore, CanopyInstaller.GameRunning()));
            if (result.Ok)
            {
                RememberBackup(folder, null);
            }
            return result;
        });
    }

    private bool CanUninstall => !IsWorking && Here is { Loader: LoaderKind.Canopy };

    [RelayCommand(CanExecute = nameof(CanInstallQoL))]
    private Task InstallQoL() => InstallAsync(QoLAsset);

    private bool CanInstallQoL => !IsWorking && ShowQoLInstall && QoLAsset is not null;

    [RelayCommand(CanExecute = nameof(CanUpdateQoL))]
    private Task UpdateQoL() => InstallAsync(QoLAsset);

    private bool CanUpdateQoL => !IsWorking && QoLUpdateAvailable;

    [RelayCommand(CanExecute = nameof(CanRemoveQoL))]
    private async Task RemoveQoL()
    {
        if (session.Folder is not { } folder)
        {
            return;
        }
        await WorkAsync("Removing VillageQoL...", () =>
            Task.Run(() => CanopyInstaller.RemoveVillageQoL(folder, CanopyInstaller.GameRunning())));
    }

    private bool CanRemoveQoL => !IsWorking && ShowQoLRemove;

    [RelayCommand(CanExecute = nameof(CanCheckAgain))]
    private Task CheckAgain() => CheckAsync();

    private bool CanCheckAgain => HasGame && !IsChecking;

    private async Task CheckAsync()
    {
        asked = true;
        IsChecking = true;
        try
        {
            Latest = await feed.LatestAsync();
        }
        finally
        {
            IsChecking = false;
        }
    }

    private async Task InstallAsync(ReleaseAsset? asset)
    {
        if (session.Folder is not { } folder || Release is not { } release || asset is null)
        {
            return;
        }
        await WorkAsync($"Downloading {asset.Name}...", async () =>
        {
            if (CanopyInstaller.GameRunning())
            {
                return new InstallResult(false, GameRunningText);
            }
            ReleaseDownload download = await feed.DownloadAsync(asset, release.Version);
            if (download.Package is not { } package)
            {
                return new InstallResult(false, download.Error ?? "The download failed.");
            }
            WorkText = $"Installing {asset.Kind} {release.Version}...";
            InstallResult result = await Task.Run(() =>
                CanopyInstaller.Install(folder, package, CanopyInstaller.GameRunning(), DateTime.Now));
            if (result.Backup is not null)
            {
                RememberBackup(folder, result.Backup);
            }
            return result;
        });
    }

    private async Task WorkAsync(string text, Func<Task<InstallResult>> work)
    {
        IsWorking = true;
        WorkText = text;
        Outcome = null;
        try
        {
            InstallResult result = await work();
            OutcomeFailed = !result.Ok;
            Outcome = result.Message;
        }
        finally
        {
            IsWorking = false;
            WorkText = "";
            await RefreshAsync();
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    private async Task RefreshAsync()
    {
        string? folder = session.Folder;
        Installed = folder is null ? null : await Task.Run(() => CanopyInstaller.Inspect(folder));
    }

    // The dinput8.dll an install set aside in folder, by its file name.
    private string? Backup(string folder) => session.Settings.ReplacedLoaders?
        .FirstOrDefault(pair => string.Equals(pair.Key, folder, StringComparison.OrdinalIgnoreCase)).Value;

    private void RememberBackup(string folder, string? backup) => session.Remember(current =>
    {
        Dictionary<string, string> loaders = new(current.ReplacedLoaders ?? new Dictionary<string, string>(),
            StringComparer.OrdinalIgnoreCase);
        loaders.Remove(folder);
        if (backup is not null)
        {
            loaders[folder] = backup;
        }
        return current with { ReplacedLoaders = loaders };
    });

    partial void OnInstalledChanged(InstalledCanopy? value) => RaiseState();

    partial void OnLatestChanged(ReleaseCheck? value) => RaiseState();

    partial void OnIsCheckingChanged(bool value) => RaiseState();

    partial void OnIsWorkingChanged(bool value) => RaiseState();

    partial void OnOutcomeChanged(string? value) => RaiseState();

    partial void OnIsConfirmingUninstallChanged(bool value) => RaiseState();

    partial void OnWorkTextChanged(string value) => RaiseState();

    private void RaiseState()
    {
        foreach (string name in (string[])[nameof(HasGame), nameof(InstalledText), nameof(UpdateAvailable),
                     nameof(ShowInstall), nameof(ShowUpdate), nameof(ShowUninstall), nameof(InstallText),
                     nameof(UpdateText), nameof(QoLText), nameof(QoLUpdateAvailable), nameof(ShowQoLInstall),
                     nameof(ShowQoLUpdate), nameof(ShowQoLRemove), nameof(QoLInstallText), nameof(QoLUpdateText),
                     nameof(LatestText), nameof(ShowWork), nameof(ShowOutcome), nameof(HomeText), nameof(HomePrompt),
                     nameof(ShowHomePrompt), nameof(ShowHomeCard)])
        {
            OnPropertyChanged(name);
        }
        InstallCommand.NotifyCanExecuteChanged();
        UpdateCommand.NotifyCanExecuteChanged();
        AskUninstallCommand.NotifyCanExecuteChanged();
        UninstallCommand.NotifyCanExecuteChanged();
        InstallQoLCommand.NotifyCanExecuteChanged();
        UpdateQoLCommand.NotifyCanExecuteChanged();
        RemoveQoLCommand.NotifyCanExecuteChanged();
        CheckAgainCommand.NotifyCanExecuteChanged();
    }
}
