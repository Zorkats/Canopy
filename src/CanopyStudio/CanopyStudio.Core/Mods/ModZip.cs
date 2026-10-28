using System.IO.Compression;
using CanopyStudio.Core.Platform;

namespace CanopyStudio.Core.Mods;

// One file an install writes. Path is relative to the game folder.
public sealed record PlannedFile(string Path, byte[] Bytes, bool Replaces);

// What installing a zip will do, worked out before anything is written.
// Mod is the folder or DLL file name in mods. Writes lists every file the
// install writes, Removes every file of the copy already there that the zip
// lacks, Skipped every zip entry left out. Problems says, a sentence each,
// why the zip is refused; the plan can be applied only when it is empty.
public sealed record ModInstallPlan(
    string Mod,
    bool IsDllFile,
    IReadOnlyList<PlannedFile> Writes,
    IReadOnlyList<string> Removes,
    IReadOnlyList<string> Skipped,
    IReadOnlyList<string> Problems)
{
    public bool Ok => Problems.Count == 0;

    // Every existing file the install gives new bytes.
    public IReadOnlyList<string> Replaces => Writes.Where(file => file.Replaces).Select(file => file.Path).ToList();

    internal static ModInstallPlan Refused(params string[] problems) => new("", false, [], [], [], problems);
}

// Caps on what a zip may unpack to.
public sealed record ModZipLimits(long MaxFileBytes = 256L << 20, long MaxTotalBytes = 1L << 30, int MaxFiles = 20000);

// Works out where a mod zip goes and what it writes, reading the zip in
// memory only. The layouts it knows, in the order it tries them:
// mods\<Mod>\... or mods\<Mod>.dll; one top folder holding a canopy.ini, a
// data folder or lua\main.lua; one top folder holding one DLL, which goes
// directly in mods; a root that is the mod itself, named after the zip; one
// DLL at the root. A lone .dll file is a DLL mod too.
public static class ModZip
{
    private const string Mods = "mods/";

    public static ModInstallPlan Plan(string gameFolder, byte[] bytes, string fileName, ModZipLimits? limits = null)
    {
        limits ??= new ModZipLimits();
        if (ModNames.IsDll(fileName))
        {
            return bytes.LongLength > limits.MaxFileBytes
                ? ModInstallPlan.Refused($"{fileName} is larger than the {Size(limits.MaxFileBytes)} a mod file may be.")
                : Build(gameFolder, Path.GetFileName(fileName), true, [(Path.GetFileName(fileName), bytes)], []);
        }
        List<(string Path, byte[] Bytes)> files = [];
        List<string> problems = [];
        try
        {
            using ZipArchive archive = new(new MemoryStream(bytes, writable: false), ZipArchiveMode.Read);
            Read(archive, limits, files, problems);
        }
        catch (InvalidDataException)
        {
            return ModInstallPlan.Refused($"{fileName} is not a zip file Canopy Studio can read.");
        }
        if (problems.Count > 0)
        {
            return ModInstallPlan.Refused([.. problems]);
        }
        if (files.Count == 0)
        {
            return ModInstallPlan.Refused($"{fileName} holds no files.");
        }
        return Place(gameFolder, Path.GetFileNameWithoutExtension(fileName), files);
    }

    // Reads every file entry, with slashes made forward, refusing names
    // that could land outside the mod and sizes past the caps.
    private static void Read(ZipArchive archive, ModZipLimits limits, List<(string, byte[])> files, List<string> problems)
    {
        HashSet<string> seen = new(StringComparer.OrdinalIgnoreCase);
        long total = 0;
        foreach (ZipArchiveEntry entry in archive.Entries)
        {
            string name = entry.FullName.Replace('\\', '/');
            if (name.EndsWith('/'))
            {
                continue;
            }
            if (files.Count + problems.Count >= limits.MaxFiles)
            {
                problems.Add($"The zip holds more than {limits.MaxFiles} files, more than a mod may have.");
                return;
            }
            if (name.StartsWith('/') || (name.Length > 1 && name[1] == ':'))
            {
                problems.Add($"{name} is an absolute path, which a mod zip may not hold.");
                continue;
            }
            string[] parts = name.Split('/');
            if (parts.Contains(".."))
            {
                problems.Add($"{name} climbs out of its folder with \"..\", which a mod zip may not do.");
                continue;
            }
            if (parts.Select(ModNames.NameProblem).FirstOrDefault(problem => problem is not null) is { } bad)
            {
                problems.Add($"{name} {bad}, so Windows cannot hold it.");
                continue;
            }
            if (!seen.Add(name))
            {
                problems.Add($"{name} is in the zip twice.");
                continue;
            }
            if (entry.Length > limits.MaxFileBytes)
            {
                problems.Add($"{name} is larger than the {Size(limits.MaxFileBytes)} a mod file may be.");
                continue;
            }
            total += entry.Length;
            if (total > limits.MaxTotalBytes)
            {
                problems.Add($"The zip unpacks to more than the {Size(limits.MaxTotalBytes)} a mod may be.");
                return;
            }
            // The sizes in a zip's header can lie, so the read stops past the cap.
            using Stream stream = entry.Open();
            using MemoryStream copy = new();
            byte[] buffer = new byte[81920];
            int read;
            while ((read = stream.Read(buffer)) > 0)
            {
                copy.Write(buffer, 0, read);
                if (copy.Length > entry.Length || copy.Length > limits.MaxFileBytes)
                {
                    problems.Add($"{name} unpacks to more bytes than the zip says it holds.");
                    return;
                }
            }
            files.Add((name, copy.ToArray()));
        }
    }

    private static ModInstallPlan Place(string gameFolder, string zipName, List<(string Path, byte[] Bytes)> files)
    {
        // mods\<Mod>\... or mods\<Mod>.dll; the rest of the zip stays out.
        List<(string Path, byte[] Bytes)> inMods = files.Where(file => file.Path.StartsWith(Mods, StringComparison.OrdinalIgnoreCase)).ToList();
        if (inMods.Count > 0)
        {
            string[] mods = inMods.Select(file => file.Path[Mods.Length..].Split('/')[0]).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            if (mods.Length > 1)
            {
                return ModInstallPlan.Refused($"The zip holds {mods.Length} mods in its mods folder; install them one at a time.");
            }
            string mod = mods[0];
            bool dll = inMods.Any(file => file.Path.Length == Mods.Length + mod.Length);
            if (dll && (inMods.Count > 1 || !ModNames.IsDll(mod)))
            {
                return ModInstallPlan.Refused($"{ModNames.Shown(mod)} in the zip is a file but not a DLL, and only DLL files go directly in mods.");
            }
            return Build(gameFolder, mod, dll, inMods.Select(file => (file.Path[Mods.Length..], file.Bytes)).ToList(),
                Others(files, inMods));
        }

        // One top folder that is a mod, or that wraps one DLL. Canopy loads
        // only DLL files directly in mods, so the DLL leaves its folder.
        string[] tops = files.Select(file => file.Path.Split('/')[0]).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (tops.Length == 1 && files.All(file => file.Path.Contains('/')))
        {
            string top = tops[0];
            List<(string Path, byte[] Bytes)> inner = files.Select(file => (file.Path[(top.Length + 1)..], file.Bytes)).ToList();
            if (LooksLikeMod(inner))
            {
                return Build(gameFolder, top, false, inner.Select(file => (top + "/" + file.Path, file.Bytes)).ToList(), []);
            }
            List<(string Path, byte[] Bytes)> wrapped = files.Where(file => file.Path.Count(c => c == '/') == 1 && ModNames.IsDll(file.Path)).ToList();
            if (wrapped.Count == 1)
            {
                string name = wrapped[0].Path[(top.Length + 1)..];
                return Build(gameFolder, name, true, [(name, wrapped[0].Bytes)], Others(files, wrapped));
            }
            if (wrapped.Count > 1)
            {
                return ModInstallPlan.Refused(SeveralDlls);
            }
        }

        // The root is the mod itself.
        if (LooksLikeMod(files))
        {
            return Build(gameFolder, zipName, false, files.Select(file => (zipName + "/" + file.Path, file.Bytes)).ToList(), []);
        }

        // One DLL at the root, with a readme or licence beside it.
        List<(string Path, byte[] Bytes)> dlls = files.Where(file => !file.Path.Contains('/') && ModNames.IsDll(file.Path)).ToList();
        if (dlls.Count == 1)
        {
            return Build(gameFolder, dlls[0].Path, true, dlls, Others(files, dlls));
        }
        return ModInstallPlan.Refused(dlls.Count > 1
            ? SeveralDlls
            : $"The zip does not look like a mod: it has no canopy.ini, data folder, {GameFiles.Shown("lua/main.lua")} or DLL.");
    }

    private const string SeveralDlls = "The zip holds several DLLs and no canopy.ini, so which mod folder they belong in is unclear.";

    private static bool LooksLikeMod(List<(string Path, byte[] Bytes)> files) => files.Any(file =>
        file.Path.Equals("canopy.ini", StringComparison.OrdinalIgnoreCase)
        || file.Path.Equals("lua/main.lua", StringComparison.OrdinalIgnoreCase)
        || file.Path.StartsWith("data/", StringComparison.OrdinalIgnoreCase));

    private static List<string> Others(List<(string Path, byte[] Bytes)> files, List<(string Path, byte[] Bytes)> used) =>
        files.Except(used).Select(file => GameFiles.Shown(file.Path)).ToList();

    // The plan for files whose paths start with mod, the folder or DLL file
    // name in mods. Checks the target, then compares with what is there.
    private static ModInstallPlan Build(string gameFolder, string mod, bool dll, List<(string Path, byte[] Bytes)> files, List<string> skipped)
    {
        if (ModNames.NameProblem(mod) is { } problem)
        {
            return ModInstallPlan.Refused($"The mod's name \"{mod}\" {problem}, so Windows cannot hold it.");
        }
        if (ModNames.Same(mod, ModNames.DataFolder))
        {
            return ModInstallPlan.Refused($"The zip aims at {ModNames.Shown(ModNames.DataFolder)}, Canopy's own folder, which no mod may write.");
        }
        string mods = Path.GetFullPath(ModNames.ModsIn(gameFolder));
        // On Linux a copy already there may have its name in another case;
        // Canopy sees one mod under Proton, so the plan takes the name on disk.
        string target = GameFiles.Find(mods, mod);
        mod = Path.GetFileName(target);
        if (dll ? Directory.Exists(target) : File.Exists(target))
        {
            return ModInstallPlan.Refused(dll
                ? $"{ModNames.Shown(mod)} is a folder, so a DLL of that name cannot go there."
                : $"{ModNames.Shown(mod)} is a file, so a mod folder of that name cannot go there.");
        }
        List<string> problems = [];
        List<PlannedFile> writes = [];
        foreach ((string path, byte[] bytes) in files)
        {
            // Slashes either way, and on Linux the case of what is there.
            string full = Path.GetFullPath(GameFiles.Find(mods, path));
            if (!ModNames.Inside(full, target) || ModNames.Inside(full, Path.Combine(mods, ModNames.DataFolder)))
            {
                problems.Add($"{path} would land outside {ModNames.Shown(mod)}, which a mod zip may not do.");
                continue;
            }
            writes.Add(new(Path.GetRelativePath(gameFolder, full), bytes, File.Exists(full)));
        }
        if (problems.Count > 0)
        {
            return ModInstallPlan.Refused([.. problems]);
        }
        List<string> removes = [];
        if (!dll && Directory.Exists(target))
        {
            if (ModChanges.LinkProblem(target, mod) is { } link)
            {
                return ModInstallPlan.Refused(link);
            }
            HashSet<string> written = new(writes.Select(file => file.Path), StringComparer.OrdinalIgnoreCase);
            removes.AddRange(Directory.EnumerateFiles(target, "*", new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = 0 })
                .Select(path => Path.GetRelativePath(gameFolder, path))
                .Where(path => !written.Contains(path))
                .Order(StringComparer.OrdinalIgnoreCase));
        }
        return new(mod, dll, writes, removes, skipped, []);
    }

    private static string Size(long bytes) => bytes >= 1L << 20 ? $"{bytes >> 20} MB" : $"{bytes} bytes";
}
