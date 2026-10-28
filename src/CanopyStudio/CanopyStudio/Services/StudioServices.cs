namespace CanopyStudio.Services;

// Moving between pages, for pages that link to each other.
public interface IStudioNavigator
{
    void Navigate(string pageId);

    // The Tables page on file, with the row whose id is rowId selected.
    void OpenTable(string file, ulong? rowId = null);

    // The Create area on the mod in folder, with canopy.ini's line (from 1)
    // shown when it is not 0.
    void OpenProject(string folder, int line = 0);

    // Shows first-time setup over the window, from its first step.
    void RunOnboarding();
}

// The window's own dialogs.
public interface IStudioDialogs
{
    // The local path of the folder picked, or null when cancelled.
    Task<string?> PickFolderAsync(string title);

    // The local paths of the files picked, empty when cancelled. kind names
    // the files, such as "Mod zips"; patterns filter them, such as "*.zip".
    Task<IReadOnlyList<string>> PickFilesAsync(string title, string kind, IReadOnlyList<string> patterns,
        bool many = false);

    // Asks before something that cannot be taken back. True when the user
    // pressed confirm, a button label such as "Remove".
    Task<bool> ConfirmAsync(string title, string message, string confirm);

    // Puts text on the clipboard. False when the clipboard could not be used.
    Task<bool> CopyTextAsync(string text);
}

public static class PageIds
{
    public const string Home = "home";
    public const string Play = "play";
    public const string Create = "create";
    public const string Game = "game";
    // Sections of the Game area.
    public const string Files = "files";
    public const string Tables = "tables";
    public const string Lists = "lists";
    public const string Text = "text";
    public const string Art = "art";
    public const string Sound = "sound";
    public const string Setup = "setup";
}
