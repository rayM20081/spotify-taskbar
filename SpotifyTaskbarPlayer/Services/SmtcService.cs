using System.Diagnostics;
using Microsoft.UI.Dispatching;
using Windows.Foundation;
using Windows.Media.Control;

namespace SpotifyTaskbarPlayer.Services;

using SessionManager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;

/// <summary>
/// Wraps the System Media Transport Controls (SMTC) for the current playback
/// session. Marshals all events onto the supplied UI dispatcher.
/// </summary>
public sealed class SmtcService : IDisposable
{
    private readonly DispatcherQueue _ui;
    private readonly DispatcherQueueTimer _tickTimer;
    private SessionManager? _manager;
    private Session? _currentSession;
    private bool _disposed;

    public SmtcService(DispatcherQueue uiDispatcher)
    {
        _ui = uiDispatcher;
        _tickTimer = _ui.CreateTimer();
        _tickTimer.Interval = TimeSpan.FromMilliseconds(250);
        _tickTimer.IsRepeating = true;
        _tickTimer.Tick += OnTick;
    }

    private void OnTick(DispatcherQueueTimer sender, object args)
    {
        if (_currentSession is { } s) RefreshTimeline(s);
    }

    /// <summary>If true, only sessions reported by Spotify are surfaced.</summary>
    public bool SpotifyOnly { get; set; }

    public TrackInfo CurrentTrack { get; private set; } = TrackInfo.Empty;
    public PlaybackState CurrentPlayback { get; private set; } = new();
    public TimelineState CurrentTimeline { get; private set; } = new();

    public event EventHandler<TrackInfo>? TrackChanged;
    public event EventHandler<PlaybackState>? PlaybackChanged;
    public event EventHandler<TimelineState>? TimelineChanged;
    /// <summary>Raised when no session is active (e.g. Spotify closed).</summary>
    public event EventHandler? SessionLost;

    public async Task InitializeAsync()
    {
        _manager = await SessionManager.RequestAsync();
        _manager.SessionsChanged += OnSessionsChanged;
        _manager.CurrentSessionChanged += OnCurrentSessionChanged;
        AttachCurrentSession();
    }

    public Task PlayPauseAsync() => InvokeAsync(s => s.TryTogglePlayPauseAsync());
    public Task NextAsync() => InvokeAsync(s => s.TrySkipNextAsync());
    public Task PreviousAsync() => InvokeAsync(s => s.TrySkipPreviousAsync());
    public Task SeekAsync(TimeSpan position) =>
        InvokeAsync(s => s.TryChangePlaybackPositionAsync(position.Ticks));

    private async Task InvokeAsync(Func<Session, IAsyncOperation<bool>> action)
    {
        var session = _currentSession;
        if (session is null) return;
        try
        {
            await action(session);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SmtcService] command failed: {ex.Message}");
        }
    }

    private void OnSessionsChanged(SessionManager sender, SessionsChangedEventArgs args)
        => AttachCurrentSession();

    private void OnCurrentSessionChanged(SessionManager sender, CurrentSessionChangedEventArgs args)
        => AttachCurrentSession();

    private void AttachCurrentSession()
    {
        if (_manager is null) return;

        DetachCurrentSession();

        var session = PickSession(_manager);
        if (session is null)
        {
            RaiseUi(() => SessionLost?.Invoke(this, EventArgs.Empty));
            return;
        }

        _currentSession = session;
        session.MediaPropertiesChanged += OnMediaPropertiesChanged;
        session.PlaybackInfoChanged += OnPlaybackInfoChanged;
        session.TimelinePropertiesChanged += OnTimelinePropertiesChanged;

        _ = RefreshAllAsync(session);
    }

    private Session? PickSession(SessionManager mgr)
    {
        var current = mgr.GetCurrentSession();
        if (!SpotifyOnly) return current;

        if (current is not null && IsSpotify(current)) return current;
        return mgr.GetSessions().FirstOrDefault(IsSpotify);
    }

    private static bool IsSpotify(Session s)
        => s.SourceAppUserModelId?.Contains("Spotify", StringComparison.OrdinalIgnoreCase) == true;

    private void DetachCurrentSession()
    {
        if (_currentSession is null) return;
        _currentSession.MediaPropertiesChanged -= OnMediaPropertiesChanged;
        _currentSession.PlaybackInfoChanged -= OnPlaybackInfoChanged;
        _currentSession.TimelinePropertiesChanged -= OnTimelinePropertiesChanged;
        _currentSession = null;
    }

    private async Task RefreshAllAsync(Session session)
    {
        await RefreshTrackAsync(session);
        RefreshPlayback(session);
        RefreshTimeline(session);
    }

    private async void OnMediaPropertiesChanged(Session sender, MediaPropertiesChangedEventArgs args)
        => await RefreshTrackAsync(sender);

    private void OnPlaybackInfoChanged(Session sender, PlaybackInfoChangedEventArgs args)
        => RefreshPlayback(sender);

    private void OnTimelinePropertiesChanged(Session sender, TimelinePropertiesChangedEventArgs args)
        => RefreshTimeline(sender);

    private async Task RefreshTrackAsync(Session session)
    {
        try
        {
            var props = await session.TryGetMediaPropertiesAsync();
            var info = new TrackInfo
            {
                Title = props?.Title ?? "",
                Artist = props?.Artist ?? "",
                Album = props?.AlbumTitle ?? "",
                SourceAppId = session.SourceAppUserModelId ?? "",
                ThumbnailRef = props?.Thumbnail
            };
            CurrentTrack = info;
            RaiseUi(() => TrackChanged?.Invoke(this, info));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SmtcService] track refresh failed: {ex.Message}");
        }
    }

    private void RefreshPlayback(Session session)
    {
        try
        {
            var info = session.GetPlaybackInfo();
            var state = new PlaybackState
            {
                IsPlaying = info?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing,
                CanPlayPause = info?.Controls.IsPlayPauseToggleEnabled == true
                            || info?.Controls.IsPlayEnabled == true
                            || info?.Controls.IsPauseEnabled == true,
                CanNext = info?.Controls.IsNextEnabled == true,
                CanPrevious = info?.Controls.IsPreviousEnabled == true,
                CanSeek = info?.Controls.IsPlaybackPositionEnabled == true
            };
            CurrentPlayback = state;
            RaiseUi(() =>
            {
                PlaybackChanged?.Invoke(this, state);
                if (state.IsPlaying) _tickTimer.Start();
                else _tickTimer.Stop();
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SmtcService] playback refresh failed: {ex.Message}");
        }
    }

    private void RefreshTimeline(Session session)
    {
        try
        {
            var t = session.GetTimelineProperties();
            if (t is null) return;

            // Spotify reports timeline snapshots infrequently (only on seek /
            // pause toggle). Locally interpolate by adding wall-clock time
            // since LastUpdatedTime so the progress bar moves smoothly.
            var duration = t.EndTime - t.StartTime;
            var position = t.Position;
            if (CurrentPlayback.IsPlaying)
            {
                var elapsed = DateTimeOffset.Now - t.LastUpdatedTime;
                if (elapsed > TimeSpan.Zero) position += elapsed;
            }
            if (position < TimeSpan.Zero) position = TimeSpan.Zero;
            if (duration > TimeSpan.Zero && position > duration) position = duration;

            var state = new TimelineState
            {
                Position = position,
                Duration = duration,
                LastUpdated = DateTimeOffset.Now
            };
            CurrentTimeline = state;
            RaiseUi(() => TimelineChanged?.Invoke(this, state));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[SmtcService] timeline refresh failed: {ex.Message}");
        }
    }

    private void RaiseUi(Action action)
    {
        if (_ui.HasThreadAccess) action();
        else _ui.TryEnqueue(() => action());
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _tickTimer.Stop();
        _tickTimer.Tick -= OnTick;
        DetachCurrentSession();
        if (_manager is not null)
        {
            _manager.SessionsChanged -= OnSessionsChanged;
            _manager.CurrentSessionChanged -= OnCurrentSessionChanged;
            _manager = null;
        }
    }
}
