using System.Buffers.Binary;

namespace CanopyStudio.Core.Game;

// What the header of a PNG says: its size, or why Canopy cannot take it.
public sealed record ArtPngInfo(int Width, int Height, string? Error)
{
    public bool Ok => Error is null;
}

// Checks a PNG before it goes into the edits mod, from its first bytes
// alone: the signature, an IHDR chunk Canopy's image readers (WIC on
// Windows, stb elsewhere) both take, and the 8192x8192 limit the loader
// sets for a [texture.] image.
public static class ArtPng
{
    public const int MaxSide = 8192;

    // The signature, then IHDR: length, type and 13 bytes of data.
    public const int HeaderLength = 8 + 8 + 13;

    private static readonly byte[] Signature = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];

    // The header of the PNG at path; name is how messages call it.
    public static ArtPngInfo Read(string path, string name)
    {
        byte[] header = new byte[HeaderLength];
        int read = 0;
        try
        {
            using FileStream stream = File.OpenRead(path);
            while (read < header.Length && stream.Read(header, read, header.Length - read) is > 0 and var count)
            {
                read += count;
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new ArtPngInfo(0, 0, $"{name} could not be read: {exception.Message}");
        }
        return Check(header.AsSpan(0, read), name);
    }

    public static ArtPngInfo Check(ReadOnlySpan<byte> header, string name)
    {
        if (header.Length < Signature.Length || !header[..Signature.Length].SequenceEqual(Signature))
        {
            return Refused($"{name} is not a PNG file. Save the image as PNG and pick it again.");
        }
        if (header.Length < HeaderLength ||
            BinaryPrimitives.ReadUInt32BigEndian(header[8..]) != 13 ||
            !header[12..16].SequenceEqual("IHDR"u8))
        {
            return Refused($"{name} is cut short or damaged: its PNG header is not there. Save it again and pick it again.");
        }
        uint width = BinaryPrimitives.ReadUInt32BigEndian(header[16..]);
        uint height = BinaryPrimitives.ReadUInt32BigEndian(header[20..]);
        byte depth = header[24];
        byte color = header[25];
        if (width == 0 || height == 0)
        {
            return Refused($"{name} has no pixels. Pick a PNG with an image in it.");
        }
        if (width > MaxSide || height > MaxSide)
        {
            return Refused($"{name} is {width}x{height}, larger than the {MaxSide}x{MaxSide} Canopy takes. Make it smaller and pick it again.");
        }
        if (!DepthFits(color, depth) || header[26] != 0 || header[27] != 0 || header[28] > 1)
        {
            return Refused($"{name} is a kind of PNG Canopy cannot read (color type {color}, {depth} bits). Save it as an 8-bit RGBA PNG and pick it again.");
        }
        return new ArtPngInfo((int)width, (int)height, null);
    }

    // The bit depths the PNG standard allows for each color type.
    private static bool DepthFits(byte color, byte depth) => color switch
    {
        0 => depth is 1 or 2 or 4 or 8 or 16,
        3 => depth is 1 or 2 or 4 or 8,
        2 or 4 or 6 => depth is 8 or 16,
        _ => false,
    };

    private static ArtPngInfo Refused(string error) => new(0, 0, error);
}
