using Microsoft.Win32;

namespace SpotifyTaskbarPlayer.Services;

/// <summary>
/// Toggles "Start with Windows" by writing the player's full exe path to
/// HKCU\Software\Microsoft\Windows\CurrentVersion\Run. No admin needed.
/// </summary>
public static class AutostartService
{
    private const string RunKey   = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueId  = "SpotifyTaskbarPlayer";

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return key?.GetValue(ValueId) is not null;
    }

    public static void SetEnabled(bool on)
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true)
            ?? Registry.CurrentUser.CreateSubKey(RunKey)!;
        if (on)
        {
            var path = Environment.ProcessPath ?? "";
            key.SetValue(ValueId, $"\"{path}\"");
        }
        else
        {
            key.DeleteValue(ValueId, throwOnMissingValue: false);
        }
    }
}
