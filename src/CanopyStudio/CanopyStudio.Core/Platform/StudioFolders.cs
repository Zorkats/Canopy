namespace CanopyStudio.Core.Platform;

// The folders Canopy Studio keeps its own files in, outside any game or mod
// folder. On Windows %APPDATA%\CanopyStudio; on Linux .NET maps the same
// call to $XDG_CONFIG_HOME, ~/.config by default, so ~/.config/CanopyStudio.
public static class StudioFolders
{
    public const string Name = "CanopyStudio";

    // The user's application data folder. GetFolderPath answers "" for a
    // folder that does not exist yet, as ~/.config may not on a new Linux
    // account, which would put Canopy Studio's files in the working folder;
    // the path is taken unchecked instead, and made when first written.
    public static string UserData() => Known(Environment.SpecialFolder.ApplicationData, ".config");

    // Canopy Studio's own folder below UserData.
    public static string Root() => Path.Combine(UserData(), Name);

    // The user's documents, ~/Documents or $XDG_DOCUMENTS_DIR on Linux.
    public static string Documents() => Known(Environment.SpecialFolder.MyDocuments, "Documents");

    private static string Known(Environment.SpecialFolder folder, string fallback)
    {
        string path = Environment.GetFolderPath(folder, Environment.SpecialFolderOption.DoNotVerify);
        return path.Length > 0
            ? path
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile, Environment.SpecialFolderOption.DoNotVerify), fallback);
    }
}
