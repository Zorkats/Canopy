using System.Buffers.Binary;

namespace CanopyStudio.Core.Sound;

// The 24-byte header every .nlsd starts with, field for field as
// src/CanopyCore/game_assets.h documents it. All numbers are little endian.
//   +0   Codec              7 for Ogg Vorbis, 5 for a RIFF WAVE.
//   +4   DataSize           the bytes after the header.
//   +8   SampleRate         48000 in every file the game ships.
//   +10  ChannelCode        0 for mono, 1 for stereo.
//   +12  SamplesPerChannel  the whole sound's length.
//   +16  LoopStart          where the loop part starts, a byte offset into
//                           the stream after the header.
//   +20  LoopEnd            where it ends, the same way; 0 and DataSize when
//                           the sound does not loop.
public sealed record NlsdHeader(uint Codec, uint DataSize, ushort SampleRate, ushort ChannelCode,
    uint SamplesPerChannel, uint LoopStart, uint LoopEnd)
{
    public const int Size = 24;
    public const uint CodecOgg = 7;
    public const uint CodecWave = 5;

    public int Channels => ChannelCode + 1;
    public bool IsOgg => Codec == CodecOgg;
    public bool IsWave => Codec == CodecWave;

    // True when the stream has a loop part apart from the whole: an intro,
    // a loop and an outro.
    public bool Loops => LoopEnd > LoopStart && (LoopStart != 0 || LoopEnd != DataSize);

    // The header at the start of bytes, or null when there are not 24.
    public static NlsdHeader? Read(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length < Size)
        {
            return null;
        }
        return new NlsdHeader(
            BinaryPrimitives.ReadUInt32LittleEndian(bytes),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[4..]),
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[8..]),
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[10..]),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]));
    }

    public void Write(Span<byte> bytes)
    {
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, Codec);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes[4..], DataSize);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes[8..], SampleRate);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes[10..], ChannelCode);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes[12..], SamplesPerChannel);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes[16..], LoopStart);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes[20..], LoopEnd);
    }
}

// The three links of a looping Ogg sound, each a whole Ogg Vorbis stream.
public sealed record OggLoopLinks(byte[] Intro, byte[] Loop, byte[] Outro);

// A whole .nlsd: its header and the stream after it.
public sealed record NlsdFile(NlsdHeader Header, byte[] Stream)
{
    // The file in bytes, or null with why not.
    public static (NlsdFile? File, string? Error) Read(byte[] bytes)
    {
        if (NlsdHeader.Read(bytes) is not { } header)
        {
            return (null, $"The file is {bytes.Length} bytes, shorter than the {NlsdHeader.Size}-byte sound header.");
        }
        if (header.DataSize > bytes.Length - NlsdHeader.Size)
        {
            return (null, $"The header says {header.DataSize} bytes follow it, but only {bytes.Length - NlsdHeader.Size} do.");
        }
        return (new NlsdFile(header, bytes[NlsdHeader.Size..(NlsdHeader.Size + (int)header.DataSize)]), null);
    }

    public byte[] ToBytes()
    {
        byte[] bytes = new byte[NlsdHeader.Size + Stream.Length];
        Header.Write(bytes);
        Stream.CopyTo(bytes, NlsdHeader.Size);
        return bytes;
    }

    // A looping Ogg sound cut at the header's loop offsets into its intro,
    // loop and outro; null with why not for any other sound.
    public (OggLoopLinks? Links, string? Error) SplitLoop()
    {
        if (!Header.IsOgg)
        {
            return (null, "Only an Ogg Vorbis sound has links to split.");
        }
        if (!Header.Loops)
        {
            return (null, "The sound does not loop, so it is one link.");
        }
        if (Header.LoopEnd > Stream.Length)
        {
            return (null, $"The loop ends at byte {Header.LoopEnd}, past the {Stream.Length}-byte stream.");
        }
        int start = (int)Header.LoopStart;
        int end = (int)Header.LoopEnd;
        foreach (int at in (int[])[0, start, end])
        {
            if (at < Stream.Length && !OggPages.StartsPage(Stream, at))
            {
                return (null, $"No Ogg page starts at byte {at} of the stream, where a link should.");
            }
        }
        return (new OggLoopLinks(Stream[..start], Stream[start..end], Stream[end..]), null);
    }

    // An .nlsd around a whole Ogg Vorbis stream: codec 7, its rate and
    // channels from the identification header, its samples per channel from
    // the last granule position of each link, and loop offsets 0 and the
    // stream size, as for a sound that does not loop.
    public static (NlsdFile? File, string? Error) FromOgg(byte[] ogg)
    {
        (IReadOnlyList<OggPage> pages, string? error) = OggPages.Walk(ogg);
        if (error is not null)
        {
            return (null, error);
        }
        if (OggPages.Identify(ogg, pages) is not { } id)
        {
            return (null, "The file is an Ogg stream, but not of Vorbis audio.");
        }
        if (id.Channels is < 1 or > 2)
        {
            return (null, $"The stream has {id.Channels} channels; a game sound has 1 or 2.");
        }
        if (id.SampleRate > ushort.MaxValue)
        {
            return (null, $"The stream's rate of {id.SampleRate} Hz does not fit the sound header.");
        }
        long samples = OggPages.LinkSamples(pages).Sum();
        if (samples is <= 0 or > uint.MaxValue)
        {
            return (null, "The stream does not say how long it is.");
        }
        NlsdHeader header = new(NlsdHeader.CodecOgg, (uint)ogg.Length, (ushort)id.SampleRate, (ushort)(id.Channels - 1),
            (uint)samples, 0, (uint)ogg.Length);
        return (new NlsdFile(header, ogg), null);
    }

    // An .nlsd around a RIFF WAVE of 16-bit PCM: codec 5 with the file's
    // rate, channels and length, not looping.
    public static (NlsdFile? File, string? Error) FromWave(byte[] wave)
    {
        (WaveInfo? info, string? error) = WaveFile.Inspect(wave);
        if (info is null)
        {
            return (null, error);
        }
        if (info.FormatTag != WaveFile.FormatPcm || info.BitsPerSample != 16)
        {
            return (null, "Only a WAV file of 16-bit PCM can be taken.");
        }
        if (info.Channels is < 1 or > 2 || info.SampleRate > ushort.MaxValue)
        {
            return (null, $"The WAV file has {info.Channels} channels at {info.SampleRate} Hz, which the sound header cannot hold.");
        }
        long samples = info.DataLength / (2L * info.Channels);
        NlsdHeader header = new(NlsdHeader.CodecWave, (uint)wave.Length, (ushort)info.SampleRate,
            (ushort)(info.Channels - 1), (uint)samples, 0, (uint)wave.Length);
        return (new NlsdFile(header, wave), null);
    }
}

// One Ogg page: where it starts in the stream, its whole length, its header
// type flags, granule position and serial number, and where its body is.
public readonly record struct OggPage(int Offset, int Length, byte Flags, long Granule, uint Serial, int BodyOffset, int BodyLength)
{
    public const byte Continued = 0x01;
    public const byte First = 0x02;
    public const byte Last = 0x04;

    public bool StartsStream => (Flags & First) != 0;
}

// What a Vorbis identification header says.
public sealed record VorbisIdentification(int Channels, int SampleRate);

// A walk over Ogg pages, without decoding any audio. A chained stream is
// several logical streams one after another; each link starts with a page
// that has the first-page flag.
public static class OggPages
{
    public const int HeaderSize = 27;

    // True when a whole page header starts at offset.
    public static bool StartsPage(ReadOnlySpan<byte> data, int offset) =>
        offset >= 0 && data.Length - offset >= HeaderSize && data.Slice(offset, 4).SequenceEqual("OggS"u8) && data[offset + 4] == 0;

    // Every page in data, which must be pages from its first byte to its
    // last; else why not.
    public static (IReadOnlyList<OggPage> Pages, string? Error) Walk(ReadOnlySpan<byte> data)
    {
        List<OggPage> pages = [];
        int at = 0;
        while (at < data.Length)
        {
            if (!StartsPage(data, at))
            {
                return (pages, $"Byte {at} of {data.Length} does not start an Ogg page.");
            }
            int segments = data[at + 26];
            if (data.Length - at < HeaderSize + segments)
            {
                return (pages, $"The Ogg page at byte {at} is cut short.");
            }
            int body = 0;
            for (int index = 0; index < segments; ++index)
            {
                body += data[at + HeaderSize + index];
            }
            int length = HeaderSize + segments + body;
            if (length > data.Length - at)
            {
                return (pages, $"The Ogg page at byte {at} is cut short.");
            }
            pages.Add(new OggPage(at, length, data[at + 5], BinaryPrimitives.ReadInt64LittleEndian(data[(at + 6)..]),
                BinaryPrimitives.ReadUInt32LittleEndian(data[(at + 14)..]), at + HeaderSize + segments, body));
            at += length;
        }
        if (pages.Count == 0)
        {
            return (pages, "The file holds no Ogg pages.");
        }
        return (pages, null);
    }

    // The byte range of each link: a new one starts at every first page.
    public static IReadOnlyList<(int Offset, int Length)> Links(IReadOnlyList<OggPage> pages)
    {
        List<(int, int)> links = [];
        int start = -1;
        int end = 0;
        foreach (OggPage page in pages)
        {
            if (page.StartsStream && start >= 0)
            {
                links.Add((start, end - start));
                start = -1;
            }
            if (start < 0)
            {
                start = page.Offset;
            }
            end = page.Offset + page.Length;
        }
        if (start >= 0)
        {
            links.Add((start, end - start));
        }
        return links;
    }

    // The samples per channel of each link: the granule position of its
    // last page that has one, as a stream that starts at sample 0 ends.
    public static IReadOnlyList<long> LinkSamples(IReadOnlyList<OggPage> pages)
    {
        List<long> samples = [];
        long last = 0;
        bool any = false;
        foreach (OggPage page in pages)
        {
            if (page.StartsStream && any)
            {
                samples.Add(last);
                last = 0;
            }
            any = true;
            if (page.Granule >= 0)
            {
                last = page.Granule;
            }
        }
        if (any)
        {
            samples.Add(last);
        }
        return samples;
    }

    // The identification header in the first page's body, or null when the
    // stream is not Vorbis.
    public static VorbisIdentification? Identify(ReadOnlySpan<byte> data, IReadOnlyList<OggPage> pages)
    {
        if (pages.Count == 0 || !pages[0].StartsStream || pages[0].BodyLength < 16)
        {
            return null;
        }
        ReadOnlySpan<byte> body = data.Slice(pages[0].BodyOffset, pages[0].BodyLength);
        if (body[0] != 1 || !body.Slice(1, 6).SequenceEqual("vorbis"u8))
        {
            return null;
        }
        return new VorbisIdentification(body[11], (int)BinaryPrimitives.ReadUInt32LittleEndian(body[12..]));
    }
}
