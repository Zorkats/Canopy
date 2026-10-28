using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Identity;

public sealed record Season(string Id, string Code);

// The game's seasonal mountain scene, data/texture/fairy_bli_8100: parallax
// layers (sky, cloud bands, ranges, haze, foliage) that come in a spring, an
// autumn and a winter version. Stacked, each makes one still 16:9 picture.
public static class Backdrops
{
    public const int Width = 2000;
    public const int Height = 1125;

    // Each layer is 2048 pixels wide with an empty 24 pixel margin each side.
    private const int Margin = 24;

    public static IReadOnlyList<Season> Seasons { get; } =
        [new("spring", "spr"), new("autumn", "aut"), new("winter", "win")];

    // Back to front: the layer and the picture row its row 0 lands on. A
    // negative row crops the layer's top.
    private static readonly (string Layer, int Top)[] Stack =
    [
        ("02", -149),  // sky
        ("09", -40),   // high clouds
        ("08", 140),   // low clouds
        ("06", 40),    // far range
        ("10", 250),   // haze
        ("05", 250),   // middle range
        ("07", 160),   // near slopes
        ("03", 160),   // foliage
    ];

    public static string LayerPath(string layer, Season season) =>
        $"data/texture/fairy_bli_8100/bg_8100_{layer}_{season.Code}.nltx";

    public static IEnumerable<string> LayerPaths(Season season) =>
        Stack.Select(entry => LayerPath(entry.Layer, season));

    // The season's picture, opaque, on black. Null with error set when a
    // layer cannot be read.
    public static TextureImage? Compose(Season season, Func<string, (TextureImage? Image, string? Error)> load, out string? error)
    {
        byte[] canvas = new byte[Width * Height * 4];
        for (int i = 3; i < canvas.Length; i += 4)
        {
            canvas[i] = 255;
        }
        foreach ((string layer, int top) in Stack)
        {
            string path = LayerPath(layer, season);
            (TextureImage? image, string? loadError) = load(path);
            if (image is null)
            {
                error = $"{path}: {loadError ?? "it could not be read."}";
                return null;
            }
            Over(canvas, image, top);
        }
        error = null;
        return new TextureImage(Width, Height, canvas);
    }

    // Straight-alpha source over the opaque canvas.
    private static void Over(byte[] canvas, TextureImage layer, int top)
    {
        int columns = Math.Min(Width, layer.Width - Margin);
        int first = Math.Max(0, top);
        int last = Math.Min(Height, top + layer.Height);
        for (int y = first; y < last; ++y)
        {
            int source = ((y - top) * layer.Width + Margin) * 4;
            int target = y * Width * 4;
            for (int x = 0; x < columns * 4; x += 4)
            {
                int alpha = layer.Pixels[source + x + 3];
                if (alpha == 0)
                {
                    continue;
                }
                for (int c = 0; c < 3; ++c)
                {
                    int over = layer.Pixels[source + x + c];
                    int under = canvas[target + x + c];
                    canvas[target + x + c] = (byte)((over * alpha + under * (255 - alpha) + 127) / 255);
                }
            }
        }
    }
}
