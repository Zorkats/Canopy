using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.ViewModels.Create.Check;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace CanopyStudio.ViewModels.Create;

// The live check of the project with Canopy's own rules, run as the text
// changes. GameFolder gives the open game, null when there is none. It
// marks the problem lines in raw. Errors and Warnings count the last
// check's messages.
//
// A check runs off the UI thread 400 ms after the last change, and once at
// start. A check that a newer one replaced is dropped when it ends, so the
// panel only ever shows the newest text's problems. Use it from the UI
// thread; Dispose stops it.
public sealed partial class ProblemsViewModel : ObservableObject, IDisposable
{
    private const int DelayMilliseconds = 400;

    private CancellationTokenSource? waiting;
    // The number of the newest check asked for.
    private int latest;
    private bool disposed;

    [ObservableProperty]
    private int errors;

    [ObservableProperty]
    private int warnings;

    // The last check's messages, in the order the rules gave them.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasRows))]
    private IReadOnlyList<ProblemRow> rows = [];

    [ObservableProperty]
    private ProblemRow? selected;

    // What the last check came to, in a sentence.
    [ObservableProperty]
    private string status = "Checking the project...";

    // Said when the last check had no game to check vanilla ids against.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasVanillaNote))]
    private string? vanillaNote;

    [ObservableProperty]
    private bool isChecking;

    public ProblemsViewModel(
        ModProject project, Func<string?> gameFolder, ICreateNavigator navigator, RawIniViewModel raw)
    {
        Project = project;
        GameFolder = gameFolder;
        Navigator = navigator;
        Raw = raw;
        Project.Changed += OnProjectChanged;
        Schedule(0);
    }

    public ModProject Project { get; }
    public Func<string?> GameFolder { get; }
    public ICreateNavigator Navigator { get; }
    public RawIniViewModel Raw { get; }

    public bool HasRows => Rows.Count > 0;
    public bool HasVanillaNote => VanillaNote is not null;

    // A check finished: forms re-read ProblemsAt for their fields.
    public event EventHandler? Checked;

    // The last check's messages about key of section, or about the section
    // itself when key is empty. A per-language key such as name.en counts
    // as its field, name.
    public IReadOnlyList<ModDiagnostic> ProblemsAt(string section, string key)
    {
        string field = key.Length == 0 ? "" : ProblemPlaces.FieldKey(key);
        return Rows.Where(row => row.Place is not null && row.Place.Is(section, field))
            .Select(row => row.Diagnostic).ToList();
    }

    // Checks again now, as after a save or a change of game.
    public void CheckNow() => Schedule(0);

    public void Dispose()
    {
        if (disposed)
        {
            return;
        }
        disposed = true;
        Project.Changed -= OnProjectChanged;
        waiting?.Cancel();
        waiting?.Dispose();
        waiting = null;
    }

    // Shows the message's field in its form, else its line in the raw view.
    [RelayCommand]
    private void Show(ProblemRow? row)
    {
        if (row is null)
        {
            return;
        }
        if (row.Place is { } place && Navigator.ShowField(place.Section, place.Key))
        {
            return;
        }
        if (row.Line > 0)
        {
            Navigator.ShowLine(row.Line);
        }
    }

    private void OnProjectChanged(object? sender, ProjectChangedEventArgs change) => Schedule(DelayMilliseconds);

    private void Schedule(int delay)
    {
        if (disposed)
        {
            return;
        }
        waiting?.Cancel();
        waiting?.Dispose();
        waiting = new CancellationTokenSource();
        _ = RunAsync(++latest, delay, waiting.Token);
    }

    private async Task RunAsync(int run, int delay, CancellationToken cancel)
    {
        try
        {
            if (delay > 0)
            {
                await Task.Delay(delay, cancel);
            }
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (run != latest || disposed)
        {
            return;
        }
        // What is checked is taken here, on the UI thread, so the lines of
        // the result match the structure they are placed with.
        string text = Project.Text;
        IniStructure structure = Project.Structure;
        string folder = Project.Folder;
        string? game = GameFolder();
        IsChecking = true;
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
        if (run != latest || disposed)
        {
            return;
        }
        IsChecking = false;
        Apply(report, ProblemPlaces.From(structure));
    }

    private void Apply(ValidationReport report, ProblemPlaces places)
    {
        IReadOnlyList<ModDiagnostic> diagnostics = report.Ok ? report.Diagnostics : [];
        Rows = diagnostics.Select(diagnostic => new ProblemRow(diagnostic, places.At(diagnostic.Line))).ToList();
        Errors = Rows.Count(row => row.IsError);
        Warnings = Rows.Count(row => row.IsWarning);
        Status = !report.Ok
            ? $"The check could not run: {report.Error ?? "canopy_core gave no reason."}"
            : (Errors, Warnings) switch
            {
                (0, 0) => "No problems found.",
                (_, 0) => $"{Count(Errors, "error")}.",
                (0, _) => $"{Count(Warnings, "warning")}.",
                _ => $"{Count(Errors, "error")} and {Count(Warnings, "warning")}.",
            };
        VanillaNote = report.Ok && !report.Vanilla
            ? "No game folder is set, so the ids of the game's own items, villagers, flags and other rows are not checked."
            : null;
        Raw.Marks = diagnostics;
        Checked?.Invoke(this, EventArgs.Empty);
    }

    private static string Count(int count, string noun) => count == 1 ? $"1 {noun}" : $"{count} {noun}s";
}
