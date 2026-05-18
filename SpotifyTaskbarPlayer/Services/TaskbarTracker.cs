using System.Diagnostics;
using Microsoft.UI.Dispatching;
using SpotifyTaskbarPlayer.Interop;

namespace SpotifyTaskbarPlayer.Services;

/// <summary>
/// Tracks the primary Windows taskbar (<c>Shell_TrayWnd</c>) and raises
/// <see cref="TaskbarMoved"/> whenever its bounds change. Internally relies on
/// the WinEvent hook EVENT_OBJECT_LOCATIONCHANGE.
/// </summary>
public sealed class TaskbarTracker : IDisposable
{
    private readonly DispatcherQueue _ui;
    // Stored in a field so the GC never collects the unmanaged-callable thunk.
    private readonly User32.WinEventDelegate _callback;

    private IntPtr _hook;
    private IntPtr _shellTrayHwnd;
    private User32.RECT _lastRect;
    private DispatcherQueueTimer? _debounce;
    private bool _disposed;

    public TaskbarTracker(DispatcherQueue ui)
    {
        _ui = ui;
        _callback = OnWinEvent;
    }

    public event EventHandler<User32.RECT>? TaskbarMoved;
    public User32.RECT CurrentRect => _lastRect;
    public IntPtr ShellTrayHwnd => _shellTrayHwnd;
    public bool IsAttached => _shellTrayHwnd != IntPtr.Zero;

    public bool Start()
    {
        _shellTrayHwnd = User32.FindWindow("Shell_TrayWnd", null);
        if (_shellTrayHwnd == IntPtr.Zero)
        {
            Debug.WriteLine("[TaskbarTracker] Shell_TrayWnd not found");
            return false;
        }
        User32.GetWindowRect(_shellTrayHwnd, out _lastRect);

        _debounce = _ui.CreateTimer();
        _debounce.Interval = TimeSpan.FromMilliseconds(50);
        _debounce.IsRepeating = false;
        _debounce.Tick += (_, _) => Publish();

        _hook = User32.SetWinEventHook(
            User32.EVENT_OBJECT_LOCATIONCHANGE,
            User32.EVENT_OBJECT_LOCATIONCHANGE,
            IntPtr.Zero, _callback, 0, 0,
            User32.WINEVENT_OUTOFCONTEXT | User32.WINEVENT_SKIPOWNPROCESS);

        if (_hook == IntPtr.Zero)
        {
            Debug.WriteLine("[TaskbarTracker] SetWinEventHook failed");
            return false;
        }

        // Publish the initial state so subscribers don't have to wait for the
        // first taskbar movement.
        TaskbarMoved?.Invoke(this, _lastRect);
        return true;
    }

    private void OnWinEvent(IntPtr hook, uint eventType, IntPtr hwnd,
                            int idObject, int idChild,
                            uint dwEventThread, uint dwmsEventTime)
    {
        if (hwnd != _shellTrayHwnd) return;
        if (idObject != User32.OBJID_WINDOW) return;
        // Marshal to UI thread and debounce. The hook fires many times in a
        // row during animations; we only care about the final position.
        _ui.TryEnqueue(() => _debounce?.Start());
    }

    private void Publish()
    {
        if (!User32.GetWindowRect(_shellTrayHwnd, out var newRect)) return;
        if (newRect.Left == _lastRect.Left && newRect.Top == _lastRect.Top
            && newRect.Right == _lastRect.Right && newRect.Bottom == _lastRect.Bottom)
            return;
        _lastRect = newRect;
        TaskbarMoved?.Invoke(this, newRect);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        if (_hook != IntPtr.Zero)
        {
            User32.UnhookWinEvent(_hook);
            _hook = IntPtr.Zero;
        }
        _debounce?.Stop();
    }
}
