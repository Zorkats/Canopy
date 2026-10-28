namespace CanopyStudio.Core;

// Moves that replace what was just written. A virus scanner or the search
// indexer often holds a new file for a moment, and Windows then refuses to
// delete, move or replace it, so each step is tried a few times before the
// error is let through.
internal static class BusyFiles
{
    private static readonly int[] Waits = [50, 100, 200, 400, 800];

    // Puts the finished staging folder in place of folder.
    public static void SwapFolder(string staging, string folder)
    {
        Retry(() =>
        {
            if (Directory.Exists(folder))
            {
                Directory.Delete(folder, recursive: true);
            }
        });
        Retry(() => Directory.Move(staging, folder));
    }

    // Moves temporary over path.
    public static void Replace(string temporary, string path) => Retry(() => File.Move(temporary, path, overwrite: true));

    private static void Retry(Action action)
    {
        for (int attempt = 0; ; ++attempt)
        {
            try
            {
                action();
                return;
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException && attempt < Waits.Length)
            {
                Thread.Sleep(Waits[attempt]);
            }
        }
    }
}
