using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using SpotifyTaskbarPlayer.Interop;
using SpotifyTaskbarPlayer.Services;
using Windows.Graphics;
using Windows.UI;
using WinRT.Interop;

namespace SpotifyTaskbarPlayer;

public sealed partial class SettingsWindow : Window
{
    private bool _suppressApply = true;

    public SettingsWindow()
    {
        InitializeComponent();
        Title = "Spotify Taskbar Player — Settings";

        var hwnd = WindowNative.GetWindowHandle(this);
        var id = Win32Interop.GetWindowIdFromWindow(hwnd);
        var appWindow = AppWindow.GetFromWindowId(id);
        appWindow.Resize(new SizeInt32(480, 600));

        LoadFromSettings();
        _suppressApply = false;
    }

    private void LoadFromSettings()
    {
        _suppressApply = true;
        var s = SettingsService.Current;
        SpotifyOnlyToggle.IsOn = s.SpotifyOnly;
        TintToggle.IsOn = s.TintFromAlbumArt;
        LeftPaddingBox.Value = s.LeftPadding;
        PlayerWidthBox.Value = s.PlayerWidth;
        OpacitySlider.Value = s.Opacity;
        AutoBgToggle.IsOn = s.AutoBackground;
        BgColorPicker.Color = UnpackArgb(s.BackgroundColor);
        UpdateOpacityLabel();
        _suppressApply = false;
    }

    private static Color UnpackArgb(uint argb) => Color.FromArgb(
        (byte)((argb >> 24) & 0xFF),
        (byte)((argb >> 16) & 0xFF),
        (byte)((argb >> 8) & 0xFF),
        (byte)(argb & 0xFF));

    private static uint PackArgb(Color c)
        => ((uint)c.A << 24) | ((uint)c.R << 16) | ((uint)c.G << 8) | c.B;

    private void Apply()
    {
        if (_suppressApply) return;
        SettingsService.Save(new PlayerSettings
        {
            SpotifyOnly = SpotifyOnlyToggle.IsOn,
            TintFromAlbumArt = TintToggle.IsOn,
            LeftPadding = SanitizeInt(LeftPaddingBox.Value, 4),
            PlayerWidth = SanitizeInt(PlayerWidthBox.Value, 280),
            Opacity = OpacitySlider.Value,
            BackgroundColor = PackArgb(BgColorPicker.Color),
            AutoBackground = AutoBgToggle.IsOn,
        });
    }

    private void OnBgColorChanged(ColorPicker sender, ColorChangedEventArgs args) => Apply();

    private void OnEyedropper(object sender, RoutedEventArgs e)
    {
        // Hide settings window, poll for next left-click anywhere on screen,
        // read the pixel under the cursor, restore window.
        var hwnd = WindowNative.GetWindowHandle(this);
        var id = Win32Interop.GetWindowIdFromWindow(hwnd);
        var app = AppWindow.GetFromWindowId(id);
        app.Hide();

        var timer = DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(30);
        timer.IsRepeating = true;
        // Wait until LBUTTON is RELEASED first (avoid catching the click that
        // dismissed the button), then wait for the next press.
        bool waitingForRelease = true;
        timer.Tick += (_, _) =>
        {
            bool down = (User32.GetAsyncKeyState(User32.VK_LBUTTON) & 0x8000) != 0;
            bool esc  = (User32.GetAsyncKeyState(User32.VK_ESCAPE) & 0x8000) != 0;
            if (esc)
            {
                timer.Stop();
                app.Show();
                return;
            }
            if (waitingForRelease)
            {
                if (!down) waitingForRelease = false;
                return;
            }
            if (!down) return;
            timer.Stop();

            User32.GetCursorPos(out var p);
            var dc = User32.GetDC(IntPtr.Zero);
            var raw = User32.GetPixel(dc, p.X, p.Y);
            User32.ReleaseDC(IntPtr.Zero, dc);

            // COLORREF = 0x00BBGGRR
            byte r = (byte)(raw & 0xFF);
            byte g = (byte)((raw >> 8) & 0xFF);
            byte b = (byte)((raw >> 16) & 0xFF);
            BgColorPicker.Color = Color.FromArgb(0xFF, r, g, b);
            // ColorChanged fires which calls Apply — settings persisted.

            app.Show();
        };
        timer.Start();
    }

    private static int SanitizeInt(double value, int fallback)
        => double.IsNaN(value) ? fallback : (int)value;

    private void OnAnyChanged(object sender, RoutedEventArgs e) => Apply();

    private void OnNumberChanged(NumberBox sender, NumberBoxValueChangedEventArgs args) => Apply();

    private void OnSliderChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        UpdateOpacityLabel();
        Apply();
    }

    private void UpdateOpacityLabel()
    {
        if (OpacityLabel is null) return;
        OpacityLabel.Text = $"Прозрачность плеера: {OpacitySlider.Value * 100:F0}%";
    }

    private void OnReset(object sender, RoutedEventArgs e)
    {
        SettingsService.Save(new PlayerSettings());
        LoadFromSettings();
    }

    private void OnClose(object sender, RoutedEventArgs e) => Close();
}
