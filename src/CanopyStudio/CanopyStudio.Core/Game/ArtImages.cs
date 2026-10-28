using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Game;

// Small copies of decoded textures, for the Art list's thumbnails.
public static class ArtImages
{
    // image shrunk to fit maxSide x maxSide, keeping its shape; image itself
    // when it already fits. Each pixel averages the box of pixels it covers,
    // weighted by alpha so clear pixels do not darken the edges.
    public static TextureImage Shrink(TextureImage image, int maxSide)
    {
        if (image.Width <= maxSide && image.Height <= maxSide)
        {
            return image;
        }
        double scale = Math.Min((double)maxSide / image.Width, (double)maxSide / image.Height);
        int width = Math.Clamp((int)Math.Round(image.Width * scale), 1, maxSide);
        int height = Math.Clamp((int)Math.Round(image.Height * scale), 1, maxSide);
        byte[] pixels = new byte[width * height * 4];
        for (int y = 0; y < height; ++y)
        {
            int top = (int)((long)y * image.Height / height);
            int bottom = Math.Max(top + 1, (int)((long)(y + 1) * image.Height / height));
            for (int x = 0; x < width; ++x)
            {
                int left = (int)((long)x * image.Width / width);
                int right = Math.Max(left + 1, (int)((long)(x + 1) * image.Width / width));
                long b = 0, g = 0, r = 0, a = 0;
                for (int sy = top; sy < bottom; ++sy)
                {
                    int row = sy * image.Width * 4;
                    for (int sx = left; sx < right; ++sx)
                    {
                        int at = row + sx * 4;
                        int alpha = image.Pixels[at + 3];
                        b += image.Pixels[at] * alpha;
                        g += image.Pixels[at + 1] * alpha;
                        r += image.Pixels[at + 2] * alpha;
                        a += alpha;
                    }
                }
                int count = (bottom - top) * (right - left);
                int to = (y * width + x) * 4;
                if (a > 0)
                {
                    pixels[to] = (byte)(b / a);
                    pixels[to + 1] = (byte)(g / a);
                    pixels[to + 2] = (byte)(r / a);
                    pixels[to + 3] = (byte)(a / count);
                }
            }
        }
        return new TextureImage(width, height, pixels);
    }
}
