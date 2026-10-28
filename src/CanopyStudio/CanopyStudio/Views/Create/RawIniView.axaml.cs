using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using AvaloniaEdit;
using AvaloniaEdit.Document;
using AvaloniaEdit.Rendering;
using CanopyStudio.Core.Create;
using CanopyStudio.ViewModels.Create;
using CanopyStudio.Views.Create.Check;

namespace CanopyStudio.Views.Create;

// The raw canopy.ini editor. Typing sets the project's text with this view
// as the source, so the view skips that change when it comes back; a change
// from a form is spliced in where the texts differ, so the caret and the
// scroll stay put. The view is bound to a model only while it is on screen.
public sealed partial class RawIniView : UserControl
{
    private readonly IniColorizer colorizer;
    private readonly ProblemMarkRenderer markRenderer;
    private RawIniViewModel? bound;
    private bool attached;
    // True while the view itself changes the editor's text.
    private bool applying;
    // The line whose messages the tooltip shows, 0 for none.
    private int tipLine;

    public RawIniView()
    {
        InitializeComponent();
        colorizer = new IniColorizer(Brush);
        markRenderer = new ProblemMarkRenderer(Brush);
        TextView textView = Editor.TextArea.TextView;
        textView.LineTransformers.Add(colorizer);
        textView.BackgroundRenderers.Add(markRenderer);
        textView.PointerMoved += OnPointerMoved;
        textView.PointerExited += (_, _) => HideTip();
        Editor.TextChanged += OnEditorTextChanged;
        // A theme swaps the token brushes: draw again with the new ones.
        ResourcesChanged += (_, _) => textView.Redraw();
        ActualThemeVariantChanged += (_, _) => textView.Redraw();
        DataContextChanged += (_, _) => Bind();
        AttachedToVisualTree += (_, _) =>
        {
            attached = true;
            Bind();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            attached = false;
            Bind();
        };
    }

    private void Bind()
    {
        RawIniViewModel? wanted = attached ? DataContext as RawIniViewModel : null;
        if (ReferenceEquals(wanted, bound))
        {
            return;
        }
        if (bound is not null)
        {
            bound.Project.Changed -= OnProjectChanged;
            bound.PropertyChanged -= OnModelPropertyChanged;
            bound.LineShown -= OnLineShown;
        }
        bound = wanted;
        if (bound is null)
        {
            return;
        }
        applying = true;
        try
        {
            Editor.Document.Text = bound.Project.Text;
            Editor.Document.UndoStack.ClearAll();
        }
        finally
        {
            applying = false;
        }
        markRenderer.SetMarks(bound.Marks);
        Editor.TextArea.TextView.Redraw();
        bound.Project.Changed += OnProjectChanged;
        bound.PropertyChanged += OnModelPropertyChanged;
        bound.LineShown += OnLineShown;
        if (bound.TakePendingLine() is > 0 and int line)
        {
            OnLineShown(bound, line);
        }
    }

    private void OnEditorTextChanged(object? sender, EventArgs e)
    {
        if (!applying && bound is not null)
        {
            bound.Project.SetText(Editor.Text, source: this);
        }
    }

    private void OnProjectChanged(object? sender, ProjectChangedEventArgs change)
    {
        if (ReferenceEquals(change.Source, this) || bound is null)
        {
            return;
        }
        Splice(bound.Project.Text);
    }

    // Replaces only the part of the editor's text that differs from text.
    private void Splice(string text)
    {
        TextDocument document = Editor.Document;
        string current = document.Text;
        if (current == text)
        {
            return;
        }
        int shorter = Math.Min(current.Length, text.Length);
        int prefix = 0;
        while (prefix < shorter && current[prefix] == text[prefix])
        {
            ++prefix;
        }
        int suffix = 0;
        while (suffix < shorter - prefix && current[^(suffix + 1)] == text[^(suffix + 1)])
        {
            ++suffix;
        }
        applying = true;
        try
        {
            document.Replace(prefix, current.Length - prefix - suffix, text.Substring(prefix, text.Length - prefix - suffix));
        }
        finally
        {
            applying = false;
        }
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (bound is not null && e.PropertyName == nameof(RawIniViewModel.Marks))
        {
            markRenderer.SetMarks(bound.Marks);
            Editor.TextArea.TextView.InvalidateLayer(markRenderer.Layer);
            HideTip();
        }
    }

    private void OnLineShown(object? sender, int line)
    {
        // After layout, so a view that was just shown has lines to scroll.
        Dispatcher.UIThread.Post(() =>
        {
            TextDocument document = Editor.Document;
            if (document.LineCount == 0)
            {
                return;
            }
            DocumentLine target = document.GetLineByNumber(Math.Clamp(line, 1, document.LineCount));
            Editor.Select(target.Offset, target.Length);
            Editor.ScrollToLine(target.LineNumber);
            Editor.TextArea.Focus();
        }, DispatcherPriority.Background);
    }

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        TextView textView = Editor.TextArea.TextView;
        TextViewPosition? position = textView.GetPositionFloor(e.GetPosition(textView) + textView.ScrollOffset);
        int line = position?.Line ?? 0;
        string? tip = line > 0 ? markRenderer.Tip(line) : null;
        if (tip is null)
        {
            HideTip();
            return;
        }
        if (line == tipLine)
        {
            return;
        }
        tipLine = line;
        ToolTip.SetTip(textView, tip);
        ToolTip.SetIsOpen(textView, true);
    }

    private void HideTip()
    {
        if (tipLine == 0)
        {
            return;
        }
        tipLine = 0;
        TextView textView = Editor.TextArea.TextView;
        ToolTip.SetIsOpen(textView, false);
        ToolTip.SetTip(textView, null);
    }

    private IBrush? Brush(string key) =>
        this.TryFindResource(key, ActualThemeVariant, out object? value) ? value as IBrush : null;
}
