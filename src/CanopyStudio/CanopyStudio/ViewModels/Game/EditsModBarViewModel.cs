using System.ComponentModel;
using CanopyStudio.Core.Game;
using CanopyStudio.Core.Native;
using CanopyStudio.Services;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Game;

// The bar over the Game area's editing sections: which mod edits are saved
// to, choosing or making one, and saving. After a save the mod is checked
// with Canopy's own rules, as Create checks a project, so a value the
// loader would refuse shows at once.
public sealed partial class EditsModBarViewModel : ObservableObject
{
    // How many of the check's messages the bar lists.
    private const int ShownProblems = 3;

    private int checkRun;

    // True while the chooser (the mods there are, and a new one) is open.
    [ObservableProperty]
    private bool isChoosing;

    // Set when an edit was tried with no mod chosen.
    [ObservableProperty]
    private bool isAsked;

    [ObservableProperty]
    private IReadOnlyList<string> candidates = [];

    [ObservableProperty]
    private string? selectedCandidate;

    [ObservableProperty]
    private string newName = "";

    // Why the last choose, make, save or stop did not happen.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasActionError))]
    private string? actionError;

    // What the check after the last save found, in a sentence, and its
    // first messages.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCheck))]
    private string? checkSummary;

    [ObservableProperty]
    private IReadOnlyList<string> checkLines = [];

    [ObservableProperty]
    private bool checkFailed;

    [ObservableProperty]
    private bool isChecking;

    public EditsModBarViewModel(GameEdits edits, IStudioDialogs dialogs)
    {
        Edits = edits;
        Dialogs = dialogs;
        edits.Edited += (_, _) => Refresh();
        edits.PropertyChanged += OnEditsChanged;
    }

    public GameEdits Edits { get; }
    public IStudioDialogs Dialogs { get; }

    public bool HasMod => Edits.Current is not null;
    public bool NoMod => Edits.Current is null;
    public string ModName => Edits.Current?.Name ?? "";
    public string ModFolder => Edits.Current?.Folder ?? "";
    public bool IsDirty => Edits.Current?.IsDirty ?? false;
    public string? Problem => Edits.Problem;
    public bool HasProblem => Edits.Problem is not null;
    public bool HasActionError => ActionError is not null;
    public bool HasCheck => CheckSummary is not null;
    public bool HasCandidates => Candidates.Count > 0;

    public string Prompt => IsAsked
        ? "Choose an edits mod to save that edit to: one of your mods, or a new one."
        : "No edits mod chosen. Choose or make one, and edits in Tables, Text, Art and Sound are saved to it as patches.";

    // Opens the chooser because an edit needs a mod.
    public void Ask()
    {
        IsAsked = true;
        OpenChooser();
    }

    [RelayCommand]
    private void OpenChooser()
    {
        Candidates = Edits.Candidates();
        SelectedCandidate = Edits.Current is { } mod && Candidates.Contains(mod.Name) ? mod.Name : null;
        NewName = "";
        ActionError = null;
        IsChoosing = true;
    }

    [RelayCommand]
    private void CloseChooser()
    {
        IsChoosing = false;
        IsAsked = false;
        ActionError = null;
    }

    [RelayCommand]
    private async Task ChooseAsync()
    {
        if (SelectedCandidate is not { } name)
        {
            ActionError = "Pick a mod in the list first.";
            return;
        }
        if (Edits.Current is { } mod && string.Equals(mod.Name, name, StringComparison.OrdinalIgnoreCase))
        {
            CloseChooser();
            return;
        }
        if (!await LeaveUnsavedAsync())
        {
            return;
        }
        Finish(Edits.Choose(name));
    }

    [RelayCommand]
    private async Task CreateAsync()
    {
        string name = NewName.Trim();
        if (name.Length == 0)
        {
            ActionError = "Type a name for the new mod first.";
            return;
        }
        if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name is "." or "..")
        {
            ActionError = $"{name} cannot be a folder name. Use letters, digits, spaces, '_' or '-'.";
            return;
        }
        if (!await LeaveUnsavedAsync())
        {
            return;
        }
        Finish(Edits.Create(name));
    }

    [RelayCommand]
    private async Task StopAsync()
    {
        if (!await LeaveUnsavedAsync())
        {
            return;
        }
        Edits.Clear();
        ActionError = null;
    }

    [RelayCommand(CanExecute = nameof(IsDirty))]
    private async Task SaveAsync()
    {
        if (Edits.Current is not { } mod)
        {
            return;
        }
        if (mod.Save() is { } error)
        {
            ActionError = $"{mod.Name} could not be saved. {error}";
            return;
        }
        ActionError = null;
        Edits.NotifyEdited();
        await CheckAsync(mod);
    }

    // Runs the project check on the saved mod, against the game it is in.
    private async Task CheckAsync(EditsMod mod)
    {
        int run = ++checkRun;
        string folder = mod.Folder;
        string text = mod.Project.Text;
        string? game = Edits.ModsFolder is { } mods ? Path.GetDirectoryName(mods) : null;
        IsChecking = true;
        CheckSummary = "Checking the mod...";
        CheckLines = [];
        CheckFailed = false;
        ValidationReport report;
        try
        {
            report = await Task.Run(() => CanopyCore.ValidateProject(game, null, folder, text));
        }
        catch (Exception exception) when (exception is DllNotFoundException or BadImageFormatException)
        {
            report = new ValidationReport
            {
                Error = CanopyCore.Check() ?? $"{CanopyCore.LibraryFile} could not be loaded. Build the C++ tools first.",
            };
        }
        if (run != checkRun || !ReferenceEquals(mod, Edits.Current))
        {
            return;
        }
        IsChecking = false;
        if (!report.Ok)
        {
            CheckSummary = $"Saved, but the check could not run: {report.Error ?? "canopy_core gave no reason."}";
            CheckFailed = true;
            return;
        }
        List<ModDiagnostic> problems = report.Diagnostics.Where(diagnostic => diagnostic.Severity is "error" or "warning").ToList();
        int errors = problems.Count(diagnostic => diagnostic.Severity == "error");
        int warnings = problems.Count - errors;
        CheckSummary = (errors, warnings) switch
        {
            (0, 0) => "Saved. The check found no problems.",
            (_, 0) => $"Saved. The check found {Count(errors, "error")}; Canopy skips those lines.",
            (0, _) => $"Saved. The check found {Count(warnings, "warning")}.",
            _ => $"Saved. The check found {Count(errors, "error")} and {Count(warnings, "warning")}; Canopy skips the lines with errors.",
        };
        CheckFailed = errors > 0;
        CheckLines = problems.Take(ShownProblems)
            .Select(diagnostic => diagnostic.Line > 0 ? $"Line {diagnostic.Line}: {diagnostic.Message}" : diagnostic.Message)
            .ToList();
    }

    // True when nothing unsaved would be lost, or the user lets it go.
    private async Task<bool> LeaveUnsavedAsync()
    {
        if (Edits.Current is not { IsDirty: true } mod)
        {
            return true;
        }
        return await Dialogs.ConfirmAsync("Leave unsaved edits?",
            $"{mod.Name} has edits that are not saved. They are lost if you switch now. Save first to keep them.",
            "Leave them");
    }

    private void Finish(string? error)
    {
        if (error is not null)
        {
            ActionError = error;
            return;
        }
        CloseChooser();
    }

    private void OnEditsChanged(object? sender, PropertyChangedEventArgs change)
    {
        if (change.PropertyName == nameof(GameEdits.Current))
        {
            ++checkRun;
            CheckSummary = null;
            CheckLines = [];
            IsChecking = false;
            if (Edits.Current is not null)
            {
                IsAsked = false;
            }
        }
        Refresh();
    }

    private void Refresh()
    {
        foreach (string name in (string[])[nameof(HasMod), nameof(NoMod), nameof(ModName), nameof(ModFolder),
                     nameof(IsDirty), nameof(Problem), nameof(HasProblem), nameof(Prompt)])
        {
            OnPropertyChanged(name);
        }
        SaveCommand.NotifyCanExecuteChanged();
    }

    partial void OnIsAskedChanged(bool value) => OnPropertyChanged(nameof(Prompt));

    partial void OnCandidatesChanged(IReadOnlyList<string> value) => OnPropertyChanged(nameof(HasCandidates));

    private static string Count(int count, string noun) => count == 1 ? $"1 {noun}" : $"{count} {noun}s";
}
