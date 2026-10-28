using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Browse;

// One folder of the game's archives: its subfolders and the files right in
// it, both sorted by name.
public sealed class FolderNode(string name, string path)
{
    public string Name { get; } = name;

    // The folder with its trailing '/', "" for the top.
    public string Path { get; } = path;

    public List<FolderNode> Children { get; } = [];
    public List<GameFile> Files { get; } = [];

    // Every file in this folder and below.
    public int TotalFiles { get; internal set; }

    // For the tree view to keep open folders open.
    public bool IsExpanded { get; set; }
}

public static class FileTree
{
    // The top folder, named topName, holding everything.
    public static FolderNode Build(IEnumerable<GameFile> files, string topName)
    {
        FolderNode top = new(topName, "");
        Dictionary<string, FolderNode> folders = new(StringComparer.Ordinal) { [""] = top };
        foreach (GameFile file in files)
        {
            FolderOf(GamePaths.FolderOf(file.Path), folders).Files.Add(file);
        }
        Finish(top);
        return top;
    }

    private static FolderNode FolderOf(string path, Dictionary<string, FolderNode> folders)
    {
        if (folders.TryGetValue(path, out FolderNode? known))
        {
            return known;
        }
        // path ends in '/'; its parent is everything up to the '/' before.
        int cut = path.LastIndexOf('/', path.Length - 2) + 1;
        FolderNode parent = FolderOf(path[..cut], folders);
        FolderNode node = new(path[cut..^1], path);
        parent.Children.Add(node);
        folders.Add(path, node);
        return node;
    }

    private static int Finish(FolderNode node)
    {
        node.Children.Sort((a, b) => string.CompareOrdinal(a.Name, b.Name));
        node.Files.Sort((a, b) => string.CompareOrdinal(a.Path, b.Path));
        int total = node.Files.Count;
        foreach (FolderNode child in node.Children)
        {
            total += Finish(child);
        }
        node.TotalFiles = total;
        return total;
    }
}
