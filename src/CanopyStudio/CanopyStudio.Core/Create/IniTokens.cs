namespace CanopyStudio.Core.Create;

public enum IniTokenKind
{
    // A [section] header, brackets included.
    Section,
    // The key of a key = value line.
    Key,
    // The value of a key = value line, which runs to the line's end: the
    // loader has no comments after a value.
    Value,
    // A line starting with ; or #.
    Comment,
    // Anything else, which the loader skips.
    Other,
}

// A run of one line's text: Start and Length in characters of the line.
public readonly record struct IniToken(int Start, int Length, IniTokenKind Kind);

// How the raw ini view colours a line, by the rules src/CanopyCore/canopy_ini
// reads it with: the line is trimmed of spaces, tabs and line ends; empty
// lines are nothing; ; and # start comments; [...] is a section; the first
// '=' splits a key from its value; everything else is skipped by Canopy.
public static class IniTokens
{
    public static IReadOnlyList<IniToken> Line(string line)
    {
        int first = 0;
        while (first < line.Length && IsSpace(line[first]))
        {
            ++first;
        }
        int end = line.Length;
        while (end > first && IsSpace(line[end - 1]))
        {
            --end;
        }
        // A byte order mark at the very start is not part of the line.
        if (first < end && line[first] == '\uFEFF')
        {
            ++first;
        }
        if (first >= end)
        {
            return [];
        }
        char start = line[first];
        if (start is ';' or '#')
        {
            return [new IniToken(first, end - first, IniTokenKind.Comment)];
        }
        if (start == '[' && line[end - 1] == ']')
        {
            return [new IniToken(first, end - first, IniTokenKind.Section)];
        }
        int equals = line.IndexOf('=', first, end - first);
        if (equals < 0)
        {
            return [new IniToken(first, end - first, IniTokenKind.Other)];
        }
        List<IniToken> tokens = [];
        int keyEnd = equals;
        while (keyEnd > first && IsSpace(line[keyEnd - 1]))
        {
            --keyEnd;
        }
        if (keyEnd > first)
        {
            tokens.Add(new IniToken(first, keyEnd - first, IniTokenKind.Key));
        }
        int valueStart = equals + 1;
        while (valueStart < end && IsSpace(line[valueStart]))
        {
            ++valueStart;
        }
        if (valueStart < end)
        {
            tokens.Add(new IniToken(valueStart, end - valueStart, IniTokenKind.Value));
        }
        return tokens;
    }

    private static bool IsSpace(char c) => c is ' ' or '\t' or '\r' or '\n';
}
