using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CommunityToolkit.Mvvm.ComponentModel;

namespace CanopyStudio.ViewModels.Create;

// canopy.ini as text, in an editor kept in step with the forms. The Create
// area shows it beside or instead of the forms. The view edits Project
// directly; this holds what the view shows besides the text.
public sealed partial class RawIniViewModel(ModProject project) : ObservableObject
{
    // The line ShowLine asked for before a view was there to show it.
    private int pendingLine;

    public ModProject Project { get; } = project;

    // The last check's messages, which the view marks on their lines. Set by
    // the problems panel.
    [ObservableProperty]
    private IReadOnlyList<ModDiagnostic> marks = [];

    // ShowLine was called: the view scrolls to the line and selects it.
    public event EventHandler<int>? LineShown;

    // Brings line (from 1) into view and selects it.
    public void ShowLine(int line)
    {
        if (line <= 0)
        {
            return;
        }
        if (LineShown is null)
        {
            pendingLine = line;
            return;
        }
        pendingLine = 0;
        LineShown.Invoke(this, line);
    }

    // The line asked for while no view listened, 0 for none; taking it
    // clears it.
    public int TakePendingLine()
    {
        int line = pendingLine;
        pendingLine = 0;
        return line;
    }
}
