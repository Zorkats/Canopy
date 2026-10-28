using System.Runtime.InteropServices;

namespace CanopyStudio.Core.Native;

// The raw calls of canopy_core.dll, as canopy_core_api.h declares them.
// Every string goes in as UTF-8, paths too, so the same calls work with
// libcanopy_core.so on Linux, where wchar_t is 4 bytes. Strings come back
// as UTF-8 JSON the caller frees; CanopyCore wraps these.
internal static partial class CanopyCoreNative
{
    private const string Library = "canopy_core";

    [LibraryImport(Library)]
    internal static partial int canopy_core_abi();

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_sections();

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_validate(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? modsFolder);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_validate_project(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? modsFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string projectFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string iniText);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_game([MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_files([MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_table(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string file);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_row(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string file,
        int index);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_grid(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string file);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_texture(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gamePath,
        out IntPtr pixels);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_file(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gamePath,
        out IntPtr bytes);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_icon(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        uint iconId,
        out IntPtr pixels);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_icons(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        uint[] ids,
        int count,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string outFolder);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_names(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string kind,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string language);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_sound(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gameFolder,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string gamePath,
        out IntPtr bytes);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_ini_parse([MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [LibraryImport(Library)]
    internal static partial IntPtr canopy_core_ini_edit(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string op,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string section,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string key,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string value);

    [LibraryImport(Library)]
    internal static partial void canopy_core_free(IntPtr text);

    [LibraryImport(Library)]
    internal static partial void canopy_core_free_pixels(IntPtr pixels);

    [LibraryImport(Library)]
    internal static partial void canopy_core_free_bytes(IntPtr bytes);
}
