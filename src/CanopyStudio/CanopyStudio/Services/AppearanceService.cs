using System.Collections.ObjectModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.Themes.Fluent;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.Services;

public sealed record ThemeChoice(string Id, string Name)
{
    public override string ToString() => Name;
}

// Which theme the window wears, and the game asset cache it is drawn from.
// A seasonal theme needs its backdrop in the cache; until the cache has it
// (a first start, before setup copies the game's art) Forest stands in. The
// cache is rebuilt off the UI thread whenever a game folder whose build it
// was not made from comes into use.
public sealed partial class AppearanceService : ObservableObject
{
    private static readonly Uri FontsKey = new("fonts:CanopyGame", UriKind.Absolute);
    private const string DisplayFamily = "Lora";
    private const string BodyFamily = "Resource Han Rounded TC";

    private readonly GameSession session;
    private readonly GameAssetCache cache;
    private readonly IResourceDictionary resources;
    private readonly ColorPaletteResources? fluentPalette;
    private readonly SemaphoreSlim building = new(1, 1);
    private readonly Dictionary<string, Bitmap?> backdrops = [];
    private CachedFontCollection? fonts;
    private AssetManifest? manifest;
    private bool applying;

    [ObservableProperty]
    private ThemeChoice? selectedTheme;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ArtSummary))]
    [NotifyPropertyChangedFor(nameof(ShowFallbackNote))]
    [NotifyCanExecuteChangedFor(nameof(CopyArtAgainCommand))]
    private bool isPreparing;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ArtSummary))]
    private string progressText = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ArtSummary))]
    private string? artProblem;

    public AppearanceService(GameSession session, GameAssetCache cache, Application app)
    {
        this.session = session;
        this.cache = cache;
        resources = app.Resources;
        fluentPalette = app.Styles.OfType<FluentTheme>().FirstOrDefault() is { } fluent &&
                        fluent.Palettes.TryGetValue(ThemeVariant.Dark, out ColorPaletteResources? palette)
            ? palette
            : null;
        manifest = cache.Load();
        session.GameChanged += (_, _) => _ = PrepareAsync(force: false);
        session.PropertyChanged += (_, _) => CopyArtAgainCommand.NotifyCanExecuteChanged();
        Apply();
    }

    // The themes that can be worn now: Forest always, a season once its
    // backdrop is in the cache, and Automatic once any season can be.
    public ObservableCollection<ThemeChoice> Choices { get; } = [];

    // True while Forest stands in for a seasonal theme the cache cannot draw.
    public bool IsFallback { get; private set; }

    // The rail explains the fallback, unless the art is being copied now.
    public bool ShowFallbackNote => IsFallback && !IsPreparing;

    public string CacheFolder => cache.Folder;

    public string ArtSummary
    {
        get
        {
            if (IsPreparing)
            {
                return ProgressText.Length > 0 ? ProgressText + "..." : "Copying the game's fonts and art...";
            }
            if (ArtProblem is not null)
            {
                return ArtProblem;
            }
            if (manifest is null)
            {
                return "Not copied yet. Canopy Studio copies them once a game folder is in use.";
            }
            string summary = $"Copied from game build {manifest.Build:X8}: {manifest.Fonts.Count} fonts and " +
                             $"{manifest.Backdrops.Count} seasonal backdrops.";
            return manifest.Problems.Count == 0
                ? summary
                : $"{summary} {manifest.Problems.Count} could not be copied, first: {manifest.Problems[0]}";
        }
    }

    // Copies the game's fonts and art into the cache unless it was already
    // made from this game build, or always when force is set.
    public async Task PrepareAsync(bool force)
    {
        if (session.Game is not { Ok: true } game)
        {
            return;
        }
        await building.WaitAsync();
        try
        {
            if (!force && cache.IsCurrent(game.Build))
            {
                return;
            }
            ArtProblem = null;
            ProgressText = "";
            IsPreparing = true;
            FileList files = await session.FilesAsync();
            if (!files.Ok)
            {
                ArtProblem = $"The game's file list could not be read: {files.Error}";
                return;
            }
            List<string> paths = files.Files.Select(file => file.Path).ToList();
            Progress<string> progress = new(text => ProgressText = text);
            AssetBuild result = await Task.Run(() =>
                cache.Build(new NativeAssetSource(game.Folder, paths), game.Build, progress));
            if (!result.Ok)
            {
                ArtProblem = result.Error;
                return;
            }
            manifest = result.Manifest;
            backdrops.Clear();
            Apply();
        }
        finally
        {
            IsPreparing = false;
            building.Release();
        }
    }

    [RelayCommand(CanExecute = nameof(CanCopyArtAgain))]
    private Task CopyArtAgain() => PrepareAsync(force: true);

    private bool CanCopyArtAgain() => session.HasGame && !IsPreparing;

    partial void OnSelectedThemeChanged(ThemeChoice? value)
    {
        if (applying || value is null)
        {
            return;
        }
        session.Remember(current => current with { Theme = value.Id });
        Apply();
    }

    // Puts the chosen theme's colours, fonts and backdrop into the
    // application's resources, where every view finds them.
    private void Apply()
    {
        StudioTheme wanted = StudioThemes.Find(session.Settings.Theme);
        Bitmap? backdrop = wanted.Season is { } season ? Backdrop(season) : null;
        StudioTheme theme = wanted.HasBackdrop && backdrop is null ? StudioThemes.Forest : wanted;
        IsFallback = theme != wanted;
        OnPropertyChanged(nameof(IsFallback));
        OnPropertyChanged(nameof(ShowFallbackNote));

        ThemePalette palette = StudioThemes.Palette(theme);
        Brush("StudioWindowBrush", palette.Window);
        Brush("StudioRailBrush", palette.Rail);
        Brush("StudioStatusBrush", palette.Status);
        Brush("StudioSurfaceBrush", palette.Surface);
        Brush("StudioRaisedBrush", palette.Raised);
        Brush("StudioBorderBrush", palette.Border);
        Brush("StudioTextBrush", palette.Text);
        Brush("StudioMutedBrush", palette.Muted);
        Brush("StudioFaintBrush", palette.Faint);
        Brush("StudioAccentBrush", palette.Accent);
        Brush("StudioAccentHoverBrush", palette.AccentHover);
        Brush("StudioAccentPressedBrush", palette.AccentPressed);
        Brush("StudioOnAccentBrush", palette.OnAccent);
        Brush("StudioAccent80Brush", palette.Accent80);
        Brush("StudioAccent50Brush", palette.Accent50);
        Brush("StudioAccent30Brush", palette.Accent30);
        Brush("StudioAccent20Brush", palette.Accent20);
        Brush("StudioSelectedBrush", palette.Selected);
        Brush("StudioSelectedHoverBrush", palette.SelectedHover);
        Brush("StudioPassBrush", palette.Pass);
        Brush("StudioFailBrush", palette.Fail);

        // Fluent's own selections and the accent its controls use.
        Brush("SystemControlHighlightListAccentLowBrush", palette.Selected);
        Brush("SystemControlHighlightListAccentMediumBrush", palette.SelectedHover);
        Brush("SystemControlHighlightListAccentHighBrush", palette.SelectedHover);
        Brush("TreeViewItemBackgroundSelected", palette.Selected);
        Brush("TreeViewItemBackgroundSelectedPointerOver", palette.SelectedHover);
        Brush("TreeViewItemBackgroundSelectedPressed", palette.SelectedHover);
        Brush("TreeViewItemForegroundSelected", palette.Text);
        Brush("TreeViewItemForegroundSelectedPointerOver", palette.Text);
        Brush("TreeViewItemForegroundSelectedPressed", palette.Text);
        Brush("DataGridRowSelectedBackgroundBrush", palette.Selected);
        Brush("DataGridRowSelectedHoveredBackgroundBrush", palette.SelectedHover);
        Brush("DataGridRowSelectedUnfocusedBackgroundBrush", palette.Selected);
        Brush("DataGridRowSelectedHoveredUnfocusedBackgroundBrush", palette.SelectedHover);
        Brush("DataGridColumnHeaderForegroundBrush", palette.Muted);
        if (fluentPalette is not null)
        {
            fluentPalette.Accent = Color.FromUInt32(palette.Accent);
        }

        resources["StudioBackdrop"] = theme.HasBackdrop ? backdrop : null;

        CachedFontCollection? loaded = Fonts();
        bool display = loaded?.Has(DisplayFamily) == true;
        bool body = loaded?.Has(BodyFamily) == true;
        resources["StudioDisplayFont"] = display ? new FontFamily($"{FontsKey}#{DisplayFamily}") : FontFamily.Default;
        resources["StudioDisplayWeight"] = display ? FontWeight.Bold : FontWeight.SemiBold;
        resources["StudioH1Size"] = display ? 32.0 : 26.0;
        resources["StudioH2Size"] = display ? 24.0 : 17.0;
        resources["StudioBrandSize"] = display ? 22.0 : 18.0;
        FontFamily bodyFont = body ? new FontFamily($"{FontsKey}#{BodyFamily}") : FontFamily.Default;
        resources["StudioBodyFont"] = bodyFont;
        resources["ContentControlThemeFontFamily"] = bodyFont;

        RefreshChoices(theme);
        OnPropertyChanged(nameof(ArtSummary));
    }

    // worn is the theme on the window now; the picker shows Automatic for it
    // when that is what the settings ask for, unless Forest stands in.
    private void RefreshChoices(StudioTheme worn)
    {
        List<ThemeChoice> now = StudioThemes.All
            .Where(theme => theme.Season is not { } season || Backdrop(season) is not null)
            .Select(theme => new ThemeChoice(theme.Id, theme.Name))
            .ToList();
        StudioTheme today = StudioThemes.Find(StudioThemes.AutomaticId);
        if (now.Count > 1)
        {
            now.Insert(0, new ThemeChoice(StudioThemes.AutomaticId, $"Automatic ({today.Name})"));
        }
        bool automatic = StudioThemes.All.All(theme => theme.Id != session.Settings.Theme);
        string shown = automatic && !IsFallback && now.Count > 1 ? StudioThemes.AutomaticId : worn.Id;
        applying = true;
        try
        {
            if (!now.SequenceEqual(Choices))
            {
                Choices.Clear();
                foreach (ThemeChoice choice in now)
                {
                    Choices.Add(choice);
                }
            }
            SelectedTheme = Choices.FirstOrDefault(choice => choice.Id == shown);
        }
        finally
        {
            applying = false;
        }
    }

    // The season's backdrop from the cache, read once. Null when the cache
    // does not have it or it cannot be decoded.
    private Bitmap? Backdrop(string season)
    {
        if (backdrops.TryGetValue(season, out Bitmap? known))
        {
            return known;
        }
        Bitmap? bitmap = null;
        if (manifest?.Backdrops.Contains(season) == true)
        {
            try
            {
                // Read into memory first, so the file is not held open.
                bitmap = new Bitmap(new MemoryStream(File.ReadAllBytes(cache.BackdropPath(season))));
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException or InvalidOperationException)
            {
                bitmap = null;
            }
        }
        backdrops[season] = bitmap;
        return bitmap;
    }

    // The game's fonts, registered once per run. A rebuilt cache holds the
    // same families, so the first load stands.
    private CachedFontCollection? Fonts()
    {
        if (fonts is null && manifest is { Fonts.Count: > 0 })
        {
            List<string> files = manifest.Fonts.Select(cache.FontPath).Where(File.Exists).ToList();
            if (files.Count > 0)
            {
                fonts = new CachedFontCollection(FontsKey, files);
                FontManager.Current.AddFontCollection(fonts);
            }
        }
        return fonts;
    }

    private void Brush(string key, uint color) => resources[key] = new SolidColorBrush(Color.FromUInt32(color));
}
