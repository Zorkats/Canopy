using System.Diagnostics;

namespace CanopyStudio.Core.Platform;

// Whether a Windows program is running, on either platform. Windows names
// the process village for village.exe. Under Proton on Linux the process
// is Wine's, named village.exe, and the kernel keeps only the first 15
// characters of a process name, so a long name is seen cut short.
public static class GameProcesses
{
    // TASK_COMM_LEN, 16, less its terminating zero.
    public const int LinuxNameLength = 15;

    // The process names exeName runs under, with or without ".exe".
    public static IReadOnlyList<string> Names(string exeName, bool windows)
    {
        string stem = exeName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? exeName[..^4] : exeName;
        if (windows)
        {
            return [stem];
        }
        string full = stem + ".exe";
        List<string> names = [];
        foreach (string name in new[] { full, stem, Cut(full), Cut(stem) })
        {
            if (!names.Contains(name, StringComparer.OrdinalIgnoreCase))
            {
                names.Add(name);
            }
        }
        return names;
    }

    // True when processName, as the system reports it, is exeName running.
    public static bool Matches(string processName, string exeName, bool windows) =>
        Names(exeName, windows).Contains(processName, StringComparer.OrdinalIgnoreCase);

    public static bool IsRunning(string exeName)
    {
        if (OperatingSystem.IsWindows())
        {
            return Count(Process.GetProcessesByName(Names(exeName, windows: true)[0])) > 0;
        }
        Process[] all = Process.GetProcesses();
        bool found = false;
        foreach (Process process in all)
        {
            try
            {
                found = found || Matches(process.ProcessName, exeName, windows: false);
            }
            catch (Exception exception) when (exception is InvalidOperationException or NotSupportedException)
            {
                // The process ended while the list was read.
            }
        }
        Count(all);
        return found;
    }

    private static string Cut(string name) => name.Length > LinuxNameLength ? name[..LinuxNameLength] : name;

    // How many there were, each disposed.
    private static int Count(Process[] processes)
    {
        foreach (Process process in processes)
        {
            process.Dispose();
        }
        return processes.Length;
    }
}
