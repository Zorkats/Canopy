using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using Avalonia.Media;
using Avalonia.Media.Fonts;
using Avalonia.Platform;

namespace CanopyStudio.Services;

// The game's fonts from the asset cache, as a fonts: collection. Each file is
// read into memory first, so the cache folder is never held open and can be
// rebuilt while Canopy Studio runs. A family is found by its name without the
// weight ("Lora", "Resource Han Rounded TC"), and a weight the files lack is
// matched to the nearest one rather than faked.
public sealed class CachedFontCollection(Uri key, IReadOnlyList<string> files) : FontCollectionBase
{
    // Avalonia 11.3 builds a typeface from a stream only through
    // IFontManagerImpl.TryCreateGlyphTypeface(Stream, ...), which its
    // reference assemblies hide although it is public at run time (11.3 has
    // no public way to load a font file from disk). If a later Avalonia drops
    // it, this finds nothing and the window keeps the system font.
    private static readonly MethodInfo? FromStream = typeof(IFontManagerImpl).GetMethod(
        "TryCreateGlyphTypeface",
        [typeof(Stream), typeof(FontSimulations), typeof(IGlyphTypeface).MakeByRefType()]);

    private static readonly string[] WeightWords =
        [" Thin", " ExtraLight", " Light", " Regular", " Medium", " SemiBold", " Bold", " ExtraBold", " Black", " Heavy"];

    private readonly List<FontFamily> families = [];

    public override Uri Key => key;

    public override int Count => families.Count;

    public override FontFamily this[int index] => families[index];

    public bool Has(string familyName) => _glyphTypefaceCache.ContainsKey(familyName);

    public override void Initialize(IFontManagerImpl fontManager)
    {
        if (FromStream is null)
        {
            return;
        }
        foreach (string file in files)
        {
            byte[] bytes;
            try
            {
                bytes = File.ReadAllBytes(file);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                continue;
            }
            object?[] arguments = [new MemoryStream(bytes), FontSimulations.None, null];
            if (FromStream.Invoke(fontManager, arguments) is true && arguments[2] is IGlyphTypeface typeface)
            {
                Add(typeface);
            }
        }
    }

    public override bool TryGetGlyphTypeface(string familyName, FontStyle style, FontWeight weight, FontStretch stretch,
        [NotNullWhen(true)] out IGlyphTypeface? glyphTypeface)
    {
        glyphTypeface = null;
        if (!_glyphTypefaceCache.TryGetValue(familyName, out ConcurrentDictionary<FontCollectionKey, IGlyphTypeface?>? faces))
        {
            return false;
        }
        if (faces.TryGetValue(new FontCollectionKey(style, weight, stretch), out glyphTypeface) && glyphTypeface is not null)
        {
            return true;
        }
        // Nearest weight, upright before italic; heavier wins a tie.
        glyphTypeface = faces.Values
            .OfType<IGlyphTypeface>()
            .OrderBy(face => face.Style == style ? 0 : 1)
            .ThenBy(face => Math.Abs((int)face.Weight - (int)weight))
            .ThenByDescending(face => (int)face.Weight)
            .FirstOrDefault();
        return glyphTypeface is not null;
    }

    public override IEnumerator<FontFamily> GetEnumerator() => families.GetEnumerator();

    private void Add(IGlyphTypeface typeface)
    {
        // "Lora Medium" is the Medium face of "Lora".
        string family = typeface.FamilyName;
        foreach (string word in WeightWords)
        {
            if (family.EndsWith(word, StringComparison.OrdinalIgnoreCase))
            {
                family = family[..^word.Length];
                break;
            }
        }
        foreach (string name in (string[])[family, typeface.FamilyName])
        {
            ConcurrentDictionary<FontCollectionKey, IGlyphTypeface?> faces = _glyphTypefaceCache.GetOrAdd(name, _ =>
            {
                families.Add(new FontFamily(key, name));
                return new ConcurrentDictionary<FontCollectionKey, IGlyphTypeface?>();
            });
            faces.TryAdd(new FontCollectionKey(typeface.Style, typeface.Weight, typeface.Stretch), typeface);
        }
    }
}
