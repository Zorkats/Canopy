using System.Collections.ObjectModel;
using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Create;

// The form of one section, laid out from its kind's schema: one editor per
// field, in schema order, each made by FieldEditors. A setting shows only
// the keys its type uses, and follows the type as it changes. The fields the
// form shows are split into the main ones and "More options". Dispose it
// when another section opens: its editors listen to the project.
public sealed partial class SectionFormViewModel : ObservableObject, IDisposable
{
    private readonly IGameAssets assets;
    private readonly string? gameFolder;
    private readonly Func<string, string, IReadOnlyList<ModDiagnostic>> problemsAt;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSectionProblems))]
    private IReadOnlyList<ModDiagnostic> sectionProblems = [];

    // Whether "More options" is expanded.
    [ObservableProperty]
    private bool moreOpen;

    // The note about the keys the form does not edit, null when there are none.
    [ObservableProperty]
    private string? ignoredNote;

    public SectionFormViewModel(ModProject project, string section, SectionSchema kind, IGameAssets assets,
        string? gameFolder, Func<string, string, IReadOnlyList<ModDiagnostic>> problemsAt)
    {
        Project = project;
        Section = section;
        Kind = kind;
        this.assets = assets;
        this.gameFolder = gameFolder;
        this.problemsAt = problemsAt;
        SyncFields();
        RefreshProblems();
        // "More options" starts open when one of its fields has a value, a translation or a problem.
        MoreOpen = MoreFields.Any(field => !string.IsNullOrEmpty(field.Value)
            || FieldLabels.Translated(project, section, field.Key) > 0 || field.HasProblems);
        project.Changed += OnProjectChanged;
    }

    public ModProject Project { get; }
    public string Section { get; }
    public SectionSchema Kind { get; }

    public string Title => Kind.Title;
    // The section's key, or its whole name for a single kind such as [mod].
    public string Key => Kind.IsSingle ? Kind.Prefix : SectionNames.KeyOf(Section, Kind);
    public string DocUrl => CanopyDocs.Url(Kind.Doc);
    public Uri DocUri => new(DocUrl);
    public string DocText => $"Read {Path.GetFileName(Kind.Doc)} on GitHub";

    public bool IsSetting => SettingFields.IsSetting(Kind);
    public string? SettingNote => IsSetting ? "Only the keys this type of setting uses are shown." : null;

    // Every field with an editor, in schema order. FieldFor searches this list.
    public ObservableCollection<FieldViewModel> Fields { get; } = [];

    // The fields the form shows first, and the ones under "More options".
    public ObservableCollection<FieldViewModel> MainFields { get; } = [];
    public ObservableCollection<FieldViewModel> MoreFields { get; } = [];

    public bool HasMoreFields => MoreFields.Count > 0;

    public bool HasSectionProblems => SectionProblems.Count > 0;

    // The editor of key, or of the base key of a language key such as
    // name.en; null when the form does not show it.
    public FieldViewModel? FieldFor(string key) =>
        Fields.FirstOrDefault(field => SectionNames.Same(field.Key, key))
        ?? Fields.FirstOrDefault(field => SectionNames.Same(field.Key, LangKeys.BaseOf(key)));

    // Opens "More options" when field is one of its fields.
    public void Reveal(FieldViewModel field)
    {
        if (field.Place == FieldPlace.More)
        {
            MoreOpen = true;
        }
    }

    // Sets each editor's problems from the last check.
    public void RefreshProblems()
    {
        SectionProblems = problemsAt(Section, "");
        foreach (FieldViewModel field in Fields)
        {
            field.Problems = problemsAt(Section, field.Key);
        }
    }

    public void Dispose()
    {
        Project.Changed -= OnProjectChanged;
        foreach (FieldViewModel field in Fields)
        {
            field.Dispose();
        }
        Fields.Clear();
        MainFields.Clear();
        MoreFields.Clear();
    }

    // Adds and removes editors so Fields is the visible fields in schema
    // order. Editors that stay are kept, so the one being used keeps focus.
    // Then the main and more lists, and the ignored note, follow Fields.
    private void SyncFields()
    {
        IReadOnlyList<FieldSchema> visible = SettingFields.Visible(Kind, Project.Value(Section, SettingFields.TypeKey));
        foreach (FieldViewModel gone in Fields.Where(field => !visible.Contains(field.Context.Field)).ToList())
        {
            Fields.Remove(gone);
            gone.Dispose();
        }
        for (int index = 0; index < visible.Count; ++index)
        {
            if (index < Fields.Count && ReferenceEquals(Fields[index].Context.Field, visible[index]))
            {
                continue;
            }
            FieldViewModel field = FieldEditors.Create(new FieldContext(Project, Section, Kind, visible[index], assets,
                gameFolder));
            field.Problems = problemsAt(Section, field.Key);
            Fields.Insert(index, field);
        }
        CollectionSync.To(MainFields, Fields.Where(field => field.Place == FieldPlace.Main).ToList());
        CollectionSync.To(MoreFields, Fields.Where(field => field.Place == FieldPlace.More).ToList());
        IgnoredNote = IgnoredNoteOf(Fields.Where(field => field.Place == FieldPlace.Ignored).ToList());
        OnPropertyChanged(nameof(HasMoreFields));
    }

    // Names the keys the form does not edit, null when there are none.
    private static string? IgnoredNoteOf(IReadOnlyList<FieldViewModel> ignored) =>
        ignored.Count == 0
            ? null
            : $"Canopy does not read these keys here: {string.Join(", ", ignored.Select(field => field.Label))}.";

    private void OnProjectChanged(object? sender, ProjectChangedEventArgs change)
    {
        if (IsSetting)
        {
            SyncFields();
        }
    }
}
