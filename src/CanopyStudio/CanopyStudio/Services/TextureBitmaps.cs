using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Services;

public static class TextureBitmaps
{
    // A bitmap of the decoded texture. Call on the UI thread.
    public static WriteableBitmap ToBitmap(TextureImage image)
    {
        WriteableBitmap bitmap = new(
            new PixelSize(image.Width, image.Height), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Unpremul);
        using ILockedFramebuffer frame = bitmap.Lock();
        int stride = image.Width * 4;
        for (int y = 0; y < image.Height; ++y)
        {
            Marshal.Copy(image.Pixels, y * stride, frame.Address + y * frame.RowBytes, stride);
        }
        return bitmap;
    }
}
