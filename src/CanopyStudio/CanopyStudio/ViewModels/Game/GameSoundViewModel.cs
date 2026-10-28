using System.ComponentModel;
using System.Globalization;
using Avalonia.Threading;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Sound;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// The game's music and sound effects: every .nlsd under data/bgm and data/se,
// played through SDL2, exported, and replaced as a file of the edits mod
// (docs/MODDING.md, Replace game files). Nothing plays until the user
// presses Play. Decoding and file work run off the UI thread.
public sealed partial class GameSoundViewModel : PageViewModel, IDisposable
{
    private const string BgmTable = "bgm.dat";

    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private List<SoundEntry> all = [];
    private bool loaded;
    private int loadVersion;
    private ISoundPlayer? player;
    // The sound last decoded, the entry it is, and the edits mod file it
    // came from, null for the game's own.
    private DecodedSound? decoded;
    private SoundEntry? decodedEntry;
    private string? decodedSource;
    private int decodeVersion;
    // True while the timer moves the position slider, so the move does not
    // seek.
    private bool followingPlayer;

    [ObservableProperty]
    private bool isLoading;

    [ObservableProperty]
    private string? loadError;

    [ObservableProperty]
    private string filterText = "";

    [ObservableProperty]
    private IReadOnlyList<SoundEntry> music = [];

    [ObservableProperty]
    private IReadOnlyList<SoundEntry> effects = [];

    // 0 shows Music, 1 Sound effects.
    [ObservableProperty]
    private int listIndex;

    [ObservableProperty]
    private SoundEntry? selectedMusic;

    [ObservableProperty]
    private SoundEntry? selectedEffect;

    [ObservableProperty]
    private bool isDecoding;

    // Why the sound could not be decoded or played, null when it could.
    [ObservableProperty]
    private string? playerMessage;

    // Where playback is and how long the sound is, in seconds.
    [ObservableProperty]
    private double position;

    [ObservableProperty]
    private double duration;

    [ObservableProperty]
    private bool loopOn = true;

    // From 0 to 100, kept while Canopy Studio runs.
    [ObservableProperty]
    private double volume = 80;

    [ObservableProperty]
    private string? status;

    [ObservableProperty]
    private bool statusIsError;

    [ObservableProperty]
    private bool isWorking;

    public GameSoundViewModel(GameSession session, GameEdits edits, IStudioDialogs dialogs)
    {
        Session = session;
        Edits = edits;
        Dialogs = dialogs;
        session.GameChanged += (_, _) => Reset();
        session.PropertyChanged += (_, _) => OnPropertyChanged(nameof(NeedsGame));
        edits.Edited += (_, _) => RefreshReplaced();
        edits.PropertyChanged += OnEditsChanged;
        timer.Tick += (_, _) => FollowPlayer();
    }

    public GameSession Session { get; }
    public GameEdits Edits { get; }
    public IStudioDialogs Dialogs { get; }

    public override string Id => PageIds.Sound;
    public override string Title => "Sound";

    public bool NeedsGame => !Session.HasGame;
    public bool IsReady => loaded && LoadError is null;

    public string MusicTitle => $"Music ({Music.Count:N0})";
    public string EffectsTitle => $"Sound effects ({Effects.Count:N0})";

    // The sound picked in the list on show.
    public SoundEntry? Selected => ListIndex == 0 ? SelectedMusic : SelectedEffect;
    public bool HasSelection => Selected is not null;
    public bool NoSelection => Selected is null;

    public bool IsPlaying => player is { IsPlaying: true } && decodedEntry == Selected;
    public bool IsPaused => player is { IsPaused: true } && decodedEntry == Selected;
    public bool CanPlay => HasSelection && !IsDecoding && !IsPlaying;
    public bool CanPause => IsPlaying;
    public bool CanStop => IsPlaying || IsPaused;
    public string PlayLabel => IsPaused ? "Resume" : "Play";
    public string PositionText => $"{SoundEntry.Time(TimeSpan.FromSeconds(Position))} / {SoundEntry.Time(TimeSpan.FromSeconds(Duration))}";

    public string LoopDetail => Selected switch
    {
        null => "",
        { IsReplaced: true, IsMusic: true } => "The replacement plays once through, then loops whole, as the game plays it.",
        { Header: { Loops: true } } => "An intro, a loop and an ending. With Loop on, the intro plays once and the loop repeats.",
        { IsMusic: true, Header: not null } => "One part. With Loop on, it repeats whole.",
        { Header: not null } => "Plays once.",
        _ => "",
    };

    public bool HasEditsMod => Edits.Current is not null;
    public bool NoEditsMod => Edits.Current is null;
    public bool CanReplace => HasSelection && HasEditsMod && Selected?.Header is not null && !IsWorking;
    public bool CanRevert => Selected is { IsReplaced: true } && HasEditsMod && !IsWorking;
    public bool CanExport => HasSelection && !IsWorking;
    public bool CanExportAll => VisibleList.Count > 0 && !IsWorking;
    public string ReplaceKinds => Selected is { IsMusic: true }
        ? "Music takes an Ogg Vorbis file at 48,000 Hz with the original's channels."
        : "A sound effect takes an Ogg Vorbis file, or a WAV file of 16-bit PCM, at 48,000 Hz with the original's channels.";
    public string ReplacedNote => Selected is { IsReplaced: true } && Edits.Current is { } mod
        ? $"{mod.Name} replaces this sound. Play plays its file; Export saves the game's own."
        : "";
    public bool HasReplacedNote => ReplacedNote.Length > 0;

    private IReadOnlyList<SoundEntry> VisibleList => ListIndex == 0 ? Music : Effects;

    public override void OnShown()
    {
        if (!loaded && !IsLoading && Session.HasGame)
        {
            _ = LoadAsync();
        }
    }

    public void Dispose()
    {
        timer.Stop();
        player?.Dispose();
        player = null;
    }

    [RelayCommand]
    private void ClearFilter() => FilterText = "";

    [RelayCommand]
    private async Task PlayAsync()
    {
        if (Selected is not { } entry)
        {
            return;
        }
        if (player is { IsPaused: true } && decodedEntry == entry)
        {
            player.Resume();
            timer.Start();
            RaisePlayState();
            return;
        }
        if (await DecodeAsync(entry) is not { } sound || Selected != entry)
        {
            return;
        }
        player ??= new SdlSoundPlayer();
        player.Volume = (float)(Volume / 100);
        if (player.Play(sound, LoopFor(entry, sound)) is { } problem)
        {
            PlayerMessage = problem;
            RaisePlayState();
            return;
        }
        // Play goes on from where the slider was put, unless it is at the end.
        if (Position > 0 && Position < Duration - 0.05)
        {
            player.Seek((long)(Position * sound.SampleRate));
        }
        timer.Start();
        RaisePlayState();
    }

    [RelayCommand]
    private void Pause()
    {
        player?.Pause();
        timer.Stop();
        RaisePlayState();
    }

    [RelayCommand]
    private void Stop()
    {
        player?.Stop();
        timer.Stop();
        followingPlayer = true;
        Position = 0;
        followingPlayer = false;
        RaisePlayState();
    }

    [RelayCommand]
    private async Task ExportSelectedAsync()
    {
        if (Selected is not { } entry || Session.Folder is not { } game)
        {
            return;
        }
        if (await Dialogs.PickFolderAsync("Choose where to export the sound") is { } target)
        {
            await ExportAsync(game, target, [entry]);
        }
    }

    [RelayCommand]
    private async Task ExportAllAsync()
    {
        IReadOnlyList<SoundEntry> entries = VisibleList;
        if (entries.Count == 0 || Session.Folder is not { } game)
        {
            return;
        }
        if (await Dialogs.PickFolderAsync($"Choose where to export {entries.Count:N0} sounds") is { } target)
        {
            await ExportAsync(game, target, entries);
        }
    }

    [RelayCommand]
    private async Task ReplaceAsync()
    {
        if (Selected is not { Header: { } original } entry || Edits.Current is not { } mod)
        {
            return;
        }
        IReadOnlyList<string> files = entry.IsMusic
            ? await Dialogs.PickFilesAsync($"Pick the new sound for {entry.Name}", "Ogg Vorbis", ["*.ogg"])
            : await Dialogs.PickFilesAsync($"Pick the new sound for {entry.Name}", "Ogg Vorbis or WAV", ["*.ogg", "*.wav"]);
        if (files.Count == 0)
        {
            return;
        }
        string target = GameSounds.OverridePath(mod.Folder, entry.Path);
        if (entry.IsReplaced && !await Dialogs.ConfirmAsync($"Replace {entry.Name} again?",
                $"{mod.Name} already has its own {entry.Name}. The file you picked takes its place.", "Replace"))
        {
            return;
        }
        string picked = files[0];
        IsWorking = true;
        (DecodedSound? sound, string? error) = await Task.Run(() => WriteReplacement(picked, entry.IsMusic, original, target));
        IsWorking = false;
        if (sound is null)
        {
            Show(error ?? "The sound could not be replaced.", error: true);
            return;
        }
        Forget(entry);
        entry.IsReplaced = true;
        string loop = entry.IsMusic ? await SetWholeLoopAsync(mod, entry.Path, sound.Frames) : "";
        Show($"Written to {target}. Restart the game to hear it.{loop}");
        RaiseSelection();
    }

    [RelayCommand]
    private async Task RevertAsync()
    {
        if (Selected is not { IsReplaced: true } entry || Edits.Current is not { } mod)
        {
            return;
        }
        string target = GameSounds.OverridePath(mod.Folder, entry.Path);
        if (!await Dialogs.ConfirmAsync($"Revert {entry.Name}?",
                $"{target} is deleted, and the game plays its own sound again after a restart.", "Revert"))
        {
            return;
        }
        IsWorking = true;
        (long frames, string? error) = await Task.Run(() =>
        {
            long length = ReadLength(target);
            return (length, GameSounds.Remove(mod.Folder, entry.Path));
        });
        IsWorking = false;
        if (error is not null)
        {
            Show(error, error: true);
            return;
        }
        Forget(entry);
        entry.IsReplaced = false;
        string loop = entry.IsMusic ? await ClearWholeLoopAsync(mod, entry.Path, frames) : "";
        Show($"{entry.Name} is the game's own again. Restart the game to hear it.{loop}");
        RaiseSelection();
    }

    private async Task LoadAsync()
    {
        int version = ++loadVersion;
        IsLoading = true;
        LoadError = null;
        FileList list = await Session.FilesAsync();
        if (version != loadVersion)
        {
            return;
        }
        IsLoading = false;
        if (!list.Ok)
        {
            LoadError = list.Error ?? "The file list could not be read.";
            return;
        }
        all = list.Files.Select(file => file.Path).Where(GameSounds.IsSound)
            .Order(StringComparer.OrdinalIgnoreCase).Select(path => new SoundEntry(path)).ToList();
        loaded = true;
        OnPropertyChanged(nameof(IsReady));
        RefreshReplaced();
        RefreshLists();
        if (Session.Folder is { } folder)
        {
            _ = ReadHeadersAsync(folder, all, version);
        }
    }

    // Each header, music first, handed to the UI thread as it is read.
    private Task ReadHeadersAsync(string folder, IReadOnlyList<SoundEntry> entries, int version) => Task.Run(() =>
    {
        foreach (SoundEntry entry in entries)
        {
            if (Volatile.Read(ref loadVersion) != version)
            {
                return;
            }
            (NlsdFile? file, string? error) = GameSounds.Read(folder, entry.Path);
            NlsdHeader? header = file?.Header;
            Dispatcher.UIThread.Post(() =>
            {
                if (loadVersion != version)
                {
                    return;
                }
                entry.Header = header;
                entry.HeaderError = error;
                if (entry == Selected)
                {
                    ShowLength();
                    RaiseSelection();
                }
            }, DispatcherPriority.Background);
        }
    });

    private void Reset()
    {
        ++loadVersion;
        Stop();
        Forget(null);
        loaded = false;
        all = [];
        Music = [];
        Effects = [];
        SelectedMusic = null;
        SelectedEffect = null;
        LoadError = null;
        Status = null;
        OnPropertyChanged(nameof(IsReady));
    }

    private void RefreshLists()
    {
        string filter = FilterText.Trim();
        IEnumerable<SoundEntry> shown = filter.Length == 0
            ? all
            : all.Where(entry => entry.Path.Contains(filter, StringComparison.OrdinalIgnoreCase));
        List<SoundEntry> list = shown.ToList();
        Music = list.Where(entry => entry.IsMusic).ToList();
        Effects = list.Where(entry => !entry.IsMusic).ToList();
    }

    // Marks every sound the edits mod has its own copy of.
    private void RefreshReplaced()
    {
        HashSet<string> replaced = Edits.Current is { } mod ? GameSounds.Replaced(mod.Folder) : [];
        foreach (SoundEntry entry in all)
        {
            bool now = replaced.Contains(entry.Path);
            if (entry.IsReplaced != now)
            {
                Forget(entry);
                entry.IsReplaced = now;
            }
        }
        RaiseSelection();
    }

    private void OnEditsChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName == nameof(GameEdits.Current))
        {
            RefreshReplaced();
        }
    }

    private async Task<DecodedSound?> DecodeAsync(SoundEntry entry)
    {
        string? source = entry.IsReplaced && Edits.Current is { } mod ? GameSounds.OverridePath(mod.Folder, entry.Path) : null;
        if (decoded is not null && decodedEntry == entry && decodedSource == source)
        {
            return decoded;
        }
        if (Session.Folder is not { } folder)
        {
            return null;
        }
        int version = ++decodeVersion;
        IsDecoding = true;
        PlayerMessage = null;
        (DecodedSound? sound, string? error) = await Task.Run(() => Decode(folder, entry.Path, source));
        if (version != decodeVersion)
        {
            return null;
        }
        IsDecoding = false;
        if (sound is null)
        {
            PlayerMessage = error;
            return null;
        }
        player?.Stop();
        decoded = sound;
        decodedEntry = entry;
        decodedSource = source;
        Duration = sound.Length.TotalSeconds;
        return sound;
    }

    private static (DecodedSound? Sound, string? Error) Decode(string game, string gamePath, string? source)
    {
        NlsdFile? file;
        string? error;
        if (source is null)
        {
            (file, error) = GameSounds.Read(game, gamePath);
        }
        else
        {
            try
            {
                (file, error) = NlsdFile.Read(File.ReadAllBytes(source));
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return (null, $"{source} could not be read: {exception.Message}");
            }
        }
        if (file is null)
        {
            return (null, error);
        }
        (DecodedSound? sound, string? why) = SoundDecoder.Decode(file);
        return sound is null ? (null, $"{gamePath} could not be decoded: {why}") : (sound, null);
    }

    // Music repeats its loop part, or the whole track when it has none, as
    // the game plays it; a sound effect repeats only a loop part it has.
    private SoundLoop? LoopFor(SoundEntry entry, DecodedSound sound)
    {
        if (!LoopOn)
        {
            return null;
        }
        if (sound.Links.Count == 3)
        {
            return new SoundLoop(sound.Links[0], sound.Links[0] + sound.Links[1]);
        }
        return entry.IsMusic ? new SoundLoop(0, sound.Frames) : null;
    }

    // Drops the decoded sound of entry, or any when entry is null, and stops
    // it if it plays.
    private void Forget(SoundEntry? entry)
    {
        if (entry is not null && decodedEntry != entry)
        {
            return;
        }
        if (decodedEntry is not null)
        {
            player?.Stop();
            timer.Stop();
        }
        decoded = null;
        decodedEntry = null;
        decodedSource = null;
        RaisePlayState();
    }

    private void FollowPlayer()
    {
        if (player is null || decoded is null)
        {
            timer.Stop();
            return;
        }
        followingPlayer = true;
        Position = (double)player.Position / decoded.SampleRate;
        followingPlayer = false;
        if (!player.IsPlaying && !player.IsPaused)
        {
            // Played to the end: back to the start, as Stop leaves it.
            Stop();
        }
    }

    private async Task ExportAsync(string game, string target, IReadOnlyList<SoundEntry> entries)
    {
        IsWorking = true;
        Show($"Reading {entries.Count:N0} sounds...");
        // The names first, so one question covers every file already there.
        List<(SoundEntry Entry, string? Name)> names = await Task.Run(() => entries.Select(entry =>
        {
            NlsdHeader? header = entry.Header ?? GameSounds.Read(game, entry.Path).File?.Header;
            string? extension = header is null ? null : GameSounds.ExportExtension(header);
            return (entry, extension is null ? null : GameSounds.ExportName(entry.Path, extension));
        }).ToList());
        int existing = names.Count(item => item.Name is { } name && File.Exists(Path.Combine(target, name)));
        if (existing > 0)
        {
            string what = existing == 1 ? "1 file is" : $"{existing:N0} files are";
            if (!await Dialogs.ConfirmAsync("Overwrite files?",
                    $"{what} already in {target} with the name an export takes. They are overwritten.", "Overwrite"))
            {
                IsWorking = false;
                Show("Nothing was exported.");
                return;
            }
        }
        Progress<int> progress = new(done => Status = $"Exporting {done:N0} of {names.Count:N0}...");
        List<string> failures = await Task.Run(() => ExportFiles(game, target, names, progress));
        IsWorking = false;
        int written = names.Count - failures.Count;
        string count = written == 1 ? "1 sound" : $"{written:N0} sounds";
        Show(failures.Count == 0
            ? $"Exported {count} to {target}."
            : $"Exported {count} to {target}. {failures.Count:N0} could not be: {failures[0]}", error: failures.Count > 0);
    }

    private static List<string> ExportFiles(string game, string target, IReadOnlyList<(SoundEntry Entry, string? Name)> names,
        IProgress<int> progress)
    {
        List<string> failures = [];
        for (int index = 0; index < names.Count; ++index)
        {
            (SoundEntry entry, string? name) = names[index];
            progress.Report(index + 1);
            (NlsdFile? file, string? error) = GameSounds.Read(game, entry.Path);
            (SoundExport? export, string? why) = (null, error);
            if (file is not null)
            {
                (export, why) = GameSounds.Export(file);
            }
            if (export is null || name is null)
            {
                failures.Add(why ?? error ?? $"{entry.Path} could not be exported.");
                continue;
            }
            try
            {
                Directory.CreateDirectory(target);
                File.WriteAllBytes(Path.Combine(target, name), export.Bytes);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                failures.Add($"{Path.Combine(target, name)} could not be written: {exception.Message}");
            }
        }
        return failures;
    }

    private static (DecodedSound? Sound, string? Error) WriteReplacement(string picked, bool music, NlsdHeader original, string target)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(picked);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return (null, $"{picked} could not be read: {exception.Message}");
        }
        (NlsdFile? file, DecodedSound? sound, string? error) = GameSounds.Replacement(bytes, picked, music, original);
        if (file is null)
        {
            return (null, error);
        }
        return GameSounds.Write(target, file) is { } failed ? (null, failed) : (sound, null);
    }

    // The samples per channel in the .nlsd at path, 0 when it cannot be read.
    private static long ReadLength(string path)
    {
        try
        {
            byte[] head = new byte[NlsdHeader.Size];
            using FileStream stream = File.OpenRead(path);
            return stream.ReadAtLeast(head, head.Length, throwOnEndOfStream: false) == head.Length
                ? NlsdHeader.Read(head)!.SamplesPerChannel
                : 0;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return 0;
        }
    }

    // bgm.dat's row for gamePath and its loop columns, when the patch rules
    // can set both; else why not.
    private async Task<(ulong Id, string? Problem)> LoopRowAsync(string gamePath)
    {
        TableGrid grid = await Session.GridAsync(BgmTable);
        if (!grid.Ok)
        {
            return (0, "bgm.dat could not be read");
        }
        List<GridColumn> columns = grid.Columns.ToList();
        int path = columns.FindIndex(column => column.Path == "fileName");
        int start = columns.FindIndex(column => column.Path == "loopStart");
        int end = columns.FindIndex(column => column.Path == "loopEnd");
        if (path < 0 || start < 0 || end < 0 || !columns[start].Editable || !columns[end].Editable)
        {
            return (0, "bgm.dat's loop points cannot be patched");
        }
        GridRow? row = grid.Rows.FirstOrDefault(candidate =>
            string.Equals(candidate.Values[path], gamePath, StringComparison.OrdinalIgnoreCase));
        return row is null ? (0, $"bgm.dat has no row for {gamePath}") : (row.Id, null);
    }

    // A replaced track's header loops it whole; bgm.dat's loop samples are
    // set to match through the edits mod. The sentence the status adds.
    private async Task<string> SetWholeLoopAsync(EditsMod mod, string gamePath, long frames)
    {
        (ulong id, string? problem) = await LoopRowAsync(gamePath);
        if (problem is not null)
        {
            return $" {problem}, so the track's loop points in bgm.dat stay the original's and it may loop at the wrong place.";
        }
        string? error = mod.Set(BgmTable, id, "loopStart", "0")
            ?? mod.Set(BgmTable, id, "loopEnd", frames.ToString(CultureInfo.InvariantCulture));
        Edits.NotifyEdited();
        return error is null
            ? $" {mod.Name} now sets the track's loop in bgm.dat to the whole sound. Save the edits mod to keep that."
            : $" The loop points in bgm.dat could not be set ({error}), so they stay the original's.";
    }

    // Takes out the loop samples SetWholeLoopAsync set for a track of frames
    // samples, and leaves any other value alone.
    private async Task<string> ClearWholeLoopAsync(EditsMod mod, string gamePath, long frames)
    {
        (ulong id, string? problem) = await LoopRowAsync(gamePath);
        if (problem is not null || mod.Value(BgmTable, id, "loopStart") != "0" ||
            mod.Value(BgmTable, id, "loopEnd") != frames.ToString(CultureInfo.InvariantCulture))
        {
            return "";
        }
        string? error = mod.Remove(BgmTable, id, "loopStart") ?? mod.Remove(BgmTable, id, "loopEnd");
        Edits.NotifyEdited();
        return error is null
            ? $" The loop points {mod.Name} set for it in bgm.dat are taken out. Save the edits mod to keep that."
            : $" The loop points in bgm.dat could not be taken out: {error}";
    }

    private void Show(string message, bool error = false)
    {
        Status = message;
        StatusIsError = error;
    }

    partial void OnFilterTextChanged(string value) => RefreshLists();

    partial void OnMusicChanged(IReadOnlyList<SoundEntry> value)
    {
        OnPropertyChanged(nameof(MusicTitle));
        OnPropertyChanged(nameof(CanExportAll));
    }

    partial void OnEffectsChanged(IReadOnlyList<SoundEntry> value)
    {
        OnPropertyChanged(nameof(EffectsTitle));
        OnPropertyChanged(nameof(CanExportAll));
    }

    partial void OnListIndexChanged(int value) => OnSelectionChanged();

    partial void OnSelectedMusicChanged(SoundEntry? value) => OnSelectionChanged();

    partial void OnSelectedEffectChanged(SoundEntry? value) => OnSelectionChanged();

    // Another sound picked: what played stops. A pick cleared by the filter
    // leaves it playing.
    private void OnSelectionChanged()
    {
        if (Selected is { } entry && entry != decodedEntry)
        {
            // A decode still running is for the sound picked before.
            ++decodeVersion;
            IsDecoding = false;
            Forget(null);
            PlayerMessage = null;
            followingPlayer = true;
            Position = 0;
            followingPlayer = false;
            ShowLength();
        }
        RaiseSelection();
    }

    // The slider's length from the header until the sound is decoded.
    private void ShowLength()
    {
        if (Selected is { } shown && shown != decodedEntry)
        {
            Duration = shown.Header is { SampleRate: > 0 } header ? (double)header.SamplesPerChannel / header.SampleRate : 0;
        }
    }

    partial void OnIsDecodingChanged(bool value) => RaisePlayState();

    partial void OnIsWorkingChanged(bool value) => RaiseSelection();

    partial void OnPositionChanged(double value)
    {
        OnPropertyChanged(nameof(PositionText));
        if (!followingPlayer && player is not null && decoded is not null && decodedEntry == Selected)
        {
            player.Seek((long)(value * decoded.SampleRate));
        }
    }

    partial void OnDurationChanged(double value) => OnPropertyChanged(nameof(PositionText));

    partial void OnVolumeChanged(double value)
    {
        if (player is not null)
        {
            player.Volume = (float)(value / 100);
        }
    }

    partial void OnLoopOnChanged(bool value)
    {
        if (player is not null && decoded is not null && decodedEntry is { } entry)
        {
            player.Loop = LoopFor(entry, decoded);
        }
    }

    private void RaisePlayState()
    {
        foreach (string name in (string[])[nameof(IsPlaying), nameof(IsPaused), nameof(CanPlay), nameof(CanPause),
                     nameof(CanStop), nameof(PlayLabel)])
        {
            OnPropertyChanged(name);
        }
    }

    private void RaiseSelection()
    {
        foreach (string name in (string[])[nameof(Selected), nameof(HasSelection), nameof(NoSelection), nameof(LoopDetail),
                     nameof(HasEditsMod), nameof(NoEditsMod), nameof(CanReplace), nameof(CanRevert), nameof(CanExport),
                     nameof(CanExportAll), nameof(ReplaceKinds), nameof(ReplacedNote), nameof(HasReplacedNote)])
        {
            OnPropertyChanged(name);
        }
        RaisePlayState();
    }
}
