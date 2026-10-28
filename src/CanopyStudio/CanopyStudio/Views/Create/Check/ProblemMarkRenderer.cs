using Avalonia;
using Avalonia.Media;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;
using CanopyStudio.Core.Native;

namespace CanopyStudio.Views.Create.Check;

// Underlines the lines the last check has messages about with a wavy line
// in the fail colour for an error, the accent for a warning, and the faint
// colour for a note. Tip gives a line's messages for its tooltip.
internal sealed class ProblemMarkRenderer(Func<string, IBrush?> brush) : IBackgroundRenderer
{
    private const double Wave = 2.5;

    private readonly Dictionary<int, Mark> marks = [];

    private sealed record Mark(int Rank, string Tip);

    public KnownLayer Layer => KnownLayer.Selection;

    public void SetMarks(IReadOnlyList<ModDiagnostic> diagnostics)
    {
        marks.Clear();
        foreach (ModDiagnostic diagnostic in diagnostics)
        {
            if (diagnostic.Line <= 0)
            {
                continue;
            }
            int rank = diagnostic.Severity switch
            {
                "error" => 2,
                "warning" => 1,
                _ => 0,
            };
            marks[diagnostic.Line] = marks.TryGetValue(diagnostic.Line, out Mark? mark)
                ? new Mark(Math.Max(rank, mark.Rank), mark.Tip + "\n" + diagnostic.Message)
                : new Mark(rank, diagnostic.Message);
        }
    }

    // The messages about line (from 1), null when there are none.
    public string? Tip(int line) => marks.TryGetValue(line, out Mark? mark) ? mark.Tip : null;

    public void Draw(TextView textView, DrawingContext drawingContext)
    {
        if (marks.Count == 0 || !textView.VisualLinesValid || textView.Document is not { } document)
        {
            return;
        }
        foreach (VisualLine visual in textView.VisualLines)
        {
            DocumentLine line = visual.FirstDocumentLine;
            if (!marks.TryGetValue(line.LineNumber, out Mark? mark) ||
                brush(mark.Rank switch { 2 => "StudioFailBrush", 1 => "StudioAccentBrush", _ => "StudioFaintBrush" })
                    is not { } stroke)
            {
                continue;
            }
            // The line's text without its leading and trailing spaces.
            string text = document.GetText(line);
            int start = text.Length - text.TrimStart().Length;
            int length = text.Trim().Length;
            TextSegment segment = new() { StartOffset = line.Offset + start, Length = Math.Max(length, 0) };
            Pen pen = new(stroke, 1);
            foreach (Rect rect in BackgroundGeometryBuilder.GetRectsForSegment(textView, segment))
            {
                double right = Math.Max(rect.Right, rect.Left + 8);
                drawingContext.DrawGeometry(null, pen, Wavy(rect.Left, right, rect.Bottom - 1));
            }
        }
    }

    private static StreamGeometry Wavy(double left, double right, double bottom)
    {
        StreamGeometry geometry = new();
        using StreamGeometryContext context = geometry.Open();
        context.BeginFigure(new Point(left, bottom), false);
        bool up = true;
        for (double x = left + Wave; x <= right; x += Wave)
        {
            context.LineTo(new Point(x, up ? bottom - Wave : bottom));
            up = !up;
        }
        context.EndFigure(false);
        return geometry;
    }
}
