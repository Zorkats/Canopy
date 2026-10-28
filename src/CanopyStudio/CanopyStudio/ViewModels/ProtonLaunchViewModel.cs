using CanopyStudio.Core.Platform;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

// The Steam launch option the game needs under Proton, with a button that
// copies it, shown by Setup and first-time setup on Linux only. The user
// pastes it into Steam; Canopy Studio never edits Steam's files.
public sealed partial class ProtonLaunchViewModel(IStudioDialogs dialogs) : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasOutcome))]
    private string outcome = "";

    public bool Needed => ProtonLaunch.Needed;
    public string Explanation => ProtonLaunch.Explanation;
    public string LaunchOption => ProtonLaunch.LaunchOption;
    public bool HasOutcome => Outcome.Length > 0;

    [RelayCommand]
    private async Task Copy()
    {
        Outcome = await dialogs.CopyTextAsync(ProtonLaunch.LaunchOption)
            ? "Copied. In Steam, right-click Village in the Shade, pick Properties, and paste it into Launch Options."
            : "The launch option could not be copied. Select it above and copy it by hand.";
    }
}
