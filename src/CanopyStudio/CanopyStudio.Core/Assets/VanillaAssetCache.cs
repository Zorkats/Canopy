using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Assets;

// Where the vanilla cache reads the game from: canopy_core.dll for the real
// thing, a fake in tests.
public interface IVanillaSource
{
    NameList Names(VanillaKind kind, string language);

    // Every icon in ids saved as outFolder\<id>.png.
    IconBatch Icons(IReadOnlyList<uint> ids, string outFolder);
}

public sealed class NativeVanillaSource(string gameFolder) : IVanillaSource
{
    public NameList Names(VanillaKind kind, string language) =>
        CanopyCore.Names(gameFolder, VanillaKinds.Code(kind), language);

    public IconBatch Icons(IReadOnlyList<uint> ids, string outFolder) => CanopyCore.Icons(gameFolder, ids, outFolder);
}

// What a finished build's cache holds: the names files under names/, how
// many icons were saved under icons/, and what could not be made, a
// sentence each.
public sealed record VanillaManifest
{
    public int Recipe { get; init; }
    public uint Build { get; init; }
    public IReadOnlyList<string> Names { get; init; } = [];
    public int Icons { get; init; }
    public IReadOnlyList<string> Problems { get; init; } = [];
}

// Entries of one kind in one language. Error says why there are none when
// they could not be read.
public sealed record VanillaRead(IReadOnlyList<VanillaEntry> Entries, string? Error)
{
    public bool Ok => Error is null;
}

// The game's names and icons, read from the user's own install into
// %APPDATA%\CanopyStudio\cache, one folder per game build:
// <build>\names\<kind>.<language>.json, <build>\icons\<id>.png and
// <build>\manifest.json once the whole cache is made. Reads work before
// that: a names file missing from the cache is read from the game and kept.
// Any thread may call.
public sealed class VanillaAssetCache(string root)
{
    // Raise when the recipe or a file's layout changes, so older caches are
    // rebuilt and older names files read again.
    public const int Recipe = 2;

    private const string Staging = ".new";

    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    // Guards the live build folder: lazy writes and the swap of a finished
    // build never run at once.
    private readonly object gate = new();

    public static VanillaAssetCache ForUser() =>
        new(Path.Combine(Platform.StudioFolders.Root(), "cache"));

    public string Root => root;

    // The folder of one game build's cache, named by the build in hex as
    // Canopy Studio shows it.
    public string BuildFolder(uint build) => Path.Combine(root, BuildName(build));

    public string NamesPath(uint build, VanillaKind kind, string language) =>
        Path.Combine(BuildFolder(build), "names", NamesFile(kind, language));

    // The cached PNG of icon id, or null when it is not cached.
    public string? IconPath(uint build, ulong id)
    {
        string path = IconFile(BuildFolder(build), id);
        return File.Exists(path) ? path : null;
    }

    // The finished cache of build, or null when there is none, it is
    // damaged, or it was made for another build.
    public VanillaManifest? Load(uint build)
    {
        try
        {
            VanillaManifest? manifest = JsonSerializer.Deserialize<VanillaManifest>(
                File.ReadAllText(ManifestPath(BuildFolder(build))), Json);
            return manifest is not null && manifest.Build == build ? manifest : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    // True when the whole cache of build was made by this recipe and every
    // names file it lists is still there.
    public bool IsCurrent(uint build) =>
        Load(build) is { } manifest &&
        manifest.Recipe == Recipe &&
        manifest.Names.All(file => File.Exists(Path.Combine(BuildFolder(build), "names", file)));

    // The entries of kind in language: from the cache when it has them, else
    // read from source and kept in the cache for next time. A cache that
    // cannot be written still returns what was read.
    public VanillaRead Entries(IVanillaSource source, uint build, VanillaKind kind, string language)
    {
        string path = NamesPath(build, kind, language);
        if (ReadNames(path) is { } cached)
        {
            return new VanillaRead(cached, null);
        }
        NameList list = source.Names(kind, language);
        if (!list.Ok)
        {
            return new VanillaRead([], list.Error ?? "The names could not be read.");
        }
        IReadOnlyList<VanillaEntry> entries = ToEntries(kind, list);
        lock (gate)
        {
            try
            {
                WriteNames(path, entries);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                // The cache is a convenience; the names were read.
            }
        }
        return new VanillaRead(entries, null);
    }

    // Saves image as icon id's cached PNG and returns its path, or null when
    // it could not be written.
    public string? SaveIcon(uint build, ulong id, TextureImage image)
    {
        byte[] png = PngWriter.Encode(image);
        lock (gate)
        {
            try
            {
                string path = IconFile(BuildFolder(build), id);
                WriteAtomically(path, temporary => File.WriteAllBytes(temporary, png));
                return path;
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return null;
            }
        }
    }

    // Makes the whole cache of build: names of every kind in every language,
    // then every icon they use, one batch per kind. Nothing is read when the
    // cache is already current. The cache is built next to the old one and
    // swapped in only when done, so a cancel or a failure leaves the old one
    // as it was; older builds' folders go once the new one is in. What could
    // not be made is listed in Problems; the build fails only when no names
    // at all could be read or nothing could be written.
    public AssetPrepareResult Build(IVanillaSource source, uint build, IProgress<string>? progress = null,
        CancellationToken cancel = default)
    {
        if (IsCurrent(build))
        {
            return new AssetPrepareResult(true, null, Load(build)?.Problems ?? []);
        }
        string folder = BuildFolder(build);
        string staging = folder + Staging;
        List<string> names = [];
        List<string> problems = [];
        try
        {
            if (Directory.Exists(staging))
            {
                Directory.Delete(staging, recursive: true);
            }
            Directory.CreateDirectory(Path.Combine(staging, "names"));
            string icons = Path.Combine(staging, "icons");
            Directory.CreateDirectory(icons);

            // Icon ids by kind in the order first seen, each id under the
            // first kind that uses it so it is cut once.
            Dictionary<VanillaKind, List<uint>> iconsByKind = [];
            HashSet<ulong> seen = [];
            foreach (VanillaKind kind in VanillaKinds.All)
            {
                List<uint> ids = [];
                iconsByKind[kind] = ids;
                for (int i = 0; i < VanillaKinds.Languages.Count; ++i)
                {
                    cancel.ThrowIfCancellationRequested();
                    GameLanguage language = VanillaKinds.Languages[i];
                    progress?.Report($"Reading {Noun(kind)} names ({i + 1} of {VanillaKinds.Languages.Count} languages)");
                    NameList list = source.Names(kind, language.Code);
                    if (!list.Ok)
                    {
                        problems.Add($"The {Noun(kind)} names in {language.Name} could not be read: {list.Error ?? "no reason was given."}");
                        continue;
                    }
                    IReadOnlyList<VanillaEntry> entries = ToEntries(kind, list);
                    string file = NamesFile(kind, language.Code);
                    WriteNames(Path.Combine(staging, "names", file), entries);
                    names.Add(file);
                    foreach (VanillaEntry entry in entries)
                    {
                        if (entry.Icon is { } icon && icon <= uint.MaxValue && seen.Add(icon))
                        {
                            ids.Add((uint)icon);
                        }
                    }
                }
            }

            if (names.Count == 0)
            {
                Directory.Delete(staging, recursive: true);
                return new AssetPrepareResult(false, problems.Count > 0
                    ? $"No names could be read from the game. First problem: {problems[0]}"
                    : "No names could be read from the game.", problems);
            }

            int written = 0;
            foreach (VanillaKind kind in VanillaKinds.All)
            {
                List<uint> ids = iconsByKind[kind];
                if (ids.Count == 0)
                {
                    continue;
                }
                cancel.ThrowIfCancellationRequested();
                progress?.Report($"Cutting {Noun(kind)} icons");
                IconBatch batch = source.Icons(ids, icons);
                if (!batch.Ok)
                {
                    problems.Add($"The {Noun(kind)} icons could not be cut: {batch.Error ?? "no reason was given."}");
                    continue;
                }
                written += batch.Written;
                problems.AddRange(batch.Problems);
            }
            cancel.ThrowIfCancellationRequested();

            VanillaManifest manifest = new()
            {
                Recipe = Recipe,
                Build = build,
                Names = names,
                Icons = written,
                Problems = problems,
            };
            File.WriteAllText(ManifestPath(staging), JsonSerializer.Serialize(manifest, Json));
            lock (gate)
            {
                BusyFiles.SwapFolder(staging, folder);
            }
            RemoveOtherBuilds(build);
            return new AssetPrepareResult(true, null, problems);
        }
        catch (OperationCanceledException)
        {
            RemoveQuietly(staging);
            return new AssetPrepareResult(false, "Copying the game's names and icons was cancelled. The cache is as it was.", problems);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            RemoveQuietly(staging);
            return new AssetPrepareResult(false,
                $"The game's names and icons could not be saved in {root}: {exception.Message}", problems);
        }
    }

    // Deletes every other build's folder, and any build left half made, now
    // that build's cache is in. Only folders named like a build are touched.
    private void RemoveOtherBuilds(uint build)
    {
        string keep = BuildName(build);
        IEnumerable<string> folders;
        try
        {
            folders = Directory.EnumerateDirectories(root).ToList();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return;
        }
        foreach (string folder in folders)
        {
            string name = Path.GetFileName(folder);
            string bare = name.EndsWith(Staging, StringComparison.Ordinal) ? name[..^Staging.Length] : name;
            if (name != keep && IsBuildName(bare))
            {
                RemoveQuietly(folder);
            }
        }
    }

    private static bool IsBuildName(string name) =>
        name.Length == 8 && uint.TryParse(name, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out _);

    private static string BuildName(uint build) => build.ToString("X8", CultureInfo.InvariantCulture);

    private static string NamesFile(VanillaKind kind, string language) => $"{VanillaKinds.Code(kind)}.{language}.json";

    private static string IconFile(string buildFolder, ulong id) =>
        Path.Combine(buildFolder, "icons", id.ToString(CultureInfo.InvariantCulture) + ".png");

    private static string ManifestPath(string folder) => Path.Combine(folder, "manifest.json");

    // How progress sentences name a kind.
    private static string Noun(VanillaKind kind) => kind switch
    {
        VanillaKind.Craft => "crafting recipe",
        VanillaKind.Cooking => "cooking recipe",
        VanillaKind.Gimmick => "furniture",
        _ => VanillaKinds.Code(kind),
    };

    private static IReadOnlyList<VanillaEntry> ToEntries(VanillaKind kind, NameList list) =>
        list.Entries.Select(name => new VanillaEntry(kind, name.Id, name.Enum, name.Name, name.Icon, name.Group)).ToList();

    // A names file: the entries with the recipe that wrote them, so a file
    // from an older layout reads as missing.
    private sealed record NamesFileContent(int Recipe, IReadOnlyList<VanillaEntry> Entries);

    private static IReadOnlyList<VanillaEntry>? ReadNames(string path)
    {
        try
        {
            if (!File.Exists(path))
            {
                return null;
            }
            NamesFileContent? content = JsonSerializer.Deserialize<NamesFileContent>(File.ReadAllText(path), Json);
            return content is not null && content.Recipe == Recipe && content.Entries is not null ? content.Entries : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    private static void WriteNames(string path, IReadOnlyList<VanillaEntry> entries)
    {
        string json = JsonSerializer.Serialize(new NamesFileContent(Recipe, entries), Json);
        WriteAtomically(path, temporary => File.WriteAllText(temporary, json));
    }

    // Writes through a temporary file next to path, so a reader never sees
    // half a file.
    private static void WriteAtomically(string path, Action<string> write)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            write(temporary);
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
            {
                File.Delete(temporary);
            }
        }
    }

    private static void RemoveQuietly(string folder)
    {
        try
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Left for the next build to clear.
        }
    }
}
