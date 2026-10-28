using CanopyStudio.Core.Assets;

namespace CanopyStudio.Core.Create;

// The per-language keys of a lang-text field: name.ja, name.en, ... in the
// order of kLanguageKeys (src/CanopyCore/mod_rules.cpp). A language without
// its own key uses the base key, as ReadTexts does.
public static class LangKeys
{
    public static IReadOnlyList<string> Codes { get; } = ["ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"];

    public static string Key(string baseKey, string code) => baseKey + "." + code;

    public static IReadOnlyList<string> Keys(string baseKey) => Codes.Select(code => Key(baseKey, code)).ToList();

    // The language's name in English, or the code when it has none.
    public static string LanguageName(string code) =>
        VanillaKinds.Languages.FirstOrDefault(language => language.Code == code)?.Name ?? code;

    // The base key of key: "name" for "name.en", key itself otherwise.
    public static string BaseOf(string key)
    {
        int dot = key.LastIndexOf('.');
        return dot > 0 && Codes.Contains(key[(dot + 1)..], StringComparer.OrdinalIgnoreCase) ? key[..dot] : key;
    }
}
