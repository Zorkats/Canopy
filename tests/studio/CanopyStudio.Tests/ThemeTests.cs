using CanopyStudio.Core.Identity;

namespace CanopyStudio.Tests;

public sealed class ThemeTests
{
    // The brightest the dimmed backdrop can get: white at 25% on black.
    private static readonly uint WorstBackdrop = StudioThemes.Over(
        StudioThemes.WithAlpha(0xFFFFFFFF, StudioThemes.BackdropOpacity), 0xFF000000);

    public static TheoryData<string> ThemeIds()
    {
        TheoryData<string> ids = [];
        foreach (StudioTheme theme in StudioThemes.All)
        {
            ids.Add(theme.Id);
        }
        return ids;
    }

    // What a panel looks like at its brightest: over the worst backdrop when
    // the theme has one, else over the window colour.
    private static uint Under(ThemePalette palette, StudioTheme theme, uint panel) =>
        StudioThemes.Over(panel, theme.HasBackdrop ? WorstBackdrop : palette.Window);

    [Theory]
    [MemberData(nameof(ThemeIds))]
    public void TextPassesAaOnEveryPanel(string id)
    {
        StudioTheme theme = StudioThemes.Find(id);
        ThemePalette palette = StudioThemes.Palette(theme);

        foreach (uint panel in (uint[])[palette.Surface, palette.Rail, palette.Status])
        {
            uint under = Under(palette, theme, panel);
            Assert.True(StudioThemes.Contrast(palette.Text, under) >= 7, $"{id} text {panel:X8}");
            Assert.True(StudioThemes.Contrast(palette.Muted, under) >= 4.5, $"{id} muted {panel:X8}");
            Assert.True(StudioThemes.Contrast(palette.Faint, under) >= 4.5, $"{id} faint {panel:X8}");
            Assert.True(StudioThemes.Contrast(palette.Accent, under) >= 4.5, $"{id} accent {panel:X8}");
        }
        uint raised = Under(palette, theme, palette.Raised);
        Assert.True(StudioThemes.Contrast(palette.Muted, raised) >= 4.5, $"{id} muted on raised");
        uint selected = StudioThemes.Over(palette.Selected, Under(palette, theme, palette.Rail));
        Assert.True(StudioThemes.Contrast(palette.Text, selected) >= 7, $"{id} text on selected");
    }

    [Theory]
    [MemberData(nameof(ThemeIds))]
    public void PageHeadersReadStraightOnTheBackdrop(string id)
    {
        StudioTheme theme = StudioThemes.Find(id);
        ThemePalette palette = StudioThemes.Palette(theme);
        uint under = theme.HasBackdrop ? WorstBackdrop : palette.Window;

        Assert.True(StudioThemes.Contrast(palette.Text, under) >= 4.5);
        Assert.True(StudioThemes.Contrast(palette.Muted, under) >= 4.5);
    }

    [Theory]
    [MemberData(nameof(ThemeIds))]
    public void AccentButtonsReadInEveryState(string id)
    {
        ThemePalette palette = StudioThemes.Palette(StudioThemes.Find(id));

        foreach (uint fill in (uint[])[palette.Accent, palette.AccentHover, palette.AccentPressed])
        {
            Assert.True(StudioThemes.Contrast(palette.OnAccent, fill) >= 4.5, $"{id} {fill:X8}");
        }
    }

    [Fact]
    public void LadderKeepsTheAccentColour()
    {
        ThemePalette palette = StudioThemes.Palette(StudioThemes.Find("winter"));

        Assert.Equal(0xFFA9D2EEu, palette.Accent);
        Assert.Equal(0xCCA9D2EEu, palette.Accent80);
        Assert.Equal(0x80A9D2EEu, palette.Accent50);
        Assert.Equal(0x4CA9D2EEu, palette.Accent30);
        Assert.Equal(0x33A9D2EEu, palette.Accent20);
    }

    [Fact]
    public void UnknownOrMissingIdFallsBackToTheDefault()
    {
        Assert.Equal(StudioThemes.AutomaticId, StudioThemes.DefaultId);
        StudioTheme today = StudioThemes.Find(StudioThemes.AutomaticId);
        Assert.Contains(today, StudioThemes.All);
        Assert.Equal(today, StudioThemes.Find(null));
        Assert.Equal(today, StudioThemes.Find("summer"));
        Assert.Equal("winter", StudioThemes.Find("winter").Id);
        Assert.False(StudioThemes.Forest.HasBackdrop);
        Assert.All(StudioThemes.All.Where(theme => theme.HasBackdrop),
            theme => Assert.Contains(Backdrops.Seasons, season => season.Id == theme.Season));
    }

    [Theory]
    [InlineData("America/Santiago", true)]
    [InlineData("America/Argentina/Buenos_Aires", true)]
    [InlineData("Australia/Sydney", true)]
    [InlineData("Pacific/Auckland", true)]
    [InlineData("Africa/Johannesburg", true)]
    [InlineData("Pacific SA Standard Time", true)]
    [InlineData("AUS Eastern Standard Time", true)]
    [InlineData("Europe/Madrid", false)]
    [InlineData("America/New_York", false)]
    [InlineData("Romance Standard Time", false)]
    [InlineData("Asia/Tokyo", false)]
    [InlineData("UTC", false)]
    [InlineData("Not/AZone", false)]
    public void HemisphereComesFromTheTimeZone(string zoneId, bool southern)
    {
        Assert.Equal(southern, StudioThemes.IsSouthern(zoneId));
    }

    [Theory]
    [InlineData(2026, 10, 16, "America/Santiago", "spring")]
    [InlineData(2026, 10, 16, "Europe/Madrid", "autumn")]
    [InlineData(2026, 1, 10, "Europe/Madrid", "winter")]
    [InlineData(2026, 4, 1, "Australia/Sydney", "autumn")]
    [InlineData(2026, 7, 1, "Australia/Sydney", "winter")]
    [InlineData(2026, 3, 1, "America/New_York", "spring")]
    [InlineData(2026, 12, 31, "America/New_York", "winter")]
    public void AutomaticWearsTheLocalSeason(int year, int month, int day, string zoneId, string theme)
    {
        Assert.Equal(theme, StudioThemes.Automatic(new DateOnly(year, month, day), zoneId).Id);
    }

    [Theory]
    [InlineData(7, "Europe/Madrid")]
    [InlineData(8, "America/New_York")]
    [InlineData(1, "America/Santiago")]
    [InlineData(12, "Pacific/Auckland")]
    public void SummerWearsForest(int month, string zoneId)
    {
        Assert.Equal(StudioThemes.Forest, StudioThemes.Automatic(new DateOnly(2026, month, 15), zoneId));
    }

    [Fact]
    public void OverAndContrastMatchKnownValues()
    {
        Assert.Equal(0xFF404040u, WorstBackdrop);
        Assert.Equal(21, StudioThemes.Contrast(0xFFFFFFFF, 0xFF000000), 3);
        Assert.Equal(0xFF808080u, StudioThemes.Over(0x80FFFFFF, 0xFF000000));
    }
}
