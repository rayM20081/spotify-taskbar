using System.Runtime.InteropServices;

namespace SpotifyTaskbarPlayer.Interop;

/// <summary>
/// Undocumented Windows accent / composition API. This is the same surface
/// TranslucentTB and Windhawk use to apply blur / acrylic to arbitrary
/// windows. Microsoft does not officially support these for third-party
/// apps; behavior may change between Windows builds.
/// </summary>
public static class AccentApi
{
    public enum AccentState : uint
    {
        ACCENT_DISABLED = 0,
        ACCENT_ENABLE_GRADIENT = 1,
        ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
        ACCENT_ENABLE_BLURBEHIND = 3,
        ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
        ACCENT_ENABLE_HOSTBACKDROP = 5,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ACCENT_POLICY
    {
        public AccentState AccentState;
        public uint AccentFlags;
        public uint GradientColor; // 0xAABBGGRR
        public uint AnimationId;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct WINDOWCOMPOSITIONATTRIBDATA
    {
        public uint Attrib;     // WCA_ACCENT_POLICY = 19
        public IntPtr pvData;
        public uint cbData;
    }

    public const uint WCA_ACCENT_POLICY = 19;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern int SetWindowCompositionAttribute(IntPtr hwnd,
        ref WINDOWCOMPOSITIONATTRIBDATA data);

    public static void ApplyAcrylic(IntPtr hwnd, uint gradientArgb)
    {
        var accent = new ACCENT_POLICY
        {
            AccentState = AccentState.ACCENT_ENABLE_ACRYLICBLURBEHIND,
            AccentFlags = 2, // draw all borders
            GradientColor = gradientArgb,
        };
        var size = Marshal.SizeOf<ACCENT_POLICY>();
        var ptr = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(accent, ptr, false);
            var data = new WINDOWCOMPOSITIONATTRIBDATA
            {
                Attrib = WCA_ACCENT_POLICY,
                pvData = ptr,
                cbData = (uint)size,
            };
            SetWindowCompositionAttribute(hwnd, ref data);
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }
}
