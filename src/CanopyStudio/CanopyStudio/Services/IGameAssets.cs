using System.ComponentModel;
using Avalonia.Media.Imaging;
using CanopyStudio.Core.Assets;

namespace CanopyStudio.Services;

// The open game's names and icons, for pickers and grids. They are read from
// the user's install once per game build and cached under
// %APPDATA%\CanopyStudio\cache; Canopy Studio ships none of them. Every
// member may be used from the UI thread: the work runs off it.
public interface IGameAssets : INotifyPropertyChanged
{
    // True when the cache for the open game's build is complete.
    bool IsReady { get; }

    // The code (VanillaKinds.Languages) names are listed in. Setting it
    // raises Changed.
    string Language { get; set; }

    // The open game's entries of kind in Language, in table order. Empty
    // when no game is open or the game's tables cannot be read.
    Task<IReadOnlyList<VanillaEntry>> EntriesAsync(VanillaKind kind, CancellationToken cancel = default);

    // Icon iconId as a bitmap, shared and kept while Canopy Studio runs. Null
    // when no game is open or the game has no such icon.
    Task<Bitmap?> IconAsync(ulong iconId, CancellationToken cancel = default);

    // Builds the whole cache for the open game now: names in every language
    // and every icon the kinds use. progress gets a sentence per step.
    Task<AssetPrepareResult> PrepareAsync(IProgress<string>? progress = null, CancellationToken cancel = default);

    // The open game or Language changed: entries read before are stale.
    event EventHandler? Changed;
}
