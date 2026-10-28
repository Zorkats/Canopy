using Avalonia.Media;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;
using CanopyStudio.Core.Create;

namespace CanopyStudio.Views.Create.Check;

// Colours canopy.ini by IniTokens, with brushes the view looks up from the
// theme tokens on every pass, so a theme change shows on the next redraw.
internal sealed class IniColorizer(Func<string, IBrush?> brush) : DocumentColorizingTransformer
{
    protected override void ColorizeLine(DocumentLine line)
    {
        string text = CurrentContext.Document.GetText(line);
        foreach (IniToken token in IniTokens.Line(text))
        {
            IBrush? foreground = brush(BrushKey(token.Kind));
            if (foreground is null)
            {
                continue;
            }
            int start = line.Offset + token.Start;
            IniTokenKind kind = token.Kind;
            ChangeLinePart(start, start + token.Length, element =>
            {
                element.TextRunProperties.SetForegroundBrush(foreground);
                Typeface typeface = element.TextRunProperties.Typeface;
                if (kind == IniTokenKind.Section)
                {
                    element.TextRunProperties.SetTypeface(
                        new Typeface(typeface.FontFamily, typeface.Style, FontWeight.SemiBold));
                }
                else if (kind == IniTokenKind.Comment)
                {
                    element.TextRunProperties.SetTypeface(
                        new Typeface(typeface.FontFamily, FontStyle.Italic, typeface.Weight));
                }
            });
        }
    }

    // Sections take the accent, keys are quieter than their values,
    // comments fade, and a line Canopy skips shows in the fail colour.
    private static string BrushKey(IniTokenKind kind) => kind switch
    {
        IniTokenKind.Section => "StudioAccentBrush",
        IniTokenKind.Key => "StudioMutedBrush",
        IniTokenKind.Value => "StudioTextBrush",
        IniTokenKind.Comment => "StudioFaintBrush",
        _ => "StudioFailBrush",
    };
}
