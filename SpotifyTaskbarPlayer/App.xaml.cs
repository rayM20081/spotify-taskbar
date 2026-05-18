using Microsoft.UI.Xaml;
using SpotifyTaskbarPlayer.Views;

namespace SpotifyTaskbarPlayer;

public partial class App : Application
{
    private Window? _window;
    private TrayIconHost? _tray;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Services.SettingsService.Load();

        _window = new MainWindow();
        _window.Activate();

        _tray = new TrayIconHost();
        _tray.Initialize();
    }
}
