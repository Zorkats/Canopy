namespace CanopyStudio.Core.Create;

// A whistle's notes, read as mod_rules::ParseMelody reads them
// (src/CanopyCore/rules_whistles.cpp): S or 0 is short, L or 1 is long,
// either case, spaces skipped, 2 to 10 notes.
public static class Melody
{
    public const int MinNotes = 2;
    public const int MaxNotes = 10;

    // How a note is drawn: a dot for short, a bar for long.
    public const string ShortSymbol = "•";
    public const string LongSymbol = "▬";

    // The notes of text, true for long, at any count; null when a character
    // is not a note.
    public static IReadOnlyList<bool>? Read(string? text)
    {
        List<bool> notes = [];
        foreach (char c in text ?? "")
        {
            switch (c)
            {
                case ' ':
                    continue;
                case 'S' or 's' or '0':
                    notes.Add(false);
                    break;
                case 'L' or 'l' or '1':
                    notes.Add(true);
                    break;
                default:
                    return null;
            }
        }
        return notes;
    }

    // The notes as a canopy.ini value: "S S L L".
    public static string Format(IEnumerable<bool> notes) => string.Join(' ', notes.Select(note => note ? 'L' : 'S'));

    // The notes as symbols, one per note.
    public static string Symbols(IEnumerable<bool> notes) =>
        string.Join(' ', notes.Select(note => note ? LongSymbol : ShortSymbol));

    // Why the loader would refuse text, or null when it takes it.
    public static string? Problem(string? text)
    {
        IReadOnlyList<bool>? notes = Read(text);
        if (notes is null)
        {
            return "Notes are S or 0 for short and L or 1 for long. This value has something else, so the editor shows it as it is.";
        }
        return notes.Count is >= MinNotes and <= MaxNotes
            ? null
            : $"A melody has {MinNotes} to {MaxNotes} notes, and this one has {notes.Count}.";
    }
}
