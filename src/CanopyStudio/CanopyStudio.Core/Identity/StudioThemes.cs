namespace CanopyStudio.Core.Identity;

// A theme is data: a name, one accent and, for the seasonal ones, the season
// whose backdrop sits behind the window. Adding a theme means adding a line
// to StudioThemes.All; no view changes. Automatic is not a theme of its own:
// it picks one of these by the date.
public sealed record StudioTheme(string Id, string Name, uint Accent, string? Season)
{
    // Needs the game's art from the asset cache.
    public bool HasBackdrop => Season is not null;
}

// Every colour the window uses, as 0xAARRGGBB. Colours with an alpha below
// 0xFF are panels laid over the backdrop.
public sealed record ThemePalette
{
    public required uint Window { get; init; }
    public required uint Rail { get; init; }
    public required uint Status { get; init; }
    public required uint Surface { get; init; }
    public required uint Raised { get; init; }
    public required uint Border { get; init; }
    public required uint Text { get; init; }
    public required uint Muted { get; init; }
    public required uint Faint { get; init; }
    public required uint Accent { get; init; }
    public required uint AccentHover { get; init; }
    public required uint AccentPressed { get; init; }
    public required uint OnAccent { get; init; }
    // The accent ladder: about 80, 50, 30 and 20 percent.
    public required uint Accent80 { get; init; }
    public required uint Accent50 { get; init; }
    public required uint Accent30 { get; init; }
    public required uint Accent20 { get; init; }
    // A selected list row or navigation item, and the same under the pointer.
    public required uint Selected { get; init; }
    public required uint SelectedHover { get; init; }
    // Status colours stay the same in every theme.
    public uint Pass => 0xFF7DB48A;
    public uint Fail => 0xFFE07B6B;
}

public static class StudioThemes
{
    // Forest is the fallback: it needs nothing from the game, so it is what
    // a first start shows before the game's art has been copied.
    public static StudioTheme Forest { get; } = new("forest", "Forest", 0xFFE3A54A, null);

    public static IReadOnlyList<StudioTheme> All { get; } =
    [
        new("spring", "Spring", 0xFF9BCB6C, "spring"),
        new("autumn", "Autumn", 0xFFE3A54A, "autumn"),
        new("winter", "Winter", 0xFFA9D2EE, "winter"),
        Forest,
    ];

    // The season of today's date where the user is. It is the default: a
    // missing or unknown id wears it too.
    public const string AutomaticId = "automatic";
    public const string DefaultId = AutomaticId;

    public static StudioTheme Find(string? id) =>
        All.FirstOrDefault(theme => theme.Id == id) ??
        Automatic(DateOnly.FromDateTime(DateTime.Now), TimeZoneInfo.Local.Id);

    // The theme of the season on day in the time zone zoneId (a Windows or
    // an IANA id). The game has no summer art, so summer wears Forest.
    public static StudioTheme Automatic(DateOnly day, string zoneId)
    {
        string season = SeasonOf(day, IsSouthern(zoneId));
        return All.FirstOrDefault(theme => theme.Season == season) ?? Forest;
    }

    // Meteorological seasons: whole months, March to May is the northern
    // spring. The southern ones are six months away.
    public static string SeasonOf(DateOnly day, bool southern)
    {
        int month = southern ? (day.Month + 5) % 12 + 1 : day.Month;
        return month switch
        {
            3 or 4 or 5 => "spring",
            6 or 7 or 8 => "summer",
            9 or 10 or 11 => "autumn",
            _ => "winter",
        };
    }

    // Whether a time zone lies south of the equator. Windows names its zones
    // its own way, so its ids go through their IANA name first. A zone this
    // does not know is northern, where most users are.
    public static bool IsSouthern(string zoneId)
    {
        string iana = TimeZoneInfo.TryConvertWindowsIdToIanaId(zoneId, out string? converted) ? converted : zoneId;
        return SouthernZones.Any(zone => zone.EndsWith('/')
            ? iana.StartsWith(zone, StringComparison.Ordinal)
            : iana == zone);
    }

    // IANA zones south of the equator; an entry ending in / covers its
    // whole area.
    private static readonly string[] SouthernZones =
    [
        "Australia/", "Antarctica/", "America/Argentina/", "Chile/", "Brazil/", "NZ", "NZ-CHAT",
        "America/Santiago", "America/Punta_Arenas", "America/Buenos_Aires", "America/Cordoba",
        "America/Mendoza", "America/Montevideo", "America/Asuncion", "America/La_Paz", "America/Lima",
        "America/Guayaquil", "America/Sao_Paulo", "America/Bahia", "America/Recife", "America/Fortaleza",
        "America/Maceio", "America/Belem", "America/Santarem", "America/Araguaina", "America/Cuiaba",
        "America/Campo_Grande", "America/Manaus", "America/Porto_Velho", "America/Rio_Branco",
        "America/Eirunepe", "America/Noronha",
        "Africa/Johannesburg", "Africa/Maseru", "Africa/Mbabane", "Africa/Windhoek", "Africa/Gaborone",
        "Africa/Harare", "Africa/Maputo", "Africa/Lusaka", "Africa/Lubumbashi", "Africa/Blantyre",
        "Africa/Luanda", "Africa/Kinshasa", "Africa/Brazzaville", "Africa/Bujumbura", "Africa/Kigali",
        "Africa/Dar_es_Salaam", "Africa/Nairobi",
        "Indian/Antananarivo", "Indian/Mauritius", "Indian/Reunion", "Indian/Mayotte", "Indian/Comoro",
        "Indian/Chagos", "Indian/Kerguelen", "Indian/Cocos", "Indian/Christmas",
        "Asia/Jakarta", "Asia/Makassar", "Asia/Jayapura", "Asia/Dili",
        "Atlantic/St_Helena", "Atlantic/South_Georgia", "Atlantic/Stanley",
        "Pacific/Auckland", "Pacific/Chatham", "Pacific/Fiji", "Pacific/Tongatapu", "Pacific/Apia",
        "Pacific/Efate", "Pacific/Noumea", "Pacific/Norfolk", "Pacific/Easter", "Pacific/Port_Moresby",
        "Pacific/Bougainville", "Pacific/Guadalcanal", "Pacific/Tahiti", "Pacific/Rarotonga",
        "Pacific/Pago_Pago", "Pacific/Niue", "Pacific/Pitcairn", "Pacific/Marquesas", "Pacific/Gambier",
        "Pacific/Wallis", "Pacific/Funafuti", "Pacific/Fakaofo",
    ];

    // How much of the backdrop shows: it sits at this opacity on black.
    public const double BackdropOpacity = 0.25;

    public static ThemePalette Palette(StudioTheme theme) => theme.HasBackdrop ? Seasonal(theme.Accent) : ForestPalette;

    // Today's green-gray neutrals, opaque, with the amber accent.
    private static readonly ThemePalette ForestPalette = new()
    {
        Window = 0xFF101613,
        Rail = 0xFF131B17,
        Status = 0xFF131B17,
        Surface = 0xFF18211D,
        Raised = 0xFF202B26,
        Border = 0xFF2A3731,
        Text = 0xFFE4EBE6,
        Muted = 0xFF9AA79F,
        Faint = 0xFF808E86,
        Accent = 0xFFE3A54A,
        AccentHover = 0xFFECB664,
        AccentPressed = 0xFFCF9440,
        OnAccent = 0xFF1B1408,
        Accent80 = WithAlpha(0xFFE3A54A, 0.8),
        Accent50 = WithAlpha(0xFFE3A54A, 0.5),
        Accent30 = WithAlpha(0xFFE3A54A, 0.3),
        Accent20 = WithAlpha(0xFFE3A54A, 0.2),
        // Selections are a lighter green, so amber stays for actions.
        Selected = 0xFF2B3932,
        SelectedHover = 0xFF31413A,
    };

    // Near-black translucent panels over the dimmed backdrop; the season's
    // accent draws borders and selections.
    private static ThemePalette Seasonal(uint accent) => new()
    {
        Window = 0xFF000000,
        Rail = 0xC8080A09,
        Status = 0xE6080A09,
        Surface = 0xBE0A0D0C,
        Raised = 0xC8161C1A,
        Border = WithAlpha(accent, 0.3),
        Text = 0xFFECEFEA,
        Muted = 0xFFAEB8B1,
        Faint = 0xFF87918A,
        Accent = accent,
        AccentHover = Mix(accent, 0xFFFFFFFF, 0.15),
        AccentPressed = Mix(accent, 0xFF000000, 0.12),
        OnAccent = 0xFF15110A,
        Accent80 = WithAlpha(accent, 0.8),
        Accent50 = WithAlpha(accent, 0.5),
        Accent30 = WithAlpha(accent, 0.3),
        Accent20 = WithAlpha(accent, 0.2),
        Selected = WithAlpha(accent, 0.2),
        SelectedHover = WithAlpha(accent, 0.3),
    };

    public static uint WithAlpha(uint color, double alpha) =>
        ((uint)Math.Round(alpha * 255) << 24) | (color & 0x00FFFFFF);

    // color moved toward other by amount, opaque.
    public static uint Mix(uint color, uint other, double amount)
    {
        uint channel(int shift) =>
            (uint)Math.Round(((color >> shift) & 0xFF) * (1 - amount) + ((other >> shift) & 0xFF) * amount);
        return 0xFF000000 | (channel(16) << 16) | (channel(8) << 8) | channel(0);
    }

    // color laid over an opaque under, as straight alpha.
    public static uint Over(uint color, uint under)
    {
        double alpha = (color >> 24) / 255.0;
        uint channel(int shift) =>
            (uint)Math.Round(((color >> shift) & 0xFF) * alpha + ((under >> shift) & 0xFF) * (1 - alpha));
        return 0xFF000000 | (channel(16) << 16) | (channel(8) << 8) | channel(0);
    }

    // WCAG contrast ratio of two opaque colours, 1 to 21.
    public static double Contrast(uint a, uint b)
    {
        double la = Luminance(a), lb = Luminance(b);
        return (Math.Max(la, lb) + 0.05) / (Math.Min(la, lb) + 0.05);
    }

    private static double Luminance(uint color)
    {
        double channel(int shift)
        {
            double value = ((color >> shift) & 0xFF) / 255.0;
            return value <= 0.03928 ? value / 12.92 : Math.Pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * channel(16) + 0.7152 * channel(8) + 0.0722 * channel(0);
    }
}
