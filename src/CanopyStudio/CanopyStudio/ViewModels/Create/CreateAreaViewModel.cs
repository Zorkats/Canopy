using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// The Create area: making a Canopy mod without knowing the ini format.
// With no project open it shows the start (new, open, recent); with one, the
// project's sections, forms, raw canopy.ini and problems. Panels that point
// at a place in the project move here through ICreateNavigator.
public sealed partial class CreateAreaViewModel : PageViewModel, ICreateNavigator
{
    private readonly GameSession session;
    private readonly IGameAssets assets;
    private readonly IStudioDialogs dialogs;
    private IReadOnlyList<SectionSchema>? kinds;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasProject))]
    [NotifyPropertyChangedFor(nameof(ShowStart))]
    [NotifyCanExecuteChangedFor(nameof(ReloadCommand))]
    [NotifyCanExecuteChangedFor(nameof(CloseCommand))]
    private ProjectViewModel? project;

    public CreateAreaViewModel(GameSession session, IGameAssets assets, IStudioDialogs dialogs)
    {
        this.session = session;
        this.assets = assets;
        this.dialogs = dialogs;
        Start = new CreateStartViewModel(session, dialogs, folder => OpenProject(folder, 0), OnCreated);
    }

    public override string Id => PageIds.Create;
    public override string Title => "Create";

    public CreateStartViewModel Start { get; }
    public bool HasProject => Project is not null;
    public bool ShowStart => Project is null;

    // A project's editor asks for focus; the view finds the editor and
    // focuses it.
    public event EventHandler<FieldViewModel>? FocusRequested;

    public override void OnShown()
    {
        if (Project is null)
        {
            Start.RefreshRecent();
        }
    }

    // Opens the mod in folder, asking first when the open project has
    // unsaved changes, and shows canopy.ini's line when it is not 0. The
    // same folder again only shows the line.
    public async Task OpenProject(string folder, int line)
    {
        if (Project is { } open && RecentProjects.Same(open.Folder, folder))
        {
            if (line > 0)
            {
                open.ShowLine(line);
            }
            return;
        }
        if (!await MayLeaveAsync("open another mod"))
        {
            return;
        }
        if (Load(folder) && line > 0)
        {
            Project?.ShowLine(line);
        }
    }

    public bool ShowField(string section, string key) => Project?.ShowField(section, key) ?? false;

    public void ShowLine(int line) => Project?.ShowLine(line);

    [RelayCommand(CanExecute = nameof(HasProject))]
    private async Task Reload()
    {
        if (Project is not { } open)
        {
            return;
        }
        if (open.IsDirty && !await dialogs.ConfirmAsync("Reload from disk",
                $"{open.Title} has changes that are not saved. Reload the mod from disk and lose them?", "Reload"))
        {
            return;
        }
        (string? Section, int View) place = open.Place;
        if (Load(open.Folder))
        {
            Project?.Restore(place);
        }
    }

    [RelayCommand(CanExecute = nameof(HasProject))]
    private async Task Close()
    {
        if (await MayLeaveAsync("close it"))
        {
            Replace(null);
            Start.RefreshRecent();
        }
    }

    // True when nothing unsaved would be lost, or the user agreed to lose it.
    private async Task<bool> MayLeaveAsync(string action) =>
        Project is not { IsDirty: true } open
        || await dialogs.ConfirmAsync("Unsaved changes",
            $"{open.Title} has changes that are not saved. Lose them and {action}? Cancel goes back so you can save first.",
            "Lose changes");

    // Opens folder as the project. False when it could not; the open
    // project, if any, stays and the message says why.
    private bool Load(string folder)
    {
        ProjectOpenResult result = ModProject.Open(folder);
        if (!result.Ok)
        {
            Fail(result.Error ?? $"{folder} could not be opened.");
            return false;
        }
        return Show(result.Project!, null);
    }

    private void OnCreated(ProjectOpenResult result)
    {
        if (result.Project is { } made)
        {
            Show(made, result.Error);
        }
    }

    private bool Show(ModProject opened, string? note)
    {
        IReadOnlyList<SectionSchema>? schema = Kinds();
        if (schema is null)
        {
            return false;
        }
        Replace(new ProjectViewModel(opened, schema, session, assets, dialogs, this) { Message = note });
        session.Remember(current => current with
        {
            RecentProjects = RecentProjects.Add(current.RecentProjects, opened.Folder),
        });
        Start.Message = null;
        return true;
    }

    private void Replace(ProjectViewModel? next)
    {
        if (Project is { } old)
        {
            old.FocusRequested -= OnFocusRequested;
            old.Dispose();
        }
        if (next is not null)
        {
            next.FocusRequested += OnFocusRequested;
        }
        Project = next;
    }

    private void Fail(string why)
    {
        if (Project is { } open)
        {
            open.Message = why;
        }
        else
        {
            Start.Message = why;
        }
    }

    // The section kinds from canopy_core.dll, read once. Null, with the
    // reason shown, when the DLL cannot give them.
    private IReadOnlyList<SectionSchema>? Kinds()
    {
        if (kinds is not null)
        {
            return kinds;
        }
        try
        {
            SchemaInfo schema = CanopyCore.Sections();
            if (schema.Ok)
            {
                return kinds = schema.Sections;
            }
            Fail(schema.Error ?? $"{CanopyCore.LibraryFile} did not list the section kinds. Rebuild it from the same source.");
        }
        catch (DllNotFoundException)
        {
            Fail($"{CanopyCore.LibraryFile} was not found next to Canopy Studio, so mods cannot be read. Build the C++ tools first.");
        }
        return null;
    }

    private void OnFocusRequested(object? sender, FieldViewModel field) => FocusRequested?.Invoke(this, field);
}
