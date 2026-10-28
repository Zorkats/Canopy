using System.Collections.ObjectModel;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Platform;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// A mod folder opened before, newest first. A folder that is gone shows
// greyed and can only be removed from the list.
public sealed partial class RecentProjectItem(CreateStartViewModel owner, string folder) : ObservableObject
{
    public string Folder { get; } = folder;
    public string Name { get; } = Path.GetFileName(Path.TrimEndingDirectorySeparator(folder));
    public bool Exists { get; } = Directory.Exists(folder);
    public string State => Exists ? Folder : $"Not found: {Folder}";

    [RelayCommand(CanExecute = nameof(Exists))]
    private Task Open() => owner.OpenRecentAsync(this);

    [RelayCommand]
    private void Remove() => owner.RemoveRecent(this);
}

// The Create area while no project is open: a new mod from a template, a
// mod folder opened from disk, or one of the recent ones.
public sealed partial class CreateStartViewModel : ObservableObject
{
    private readonly GameSession session;
    private readonly IStudioDialogs dialogs;
    private readonly Func<string, Task> open;
    private readonly Action<ProjectOpenResult> created;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(NameProblem))]
    [NotifyPropertyChangedFor(nameof(HasNameProblem))]
    [NotifyPropertyChangedFor(nameof(FolderPreview))]
    [NotifyCanExecuteChangedFor(nameof(CreateCommand))]
    private string newName = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(FolderPreview))]
    [NotifyCanExecuteChangedFor(nameof(CreateCommand))]
    private string parent;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(CreateCommand))]
    private ProjectTemplate? selectedTemplate = ProjectTemplates.Empty;

    // What the last action could not do, null when it worked.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasMessage))]
    private string? message;

    public CreateStartViewModel(GameSession session, IStudioDialogs dialogs, Func<string, Task> open,
        Action<ProjectOpenResult> created)
    {
        this.session = session;
        this.dialogs = dialogs;
        this.open = open;
        this.created = created;
        parent = Path.Combine(StudioFolders.Documents(), "Canopy Mods");
        RefreshRecent();
    }

    public IReadOnlyList<ProjectTemplate> Templates => ProjectTemplates.All;

    public ObservableCollection<RecentProjectItem> Recent { get; } = [];
    public bool HasRecent => Recent.Count > 0;

    public bool HasMessage => Message is not null;

    // Shown once a name is typed.
    public string? NameProblem
    {
        get
        {
            string name = NewName.Trim();
            if (name.Length == 0)
            {
                return null;
            }
            if (!SectionNames.KeyValid(name))
            {
                return "A mod name is 1 to 32 letters, digits, '_' or '-', with no spaces. It is also the mod's key.";
            }
            string folder = FolderPreview;
            return Directory.Exists(folder) && Directory.EnumerateFileSystemEntries(folder).Any()
                ? $"{folder} already has files in it. Pick another name or folder."
                : null;
        }
    }

    public bool HasNameProblem => NameProblem is not null;

    public string FolderPreview => NewName.Trim().Length == 0 || Parent.Trim().Length == 0
        ? ""
        : Path.Combine(Parent.Trim(), NewName.Trim());

    // Lists the recent projects from the settings again.
    public void RefreshRecent()
    {
        Recent.Clear();
        foreach (string folder in session.Settings.RecentProjects ?? [])
        {
            Recent.Add(new RecentProjectItem(this, folder));
        }
        OnPropertyChanged(nameof(HasRecent));
    }

    internal Task OpenRecentAsync(RecentProjectItem item) => open(item.Folder);

    internal void RemoveRecent(RecentProjectItem item)
    {
        session.Remember(current => current with
        {
            RecentProjects = RecentProjects.Remove(current.RecentProjects, item.Folder),
        });
        RefreshRecent();
    }

    private bool CanCreate => NewName.Trim().Length > 0 && Parent.Trim().Length > 0 && SelectedTemplate is not null
        && NameProblem is null;

    [RelayCommand(CanExecute = nameof(CanCreate))]
    private void Create()
    {
        if (SelectedTemplate is not { } template)
        {
            return;
        }
        ProjectOpenResult result = ProjectTemplates.Create(Parent.Trim(), NewName.Trim(), template);
        Message = result.Error;
        if (result.Ok)
        {
            NewName = "";
            created(result);
        }
    }

    [RelayCommand]
    private async Task BrowseParent()
    {
        string? folder = await dialogs.PickFolderAsync("Choose where to make the mod's folder");
        if (folder is not null)
        {
            Parent = folder;
        }
    }

    [RelayCommand]
    private async Task OpenFolder()
    {
        string? folder = await dialogs.PickFolderAsync("Choose a mod folder");
        if (folder is not null)
        {
            await open(folder);
        }
    }
}
