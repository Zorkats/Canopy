using System.Buffers.Binary;
using System.IO.Compression;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Identity;

// Writes a decoded texture as an 8-bit RGBA PNG, so the asset cache needs
// no image library. Each row uses the Sub filter, which suits painted art.
public static class PngWriter
{
    private static readonly uint[] CrcTable = BuildCrcTable();

    public static byte[] Encode(TextureImage image)
    {
        using MemoryStream png = new();
        png.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);

        byte[] header = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(header, image.Width);
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), image.Height);
        header[8] = 8;  // bits per channel
        header[9] = 6;  // RGBA
        Chunk(png, "IHDR", header);

        using MemoryStream packed = new();
        using (ZLibStream zlib = new(packed, CompressionLevel.Optimal, leaveOpen: true))
        {
            int stride = image.Width * 4;
            byte[] row = new byte[1 + stride];
            row[0] = 1;  // Sub: each byte minus the same channel of the pixel to its left
            for (int y = 0; y < image.Height; ++y)
            {
                int source = y * stride;
                for (int x = 0; x < stride; x += 4)
                {
                    int at = source + x;
                    int left = x >= 4 ? at - 4 : -1;
                    // BGRA in, RGBA out.
                    row[1 + x] = Sub(image.Pixels[at + 2], left < 0 ? (byte)0 : image.Pixels[left + 2]);
                    row[2 + x] = Sub(image.Pixels[at + 1], left < 0 ? (byte)0 : image.Pixels[left + 1]);
                    row[3 + x] = Sub(image.Pixels[at], left < 0 ? (byte)0 : image.Pixels[left]);
                    row[4 + x] = Sub(image.Pixels[at + 3], left < 0 ? (byte)0 : image.Pixels[left + 3]);
                }
                zlib.Write(row);
            }
        }
        Chunk(png, "IDAT", packed.ToArray());
        Chunk(png, "IEND", []);
        return png.ToArray();
    }

    private static byte Sub(byte value, byte left) => unchecked((byte)(value - left));

    private static void Chunk(Stream png, string type, byte[] data)
    {
        Span<byte> number = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(number, data.Length);
        png.Write(number);
        byte[] typeBytes = System.Text.Encoding.ASCII.GetBytes(type);
        png.Write(typeBytes);
        png.Write(data);
        uint crc = Crc(Crc(0xFFFFFFFFu, typeBytes), data) ^ 0xFFFFFFFFu;
        BinaryPrimitives.WriteUInt32BigEndian(number, crc);
        png.Write(number);
    }

    private static uint Crc(uint crc, byte[] bytes)
    {
        foreach (byte value in bytes)
        {
            crc = CrcTable[(crc ^ value) & 0xFF] ^ (crc >> 8);
        }
        return crc;
    }

    private static uint[] BuildCrcTable()
    {
        uint[] table = new uint[256];
        for (uint n = 0; n < 256; ++n)
        {
            uint c = n;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        return table;
    }
}
