using CanopyStudio.Core.Game;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// The editor under the Text list: the picked entry, one box per language
// its field has. Each change goes into the edits mod at once as a patch,
// and GameEdits is told; saving is the edits mod bar's.
public sealed partial class TextEditorViewModel : ObservableObject
{
    private readonly GameEdits edits;
    private readonly IStudioNavigator navigator;
    private readonly Func<GameTextPatches> patches;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RevertAllCommand))]
    private bool isChanged;

    [ObservableProperty]
    private string? problem;

    public TextEditorViewModel(TextRowItem item, IReadOnlyList<TextLanguage> order, GameEdits edits,
        IStudioNavigator navigator, Func<GameTextPatches> patches)
    {
        Item = item;
        this.edits = edits;
        this.navigator = navigator;
        this.patches = patches;
        Languages = order
            .Where(language => item.Entry.Has(language.Index))
            .Select(language => new TextLanguageEditor(this, language))
            .ToList();
        Refresh();
    }

    public TextRowItem Item { get; }
    public GameTextEntry Entry => Item.Entry;
    public IReadOnlyList<TextLanguageEditor> Languages { get; }

    public string Title => $"{Entry.Field} of {Item.IdText}";
    public string Subtitle => Entry.Enum is { Length: > 0 } name
        ? $"{Entry.File}, row {Entry.Index}, {name}"
        : $"{Entry.File}, row {Entry.Index}";

    public bool HasMod => edits.Current is not null;
    public string NoModText => GameTextEdit.NoMod + " Saving is the bar's too.";

    // After the edits mod changed or another one was chosen.
    public void Refresh()
    {
        GameTextPatches now = patches();
        foreach (TextLanguageEditor language in Languages)
        {
            language.Refresh(edits.Current, now);
        }
        IsChanged = HasMod && now.Changes(Entry);
        OnPropertyChanged(nameof(HasMod));
    }

    [RelayCommand]
    private void OpenInTables() => navigator.OpenTable(Entry.File, Entry.Id);

    [RelayCommand(CanExecute = nameof(IsChanged))]
    private void RevertAll()
    {
        Problem = GameTextEdit.RevertAll(edits.Current, patches(), Entry);
        edits.NotifyEdited();
    }

    internal string? Commit(int language, string text)
    {
        string? reason = GameTextEdit.Commit(edits.Current, patches(), Entry, language, text);
        if (reason is null)
        {
            edits.NotifyEdited();
        }
        return reason;
    }

    internal string? Revert(int language)
    {
        string? reason = GameTextEdit.Revert(edits.Current, patches(), Entry, language);
        if (reason is null)
        {
            edits.NotifyEdited();
        }
        return reason;
    }

    internal void OpenLine(int line)
    {
        if (edits.Current is { } mod)
        {
            navigator.OpenProject(mod.Folder, line);
        }
    }
}

// One language of the picked entry: the vanilla text, and a box holding the
// edits mod's text when it sets one, else the vanilla text.
public sealed partial class TextLanguageEditor : ObservableObject
{
    private readonly TextEditorViewModel owner;
    private string shown = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsDirty))]
    [NotifyCanExecuteChangedFor(nameof(ApplyCommand))]
    private string draft = "";

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RevertCommand))]
    private bool isPatched;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsReadOnly))]
    [NotifyPropertyChangedFor(nameof(HasBlock))]
    [NotifyCanExecuteChangedFor(nameof(ApplyCommand))]
    [NotifyCanExecuteChangedFor(nameof(RevertCommand))]
    private string? blocked;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasProblem))]
    private string? problem;

    // The line of canopy.ini that sets the text in a form this page leaves
    // to Create, 0 for none.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasLine))]
    private int line;

    public TextLanguageEditor(TextEditorViewModel owner, TextLanguage language)
    {
        this.owner = owner;
        Language = language;
        Vanilla = owner.Entry.Values[language.Index] ?? "";
    }

    public TextLanguage Language { get; }
    public string Name => Language.Name;
    public string Vanilla { get; }
    public bool VanillaEmpty => Vanilla.Length == 0;
    public string VanillaShown => VanillaEmpty ? "(empty)" : Vanilla;

    public bool IsDirty => Draft != shown;
    public bool IsReadOnly => Blocked is not null;
    public bool HasProblem => Problem is not null;
    public bool HasLine => Line > 0;

    // Shown only with an edits mod; without one the editor says so once.
    public bool HasBlock => Blocked is not null && Blocked != GameTextEdit.NoMod;

    internal void Refresh(EditsMod? mod, GameTextPatches patches)
    {
        GameTextEntry entry = owner.Entry;
        int index = Language.Index;
        string now = patches.Shown(entry, index) ?? "";
        // A box being typed in keeps what was typed.
        bool typing = IsDirty;
        shown = now;
        if (!typing)
        {
            Draft = now;
        }
        OnPropertyChanged(nameof(IsDirty));
        ApplyCommand.NotifyCanExecuteChanged();
        PatchValue? value = mod is null ? null : patches.Find(entry, index);
        IsPatched = value is not null;
        Blocked = GameTextEdit.CannotEdit(mod, patches, entry, index);
        Line = mod is null ? 0
            : patches.EnumSection(entry)?.Line
              ?? (value is not null && !GameTextPatches.IsPlain(value, entry, index) ? value.Line : 0);
    }

    partial void OnDraftChanged(string value) =>
        Problem = value != Vanilla ? EditsMod.CannotHold(value) : null;

    private bool CanApply => IsDirty && Blocked is null;

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void Apply() => Problem = owner.Commit(Language.Index, Draft);

    private bool CanRevert => IsPatched && Blocked is null;

    [RelayCommand(CanExecute = nameof(CanRevert))]
    private void Revert()
    {
        // Back to vanilla in the box too, even when it was being typed in.
        string before = shown;
        shown = Draft;
        Problem = owner.Revert(Language.Index);
        if (Problem is not null)
        {
            shown = before;
            OnPropertyChanged(nameof(IsDirty));
        }
    }

    [RelayCommand]
    private void Reset() => Draft = shown;

    [RelayCommand]
    private void OpenLine() => owner.OpenLine(Line);
}
