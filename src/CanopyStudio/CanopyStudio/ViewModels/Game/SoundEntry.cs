using System.Globalization;
using CanopyStudio.Core.Sound;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Game;

// One sound in the Sound section's lists: its path, and what its header
// says once read. The header is read in the background after the list
// shows, so the columns fill in as they come.
public sealed partial class SoundEntry(string path) : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CodecText), nameof(ChannelsText), nameof(LengthText), nameof(LoopText), nameof(Frames))]
    private NlsdHeader? header;

    // Why the header could not be read, null when it was or is not yet.
    [ObservableProperty]
    private string? headerError;

    // True when the edits mod has its own copy of this file.
    [ObservableProperty]
    private bool isReplaced;

    public string Path { get; } = path;
    public bool IsMusic => GameSounds.IsMusic(Path);

    // The path below data/bgm/ or data/se/, which is what the lists show.
    public string Name => Path[(IsMusic ? GameSounds.MusicFolder : GameSounds.EffectsFolder).Length..];

    public long Frames => Header?.SamplesPerChannel ?? 0;

    public string CodecText => Header switch
    {
        null => HeaderError is null ? "" : "Unreadable",
        { IsOgg: true } => "Ogg Vorbis",
        { IsWave: true } => "MS ADPCM",
        { } other => $"Codec {other.Codec}",
    };

    public string ChannelsText => Header switch
    {
        null => "",
        { Channels: 1 } => "Mono",
        { Channels: 2 } => "Stereo",
        { } other => other.Channels.ToString(CultureInfo.CurrentCulture),
    };

    public string LengthText => Header is { SampleRate: > 0 } known
        ? Time(TimeSpan.FromSeconds((double)known.SamplesPerChannel / known.SampleRate))
        : "";

    public string LoopText => Header is null ? "" : Header.Loops ? "Yes" : "No";

    public string ReplacedText => IsReplaced ? "Replaced" : "";

    partial void OnIsReplacedChanged(bool value) => OnPropertyChanged(nameof(ReplacedText));

    partial void OnHeaderErrorChanged(string? value) => OnPropertyChanged(nameof(CodecText));

    // m:ss, with tenths under a minute so short effects tell apart.
    public static string Time(TimeSpan time) => time.TotalMinutes >= 1
        ? $"{(int)time.TotalMinutes}:{time.Seconds:00}"
        : time.TotalSeconds.ToString("0.0 s", CultureInfo.CurrentCulture);
}
