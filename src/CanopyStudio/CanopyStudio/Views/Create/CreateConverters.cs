using Avalonia.Data.Converters;

namespace CanopyStudio.Views.Create;

// Small converters of the Create views. A problem's severity is "error",
// "warning" or "note", as ModDiagnostic says.
public static class CreateConverters
{
    public static readonly FuncValueConverter<string?, bool> IsError =
        new(severity => string.Equals(severity, "error", StringComparison.OrdinalIgnoreCase));

    public static readonly FuncValueConverter<string?, bool> IsWarning =
        new(severity => string.Equals(severity, "warning", StringComparison.OrdinalIgnoreCase));
}
