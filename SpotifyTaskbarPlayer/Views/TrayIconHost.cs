using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using SpotifyTaskbarPlayer.Interop;
using SpotifyTaskbarPlayer.Services;

namespace SpotifyTaskbarPlayer.Views;

/// <summary>
/// Pure-Win32 tray icon + popup menu. Owns a hidden message-only window that
/// receives <c>WM_TRAY</c> callbacks from Shell_NotifyIcon and dispatches
/// popup menu commands.
/// </summary>
public sealed class TrayIconHost : IDisposable
{
    private const string WindowClassName = "SpotifyTaskbarPlayer_TrayHost";
    private const uint IconId = 1;

    // Menu command ids
    private const uint CmdOpenSpotify = 100;
    private const uint CmdAutostart   = 101;
    private const uint CmdSettings    = 102;
    private const uint CmdExit        = 999;

    private TrayInterop.WndProcDelegate? _wndProc; // keep alive against GC
    private IntPtr _hwnd;
    private IntPtr _hIcon;
    private bool _registered;
    private SettingsWindow? _settingsWindow;

    public void Initialize()
    {
        _wndProc = WndProc;

        var hInstance = TrayInterop.GetModuleHandle(null);

        var wc = new TrayInterop.WNDCLASS
        {
            lpfnWndProc = _wndProc,
            hInstance = hInstance,
            lpszClassName = WindowClassName,
        };
        TrayInterop.RegisterClass(ref wc); // ignore failure (already registered on relaunch)

        _hwnd = TrayInterop.CreateWindowEx(0, WindowClassName, null, 0, 0, 0, 0, 0,
            new IntPtr(TrayInterop.HWND_MESSAGE), IntPtr.Zero, hInstance, IntPtr.Zero);
        if (_hwnd == IntPtr.Zero)
        {
            Debug.WriteLine("[TrayIconHost] CreateWindowEx failed");
            return;
        }

        _hIcon = LoadTrayIcon();

        var nid = new TrayInterop.NOTIFYICONDATA
        {
            cbSize = (uint)Marshal.SizeOf<TrayInterop.NOTIFYICONDATA>(),
            hWnd = _hwnd,
            uID = IconId,
            uFlags = TrayInterop.NIF_MESSAGE | TrayInterop.NIF_ICON | TrayInterop.NIF_TIP,
            uCallbackMessage = TrayInterop.WM_TRAY,
            hIcon = _hIcon,
            szTip = "Spotify Taskbar Player",
        };
        _registered = TrayInterop.Shell_NotifyIcon(TrayInterop.NIM_ADD, ref nid);
        if (!_registered) Debug.WriteLine("[TrayIconHost] Shell_NotifyIcon ADD failed");
    }

    /// <summary>
    /// Picks an icon from shell32.dll. Index 40 in modern shells is a music
    /// note glyph; falls back to a generic icon if extraction fails.
    /// </summary>
    private static IntPtr LoadTrayIcon()
    {
        try
        {
            // Pull the icon embedded into our own exe (ApplicationIcon in csproj).
            var exe = Environment.ProcessPath;
            if (!string.IsNullOrEmpty(exe))
            {
                var small = new IntPtr[1];
                if (TrayInterop.ExtractIconEx(exe, 0, null, small, 1) > 0
                    && small[0] != IntPtr.Zero)
                    return small[0];
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[TrayIconHost] icon extract failed: {ex.Message}");
        }
        return TrayInterop.LoadIcon(IntPtr.Zero, new IntPtr(32512)); // IDI_APPLICATION
    }

    private IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == TrayInterop.WM_TRAY && (uint)lParam.ToInt64() == TrayInterop.WM_RBUTTONUP)
        {
            ShowContextMenu();
            return IntPtr.Zero;
        }
        return TrayInterop.DefWindowProc(hWnd, msg, wParam, lParam);
    }

    private void ShowContextMenu()
    {
        var menu = TrayInterop.CreatePopupMenu();
        if (menu == IntPtr.Zero) return;

        TrayInterop.AppendMenu(menu, TrayInterop.MF_STRING, CmdOpenSpotify, "Open Spotify");

        var autostartFlags = TrayInterop.MF_STRING
            | (AutostartService.IsEnabled() ? TrayInterop.MF_CHECKED : 0);
        TrayInterop.AppendMenu(menu, autostartFlags, CmdAutostart, "Start with Windows");

        TrayInterop.AppendMenu(menu, TrayInterop.MF_STRING, CmdSettings, "Settings…");
        TrayInterop.AppendMenu(menu, TrayInterop.MF_SEPARATOR, 0, null);
        TrayInterop.AppendMenu(menu, TrayInterop.MF_STRING, CmdExit, "Exit");

        TrayInterop.GetCursorPos(out var p);
        // Required by Win32 docs — otherwise menu won't dismiss when clicking
        // elsewhere.
        TrayInterop.SetForegroundWindow(_hwnd);

        var cmd = (uint)TrayInterop.TrackPopupMenu(menu,
            TrayInterop.TPM_RETURNCMD | TrayInterop.TPM_RIGHTBUTTON,
            p.X, p.Y, 0, _hwnd, IntPtr.Zero);

        TrayInterop.DestroyMenu(menu);

        switch (cmd)
        {
            case CmdOpenSpotify: OpenSpotify(); break;
            case CmdAutostart:   AutostartService.SetEnabled(!AutostartService.IsEnabled()); break;
            case CmdSettings:    OpenSettings(); break;
            case CmdExit:        Application.Current.Exit(); break;
        }
    }

    private static void OpenSpotify()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "spotify:",
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[TrayIconHost] Open Spotify failed: {ex.Message}");
        }
    }

    private void OpenSettings()
    {
        if (_settingsWindow is not null)
        {
            _settingsWindow.Activate();
            return;
        }
        _settingsWindow = new SettingsWindow();
        _settingsWindow.Closed += (_, _) => _settingsWindow = null;
        _settingsWindow.Activate();
    }

    public void Dispose()
    {
        if (_registered)
        {
            var nid = new TrayInterop.NOTIFYICONDATA
            {
                cbSize = (uint)Marshal.SizeOf<TrayInterop.NOTIFYICONDATA>(),
                hWnd = _hwnd,
                uID = IconId,
            };
            TrayInterop.Shell_NotifyIcon(TrayInterop.NIM_DELETE, ref nid);
            _registered = false;
        }
        if (_hIcon != IntPtr.Zero)
        {
            TrayInterop.DestroyIcon(_hIcon);
            _hIcon = IntPtr.Zero;
        }
        if (_hwnd != IntPtr.Zero)
        {
            TrayInterop.DestroyWindow(_hwnd);
            _hwnd = IntPtr.Zero;
        }
    }
}
