namespace SpotifyTaskbarPlayer.Services;

public sealed class PlayerSettings
{
    public bool SpotifyOnly { get; set; } = false;
    public bool TintFromAlbumArt { get; set; } = true;
    public int LeftPadding { get; set; } = 4;
    public int PlayerWidth { get; set; } = 280;
    public double SaturationBoost { get; set; } = 1.4;
    public double Opacity { get; set; } = 1.0;
    /// <summary>Background color of the player tile, packed as 0xAARRGGBB.</summary>
    public uint BackgroundColor { get; set; } = 0xFF202020;
    /// <summary>When true, sample the taskbar pixel next to the widget on a
    /// timer and apply it as background, instead of using BackgroundColor.</summary>
    public bool AutoBackground { get; set; } = false;

    public PlayerSettings Clone() => new()
    {
        SpotifyOnly = SpotifyOnly,
        TintFromAlbumArt = TintFromAlbumArt,
        LeftPadding = LeftPadding,
        PlayerWidth = PlayerWidth,
        SaturationBoost = SaturationBoost,
        Opacity = Opacity,
        BackgroundColor = BackgroundColor,
        AutoBackground = AutoBackground,
    };
}
