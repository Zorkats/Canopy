using CanopyStudio.Core.Create;

namespace CanopyStudio.ViewModels.Create.Simple;

// A file inside the mod folder, typed or picked. A picked file outside the
// folder is refused: the loader reads only the mod's own files.
public sealed class PathFieldViewModel(FieldContext context) : TextFieldViewModel(context)
{
    private static readonly string[] Images = ["*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff", "*.gif", "*.nltx"];

    public string Folder => Context.Project.Folder;

    // Lua scripts or images, from what the field's help asks for; anything
    // else takes any file.
    public bool IsScript => Help.Contains(".lua", StringComparison.OrdinalIgnoreCase);
    public bool IsImage => !IsScript && Help.Contains("image", StringComparison.OrdinalIgnoreCase);

    public string FileKind => IsScript ? "Lua scripts" : IsImage ? "Images" : "All files";

    public IReadOnlyList<string> Patterns => IsScript ? ["*.lua"] : IsImage ? Images : ["*"];

    public override string Watermark => Default ?? (IsScript ? "For example scripts/main.lua" : "A path inside the mod folder");

    // Writes the picked file as its path inside the mod folder.
    public void UseFile(string file)
    {
        string? relative = ProjectPaths.Relative(Folder, file);
        if (relative is null)
        {
            Error = $"{Path.GetFileName(file)} is not inside the mod folder. Copy it into {Folder} first, then pick it there.";
            return;
        }
        Text = relative;
    }

    protected override string? Check(string value)
    {
        string text = value.Trim();
        if (text.Length == 0)
        {
            return null;
        }
        string? full = ProjectPaths.Full(Folder, text);
        if (full is null)
        {
            return "This is not a path inside the mod folder. Write it relative to the folder, such as art/picture.png.";
        }
        return File.Exists(full) ? null : $"There is no file at {text} in the mod folder yet.";
    }
}
