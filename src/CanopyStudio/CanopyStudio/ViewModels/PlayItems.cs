using System.Globalization;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Play;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

// One mod's row on the Play page. Flipping IsOn asks the page to turn the
// mod on or off; the page puts the switch back when that is refused.
public sealed class PlayModItem : ObservableObject
{
    private readonly Func<PlayModItem, bool, Task> toggle;
    private bool isOn;
    private IReadOnlyList<ProblemItem> problems = [];

    public PlayModItem(ModEntry mod, bool unsafeLua, bool inConflict, IRelayCommand remove, Func<PlayModItem, bool, Task> toggle)
    {
        Mod = mod;
        this.toggle = toggle;
        isOn = !mod.Disabled;
        Title = PlayMods.Title(mod);
        VersionText = PlayMods.Version(mod) is { } version ? $"v{version}" : "";
        Badges = ModBadges.For(mod, unsafeLua, inConflict);
        Remove = remove;
    }

    public ModEntry Mod { get; }
    public string Name => Mod.Name;
    public string Title { get; }
    public string VersionText { get; }
    public bool HasVersion => VersionText.Length > 0;
    // The name on disk, shown under the title when the two differ.
    public string Subtitle => Title == Name ? "" : Name;
    public bool HasSubtitle => Subtitle.Length > 0;
    public string Place => Mod.IsDllFile ? $"{PlayPaths.Shown(Name)}, a DLL file" : PlayPaths.Shown(Name);
    public IReadOnlyList<ModBadge> Badges { get; }
    public IRelayCommand Remove { get; }
    public string SwitchName => $"{Title} on";

    public bool IsOn
    {
        get => isOn;
        set
        {
            if (value == isOn)
            {
                return;
            }
            isOn = value;
            OnPropertyChanged();
            _ = toggle(this, value);
        }
    }

    public IReadOnlyList<ProblemItem> Problems
    {
        get => problems;
        set
        {
            problems = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasProblems));
            OnPropertyChanged(nameof(HasErrors));
            OnPropertyChanged(nameof(ProblemText));
            OnPropertyChanged(nameof(NoProblems));
        }
    }

    public bool HasProblems => problems.Count > 0;
    public bool NoProblems => problems.Count == 0;
    public bool HasErrors => problems.Any(problem => problem.IsError);
    public string ProblemText => problems.Count == 1 ? "1 problem" : $"{problems.Count} problems";

    // Puts the switch back without asking for a change.
    public void SetOn(bool value)
    {
        isOn = value;
        OnPropertyChanged(nameof(IsOn));
    }
}

// One message of Canopy's check. ShowMe opens the line in Create when the
// message is about one line of a mod's canopy.ini.
public sealed record ProblemItem(ModDiagnostic Diagnostic, IRelayCommand? ShowMe)
{
    public bool IsError => Diagnostic.Severity == "error";
    public string SeverityText => Diagnostic.Severity switch
    {
        "error" => "Error",
        "warning" => "Warning",
        _ => "Note",
    };
    public string Message => Diagnostic.Message;
    public string Where => (Diagnostic.Folder.Length > 0, Diagnostic.Line > 0) switch
    {
        (true, true) => string.Create(CultureInfo.InvariantCulture, $"{Diagnostic.Folder}, line {Diagnostic.Line}"),
        (true, false) => Diagnostic.Folder,
        (false, true) => string.Create(CultureInfo.InvariantCulture, $"line {Diagnostic.Line}"),
        _ => "",
    };
    public bool HasWhere => Where.Length > 0;
    public bool CanShow => ShowMe is not null;
}

// One game path two or more turned-on mods ship.
public sealed record ConflictItem(OverrideConflict Conflict)
{
    public string Key => Conflict.Key;
    public string ModsText => "Shipped by " + string.Join(", then ", Conflict.Mods) + ".";
    public string WinnerText => $"{Conflict.Winner} wins: it loads last.";
}

// What installing one file will do, before anything is written.
public sealed class PlanItem(string file, ModInstallPlan plan, IRelayCommand install, IRelayCommand dismiss)
{
    // Lists longer than this end with a count of the rest.
    private const int MaxShown = 200;

    public string File { get; } = file;
    public string FileName => Path.GetFileName(File);
    public ModInstallPlan Plan { get; } = plan;
    public IRelayCommand Install { get; } = install;
    public IRelayCommand Dismiss { get; } = dismiss;

    public bool CanInstall => Plan.Ok;
    public string Target => Plan.Ok ? $"Installs as {PlayPaths.Shown(Plan.Mod)}" : "Cannot be installed";
    public string Summary => Plan.Ok
        ? string.Create(CultureInfo.CurrentCulture,
            $"Writes {Plan.Writes.Count:N0} files: {Plan.Writes.Count - Plan.Replaces.Count:N0} new, {Plan.Replaces.Count:N0} replaced. Removes {Plan.Removes.Count:N0}.")
        : "";
    public IReadOnlyList<string> Problems => Plan.Problems;
    public bool HasProblems => Plan.Problems.Count > 0;
    public bool HasReplaces => Plan.Replaces.Count > 0;
    public bool HasRemoves => Plan.Removes.Count > 0;
    public bool HasSkipped => Plan.Skipped.Count > 0;
    public string WritesText => Lines(Plan.Writes.Select(write => write.Path));
    public string ReplacesText => Lines(Plan.Replaces);
    public string RemovesText => Lines(Plan.Removes);
    public string SkippedText => Lines(Plan.Skipped);

    private static string Lines(IEnumerable<string> paths)
    {
        List<string> all = paths.ToList();
        string shown = string.Join(Environment.NewLine, all.Take(MaxShown));
        return all.Count > MaxShown
            ? shown + Environment.NewLine + string.Create(CultureInfo.CurrentCulture, $"and {all.Count - MaxShown:N0} more")
            : shown;
    }
}
