using System.Text.RegularExpressions;

namespace CanopyStudio.Core.Lua;

// What to offer at a caret: the entries, and the run of text from Start to
// the caret, Typed, that picking one replaces.
public sealed record LuaCompletionResult(int Start, string Typed, IReadOnlyList<LuaEntry> Entries);

// Completion in the Lua editor. After a dotted name and a dot (canopy.items.)
// it offers that name's members; elsewhere the top-level names, without the
// guarded game commands unless the mod is unsafe; inside the string that
// opens canopy.on( it offers the events. Inside other strings, comments and
// numbers it offers nothing.
public static partial class LuaCompletion
{
    public static LuaCompletionResult? At(string text, int caret, LuaCatalog catalog, bool unsafeMod)
    {
        caret = Math.Clamp(caret, 0, text.Length);
        foreach (LuaToken token in LuaTokens.Scan(text[..caret]))
        {
            // The text stops at the caret, so only a token it ends inside
            // holds the caret.
            if (token.Kind is not (LuaTokenKind.String or LuaTokenKind.Comment) || token.End < caret || token.Closed)
            {
                continue;
            }
            return token.Kind == LuaTokenKind.String ? EventsAt(text, token, caret, catalog) : null;
        }

        int start = caret;
        while (start > 0 && LuaTokens.IsNamePart(text[start - 1]))
        {
            --start;
        }
        string typed = text[start..caret];
        if (typed.Length > 0 && char.IsAsciiDigit(typed[0]))
        {
            return null;
        }
        IEnumerable<LuaEntry> offered;
        if (start > 0 && text[start - 1] is '.' or ':')
        {
            // a..b joins strings; anything but a plain dotted name before
            // the dot has no members Canopy knows.
            if ((start > 1 && text[start - 2] == '.') || OwnerBefore(text, start - 1) is not { } owner)
            {
                return null;
            }
            offered = catalog.Members(owner);
        }
        else
        {
            offered = catalog.Members("").Where(entry => unsafeMod || entry.Kind != LuaEntryKind.Guarded);
        }
        List<LuaEntry> entries = offered
            .Where(entry => entry.Leaf.StartsWith(typed, StringComparison.OrdinalIgnoreCase))
            .ToList();
        return entries.Count == 0 ? null : new LuaCompletionResult(start, typed, entries);
    }

    // The dotted name that ends just before dot, such as canopy.items, or
    // null when the text there is not one.
    private static string? OwnerBefore(string text, int dot)
    {
        int end = dot;
        int start = end;
        while (true)
        {
            while (start > 0 && LuaTokens.IsNamePart(text[start - 1]))
            {
                --start;
            }
            if (start == end || char.IsAsciiDigit(text[start]))
            {
                return null;
            }
            if (start > 1 && text[start - 1] == '.' && text[start - 2] != '.')
            {
                end = --start;
                continue;
            }
            string owner = text[start..dot];
            return owner.Length > 0 ? owner : null;
        }
    }

    // The events, when the open string token is canopy.on's first argument.
    private static LuaCompletionResult? EventsAt(string text, LuaToken token, int caret, LuaCatalog catalog)
    {
        if (text[token.Start] is not ('"' or '\'') || !OnCall().IsMatch(text[..token.Start]))
        {
            return null;
        }
        int start = token.Start + 1;
        string typed = text[start..caret];
        List<LuaEntry> entries = catalog.Events
            .Where(entry => entry.Name.StartsWith(typed, StringComparison.OrdinalIgnoreCase))
            .ToList();
        return entries.Count == 0 ? null : new LuaCompletionResult(start, typed, entries);
    }

    [GeneratedRegex(@"\bcanopy\s*\.\s*on\s*\(\s*$")]
    private static partial Regex OnCall();
}
