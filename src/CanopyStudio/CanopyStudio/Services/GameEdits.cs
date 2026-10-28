using CanopyStudio.Core.Create;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Mods;
using CanopyStudio.Core.Platform;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.Services;

// The edits mod of the open game: the mod in its mods folder that the Game
// area saves edits to, as [patch.] sections. The choice is kept in the
// settings by game folder and opened again with that game. Pages that edit
// read Current, and leave saving to the user's Save.
public sealed partial class GameEdits : ObservableObject
{
    private readonly GameSession session;

    // The open edits mod, null when none is chosen or it could not be opened.
    [ObservableProperty]
    private EditsMod? current;

    // Why the chosen mod could not be opened, null when it was or when none
    // is chosen.
    [ObservableProperty]
    private string? problem;

    public GameEdits(GameSession session)
    {
        this.session = session;
        session.GameChanged += (_, _) => Reload();
        Reload();
    }

    // Raised after a page changes Current or saves it, so the edits mod bar
    // and the other pages can show the change.
    public event EventHandler? Edited;

    // Pages call this after each change they make to Current, and after
    // saving it.
    public void NotifyEdited() => Edited?.Invoke(this, EventArgs.Empty);

    // The game's mods folder, null without a game.
    public string? ModsFolder => session.Folder is { } folder ? Path.Combine(folder, "mods") : null;

    // The mods an edits mod can be: each folder in the mods folder with a
    // canopy.ini, in name order.
    public IReadOnlyList<string> Candidates()
    {
        if (ModsFolder is not { } mods || !Directory.Exists(mods))
        {
            return [];
        }
        try
        {
            return Directory.EnumerateDirectories(mods)
                .Where(folder => File.Exists(GameFiles.Find(folder, ModProject.IniName)))
                .Select(Path.GetFileName)
                .OfType<string>()
                .Order(StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return [];
        }
    }

    // Opens mods\<name> as the edits mod and keeps the choice. Null when
    // opened, else why not.
    public string? Choose(string name)
    {
        if (ModsFolder is not { } mods)
        {
            return "Open the game folder first.";
        }
        (EditsMod? mod, string? error) = EditsMod.Open(Path.Combine(mods, name));
        if (mod is null)
        {
            return error ?? $"{name} could not be opened.";
        }
        Current = mod;
        Problem = null;
        Keep(name);
        return null;
    }

    // Makes mods\<name>, a mod with only its [mod] section, and chooses it.
    // Null when made, else why not.
    public string? Create(string name)
    {
        if (ModsFolder is not { } mods)
        {
            return "Open the game folder first.";
        }
        if (Directory.Exists(Path.Combine(mods, name)))
        {
            return $"mods{Path.DirectorySeparatorChar}{name} already exists. Choose it, or pick another name.";
        }
        ProjectOpenResult made = ProjectTemplates.Create(mods, name, ProjectTemplates.Empty);
        return made.Ok ? Choose(name) : made.Error;
    }

    // Stops saving edits anywhere until a mod is chosen again.
    public void Clear()
    {
        Current = null;
        Problem = null;
        Keep(null);
    }

    private void Reload()
    {
        Current = null;
        Problem = null;
        if (session.Folder is not { } game || ModsFolder is not { } mods ||
            session.Settings.EditsMods?.GetValueOrDefault(game) is not { } name)
        {
            return;
        }
        (EditsMod? mod, string? error) = EditsMod.Open(Path.Combine(mods, name));
        Current = mod;
        Problem = mod is null ? error ?? $"The edits mod {name} could not be opened." : null;
    }

    private void Keep(string? name)
    {
        if (session.Folder is not { } game)
        {
            return;
        }
        session.Remember(settings =>
        {
            Dictionary<string, string> kept = new(settings.EditsMods ?? new Dictionary<string, string>(),
                StringComparer.OrdinalIgnoreCase);
            if (name is null)
            {
                kept.Remove(game);
            }
            else
            {
                kept[game] = name;
            }
            return settings with { EditsMods = kept };
        });
    }
}
