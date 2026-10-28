using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;

namespace CanopyStudio.ViewModels.Create.Check;

// One message of the last check, with the place in the project its line
// is, null when it is about no line in a section.
public sealed record ProblemRow(ModDiagnostic Diagnostic, ProblemPlace? Place)
{
    public int Line => Diagnostic.Line;
    public string Message => Diagnostic.Message;
    public bool IsError => Diagnostic.Severity == "error";
    public bool IsWarning => Diagnostic.Severity == "warning";
    public bool IsNote => !IsError && !IsWarning;

    public string SeverityText => IsError ? "Error" : IsWarning ? "Warning" : "Note";

    public string LineText => Line > 0 ? $"Line {Line}" : "";

    // Where it is, as the forms name it: [section] key.
    public string PlaceText => Place is null
        ? ""
        : Place.Key.Length == 0 ? $"[{Place.Section}]" : $"[{Place.Section}] {Place.Key}";

    public bool CanShow => Place is not null || Line > 0;
}
