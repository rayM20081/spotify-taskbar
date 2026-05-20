using System.Diagnostics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using SpotifyTaskbarPlayer.Interop;
using SpotifyTaskbarPlayer.Services;
using Windows.Graphics;
using WinRT.Interop;

namespace SpotifyTaskbarPlayer;

public sealed partial class MainWindow : Window
{
    private const string PlayGlyph  = ""; // Segoe Fluent Icons "Play"
    private const string PauseGlyph = ""; // Segoe Fluent Icons "Pause"

    private int PlayerWidth => SettingsService.Current.PlayerWidth;
    private int LeftPadding => SettingsService.Current.LeftPadding;

    private readonly SmtcService _smtc;
    private readonly TaskbarTracker _taskbar;
    private readonly AutoBackgroundSampler _bgSampler;
    private AppWindow? _appWindow;
    private IntPtr _hwnd;
    private bool _firstActivation = true;
    private AlbumColorExtractor.MeanRgb? _lastCoverMean;

    public MainWindow()
    {
        InitializeComponent();
        Title = "Spotify Taskbar Player";

        ConfigureChrome();

        _smtc = new SmtcService(DispatcherQueue) { SpotifyOnly = SettingsService.Current.SpotifyOnly };
        _smtc.TrackChanged += OnTrackChanged;
        _smtc.PlaybackChanged += OnPlaybackChanged;
        _smtc.TimelineChanged += OnTimelineChanged;
        _smtc.SessionLost += OnSessionLost;

        _taskbar = new TaskbarTracker(DispatcherQueue);
        _taskbar.TaskbarMoved += OnTaskbarMoved;

        _bgSampler = new AutoBackgroundSampler(
            DispatcherQueue,
            () => _hwnd,
            () => _taskbar.CurrentRect,
            c => RootGrid.Background = new SolidColorBrush(c));

        Activated += OnFirstActivated;
        SettingsService.Changed += OnSettingsChanged;
        Closed += (_, _) =>
        {
            SettingsService.Changed -= OnSettingsChanged;
            _bgSampler.Dispose();
            _smtc.Dispose();
            _taskbar.Dispose();
        };

        ApplyOpacity();
        ApplyBackgroundColor();
        _ = InitSmtcAsync();
    }

    private void OnSettingsChanged(object? sender, EventArgs e)
    {
        _smtc.SpotifyOnly = SettingsService.Current.SpotifyOnly;
        ApplyOpacity();
        ApplyBackgroundColor();
        _bgSampler.SetEnabled(SettingsService.Current.AutoBackground);
        if (_taskbar.IsAttached)
            OnTaskbarMoved(this, _taskbar.CurrentRect);
        ApplyAccentFromCache();
    }

    private void ApplyOpacity()
    {
        RootGrid.Opacity = SettingsService.Current.Opacity;
    }

    private void ApplyBackgroundColor()
    {
        if (SettingsService.Current.AutoBackground) return; // sampler owns it
        var argb = SettingsService.Current.BackgroundColor;
        var c = Windows.UI.Color.FromArgb(
            (byte)((argb >> 24) & 0xFF),
            (byte)((argb >> 16) & 0xFF),
            (byte)((argb >> 8) & 0xFF),
            (byte)(argb & 0xFF));
        RootGrid.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(c);
    }

    private void OnFirstActivated(object sender, WindowActivatedEventArgs args)
    {
        if (!_firstActivation) return;
        _firstActivation = false;
        if (!_taskbar.Start())
        {
            Debug.WriteLine("[Main] Shell_TrayWnd not found");
            return;
        }
        EmbedIntoTaskbar();
        _bgSampler.SetEnabled(SettingsService.Current.AutoBackground);
    }

    private void EmbedIntoTaskbar()
    {
        var parent = _taskbar.ShellTrayHwnd;
        if (parent == IntPtr.Zero) return;

        var style = (long)User32.GetWindowLongPtr(_hwnd, User32.GWL_STYLE);
        style |= User32.WS_CHILD;
        style &= ~User32.WS_POPUP;
        User32.SetWindowLongPtr(_hwnd, User32.GWL_STYLE, new IntPtr(style));

        User32.SetParent(_hwnd, parent);

        var h = _taskbar.CurrentRect.Height;
        User32.SetWindowPos(_hwnd, IntPtr.Zero,
            LeftPadding, 0, PlayerWidth, h,
            User32.SWP_NOACTIVATE | User32.SWP_SHOWWINDOW);
    }

    private void ConfigureChrome()
    {
        _hwnd = WindowNative.GetWindowHandle(this);
        var windowId = Win32Interop.GetWindowIdFromWindow(_hwnd);
        _appWindow = AppWindow.GetFromWindowId(windowId);

        if (_appWindow.Presenter is OverlappedPresenter p)
        {
            p.SetBorderAndTitleBar(false, false);
            p.IsResizable = false;
            p.IsMaximizable = false;
            p.IsMinimizable = false;
            p.IsAlwaysOnTop = true;
        }

        // Disable the 1-px DWM border that Win11 paints by default.
        uint none = DwmApi.DWMWA_COLOR_NONE;
        DwmApi.DwmSetWindowAttribute(_hwnd, DwmApi.DWMWA_BORDER_COLOR, ref none, sizeof(uint));
    }

    private async Task InitSmtcAsync()
    {
        try
        {
            await _smtc.InitializeAsync();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Main] SMTC init failed: {ex.Message}");
        }
    }

    private void OnTaskbarMoved(object? sender, User32.RECT r)
    {
        User32.SetWindowPos(_hwnd, IntPtr.Zero,
            LeftPadding, 0, PlayerWidth, r.Height,
            User32.SWP_NOACTIVATE | User32.SWP_NOZORDER);
    }

    private async void OnTrackChanged(object? sender, TrackInfo t)
    {
        TitleText.Text = string.IsNullOrEmpty(t.Title) ? "(no track)" : t.Title;
        ArtistText.Text = t.Artist;
        await LoadAlbumArtAndAccentAsync(t);
    }

    private async Task LoadAlbumArtAndAccentAsync(TrackInfo t)
    {
        _lastCoverMean = null;
        if (t.ThumbnailRef is null)
        {
            AlbumArt.Source = null;
            ResetAccent();
            return;
        }
        try
        {
            // Read once into a byte[]; spawn an independent
            // InMemoryRandomAccessStream per consumer. `CopyAsync` strips
            // content-type, which breaks BitmapDecoder for some encoders.
            byte[] bytes;
            using (var source = await t.ThumbnailRef.OpenReadAsync())
            {
                var size = (uint)source.Size;
                var reader = new Windows.Storage.Streams.DataReader(source);
                await reader.LoadAsync(size);
                bytes = new byte[size];
                reader.ReadBytes(bytes);
            }

            _lastCoverMean = await AlbumColorExtractor.ComputeMeanAsync(await StreamFromBytesAsync(bytes));

            var bmp = new BitmapImage();
            await bmp.SetSourceAsync(await StreamFromBytesAsync(bytes));
            AlbumArt.Source = bmp;

            ApplyAccentFromCache();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Main] cover load failed: {ex.Message}");
            AlbumArt.Source = null;
            ResetAccent();
        }
    }

    private static async Task<Windows.Storage.Streams.InMemoryRandomAccessStream>
        StreamFromBytesAsync(byte[] bytes)
    {
        var s = new Windows.Storage.Streams.InMemoryRandomAccessStream();
        var writer = new Windows.Storage.Streams.DataWriter(s);
        writer.WriteBytes(bytes);
        await writer.StoreAsync();
        await writer.FlushAsync();
        writer.DetachStream();
        s.Seek(0);
        return s;
    }

    private void ApplyAccentFromCache()
    {
        if (!SettingsService.Current.TintFromAlbumArt || _lastCoverMean is null)
        {
            ResetAccent();
            return;
        }
        var color = AlbumColorExtractor.BoostSaturation(_lastCoverMean.Value,
            SettingsService.Current.SaturationBoost);
        TimelineBar.Foreground = new SolidColorBrush(color);
    }

    private void ResetAccent()
    {
        TimelineBar.ClearValue(Microsoft.UI.Xaml.Controls.Control.ForegroundProperty);
    }

    private void OnPlaybackChanged(object? sender, PlaybackState s)
    {
        PlayPauseButton.Content = s.IsPlaying ? PauseGlyph : PlayGlyph;
        PrevButton.IsEnabled = s.CanPrevious;
        NextButton.IsEnabled = s.CanNext;
        PlayPauseButton.IsEnabled = s.CanPlayPause;
    }

    private void OnTimelineChanged(object? sender, TimelineState t)
    {
        var dur = t.Duration.TotalSeconds;
        TimelineBar.Maximum = dur > 0 ? dur : 1;
        TimelineBar.Value = Math.Clamp(t.Position.TotalSeconds, 0, TimelineBar.Maximum);
    }

    private void OnSessionLost(object? sender, EventArgs e)
    {
        TitleText.Text = "(no active session)";
        ArtistText.Text = "";
        AlbumArt.Source = null;
        TimelineBar.Value = 0;
    }

    private async void OnPrev(object sender, RoutedEventArgs e) => await _smtc.PreviousAsync();
    private async void OnPlayPause(object sender, RoutedEventArgs e) => await _smtc.PlayPauseAsync();
    private async void OnNext(object sender, RoutedEventArgs e) => await _smtc.NextAsync();

    private void OnOpenSpotifyTap(object sender, Microsoft.UI.Xaml.Input.TappedRoutedEventArgs e)
    {
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "spotify:",
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Main] open Spotify failed: {ex.Message}");
        }
    }
}
