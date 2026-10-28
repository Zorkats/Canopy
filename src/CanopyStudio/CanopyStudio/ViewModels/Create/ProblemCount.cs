namespace CanopyStudio.ViewModels.Create;

// The words for a count of problems: "2 errors, 1 warning", with a zero part
// left out, or "No problems." when there are neither errors nor warnings.
public static class ProblemCount
{
    public static string Summary(int errors, int warnings)
    {
        List<string> parts = [];
        if (errors > 0)
        {
            parts.Add(Plural(errors, "error", "errors"));
        }
        if (warnings > 0)
        {
            parts.Add(Plural(warnings, "warning", "warnings"));
        }
        return parts.Count == 0 ? "No problems." : string.Join(", ", parts);
    }

    private static string Plural(int count, string one, string many) => $"{count} {(count == 1 ? one : many)}";
}
