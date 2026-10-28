using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using CanopyStudio.Core.Assets;
using CanopyStudio.Core.Identity;
using CanopyStudio.Core.Install;
using CanopyStudio.Core.Setup;
using CanopyStudio.Services;
using CanopyStudio.ViewModels;
using CanopyStudio.Views;

namespace CanopyStudio;

public sealed partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            GameSession session = new(SettingsStore.ForUser());
            // Before the window: the theme's resources must be in place when
            // it first draws.
            AppearanceService appearance = new(session, GameAssetCache.ForUser(), this);
            // Before the folder opens: it asks GitHub for the latest Canopy
            // when the first folder comes into use.
            CanopyService canopy = new(session, new ReleaseFeed(new HttpClient { Timeout = TimeSpan.FromSeconds(30) }));
            // The game's names and icons, cached per game build.
            GameAssetsService assets = new(session, VanillaAssetCache.ForUser());
            ShellWindow window = new();
            ShellViewModel shell = new(session, appearance, canopy, assets, window);
            window.DataContext = shell;

            StudioSettings settings = session.Settings;
            window.Width = settings.WindowWidth;
            window.Height = settings.WindowHeight;
            if (settings.WindowMaximized)
            {
                window.WindowState = WindowState.Maximized;
            }
            window.Opened += async (_, _) => await shell.StartAsync();
            window.Closing += (_, _) => session.Remember(current => current with
            {
                LastPage = shell.CurrentPageId ?? current.LastPage,
                WindowMaximized = window.WindowState == WindowState.Maximized,
                // A maximized window's size is the screen's; keep the last
                // normal size for next time.
                WindowWidth = window.WindowState == WindowState.Normal ? window.Width : current.WindowWidth,
                WindowHeight = window.WindowState == WindowState.Normal ? window.Height : current.WindowHeight,
            });
            // The sound device stays open while Studio runs.
            window.Closed += (_, _) => shell.Game.Sound.Dispose();
            desktop.MainWindow = window;
        }
        base.OnFrameworkInitializationCompleted();
    }
}
