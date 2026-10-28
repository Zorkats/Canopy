using System.Collections.ObjectModel;
using CanopyStudio.Core.Install;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Setup;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels;

// How first-time setup ended: the page the shell opens next, and whether
// setup now counts as done.
public sealed record OnboardingFinished(string PageId, bool SetupDone);

// One step in the indicator at the top of first-time setup.
public sealed partial class OnboardingStepItem(OnboardingStep step, string name) : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AccessibleName))]
    private bool isCurrent;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowNumber))]
    [NotifyPropertyChangedFor(nameof(AccessibleName))]
    private OnboardingState state;

    public OnboardingStep Step { get; } = step;
    public string Name { get; } = name;
    public string Number { get; } = ((int)step + 1).ToString();

    public bool IsDone => State == OnboardingState.Done;
    public bool IsSkipped => State == OnboardingState.Skipped;

    // A done step shows a check instead of its number.
    public bool ShowNumber => !IsDone;

    public string AccessibleName => $"Step {Number}, {Name}" + (IsCurrent ? ", current" : "") + State switch
    {
        OnboardingState.Done => ", done",
        OnboardingState.Skipped => ", skipped",
        _ => "",
    };

    partial void OnStateChanged(OnboardingState value)
    {
        OnPropertyChanged(nameof(IsDone));
        OnPropertyChanged(nameof(IsSkipped));
    }
}

// A game folder offered in first-time setup, with what Setup's checks say
// about it. Only a folder whose archives open can be used, as on Setup.
public sealed partial class OnboardingFolder(string folder) : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanUse))]
    private GameInfo? info;

    [ObservableProperty]
    private bool isChecking;

    public string Folder { get; } = folder;

    public ObservableCollection<CheckItem> Checks { get; } = [];

    public bool CanUse => Info is { Ok: true };

    // Reads the folder the way Setup's Check does: canopy_core.dll for the
    // game, the installer for what its dinput8.dll is.
    public async Task CheckAsync()
    {
        IsChecking = true;
        bool exists = Directory.Exists(Folder);
        GameInfo found = exists ? await GameSession.InspectAsync(Folder) : new GameInfo { Folder = Folder };
        LoaderKind? loader = exists ? await Task.Run(() => CanopyInstaller.Inspect(Folder).Loader) : null;
        Checks.Clear();
        foreach (SetupCheck check in SetupChecks.For(found, exists, loader))
        {
            Checks.Add(new CheckItem(check));
        }
        Info = found;
        IsChecking = false;
    }
}
