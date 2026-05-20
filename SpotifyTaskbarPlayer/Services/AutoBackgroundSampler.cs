using Microsoft.UI.Dispatching;
using SpotifyTaskbarPlayer.Interop;
using Windows.UI;

namespace SpotifyTaskbarPlayer.Services;

/// <summary>
/// Continuously samples the on-screen pixel just outside the widget on the
/// taskbar band, averages a small horizontal strip, and applies the smoothed
/// result via the supplied callback so the widget visually blends with the
/// surrounding acrylic taskbar.
/// </summary>
public sealed class AutoBackgroundSampler : IDisposable
{
    private const int IntervalMs       = 500;
    private const int SampleCount      = 1;
    private const int SampleStepPx     = 1;
    private const int EdgeOffsetPx     = 1;
    private const double SmoothFactor  = 0.30; // 0..1, higher = snappier
    private const int DeltaThreshold   = 2;    // sum-of-channels, skip if below

    private readonly DispatcherQueue _ui;
    private readonly Func<IntPtr> _widgetHwnd;
    private readonly Func<User32.RECT> _taskbarRect;
    private readonly Action<Color> _apply;

    private DispatcherQueueTimer? _timer;
    private (byte r, byte g, byte b)? _current;
    private bool _enabled;

    public AutoBackgroundSampler(DispatcherQueue ui,
                                 Func<IntPtr> widgetHwnd,
                                 Func<User32.RECT> taskbarRect,
                                 Action<Color> apply)
    {
        _ui = ui;
        _widgetHwnd = widgetHwnd;
        _taskbarRect = taskbarRect;
        _apply = apply;
    }

    public void SetEnabled(bool on)
    {
        if (on == _enabled) return;
        _enabled = on;
        if (on)
        {
            _current = null;
            _timer = _ui.CreateTimer();
            _timer.Interval = TimeSpan.FromMilliseconds(IntervalMs);
            _timer.IsRepeating = true;
            _timer.Tick += OnTick;
            _timer.Start();
            Tick();
        }
        else if (_timer is not null)
        {
            _timer.Tick -= OnTick;
            _timer.Stop();
            _timer = null;
        }
    }

    private void OnTick(DispatcherQueueTimer sender, object args) => Tick();

    private void Tick()
    {
        var hwnd = _widgetHwnd();
        if (hwnd == IntPtr.Zero) return;
        if (!User32.GetWindowRect(hwnd, out var w)) return;
        var t = _taskbarRect();
        if (t.Right <= t.Left || t.Bottom <= t.Top) return;

        bool right = (t.Right - w.Right) >= (SampleCount * SampleStepPx + EdgeOffsetPx);
        int startX = right ? w.Right + EdgeOffsetPx : w.Left - EdgeOffsetPx;
        int yMid = t.Top + (t.Bottom - t.Top) / 2;

        var dc = User32.GetDC(IntPtr.Zero);
        if (dc == IntPtr.Zero) return;
        try
        {
            long sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (int i = 0; i < SampleCount; i++)
            {
                int x = right ? startX + i * SampleStepPx : startX - i * SampleStepPx;
                if (x < t.Left + 1 || x > t.Right - 2) continue;
                uint raw = User32.GetPixel(dc, x, yMid);
                if (raw == 0xFFFFFFFF) continue; // CLR_INVALID
                sr += raw & 0xFF;
                sg += (raw >> 8) & 0xFF;
                sb += (raw >> 16) & 0xFF;
                n++;
            }
            if (n == 0) return;

            byte r = (byte)(sr / n);
            byte g = (byte)(sg / n);
            byte b = (byte)(sb / n);

            if (_current is null)
            {
                _current = (r, g, b);
                _apply(Color.FromArgb(0xFF, r, g, b));
                return;
            }

            var (cr, cg, cb) = _current.Value;
            byte nr = (byte)(cr + (int)Math.Round((r - cr) * SmoothFactor));
            byte ng = (byte)(cg + (int)Math.Round((g - cg) * SmoothFactor));
            byte nb = (byte)(cb + (int)Math.Round((b - cb) * SmoothFactor));

            int delta = Math.Abs(nr - cr) + Math.Abs(ng - cg) + Math.Abs(nb - cb);
            if (delta < DeltaThreshold) return;

            _current = (nr, ng, nb);
            _apply(Color.FromArgb(0xFF, nr, ng, nb));
        }
        finally
        {
            User32.ReleaseDC(IntPtr.Zero, dc);
        }
    }

    public void Dispose() => SetEnabled(false);
}
