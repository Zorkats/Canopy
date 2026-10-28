using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// A language the selected texture can be shown in: the main file, or a
// language whose file differs from it.
public sealed record ArtLanguage(string Code, string Label, string File);

// A zoom of the viewer: 0 fits the texture to the space.
public sealed record ArtZoom(int Factor, string Label);

// The game's textures: every row of texture.dat listed with a lazy
// thumbnail, shown at a zoom, exported as PNG, and replaced through the
// edits mod with a [texture.<key>] section and a [patch.texture.<id>]
// section that points the vanilla row at it (docs/ART.md, docs/PATCHES.md).
public sealed partial class GameArtViewModel : PageViewModel
{
    private readonly GameSession session;
    private readonly GameEdits edits;
    private readonly IStudioDialogs dialogs;
    private IReadOnlyList<ArtRowViewModel> all = [];
    private TextureImage? shownImage;
    private bool loaded;
    private int viewVersion;
    private int replacementVersion;
    private CancellationTokenSource? exporting;

    [ObservableProperty]
    private bool isLoading;

    [ObservableProperty]
    private string? loadError;

    [ObservableProperty]
    private string filter = "";

    [ObservableProperty]
    private bool changedOnly;

    [ObservableProperty]
    private IReadOnlyList<ArtRowViewModel> visibleRows = [];

    [ObservableProperty]
    private string listCaption = "";

    [ObservableProperty]
    private ArtRowViewModel? selectedRow;

    [ObservableProperty]
    private IReadOnlyList<ArtLanguage> languages = [];

    [ObservableProperty]
    private ArtLanguage? shownLanguage;

    [ObservableProperty]
    private ArtZoom zoom;

    [ObservableProperty]
    private Bitmap? vanillaImage;

    [ObservableProperty]
    private string vanillaCaption = "";

    [ObservableProperty]
    private string? vanillaError;

    [ObservableProperty]
    private bool isDecoding;

    [ObservableProperty]
    private Bitmap? replacementImage;

    [ObservableProperty]
    private string replacementCaption = "";

    [ObservableProperty]
    private string? status;

    [ObservableProperty]
    private bool statusIsError;

    [ObservableProperty]
    private bool isExporting;

    [ObservableProperty]
    private string exportProgress = "";

    [ObservableProperty]
    private IReadOnlyList<string> exportProblems = [];

    public GameArtViewModel(GameSession session, GameEdits edits, IStudioDialogs dialogs)
    {
        this.session = session;
        this.edits = edits;
        this.dialogs = dialogs;
        Thumbnails = new ArtThumbnails(session);
        zoom = Zooms[0];
        session.GameChanged += (_, _) => Reset();
        session.PropertyChanged += (_, _) => OnPropertyChanged(nameof(NeedsGame));
        edits.Edited += (_, _) => RefreshReplacements();
        edits.PropertyChanged += OnEditsChanged;
    }

    public override string Id => PageIds.Art;
    public override string Title => "Art";

    public ArtThumbnails Thumbnails { get; }

    public IReadOnlyList<ArtZoom> Zooms { get; } =
        [new(0, "Fit"), new(1, "1x"), new(2, "2x"), new(4, "4x")];

    public bool NeedsGame => !session.HasGame;
    public bool IsReady => loaded && LoadError is null;

    public bool HasSelection => SelectedRow is not null;
    public bool NoSelection => SelectedRow is null;
    public bool HasOtherLanguages => Languages.Count > 1;
    public bool HasVanillaImage => VanillaImage is not null;
    public bool HasReplacement => SelectedRow?.Replacement is not null;

    // The replacement's half of the viewer, none without one.
    public GridLength ReplacementWidth => HasReplacement ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
    public bool HasReplacementImage => ReplacementImage is not null;
    public bool HasStatus => Status is not null;
    public bool HasExportProblems => ExportProblems.Count > 0;
    public string ShownFile => ShownLanguage?.File ?? "";

    public string SelectedSubtitle => SelectedRow is { } row
        ? $"Id {row.Id}, row {row.Texture.Index} of texture.dat"
        : "";

    // Why Replace cannot be used now, null when it can.
    public string? ReplaceNote => edits.Current is not { } mod
        ? "Choose or make an edits mod in the bar above to replace textures."
        : ArtReplace.ModKeyProblem(mod);

    public bool HasReplaceNote => ReplaceNote is not null;

    public string ReplacementWhere => SelectedRow?.Replacement is { } replacement && edits.Current is { } current
        ? $"Replaced in {current.Name} by {replacement.File}, declared as [texture.{replacement.Key}]."
        : "";

    public override void OnShown()
    {
        if (!loaded && !IsLoading && session.HasGame)
        {
            _ = LoadAsync();
        }
    }

    [RelayCommand]
    private void ClearFilter() => Filter = "";

    private async Task LoadAsync()
    {
        IsLoading = true;
        LoadError = null;
        TableGrid grid = await session.GridAsync(ArtReplace.TableFile);
        IsLoading = false;
        if (!grid.Ok)
        {
            LoadError = grid.Error ?? "texture.dat could not be read.";
            return;
        }
        all = [.. ArtTextures.FromGrid(grid).Select(texture => new ArtRowViewModel(texture))];
        loaded = true;
        OnPropertyChanged(nameof(IsReady));
        RefreshReplacements();
    }

    private void Reset()
    {
        CancelExport();
        loaded = false;
        all = [];
        SelectedRow = null;
        VisibleRows = [];
        ListCaption = "";
        LoadError = null;
        Status = null;
        OnPropertyChanged(nameof(IsReady));
    }

    partial void OnFilterChanged(string value) => RefreshList();

    partial void OnChangedOnlyChanged(bool value) => RefreshList();

    partial void OnLoadErrorChanged(string? value) => OnPropertyChanged(nameof(IsReady));

    partial void OnStatusChanged(string? value) => OnPropertyChanged(nameof(HasStatus));

    partial void OnExportProblemsChanged(IReadOnlyList<string> value) => OnPropertyChanged(nameof(HasExportProblems));

    partial void OnVanillaImageChanged(Bitmap? value) => OnPropertyChanged(nameof(HasVanillaImage));

    partial void OnReplacementImageChanged(Bitmap? value) => OnPropertyChanged(nameof(HasReplacementImage));

    partial void OnLanguagesChanged(IReadOnlyList<ArtLanguage> value) => OnPropertyChanged(nameof(HasOtherLanguages));

    partial void OnSelectedRowChanged(ArtRowViewModel? value)
    {
        foreach (string name in (string[])[nameof(HasSelection), nameof(NoSelection), nameof(SelectedSubtitle)])
        {
            OnPropertyChanged(name);
        }
        // The main file first, for every language that shares it, then each
        // language with a file of its own.
        IReadOnlyList<(string Code, string File)> others = value?.Texture.OtherLanguages() ?? [];
        Languages = value is null
            ? []
            :
            [
                new ArtLanguage("", others.Count > 0 ? "Main file" : "Every language", value.File),
                .. others.Select(other => new ArtLanguage(other.Code, LangKeys.LanguageName(other.Code), other.File)),
            ];
        ShownLanguage = Languages.FirstOrDefault();
        ShowReplacement();
    }

    partial void OnShownLanguageChanged(ArtLanguage? value)
    {
        OnPropertyChanged(nameof(ShownFile));
        _ = ShowAsync(SelectedRow, value);
    }

    private void OnEditsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(GameEdits.Current))
        {
            RefreshReplacements();
        }
    }

    private void RefreshList()
    {
        string text = Filter.Trim();
        List<ArtRowViewModel> found = [.. all.Where(row => (!ChangedOnly || row.IsReplaced) && row.Texture.Matches(text))];
        // A new list scrolls the grid to the top and drops its selection, so
        // the same rows keep the list they have.
        if (!found.SequenceEqual(VisibleRows))
        {
            ArtRowViewModel? selected = SelectedRow;
            VisibleRows = found;
            if (selected is not null && found.Contains(selected))
            {
                SelectedRow = selected;
            }
        }
        int replaced = all.Count(row => row.IsReplaced);
        string marks = replaced > 0 ? $" The edits mod replaces {replaced:N0}." : "";
        ListCaption = found.Count == all.Count
            ? $"{all.Count:N0} textures.{marks}"
            : $"{found.Count:N0} of {all.Count:N0} textures shown.{marks}";
    }

    // Marks the rows the edits mod replaces, after an edit or a new choice of
    // edits mod.
    private void RefreshReplacements()
    {
        Dictionary<ulong, ArtReplacement> byId = [];
        if (edits.Current is { } mod && all.Count > 0)
        {
            Dictionary<string, ulong>? byEnum = null;
            foreach (ArtReplacement replacement in ArtReplace.Read(mod))
            {
                ulong? id = replacement.Id;
                if (id is null)
                {
                    byEnum ??= all.Where(row => row.Texture.Enum is not null)
                        .GroupBy(row => row.Texture.Enum!, StringComparer.Ordinal)
                        .ToDictionary(group => group.Key, group => group.First().Id, StringComparer.Ordinal);
                    id = byEnum.TryGetValue(replacement.Row, out ulong found) ? found : null;
                }
                if (id is { } known)
                {
                    byId.TryAdd(known, replacement);
                }
            }
        }
        foreach (ArtRowViewModel row in all)
        {
            row.Replacement = byId.GetValueOrDefault(row.Id);
        }
        OnPropertyChanged(nameof(ReplaceNote));
        OnPropertyChanged(nameof(HasReplaceNote));
        RefreshList();
        ShowReplacement();
    }

    // ---- The viewer ----------------------------------------------------------

    private async Task ShowAsync(ArtRowViewModel? row, ArtLanguage? language)
    {
        int version = ++viewVersion;
        shownImage = null;
        VanillaImage = null;
        VanillaCaption = "";
        VanillaError = null;
        IsDecoding = false;
        if (row is null || language is null)
        {
            return;
        }
        if (language.File.Length == 0)
        {
            VanillaError = "This row names no file in any language, so there is nothing to show.";
            return;
        }
        IsDecoding = true;
        (TextureImage? image, string? error) = await session.TextureAsync(language.File);
        if (version != viewVersion)
        {
            return;
        }
        IsDecoding = false;
        if (image is null)
        {
            VanillaError = error ?? "The texture could not be decoded.";
            return;
        }
        shownImage = image;
        if (language.Code.Length == 0)
        {
            row.Saw(image.Width, image.Height);
        }
        VanillaImage = TextureBitmaps.ToBitmap(image);
        VanillaCaption = $"{image.Width} x {image.Height} pixels";
    }

    private void ShowReplacement()
    {
        OnPropertyChanged(nameof(HasReplacement));
        OnPropertyChanged(nameof(ReplacementWidth));
        OnPropertyChanged(nameof(ReplacementWhere));
        _ = ShowReplacementAsync();
    }

    private async Task ShowReplacementAsync()
    {
        int version = ++replacementVersion;
        if (SelectedRow?.Replacement is not { } replacement || edits.Current is not { } mod)
        {
            ReplacementImage = null;
            ReplacementCaption = "";
            return;
        }
        string? full = ProjectPaths.Full(mod.Folder, replacement.File);
        byte[]? bytes = full is null ? null : await Task.Run(() => ReadAll(full));
        if (version != replacementVersion)
        {
            return;
        }
        Bitmap? bitmap = null;
        if (bytes is not null)
        {
            try
            {
                using MemoryStream stream = new(bytes);
                bitmap = new Bitmap(stream);
            }
            catch (Exception exception) when (exception is ArgumentException or InvalidOperationException or IOException
                or NotSupportedException)
            {
                bitmap = null;
            }
        }
        ReplacementImage = bitmap;
        ReplacementCaption = bitmap is not null
            ? $"{bitmap.PixelSize.Width} x {bitmap.PixelSize.Height} pixels"
            : bytes is null
                ? $"{replacement.File} is not in {mod.Name}. Put the image back there, or revert the replacement."
                : $"{replacement.File} could not be shown. Check that it is an image Canopy reads.";
    }

    private static byte[]? ReadAll(string path)
    {
        try
        {
            return File.Exists(path) ? File.ReadAllBytes(path) : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    // ---- Export --------------------------------------------------------------

    [RelayCommand]
    private async Task ExportPngAsync()
    {
        if (SelectedRow is not { } row || shownImage is not { } image || ShownLanguage is not { } language)
        {
            Say("Pick a texture and wait for it to show before exporting it.", error: true);
            return;
        }
        string? folder = await dialogs.PickFolderAsync("Export the texture as PNG");
        if (folder is null)
        {
            return;
        }
        string name = ArtKeys.ExportName(row.Texture.Enum, row.Id);
        if (language.Code.Length > 0)
        {
            // Another language's file goes beside the main one.
            name = Path.GetFileNameWithoutExtension(name) + "." + language.Code + ".png";
        }
        string target = Path.Combine(folder, name);
        if (File.Exists(target) &&
            !await dialogs.ConfirmAsync("Replace the file?", $"{name} is already in {folder}. Replace it with this texture?", "Replace"))
        {
            Say("Nothing was exported.", error: false);
            return;
        }
        string? problem = await Task.Run(() => Write(target, image));
        Say(problem ?? $"Saved {name} in {folder}.", problem is not null);
    }

    [RelayCommand]
    private async Task ExportAllAsync()
    {
        List<ArtRowViewModel> rows = [.. VisibleRows.Where(row => row.HasFile)];
        int skipped = VisibleRows.Count - rows.Count;
        if (rows.Count == 0 || session.Folder is not { } game)
        {
            Say("The filter shows no texture with a file to export.", error: true);
            return;
        }
        string? folder = await dialogs.PickFolderAsync($"Export {rows.Count:N0} textures as PNG");
        if (folder is null)
        {
            return;
        }
        // One name per row; a name used twice takes the row's id.
        HashSet<string> names = new(StringComparer.OrdinalIgnoreCase);
        List<(ArtRowViewModel Row, string Name)> plan = [];
        foreach (ArtRowViewModel row in rows)
        {
            string name = ArtKeys.ExportName(row.Texture.Enum, row.Id);
            if (!names.Add(name))
            {
                name = Path.GetFileNameWithoutExtension(name) + "_" + row.Id + ".png";
                names.Add(name);
            }
            plan.Add((row, name));
        }
        int existing = plan.Count(entry => File.Exists(Path.Combine(folder, entry.Name)));
        if (existing > 0 && !await dialogs.ConfirmAsync("Replace the files?",
                $"{existing:N0} of the {plan.Count:N0} PNG files are already in {folder}. Replace them?", "Replace"))
        {
            Say("Nothing was exported.", error: false);
            return;
        }

        exporting = new CancellationTokenSource();
        CancellationToken token = exporting.Token;
        IsExporting = true;
        ExportProblems = [];
        Status = null;
        ExportProgress = $"Exporting 0 of {plan.Count:N0}...";
        Progress<int> progress = new(done => ExportProgress = $"Exporting {done:N0} of {plan.Count:N0}...");
        List<string> problems = await Task.Run(() =>
        {
            List<string> found = [];
            int done = 0;
            foreach ((ArtRowViewModel row, string name) in plan)
            {
                if (token.IsCancellationRequested)
                {
                    break;
                }
                TextureImage? image = CanopyCore.Texture(game, row.File, out string? error);
                string? problem = image is null
                    ? $"Texture {row.Id}: {error ?? "it could not be decoded."}"
                    : Write(Path.Combine(folder, name), image);
                if (problem is not null)
                {
                    found.Add(problem);
                }
                ((IProgress<int>)progress).Report(++done);
            }
            return found;
        }, CancellationToken.None);
        bool cancelled = token.IsCancellationRequested;
        exporting = null;
        IsExporting = false;
        ExportProgress = "";
        ExportProblems = problems;
        string end = cancelled ? "Export stopped." : "Export done.";
        string skips = skipped > 0 ? $" {skipped:N0} rows name no file and were skipped." : "";
        string issues = problems.Count > 0 ? $" {problems.Count:N0} could not be exported; they are listed below." : "";
        Say($"{end} The PNG files are in {folder}.{skips}{issues}", problems.Count > 0);
    }

    [RelayCommand]
    private void CancelExport() => exporting?.Cancel();

    // Null when written, else why not.
    private static string? Write(string target, TextureImage image)
    {
        try
        {
            File.WriteAllBytes(target, PngWriter.Encode(image));
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{Path.GetFileName(target)} could not be written: {exception.Message}";
        }
    }

    // ---- Replace -------------------------------------------------------------

    [RelayCommand]
    private async Task ReplaceAsync()
    {
        if (SelectedRow is not { } row)
        {
            return;
        }
        if (ReplaceNote is { } note)
        {
            Say(note, error: true);
            return;
        }
        EditsMod mod = edits.Current!;
        if (!row.HasFile)
        {
            Say("This row names no file, so the game draws nothing from it to replace.", error: true);
            return;
        }
        IReadOnlyList<string> picked = await dialogs.PickFilesAsync("Pick the PNG that replaces this texture", "PNG images", ["*.png"]);
        if (picked.Count == 0)
        {
            return;
        }
        string source = picked[0];
        string sourceName = Path.GetFileName(source);
        ArtPngInfo png = await Task.Run(() => ArtPng.Read(source, sourceName));
        if (!png.Ok)
        {
            Say(png.Error!, error: true);
            return;
        }

        // The vanilla size is the decoded main file's: vanilla rows record
        // none.
        (int Width, int Height)? vanilla = row.Width > 0 && row.Height > 0 ? (row.Width, row.Height) : null;
        if (vanilla is null)
        {
            (TextureImage? image, _) = await session.TextureAsync(row.File);
            if (image is not null)
            {
                row.Saw(image.Width, image.Height);
                vanilla = (image.Width, image.Height);
            }
        }
        if (vanilla is { } size && (size.Width != png.Width || size.Height != png.Height) &&
            !await dialogs.ConfirmAsync("A different size",
                $"{sourceName} is {png.Width} x {png.Height} and the game's texture is {size.Width} x {size.Height}. " +
                "Icons, skins and other parts of the game cut this texture by pixel rectangles, so they may need matching " +
                "edits in Tables to show right. Replace it anyway?", "Replace"))
        {
            return;
        }
        if (!ReferenceEquals(edits.Current, mod))
        {
            Say("The edits mod changed while the PNG was picked. Pick it again.", error: true);
            return;
        }

        // A texture replaced before keeps its key; a new one gets a key no
        // other texture of the mod has.
        string key = row.Replacement?.Key ??
            ArtKeys.Unique(ArtKeys.FromRow(row.Texture.Enum, row.Id), ArtKeys.Taken(mod.Project.SectionNames).Contains);
        string relative = ArtKeys.ArtFile(key);
        string target = Path.Combine(mod.Folder, "art", key + ".png");
        bool same = string.Equals(Path.GetFullPath(source), Path.GetFullPath(target), StringComparison.OrdinalIgnoreCase);
        if (!same && File.Exists(target) &&
            !await dialogs.ConfirmAsync("Replace the file?",
                $"{relative} is already in {mod.Name}. Replace it with {sourceName}?", "Replace"))
        {
            return;
        }
        if (!same)
        {
            string? copyError = await Task.Run(() => Copy(source, target));
            if (copyError is not null)
            {
                Say(copyError, error: true);
                return;
            }
        }
        if (ArtReplace.Apply(mod, row.Id, key) is { } error)
        {
            Say($"{relative} is copied into {mod.Name}, but canopy.ini could not be changed: {error}", error: true);
            edits.NotifyEdited();
            return;
        }
        edits.NotifyEdited();
        Say($"{sourceName} is copied to {relative} in {mod.Name}. The texture section and the patch are in canopy.ini " +
            "once you press Save in the bar above; the game shows the new texture on its next start.", error: false);
    }

    [RelayCommand]
    private void Revert()
    {
        if (SelectedRow is not { Replacement: { } replacement } row || edits.Current is not { } mod)
        {
            return;
        }
        if (replacement.Id is null)
        {
            Say($"{mod.Name} names this row by its enum name, [patch.texture.{replacement.Row}], and Revert takes out only rows " +
                "named by id. Take that section and [texture." + replacement.Key + "] out of canopy.ini by hand.", error: true);
            return;
        }
        if (ArtReplace.Revert(mod, row.Id, replacement.Key) is { } error)
        {
            Say($"The replacement could not be taken out: {error}", error: true);
            edits.NotifyEdited();
            return;
        }
        edits.NotifyEdited();
        string where = ProjectPaths.Full(mod.Folder, replacement.File) ?? Path.Combine(mod.Folder, replacement.File);
        Say($"The replacement is taken out of canopy.ini once you press Save in the bar above. The PNG stays at {where}; " +
            "delete it there when you no longer need it.", error: false);
    }

    // Null when copied, else why not.
    private static string? Copy(string source, string target)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(source, target, overwrite: true);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{Path.GetFileName(source)} could not be copied into the edits mod: {exception.Message}";
        }
    }

    private void Say(string message, bool error)
    {
        StatusIsError = error;
        Status = message;
    }
}
