using System.Text.Json;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Setup;

// What Canopy Studio remembers on this PC. Never stored in a mod folder, so
// a shared mod never carries one person's paths.
public sealed record StudioSettings
{
    // The game folder the user picked or accepted, null before setup.
    public string? GameFolder { get; init; }
    // True once the user finished first-time setup. Until then Canopy Studio
    // opens on Setup and keeps the other areas closed.
    public bool SetupDone { get; init; }
    // The page id open when Canopy Studio last closed.
    public string? LastPage { get; init; }
    // The theme id picked in the rail, null for the default.
    public string? Theme { get; init; }
    // The language code game names are listed in, null for English.
    public string? NamesLanguage { get; init; }
    // The mod folders opened in Create, most recent first.
    public IReadOnlyList<string>? RecentProjects { get; init; }
    // By game folder, the dinput8.dll Canopy Studio renamed when it installed
    // Canopy there, so removing Canopy can put it back.
    public IReadOnlyDictionary<string, string>? ReplacedLoaders { get; init; }
    // By game folder, the folder name in its mods folder of the mod the Game
    // area saves its edits to.
    public IReadOnlyDictionary<string, string>? EditsMods { get; init; }
    public double WindowWidth { get; init; } = 1280;
    public double WindowHeight { get; init; } = 800;
    public bool WindowMaximized { get; init; }
}

// settings.json in %APPDATA%\CanopyStudio, ~/.config/CanopyStudio on Linux.
// A missing or damaged file reads
// as the defaults; a write goes to a temporary file first, so a crash never
// leaves half a file.
public sealed class SettingsStore(string folder)
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { WriteIndented = true };

    public static SettingsStore ForUser() =>
        new(StudioFolders.Root());

    public string FilePath { get; } = Path.Combine(folder, "settings.json");

    public StudioSettings Load()
    {
        try
        {
            return JsonSerializer.Deserialize<StudioSettings>(File.ReadAllText(FilePath), Json) ?? new StudioSettings();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return new StudioSettings();
        }
    }

    // False when the file could not be written; the settings then last
    // until Canopy Studio closes.
    public bool Save(StudioSettings settings)
    {
        try
        {
            Directory.CreateDirectory(folder);
            string temporary = FilePath + ".tmp";
            File.WriteAllText(temporary, JsonSerializer.Serialize(settings, Json));
            File.Move(temporary, FilePath, overwrite: true);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }
}
