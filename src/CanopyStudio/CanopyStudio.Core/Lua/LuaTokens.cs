namespace CanopyStudio.Core.Lua;

public enum LuaTokenKind
{
    Keyword,
    Name,
    Number,
    // A quoted or long-bracket string, its quotes or brackets included.
    String,
    // A -- comment to the line's end, or a --[[ ]] comment.
    Comment,
}

// A run of a Lua source: Start and Length in characters of the whole text.
// Closed is false for a string or comment the text ends inside (a line
// comment with no line end after it), or a quoted string a line end cuts.
public readonly record struct LuaToken(int Start, int Length, LuaTokenKind Kind, bool Closed = true)
{
    public int End => Start + Length;
}

// The tokens the Lua editor colours and completes by, read the way Lua 5.3's
// lexer reads them. Operators and spaces are not tokens.
public static class LuaTokens
{
    public static readonly IReadOnlySet<string> Keywords = new HashSet<string>(StringComparer.Ordinal)
    {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
        "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
    };

    public static IReadOnlyList<LuaToken> Scan(string text)
    {
        List<LuaToken> tokens = [];
        int at = 0;
        while (at < text.Length)
        {
            char c = text[at];
            if (c == '-' && At(text, at + 1) == '-')
            {
                int level = LongBracket(text, at + 2);
                if (level >= 0)
                {
                    tokens.Add(LongRun(text, at, at + 2, level, LuaTokenKind.Comment));
                }
                else
                {
                    int end = LineEnd(text, at);
                    tokens.Add(new LuaToken(at, end - at, LuaTokenKind.Comment, Closed: end < text.Length));
                }
            }
            else if (c == '[' && LongBracket(text, at) is var level and >= 0)
            {
                tokens.Add(LongRun(text, at, at, level, LuaTokenKind.String));
            }
            else if (c is '"' or '\'')
            {
                tokens.Add(Quoted(text, at));
            }
            else if (char.IsAsciiDigit(c) || (c == '.' && char.IsAsciiDigit(At(text, at + 1))))
            {
                tokens.Add(new LuaToken(at, NumberEnd(text, at) - at, LuaTokenKind.Number));
            }
            else if (IsNameStart(c))
            {
                int end = at + 1;
                while (end < text.Length && IsNamePart(text[end]))
                {
                    ++end;
                }
                bool keyword = Keywords.Contains(text[at..end]);
                tokens.Add(new LuaToken(at, end - at, keyword ? LuaTokenKind.Keyword : LuaTokenKind.Name));
            }
            else
            {
                ++at;
                continue;
            }
            at = tokens[^1].End;
        }
        return tokens;
    }

    public static bool IsNameStart(char c) => char.IsAsciiLetter(c) || c == '_';

    public static bool IsNamePart(char c) => char.IsAsciiLetterOrDigit(c) || c == '_';

    private static char At(string text, int index) => index < text.Length ? text[index] : '\0';

    private static int LineEnd(string text, int from)
    {
        int end = text.IndexOfAny(['\r', '\n'], from);
        return end < 0 ? text.Length : end;
    }

    // The level of the long bracket [==[ opening at, or -1 when there is none.
    private static int LongBracket(string text, int at)
    {
        if (At(text, at) != '[')
        {
            return -1;
        }
        int level = 0;
        while (At(text, at + 1 + level) == '=')
        {
            ++level;
        }
        return At(text, at + 1 + level) == '[' ? level : -1;
    }

    // A long string or comment from start, its bracket opening at open, to
    // the matching ]==] or the text's end.
    private static LuaToken LongRun(string text, int start, int open, int level, LuaTokenKind kind)
    {
        string close = "]" + new string('=', level) + "]";
        int found = text.IndexOf(close, open + level + 2, StringComparison.Ordinal);
        return found < 0
            ? new LuaToken(start, text.Length - start, kind, Closed: false)
            : new LuaToken(start, found + close.Length - start, kind);
    }

    // A quoted string: a backslash escapes the next character, a line end it
    // does not escape cuts the string.
    private static LuaToken Quoted(string text, int start)
    {
        char quote = text[start];
        int at = start + 1;
        while (at < text.Length)
        {
            char c = text[at];
            if (c == quote)
            {
                return new LuaToken(start, at + 1 - start, LuaTokenKind.String);
            }
            if (c is '\r' or '\n')
            {
                break;
            }
            at += c == '\\' && at + 1 < text.Length ? 2 : 1;
        }
        return new LuaToken(start, Math.Min(at, text.Length) - start, LuaTokenKind.String, Closed: false);
    }

    // Where a number from start ends: hex with 0x, else decimal, with a
    // fraction and an exponent (p for hex) each with a sign.
    private static int NumberEnd(string text, int start)
    {
        bool hex = text[start] == '0' && At(text, start + 1) is 'x' or 'X';
        int at = hex ? start + 2 : start;
        char exponent = hex ? 'p' : 'e';
        while (at < text.Length)
        {
            char c = char.ToLowerInvariant(text[at]);
            if (c == exponent && At(text, at + 1) is '+' or '-')
            {
                at += 2;
            }
            else if (char.IsAsciiDigit(c) || c == '.' || c == exponent || (hex && c is >= 'a' and <= 'f'))
            {
                ++at;
            }
            else
            {
                break;
            }
        }
        return at;
    }
}
