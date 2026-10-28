using System.ComponentModel;
using System.Globalization;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Browse;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// The game's texts, table by table and language by language, searchable; an
// edit is saved as a patch in the edits mod. The index is built once per
// game folder, the first time the page is shown, from the grids of the
// patchable tables (GameSession caches them for Tables too).
public sealed partial class GameTextViewModel : PageViewModel
{
    private string? indexedFolder;
    private int buildVersion;
    private int filterVersion;
    private bool refilling;
    private GameTextQuery? query;
    private TextRowItem[] items = [];
    private GameTextPatches patches = GameTextPatches.Empty;
    private HashSet<int> changed = [];

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsReady))]
    [NotifyPropertyChangedFor(nameof(NoMatches))]
    [NotifyPropertyChangedFor(nameof(NoEditor))]
    private bool isBuilding;

    [ObservableProperty]
    private string progress = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasBuildError))]
    private string? buildError;

    // The tables that could not be read, null when every one was.
    [ObservableProperty]
    private string? unreadNote;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsReady))]
    [NotifyPropertyChangedFor(nameof(NoMatches))]
    [NotifyPropertyChangedFor(nameof(NoEditor))]
    [NotifyPropertyChangedFor(nameof(CountText))]
    private GameTextIndex? index;

    [ObservableProperty]
    private IReadOnlyList<TextTableOption> tableOptions = [];

    [ObservableProperty]
    private TextTableOption? selectedTable;

    [ObservableProperty]
    private IReadOnlyList<TextLanguage> languageOptions = [];

    [ObservableProperty]
    private IReadOnlyList<TextLanguage> secondOptions = [];

    [ObservableProperty]
    private TextLanguage? firstLanguage;

    [ObservableProperty]
    private TextLanguage? secondLanguage;

    [ObservableProperty]
    private string search = "";

    [ObservableProperty]
    private bool changedOnly;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CountText))]
    [NotifyPropertyChangedFor(nameof(NoMatches))]
    private IReadOnlyList<TextRowItem> visibleItems = [];

    [ObservableProperty]
    private TextRowItem? selectedItem;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasEditor))]
    [NotifyPropertyChangedFor(nameof(NoEditor))]
    private TextEditorViewModel? editor;

    public GameTextViewModel(GameSession session, GameEdits edits, IStudioNavigator navigator)
    {
        Session = session;
        Edits = edits;
        Navigator = navigator;
        session.GameChanged += (_, _) => Reset();
        session.PropertyChanged += (_, _) => OnPropertyChanged(nameof(NeedsGame));
        edits.Edited += (_, _) => OnEditsChanged();
        edits.PropertyChanged += OnEditsPropertyChanged;
    }

    public GameSession Session { get; }
    public GameEdits Edits { get; }
    public IStudioNavigator Navigator { get; }

    public override string Id => PageIds.Text;
    public override string Title => "Text";

    public bool NeedsGame => !Session.HasGame;
    public bool IsReady => Index is not null && !IsBuilding;
    public bool HasBuildError => BuildError is not null;
    public bool NoMatches => IsReady && VisibleItems.Count == 0;
    public bool HasEditor => Editor is not null;
    public bool NoEditor => Editor is null && IsReady;

    public string CountText => Index is null
        ? ""
        : string.Create(CultureInfo.CurrentCulture, $"{VisibleItems.Count:N0} of {Index.Entries.Count:N0} texts");

    public string ChangedText => changed.Count == 0
        ? ""
        : string.Create(CultureInfo.CurrentCulture, $"{changed.Count:N0} changed by the edits mod");

    public override void OnShown() => _ = BuildAsync();

    [RelayCommand]
    private void OpenSetup() => Navigator.Navigate(PageIds.Setup);

    [RelayCommand]
    private void ClearSearch() => Search = "";

    // Reads the patchable tables' grids, table by table, then builds the
    // index and the list's items off the UI thread.
    private async Task BuildAsync()
    {
        if (Session.Folder is not { } folder || folder == indexedFolder)
        {
            return;
        }
        indexedFolder = folder;
        int version = ++buildVersion;
        IsBuilding = true;
        BuildError = null;
        UnreadNote = null;
        Progress = "Reading the archives...";
        FileList list = await Session.FilesAsync();
        if (version != buildVersion)
        {
            return;
        }
        if (!list.Ok)
        {
            Fail(list.Error ?? "The game's file list could not be read.");
            return;
        }

        List<string> files = GamePaths.Tables(list.Files).Where(GameTextIndex.Holds).ToList();
        // Every read starts at once; they are awaited in order for the
        // progress line.
        List<Task<TableGrid>> reads = files.Select(Session.GridAsync).ToList();
        List<TableGrid> grids = [];
        List<string> unread = [];
        for (int at = 0; at < files.Count; ++at)
        {
            Progress = string.Create(CultureInfo.CurrentCulture, $"Reading {files[at]}, table {at + 1} of {files.Count}...");
            TableGrid grid = await reads[at];
            if (version != buildVersion)
            {
                return;
            }
            if (grid.Ok)
            {
                grids.Add(grid);
            }
            else
            {
                unread.Add(files[at]);
            }
        }

        Progress = "Building the text index...";
        (GameTextIndex built, GameTextQuery made, TextRowItem[] rows) = await Task.Run(() =>
        {
            GameTextIndex result = GameTextIndex.Build(grids);
            return (result, new GameTextQuery(result.Entries), result.Entries.Select((entry, at) => new TextRowItem(entry, at)).ToArray());
        });
        if (version != buildVersion)
        {
            return;
        }
        if (built.Entries.Count == 0)
        {
            Fail("No table the Text page reads could be read, so there are no texts to show.");
            return;
        }
        UnreadNote = unread.Count == 0
            ? null
            : $"Not read, so their texts are not listed: {string.Join(", ", unread)}.";
        query = made;
        items = rows;
        Load(built);
        IsBuilding = false;
    }

    // Showing the page again tries once more.
    private void Fail(string error)
    {
        indexedFolder = null;
        IsBuilding = false;
        BuildError = error;
    }

    // Fills the pickers from a built index and shows its list.
    private void Load(GameTextIndex built)
    {
        refilling = true;
        TableOptions =
        [
            new TextTableOption(null, "All tables"),
            .. built.Tables.Select(table => new TextTableOption(table.File,
                string.Create(CultureInfo.CurrentCulture, $"{table.File} ({table.Count:N0})"))),
        ];
        SelectedTable = TableOptions[0];
        LanguageOptions = built.Languages
            .Select(language => new TextLanguage(language, GameTextIndex.Codes[language], VanillaKinds.Languages[language].Name))
            .ToList();
        SecondOptions = [TextLanguage.None, .. LanguageOptions];
        string wanted = Session.Settings.NamesLanguage ?? VanillaKinds.DefaultLanguage;
        FirstLanguage = LanguageOptions.FirstOrDefault(language => language.Code == wanted)
                        ?? LanguageOptions.FirstOrDefault(language => language.Code == VanillaKinds.DefaultLanguage)
                        ?? LanguageOptions.FirstOrDefault();
        SecondLanguage = TextLanguage.None;
        refilling = false;
        Index = built;
        RefreshMarks();
        ShowAll();
        _ = FilterAsync();
    }

    private void Reset()
    {
        ++buildVersion;
        ++filterVersion;
        indexedFolder = null;
        query = null;
        items = [];
        changed = [];
        IsBuilding = false;
        BuildError = null;
        UnreadNote = null;
        Index = null;
        VisibleItems = [];
        SelectedItem = null;
        TableOptions = [];
        LanguageOptions = [];
        SecondOptions = [];
        OnPropertyChanged(nameof(ChangedText));
    }

    private int First => FirstLanguage?.Index ?? -1;
    private int Second => SecondLanguage is { Index: >= 0 } second && second.Index != First ? second.Index : -1;

    // The languages shown, first then second, as the editor orders its
    // boxes: those two, then the rest in table order.
    private List<TextLanguage> EditorOrder()
    {
        List<TextLanguage> order = LanguageOptions.Where(language => language.Index == First || language.Index == Second)
            .OrderBy(language => language.Index == First ? 0 : 1)
            .ToList();
        order.AddRange(LanguageOptions.Where(language => !order.Contains(language)));
        return order;
    }

    partial void OnSelectedTableChanged(TextTableOption? value) => _ = FilterAsync();

    partial void OnSearchChanged(string value) => _ = FilterAsync();

    partial void OnChangedOnlyChanged(bool value) => _ = FilterAsync();

    partial void OnFirstLanguageChanged(TextLanguage? value) => OnLanguagesChanged();

    partial void OnSecondLanguageChanged(TextLanguage? value) => OnLanguagesChanged();

    private void OnLanguagesChanged()
    {
        if (refilling)
        {
            return;
        }
        ShowAll();
        if (Editor is { } shown)
        {
            Editor = new TextEditorViewModel(shown.Item, EditorOrder(), Edits, Navigator, () => patches);
        }
        // Search looks at the languages shown.
        if (Search.Trim().Length > 0)
        {
            _ = FilterAsync();
        }
    }

    partial void OnSelectedItemChanged(TextRowItem? value)
    {
        if (refilling)
        {
            return;
        }
        Editor = value is null ? null : new TextEditorViewModel(value, EditorOrder(), Edits, Navigator, () => patches);
    }

    // Filters off the UI thread; a newer filter wins over one still running.
    private async Task FilterAsync()
    {
        if (refilling || query is not { } current)
        {
            return;
        }
        int version = ++filterVersion;
        string text = Search;
        string? file = SelectedTable?.File;
        int[] shown = Second >= 0 ? [First, Second] : [First];
        HashSet<int> marked = changed;
        GameTextPatches lookup = patches;
        Func<int, bool>? keep = ChangedOnly ? marked.Contains : null;
        IReadOnlyList<int> found = await Task.Run(() => current.Find(text, shown, file, keep,
            (position, language) => marked.Contains(position) ? lookup.Find(current.Entries[position], language)?.Value : null));
        if (version != filterVersion)
        {
            return;
        }
        TextRowItem? picked = SelectedItem;
        refilling = true;
        VisibleItems = found.Select(position => items[position]).ToList();
        bool stays = picked is not null && VisibleItems.Contains(picked);
        SelectedItem = stays ? picked : null;
        refilling = false;
        if (!stays)
        {
            Editor = null;
        }
    }

    private void OnEditsPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(GameEdits.Current))
        {
            OnEditsChanged();
        }
    }

    // The edits mod changed, here or in another section, or another one was
    // chosen: the marks, the shown texts and the editor follow it.
    private void OnEditsChanged()
    {
        if (query is null)
        {
            return;
        }
        RefreshMarks();
        Editor?.Refresh();
        if (ChangedOnly)
        {
            _ = FilterAsync();
        }
    }

    // Reads the edits mod's patches once and marks the entries it changes.
    private void RefreshMarks()
    {
        patches = GameTextPatches.Of(Edits.Current);
        HashSet<int> now = [];
        if (Edits.Current is not null && !patches.IsEmpty)
        {
            foreach (TextRowItem item in items)
            {
                if (patches.Changes(item.Entry))
                {
                    now.Add(item.Position);
                }
            }
        }
        HashSet<int> before = changed;
        changed = now;
        int first = First;
        int second = Second;
        foreach (TextRowItem item in items)
        {
            bool marked = now.Contains(item.Position);
            // Only the entries whose state can have changed are shown again.
            if (marked || before.Contains(item.Position))
            {
                item.Show(first, second, marked, patches);
            }
        }
        OnPropertyChanged(nameof(ChangedText));
    }

    private void ShowAll()
    {
        int first = First;
        int second = Second;
        foreach (TextRowItem item in items)
        {
            item.Show(first, second, changed.Contains(item.Position), patches);
        }
    }
}
