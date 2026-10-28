using CanopyStudio.Core.Native;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Sound;

// A sound ready to write somewhere: the bytes and the file extension they
// take.
public sealed record SoundExport(byte[] Bytes, string Extension);

// The game's sounds as files: where they live, what to export them as, and
// a replacement checked and wrapped for the edits mod. A mod's data folder
// replaces the game's file at the same path (docs/MODDING.md).
public static class GameSounds
{
    public const string MusicFolder = "data/bgm/";
    public const string EffectsFolder = "data/se/";
    public const string Extension = ".nlsd";

    // The rate of every sound the game ships; a replacement must match it.
    public const int GameRate = 48000;

    public static bool IsSound(string path) =>
        path.EndsWith(Extension, StringComparison.OrdinalIgnoreCase) && (IsMusic(path) || IsEffect(path));

    public static bool IsMusic(string path) => path.StartsWith(MusicFolder, StringComparison.OrdinalIgnoreCase);

    public static bool IsEffect(string path) => path.StartsWith(EffectsFolder, StringComparison.OrdinalIgnoreCase);

    // The .nlsd at gamePath, as the game reads it.
    public static (NlsdFile? File, string? Error) Read(string gameFolder, string gamePath)
    {
        byte[]? bytes = CanopyCore.File(gameFolder, gamePath, out string? error);
        if (bytes is null)
        {
            return (null, error ?? $"{gamePath} could not be read.");
        }
        (NlsdFile? file, string? why) = NlsdFile.Read(bytes);
        return file is null ? (null, $"{gamePath} is not a sound file Canopy Studio can read: {why}") : (file, null);
    }

    // Where a mod's copy of gamePath goes, below the mod folder.
    public static string OverridePath(string modFolder, string gamePath) => GameFiles.Find(modFolder, gamePath);

    // The extension an export of a sound with header takes, null for a codec
    // Canopy Studio cannot export.
    public static string? ExportExtension(NlsdHeader header) => header.IsOgg ? ".ogg" : header.IsWave ? ".wav" : null;

    // The file name an export of gamePath takes, with extension.
    public static string ExportName(string gamePath, string extension)
    {
        string name = gamePath[(gamePath.LastIndexOf('/') + 1)..];
        if (name.EndsWith(Extension, StringComparison.OrdinalIgnoreCase))
        {
            name = name[..^Extension.Length];
        }
        return name + extension;
    }

    // What an export writes: an Ogg sound as its Ogg stream, a looping one
    // still chained as the game has it; a WAV sound as 16-bit PCM, decoded
    // from MS ADPCM when it is that.
    public static (SoundExport? Export, string? Error) Export(NlsdFile file)
    {
        if (file.Header.IsOgg)
        {
            int start = file.Stream.AsSpan().IndexOf("OggS"u8);
            return start < 0
                ? (null, "The sound holds no Ogg stream.")
                : (new SoundExport(file.Stream[start..], ".ogg"), null);
        }
        if (file.Header.IsWave)
        {
            (WaveInfo? info, string? error) = WaveFile.Inspect(file.Stream);
            if (info is null)
            {
                return (null, error);
            }
            if (info.FormatTag == WaveFile.FormatPcm && info.BitsPerSample == 16)
            {
                return (new SoundExport(file.Stream, ".wav"), null);
            }
            (DecodedSound? sound, string? why) = SoundDecoder.Decode(file);
            return sound is null ? (null, why) : (new SoundExport(WaveFile.WritePcm16(sound), ".wav"), null);
        }
        return (null, $"The sound is in codec {file.Header.Codec}, which Canopy Studio cannot export.");
    }

    // A new file for a sound whose original is the .nlsd original: an Ogg
    // Vorbis stream, or for a sound effect a WAV file of 16-bit PCM, at
    // 48,000 Hz with the original's channels, decoded once to be sure it
    // plays. Nothing is encoded. The .nlsd and the decoded sound, else why
    // not.
    public static (NlsdFile? File, DecodedSound? Sound, string? Error) Replacement(byte[] bytes, string fileName, bool music,
        NlsdHeader original)
    {
        // The game's WAV sounds are all MS ADPCM, and whether it plays one of
        // plain PCM is not known yet, so every replacement is Ogg Vorbis.
        bool wave = fileName.EndsWith(".wav", StringComparison.OrdinalIgnoreCase);
        if (wave)
        {
            return (null, null, "A replacement must be an Ogg Vorbis file. Convert the WAV file to .ogg at 48,000 Hz first.");
        }
        (NlsdFile? file, string? error) = wave ? NlsdFile.FromWave(bytes) : NlsdFile.FromOgg(bytes);
        if (file is null)
        {
            return (null, null, wave ? error : $"The file is not an Ogg Vorbis stream: {error}");
        }
        if (file.Header.SampleRate != GameRate)
        {
            return (null, null, $"The file plays at {file.Header.SampleRate:N0} Hz, but the game's sounds are {GameRate:N0} Hz. Resample it to {GameRate:N0} Hz first.");
        }
        if (file.Header.Channels != original.Channels)
        {
            return (null, null, $"The file has {Channels(file.Header.Channels)}, but the original has {Channels(original.Channels)}. Make it {Channels(original.Channels)} first.");
        }
        (DecodedSound? sound, string? why) = SoundDecoder.Decode(file);
        if (sound is null)
        {
            return (null, null, $"The file could not be decoded: {why}");
        }
        if (sound.Frames == 0)
        {
            return (null, null, "The file holds no audio.");
        }
        return (file, sound, null);
    }

    // Writes file at path, through a temporary file beside it, so a failed
    // write leaves no half file. Null when written, else why not.
    public static string? Write(string path, NlsdFile file)
    {
        string temporary = path + ".canopy-new";
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(temporary, file.ToBytes());
            File.Move(temporary, path, overwrite: true);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            try
            {
                File.Delete(temporary);
            }
            catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException)
            {
            }
            return $"{path} could not be written: {exception.Message}";
        }
    }

    // The game paths of every sound modFolder has its own copy of, in
    // lowercase, as the loader matches them.
    public static HashSet<string> Replaced(string modFolder)
    {
        HashSet<string> found = new(StringComparer.OrdinalIgnoreCase);
        foreach (string folder in (string[])[MusicFolder, EffectsFolder])
        {
            string path = GameFiles.Find(modFolder, folder);
            if (!Directory.Exists(path))
            {
                continue;
            }
            try
            {
                foreach (string file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
                {
                    string relative = Path.GetRelativePath(path, file).Replace(Path.DirectorySeparatorChar, '/');
                    if (relative.EndsWith(Extension, StringComparison.OrdinalIgnoreCase))
                    {
                        found.Add((folder + relative).ToLowerInvariant());
                    }
                }
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
            }
        }
        return found;
    }

    // Deletes modFolder's copy of gamePath, and the folders it leaves empty
    // up to the mod folder. Null when gone, else why not.
    public static string? Remove(string modFolder, string gamePath)
    {
        string path = OverridePath(modFolder, gamePath);
        try
        {
            File.Delete(path);
            string top = Path.GetFullPath(modFolder).TrimEnd(Path.DirectorySeparatorChar);
            for (string? folder = Path.GetDirectoryName(Path.GetFullPath(path));
                 folder is not null && folder.Length > top.Length && !Directory.EnumerateFileSystemEntries(folder).Any();
                 folder = Path.GetDirectoryName(folder))
            {
                Directory.Delete(folder);
            }
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"{path} could not be deleted: {exception.Message}";
        }
    }

    public static string Channels(int channels) => channels == 1 ? "1 channel (mono)" : $"{channels} channels";
}
