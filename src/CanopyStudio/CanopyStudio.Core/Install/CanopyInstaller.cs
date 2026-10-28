using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using CanopyStudio.Core.Platform;
using CanopyStudio.Core.Setup;

namespace CanopyStudio.Core.Install;

public enum LoaderKind
{
    // No dinput8.dll next to village.exe.
    None,
    Canopy,
    // A dinput8.dll that is not Canopy, such as another mod's proxy.
    Other,
}

// What a game folder holds. A version is null for a build made before its
// DLL carried one; QoLVersion is null too when VillageQoL is not there.
public sealed record InstalledCanopy(LoaderKind Loader, string? Version, bool HasQoL, string? QoLVersion);

// Backup names the file the installer set aside, for Uninstall to restore.
public sealed record InstallResult(bool Ok, string Message, string? Backup = null);

// Installs, updates and removes Canopy and VillageQoL in a game folder, each
// on its own. Only dinput8.dll and mods\VillageQoL.dll are ever written or
// removed; no temporary file is made there. Another program's dinput8.dll is
// renamed, never deleted, and a failure puts back everything already
// changed.
public static partial class CanopyInstaller
{
    public const string LoaderFile = "dinput8.dll";
    public const string ModsFolder = "mods";
    public const string QoLFile = "VillageQoL.dll";

    private const string GameRunningText = "Close the game first: Windows keeps the game's DLLs in use while it runs.";

    // Logged by every Canopy build at start, so found in every Canopy DLL,
    // even one made before the DLL carried its version.
    private static readonly byte[] Marker = Encoding.ASCII.GetBytes("Canopy starting, API version");

    // True while the game runs, under Proton too: Windows holds its DLLs then.
    public static bool GameRunning() => GameProcesses.IsRunning(GameLocator.GameExe);

    public static bool IsCanopy(ReadOnlySpan<byte> dll) => dll.IndexOf(Marker) >= 0;

    public static InstalledCanopy Inspect(string folder)
    {
        string qol = GameFiles.Find(folder, Path.Combine(ModsFolder, QoLFile));
        bool hasQoL = File.Exists(qol);
        string? qolVersion = hasQoL ? ProductVersion(qol, "VillageQoL") : null;
        string loader = GameFiles.Find(folder, LoaderFile);
        if (!File.Exists(loader))
        {
            return new(LoaderKind.None, null, hasQoL, qolVersion);
        }
        if (ProductVersion(loader, "Canopy") is { } version)
        {
            return new(LoaderKind.Canopy, version, hasQoL, qolVersion);
        }
        try
        {
            LoaderKind kind = IsCanopy(File.ReadAllBytes(loader)) ? LoaderKind.Canopy : LoaderKind.Other;
            return new(kind, null, hasQoL, qolVersion);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(LoaderKind.Other, null, hasQoL, qolVersion);
        }
    }

    // True when latest is a newer release than installed. A version that is
    // unknown or not a release number (a build of one's own) can always be
    // replaced by the release.
    public static bool IsNewer(string latest, string? installed)
    {
        if (!Version.TryParse(latest, out Version? next))
        {
            return false;
        }
        return installed is null || !Version.TryParse(installed, out Version? current) || next > current;
    }

    // Puts the package's file in folder, replacing an older one. A Canopy
    // package sets another program's dinput8.dll aside first.
    public static InstallResult Install(string folder, CanopyPackage package, bool gameRunning, DateTime now)
    {
        if (gameRunning)
        {
            return new(false, GameRunningText);
        }
        if (!File.Exists(GameFiles.Find(folder, GameLocator.GameExe)))
        {
            return new(false, $"{GameLocator.GameExe} is not in this folder, so {package.Kind} was not installed.");
        }
        string loader = GameFiles.Find(folder, LoaderFile);
        string mods = GameFiles.Find(folder, ModsFolder);
        string path = GameFiles.Find(folder, package.File.Path);
        string? backup = null;
        bool madeMods = false;
        byte[]? old = null;
        bool writing = false;
        try
        {
            if (package.Kind == PackageKind.Canopy && Inspect(folder).Loader == LoaderKind.Other)
            {
                backup = $"{LoaderFile}.old-{now.ToString("HHmmss", CultureInfo.InvariantCulture)}";
                File.Move(loader, Path.Combine(folder, backup));
                // On Linux the one set aside may have been DINPUT8.DLL; the
                // new one is written as dinput8.dll.
                path = GameFiles.Find(folder, package.File.Path);
            }
            if (package.Kind == PackageKind.VillageQoL && !Directory.Exists(mods))
            {
                Directory.CreateDirectory(mods);
                madeMods = true;
            }
            old = File.Exists(path) ? File.ReadAllBytes(path) : null;
            writing = true;
            File.WriteAllBytes(path, package.File.Bytes);
            if (Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path))) != package.File.Sha256)
            {
                throw new IOException($"{package.File.Path} did not read back as it was written.");
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            bool undone = Undo(folder, writing ? path : null, old, backup, madeMods);
            return new(false, undone
                ? $"{package.Kind} was not installed: {exception.Message} The game folder is as it was."
                : $"{package.Kind} was not installed: {exception.Message} Some files could not be put back; check the game folder.");
        }
        string message = package.Kind == PackageKind.Canopy
            ? $"Canopy {package.Version} is installed."
            : $"VillageQoL {package.Version} is installed. It loads with Canopy.";
        if (backup is not null)
        {
            message += $" The dinput8.dll that was there is kept as {backup}.";
        }
        return new(true, message, backup);
    }

    // Removes Canopy's dinput8.dll from folder and puts back restore, the
    // dinput8.dll Install set aside, when it is still there. The mods folder
    // and every mod in it, VillageQoL too, stay.
    public static InstallResult Uninstall(string folder, string? restore, bool gameRunning)
    {
        if (gameRunning)
        {
            return new(false, GameRunningText);
        }
        InstalledCanopy installed = Inspect(folder);
        if (installed.Loader == LoaderKind.None)
        {
            return new(false, "Canopy is not installed in this folder.");
        }
        if (installed.Loader == LoaderKind.Other)
        {
            return new(false, "The dinput8.dll in this folder is not Canopy, so it was left alone.");
        }
        string loader = GameFiles.Find(folder, LoaderFile);
        try
        {
            File.Delete(loader);
            string message = "Canopy is removed. The mods folder and its mods are as they were; they load again once Canopy is back.";
            if (restore is not null && BackupName().IsMatch(restore) && File.Exists(GameFiles.Find(folder, restore)))
            {
                File.Move(GameFiles.Find(folder, restore), Path.Combine(folder, LoaderFile));
                message += $" The dinput8.dll kept as {restore} is back in place.";
            }
            return new(true, message);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(false, $"Canopy could not be removed: {exception.Message}");
        }
    }

    // Removes mods\VillageQoL.dll. Its settings file stays, so installing it
    // again brings the same settings back.
    public static InstallResult RemoveVillageQoL(string folder, bool gameRunning)
    {
        if (gameRunning)
        {
            return new(false, GameRunningText);
        }
        string path = GameFiles.Find(folder, Path.Combine(ModsFolder, QoLFile));
        if (!File.Exists(path))
        {
            return new(false, "VillageQoL is not installed in this folder.");
        }
        try
        {
            File.Delete(path);
            return new(true, "VillageQoL is removed. Its settings file stays, so installing it again brings them back.");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(false, $"VillageQoL could not be removed: {exception.Message}");
        }
    }

    // ProductVersion of the DLL at path when its ProductName is product.
    private static string? ProductVersion(string path, string product)
    {
        DllVersion? info = DllVersions.Read(path).Version;
        return info is not null && info.ProductName == product && !string.IsNullOrWhiteSpace(info.ProductVersionText)
            ? info.ProductVersionText.Trim()
            : null;
    }

    // Puts back what Install changed. written is the file whose write began,
    // old its bytes before. False when something could not be put back.
    private static bool Undo(string folder, string? written, byte[]? old, string? backup, bool madeMods)
    {
        bool undone = true;
        if (written is not null)
        {
            try
            {
                if (old is null)
                {
                    File.Delete(written);
                }
                else
                {
                    File.WriteAllBytes(written, old);
                }
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                // A file whose write itself failed still holds its old bytes.
                undone = old is not null && File.Exists(written) && File.ReadAllBytes(written).AsSpan().SequenceEqual(old);
            }
        }
        string loader = GameFiles.Find(folder, LoaderFile);
        try
        {
            if (backup is not null && !File.Exists(loader))
            {
                File.Move(Path.Combine(folder, backup), loader);
            }
            string mods = GameFiles.Find(folder, ModsFolder);
            if (madeMods && !Directory.EnumerateFileSystemEntries(mods).Any())
            {
                Directory.Delete(mods);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            undone = false;
        }
        return undone;
    }

    [GeneratedRegex(@"^dinput8\.dll\.old-\d{6}$", RegexOptions.IgnoreCase)]
    private static partial Regex BackupName();
}
