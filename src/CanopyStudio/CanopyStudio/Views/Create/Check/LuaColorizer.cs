using Avalonia.Media;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;
using CanopyStudio.Core.Lua;

namespace CanopyStudio.Views.Create.Check;

// Colours Lua by LuaTokens, with brushes the view looks up from the theme
// tokens on every pass, so a theme change shows on the next redraw. Strings
// and comments can span lines, so the whole document is scanned once per
// change and each line takes the tokens that cross it.
internal sealed class LuaColorizer(Func<string, IBrush?> brush) : DocumentColorizingTransformer
{
    private TextDocument? scanned;
    private ITextSourceVersion? version;
    private List<LuaToken> tokens = [];

    protected override void ColorizeLine(DocumentLine line)
    {
        TextDocument document = CurrentContext.Document;
        if (!ReferenceEquals(document, scanned) || !ReferenceEquals(document.Version, version))
        {
            scanned = document;
            version = document.Version;
            tokens = LuaTokens.Scan(document.Text).ToList();
        }
        for (int index = FirstEndingAfter(line.Offset); index < tokens.Count; ++index)
        {
            LuaToken token = tokens[index];
            if (token.Start >= line.EndOffset)
            {
                break;
            }
            if (BrushKey(token.Kind) is not { } key || brush(key) is not { } foreground)
            {
                continue;
            }
            int start = Math.Max(token.Start, line.Offset);
            int end = Math.Min(token.End, line.EndOffset);
            if (end <= start)
            {
                continue;
            }
            LuaTokenKind kind = token.Kind;
            ChangeLinePart(start, end, element =>
            {
                element.TextRunProperties.SetForegroundBrush(foreground);
                Typeface typeface = element.TextRunProperties.Typeface;
                if (kind == LuaTokenKind.Keyword)
                {
                    element.TextRunProperties.SetTypeface(
                        new Typeface(typeface.FontFamily, typeface.Style, FontWeight.SemiBold));
                }
                else if (kind == LuaTokenKind.Comment)
                {
                    element.TextRunProperties.SetTypeface(
                        new Typeface(typeface.FontFamily, FontStyle.Italic, typeface.Weight));
                }
            });
        }
    }

    // The first token that ends after offset; tokens do not overlap, so
    // their ends rise with their starts.
    private int FirstEndingAfter(int offset)
    {
        int low = 0;
        int high = tokens.Count;
        while (low < high)
        {
            int middle = (low + high) / 2;
            if (tokens[middle].End <= offset)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        return low;
    }

    // Keywords take the accent, strings the pass colour, numbers a softer
    // accent, and comments fade. Names keep the text colour.
    private static string? BrushKey(LuaTokenKind kind) => kind switch
    {
        LuaTokenKind.Keyword => "StudioAccentBrush",
        LuaTokenKind.String => "StudioPassBrush",
        LuaTokenKind.Number => "StudioAccent80Brush",
        LuaTokenKind.Comment => "StudioFaintBrush",
        _ => null,
    };
}
