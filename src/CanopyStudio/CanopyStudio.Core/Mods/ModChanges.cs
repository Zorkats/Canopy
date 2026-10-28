using System.Text.Json;
using System.Text.Json.Serialization;
using CanopyStudio.Core.Platform;
using CanopyStudio.Core.Setup;

namespace CanopyStudio.Core.Mods;

// The outcome of a change to the game folder, said for the player.
public sealed record ModResult(bool Ok, string Message);

public enum ModChangeKind
{
    Install,
    Remove,
}

// One file a change touched, relative to the game folder, with its SHA-256
// before and after; null where the file was not there.
public sealed record JournalFile(string Path, string? Before, string? After);

// One change in the journal. Folders are relative to the game folder too:
// mods when it was there, and every folder of the mod.
public sealed record ModChange(
    string Id,
    string Game,
    ModChangeKind Kind,
    string Mod,
    DateTime When,
    IReadOnlyList<string> FoldersBefore,
    IReadOnlyList<string> FoldersAfter,
    IReadOnlyList<JournalFile> Files);

// Installs and removes mods, and undoes the last change. Before a change,
// every file of the mod it touches is copied to a folder of its own in the
// backup folder, outside the game folder, and the change is recorded in
// journal.json there. Each write is read back. A failure part way puts the
// mod back from those copies; undo does too, and is refused when the mod
// changed since, so it never throws away work done after.
public static class ModChanges
{
    public const string JournalName = "journal.json";
    // Older changes, and their copies, are dropped past this many.
    private const int MaxChanges = 20;

    private const string GameRunningText = "Close the game first: Canopy reads the mods folder as the game starts, and Windows keeps mod DLLs in use while it runs.";

    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    // A mod's files by path with their SHA-256, and its folders.
    private sealed record State(HashSet<string> Folders, Dictionary<string, string> Files)
    {
        public static State Empty() => new(new(StringComparer.OrdinalIgnoreCase), new(StringComparer.OrdinalIgnoreCase));

        public bool SameAs(State other) => Folders.SetEquals(other.Folders) && Files.Count == other.Files.Count
            && Files.All(pair => other.Files.TryGetValue(pair.Key, out string? sha) && sha == pair.Value);
    }

    public static ModResult Apply(string gameFolder, ModInstallPlan plan, string backupFolder, bool gameRunning, DateTime now)
    {
        if (!plan.Ok)
        {
            return new(false, plan.Problems[0]);
        }
        if (Refusal(gameFolder, backupFolder, plan.Mod, gameRunning) is { } refused)
        {
            return new(false, refused);
        }
        string game = Path.GetFullPath(gameFolder);
        if (Scan(game, plan.Mod, out State before) is { } unreadable)
        {
            return new(false, unreadable);
        }
        HashSet<string> expected = new([.. plan.Replaces, .. plan.Removes], StringComparer.OrdinalIgnoreCase);
        if (!expected.SetEquals(before.Files.Keys))
        {
            return new(false, $"{ModNames.Shown(plan.Mod)} changed since the install was planned, so nothing was written. Plan the install again.");
        }
        State after = State.Empty();
        after.Folders.Add(ModNames.ModsName(game));
        Dictionary<string, byte[]> bytes = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlannedFile file in plan.Writes)
        {
            after.Files[file.Path] = ModNames.Sha256(file.Bytes);
            bytes[file.Path] = file.Bytes;
            for (string? folder = Path.GetDirectoryName(file.Path); !string.IsNullOrEmpty(folder); folder = Path.GetDirectoryName(folder))
            {
                after.Folders.Add(folder);
            }
        }
        return Record(game, backupFolder, ModChangeKind.Install, plan.Mod, now, before, after, path => bytes[path]);
    }

    public static ModResult Remove(string gameFolder, string mod, string backupFolder, bool gameRunning, DateTime now)
    {
        if (Refusal(gameFolder, backupFolder, mod, gameRunning) is { } refused)
        {
            return new(false, refused);
        }
        string game = Path.GetFullPath(gameFolder);
        string target = Path.Combine(ModNames.ModsIn(game), mod);
        if (!File.Exists(target) && !Directory.Exists(target))
        {
            return new(false, $"{ModNames.Shown(mod)} is not there, so there is nothing to remove.");
        }
        if (Scan(game, mod, out State before) is { } unreadable)
        {
            return new(false, unreadable);
        }
        State after = State.Empty();
        after.Folders.Add(ModNames.ModsName(game));
        return Record(game, backupFolder, ModChangeKind.Remove, mod, now, before, after, _ => []);
    }

    // The last change made in gameFolder, the one undo takes back, or null.
    public static ModChange? Last(string gameFolder, string backupFolder)
    {
        string game = Path.GetFullPath(gameFolder);
        return ReadJournal(backupFolder, out List<ModChange> journal) is null
            ? journal.LastOrDefault(change => change.Game.Equals(game, StringComparison.OrdinalIgnoreCase))
            : null;
    }

    public static ModResult Undo(string gameFolder, string backupFolder, bool gameRunning)
    {
        if (gameRunning)
        {
            return new(false, GameRunningText);
        }
        if (ReadJournal(backupFolder, out List<ModChange> journal) is { } damaged)
        {
            return new(false, damaged);
        }
        string game = Path.GetFullPath(gameFolder);
        ModChange? change = journal.LastOrDefault(item => item.Game.Equals(game, StringComparison.OrdinalIgnoreCase));
        if (change is null)
        {
            return new(false, "There is no mod change to undo in this game folder.");
        }
        string what = Describe(change);
        if (Refusal(game, backupFolder, change.Mod, false) is { } refused)
        {
            return new(false, refused);
        }
        if (!Belongs(change))
        {
            return new(false, $"The journal entry for {what} names files outside {ModNames.Shown(change.Mod)}, so it was not used.");
        }
        string copies = Path.Combine(backupFolder, change.Id);
        State before = State.Empty();
        State after = State.Empty();
        before.Folders.UnionWith(change.FoldersBefore);
        after.Folders.UnionWith(change.FoldersAfter);
        foreach (JournalFile file in change.Files)
        {
            if (file.Before is not null)
            {
                before.Files[file.Path] = file.Before;
                string copy = Path.Combine(copies, file.Path);
                if (!File.Exists(copy) || ModNames.Sha256File(copy) != file.Before)
                {
                    return new(false, $"The backup of {file.Path} is missing or changed, so {what} cannot be undone.");
                }
            }
            if (file.After is not null)
            {
                after.Files[file.Path] = file.After;
            }
        }
        if (Scan(game, change.Mod, out State now) is { } unreadable)
        {
            return new(false, unreadable);
        }
        if (!now.SameAs(after))
        {
            return new(false, $"{ModNames.Shown(change.Mod)} changed after {what}, so undoing it could lose that change. Nothing was undone.");
        }
        string stash = Path.Combine(backupFolder, "undo-" + Guid.NewGuid().ToString("N"));
        string? failed = Change(game, change.Mod, after, before, stash, path => File.ReadAllBytes(Path.Combine(copies, path)), () =>
        {
            journal.Remove(change);
            return WriteJournal(backupFolder, journal);
        }, out bool restored);
        if (failed is null || restored)
        {
            TryDeleteFolder(stash);
        }
        if (failed is not null)
        {
            return new(false, $"{Capital(what)} was not undone: {failed}");
        }
        TryDeleteFolder(copies);
        return new(true, $"{Capital(what)} is undone. {ModNames.Shown(change.Mod)} is as it was before.");
    }

    // Why the target of a change may not be changed now, or null.
    private static string? Refusal(string gameFolder, string backupFolder, string mod, bool gameRunning)
    {
        if (gameRunning)
        {
            return GameRunningText;
        }
        if (!File.Exists(GameFiles.Find(gameFolder, GameLocator.GameExe)))
        {
            return $"{GameLocator.GameExe} is not in this folder, so its mods were left alone.";
        }
        if (ModNames.Inside(backupFolder, gameFolder))
        {
            return "The backup folder is inside the game folder; choose one outside it, so a Steam check never removes the backups.";
        }
        if (ModNames.NameProblem(mod) is not null || ModNames.Same(mod, ModNames.DataFolder))
        {
            return $"\"{mod}\" is not a mod in the mods folder, so nothing was changed.";
        }
        return LinkProblem(Path.Combine(ModNames.ModsIn(gameFolder), mod), mod);
    }

    // Why target, a mod in mods, is left alone because it is or holds a
    // link, such as a project folder a playtest linked in. Null when it is
    // plain files and folders.
    internal static string? LinkProblem(string target, string mod)
    {
        FileSystemInfo? info = Directory.Exists(target) ? new DirectoryInfo(target) : File.Exists(target) ? new FileInfo(target) : null;
        bool linked = info is not null && (ModNames.IsLink(info) || (info is DirectoryInfo folder && folder
            .EnumerateFileSystemInfos("*", new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = 0 })
            .Any(ModNames.IsLink)));
        return linked
            ? $"{ModNames.Shown(mod)} is or holds a link to another folder, so Canopy Studio leaves it alone. Change it in {FileManager} instead."
            : null;
    }

    private static string FileManager => OperatingSystem.IsWindows() ? "File Explorer" : "your file manager";

    // Copies the mod's files aside, makes the change and records it.
    private static ModResult Record(string game, string backupFolder, ModChangeKind kind, string mod, DateTime now,
        State before, State after, Func<string, byte[]> bytes)
    {
        if (ReadJournal(backupFolder, out List<ModChange> journal) is { } damaged)
        {
            return new(false, damaged);
        }
        string id = $"{now:yyyyMMdd-HHmmss}-{Guid.NewGuid().ToString("N")[..8]}";
        string copies = Path.Combine(backupFolder, id);
        ModChange change = new(id, game, kind, mod, now, Sorted(before.Folders), Sorted(after.Folders),
            before.Files.Keys.Union(after.Files.Keys, StringComparer.OrdinalIgnoreCase)
                .Order(StringComparer.OrdinalIgnoreCase)
                .Select(path => new JournalFile(path, before.Files.GetValueOrDefault(path), after.Files.GetValueOrDefault(path)))
                .ToList());
        List<ModChange> dropped = [];
        string? failed = Change(game, mod, before, after, copies, bytes, () =>
        {
            journal.Add(change);
            dropped = journal.Count > MaxChanges ? journal.GetRange(0, journal.Count - MaxChanges) : [];
            journal.RemoveRange(0, dropped.Count);
            return WriteJournal(backupFolder, journal);
        }, out bool restored);
        if (failed is not null)
        {
            if (restored)
            {
                TryDeleteFolder(copies);
            }
            return new(false, $"{Capital(Describe(change))} did not happen: {failed}");
        }
        dropped.ForEach(old => TryDeleteFolder(Path.Combine(backupFolder, old.Id)));
        return new(true, kind == ModChangeKind.Install
            ? $"{mod} is installed. It loads the next time the game starts, and undo puts the mods folder back as it was."
            : $"{mod} is removed. A copy is kept outside the game folder, so undo can bring it back.");
    }

    // Copies from's files to stash, turns the mod from from into to, then
    // runs commit, which records the change. When a step fails, the mod is
    // put back to from. Null when done; otherwise why not, with restored
    // saying whether the mod is as it was.
    private static string? Change(string game, string mod, State from, State to, string stash,
        Func<string, byte[]> bytes, Func<string?> commit, out bool restored)
    {
        restored = true;
        try
        {
            foreach (string path in from.Files.Keys)
            {
                string copy = Path.Combine(stash, path);
                Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
                File.Copy(Path.Combine(game, path), copy);
                if (ModNames.Sha256File(copy) != from.Files[path])
                {
                    throw new IOException($"The backup copy of {path} does not match it.");
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{exception.Message} Nothing in the game folder was changed.";
        }
        string? failed;
        try
        {
            Turn(game, from, to, bytes);
            failed = commit();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            failed = exception.Message;
        }
        if (failed is null)
        {
            return null;
        }
        restored = PutBack(game, mod, from, stash);
        return restored
            ? $"{failed} The game folder is as it was."
            : $"{failed} Some files could not be put back; copies of them are in {stash}.";
    }

    // Puts the mod back to state from the copies in stash, whatever was
    // done so far. False when that failed too.
    private static bool PutBack(string game, string mod, State state, string stash)
    {
        try
        {
            Turn(game, Snapshot(game, mod), state, path => File.ReadAllBytes(Path.Combine(stash, path)));
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    // Makes the folders and files of to, then removes what from had and to
    // lacks. Files already right are left alone. Every write is read back.
    private static void Turn(string game, State from, State to, Func<string, byte[]> bytes)
    {
        foreach (string folder in to.Folders.Where(folder => !from.Folders.Contains(folder)).OrderBy(path => path.Length))
        {
            Directory.CreateDirectory(Path.Combine(game, folder));
        }
        foreach ((string path, string sha) in to.Files.OrderBy(pair => pair.Key, StringComparer.OrdinalIgnoreCase))
        {
            if (from.Files.GetValueOrDefault(path) == sha)
            {
                continue;
            }
            string full = Path.Combine(game, path);
            File.WriteAllBytes(full, bytes(path));
            if (ModNames.Sha256File(full) != sha)
            {
                throw new IOException($"{path} did not read back as it was written.");
            }
        }
        foreach (string path in from.Files.Keys.Where(path => !to.Files.ContainsKey(path)).Order(StringComparer.OrdinalIgnoreCase))
        {
            File.Delete(Path.Combine(game, path));
        }
        foreach (string folder in from.Folders.Where(folder => !to.Folders.Contains(folder)).OrderByDescending(path => path.Length))
        {
            // mods may hold other mods' files by now, such as a settings
            // file Canopy wrote; it then stays.
            string full = Path.Combine(game, folder);
            if (!folder.Equals(ModNames.ModsFolder, StringComparison.OrdinalIgnoreCase) || !Directory.EnumerateFileSystemEntries(full).Any())
            {
                Directory.Delete(full);
            }
        }
    }

    private static string? Scan(string game, string mod, out State state)
    {
        try
        {
            state = Snapshot(game, mod);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            state = State.Empty();
            return $"{ModNames.Shown(mod)} could not be read, so nothing was changed: {exception.Message}";
        }
    }

    // The mod's files and folders now, and mods itself when it is there.
    private static State Snapshot(string game, string mod)
    {
        State state = State.Empty();
        string mods = ModNames.ModsIn(game);
        if (Directory.Exists(mods))
        {
            state.Folders.Add(Path.GetFileName(mods));
        }
        string target = Path.Combine(mods, mod);
        if (File.Exists(target))
        {
            state.Files[Path.GetRelativePath(game, target)] = ModNames.Sha256File(target);
        }
        else if (Directory.Exists(target))
        {
            Walk(game, new DirectoryInfo(target), state);
        }
        return state;
    }

    private static void Walk(string game, DirectoryInfo folder, State state)
    {
        if (ModNames.IsLink(folder))
        {
            throw new IOException($"{Path.GetRelativePath(game, folder.FullName)} is a link to another folder.");
        }
        state.Folders.Add(Path.GetRelativePath(game, folder.FullName));
        foreach (FileSystemInfo item in folder.EnumerateFileSystemInfos("*", new EnumerationOptions { AttributesToSkip = 0 }))
        {
            if (item is DirectoryInfo sub)
            {
                Walk(game, sub, state);
            }
            else
            {
                state.Files[Path.GetRelativePath(game, item.FullName)] = ModNames.Sha256File(item.FullName);
            }
        }
    }

    // True when every path in change stays in mods\<Mod>, or is mods.
    private static bool Belongs(ModChange change)
    {
        // Any root does: the paths are only compared, never opened.
        string root = OperatingSystem.IsWindows() ? @"X:\" : "/x/";
        string target = Path.Combine(root, ModNames.ModsFolder, change.Mod);
        return change.Files.Select(file => file.Path).Concat(change.FoldersBefore).Concat(change.FoldersAfter)
            .All(path => !Path.IsPathRooted(path) && (path.Equals(ModNames.ModsFolder, StringComparison.OrdinalIgnoreCase)
                || ModNames.Inside(Path.Combine(root, path), target)));
    }

    // Null when the journal was read, or there is none yet; otherwise why
    // it cannot be used.
    private static string? ReadJournal(string backupFolder, out List<ModChange> journal)
    {
        journal = [];
        string path = Path.Combine(backupFolder, JournalName);
        try
        {
            if (File.Exists(path))
            {
                journal = JsonSerializer.Deserialize<List<ModChange>>(File.ReadAllText(path), Json) ?? [];
            }
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return $"The mod change journal at {path} cannot be read, so nothing was changed. Move it aside and try again.";
        }
    }

    private static string? WriteJournal(string backupFolder, List<ModChange> journal)
    {
        string path = Path.Combine(backupFolder, JournalName);
        string temporary = path + ".tmp";
        try
        {
            Directory.CreateDirectory(backupFolder);
            File.WriteAllText(temporary, JsonSerializer.Serialize(journal, Json));
            BusyFiles.Replace(temporary, path);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"The mod change journal could not be written: {exception.Message}";
        }
    }

    private static string Describe(ModChange change) =>
        change.Kind == ModChangeKind.Install ? $"the install of {change.Mod}" : $"the removal of {change.Mod}";

    private static string Capital(string text) => char.ToUpperInvariant(text[0]) + text[1..];

    private static List<string> Sorted(HashSet<string> paths) => paths.Order(StringComparer.OrdinalIgnoreCase).ToList();

    private static void TryDeleteFolder(string path)
    {
        try
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path, recursive: true);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }
}
