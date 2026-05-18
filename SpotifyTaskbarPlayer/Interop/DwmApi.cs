using System.Runtime.InteropServices;

namespace SpotifyTaskbarPlayer.Interop;

public static class DwmApi
{
    [DllImport("dwmapi.dll")]
    public static extern int DwmSetWindowAttribute(IntPtr hwnd, uint dwAttribute,
        ref uint pvAttribute, uint cbAttribute);

    public const uint DWMWA_BORDER_COLOR  = 34;
    public const uint DWMWA_COLOR_NONE    = 0xFFFFFFFE;
    public const uint DWMWA_COLOR_DEFAULT = 0xFFFFFFFF;
}
