using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using AvaloniaEdit.CodeCompletion;
using AvaloniaEdit.Document;
using AvaloniaEdit.Editing;
using CanopyStudio.Core.Lua;
using CanopyStudio.ViewModels.Create;
using CanopyStudio.Views.Create.Check;

namespace CanopyStudio.Views.Create;

// The Lua editor. Each file keeps its own document, so switching files
// keeps every file's caret and undo history; typing sets the file's text.
// The completion list opens after a '.' or ':', after two letters of a
// name, in canopy.on's event string, and on Ctrl+Space. The view is bound to
// a model only while it is on screen.
public sealed partial class LuaView : UserControl
{
    private readonly LuaColorizer colorizer;
    private readonly Dictionary<LuaFileViewModel, TextDocument> documents = new();
    private LuaViewModel? bound;
    private bool attached;
    // The model the documents belong to, kept while the view is off screen.
    private LuaViewModel? documentsFor;
    private LuaFileViewModel? shown;
    private CompletionWindow? completion;
    // True while the view itself changes the editor's document.
    private bool applying;

    public LuaView()
    {
        InitializeComponent();
        colorizer = new LuaColorizer(Brush);
        TextArea area = Editor.TextArea;
        area.TextView.LineTransformers.Add(colorizer);
        area.TextEntered += OnTextEntered;
        area.AddHandler(KeyDownEvent, OnEditorKeyDown, RoutingStrategies.Tunnel);
        Editor.TextChanged += OnEditorTextChanged;
        // A theme swaps the token brushes: draw again with the new ones.
        ResourcesChanged += (_, _) => area.TextView.Redraw();
        ActualThemeVariantChanged += (_, _) => area.TextView.Redraw();
        DataContextChanged += (_, _) => Bind();
        AttachedToVisualTree += (_, _) =>
        {
            attached = true;
            Bind();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            attached = false;
            completion?.Close();
            Bind();
        };
    }

    private void Bind()
    {
        LuaViewModel? wanted = attached ? DataContext as LuaViewModel : null;
        if (ReferenceEquals(wanted, bound))
        {
            return;
        }
        if (bound is not null)
        {
            bound.PropertyChanged -= OnModelPropertyChanged;
        }
        bound = wanted;
        if (bound is null)
        {
            return;
        }
        if (!ReferenceEquals(bound, documentsFor))
        {
            documents.Clear();
            documentsFor = bound;
        }
        bound.PropertyChanged += OnModelPropertyChanged;
        Show(bound.SelectedFile);
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (bound is not null && e.PropertyName == nameof(LuaViewModel.SelectedFile))
        {
            Show(bound.SelectedFile);
        }
    }

    // Puts file's document in the editor, making it from the file's text
    // the first time.
    private void Show(LuaFileViewModel? file)
    {
        completion?.Close();
        shown = file;
        applying = true;
        try
        {
            if (file is null)
            {
                Editor.Document = new TextDocument();
                Editor.IsReadOnly = true;
                return;
            }
            if (!documents.TryGetValue(file, out TextDocument? document))
            {
                document = new TextDocument(file.Text);
                document.UndoStack.ClearAll();
                documents[file] = document;
            }
            Editor.Document = document;
            Editor.IsReadOnly = file.IsReadOnly;
        }
        finally
        {
            applying = false;
        }
    }

    private void OnEditorTextChanged(object? sender, EventArgs e)
    {
        if (!applying && shown is { IsReadOnly: false } file)
        {
            file.Text = Editor.Text;
        }
    }

    private void OnEditorKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Space && e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            e.Handled = true;
            completion?.Close();
            OpenCompletion(always: true);
        }
    }

    private void OnTextEntered(object? sender, TextInputEventArgs e)
    {
        if (e.Text is not { Length: > 0 } text)
        {
            return;
        }
        char typed = text[^1];
        if (LuaTokens.IsNamePart(typed))
        {
            // An open list filters itself as the name grows.
            if (completion is null)
            {
                OpenCompletion(always: false);
            }
            return;
        }
        // Anything else ends the name being completed.
        completion?.Close();
        if (typed is '.' or ':' or '"' or '\'')
        {
            OpenCompletion(always: true);
        }
    }

    // Opens the list for the caret when there is something to offer. A name
    // typed at the top level opens it only from its second letter, and not
    // when it is already the only name offered.
    private void OpenCompletion(bool always)
    {
        if (bound is null || shown is not { IsReadOnly: false })
        {
            return;
        }
        int caret = Editor.CaretOffset;
        if (LuaCompletion.At(Editor.Text, caret, bound.Catalog, bound.UnsafeMod) is not { } result)
        {
            return;
        }
        if (!always)
        {
            bool member = result.Start > 0 && Editor.Document.GetCharAt(result.Start - 1) is '.' or ':';
            if (!member && result.Typed.Length < 2)
            {
                return;
            }
            if (result.Entries is [{ } only] && only.Leaf == result.Typed)
            {
                return;
            }
        }
        CompletionWindow window = new(Editor.TextArea)
        {
            StartOffset = result.Start,
            EndOffset = caret,
        };
        IList<ICompletionData> data = window.CompletionList.CompletionData;
        foreach (LuaEntry entry in result.Entries)
        {
            data.Add(new LuaCompletionData(entry));
        }
        window.Closed += (_, _) =>
        {
            if (ReferenceEquals(completion, window))
            {
                completion = null;
            }
        };
        completion = window;
        window.Show();
        if (result.Typed.Length > 0)
        {
            window.CompletionList.SelectItem(result.Typed);
        }
    }

    private IBrush? Brush(string key) =>
        this.TryFindResource(key, ActualThemeVariant, out object? value) ? value as IBrush : null;

    // One name in the completion list: the name, and its signature and
    // documentation beside it.
    private sealed class LuaCompletionData(LuaEntry entry) : ICompletionData
    {
        public IImage? Image => null;
        public string Text => entry.Leaf;
        public object Content => entry.Leaf;
        public object Description => entry.Signature is { Length: > 0 } signature
            ? $"{signature}\n\n{entry.Doc}"
            : entry.Doc;
        public double Priority => 0;

        public void Complete(TextArea textArea, ISegment completionSegment, EventArgs insertionRequestEventArgs) =>
            textArea.Document.Replace(completionSegment, Text);
    }
}
