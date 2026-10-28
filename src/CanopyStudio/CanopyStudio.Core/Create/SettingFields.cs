using CanopyStudio.Core.Native;

namespace CanopyStudio.Core.Create;

// Which keys a [setting.<Key>] section uses for its type, as ReadSetting in
// src/CanopyCore/rules_mod.cpp reads them: every type has type, label, help
// and default; a slider adds min, max and step; a choice adds choices.
public static class SettingFields
{
    public const string Prefix = "setting.";
    public const string TypeKey = "type";
    // What ReadSetting uses when type is left out.
    public const string DefaultType = "toggle";

    private static readonly string[] Common = ["type", "label", "help", "default"];
    private static readonly string[] Slider = ["min", "max", "step"];
    private static readonly string[] Choice = ["choices"];

    public static bool IsSetting(SectionSchema kind) => SectionNames.Same(kind.Prefix, Prefix);

    // The keys a setting of type uses, or null for a type the rules refuse,
    // so a form shows every key while the type is being fixed.
    public static IReadOnlySet<string>? Keys(string? type)
    {
        string name = string.IsNullOrWhiteSpace(type) ? DefaultType : type.Trim().ToLowerInvariant();
        IEnumerable<string>? keys = name switch
        {
            "toggle" => Common,
            "slider" => Common.Concat(Slider),
            "choice" => Common.Concat(Choice),
            _ => null,
        };
        return keys?.ToHashSet(StringComparer.OrdinalIgnoreCase);
    }

    // The fields of kind a section shows: all of them, or for a setting the
    // ones its type uses.
    public static IReadOnlyList<FieldSchema> Visible(SectionSchema kind, string? type)
    {
        IReadOnlySet<string>? keys = IsSetting(kind) ? Keys(type) : null;
        return keys is null ? kind.Fields : kind.Fields.Where(field => keys.Contains(field.Key)).ToList();
    }
}
