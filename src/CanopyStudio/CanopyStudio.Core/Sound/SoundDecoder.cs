using NVorbis;

namespace CanopyStudio.Core.Sound;

// A sound as samples: Samples holds Frames x Channels floats from -1 to 1,
// channels interleaved. Links is the frames of each link of a chained Ogg
// stream in order, or the one length of any other sound.
public sealed record DecodedSound(int SampleRate, int Channels, float[] Samples, IReadOnlyList<long> Links)
{
    public long Frames => Samples.Length / Channels;
    public TimeSpan Length => TimeSpan.FromSeconds((double)Frames / SampleRate);
}

// Sounds into samples, without playing them. Pure: bytes in, samples out.
public static class SoundDecoder
{
    // A WAV sound's decoded length is fitted to its header's only this close;
    // past it the header is not about these blocks, and the blocks win.
    private const long MaxFit = 4096;

    // The sound in an .nlsd: Ogg Vorbis for codec 7, a RIFF WAVE for codec 5.
    public static (DecodedSound? Sound, string? Error) Decode(NlsdFile file)
    {
        if (file.Header.IsOgg)
        {
            return DecodeOgg(file.Stream);
        }
        if (file.Header.IsWave)
        {
            // MS ADPCM holds whole blocks, and the game's files carry no fact
            // chunk. Their header gives the source's length, which the
            // blocks round to the nearest block, within 64 frames either
            // way. The header's length is the sound's: a block's padding is
            // cut, and frames the blocks dropped are silence.
            (DecodedSound? sound, string? error) = WaveFile.Decode(file.Stream);
            long frames = file.Header.SamplesPerChannel;
            if (sound is not null && frames > 0 && frames != sound.Frames && Math.Abs(frames - sound.Frames) <= MaxFit)
            {
                float[] samples = sound.Samples;
                Array.Resize(ref samples, (int)(frames * sound.Channels));
                sound = sound with { Samples = samples, Links = [frames] };
            }
            return (sound, error);
        }
        return (null, $"The sound is in codec {file.Header.Codec}, which Canopy Studio does not decode.");
    }

    // An Ogg Vorbis stream, every link of a chained one in order. Each link
    // is decoded on its own, so the length of each is known.
    public static (DecodedSound? Sound, string? Error) DecodeOgg(byte[] ogg)
    {
        (IReadOnlyList<OggPage> pages, string? error) = OggPages.Walk(ogg);
        if (error is not null)
        {
            return (null, error);
        }
        IReadOnlyList<(int Offset, int Length)> links = OggPages.Links(pages);
        IReadOnlyList<long> granules = OggPages.LinkSamples(pages);
        List<float[]> parts = [];
        List<long> frames = [];
        int rate = 0;
        int channels = 0;
        for (int index = 0; index < links.Count; ++index)
        {
            (int offset, int length) = links[index];
            try
            {
                using MemoryStream stream = new(ogg, offset, length, writable: false);
                using VorbisReader reader = new(stream, closeOnDispose: false);
                if (index == 0)
                {
                    rate = reader.SampleRate;
                    channels = reader.Channels;
                }
                else if (reader.SampleRate != rate || reader.Channels != channels)
                {
                    return (null, $"Link {index + 1} of the Ogg stream has {reader.Channels} channels at {reader.SampleRate} Hz, " +
                        $"unlike the first link's {channels} at {rate} Hz.");
                }
                float[] part = ReadAll(reader, channels);
                // A link ends at its last granule position, as Ogg Vorbis
                // says; the decoder can hand out a little of the last
                // packet past it.
                if (granules[index] > 0 && granules[index] * channels < part.Length)
                {
                    part = part[..(int)(granules[index] * channels)];
                }
                parts.Add(part);
                frames.Add(part.Length / channels);
            }
            catch (Exception exception) when (exception is InvalidDataException or ArgumentException or InvalidOperationException
                or IOException or IndexOutOfRangeException or NotSupportedException)
            {
                return (null, $"Link {index + 1} of the Ogg stream could not be decoded: {exception.Message}");
            }
        }
        if (channels < 1)
        {
            return (null, "The Ogg stream holds no audio.");
        }
        float[] samples = new float[parts.Sum(part => (long)part.Length)];
        long at = 0;
        foreach (float[] part in parts)
        {
            part.CopyTo(samples, at);
            at += part.Length;
        }
        return (new DecodedSound(rate, channels, samples, frames), null);
    }

    private static float[] ReadAll(VorbisReader reader, int channels)
    {
        long expected = Math.Max(0, reader.TotalSamples) * channels;
        float[] samples = new float[Math.Min(expected, int.MaxValue / 2)];
        float[] chunk = new float[4096 * channels];
        int length = 0;
        int read;
        while ((read = reader.ReadSamples(chunk, 0, chunk.Length)) > 0)
        {
            if (length + read > samples.Length)
            {
                Array.Resize(ref samples, Math.Max(length + read, samples.Length * 2));
            }
            Array.Copy(chunk, 0, samples, length, read);
            length += read;
        }
        if (length != samples.Length)
        {
            Array.Resize(ref samples, length);
        }
        return samples;
    }
}
