using System.Collections.ObjectModel;
using System.ComponentModel;
using Avalonia.Threading;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// One open mod project in the Create area: its content gallery, the drawer
// with the picked tile's form or the draft of a new section, the raw
// canopy.ini, the Lua files, and the live problems. The gallery follows the project as the
// raw view changes it. Dispose it when the project closes.
public sealed partial class ProjectViewModel : ObservableObject, IDisposable
{
    public const int FormView = 0;
    public const int RawView = 1;
    public const int LuaView = 2;

    private const string PatchPrefix = "patch.";

    private readonly IReadOnlyList<SectionSchema> kinds;
    private readonly GameSession session;
    private readonly IGameAssets assets;
    private readonly IStudioDialogs dialogs;
    private readonly ContentImages images;
    private readonly Dictionary<string, ContentGroupViewModel> groupViews = new();
    // The catalog as the last rebuild read it, advanced groups included.
    private IReadOnlyList<ContentGroup> catalog = [];
    // True while the gallery is rebuilt, so the tile it loses and gets back
    // does not reopen the form or change the drawer.
    private bool rebuilding;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection))]
    [NotifyPropertyChangedFor(nameof(HasOtherNote))]
    [NotifyPropertyChangedFor(nameof(OtherNote))]
    [NotifyPropertyChangedFor(nameof(CanRename))]
    [NotifyPropertyChangedFor(nameof(IsDrawerOpen))]
    [NotifyPropertyChangedFor(nameof(ShowDrawer))]
    [NotifyPropertyChangedFor(nameof(DrawerKindTitle))]
    [NotifyPropertyChangedFor(nameof(SectionText))]
    [NotifyPropertyChangedFor(nameof(RenameProblem))]
    [NotifyPropertyChangedFor(nameof(HasRenameProblem))]
    [NotifyCanExecuteChangedFor(nameof(StartRenameCommand))]
    [NotifyCanExecuteChangedFor(nameof(RemoveCommand))]
    [NotifyCanExecuteChangedFor(nameof(ConfirmRenameCommand))]
    private ContentTileViewModel? selectedTile;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasForm))]
    private SectionFormViewModel? form;

    // The draft of a new section, while one is being made.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsCreating))]
    [NotifyPropertyChangedFor(nameof(IsDrawerOpen))]
    [NotifyPropertyChangedFor(nameof(ShowDrawer))]
    [NotifyPropertyChangedFor(nameof(DrawerKindTitle))]
    private NewSectionViewModel? newSection;

    // FormView, RawView or LuaView: the Content gallery, canopy.ini or the
    // Lua files.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowForm))]
    [NotifyPropertyChangedFor(nameof(ShowRaw))]
    [NotifyPropertyChangedFor(nameof(ShowLua))]
    [NotifyPropertyChangedFor(nameof(ShowDrawer))]
    private int viewIndex;

    // Whether the gallery shows the raw rows and the sections Canopy does not read.
    [ObservableProperty]
    private bool showAdvanced;

    // What the last action could not do, null when it worked.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasMessage))]
    private string? message;

    [ObservableProperty]
    private bool isRenaming;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(RenameProblem))]
    [NotifyPropertyChangedFor(nameof(HasRenameProblem))]
    [NotifyCanExecuteChangedFor(nameof(ConfirmRenameCommand))]
    private string renameKey = "";

    // Whether the problems list under the gallery is open.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ProblemsToggleText))]
    private bool isProblemsOpen;

    public ProjectViewModel(ModProject project, IReadOnlyList<SectionSchema> kinds, GameSession session,
        IGameAssets assets, IStudioDialogs dialogs, ICreateNavigator navigator)
    {
        Project = project;
        this.kinds = kinds;
        this.session = session;
        this.assets = assets;
        this.dialogs = dialogs;
        images = new ContentImages(assets);
        Raw = new RawIniViewModel(project);
        Problems = new ProblemsViewModel(project, () => session.Folder, navigator, Raw);
        Lua = new LuaViewModel(project.Folder, () => SwitchText.Read(project.Value("mod", "unsafe")) == true);
        Lua.DirtyChanged += OnLuaDirtyChanged;
        Problems.Checked += OnChecked;
        Problems.PropertyChanged += OnProblemsChanged;
        project.Changed += OnProjectChanged;
        RebuildGallery();
    }

    public ModProject Project { get; }
    public RawIniViewModel Raw { get; }
    public ProblemsViewModel Problems { get; }
    public LuaViewModel Lua { get; }

    public string Title => Project.Name;
    public string Folder => Project.Folder;
    // canopy.ini or a Lua file has changes that are not saved.
    public bool IsDirty => Project.IsDirty || Lua.IsDirty;

    // The gallery's groups in order. The advanced ones are there only while
    // ShowAdvanced is on.
    public ObservableCollection<ContentGroupViewModel> Groups { get; } = [];

    public bool ShowForm => ViewIndex == FormView;
    public bool ShowRaw => ViewIndex == RawView;
    public bool ShowLua => ViewIndex == LuaView;
    public bool HasMessage => Message is not null;

    public bool HasForm => Form is not null;
    public bool IsCreating => NewSection is not null;
    public bool IsDrawerOpen => SelectedTile is not null || NewSection is not null;
    public bool ShowDrawer => ShowForm && IsDrawerOpen;

    // The kind's title in the drawer's header: the draft's kind, else the tile's.
    public string DrawerKindTitle => NewSection?.KindTitle ?? SelectedTile?.KindTitle ?? "";

    // The picked section as canopy.ini writes it, such as [item.Moss].
    public string SectionText => SelectedTile?.Section is { } section ? $"[{section}]" : "";

    public bool HasSelection => SelectedTile is not null;
    // Patch tiles are Game edits with no form, so they cannot be renamed or removed.
    public bool CanRename => SelectedTile is { Tile: not null, Kind: { IsSingle: false } kind } && !IsPatch(kind);

    // Why the picked section has no form, null when it has one.
    public string? OtherNote => SelectedTile switch
    {
        { Tile: not null, Kind: null } => "Canopy does not read this section. Edit it in canopy.ini.",
        { Tile: not null, Kind: { } kind } when IsPatch(kind) => "A Game edit. Change it in the Game area, or in canopy.ini.",
        _ => null,
    };

    public bool HasOtherNote => OtherNote is not null;

    private static bool IsPatch(SectionSchema kind) => SectionNames.Same(kind.Prefix, PatchPrefix);

    public string ProblemsSummary => ProblemCount.Summary(Problems.Errors, Problems.Warnings);
    public bool HasProblemErrors => Problems.Errors > 0;
    public string ProblemsToggleText => IsProblemsOpen ? "Hide" : "Show";

    // Why the picked section cannot take RenameKey as its key, null when it can.
    public string? RenameProblem =>
        SelectedTile is { Section: { } section, Kind: { } kind } && RenameKey.Trim().Length > 0
            ? SectionNames.RenameProblem(Project.SectionNames, section, kind, RenameKey.Trim())
            : null;

    public bool HasRenameProblem => RenameProblem is not null;

    // Asks the view to bring an editor into view and focus it.
    public event EventHandler<FieldViewModel>? FocusRequested;

    // Saves canopy.ini, then the Lua files. False when it could not; Message
    // says why.
    public bool Save()
    {
        Message = Project.Save() ?? Lua.Save();
        OnPropertyChanged(nameof(IsDirty));
        return Message is null;
    }

    // Picks section's tile, which opens its form in the drawer. The tile is
    // revealed first when it is in the advanced groups. Never changes the view.
    // False when the project has no such section.
    public bool Select(string section)
    {
        if (FindTile(section) is null && IsAdvanced(section))
        {
            ShowAdvanced = true;
        }
        if (FindTile(section) is not { } tile)
        {
            return false;
        }
        SelectedTile = tile;
        return true;
    }

    // ICreateNavigator.ShowField for this project. The Content view picks the
    // section's tile and focuses the field in its form. The canopy.ini view
    // shows the field's line instead.
    public bool ShowField(string section, string key)
    {
        if (!Select(section))
        {
            return false;
        }
        if (ShowRaw)
        {
            ShowLineOf(section, key);
            return true;
        }
        if (key.Length == 0)
        {
            return true;
        }
        if (Form?.FieldFor(key) is not { } field || field.Place == FieldPlace.Ignored)
        {
            // A key the form does not show: a key its kind ignores, a key the
            // schema does not know, or a section with no form.
            ShowLineOf(section, key);
            return true;
        }
        Form?.Reveal(field);
        FocusRequested?.Invoke(this, field);
        return true;
    }

    public void ShowLine(int line)
    {
        ViewIndex = RawView;
        if (line > 0)
        {
            // After the raw view is laid out, so it can scroll.
            Dispatcher.UIThread.Post(() => Raw.ShowLine(line), DispatcherPriority.Background);
        }
    }

    // The picked section and view, to keep them across a reload.
    public (string? Section, int View) Place => (SelectedTile?.Section, ViewIndex);

    public void Restore((string? Section, int View) place)
    {
        if (place.Section is { } section)
        {
            Select(section);
        }
        ViewIndex = place.View;
    }

    public void Dispose()
    {
        Project.Changed -= OnProjectChanged;
        Problems.Checked -= OnChecked;
        Problems.PropertyChanged -= OnProblemsChanged;
        Lua.DirtyChanged -= OnLuaDirtyChanged;
        Form?.Dispose();
        Form = null;
        images.Dispose();
        // The raw view and the problems panel may hold an editor or a timer.
        ((object)Problems as IDisposable)?.Dispose();
        ((object)Raw as IDisposable)?.Dispose();
    }

    [RelayCommand]
    private void SaveNow() => Save();

    private void OnLuaDirtyChanged(object? sender, EventArgs e) => OnPropertyChanged(nameof(IsDirty));

    [RelayCommand]
    private void CloseDrawer()
    {
        SelectedTile = null;
        NewSection = null;
        IsRenaming = false;
    }

    [RelayCommand]
    private void ToggleProblems() => IsProblemsOpen = !IsProblemsOpen;

    [RelayCommand]
    private void ShowSelectedInIni()
    {
        if (SelectedTile?.Section is { } section)
        {
            ShowLine(Project.LineOf(section));
        }
    }

    [RelayCommand(CanExecute = nameof(CanRename))]
    private void StartRename()
    {
        if (SelectedTile is not { Section: { } section, Kind: { } kind })
        {
            return;
        }
        RenameKey = SectionNames.KeyOf(section, kind);
        IsRenaming = true;
    }

    [RelayCommand(CanExecute = nameof(CanConfirmRename))]
    private void ConfirmRename()
    {
        if (SelectedTile is not { Section: { } section, Kind: { } kind })
        {
            return;
        }
        string name = SectionNames.Name(kind, RenameKey.Trim());
        if (name == section)
        {
            IsRenaming = false;
            return;
        }
        Message = Project.Edit("renameSection", section, value: name, source: this);
        if (Message is null)
        {
            IsRenaming = false;
            Select(name);
        }
    }

    private bool CanConfirmRename => RenameKey.Trim().Length > 0 && RenameProblem is null;

    [RelayCommand]
    private void CancelRename() => IsRenaming = false;

    [RelayCommand(CanExecute = nameof(CanRename))]
    private async Task Remove()
    {
        if (SelectedTile?.Section is not { } section)
        {
            return;
        }
        bool sure = await dialogs.ConfirmAsync("Remove a section",
            $"Remove [{section}] and every value in it from canopy.ini? The file on disk changes only when you save.",
            "Remove");
        if (sure)
        {
            Message = Project.Edit("removeSection", section, source: this);
        }
    }

    // Opens the draft that adds a section of the group's kind. The drawer
    // shows the draft in place of a tile.
    private void StartNew(ContentGroupViewModel group)
    {
        if (group.Kind is not { } kind)
        {
            return;
        }
        SelectedTile = null;
        IsRenaming = false;
        NewSection = new NewSectionViewModel(kind, () => Project.SectionNames, AddNewSection, CancelNewSection);
    }

    private void CancelNewSection() => NewSection = null;

    // Adds the draft's section, with its name or label when one was typed. A
    // draft whose section could not be added stays open, and Message says why.
    private void AddNewSection(NewSectionViewModel draft)
    {
        string section = draft.SectionName;
        string? failure = Project.Edit("addSection", section, source: this);
        if (failure is null && draft.NameKey is { } nameKey && draft.Name.Trim() is { Length: > 0 } name)
        {
            failure = Project.Edit("set", section, nameKey, name, source: this);
        }
        Message = failure;
        if (failure is not null && !Project.HasSection(section))
        {
            return;
        }
        NewSection = null;
        Select(section);
    }

    // Picks a tile from the gallery. A "+ New" tile never reaches here: its
    // group opens the draft instead.
    private void OpenTile(ContentTileViewModel tile)
    {
        if (tile.Tile is not null)
        {
            SelectedTile = tile;
        }
    }

    // Shows the field's line in canopy.ini, or the section's line when the
    // field has none.
    private void ShowLineOf(string section, string key)
    {
        int line = Project.LineOf(section, key);
        ShowLine(line > 0 ? line : Project.LineOf(section));
    }

    partial void OnShowAdvancedChanged(bool value) => RebuildGallery();

    partial void OnSelectedTileChanged(ContentTileViewModel? oldValue, ContentTileViewModel? newValue)
    {
        if (rebuilding)
        {
            return;
        }
        if (oldValue is not null)
        {
            oldValue.IsSelected = false;
        }
        if (newValue is not null)
        {
            newValue.IsSelected = true;
            NewSection = null;
        }
        if (!SameSection(oldValue, newValue))
        {
            IsRenaming = false;
        }
        OpenForm(newValue);
    }

    private static bool SameSection(ContentTileViewModel? left, ContentTileViewModel? right) =>
        left?.Section is { } first && right?.Section is { } second && SectionNames.Same(first, second);

    // Shows tile's form in the drawer. The open form is kept while it is still
    // the form of the same section and kind. A patch, or a section no kind
    // takes, has no form.
    private void OpenForm(ContentTileViewModel? tile)
    {
        if (tile is { Section: { } section, Kind: { } kind } && !IsPatch(kind))
        {
            if (Form is { } open && ReferenceEquals(open.Kind, kind) && SectionNames.Same(open.Section, section))
            {
                return;
            }
            Form?.Dispose();
            Form = new SectionFormViewModel(Project, section, kind, assets, session.Folder, Problems.ProblemsAt);
            return;
        }
        Form?.Dispose();
        Form = null;
    }

    // Rebuilds the gallery from the project: the groups the Advanced switch
    // allows, their tiles, and the picked tile, found again by section. The
    // drawer's form is kept when it is still the picked section's.
    private void RebuildGallery()
    {
        catalog = ContentCatalog.Groups(Project, kinds);
        List<ContentGroupViewModel> next = [];
        foreach (ContentGroup group in catalog)
        {
            if (group.Advanced && !ShowAdvanced)
            {
                continue;
            }
            if (!groupViews.TryGetValue(group.Id, out ContentGroupViewModel? view))
            {
                view = new ContentGroupViewModel(group.Id, images, OpenTile, StartNew);
                groupViews[group.Id] = view;
            }
            view.Update(group);
            next.Add(view);
        }
        string? picked = SelectedTile?.Section;
        rebuilding = true;
        try
        {
            CollectionSync.To(Groups, next);
            SelectedTile = picked is null ? null : FindTile(picked);
            foreach (ContentTileViewModel tile in Groups.SelectMany(group => group.Tiles))
            {
                tile.IsSelected = ReferenceEquals(tile, SelectedTile);
            }
        }
        finally
        {
            rebuilding = false;
        }
        if (SelectedTile is null)
        {
            IsRenaming = false;
        }
        OpenForm(SelectedTile);
        // Closes a draft whose kind's group the Advanced switch has hidden.
        if (NewSection is { } draft && !Groups.Any(group => ReferenceEquals(group.Kind, draft.Kind)))
        {
            NewSection = null;
        }
        RefreshProblemCounts();
        NewSection?.Refresh();
    }

    // Sets each visible tile's error and warning counts from the last check.
    private void RefreshProblemCounts()
    {
        var rows = Problems.Rows;
        foreach (ContentTileViewModel tile in Groups.SelectMany(group => group.Tiles))
        {
            if (tile.Section is not { } section)
            {
                continue;
            }
            tile.Errors = rows.Count(row => row.IsError && row.Place is { } place && SectionNames.Same(place.Section, section));
            tile.Warnings = rows.Count(row => row.IsWarning && row.Place is { } place && SectionNames.Same(place.Section, section));
        }
    }

    private ContentTileViewModel? FindTile(string section) =>
        Groups.SelectMany(group => group.Tiles)
            .FirstOrDefault(tile => tile.Section is { } known && SectionNames.Same(known, section));

    // Whether section is in one of the advanced groups.
    private bool IsAdvanced(string section) =>
        catalog.Any(group => group.Advanced && group.Tiles.Any(tile => SectionNames.Same(tile.Section, section)));

    private void OnProjectChanged(object? sender, ProjectChangedEventArgs change)
    {
        RebuildGallery();
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(RenameProblem));
        OnPropertyChanged(nameof(HasRenameProblem));
        ConfirmRenameCommand.NotifyCanExecuteChanged();
    }

    private void OnChecked(object? sender, EventArgs e)
    {
        if (Dispatcher.UIThread.CheckAccess())
        {
            RefreshChecked();
        }
        else
        {
            Dispatcher.UIThread.Post(() => RefreshChecked());
        }
    }

    private void RefreshChecked()
    {
        RefreshProblemCounts();
        Form?.RefreshProblems();
    }

    private void OnProblemsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(ProblemsViewModel.Errors) or nameof(ProblemsViewModel.Warnings))
        {
            OnPropertyChanged(nameof(ProblemsSummary));
            OnPropertyChanged(nameof(HasProblemErrors));
        }
    }
}
