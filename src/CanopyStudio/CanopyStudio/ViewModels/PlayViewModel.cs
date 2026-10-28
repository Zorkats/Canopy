using System.Collections.ObjectModel;
using System.ComponentModel;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Play;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

// The Play area: the mods in the game folder, in load order. Turn one off or
// on, install one from a zip or DLL, remove one, undo the last change, see
// which mods ship the same game file, what Canopy's check says about each
// and the log of the last run. Every read and write runs off the UI thread;
// writes go through DisabledMods and ModChanges only.
public sealed partial class PlayViewModel : PageViewModel
{
    public const int ModTab = 0;
    public const int ProblemsTab = 1;
    public const int ConflictsTab = 2;
    public const int LogTab = 3;

    // A file past this is refused before it is read; ModZip unpacks at most
    // as much.
    private const long MaxInstallBytes = 1L << 30;

    private readonly GameSession session;
    private readonly IStudioDialogs dialogs;
    private readonly IStudioNavigator navigator;
    private readonly string backups = PlayPaths.Backups();
    private IReadOnlyList<string> logLines = [];
    private int version;

    // Everything one refresh reads, off the UI thread.
    private sealed record Snapshot(
        IReadOnlyList<ModEntry> Mods,
        IReadOnlySet<string> Unsafe,
        IReadOnlyList<OverrideConflict> Conflicts,
        ModChange? Last,
        CanopyLogRun Log,
        string? Error);

    [ObservableProperty]
    private IReadOnlyList<PlayModItem> mods = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection))]
    [NotifyPropertyChangedFor(nameof(NoSelection))]
    private PlayModItem? selectedMod;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(NoMods))]
    private bool isLoading;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(InstallCommand))]
    [NotifyCanExecuteChangedFor(nameof(UndoCommand))]
    [NotifyCanExecuteChangedFor(nameof(RefreshCommand))]
    private bool isWorking;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStatus))]
    private string? status;

    [ObservableProperty]
    private bool statusFailed;

    // Shown once a mod was turned on or off, installed or removed.
    [ObservableProperty]
    private bool showNextStart;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasConflicts))]
    [NotifyPropertyChangedFor(nameof(ConflictsTitle))]
    private IReadOnlyList<ConflictItem> conflicts = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasGeneralProblems))]
    private IReadOnlyList<ProblemItem> generalProblems = [];

    // While the check runs, or why it could not; null once it is done.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCheckState))]
    private string? checkState;

    [ObservableProperty]
    private string checkSummary = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(UndoText))]
    [NotifyCanExecuteChangedFor(nameof(UndoCommand))]
    private ModChange? lastChange;

    [ObservableProperty]
    private bool onlyProblems;

    [ObservableProperty]
    private string logText = "";

    // Why there is no log to show, or what the filter left; null otherwise.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasLogState))]
    private string? logState;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsModTab))]
    [NotifyPropertyChangedFor(nameof(IsProblemsTab))]
    [NotifyPropertyChangedFor(nameof(IsConflictsTab))]
    [NotifyPropertyChangedFor(nameof(IsLogTab))]
    private int selectedTab;

    public PlayViewModel(GameSession session, IStudioDialogs dialogs, IStudioNavigator navigator)
    {
        this.session = session;
        this.dialogs = dialogs;
        this.navigator = navigator;
        session.GameChanged += OnGameChanged;
        session.PropertyChanged += OnSessionChanged;
        Plans.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasPlans));
    }

    public override string Id => PageIds.Play;
    public override string Title => "Play";

    public bool NeedsGame => !session.HasGame;
    public bool HasGame => session.HasGame;
    public bool NoMods => !IsLoading && Mods.Count == 0;
    public bool HasSelection => SelectedMod is not null;
    public bool NoSelection => SelectedMod is null;
    public bool HasStatus => Status is not null;
    public bool HasConflicts => Conflicts.Count > 0;
    public string ConflictsTitle => $"Conflicts ({Conflicts.Count})";
    public bool HasGeneralProblems => GeneralProblems.Count > 0;
    public bool HasCheckState => CheckState is not null;
    public bool HasLogState => LogState is not null;
    public bool HasPlans => Plans.Count > 0;
    public bool IsModTab => SelectedTab == ModTab;
    public bool IsProblemsTab => SelectedTab == ProblemsTab;
    public bool IsConflictsTab => SelectedTab == ConflictsTab;
    public bool IsLogTab => SelectedTab == LogTab;

    public ObservableCollection<PlanItem> Plans { get; } = [];

    public string UndoText => LastChange is { } change
        ? $"Undo takes back {PlayMods.Describe(change)}."
        : "There is no mod change to undo.";

    public override void OnShown() => _ = RefreshAsync();

    // Plans an install of each .zip or .dll in paths, for the user to check
    // before anything is written. The page's drop and Install both end here.
    public async Task PlanAsync(IEnumerable<string> paths)
    {
        if (session.Folder is not { } folder || IsWorking)
        {
            return;
        }
        List<string> files = paths.Where(path => path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)
            || path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)).ToList();
        if (files.Count == 0)
        {
            Show(new(false, "Only .zip and .dll files can be installed as mods. Drop or pick one of those."));
            return;
        }
        IsWorking = true;
        foreach (string file in files)
        {
            ModInstallPlan plan = await Task.Run(() => PlanFile(folder, file));
            if (session.Folder != folder)
            {
                break;
            }
            foreach (PlanItem old in Plans.Where(item => item.File.Equals(file, StringComparison.OrdinalIgnoreCase)).ToList())
            {
                Plans.Remove(old);
            }
            PlanItem? added = null;
            added = new(file, plan,
                new AsyncRelayCommand(() => InstallPlanAsync(added!)),
                new RelayCommand(() => Plans.Remove(added!)));
            Plans.Add(added);
        }
        IsWorking = false;
        Show(new(true, files.Count == 1
            ? "The install is planned below. Check what it writes, then press Install."
            : "The installs are planned below. Check what each one writes, then press Install on it."));
    }

    [RelayCommand(CanExecute = nameof(CanChange))]
    private async Task InstallAsync()
    {
        IReadOnlyList<string> files = await dialogs.PickFilesAsync("Install mods", "Mod zips", ["*.zip", "*.dll"], many: true);
        if (files.Count > 0)
        {
            await PlanAsync(files);
        }
    }

    [RelayCommand(CanExecute = nameof(CanChange))]
    private Task RefreshAsync() => Refresh();

    [RelayCommand(CanExecute = nameof(CanUndo))]
    private async Task UndoAsync()
    {
        if (session.Folder is not { } folder || LastChange is not { } change)
        {
            return;
        }
        string what = PlayMods.Describe(change);
        if (!await dialogs.ConfirmAsync("Undo the last change?",
                $"This takes back {what}: {PlayPaths.Shown(change.Mod)} goes back to how it was before. It cannot be undone again.", "Undo"))
        {
            return;
        }
        IsWorking = true;
        ModResult result = await Task.Run(() => ModChanges.Undo(folder, backups, GameProcess.IsRunning()));
        IsWorking = false;
        Show(result);
        ShowNextStart |= result.Ok;
        await Refresh();
    }

    [RelayCommand]
    private void OpenSetup() => navigator.Navigate(PageIds.Setup);

    private bool CanChange() => !IsWorking;

    private bool CanUndo() => !IsWorking && LastChange is not null;

    private async Task Refresh()
    {
        int run = ++version;
        OnPropertyChanged(nameof(NeedsGame));
        OnPropertyChanged(nameof(HasGame));
        if (session.Folder is not { } folder)
        {
            Clear();
            return;
        }
        string? selected = SelectedMod?.Name;
        IsLoading = true;
        Snapshot snapshot = await Task.Run(() => Read(folder));
        if (run != version)
        {
            return;
        }
        IsLoading = false;
        if (snapshot.Error is not null)
        {
            Show(new(false, snapshot.Error));
        }
        Mods = snapshot.Mods.Select(mod => new PlayModItem(mod, snapshot.Unsafe.Contains(mod.Name),
            ModBadges.InConflict(snapshot.Conflicts, mod.Name),
            new AsyncRelayCommand(() => RemoveAsync(mod)), ToggleAsync)).ToList();
        SelectedMod = Mods.FirstOrDefault(item => item.Name == selected) ?? Mods.FirstOrDefault();
        OnPropertyChanged(nameof(NoMods));
        Conflicts = snapshot.Conflicts.Select(conflict => new ConflictItem(conflict)).ToList();
        if (SelectedTab == ConflictsTab && !HasConflicts)
        {
            SelectedTab = ModTab;
        }
        LastChange = snapshot.Last;
        SetLog(snapshot.Log);

        CheckState = "Checking the mods the way Canopy does as the game starts...";
        CheckSummary = "";
        GeneralProblems = [];
        ValidationReport report = await Task.Run(() => CanopyCore.Validate(folder));
        if (run != version)
        {
            return;
        }
        SetReport(folder, report, snapshot.Mods);
    }

    private Snapshot Read(string folder)
    {
        try
        {
            IReadOnlyList<ModEntry> mods = ModInventory.Scan(folder);
            HashSet<string> risky = mods.Where(mod => !mod.IsDllFile && ModBadges.ReadUnsafeLua(PlayPaths.Mod(folder, mod.Name)))
                .Select(mod => mod.Name).ToHashSet(StringComparer.Ordinal);
            return new(mods, risky, ModInventory.Conflicts(mods), ModChanges.Last(folder, backups), CanopyLog.Read(folder), null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new([], new HashSet<string>(), [], null, CanopyLog.Read(folder),
                $"The mods folder could not be read: {exception.Message}");
        }
    }

    private void SetReport(string folder, ValidationReport report, IReadOnlyList<ModEntry> scanned)
    {
        if (!report.Ok)
        {
            CheckState = $"Canopy's check could not run: {report.Error ?? $"{CanopyCore.LibraryFile} gave no reason."}";
            return;
        }
        CheckState = null;
        foreach (PlayModItem item in Mods)
        {
            item.Problems = PlayMods.Of(report.Diagnostics, item.Name).Select(diagnostic => Problem(folder, diagnostic)).ToList();
        }
        GeneralProblems = PlayMods.General(report.Diagnostics, scanned).Select(diagnostic => Problem(folder, diagnostic)).ToList();
        CheckSummary = report.Diagnostics.Count == 0
            ? "Canopy's check found nothing to report."
            : $"Canopy's check found {Count(report.Errors, "error")}, {Count(report.Warnings, "warning")} and {Count(report.Notes, "note")}.";
    }

    private ProblemItem Problem(string folder, ModDiagnostic diagnostic) => new(diagnostic, PlayMods.CanShow(diagnostic)
        ? new RelayCommand(() => navigator.OpenProject(PlayPaths.Mod(folder, diagnostic.Folder), diagnostic.Line))
        : null);

    private static string Count(int count, string what) => count == 1 ? $"1 {what}" : $"{count} {what}s";

    private void SetLog(CanopyLogRun log)
    {
        logLines = log.Lines;
        if (!log.Exists)
        {
            logLines = [];
            LogState = "There is no Canopy.log yet. It appears the first time the game starts with Canopy installed.";
        }
        else if (log.Error is not null)
        {
            LogState = log.Error;
        }
        else
        {
            LogState = null;
        }
        ShowLog();
    }

    partial void OnOnlyProblemsChanged(bool value) => ShowLog();

    private void ShowLog()
    {
        IReadOnlyList<string> shown = OnlyProblems ? CanopyLog.Problems(logLines) : logLines;
        LogText = string.Join(Environment.NewLine, shown);
        if (OnlyProblems && shown.Count == 0 && logLines.Count > 0)
        {
            LogState = "The last run has no lines with a warning, error or failure.";
        }
        else if (LogState is not null && logLines.Count > 0)
        {
            LogState = null;
        }
    }

    private async Task ToggleAsync(PlayModItem item, bool on)
    {
        // Lets the switch finish its own change before it may be put back.
        await Task.Yield();
        if (session.Folder is not { } folder || IsWorking)
        {
            item.SetOn(!on);
            return;
        }
        IsWorking = true;
        ModResult result = await Task.Run(() => GameProcess.IsRunning()
            ? new ModResult(false, GameProcess.CloseFirstText)
            : DisabledMods.SetEnabled(folder, item.Name, on));
        IsWorking = false;
        Show(result);
        if (!result.Ok)
        {
            item.SetOn(!on);
            return;
        }
        ShowNextStart = true;
        await Refresh();
    }

    private async Task InstallPlanAsync(PlanItem item)
    {
        if (session.Folder is not { } folder || IsWorking || !item.Plan.Ok)
        {
            return;
        }
        ModInstallPlan plan = item.Plan;
        if (plan.Replaces.Count > 0 || plan.Removes.Count > 0)
        {
            string message = $"Installing {item.FileName} replaces {Count(plan.Replaces.Count, "file")} and removes "
                + $"{Count(plan.Removes.Count, "file")} in {PlayPaths.Shown(plan.Mod)}. A copy of each is kept, so Undo last change can put them back.";
            if (!await dialogs.ConfirmAsync($"Replace files in {PlayPaths.Shown(plan.Mod)}?", message, "Install"))
            {
                return;
            }
        }
        IsWorking = true;
        ModResult result = await Task.Run(() => ModChanges.Apply(folder, plan, backups, GameProcess.IsRunning(), DateTime.Now));
        IsWorking = false;
        Show(result);
        if (result.Ok)
        {
            Plans.Remove(item);
            ShowNextStart = true;
        }
        await Refresh();
    }

    private async Task RemoveAsync(ModEntry mod)
    {
        if (session.Folder is not { } folder || IsWorking)
        {
            return;
        }
        if (!await dialogs.ConfirmAsync($"Remove {PlayMods.Title(mod)}?",
                $"{PlayPaths.Shown(mod.Name)} and everything in it is deleted from the game folder. A copy is kept, so Undo last change can bring it back.",
                "Remove"))
        {
            return;
        }
        IsWorking = true;
        ModResult result = await Task.Run(() => ModChanges.Remove(folder, mod.Name, backups, GameProcess.IsRunning(), DateTime.Now));
        IsWorking = false;
        Show(result);
        ShowNextStart |= result.Ok;
        await Refresh();
    }

    // The plan for one file, or a refused plan saying why it was not read.
    private static ModInstallPlan PlanFile(string folder, string file)
    {
        string name = Path.GetFileName(file);
        try
        {
            if (new FileInfo(file).Length > MaxInstallBytes)
            {
                return Refused($"{name} is over 1 GB, more than any mod may unpack to, so it was not read.");
            }
            return ModZip.Plan(folder, File.ReadAllBytes(file), name);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return Refused($"{name} could not be read: {exception.Message}");
        }
    }

    private static ModInstallPlan Refused(string problem) => new("", false, [], [], [], [problem]);

    private void Show(ModResult result)
    {
        Status = result.Message;
        StatusFailed = !result.Ok;
    }

    private void Clear()
    {
        IsLoading = false;
        Mods = [];
        SelectedMod = null;
        Conflicts = [];
        GeneralProblems = [];
        CheckState = null;
        CheckSummary = "";
        LastChange = null;
        Status = null;
        ShowNextStart = false;
        Plans.Clear();
        SetLog(CanopyLogRun.None);
        OnPropertyChanged(nameof(NoMods));
    }

    private void OnGameChanged(object? sender, EventArgs e)
    {
        Plans.Clear();
        Status = null;
        ShowNextStart = false;
        _ = Refresh();
    }

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs e)
    {
        OnPropertyChanged(nameof(NeedsGame));
        OnPropertyChanged(nameof(HasGame));
    }
}
