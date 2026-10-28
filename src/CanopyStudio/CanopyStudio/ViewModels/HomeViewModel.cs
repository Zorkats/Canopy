using System.ComponentModel;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels;

public sealed partial class HomeViewModel : PageViewModel
{
    private readonly GameSession session;
    private readonly IStudioNavigator navigator;

    public HomeViewModel(GameSession session, IStudioNavigator navigator, CanopyService canopy)
    {
        this.session = session;
        this.navigator = navigator;
        Canopy = canopy;
        session.PropertyChanged += OnSessionChanged;
        canopy.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(CanopyService.HomeText))
            {
                OnPropertyChanged(nameof(CanopyText));
            }
        };
    }

    // Canopy in the folder in use, with its install and update buttons.
    public CanopyService Canopy { get; }

    public override string Id => PageIds.Home;
    public override string Title => "Home";

    public bool HasGame => session.HasGame;
    public bool NeedsGame => !session.HasGame;
    public string GameFolder => session.Folder ?? "";
    public string FilesText => session.Game?.Files.ToString("N0") ?? "";
    public string TablesText => session.Game?.Tables.ToString("N0") ?? "";
    public string ArchivesText => session.Game?.Archives.Count.ToString("N0") ?? "";
    public string VersionText => session.Game is { Supported: true } ? "Checked" : $"Build {session.Game?.Build:X8}";
    public bool IsUnsupported => session.Game is { Ok: true, Supported: false };
    public string CanopyText => Canopy.HomeText;

    [RelayCommand]
    private void OpenFiles() => navigator.Navigate(PageIds.Files);

    [RelayCommand]
    private void OpenTables() => navigator.Navigate(PageIds.Tables);

    [RelayCommand]
    private void OpenSetup() => navigator.Navigate(PageIds.Setup);

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs e)
    {
        foreach (string name in (string[])[nameof(HasGame), nameof(NeedsGame), nameof(GameFolder), nameof(FilesText),
                     nameof(TablesText), nameof(ArchivesText), nameof(VersionText), nameof(IsUnsupported)])
        {
            OnPropertyChanged(name);
        }
    }
}
