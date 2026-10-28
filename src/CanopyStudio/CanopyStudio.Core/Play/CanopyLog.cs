using System.Text;

namespace CanopyStudio.Core.Play;

// The last run in Canopy.log. Exists is false when there is no log yet;
// Error says why an existing log could not be read. Lines are the run's
// lines without their line endings.
public sealed record CanopyLogRun(bool Exists, IReadOnlyList<string> Lines, string? Error)
{
    public static readonly CanopyLogRun None = new(false, [], null);
}

// Reads <game>\Canopy.log, which Canopy appends to on every start and keeps
// open while the game runs. Only the last run is kept: the lines from the
// last one that says "Canopy starting".
public static class CanopyLog
{
    public const string FileName = "Canopy.log";

    // Logged by every Canopy build as it starts (src/Canopy/main.cpp).
    public const string StartMarker = "Canopy starting";

    // A log that has grown past this is read from its end only; a run is a
    // few hundred lines.
    public const int MaxBytes = 8 << 20;

    // A line with any of these, in any case, is a problem line.
    public static readonly IReadOnlyList<string> ProblemWords = ["warning", "skipped", "failed", "could not", "refused", "error"];

    public static CanopyLogRun Read(string gameFolder)
    {
        string path = PlayPaths.Log(gameFolder);
        if (!File.Exists(path))
        {
            return CanopyLogRun.None;
        }
        try
        {
            // Shared for reading and writing: the game may have the log open.
            using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            bool cut = stream.Length > MaxBytes;
            if (cut)
            {
                stream.Seek(-MaxBytes, SeekOrigin.End);
            }
            using MemoryStream bytes = new();
            stream.CopyTo(bytes);
            string text = new UTF8Encoding(false).GetString(bytes.GetBuffer(), 0, (int)bytes.Length);
            if (cut)
            {
                // The first line read is likely the end of a longer one.
                int newline = text.IndexOf('\n');
                text = newline < 0 ? "" : text[(newline + 1)..];
            }
            return new(true, LastRun(text), null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(true, [], $"{FileName} could not be read: {exception.Message}");
        }
    }

    // The lines of text from the last one containing StartMarker. A log with
    // no such line, such as one cut short, is kept whole.
    public static IReadOnlyList<string> LastRun(string text)
    {
        List<string> lines = text.Split('\n').Select(line => line.TrimEnd('\r')).ToList();
        if (lines.Count > 0 && lines[^1].Length == 0)
        {
            lines.RemoveAt(lines.Count - 1);
        }
        int start = lines.FindLastIndex(line => line.Contains(StartMarker, StringComparison.Ordinal));
        return start <= 0 ? lines : lines[start..];
    }

    public static bool IsProblem(string line) =>
        ProblemWords.Any(word => line.Contains(word, StringComparison.OrdinalIgnoreCase));

    public static IReadOnlyList<string> Problems(IEnumerable<string> lines) => lines.Where(IsProblem).ToList();
}
