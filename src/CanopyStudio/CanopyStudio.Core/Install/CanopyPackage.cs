using System.IO.Compression;
using System.Security.Cryptography;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Install;

// The two things a release installs, each from its own package. The names
// are the packages' names too: Canopy-<version>.zip, VillageQoL-<version>.zip.
public enum PackageKind
{
    // The loader, dinput8.dll next to village.exe.
    Canopy,
    // The VillageQoL mod, mods\VillageQoL.dll.
    VillageQoL,
}

// A file the installer writes, by its path under the game folder.
public sealed record PackageFile(string Path, byte[] Bytes)
{
    public string Sha256 { get; } = Convert.ToHexStringLower(SHA256.HashData(Bytes));
}

// The one file Canopy Studio installs from a package, read from the zip in
// memory. The rest of the zip (the readme and the licences) never reaches
// the game folder.
public sealed class CanopyPackage
{
    // The file is well under a megabyte.
    private const long MaxFileSize = 32L * 1024 * 1024;

    private CanopyPackage(PackageKind kind, string version, PackageFile file)
    {
        Kind = kind;
        Version = version;
        File = file;
    }

    public PackageKind Kind { get; }
    public string Version { get; }
    public PackageFile File { get; }

    // The file's path under the game folder, with slashes.
    public static string PathOf(PackageKind kind) => kind == PackageKind.Canopy
        ? CanopyInstaller.LoaderFile
        : $"{CanopyInstaller.ModsFolder}/{CanopyInstaller.QoLFile}";

    public static (CanopyPackage? Package, string? Error) Read(byte[] zip, PackageKind kind, string version)
    {
        string wanted = PathOf(kind);
        byte[]? bytes = null;
        try
        {
            using ZipArchive archive = new(new MemoryStream(zip), ZipArchiveMode.Read);
            foreach (ZipArchiveEntry entry in archive.Entries)
            {
                // Windows PowerShell writes backslashes, PowerShell 7 slashes.
                string path = entry.FullName.Replace('\\', '/').TrimStart('.', '/');
                if (!path.Equals(wanted, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                if (entry.Length > MaxFileSize)
                {
                    return (null, $"{path} in the {kind} package is too large.");
                }
                using Stream stream = entry.Open();
                using MemoryStream memory = new();
                stream.CopyTo(memory);
                bytes = memory.ToArray();
            }
        }
        catch (InvalidDataException)
        {
            return (null, $"The {kind} package is not a zip that can be read.");
        }
        if (bytes is null)
        {
            return (null, $"The {kind} package lacks {GameFiles.Shown(wanted)}.");
        }
        if (kind == PackageKind.Canopy && !CanopyInstaller.IsCanopy(bytes))
        {
            return (null, $"The Canopy package's {CanopyInstaller.LoaderFile} is not Canopy.");
        }
        return (new CanopyPackage(kind, version,
            new PackageFile(wanted.Replace('/', System.IO.Path.DirectorySeparatorChar), bytes)), null);
    }
}
