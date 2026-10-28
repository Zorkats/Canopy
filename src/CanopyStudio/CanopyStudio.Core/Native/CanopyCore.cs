using System.Runtime.InteropServices;
using System.Text.Json;

namespace CanopyStudio.Core.Native;

// canopy_core.dll for C#: each call opens what it needs and keeps nothing,
// so any thread may call. A failure inside the DLL comes back as a result
// with Ok false; only a missing or mismatched DLL throws, from Check.
public static class CanopyCore
{
    // The JSON layout this build of Canopy Studio reads.
    public const int ExpectedAbi = 1;

    // The library's file on this platform, for messages.
    public static string LibraryFile { get; } = OperatingSystem.IsWindows() ? "canopy_core.dll"
        : OperatingSystem.IsMacOS() ? "libcanopy_core.dylib"
        : "libcanopy_core.so";

    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    // Null when the DLL loads and speaks the expected layout, else why not,
    // in a sentence for the user.
    public static string? Check()
    {
        try
        {
            int abi = CanopyCoreNative.canopy_core_abi();
            return abi == ExpectedAbi
                ? null
                : $"{LibraryFile} speaks JSON layout {abi}, but this Canopy Studio reads layout {ExpectedAbi}. Rebuild both from the same source.";
        }
        catch (DllNotFoundException)
        {
            return $"{LibraryFile} was not found next to Canopy Studio. Build the C++ tools first (cmake --build --preset release).";
        }
        catch (EntryPointNotFoundException)
        {
            return TooOld;
        }
    }

    // What every section kind of canopy.ini accepts, in the order a tool
    // lists them. The same for every call, so callers may keep it.
    public static SchemaInfo Sections()
    {
        SchemaInfo schema = Call(() => Take<SchemaInfo>(CanopyCoreNative.canopy_core_sections()));
        // The list carries no ok of its own: it is fine when it has kinds.
        return schema.Error is null && schema.Sections.Count > 0 ? schema with { Ok = true } : schema;
    }

    // Checks every mod in modsFolder the way Canopy does at startup.
    // Without modsFolder the game's mods folder is checked; without
    // gameFolder vanilla ids are not.
    public static ValidationReport Validate(string? gameFolder, string? modsFolder = null) =>
        Call(() => Take<ValidationReport>(CanopyCoreNative.canopy_core_validate(gameFolder, modsFolder)));

    // Checks the mod in projectFolder with iniText as its canopy.ini, saved
    // or not, against the other mods in modsFolder (the game's when null).
    // Only the project's diagnostics come back.
    public static ValidationReport ValidateProject(string? gameFolder, string? modsFolder, string projectFolder,
        string iniText) =>
        Call(() => Take<ValidationReport>(
            CanopyCoreNative.canopy_core_validate_project(gameFolder, modsFolder, projectFolder, iniText)));

    public static GameInfo Game(string gameFolder) =>
        Take<GameInfo>(CanopyCoreNative.canopy_core_game(gameFolder));

    public static FileList Files(string gameFolder) =>
        Take<FileList>(CanopyCoreNative.canopy_core_files(gameFolder));

    public static TableInfo Table(string gameFolder, string file) =>
        Take<TableInfo>(CanopyCoreNative.canopy_core_table(gameFolder, file));

    public static RowInfo Row(string gameFolder, string file, int index) =>
        Take<RowInfo>(CanopyCoreNative.canopy_core_row(gameFolder, file, index));

    // Every row of a table with a schema, under its columns.
    public static TableGrid Grid(string gameFolder, string file) =>
        Take<TableGrid>(CanopyCoreNative.canopy_core_grid(gameFolder, file));

    // The texture at gamePath, or null with error set.
    public static TextureImage? Texture(string gameFolder, string gamePath, out string? error) =>
        Image((out IntPtr pixels) => CanopyCoreNative.canopy_core_texture(gameFolder, gamePath, out pixels),
            "The texture could not be decoded.", out error);

    // The cell of icon.dat row iconId cut out of its texture, or null with
    // error set.
    public static TextureImage? Icon(string gameFolder, uint iconId, out string? error) =>
        Image((out IntPtr pixels) => CanopyCoreNative.canopy_core_icon(gameFolder, iconId, out pixels),
            "The icon could not be read.", out error);

    // Every icon in ids saved as outFolder\<id>.png, each texture decoded
    // once. Much faster than Icon for a whole picker.
    public static IconBatch Icons(string gameFolder, IReadOnlyList<uint> ids, string outFolder)
    {
        uint[] list = [.. ids];
        return Call(() => Take<IconBatch>(CanopyCoreNative.canopy_core_icons(gameFolder, list, list.Length, outFolder)));
    }

    // The file at gamePath as the game reads it, or null with error set.
    public static byte[]? File(string gameFolder, string gamePath, out string? error) =>
        Bytes((out IntPtr bytes) => CanopyCoreNative.canopy_core_file(gameFolder, gamePath, out bytes),
            "The file could not be read.", out error);

    // The Ogg Vorbis stream inside the .nlsd at gamePath, or null with error
    // set. Most sound effects are not Ogg and fail with a reason.
    public static byte[]? Sound(string gameFolder, string gamePath, out string? error) =>
        Bytes((out IntPtr bytes) => CanopyCoreNative.canopy_core_sound(gameFolder, gamePath, out bytes),
            "The sound could not be read.", out error);

    // What a picker lists for kind (item, villager, whistle, craft, cooking)
    // in language, a canopy.ini code such as "en".
    public static NameList Names(string gameFolder, string kind, string language) =>
        Call(() => Take<NameList>(CanopyCoreNative.canopy_core_names(gameFolder, kind, language)));

    // The lines of canopy.ini text, read the way the loader reads them.
    public static IniStructure IniParse(string text) =>
        Call(() => Take<IniStructure>(CanopyCoreNative.canopy_core_ini_parse(text)));

    // text with one edit applied and every other byte kept. op is "set",
    // "remove", "addSection", "removeSection" or "renameSection"; see
    // src/CanopyCore/ini_document.h.
    public static IniEditResult IniEdit(string text, string op, string section, string key = "", string value = "") =>
        Call(() => Take<IniEditResult>(CanopyCoreNative.canopy_core_ini_edit(text, op, section, key, value)));

    // A DLL from before an export still passes Check.
    private static readonly string TooOld = $"{LibraryFile} is too old for this Canopy Studio. Rebuild it from the same source.";

    private delegate IntPtr BufferCall(out IntPtr buffer);

    private static T Call<T>(Func<T> call) where T : CoreResult
    {
        try
        {
            return call();
        }
        catch (EntryPointNotFoundException)
        {
            return Failed<T>(TooOld);
        }
    }

    // Pixels the DLL handed out, copied and freed.
    private static TextureImage? Image(BufferCall call, string fallback, out string? error)
    {
        IntPtr pixels = IntPtr.Zero;
        try
        {
            TextureInfo info = Take<TextureInfo>(call(out pixels));
            if (!info.Ok || pixels == IntPtr.Zero)
            {
                error = info.Error ?? fallback;
                return null;
            }
            byte[] copy = new byte[checked(info.Width * info.Height * 4)];
            Marshal.Copy(pixels, copy, 0, copy.Length);
            error = null;
            return new TextureImage(info.Width, info.Height, copy);
        }
        catch (EntryPointNotFoundException)
        {
            error = TooOld;
            return null;
        }
        finally
        {
            CanopyCoreNative.canopy_core_free_pixels(pixels);
        }
    }

    // Bytes the DLL handed out, copied and freed.
    private static byte[]? Bytes(BufferCall call, string fallback, out string? error)
    {
        IntPtr bytes = IntPtr.Zero;
        try
        {
            FileReadInfo info = Take<FileReadInfo>(call(out bytes));
            if (!info.Ok || bytes == IntPtr.Zero)
            {
                error = info.Error ?? fallback;
                return null;
            }
            byte[] copy = new byte[info.Size];
            Marshal.Copy(bytes, copy, 0, copy.Length);
            error = null;
            return copy;
        }
        catch (EntryPointNotFoundException)
        {
            error = TooOld;
            return null;
        }
        finally
        {
            if (bytes != IntPtr.Zero)
            {
                CanopyCoreNative.canopy_core_free_bytes(bytes);
            }
        }
    }

    // Parses and frees a string the DLL handed out.
    private static T Take<T>(IntPtr text) where T : CoreResult
    {
        try
        {
            string json = Marshal.PtrToStringUTF8(text) ?? "";
            return Parse<T>(json);
        }
        finally
        {
            CanopyCoreNative.canopy_core_free(text);
        }
    }

    // Public for tests: the result JSON as T, or a failed T when it does not
    // parse.
    public static T Parse<T>(string json) where T : CoreResult
    {
        try
        {
            T? result = JsonSerializer.Deserialize<T>(json, Json);
            if (result is not null)
            {
                return result;
            }
        }
        catch (JsonException)
        {
        }
        return Failed<T>($"{LibraryFile} answered with JSON this Canopy Studio cannot read.");
    }

    private static T Failed<T>(string error) where T : CoreResult
    {
        CoreResult empty = (CoreResult)Activator.CreateInstance(typeof(T))!;
        return (T)(empty with { Ok = false, Error = error });
    }
}
