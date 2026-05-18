using System.Diagnostics;
using System.IO;
using System.Text.Json;

namespace SpotifyTaskbarPlayer.Services;

/// <summary>
/// Loads / saves <see cref="PlayerSettings"/> as JSON in
/// %AppData%\SpotifyTaskbarPlayer\settings.json. Exposes a process-wide
/// <see cref="Current"/> singleton and a <see cref="Changed"/> event so
/// subscribers can re-apply state after the settings window saves.
/// </summary>
public static class SettingsService
{
    private static readonly string Dir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                     "SpotifyTaskbarPlayer");
    private static readonly string FilePath = Path.Combine(Dir, "settings.json");

    public static PlayerSettings Current { get; private set; } = new();

    /// <summary>Raised on the calling thread (UI dispatcher).</summary>
    public static event EventHandler? Changed;

    public static void Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            var json = File.ReadAllText(FilePath);
            var loaded = JsonSerializer.Deserialize<PlayerSettings>(json);
            if (loaded is not null) Current = loaded;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SettingsService] load failed: {ex.Message}");
        }
    }

    public static void Save(PlayerSettings updated)
    {
        Current = updated;
        try
        {
            Directory.CreateDirectory(Dir);
            var json = JsonSerializer.Serialize(updated,
                new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(FilePath, json);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SettingsService] save failed: {ex.Message}");
        }
        Changed?.Invoke(null, EventArgs.Empty);
    }
}
