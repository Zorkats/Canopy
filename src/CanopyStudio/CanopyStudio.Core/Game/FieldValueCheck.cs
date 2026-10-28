using System.Globalization;
using System.Text;

namespace CanopyStudio.Core.Game;

// A value checked for one place of a row: Text is what a patch writes
// there, in the form the game's grid shows (so 007 becomes 7 and true
// becomes 1); Error says why the value is refused, with Text null.
public sealed record FieldValue(string? Text, string? Error)
{
    public bool Ok => Error is null;
}

// Checks a value for a grid column before it is written to the edits mod,
// by the column's type as canopy_core names it, with the rules of
// table_schema::Check (src/CanopyCore/table_schema.cpp), so a value the
// loader would refuse is refused here first. A reference must also be one
// of ids when they are given: the referenced table's row ids and the
// values the column already holds in the game.
public static class FieldValueCheck
{
    // The longest text the game's tables take, in UTF-8 bytes.
    public const int MaxTextBytes = 1024;

    private const int MaxDigits = 20;
    private const int MaxFloatLength = 32;

    // True for the types whose values are numbers, which a grid shows in
    // a fixed-width font, right-aligned.
    public static bool IsNumber(string type) =>
        type is "i32" or "u32" or "u64" or "i64" or "f32" or "bool" or "ref" or "flag" or "id";

    // True for the types a patch can write at all; a column's Editable
    // also says whether its field is confirmed.
    public static bool IsWritable(string type) =>
        type is "text" or "path" or "i32" or "flag" or "u32" or "u64" or "i64" or "ref" or "f32" or "bool";

    public static FieldValue Check(string type, string value, IReadOnlySet<ulong>? ids = null, string? table = null)
    {
        switch (type)
        {
            case "text":
            case "path":
                return CheckText(value);
            case "i32":
            case "flag":
                return ParseSigned(value, out int signed)
                    ? Fine(signed.ToString(CultureInfo.InvariantCulture))
                    : Refused("This field takes a whole number from -2147483648 to 2147483647.");
            case "u32":
                return ParseUnsigned(value, uint.MaxValue, out ulong small)
                    ? Fine(small.ToString(CultureInfo.InvariantCulture))
                    : Refused("This field takes a whole number from 0 to 4294967295.");
            case "u64":
                return ParseUnsigned(value, ulong.MaxValue, out ulong large)
                    ? Fine(large.ToString(CultureInfo.InvariantCulture))
                    : Refused("This field takes a whole number of at least 0.");
            case "i64":
                return ParseSigned64(value, out long wide)
                    ? Fine(wide.ToString(CultureInfo.InvariantCulture))
                    : Refused("This field takes a whole number from -9223372036854775808 to 9223372036854775807.");
            case "ref":
                if (!ParseUnsigned(value, ulong.MaxValue, out ulong id))
                {
                    return Refused(table is null
                        ? "This field takes the id of a row, a whole number of at least 0."
                        : $"This field takes the id of a row of {table}, a whole number of at least 0.");
                }
                if (ids is not null && !ids.Contains(id))
                {
                    return Refused(table is null
                        ? $"No row has the id {id}. Use the id of a row the referenced table has."
                        : $"{table} has no row with the id {id}. Use the id of one of its rows.");
                }
                return Fine(id.ToString(CultureInfo.InvariantCulture));
            case "f32":
                return ParseFloat(value, out float real)
                    ? Fine(real.ToString("R", CultureInfo.InvariantCulture))
                    : Refused("This field takes a number, such as 1.5 or -2, within the range of a 32-bit float.");
            case "bool":
                if (value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase))
                {
                    return Fine("1");
                }
                if (value == "0" || value.Equals("false", StringComparison.OrdinalIgnoreCase))
                {
                    return Fine("0");
                }
                return Refused("This field takes 0 or 1 (true or false).");
            default:
                return Refused("A patch cannot write this field.");
        }
    }

    // True when two values of type are the same value: numbers by what
    // they read as (a float by its bits, so 0 and -0 differ, as they do in
    // the game), texts as written. A value that does not check is only the
    // same as itself.
    public static bool Same(string type, string? left, string? right)
    {
        if (left is null || right is null)
        {
            return left is null && right is null;
        }
        if (string.Equals(left, right, StringComparison.Ordinal))
        {
            return true;
        }
        if (type == "f32")
        {
            return ParseFloat(left, out float a) && ParseFloat(right, out float b) &&
                BitConverter.SingleToInt32Bits(a) == BitConverter.SingleToInt32Bits(b);
        }
        if (type is "text" or "path" or "enum")
        {
            return false;
        }
        FieldValue first = Check(type, left);
        FieldValue second = Check(type, right);
        return first.Ok && second.Ok && first.Text == second.Text;
    }

    private static FieldValue CheckText(string value)
    {
        if (EditsMod.CannotHold(value) is { } reason)
        {
            return Refused(reason);
        }
        foreach (char c in value)
        {
            if (c < ' ' || c == '\u007f')
            {
                return Refused("A text cannot hold control characters such as tabs.");
            }
        }
        if (Encoding.UTF8.GetByteCount(value) > MaxTextBytes)
        {
            return Refused($"A text holds at most {MaxTextBytes} bytes.");
        }
        return Fine(value);
    }

    // ParseUnsigned of table_schema.cpp: 1 to 20 decimal digits, no sign,
    // at most limit.
    private static bool ParseUnsigned(string text, ulong limit, out ulong value)
    {
        value = 0;
        if (text.Length == 0 || text.Length > MaxDigits)
        {
            return false;
        }
        foreach (char c in text)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
            ulong digit = (ulong)(c - '0');
            if (value > (limit - digit) / 10)
            {
                return false;
            }
            value = value * 10 + digit;
        }
        return true;
    }

    // ParseSigned of table_schema.cpp: an optional '-', then the digits of
    // a value in the int32 range. "-0" is 0.
    private static bool ParseSigned(string text, out int value)
    {
        value = 0;
        bool negative = text.Length > 0 && text[0] == '-';
        if (!ParseUnsigned(negative ? text[1..] : text, negative ? 2147483648UL : 2147483647UL, out ulong magnitude))
        {
            return false;
        }
        value = negative ? (int)(-(long)magnitude) : (int)magnitude;
        return true;
    }

    // ParseSigned64 of table_schema.cpp: the same, in the int64 range.
    private static bool ParseSigned64(string text, out long value)
    {
        value = 0;
        bool negative = text.Length > 0 && text[0] == '-';
        if (!ParseUnsigned(negative ? text[1..] : text, negative ? 9223372036854775808UL : 9223372036854775807UL,
                out ulong magnitude))
        {
            return false;
        }
        value = negative ? unchecked((long)(0UL - magnitude)) : (long)magnitude;
        return true;
    }

    // ParseFloat of table_schema.cpp reads with strtof and wants the whole
    // text read, no range error and a finite result. Here: a sign, digits
    // with one point and an exponent, read invariantly. strtof reports a
    // range error when a value overflows or falls below the smallest
    // normal float, so those are refused too.
    private static bool ParseFloat(string text, out float value)
    {
        value = 0;
        if (text.Length == 0 || text.Length > MaxFloatLength)
        {
            return false;
        }
        const NumberStyles style = NumberStyles.AllowLeadingSign | NumberStyles.AllowDecimalPoint | NumberStyles.AllowExponent;
        if (!float.TryParse(text, style, CultureInfo.InvariantCulture, out value) || !float.IsFinite(value))
        {
            return false;
        }
        if (float.IsSubnormal(value))
        {
            return false;
        }
        if (value == 0 && HasNonZeroDigit(text))
        {
            // Too small for a float: strtof gives 0 with a range error.
            return false;
        }
        return true;
    }

    // True when the digits before any exponent are not all zero.
    private static bool HasNonZeroDigit(string text)
    {
        foreach (char c in text)
        {
            if (c is 'e' or 'E')
            {
                break;
            }
            if (c is >= '1' and <= '9')
            {
                return true;
            }
        }
        return false;
    }

    private static FieldValue Fine(string text) => new(text, null);

    private static FieldValue Refused(string error) => new(null, error);
}
