using System.Buffers.Binary;

namespace CanopyStudio.Core.Sound;

// What a RIFF WAVE file's fmt chunk says, and where its data chunk is.
// Coefficients are MS ADPCM's predictor pairs, empty for PCM.
public sealed record WaveInfo(int FormatTag, int Channels, int SampleRate, int BlockAlign, int BitsPerSample,
    int SamplesPerBlock, IReadOnlyList<(short First, short Second)> Coefficients, int DataOffset, int DataLength,
    long? FactSamples);

// RIFF WAVE files: read the chunks, decode 16-bit PCM and MS ADPCM, and
// write 16-bit PCM.
public static class WaveFile
{
    public const int FormatPcm = 1;
    public const int FormatMsAdpcm = 2;

    // MS ADPCM's step adaptation, by the nibble just decoded.
    private static readonly int[] Adaptation = [230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230];

    // The chunks of a RIFF WAVE file, or null with why not.
    public static (WaveInfo? Info, string? Error) Inspect(ReadOnlySpan<byte> file)
    {
        if (file.Length < 12 || !file[..4].SequenceEqual("RIFF"u8) || !file.Slice(8, 4).SequenceEqual("WAVE"u8))
        {
            return (null, "The file is not a RIFF WAVE file.");
        }
        int format = -1, channels = 0, rate = 0, blockAlign = 0, bits = 0, perBlock = 0;
        List<(short, short)> coefficients = [];
        int dataOffset = -1, dataLength = 0;
        long? fact = null;
        int at = 12;
        while (at + 8 <= file.Length)
        {
            ReadOnlySpan<byte> id = file.Slice(at, 4);
            uint size = BinaryPrimitives.ReadUInt32LittleEndian(file[(at + 4)..]);
            int body = at + 8;
            int length = (int)Math.Min(size, (uint)(file.Length - body));
            ReadOnlySpan<byte> chunk = file.Slice(body, length);
            if (id.SequenceEqual("fmt "u8) && chunk.Length >= 16)
            {
                format = BinaryPrimitives.ReadUInt16LittleEndian(chunk);
                channels = BinaryPrimitives.ReadUInt16LittleEndian(chunk[2..]);
                rate = (int)BinaryPrimitives.ReadUInt32LittleEndian(chunk[4..]);
                blockAlign = BinaryPrimitives.ReadUInt16LittleEndian(chunk[12..]);
                bits = BinaryPrimitives.ReadUInt16LittleEndian(chunk[14..]);
                if (format == FormatMsAdpcm && chunk.Length >= 22)
                {
                    perBlock = BinaryPrimitives.ReadUInt16LittleEndian(chunk[18..]);
                    int count = BinaryPrimitives.ReadUInt16LittleEndian(chunk[20..]);
                    for (int index = 0; index < count && 22 + index * 4 + 4 <= chunk.Length; ++index)
                    {
                        coefficients.Add((BinaryPrimitives.ReadInt16LittleEndian(chunk[(22 + index * 4)..]),
                            BinaryPrimitives.ReadInt16LittleEndian(chunk[(24 + index * 4)..])));
                    }
                }
            }
            else if (id.SequenceEqual("fact"u8) && chunk.Length >= 4)
            {
                fact = BinaryPrimitives.ReadUInt32LittleEndian(chunk);
            }
            else if (id.SequenceEqual("data"u8))
            {
                dataOffset = body;
                dataLength = length;
            }
            // Chunks are padded to an even size.
            at = body + (int)Math.Min(size + (size & 1), (uint)(file.Length - body));
        }
        if (format < 0)
        {
            return (null, "The WAV file has no fmt chunk.");
        }
        if (dataOffset < 0)
        {
            return (null, "The WAV file has no data chunk.");
        }
        if (channels < 1)
        {
            return (null, "The WAV file says it has no channels.");
        }
        return (new WaveInfo(format, channels, rate, blockAlign, bits, perBlock, coefficients, dataOffset, dataLength, fact), null);
    }

    // A WAV file of 16-bit PCM or MS ADPCM as samples, or null with why not.
    public static (DecodedSound? Sound, string? Error) Decode(ReadOnlySpan<byte> file)
    {
        (WaveInfo? info, string? error) = Inspect(file);
        if (info is null)
        {
            return (null, error);
        }
        ReadOnlySpan<byte> data = file.Slice(info.DataOffset, info.DataLength);
        if (info.FormatTag == FormatPcm && info.BitsPerSample == 16)
        {
            float[] samples = new float[data.Length / 2 / info.Channels * info.Channels];
            for (int index = 0; index < samples.Length; ++index)
            {
                samples[index] = BinaryPrimitives.ReadInt16LittleEndian(data[(index * 2)..]) / 32768f;
            }
            return (new DecodedSound(info.SampleRate, info.Channels, samples, [samples.Length / info.Channels]), null);
        }
        if (info.FormatTag == FormatMsAdpcm)
        {
            return DecodeMsAdpcm(info, data);
        }
        return (null, $"The WAV file is in format {info.FormatTag} with {info.BitsPerSample} bits, which Canopy Studio does not decode.");
    }

    // The standard MS ADPCM decoder. Each block starts, per channel, with a
    // predictor index, a step, and the two samples before the nibbles, the
    // older one played first; then nibbles follow, high one first, taking
    // turns between channels.
    public static (DecodedSound? Sound, string? Error) DecodeMsAdpcm(WaveInfo info, ReadOnlySpan<byte> data)
    {
        int channels = info.Channels;
        int header = 7 * channels;
        if (info.BlockAlign <= header || info.Coefficients.Count == 0)
        {
            return (null, "The MS ADPCM format chunk has no usable block size or coefficients.");
        }
        int perBlock = info.SamplesPerBlock > 0 ? info.SamplesPerBlock : 2 + (info.BlockAlign - header) * 2 / channels;
        int blocks = (data.Length + info.BlockAlign - 1) / info.BlockAlign;
        long most = (long)blocks * perBlock;
        long frames = info.FactSamples is { } fact && fact <= most ? fact : most;
        float[] samples = new float[frames * channels];
        Span<int> coefficient1 = stackalloc int[channels];
        Span<int> coefficient2 = stackalloc int[channels];
        Span<int> step = stackalloc int[channels];
        Span<int> sample1 = stackalloc int[channels];
        Span<int> sample2 = stackalloc int[channels];
        long frame = 0;
        for (int at = 0; at < data.Length && frame < frames; at += info.BlockAlign)
        {
            ReadOnlySpan<byte> block = data.Slice(at, Math.Min(info.BlockAlign, data.Length - at));
            if (block.Length < header)
            {
                break;
            }
            for (int channel = 0; channel < channels; ++channel)
            {
                int predictor = block[channel];
                if (predictor >= info.Coefficients.Count)
                {
                    return (null, $"An MS ADPCM block names predictor {predictor}, but the file has {info.Coefficients.Count}.");
                }
                coefficient1[channel] = info.Coefficients[predictor].First;
                coefficient2[channel] = info.Coefficients[predictor].Second;
                step[channel] = BinaryPrimitives.ReadInt16LittleEndian(block[(channels + channel * 2)..]);
                sample1[channel] = BinaryPrimitives.ReadInt16LittleEndian(block[(3 * channels + channel * 2)..]);
                sample2[channel] = BinaryPrimitives.ReadInt16LittleEndian(block[(5 * channels + channel * 2)..]);
            }
            int blockFrames = (int)Math.Min(Math.Min(perBlock, 2 + (block.Length - header) * 2 / channels), frames - frame);
            for (int channel = 0; channel < channels && blockFrames > 0; ++channel)
            {
                samples[frame * channels + channel] = sample2[channel] / 32768f;
            }
            for (int channel = 0; channel < channels && blockFrames > 1; ++channel)
            {
                samples[(frame + 1) * channels + channel] = sample1[channel] / 32768f;
            }
            int nibbles = (blockFrames - 2) * channels;
            for (int index = 0; index < nibbles; ++index)
            {
                int value = block[header + index / 2];
                int nibble = (index & 1) == 0 ? value >> 4 : value & 0x0F;
                int channel = index % channels;
                int predicted = (sample1[channel] * coefficient1[channel] + sample2[channel] * coefficient2[channel]) >> 8;
                predicted += (nibble >= 8 ? nibble - 16 : nibble) * step[channel];
                predicted = Math.Clamp(predicted, short.MinValue, short.MaxValue);
                sample2[channel] = sample1[channel];
                sample1[channel] = predicted;
                step[channel] = Math.Max(16, Adaptation[nibble] * step[channel] >> 8);
                samples[(frame + 2 + index / channels) * channels + channel] = predicted / 32768f;
            }
            frame += Math.Max(blockFrames, 0);
        }
        if (frame < frames)
        {
            Array.Resize(ref samples, (int)(frame * channels));
        }
        return (new DecodedSound(info.SampleRate, channels, samples, [frame]), null);
    }

    // sound as a WAV file of 16-bit PCM.
    public static byte[] WritePcm16(DecodedSound sound)
    {
        int data = sound.Samples.Length * 2;
        byte[] file = new byte[44 + data];
        Span<byte> span = file;
        "RIFF"u8.CopyTo(span);
        BinaryPrimitives.WriteUInt32LittleEndian(span[4..], (uint)(36 + data));
        "WAVE"u8.CopyTo(span[8..]);
        "fmt "u8.CopyTo(span[12..]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[16..], 16);
        BinaryPrimitives.WriteUInt16LittleEndian(span[20..], FormatPcm);
        BinaryPrimitives.WriteUInt16LittleEndian(span[22..], (ushort)sound.Channels);
        BinaryPrimitives.WriteUInt32LittleEndian(span[24..], (uint)sound.SampleRate);
        BinaryPrimitives.WriteUInt32LittleEndian(span[28..], (uint)(sound.SampleRate * sound.Channels * 2));
        BinaryPrimitives.WriteUInt16LittleEndian(span[32..], (ushort)(sound.Channels * 2));
        BinaryPrimitives.WriteUInt16LittleEndian(span[34..], 16);
        "data"u8.CopyTo(span[36..]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[40..], (uint)data);
        for (int index = 0; index < sound.Samples.Length; ++index)
        {
            BinaryPrimitives.WriteInt16LittleEndian(span[(44 + index * 2)..], ToShort(sound.Samples[index]));
        }
        return file;
    }

    // A sample back to 16 bits: exact for a sample that came from 16 bits.
    public static short ToShort(float sample) =>
        (short)Math.Clamp(MathF.Round(sample * 32768f), short.MinValue, short.MaxValue);
}
