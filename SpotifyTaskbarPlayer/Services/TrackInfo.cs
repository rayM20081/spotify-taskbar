using Windows.Storage.Streams;

namespace SpotifyTaskbarPlayer.Services;

public sealed record TrackInfo
{
    public string Title { get; init; } = "";
    public string Artist { get; init; } = "";
    public string Album { get; init; } = "";
    public string SourceAppId { get; init; } = "";
    public IRandomAccessStreamReference? ThumbnailRef { get; init; }

    public bool IsEmpty => string.IsNullOrEmpty(Title) && string.IsNullOrEmpty(Artist);

    public static TrackInfo Empty { get; } = new();
}

public sealed record PlaybackState
{
    public bool IsPlaying { get; init; }
    public bool CanPlayPause { get; init; }
    public bool CanNext { get; init; }
    public bool CanPrevious { get; init; }
    public bool CanSeek { get; init; }
}

public sealed record TimelineState
{
    public TimeSpan Position { get; init; }
    public TimeSpan Duration { get; init; }
    public DateTimeOffset LastUpdated { get; init; }
}
