using System.Globalization;

namespace CanopyStudio.Core.Game;

// Writing a text of the index into the edits mod as a patch, and taking it
// out again. Each call is one ini edit of the mod; nothing is saved. The
// page tells GameEdits after each change.
public static class GameTextEdit
{
    public const string NoMod = "Choose an edits mod in the bar above to edit texts.";

    // Null when entry's text in language can be edited through the Text
    // page, else why not.
    public static string? CannotEdit(EditsMod? mod, GameTextPatches patches, GameTextEntry entry, int language)
    {
        if (mod is null)
        {
            return NoMod;
        }
        if (!entry.Has(language))
        {
            return "This row has no text in that language.";
        }
        if (!entry.Editable[language])
        {
            return "Canopy does not know this field well enough to patch it yet, so it is read only.";
        }
        if (patches.EnumSection(entry) is { } named)
        {
            return string.Create(CultureInfo.CurrentCulture,
                $"The edits mod names this row by its enum name (line {named.Line} of its canopy.ini), and a mod patches a row in one section only. Change it there, in Create.");
        }
        if (patches.Find(entry, language) is { } value && !GameTextPatches.IsPlain(value, entry, language))
        {
            return string.Create(CultureInfo.CurrentCulture,
                $"The edits mod sets this text as {value.Path}, at line {value.Line} of its canopy.ini. Change it there, in Create.");
        }
        return null;
    }

    // Makes text entry's text in language. Text equal to the vanilla one
    // takes the patch out; any other is set as <field>.<language> on the
    // row's id. Null when made, else why not, and then nothing was written.
    public static string? Commit(EditsMod? mod, GameTextPatches patches, GameTextEntry entry, int language, string text)
    {
        if (CannotEdit(mod, patches, entry, language) is { } reason)
        {
            return reason;
        }
        if (text == entry.Values[language])
        {
            return mod!.Remove(entry.File, entry.Id, entry.Path(language));
        }
        return mod!.Set(entry.File, entry.Id, entry.Path(language), text);
    }

    // Takes the mod's text for entry in language out, back to vanilla. Null
    // when done or when the mod set none, else why not.
    public static string? Revert(EditsMod? mod, GameTextPatches patches, GameTextEntry entry, int language)
    {
        if (patches.Find(entry, language) is null)
        {
            return null;
        }
        return CannotEdit(mod, patches, entry, language) ?? mod!.Remove(entry.File, entry.Id, entry.Path(language));
    }

    // Reverts every language of entry the mod sets. Null when all were
    // reverted, else the first reason one was not.
    public static string? RevertAll(EditsMod? mod, GameTextPatches patches, GameTextEntry entry)
    {
        string? first = null;
        for (int language = 0; language < entry.Values.Count; ++language)
        {
            if (Revert(mod, patches, entry, language) is { } reason)
            {
                first ??= reason;
            }
        }
        return first;
    }
}
