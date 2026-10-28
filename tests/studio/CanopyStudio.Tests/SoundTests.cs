using System.Buffers.Binary;
using System.Diagnostics;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Sound;
using Xunit.Abstractions;

namespace CanopyStudio.Tests;

// The .nlsd header, the Ogg page walk, the MS ADPCM decoder and the SDL
// player, on bytes made here.
public sealed class SoundTests
{
    [Fact]
    public void HeaderWritesAndReadsTheSameFields()
    {
        NlsdHeader header = new(NlsdHeader.CodecOgg, 123456, 48000, 1, 987654, 4000, 90000);
        byte[] bytes = new byte[NlsdHeader.Size];

        header.Write(bytes);

        Assert.Equal(header, NlsdHeader.Read(bytes));
        Assert.Equal(7u, BinaryPrimitives.ReadUInt32LittleEndian(bytes));
        Assert.Equal(48000, BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(8)));
        Assert.Equal(2, header.Channels);
        Assert.True(header.Loops);
        Assert.False((header with { LoopStart = 0, LoopEnd = 123456 }).Loops);
        Assert.Null(NlsdHeader.Read(new byte[23]));
    }

    [Fact]
    public void FileRoundTripsAndRefusesAShortStream()
    {
        byte[] stream = [1, 2, 3, 4, 5];
        NlsdFile file = new(new NlsdHeader(NlsdHeader.CodecWave, 5, 48000, 0, 1, 0, 5), stream);

        (NlsdFile? read, string? error) = NlsdFile.Read(file.ToBytes());

        Assert.Null(error);
        Assert.Equal(file.Header, read!.Header);
        Assert.Equal(stream, read.Stream);
        Assert.NotNull(NlsdFile.Read(file.ToBytes()[..^1]).Error);
    }

    [Fact]
    public void ChainedOggSplitsIntoItsLinksAtTheLoopOffsets()
    {
        byte[] intro = OggLink(1, 2, 48000, 1000);
        byte[] loop = OggLink(2, 2, 48000, 5000);
        byte[] outro = OggLink(3, 2, 48000, 300);
        byte[] ogg = [.. intro, .. loop, .. outro];
        NlsdHeader header = new(NlsdHeader.CodecOgg, (uint)ogg.Length, 48000, 1, 6300, (uint)intro.Length,
            (uint)(intro.Length + loop.Length));

        (OggLoopLinks? links, string? error) = new NlsdFile(header, ogg).SplitLoop();

        Assert.Null(error);
        Assert.Equal(intro, links!.Intro);
        Assert.Equal(loop, links.Loop);
        Assert.Equal(outro, links.Outro);

        (IReadOnlyList<OggPage> pages, string? walkError) = OggPages.Walk(ogg);
        Assert.Null(walkError);
        Assert.Equal(6, pages.Count);
        Assert.Equal([(0, intro.Length), (intro.Length, loop.Length), (intro.Length + loop.Length, outro.Length)], OggPages.Links(pages));
        Assert.Equal([1000L, 5000L, 300L], OggPages.LinkSamples(pages));
    }

    [Fact]
    public void LoopOffsetsThatMissAPageAreRefused()
    {
        byte[] ogg = [.. OggLink(1, 1, 48000, 10), .. OggLink(2, 1, 48000, 10)];
        NlsdHeader header = new(NlsdHeader.CodecOgg, (uint)ogg.Length, 48000, 0, 20, 3, (uint)ogg.Length - 1);

        Assert.NotNull(new NlsdFile(header, ogg).SplitLoop().Error);
        Assert.NotNull(new NlsdFile(header with { LoopStart = 0, LoopEnd = (uint)ogg.Length }, ogg).SplitLoop().Error);
    }

    [Fact]
    public void NewOggGetsAHeaderFromItsStream()
    {
        byte[] ogg = [.. OggLink(9, 1, 48000, 700), .. OggLink(10, 1, 48000, 300)];

        (NlsdFile? file, string? error) = NlsdFile.FromOgg(ogg);

        Assert.Null(error);
        Assert.Equal(new NlsdHeader(NlsdHeader.CodecOgg, (uint)ogg.Length, 48000, 0, 1000, 0, (uint)ogg.Length), file!.Header);
        Assert.False(file.Header.Loops);
        Assert.Equal(ogg, file.Stream);
        Assert.NotNull(NlsdFile.FromOgg([1, 2, 3]).Error);
    }

    [Fact]
    public void MsAdpcmMonoBlockDecodesToKnownSamples()
    {
        // Predictor 0 (256, 0), step 16, then 100 and 50: 50 plays first.
        byte[] block = [0, 16, 0, 100, 0, 50, 0, 0x12, 0x34, 0xF8, 0x70];
        byte[] wave = AdpcmWave(1, block, samplesPerBlock: 10, fact: null);

        (DecodedSound? sound, string? error) = WaveFile.Decode(wave);

        Assert.Null(error);
        Assert.Equal(1, sound!.Channels);
        Assert.Equal(48000, sound.SampleRate);
        Assert.Equal([50, 100, 116, 148, 196, 260, 241, 105, 462, 462], sound.Samples.Select(WaveFile.ToShort).Select(value => (int)value));
    }

    [Fact]
    public void MsAdpcmStereoBlockTakesNibblesInTurnsAndHonoursFact()
    {
        // Left: predictor 1 (512, -256), step 16, 100 then 50. Right:
        // predictor 0, step 20, -10 then 0. One byte: 1 for left, F for right.
        byte[] block = [1, 0, 16, 0, 20, 0, 100, 0, 0xF6, 0xFF, 50, 0, 0, 0, 0x1F];
        byte[] wave = AdpcmWave(2, block, samplesPerBlock: 3, fact: 3);

        (DecodedSound? sound, string? error) = WaveFile.Decode(wave);

        Assert.Null(error);
        Assert.Equal(3, sound!.Frames);
        Assert.Equal([50, 0, 100, -10, 166, -30], sound.Samples.Select(WaveFile.ToShort).Select(value => (int)value));
    }

    [Fact]
    public void Pcm16WaveRoundTripsThroughTheWriter()
    {
        float[] samples = [0f, 0.5f, -0.5f, 32767f / 32768f, -1f, 0.25f];
        DecodedSound sound = new(48000, 2, samples, [3]);

        byte[] wave = WaveFile.WritePcm16(sound);
        (DecodedSound? read, string? error) = WaveFile.Decode(wave);
        (NlsdFile? file, string? wrapError) = NlsdFile.FromWave(wave);

        Assert.Null(error);
        Assert.Equal(samples, read!.Samples);
        Assert.Null(wrapError);
        Assert.Equal(new NlsdHeader(NlsdHeader.CodecWave, (uint)wave.Length, 48000, 1, 3, 0, (uint)wave.Length), file!.Header);
    }

    [Fact]
    public void ReplacementIsCheckedAgainstTheOriginal()
    {
        NlsdHeader stereo = new(NlsdHeader.CodecOgg, 10, 48000, 1, 10, 0, 10);
        byte[] good = WaveFile.WritePcm16(new DecodedSound(48000, 2, [0f, 0.5f], [1]));

        // A WAV is refused for music and effects alike, until the game is
        // seen playing a plain PCM one.
        Assert.Contains("Ogg", GameSounds.Replacement(good, "new.wav", music: true, stereo).Error);
        Assert.Contains("Ogg", GameSounds.Replacement(good, "new.wav", music: false, stereo).Error);
        Assert.NotNull(GameSounds.Replacement([1, 2, 3], "new.ogg", music: true, stereo).Error);
    }

    [Fact]
    public void ExportKeepsOggAndTurnsAdpcmIntoPcm()
    {
        byte[] ogg = OggLink(1, 2, 48000, 10);
        NlsdFile oggFile = new(new NlsdHeader(NlsdHeader.CodecOgg, (uint)ogg.Length, 48000, 1, 10, 0, (uint)ogg.Length), ogg);
        byte[] adpcm = AdpcmWave(1, [0, 16, 0, 100, 0, 50, 0, 0x12, 0x34, 0xF8, 0x70], 10, null);
        NlsdFile waveFile = new(new NlsdHeader(NlsdHeader.CodecWave, (uint)adpcm.Length, 48000, 0, 10, 0, (uint)adpcm.Length), adpcm);

        SoundExport oggExport = GameSounds.Export(oggFile).Export!;
        SoundExport waveExport = GameSounds.Export(waveFile).Export!;

        Assert.Equal(ogg, oggExport.Bytes);
        Assert.Equal("x.ogg", GameSounds.ExportName("data/bgm/x.nlsd", oggExport.Extension));
        Assert.Equal(".wav", waveExport.Extension);
        WaveInfo info = WaveFile.Inspect(waveExport.Bytes).Info!;
        Assert.Equal((WaveFile.FormatPcm, 16, 20), (info.FormatTag, info.BitsPerSample, info.DataLength));
    }

    [Fact]
    public void ReplacementIsWrittenAtTheGamePathFoundAndRemovedWithItsFolders()
    {
        string mod = Path.Combine(Path.GetTempPath(), "canopy_studio_sound_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(mod);
            byte[] wave = WaveFile.WritePcm16(new DecodedSound(48000, 1, [0f, 0.5f], [2]));
            NlsdFile file = NlsdFile.FromWave(wave).File!;
            string path = GameSounds.OverridePath(mod, "data/se/se0001.nlsd");

            Assert.Null(GameSounds.Write(path, file));

            Assert.Equal(file.ToBytes(), File.ReadAllBytes(path));
            Assert.Contains("data/se/se0001.nlsd", GameSounds.Replaced(mod));
            Assert.Null(GameSounds.Remove(mod, "data/se/se0001.nlsd"));
            Assert.Empty(GameSounds.Replaced(mod));
            Assert.False(Directory.Exists(Path.Combine(mod, "data")));
            Assert.True(Directory.Exists(mod));
        }
        finally
        {
            Directory.Delete(mod, recursive: true);
        }
    }

    [Fact]
    public void SdlPlayerPlaysAShortSoundToTheEndOnTheDummyDriver()
    {
        Environment.SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        using SdlSoundPlayer player = new("dummy");
        DecodedSound sound = new(48000, 1, new float[4800], [4800]);

        string? problem = player.Play(sound);

        Assert.Null(problem);
        Assert.True(player.IsPlaying);
        Stopwatch clock = Stopwatch.StartNew();
        while (player.IsPlaying && clock.Elapsed < TimeSpan.FromSeconds(10))
        {
            Thread.Sleep(10);
        }
        Assert.False(player.IsPlaying);
        Assert.Equal(sound.Frames, player.Position);
    }

    [Fact]
    public void SdlPlayerLoopsAndPausesOnTheDummyDriver()
    {
        using SdlSoundPlayer player = new("dummy");
        DecodedSound sound = new(48000, 2, new float[4800 * 2], [4800]);

        Assert.Null(player.Play(sound, new SoundLoop(1000, 2000)));
        Thread.Sleep(300);
        Assert.True(player.IsPlaying);
        Assert.InRange(player.Position, 0, 2000);
        player.Pause();
        Assert.True(player.IsPaused);
        player.Loop = null;
        player.Seek(4000);
        player.Resume();
        Stopwatch clock = Stopwatch.StartNew();
        while (player.IsPlaying && clock.Elapsed < TimeSpan.FromSeconds(10))
        {
            Thread.Sleep(10);
        }
        Assert.Equal(4800, player.Position);
        player.Stop();
        Assert.Equal(0, player.Position);
    }

    // One logical Ogg stream of two pages: a Vorbis identification header,
    // then a page ending at granule samples. The CRC is left 0: the page
    // walk does not check it, and nothing here decodes audio.
    internal static byte[] OggLink(uint serial, int channels, int rate, long samples)
    {
        byte[] id = new byte[30];
        id[0] = 1;
        "vorbis"u8.CopyTo(id.AsSpan(1));
        id[11] = (byte)channels;
        BinaryPrimitives.WriteUInt32LittleEndian(id.AsSpan(12), (uint)rate);
        id[29] = 1;
        return [.. OggPage(serial, 0, OggPageFlags.First, 0, id), .. OggPage(serial, 1, OggPageFlags.Last, samples, [7, 7, 7])];
    }

    private static class OggPageFlags
    {
        public const byte First = 0x02;
        public const byte Last = 0x04;
    }

    private static byte[] OggPage(uint serial, uint sequence, byte flags, long granule, byte[] body)
    {
        byte[] page = new byte[27 + 1 + body.Length];
        "OggS"u8.CopyTo(page);
        page[5] = flags;
        BinaryPrimitives.WriteInt64LittleEndian(page.AsSpan(6), granule);
        BinaryPrimitives.WriteUInt32LittleEndian(page.AsSpan(14), serial);
        BinaryPrimitives.WriteUInt32LittleEndian(page.AsSpan(18), sequence);
        page[26] = 1;
        page[27] = (byte)body.Length;
        body.CopyTo(page, 28);
        return page;
    }

    // A RIFF WAVE of MS ADPCM with the seven standard coefficient pairs and
    // one block.
    private static byte[] AdpcmWave(int channels, byte[] block, int samplesPerBlock, uint? fact)
    {
        short[] coefficients = [256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232];
        using MemoryStream stream = new();
        using BinaryWriter writer = new(stream);
        writer.Write("RIFF"u8);
        writer.Write(0u);
        writer.Write("WAVE"u8);
        writer.Write("fmt "u8);
        writer.Write((uint)(20 + 2 + coefficients.Length * 2));
        writer.Write((ushort)WaveFile.FormatMsAdpcm);
        writer.Write((ushort)channels);
        writer.Write(48000u);
        writer.Write(48000u);
        writer.Write((ushort)block.Length);
        writer.Write((ushort)4);
        writer.Write((ushort)(4 + coefficients.Length * 2));
        writer.Write((ushort)samplesPerBlock);
        writer.Write((ushort)(coefficients.Length / 2));
        foreach (short value in coefficients)
        {
            writer.Write(value);
        }
        if (fact is { } samples)
        {
            writer.Write("fact"u8);
            writer.Write(4u);
            writer.Write(samples);
        }
        writer.Write("data"u8);
        writer.Write((uint)block.Length);
        writer.Write(block);
        if ((block.Length & 1) != 0)
        {
            writer.Write((byte)0);
        }
        writer.Flush();
        byte[] bytes = stream.ToArray();
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), (uint)(bytes.Length - 8));
        return bytes;
    }
}

// Every sound the installed game ships, read only. Skips like NativeTests;
// prints paths, counts and times only, never a name or title.
public sealed class SoundGameTests(ITestOutputHelper output)
{
    private const string DefaultGameFolder = @"D:\SteamLibrary\steamapps\common\Village in the Shade";

    private bool Ready(out string folder, out IReadOnlyList<string> sounds)
    {
        folder = "";
        sounds = [];
        if (CanopyCore.Check() is { } problem)
        {
            output.WriteLine($"Skipped: {problem}");
            return false;
        }
        string found = Environment.GetEnvironmentVariable("CANOPY_GAME_DIR") is { Length: > 0 } dir ? dir : DefaultGameFolder;
        if (!Directory.Exists(found))
        {
            output.WriteLine("Skipped: the game is not installed here (set CANOPY_GAME_DIR).");
            return false;
        }
        FileList list = CanopyCore.Files(found);
        Assert.True(list.Ok, list.Error);
        folder = found;
        sounds = list.Files.Select(file => file.Path).Where(GameSounds.IsSound).Order(StringComparer.Ordinal).ToList();
        return true;
    }

    [Fact]
    public void EverySoundHeaderReadsAndMatchesItsStream()
    {
        if (!Ready(out string folder, out IReadOnlyList<string> sounds))
        {
            return;
        }
        int ogg = 0, wave = 0, looping = 0;
        foreach (string path in sounds)
        {
            (NlsdFile? file, string? error) = GameSounds.Read(folder, path);
            Assert.True(file is not null, $"{path}: {error}");
            NlsdHeader header = file!.Header;
            Assert.Equal(GameSounds.GameRate, header.SampleRate);
            if (header.IsOgg)
            {
                ++ogg;
                (IReadOnlyList<OggPage> pages, string? walk) = OggPages.Walk(file.Stream);
                Assert.True(walk is null, $"{path}: {walk}");
                Assert.Equal(header.Channels, OggPages.Identify(file.Stream, pages)!.Channels);
                IReadOnlyList<long> links = OggPages.LinkSamples(pages);
                Assert.Equal(header.SamplesPerChannel, links.Sum());
                if (header.Loops)
                {
                    ++looping;
                    (OggLoopLinks? split, string? why) = file.SplitLoop();
                    Assert.True(split is not null, $"{path}: {why}");
                    Assert.Equal(3, links.Count);
                }
                else
                {
                    Assert.Equal((0u, header.DataSize), (header.LoopStart, header.LoopEnd));
                }
            }
            else
            {
                Assert.True(header.IsWave, $"{path}: codec {header.Codec}");
                ++wave;
                WaveInfo info = WaveFile.Inspect(file.Stream).Info!;
                Assert.Equal(header.Channels, info.Channels);
                Assert.Equal(WaveFile.FormatMsAdpcm, info.FormatTag);
                Assert.Null(info.FactSamples);
                // The blocks hold the header's length rounded to the
                // nearest whole block.
                long blocks = (long)info.DataLength / info.BlockAlign;
                Assert.Equal(0, info.DataLength % info.BlockAlign);
                Assert.InRange(header.SamplesPerChannel - blocks * info.SamplesPerBlock, -info.SamplesPerBlock / 2, info.SamplesPerBlock / 2);
                Assert.Equal(blocks * info.SamplesPerBlock, WaveFile.Decode(file.Stream).Sound!.Frames);
            }
        }
        output.WriteLine($"{sounds.Count} sounds: {sounds.Count(GameSounds.IsMusic)} music, {sounds.Count(GameSounds.IsEffect)} effects; " +
            $"{ogg} Ogg ({looping} looping), {wave} MS ADPCM");
        Assert.True(sounds.Count > 0);
    }

    [Fact]
    public void MusicLinksMatchTheLoopSamplesInBgmTable()
    {
        if (!Ready(out string folder, out _))
        {
            return;
        }
        TableGrid grid = CanopyCore.Grid(folder, "bgm.dat");
        Assert.True(grid.Ok, grid.Error);
        int path = grid.Columns.ToList().FindIndex(column => column.Path == "fileName");
        int start = grid.Columns.ToList().FindIndex(column => column.Path == "loopStart");
        int end = grid.Columns.ToList().FindIndex(column => column.Path == "loopEnd");
        Assert.True(path >= 0 && start >= 0 && end >= 0);
        int looping = 0;
        foreach (GridRow row in grid.Rows)
        {
            NlsdFile file = GameSounds.Read(folder, row.Values[path]!).File!;
            if (!file.Header.Loops)
            {
                continue;
            }
            ++looping;
            IReadOnlyList<long> links = OggPages.LinkSamples(OggPages.Walk(file.Stream).Pages);
            Assert.Equal(links[0], long.Parse(row.Values[start]!, System.Globalization.CultureInfo.InvariantCulture));
            Assert.Equal(links[0] + links[1], long.Parse(row.Values[end]!, System.Globalization.CultureInfo.InvariantCulture));
        }
        output.WriteLine($"{grid.Rows.Count} rows, {looping} with loop samples that match their links; loopStart editable " +
            $"{grid.Columns[start].Editable}, loopEnd editable {grid.Columns[end].Editable}");
    }

    [Fact]
    public void FirstAndLastOfEachCodecDecodeToTheHeaderLength()
    {
        if (!Ready(out string folder, out IReadOnlyList<string> sounds))
        {
            return;
        }
        foreach (string prefix in (string[])[GameSounds.MusicFolder, GameSounds.EffectsFolder])
        {
            List<(string Path, NlsdFile File)> files = sounds.Where(path => path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                .Select(path => (path, GameSounds.Read(folder, path).File!)).ToList();
            foreach (IGrouping<uint, (string Path, NlsdFile File)> codec in files.GroupBy(entry => entry.File.Header.Codec))
            {
                foreach ((string path, NlsdFile file) in (IEnumerable<(string, NlsdFile)>)[codec.First(), codec.Last()])
                {
                    Stopwatch clock = Stopwatch.StartNew();
                    (DecodedSound? sound, string? error) = SoundDecoder.Decode(file);
                    Assert.True(sound is not null, $"{path}: {error}");
                    Assert.Equal(file.Header.SamplesPerChannel, sound!.Frames);
                    Assert.Equal(file.Header.Channels, sound.Channels);
                    output.WriteLine($"{path}: codec {file.Header.Codec}, {sound.Frames} frames, {sound.Links.Count} links, " +
                        $"decoded in {clock.ElapsedMilliseconds} ms");
                }
            }
        }
    }

    [Fact]
    public void LongestMusicAndEveryEffectDecode()
    {
        if (!Ready(out string folder, out IReadOnlyList<string> sounds))
        {
            return;
        }
        (string path, NlsdFile file) = sounds.Where(GameSounds.IsMusic).Select(path => (path, GameSounds.Read(folder, path).File!))
            .MaxBy(entry => entry.Item2.Header.SamplesPerChannel);
        Stopwatch clock = Stopwatch.StartNew();
        (DecodedSound? music, string? error) = SoundDecoder.Decode(file);
        long musicTime = clock.ElapsedMilliseconds;
        Assert.True(music is not null, $"{path}: {error}");
        Assert.Equal(file.Header.SamplesPerChannel, music!.Frames);
        if (file.Header.Loops)
        {
            (OggLoopLinks? links, _) = file.SplitLoop();
            Assert.Equal(OggPages.LinkSamples(OggPages.Walk(links!.Intro).Pages)[0], music.Links[0]);
        }
        output.WriteLine($"Longest music {path}: {music.Length.TotalSeconds:F1} s, {music.Links.Count} links, decoded in {musicTime} ms");

        clock.Restart();
        int effects = 0;
        long reading = 0;
        foreach (string effect in sounds.Where(GameSounds.IsEffect))
        {
            Stopwatch read = Stopwatch.StartNew();
            NlsdFile sound = GameSounds.Read(folder, effect).File!;
            reading += read.ElapsedMilliseconds;
            (DecodedSound? decoded, string? why) = SoundDecoder.Decode(sound);
            Assert.True(decoded is not null, $"{effect}: {why}");
            Assert.Equal(sound.Header.SamplesPerChannel, decoded!.Frames);
            ++effects;
        }
        output.WriteLine($"{effects} effects read and decoded in {clock.ElapsedMilliseconds} ms ({reading} ms of it reading)");
    }
}
