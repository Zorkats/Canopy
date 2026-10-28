using System.Buffers.Binary;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace CanopyStudio.Core.Platform;

// What a DLL's version resource says. FileVersion and ProductVersion are the
// numbers of its fixed part, null when it has none; Strings holds its string
// table, ProductName, ProductVersion and FileVersion among them, by name.
public sealed record DllVersion(Version? FileVersion, Version? ProductVersion, IReadOnlyDictionary<string, string> Strings)
{
    public static readonly DllVersion None = new(null, null, new Dictionary<string, string>());

    public string? ProductName => Strings.GetValueOrDefault("ProductName");
    public string? ProductVersionText => Strings.GetValueOrDefault("ProductVersion");
    public string? FileVersionText => Strings.GetValueOrDefault("FileVersion");
}

public sealed record DllVersionResult(DllVersion? Version, string? Error)
{
    public bool Ok => Version is not null;
}

// Reads VS_VERSIONINFO straight out of a PE file, on every platform:
// FileVersionInfo reads no PE resources off Windows, and Canopy's DLLs stay
// Windows DLLs when Canopy Studio runs on Linux. It picks the string table
// the way FileVersionInfo does, so both give the same answers. A file that
// is not a PE, or is cut short, gives an error result, never an exception;
// a PE without a version resource gives DllVersion.None.
public static class DllVersions
{
    private const int ResourceTable = 2;
    private const uint VersionType = 16;
    private const uint FixedSignature = 0xFEEF04BD;
    // A version resource is a few kilobytes; a resource section far past
    // this is not one to trust.
    private const int MaxSection = 64 << 20;

    // The string tables FileVersionInfo tries after the one the
    // translation names: US English in Unicode, in Windows Latin 1, neutral.
    private static readonly string[] FallbackTables = ["040904B0", "040904E4", "04090000"];

    public static DllVersionResult Read(string path)
    {
        try
        {
            using SafeFileHandle file = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            return Read(file, Path.GetFileName(path));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(null, $"{Path.GetFileName(path)} could not be read: {exception.Message}");
        }
    }

    private static DllVersionResult Read(SafeFileHandle file, string name)
    {
        string notPe = $"{name} is not a Windows DLL or program.";
        string cut = $"{name} is cut short or damaged, so its version cannot be read.";
        long length = RandomAccess.GetLength(file);

        byte[] dos = ReadAt(file, 0, 64);
        if (dos.Length < 64 || dos[0] != 'M' || dos[1] != 'Z')
        {
            return new(null, notPe);
        }
        long header = BinaryPrimitives.ReadUInt32LittleEndian(dos.AsSpan(0x3C));
        byte[] coff = header + 24 <= length ? ReadAt(file, header, 24) : [];
        if (coff.Length < 24 || BinaryPrimitives.ReadUInt32LittleEndian(coff) != 0x00004550)
        {
            return new(null, notPe);
        }
        int sections = BinaryPrimitives.ReadUInt16LittleEndian(coff.AsSpan(6));
        int optionalSize = BinaryPrimitives.ReadUInt16LittleEndian(coff.AsSpan(20));
        byte[] optional = ReadAt(file, header + 24, optionalSize);
        if (optional.Length < optionalSize || optionalSize < 2)
        {
            return new(null, cut);
        }
        // PE32 and PE32+ differ in where the data directories start.
        int directories = BinaryPrimitives.ReadUInt16LittleEndian(optional) switch
        {
            0x10B => 96,
            0x20B => 112,
            _ => -1,
        };
        if (directories < 0)
        {
            return new(null, notPe);
        }
        if (optional.Length < directories)
        {
            return new(null, cut);
        }
        int count = (int)BinaryPrimitives.ReadUInt32LittleEndian(optional.AsSpan(directories - 4));
        int entry = directories + ResourceTable * 8;
        if (count <= ResourceTable || entry + 8 > optional.Length)
        {
            return new(DllVersion.None, null);
        }
        uint resourceRva = BinaryPrimitives.ReadUInt32LittleEndian(optional.AsSpan(entry));
        if (resourceRva == 0)
        {
            return new(DllVersion.None, null);
        }

        byte[] table = ReadAt(file, header + 24 + optionalSize, sections * 40);
        if (table.Length < sections * 40)
        {
            return new(null, cut);
        }
        List<Section> list = [];
        for (int index = 0; index < sections; ++index)
        {
            ReadOnlySpan<byte> row = table.AsSpan(index * 40, 40);
            list.Add(new(
                BinaryPrimitives.ReadUInt32LittleEndian(row[8..]),
                BinaryPrimitives.ReadUInt32LittleEndian(row[12..]),
                BinaryPrimitives.ReadUInt32LittleEndian(row[16..]),
                BinaryPrimitives.ReadUInt32LittleEndian(row[20..])));
        }
        Section? home = list.FirstOrDefault(section => section.Holds(resourceRva));
        if (home is null || home.RawSize > MaxSection || home.RawOffset + (long)home.RawSize > length)
        {
            return new(null, cut);
        }
        byte[] resources = ReadAt(file, home.RawOffset, (int)home.RawSize);
        if (resources.Length < home.RawSize)
        {
            return new(null, cut);
        }
        int root = (int)(resourceRva - home.Address);

        // Type 16, then the name (1, else the first), then the first language.
        int? byName = Child(resources, root, VersionType, exact: true);
        int? byLanguage = byName is { } names ? Child(resources, names, 1, exact: false) : null;
        int? leaf = byLanguage is { } languages ? Child(resources, languages, 0, exact: false) : null;
        if (byName is null)
        {
            return new(DllVersion.None, null);
        }
        if (leaf is not { } data || data + 16 > resources.Length)
        {
            return new(null, cut);
        }
        uint dataRva = BinaryPrimitives.ReadUInt32LittleEndian(resources.AsSpan(data));
        int dataSize = (int)Math.Min(BinaryPrimitives.ReadUInt32LittleEndian(resources.AsSpan(data + 4)), MaxSection);
        byte[] block;
        if (home.Holds(dataRva) && dataRva - home.Address + (long)dataSize <= resources.Length)
        {
            block = resources.AsSpan((int)(dataRva - home.Address), dataSize).ToArray();
        }
        else if (list.FirstOrDefault(section => section.Holds(dataRva)) is { } other)
        {
            block = ReadAt(file, other.RawOffset + (dataRva - other.Address), dataSize);
        }
        else
        {
            return new(null, cut);
        }
        return block.Length < dataSize ? new(null, cut) : Parse(block) is { } version ? new(version, null) : new(null, cut);
    }

    // The offset in resources of the entry below the directory at offset
    // whose id is id, or the first entry when none is and exact is false.
    // A subdirectory's offset has its top bit set; a leaf's does not.
    private static int? Child(byte[] resources, int offset, uint id, bool exact)
    {
        if (offset < 0 || offset + 16 > resources.Length)
        {
            return null;
        }
        int named = BinaryPrimitives.ReadUInt16LittleEndian(resources.AsSpan(offset + 12));
        int numbered = BinaryPrimitives.ReadUInt16LittleEndian(resources.AsSpan(offset + 14));
        int? first = null;
        for (int index = 0; index < named + numbered; ++index)
        {
            int at = offset + 16 + index * 8;
            if (at + 8 > resources.Length)
            {
                return null;
            }
            uint key = BinaryPrimitives.ReadUInt32LittleEndian(resources.AsSpan(at));
            int target = (int)(BinaryPrimitives.ReadUInt32LittleEndian(resources.AsSpan(at + 4)) & 0x7FFFFFFF);
            first ??= target;
            if ((key & 0x80000000) == 0 && key == id)
            {
                return target;
            }
        }
        return exact ? null : first;
    }

    // VS_VERSIONINFO: the fixed part, then StringFileInfo and VarFileInfo.
    // Null when the block is not one.
    private static DllVersion? Parse(byte[] data)
    {
        if (Node(data, 0) is not { Key: "VS_VERSION_INFO" } top)
        {
            return null;
        }
        Version? file = null;
        Version? product = null;
        if (top.ValueLength >= 52 && top.Value + 52 <= data.Length
            && BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(top.Value)) == FixedSignature)
        {
            file = Numbers(data, top.Value + 8);
            product = Numbers(data, top.Value + 16);
        }

        Dictionary<string, Dictionary<string, string>> tables = new(StringComparer.OrdinalIgnoreCase);
        string? translation = null;
        foreach (Block info in Children(data, top))
        {
            if (info.Key == "StringFileInfo")
            {
                foreach (Block table in Children(data, info))
                {
                    Dictionary<string, string> strings = new(StringComparer.Ordinal);
                    foreach (Block text in Children(data, table))
                    {
                        strings.TryAdd(text.Key, Text(data, text.Value, text.End));
                    }
                    tables.TryAdd(table.Key, strings);
                }
            }
            else if (info.Key == "VarFileInfo")
            {
                Block? var = Children(data, info).FirstOrDefault(child => child.Key == "Translation");
                if (translation is null && var is { ValueLength: >= 4 } found && found.Value + 4 <= data.Length)
                {
                    int language = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(found.Value));
                    int codePage = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(found.Value + 2));
                    translation = $"{language:X4}{codePage:X4}";
                }
            }
        }
        // As FileVersionInfo: the translation's table, else the fallbacks; a
        // file without a translation is taken to be US English, Latin 1.
        IEnumerable<string> order = [translation ?? "040904E4", .. FallbackTables];
        IReadOnlyDictionary<string, string> chosen = order.Select(key => tables.GetValueOrDefault(key))
            .FirstOrDefault(table => table is not null) ?? new Dictionary<string, string>();
        return new(file, product, chosen);
    }

    // A version block: its key, where its value starts and how long the
    // value is as written, where its children start and where it ends.
    private sealed record Block(string Key, int Value, int ValueLength, int Children, int End);

    private sealed record Section(uint VirtualSize, uint Address, uint RawSize, uint RawOffset)
    {
        public bool Holds(uint rva) => rva >= Address && rva < Address + Math.Max(VirtualSize, RawSize);
    }

    private static Block? Node(byte[] data, int offset)
    {
        if (offset < 0 || offset + 6 > data.Length)
        {
            return null;
        }
        int length = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset));
        int valueLength = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset + 2));
        int type = BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(offset + 4));
        int end = offset + length;
        if (length < 6 || end > data.Length)
        {
            return null;
        }
        int keyEnd = offset + 6;
        while (keyEnd + 1 < end && (data[keyEnd] | data[keyEnd + 1]) != 0)
        {
            keyEnd += 2;
        }
        string key = Encoding.Unicode.GetString(data, offset + 6, keyEnd - offset - 6);
        int value = Align(keyEnd + 2);
        // A text value's length counts characters, a binary one's bytes.
        int valueBytes = type == 1 ? valueLength * 2 : valueLength;
        return new(key, value, valueBytes, Math.Min(Align(value + valueBytes), end), end);
    }

    private static IEnumerable<Block> Children(byte[] data, Block parent)
    {
        int offset = parent.Children;
        while (offset + 6 <= parent.End && Node(data, offset) is { } child && child.End <= parent.End)
        {
            yield return child;
            offset = Align(child.End);
        }
    }

    // A text value up to its terminating zero or its block's end, as
    // VerQueryValue hands it out.
    private static string Text(byte[] data, int start, int end)
    {
        int stop = start;
        while (stop + 1 < end && (data[stop] | data[stop + 1]) != 0)
        {
            stop += 2;
        }
        return stop > start ? Encoding.Unicode.GetString(data, start, stop - start) : "";
    }

    // Four 16-bit parts from two 32-bit words, most significant first.
    private static Version Numbers(byte[] data, int offset)
    {
        uint high = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset));
        uint low = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(offset + 4));
        return new((int)(high >> 16), (int)(high & 0xFFFF), (int)(low >> 16), (int)(low & 0xFFFF));
    }

    private static int Align(int offset) => (offset + 3) & ~3;

    // Up to count bytes from offset; fewer at the end of the file.
    private static byte[] ReadAt(SafeFileHandle file, long offset, int count)
    {
        if (count <= 0 || offset < 0)
        {
            return [];
        }
        byte[] buffer = new byte[count];
        int total = 0;
        while (total < count)
        {
            int read = RandomAccess.Read(file, buffer.AsSpan(total), offset + total);
            if (read == 0)
            {
                break;
            }
            total += read;
        }
        return total == count ? buffer : buffer[..total];
    }
}
