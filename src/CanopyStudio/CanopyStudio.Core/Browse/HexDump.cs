using System.Text;

namespace CanopyStudio.Core.Browse;

public static class HexDump
{
    // 8 rather than the usual 16, so a line fits the row detail pane at any
    // window width.
    public const int BytesPerLine = 8;

    // hex (two digits a byte, as canopy_core.dll hands rows out) as lines
    // of an offset and 8 bytes: "0008  08 09 0a 0b 0c 0d 0e 0f".
    public static string Format(string hex)
    {
        StringBuilder text = new();
        int bytes = hex.Length / 2;
        for (int line = 0; line < bytes; line += BytesPerLine)
        {
            if (line > 0)
            {
                text.Append('\n');
            }
            text.Append(line.ToString("x4")).Append(' ');
            for (int at = line; at < Math.Min(line + BytesPerLine, bytes); ++at)
            {
                text.Append(' ');
                text.Append(hex, at * 2, 2);
            }
        }
        return text.ToString();
    }
}
