using CanopyStudio.Core.Game;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Game;

// One texture of the Art list. Its size is the decoded file's, known once
// its thumbnail or the viewer has decoded it: vanilla rows record none.
public sealed partial class ArtRowViewModel(ArtTexture texture) : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SizeText))]
    private int width = texture.Width;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SizeText))]
    private int height = texture.Height;

    // The edits mod's replacement of this row, null when it has none.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsReplaced))]
    private ArtReplacement? replacement;

    public ArtTexture Texture { get; } = texture;
    public ulong Id => Texture.Id;
    public string Name => Texture.Enum ?? "";
    public string File => Texture.File;
    public bool HasFile => File.Length > 0;
    public bool IsReplaced => Replacement is not null;

    // The enum name, or the id when the row has none.
    public string Title => Texture.Enum is { Length: > 0 } name ? name : $"Texture {Id}";

    public string SizeText => Width > 0 && Height > 0 ? $"{Width} x {Height}" : "";

    public void Saw(int decodedWidth, int decodedHeight)
    {
        Width = decodedWidth;
        Height = decodedHeight;
    }
}
