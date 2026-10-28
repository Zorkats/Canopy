using System.Text.Json;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Identity;

// Where the cache reads the game from: canopy_core.dll for the real thing.
public interface IGameAssetSource
{
    IReadOnlyList<string> Paths { get; }
    byte[]? File(string gamePath, out string? error);
    (TextureImage? Image, string? Error) Texture(string gamePath);
}

public sealed class NativeAssetSource(string gameFolder, IReadOnlyList<string> paths) : IGameAssetSource
{
    public IReadOnlyList<string> Paths => paths;

    public byte[]? File(string gamePath, out string? error) => CanopyCore.File(gameFolder, gamePath, out error);

    public (TextureImage? Image, string? Error) Texture(string gamePath)
    {
        TextureImage? image = CanopyCore.Texture(gameFolder, gamePath, out string? error);
        return (image, error);
    }
}

// What a finished cache holds. Fonts are file names under fonts/, backdrops
// season ids with a backdrops/<id>.png.
public sealed record AssetManifest
{
    public int Recipe { get; init; }
    public uint Build { get; init; }
    public IReadOnlyList<string> Fonts { get; init; } = [];
    public IReadOnlyList<string> Backdrops { get; init; } = [];
    // Assets that could not be made, one sentence each.
    public IReadOnlyList<string> Problems { get; init; } = [];
}

public sealed record AssetBuild(AssetManifest? Manifest, string? Error)
{
    public bool Ok => Manifest is not null;
}

// The game's fonts and backdrop art, copied from the user's own install into
// %APPDATA%\CanopyStudio\game-assets. Canopy Studio ships none of the game's
// files: setup builds this once per game build, and the themes read it.
public sealed class GameAssetCache(string folder)
{
    // Raise when the recipe changes, so older caches are rebuilt.
    public const int Recipe = 1;

    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    public static GameAssetCache ForUser() =>
        new(Path.Combine(Platform.StudioFolders.Root(), "game-assets"));

    public string Folder => folder;

    public string FontPath(string file) => Path.Combine(folder, "fonts", file);

    public string BackdropPath(string season) => Path.Combine(folder, "backdrops", season + ".png");

    // The finished cache, or null when there is none or it is damaged.
    public AssetManifest? Load()
    {
        try
        {
            return JsonSerializer.Deserialize<AssetManifest>(File.ReadAllText(ManifestPath(folder)), Json);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    // True when the cache was made by this recipe from this game build and
    // every file it lists is still there.
    public bool IsCurrent(uint build) =>
        Load() is { } manifest &&
        manifest.Recipe == Recipe &&
        manifest.Build == build &&
        manifest.Fonts.All(font => File.Exists(FontPath(font))) &&
        manifest.Backdrops.All(season => File.Exists(BackdropPath(season)));

    // The font families the themes use. Lora is the display face, Kiwi Maru
    // the Japanese one, and Resource Han Rounded TC the body face; it carries
    // every CJK glyph, so the game's K copy (and its 60 MB) is left out.
    private static readonly string[] FontFamilies = ["lora-", "kiwimaru-", "resourcehanroundedtc-"];

    // The game font files the cache copies: those families' .ttf and .otf
    // files right in data/misc.
    public static IReadOnlyList<string> FontPaths(IEnumerable<string> paths) =>
        paths.Where(path => path.StartsWith("data/misc/", StringComparison.OrdinalIgnoreCase) &&
                            path.IndexOf('/', "data/misc/".Length) < 0 &&
                            FontFamilies.Any(family => Path.GetFileName(path).StartsWith(family, StringComparison.OrdinalIgnoreCase)) &&
                            (path.EndsWith(".ttf", StringComparison.OrdinalIgnoreCase) ||
                             path.EndsWith(".otf", StringComparison.OrdinalIgnoreCase)))
             .Order(StringComparer.OrdinalIgnoreCase)
             .ToList();

    // Builds the cache next to the old one and swaps it in only when done,
    // so a failure or a cancel leaves the old cache as it was. A missing
    // asset is listed in Problems; the build fails only when nothing was made
    // or nothing could be written.
    public AssetBuild Build(IGameAssetSource source, uint build, IProgress<string>? progress = null,
        CancellationToken cancel = default)
    {
        string staging = folder + ".new";
        List<string> fonts = [];
        List<string> backdrops = [];
        List<string> problems = [];
        try
        {
            if (Directory.Exists(staging))
            {
                Directory.Delete(staging, recursive: true);
            }
            Directory.CreateDirectory(Path.Combine(staging, "fonts"));
            Directory.CreateDirectory(Path.Combine(staging, "backdrops"));

            foreach (string path in FontPaths(source.Paths))
            {
                cancel.ThrowIfCancellationRequested();
                string name = Path.GetFileName(path);
                progress?.Report($"Copying the font {name}");
                byte[]? bytes = source.File(path, out string? error);
                if (bytes is null)
                {
                    problems.Add($"{path}: {error}");
                    continue;
                }
                File.WriteAllBytes(Path.Combine(staging, "fonts", name), bytes);
                fonts.Add(name);
            }

            foreach (Season season in Backdrops.Seasons)
            {
                cancel.ThrowIfCancellationRequested();
                progress?.Report($"Painting the {season.Id} backdrop");
                TextureImage? picture = Backdrops.Compose(season, source.Texture, out string? error);
                if (picture is null)
                {
                    problems.Add(error!);
                    continue;
                }
                File.WriteAllBytes(Path.Combine(staging, "backdrops", season.Id + ".png"), PngWriter.Encode(picture));
                backdrops.Add(season.Id);
            }

            if (fonts.Count == 0 && backdrops.Count == 0)
            {
                Directory.Delete(staging, recursive: true);
                return new AssetBuild(null, problems.Count > 0
                    ? $"Nothing could be copied from the game. First problem: {problems[0]}"
                    : "The game has none of the fonts or art Canopy Studio looks for.");
            }

            AssetManifest manifest = new()
            {
                Recipe = Recipe,
                Build = build,
                Fonts = fonts,
                Backdrops = backdrops,
                Problems = problems,
            };
            File.WriteAllText(ManifestPath(staging), JsonSerializer.Serialize(manifest, Json));
            BusyFiles.SwapFolder(staging, folder);
            return new AssetBuild(manifest, null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new AssetBuild(null, $"The game's fonts and art could not be saved in {folder}: {exception.Message}");
        }
    }

    private static string ManifestPath(string root) => Path.Combine(root, "manifest.json");
}
