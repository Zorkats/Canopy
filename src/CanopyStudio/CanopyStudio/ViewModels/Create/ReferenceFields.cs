using CanopyStudio.Core.Create;
using CanopyStudio.Core.Native;
using CanopyStudio.Core.Play;
using CanopyStudio.ViewModels.Create.Refs;

namespace CanopyStudio.ViewModels.Create;

// Editors for values that point at something else: vanilla rows, flags,
// items, icons, textures, skins, and melodies, plus the two lists of
// references, a recipe's needs and a skin's replace. Null for a kind it does
// not take.
public static class ReferenceFields
{
    public static FieldViewModel? Create(FieldContext context)
    {
        string kind = context.Field.Kind;
        if (kind == FieldKinds.VanillaId)
        {
            return new VanillaIdFieldViewModel(context);
        }
        if (kind == FieldKinds.Melody)
        {
            return new MelodyFieldViewModel(context);
        }
        if (kind == FieldKinds.List)
        {
            return IsList(context, "needs", "craft.", "cooking.") ? new NeedsFieldViewModel(context)
                : IsList(context, "replace", "skin.") ? new SwapsFieldViewModel(context)
                : null;
        }
        return References.For(kind, context.Kind.Prefix) is { } rules ? new RefFieldViewModel(context, rules) : null;
    }

    // The keys the field's project and the game's other mods define. The
    // other mods are those in the open game's mods folder, none without one.
    internal static ProjectKeys KeysFor(FieldContext context) =>
        ProjectKeys.Read(context.Project.Structure, context.Project.Name,
            context.GameFolder is { } game ? PlayPaths.Mods(game) : null, context.Project.Folder);

    private static bool IsList(FieldContext context, string key, params string[] prefixes) =>
        context.Field.Key.Equals(key, StringComparison.OrdinalIgnoreCase)
        && prefixes.Any(prefix => context.Kind.Prefix.Equals(prefix, StringComparison.OrdinalIgnoreCase));
}
