using Avalonia;
#if CANOPY_LIVE_INSPECT
using Zafiro.Avalonia.Mcp.AppHost;
#endif

namespace CanopyStudio;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args) =>
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

    // Also what the Avalonia previewer calls.
    public static AppBuilder BuildAvaloniaApp()
    {
        AppBuilder builder = AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
#if CANOPY_LIVE_INSPECT
        builder = builder.UseMcpDiagnostics();
#endif
        return builder;
    }
}
