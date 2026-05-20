// ==WindhawkMod==
// @id              spotify-taskbar-player
// @name            Spotify Taskbar Player
// @description     Spotify/SMTC media player widget pinned to the Windows 11 taskbar with native acrylic.
// @version         0.1.0
// @author          rayM
// @include         explorer.exe
// @compilerOptions -lole32 -ldwmapi -lgdi32 -luser32 -lwindowsapp -lshcore -lgdiplus -lshell32 -ld2d1 -ldwrite
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
A taskbar-pinned mini Spotify-style player.

Features:
- Album art + track title + artist
- Prev / Play-Pause / Next controls
- Progress bar at the bottom
- Click album/title area to open Spotify
- Real Win11 acrylic blur (via DWM, the same recipe the taskbar uses)
- Sits in the same z-band as the Action Center, so it never sinks behind the taskbar

Requirements:
- Disable system Widgets if they conflict: Settings → Personalization → Taskbar → Widgets → Off
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- LeftPadding: 0
  $name: Left padding
  $description: Distance from the left edge of the taskbar, in pixels.
- PlayerWidth: 250
  $name: Player width
  $description: Width of the player widget, in pixels.
- TintAlpha: 0
  $name: Background tint alpha
  $description: 0 = fully transparent, 255 = fully opaque dark background.
- SpotifyOnly: true
  $name: Spotify only
  $description: Only show when Spotify is the playing source.
- HideOnFullscreen: true
  $name: Hide on fullscreen
  $description: Auto-hide when a fullscreen app or game is running.
- ArtistAlpha: 70
  $name: Artist text opacity (%)
  $description: 0 = invisible, 100 = same brightness as the title.
- ProgressBarHeight: 2
  $name: Progress bar thickness (px)
  $description: Height of the timeline at the bottom of the widget.
- ProgressBarAlpha: 0
  $name: Progress bar track alpha (0-255)
  $description: Opacity of the unplayed part. 0 = invisible (only the played portion shows), 120 ≈ subtle.
- ProgressBarBottomGap: 3
  $name: Progress bar bottom gap (px)
  $description: Vertical distance from the bar to the bottom edge of the widget.
- ProgressBarTrackLength: full
  $name: Progress bar track length
  $description: How long the unplayed track is drawn.
  $options:
  - full: Full width (track goes to the end)
  - played: Match played length (track stops where the accent does)
- IconStyle: mdl2
  $name: Icon style
  $description: Playback control icons.
  $options:
  - mdl2: MDL2 — Previous / Pause / Next with bars (Win10 style)
  - fluent: Fluent chevrons — minimal arrows
  - vector: Vector primitives — filled hand-drawn shapes
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shcore.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace Gdiplus;
using namespace std;
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

// ------ Undocumented Z-band API: lets us sit above Shell_TrayWnd ------
enum ZBID {
    ZBID_DEFAULT                 = 0,
    ZBID_DESKTOP                 = 1,
    ZBID_UIACCESS                = 2,
    ZBID_IMMERSIVE_IHM           = 3,
    ZBID_IMMERSIVE_NOTIFICATION  = 4,
    ZBID_IMMERSIVE_APPCHROME     = 5,
};
typedef HWND (WINAPI* pCreateWindowInBand)(
    DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
    DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam,
    DWORD band);

// ------ Undocumented composition attribute: enables real DWM acrylic ------
typedef enum _WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 } WINDOWCOMPOSITIONATTRIB;
typedef enum _ACCENT_STATE {
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,
} ACCENT_STATE;
typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;  // 0xAABBGGRR
    DWORD        AnimationId;
} ACCENT_POLICY;
typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attribute;
    PVOID  Data;
    SIZE_T SizeOfData;
} WINDOWCOMPOSITIONATTRIBDATA;
typedef BOOL (WINAPI* pSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

enum IconCode { ICON_PREV = 1, ICON_PLAY = 2, ICON_PAUSE = 3, ICON_NEXT = 4 };

// ------ Settings ------
struct Settings {
    int  leftPadding      = 0;
    int  playerWidth      = 250;
    int  tintAlpha        = 0;       // 0..255 — alpha of background tint
    bool spotifyOnly      = true;
    bool hideOnFullscreen = true;
    int  artistAlpha      = 70;      // 0..100 — artist text opacity %
    int  progressHeight   = 2;       // 1..8 — px
    int  progressAlpha    = 0;       // 0..255 — track alpha (0 = invisible)
    int  progressBottomGap = 3;      // px — gap from widget bottom
    int  progressTrackLength = 0;    // 0=full width, 1=match played length
    int  iconStyle        = 0;       // 0=MDL2, 1=Fluent chevrons, 2=Vector
};
static Settings g_Settings;

void LoadSettings() {
    g_Settings.leftPadding      = Wh_GetIntSetting(L"LeftPadding");
    g_Settings.playerWidth      = Wh_GetIntSetting(L"PlayerWidth");
    g_Settings.tintAlpha        = Wh_GetIntSetting(L"TintAlpha");
    g_Settings.spotifyOnly      = Wh_GetIntSetting(L"SpotifyOnly") != 0;
    g_Settings.hideOnFullscreen = Wh_GetIntSetting(L"HideOnFullscreen") != 0;
    g_Settings.artistAlpha      = Wh_GetIntSetting(L"ArtistAlpha");
    if (g_Settings.artistAlpha < 0)   g_Settings.artistAlpha = 0;
    if (g_Settings.artistAlpha > 100) g_Settings.artistAlpha = 100;
    g_Settings.progressHeight   = Wh_GetIntSetting(L"ProgressBarHeight");
    if (g_Settings.progressHeight < 1) g_Settings.progressHeight = 1;
    if (g_Settings.progressHeight > 8) g_Settings.progressHeight = 8;
    g_Settings.progressAlpha    = Wh_GetIntSetting(L"ProgressBarAlpha");
    if (g_Settings.progressAlpha < 0)   g_Settings.progressAlpha = 0;
    if (g_Settings.progressAlpha > 255) g_Settings.progressAlpha = 255;
    g_Settings.progressBottomGap = Wh_GetIntSetting(L"ProgressBarBottomGap");
    if (g_Settings.progressBottomGap < 0)  g_Settings.progressBottomGap = 0;
    if (g_Settings.progressBottomGap > 20) g_Settings.progressBottomGap = 20;
    // String enums via $options. Map to internal int values.
    PCWSTR tlen = Wh_GetStringSetting(L"ProgressBarTrackLength");
    g_Settings.progressTrackLength = (tlen && wcscmp(tlen, L"played") == 0) ? 1 : 0;
    if (tlen) Wh_FreeStringSetting(tlen);

    PCWSTR icon = Wh_GetStringSetting(L"IconStyle");
    if (icon && wcscmp(icon, L"fluent") == 0)      g_Settings.iconStyle = 1;
    else if (icon && wcscmp(icon, L"vector") == 0) g_Settings.iconStyle = 2;
    else                                            g_Settings.iconStyle = 0;
    if (icon) Wh_FreeStringSetting(icon);

    if (g_Settings.leftPadding < 0)      g_Settings.leftPadding = 0;
    if (g_Settings.playerWidth < 150)    g_Settings.playerWidth = 280;
    if (g_Settings.tintAlpha < 0)        g_Settings.tintAlpha = 0;
    if (g_Settings.tintAlpha > 255)      g_Settings.tintAlpha = 255;
}

// ------ Media state ------
struct MediaState {
    wstring  title  = L"(no track)";
    wstring  artist = L"";
    Bitmap*  albumArt = nullptr;
    bool     isPlaying = false;
    bool     hasMedia  = false;
    // Time-anchored position: SMTC's Position() at SMTC's LastUpdatedTime.
    // For paint, current position = anchor + (now - anchorTime). This avoids
    // local-ticker drift and lets us interpolate smoothly between polls.
    double   positionAtAnchorSec = 0;
    winrt::Windows::Foundation::DateTime anchorTime{};
    double   positionSec = 0;   // interpolated, computed by paint
    double   durationSec = 0;
    // Accent color derived from the cover art (saturation-boosted mean RGB).
    // Used to colorize the progress bar like the C# version's TimelineBar.
    Color    accent      = Color(255, 30, 215, 96); // default Spotify-green
    bool     accentValid = false;
    mutex    lock;
};
static MediaState g_MediaState;
static GlobalSystemMediaTransportControlsSessionManager g_SessionManager = nullptr;
static atomic<bool> g_Running{false};
static HWND g_hMediaWindow = nullptr;
static HWINEVENTHOOK g_TaskbarHook = nullptr;
static int g_HoverButton = 0; // 0=none, 1=prev, 2=playpause, 3=next, 4=album/title
// Per-button hover animation state. Each value is the current alpha of the
// circular hover background (0=no hover, 1=fully shown). On each paint we
// step toward the target (1 if hovered, 0 otherwise) for a smooth fade in/out.
static float g_HoverAnim[3] = {0.0f, 0.0f, 0.0f}; // prev, playpause, next

// ------ DirectWrite / Direct2D ------
// Used for text and icon rendering. GDI+ DrawString at small point sizes
// produces blurry edges; DWrite is what WinUI 3 uses under the hood and
// matches the native Win11 text quality.
static ID2D1Factory*     g_pD2DFactory     = nullptr;
static IDWriteFactory*   g_pDWriteFactory  = nullptr;
static IDWriteTextFormat*    g_pTitleFmt    = nullptr;
static IDWriteTextFormat*    g_pArtistFmt   = nullptr;
static IDWriteTextFormat*    g_pMdl2Small   = nullptr; // MDL2: 11 DIP prev/next
static IDWriteTextFormat*    g_pMdl2Big     = nullptr; // MDL2: 12 DIP play/pause
static IDWriteTextFormat*    g_pFluentSmall = nullptr; // Fluent: 11 DIP prev/next
static IDWriteTextFormat*    g_pFluentBig   = nullptr; // Fluent: 12 DIP play/pause
static IDWriteRenderingParams* g_pRenderParams = nullptr;

static void InitD2DDWrite() {
    if (g_pD2DFactory) return;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory), nullptr, (void**)&g_pD2DFactory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&g_pDWriteFactory);

    // XAML FontSize values are in DIPs (effective pixels), NOT points.
    // FontSize="11" => 11 DIPs em-size. Pass raw values to DWrite — no
    // 96/72 conversion (was making text 33% too large).

    // Title: 11 DIPs BOLD "Segoe UI". XAML uses SemiBold + ClearType-on-opaque,
    // which optically reads heavier than the same weight on grayscale-AA.
    // We bump one weight step (SemiBold->Bold) to compensate.
    g_pDWriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &g_pTitleFmt);
    if (g_pTitleFmt) {
        g_pTitleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        // NEAR (top) — combined with precise rect Y we get tight stacking
        // matching the C# StackPanel layout.
        g_pTitleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        g_pTitleFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* trim = nullptr;
        g_pDWriteFactory->CreateEllipsisTrimmingSign(g_pTitleFmt, &trim);
        DWRITE_TRIMMING tr = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        g_pTitleFmt->SetTrimming(&tr, trim);
        if (trim) trim->Release();
    }

    // Artist: 10 DIPs MEDIUM (500) "Segoe UI". XAML uses Regular + Opacity=0.7,
    // but pixel analysis of the C# screenshot shows the artist text renders
    // at full white (255,255,255) — Opacity doesn't reduce text alpha there.
    // Grayscale AA on a layered surface renders 1 weight thinner than
    // ClearType-on-opaque, so we bump weight one step to MEDIUM to match.
    g_pDWriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &g_pArtistFmt);
    if (g_pArtistFmt) {
        g_pArtistFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_pArtistFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        g_pArtistFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* trim = nullptr;
        g_pDWriteFactory->CreateEllipsisTrimmingSign(g_pArtistFmt, &trim);
        DWRITE_TRIMMING tr = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        g_pArtistFmt->SetTrimming(&tr, trim);
        if (trim) trim->Release();
    }

    // Two icon fonts — user picks the style at runtime.
    // MDL2 Assets: classic Win10-era media controls (Previous/Pause/Next w/ bars).
    // Fluent Icons: minimal chevron arrows.
    auto mkIcon = [&](const wchar_t* family, float size, IDWriteTextFormat** out) {
        g_pDWriteFactory->CreateTextFormat(
            family, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out);
        if (*out) {
            (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    };
    mkIcon(L"Segoe MDL2 Assets",   11.0f, &g_pMdl2Small);
    mkIcon(L"Segoe MDL2 Assets",   12.0f, &g_pMdl2Big);
    mkIcon(L"Segoe Fluent Icons",  11.0f, &g_pFluentSmall);
    mkIcon(L"Segoe Fluent Icons",  12.0f, &g_pFluentBig);

    // Custom rendering params — NATURAL_SYMMETRIC mode gives the sharpest
    // grayscale antialiasing DWrite supports for layered surfaces (default
    // params produce softer edges). Higher gamma/contrast pushes crispness.
    g_pDWriteFactory->CreateCustomRenderingParams(
        2.2f,                                        // gamma
        0.5f,                                        // enhanced contrast
        1.0f,                                        // ClearType level (unused in grayscale mode but required)
        DWRITE_PIXEL_GEOMETRY_RGB,
        DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        &g_pRenderParams);
}

static void ShutdownD2DDWrite() {
    if (g_pTitleFmt)     { g_pTitleFmt->Release();     g_pTitleFmt     = nullptr; }
    if (g_pArtistFmt)    { g_pArtistFmt->Release();    g_pArtistFmt    = nullptr; }
    if (g_pMdl2Small)    { g_pMdl2Small->Release();    g_pMdl2Small    = nullptr; }
    if (g_pMdl2Big)      { g_pMdl2Big->Release();      g_pMdl2Big      = nullptr; }
    if (g_pFluentSmall)  { g_pFluentSmall->Release();  g_pFluentSmall  = nullptr; }
    if (g_pFluentBig)    { g_pFluentBig->Release();    g_pFluentBig    = nullptr; }
    if (g_pRenderParams) { g_pRenderParams->Release(); g_pRenderParams = nullptr; }
    if (g_pDWriteFactory){ g_pDWriteFactory->Release();g_pDWriteFactory= nullptr; }
    if (g_pD2DFactory)   { g_pD2DFactory->Release();   g_pD2DFactory   = nullptr; }
}

// ------ Helpers ------
// Compute the dominant-ish accent color from a cover bitmap:
// downscale to 64x64, mean RGB excluding near-black and near-white pixels,
// then boost HSV saturation so it pops on the taskbar acrylic.
// Mirrors AlbumColorExtractor.cs from the C# project.
static Color BoostSaturation(BYTE r, BYTE g, BYTE b, double factor) {
    double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    double mx = max(rf, max(gf, bf));
    double mn = min(rf, min(gf, bf));
    double v = mx;
    double s = mx == 0 ? 0 : (mx - mn) / mx;
    double h = 0;
    if (mx != mn) {
        if      (mx == rf) h = fmod((gf - bf) / (mx - mn), 6.0);
        else if (mx == gf) h = (bf - rf) / (mx - mn) + 2.0;
        else               h = (rf - gf) / (mx - mn) + 4.0;
        h *= 60.0;
        if (h < 0) h += 360.0;
    }
    s = min(1.0, s * factor);
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double mm = v - c;
    double rp, gp, bp;
    if      (h <  60) { rp = c; gp = x; bp = 0; }
    else if (h < 120) { rp = x; gp = c; bp = 0; }
    else if (h < 180) { rp = 0; gp = c; bp = x; }
    else if (h < 240) { rp = 0; gp = x; bp = c; }
    else if (h < 300) { rp = x; gp = 0; bp = c; }
    else              { rp = c; gp = 0; bp = x; }
    auto cl = [](double v){ if (v < 0) v = 0; if (v > 255) v = 255; return (BYTE)v; };
    return Color(255, cl((rp + mm) * 255), cl((gp + mm) * 255), cl((bp + mm) * 255));
}

static bool ComputeAccent(Bitmap* src, Color& out) {
    if (!src) return false;
    const int N = 64;
    Bitmap scaled(N, N, PixelFormat32bppARGB);
    {
        Graphics g(&scaled);
        g.SetInterpolationMode(InterpolationModeBilinear);
        g.DrawImage(src, 0, 0, N, N);
    }
    BitmapData bd;
    Gdiplus::Rect rect(0, 0, N, N);
    if (scaled.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok)
        return false;
    long sr = 0, sg = 0, sb = 0; int count = 0;
    BYTE* base = (BYTE*)bd.Scan0;
    for (int y = 0; y < N; y++) {
        BYTE* row = base + y * bd.Stride;
        for (int x = 0; x < N; x++) {
            BYTE pb = row[x * 4 + 0];
            BYTE pg = row[x * 4 + 1];
            BYTE pr = row[x * 4 + 2];
            int mx = max(pr, max(pg, pb));
            int mn = min(pr, min(pg, pb));
            if (mx < 30 || mn > 230) continue;
            sr += pr; sg += pg; sb += pb; count++;
        }
    }
    scaled.UnlockBits(&bd);
    if (count == 0) return false;
    out = BoostSaturation((BYTE)(sr / count), (BYTE)(sg / count), (BYTE)(sb / count), 1.4);
    return true;
}

static Bitmap* StreamToBitmap(IRandomAccessStreamWithContentType const& stream) {
    if (!stream) return nullptr;
    IStream* nativeStream = nullptr;
    if (SUCCEEDED(CreateStreamOverRandomAccessStream(
            reinterpret_cast<IUnknown*>(winrt::get_abi(stream)),
            IID_PPV_ARGS(&nativeStream)))) {
        Bitmap* bmp = Bitmap::FromStream(nativeStream);
        nativeStream->Release();
        if (bmp && bmp->GetLastStatus() == Ok) return bmp;
        delete bmp;
    }
    return nullptr;
}

static bool IsSpotifySession(GlobalSystemMediaTransportControlsSession const& s) {
    try {
        auto id = s.SourceAppUserModelId();
        wstring sid = id.c_str();
        for (auto& c : sid) c = (wchar_t)towlower(c);
        return sid.find(L"spotify") != wstring::npos;
    } catch (...) { return false; }
}

static void UpdateMediaInfo() {
    try {
        if (!g_SessionManager) {
            g_SessionManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        }
        if (!g_SessionManager) return;

        GlobalSystemMediaTransportControlsSession picked = nullptr;
        // Prefer a Playing session, falling back to current.
        auto sessions = g_SessionManager.GetSessions();
        for (auto const& s : sessions) {
            if (g_Settings.spotifyOnly && !IsSpotifySession(s)) continue;
            auto pb = s.GetPlaybackInfo();
            if (pb && pb.PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                picked = s;
                break;
            }
        }
        if (!picked) {
            auto cur = g_SessionManager.GetCurrentSession();
            if (cur && (!g_Settings.spotifyOnly || IsSpotifySession(cur))) {
                picked = cur;
            }
        }

        if (picked) {
            auto props = picked.TryGetMediaPropertiesAsync().get();
            auto info  = picked.GetPlaybackInfo();
            auto timeline = picked.GetTimelineProperties();

            // Pull artist info: combine Artist + Subtitle if both present
            // (some apps stash featured artists in Subtitle).
            wstring artistStr = props.Artist().c_str();
            wstring subtitleStr = props.Subtitle().c_str();
            if (!subtitleStr.empty() && subtitleStr != artistStr
                && artistStr.find(subtitleStr) == wstring::npos
                && subtitleStr.find(artistStr) == wstring::npos) {
                if (!artistStr.empty()) artistStr += L" \x2022 ";
                artistStr += subtitleStr;
            }

            lock_guard<mutex> guard(g_MediaState.lock);
            wstring newTitle = props.Title().c_str();
            bool trackChanged = (newTitle != g_MediaState.title);
            if (trackChanged || g_MediaState.albumArt == nullptr) {
                if (g_MediaState.albumArt) { delete g_MediaState.albumArt; g_MediaState.albumArt = nullptr; }
                auto thumbRef = props.Thumbnail();
                if (thumbRef) {
                    auto stream = thumbRef.OpenReadAsync().get();
                    g_MediaState.albumArt = StreamToBitmap(stream);
                }
                // Cover changed — recompute accent.
                Color newAccent;
                if (g_MediaState.albumArt && ComputeAccent(g_MediaState.albumArt, newAccent)) {
                    g_MediaState.accent = newAccent;
                    g_MediaState.accentValid = true;
                } else {
                    g_MediaState.accentValid = false;
                }
            }
            g_MediaState.title    = newTitle;
            g_MediaState.artist   = artistStr;
            g_MediaState.isPlaying = (info.PlaybackStatus() ==
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            g_MediaState.hasMedia = true;

            // Time-anchored position (same trick the C# SmtcService uses):
            // store SMTC's Position() at its LastUpdatedTime; the paint path
            // adds (now - anchorTime) to interpolate smoothly between polls
            // without drift.
            g_MediaState.positionAtAnchorSec = (double)timeline.Position().count() / 10'000'000.0;
            g_MediaState.anchorTime          = timeline.LastUpdatedTime();
            g_MediaState.durationSec         = (double)timeline.EndTime().count() / 10'000'000.0;
        } else {
            lock_guard<mutex> guard(g_MediaState.lock);
            g_MediaState.hasMedia = false;
            g_MediaState.title    = L"(no track)";
            g_MediaState.artist   = L"";
            g_MediaState.isPlaying = false;
            if (g_MediaState.albumArt) { delete g_MediaState.albumArt; g_MediaState.albumArt = nullptr; }
        }
    } catch (...) {
        lock_guard<mutex> guard(g_MediaState.lock);
        g_MediaState.hasMedia = false;
    }
}

static void SendMediaCommand(int cmd) {
    try {
        if (!g_SessionManager) return;
        auto session = g_SessionManager.GetCurrentSession();
        if (!session) return;
        if (cmd == 1) session.TrySkipPreviousAsync();
        else if (cmd == 2) session.TryTogglePlayPauseAsync();
        else if (cmd == 3) session.TrySkipNextAsync();
    } catch (...) {}
}

// ------ DWM appearance ------
static void UpdateAppearance(HWND hwnd) {
    // No rounded corners — taskbar is sharp.
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    // We don't use the DWM ACCENT acrylic API: it doesn't compose under
    // layered windows on Win11 22H2+ for non-Explorer-injected processes.
    // Background tint is painted manually in Paint() via TintAlpha.
    HMODULE hUser = GetModuleHandle(L"user32.dll");
    if (!hUser) return;
    auto SetComp = (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!SetComp) return;
    ACCENT_POLICY policy = { ACCENT_DISABLED, 0, 0, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(ACCENT_POLICY) };
    SetComp(hwnd, &data);
}

// ------ Rendering ------
static void AddRoundedRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r) {
    REAL d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

// Layout regions (filled by Paint, read by hit-test).
static RECT g_RectAlbumTitle = {};
static RECT g_RectPrev       = {};
static RECT g_RectPlayPause  = {};
static RECT g_RectNext       = {};
static RECT g_RectProgress   = {};

static void Paint(HDC hdc, int width, int height) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // AntiAliasGridFit: snaps glyph hinting to the pixel grid for crisper
    // small-size text. ClearType would give sharper results but requires a
    // known opaque background — we composite per-pixel-alpha onto whatever
    // is behind, so ClearType would produce colored fringes.
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    // Our own semi-transparent background fill. Min alpha = 1 even when
    // TintAlpha=0 — UpdateLayeredWindow uses per-pixel alpha for hit
    // testing, so alpha=0 pixels are click-through. alpha=1 is invisible
    // to the eye but ensures the whole widget catches clicks.
    BYTE bgA = (BYTE)max(1, g_Settings.tintAlpha);
    SolidBrush bgBrush(Color(bgA, 32, 32, 32));
    g.FillRectangle(&bgBrush, 0, 0, width, height);

    MediaState m;
    double currentPos = 0;
    Bitmap* artClone = nullptr;
    Color accentColor;
    bool  haveAccent = false;
    {
        lock_guard<mutex> guard(g_MediaState.lock);
        m.title       = g_MediaState.title;
        m.artist      = g_MediaState.artist;
        m.isPlaying   = g_MediaState.isPlaying;
        m.hasMedia    = g_MediaState.hasMedia;
        m.durationSec = g_MediaState.durationSec;
        // Interpolate position from SMTC anchor + wall-clock delta (same
        // pattern the C# SmtcService.RefreshTimeline uses).
        currentPos = g_MediaState.positionAtAnchorSec;
        if (g_MediaState.isPlaying) {
            auto now = winrt::clock::now();
            auto elapsed = now - g_MediaState.anchorTime;
            if (elapsed.count() > 0)
                currentPos += (double)elapsed.count() / 10'000'000.0;
        }
        if (currentPos < 0) currentPos = 0;
        if (m.durationSec > 0 && currentPos > m.durationSec) currentPos = m.durationSec;
        artClone      = g_MediaState.albumArt ? g_MediaState.albumArt->Clone() : nullptr;
        accentColor   = g_MediaState.accent;
        haveAccent    = g_MediaState.accentValid;
    }
    // Make the interpolated value available to the existing draw code that
    // reads m.positionSec for the progress bar.
    m.positionSec = currentPos;

    // Progress bar layout: thin bar inset from both sides (aligned with the
    // album art) with a user-configurable gap above the bottom edge.
    const int pbH        = g_Settings.progressHeight;
    const int pbBottomGap = g_Settings.progressBottomGap;
    // Content (album, text, buttons) is centered in the FULL taskbar height
    // — independent of the progress bar position, so the progress bar can
    // grow/shrink without shoving the rest off-center.
    const int contentH = height;
    const int artSize  = 32;
    const int artX     = 6;                          // 4 root padding + 2 left margin
    const int artY     = (contentH - artSize) / 2;   // vertically centered

    // Album art with rounded corners (4 px radius).
    GraphicsPath artPath;
    AddRoundedRect(artPath, (REAL)artX, (REAL)artY, (REAL)artSize, (REAL)artSize, 4);
    if (artClone) {
        g.SetClip(&artPath);
        // UniformToFill: scale so the smaller side fills, crop the rest.
        REAL aw = (REAL)artClone->GetWidth();
        REAL ah = (REAL)artClone->GetHeight();
        REAL scale = max((REAL)artSize / aw, (REAL)artSize / ah);
        REAL dw = aw * scale, dh = ah * scale;
        REAL dx = (REAL)artX + ((REAL)artSize - dw) / 2;
        REAL dy = (REAL)artY + ((REAL)artSize - dh) / 2;
        g.DrawImage(artClone, dx, dy, dw, dh);
        g.ResetClip();
        delete artClone;
    } else {
        SolidBrush ph(Color(40, 255, 255, 255));
        g.FillPath(&ph, &artPath);
    }

    // Right-side controls: prev, play/pause, next — 28x28 each, no gap.
    const int btnSize = 28;
    const int btnY    = (contentH - btnSize) / 2;
    int rightX = width - 4 - btnSize;
    g_RectNext      = { rightX, btnY, rightX + btnSize, btnY + btnSize }; rightX -= btnSize;
    g_RectPlayPause = { rightX, btnY, rightX + btnSize, btnY + btnSize }; rightX -= btnSize;
    g_RectPrev      = { rightX, btnY, rightX + btnSize, btnY + btnSize };

    // Clickable album+title region: everything left of the buttons.
    g_RectAlbumTitle = { 0, 0, g_RectPrev.left, contentH };

    // Title + artist text area — start right after the album art.
    int textX     = artX + artSize + 6;
    int textRight = g_RectPrev.left - 4;
    int textWidth = textRight - textX;
    if (textWidth < 0) textWidth = 0;

    // Advance per-button hover animations one step toward target. Step size
    // is calibrated for the paint timer cadence (~33 ms) — full fade ~250 ms.
    const float HOVER_STEP = 0.12f;
    for (int i = 0; i < 3; i++) {
        float target = (g_HoverButton == (i + 1)) ? 1.0f : 0.0f;
        if (g_HoverAnim[i] < target)
            g_HoverAnim[i] = (g_HoverAnim[i] + HOVER_STEP > target) ? target : g_HoverAnim[i] + HOVER_STEP;
        else if (g_HoverAnim[i] > target)
            g_HoverAnim[i] = (g_HoverAnim[i] - HOVER_STEP < target) ? target : g_HoverAnim[i] - HOVER_STEP;
    }
    // Win11 native control-hover style: rounded rectangle with subtle
    // translucent white fill (not a circle). Corner radius ≈ 4 px matches
    // taskbar / settings flyout buttons. Alpha animates 0→peak for a smooth
    // fade-in matching the system motion.
    auto drawHover = [&](RECT r, float anim) {
        if (anim <= 0.001f) return;
        const float peakAlpha = 55.0f; // ~22% white tint at full hover
        BYTE a = (BYTE)(peakAlpha * anim);
        SolidBrush bg(Color(a, 255, 255, 255));
        // Inset 1px so the rounded fill doesn't touch adjacent buttons.
        REAL x = (REAL)(r.left + 1);
        REAL y = (REAL)(r.top  + 1);
        REAL w = (REAL)(r.right  - r.left - 2);
        REAL h = (REAL)(r.bottom - r.top  - 2);
        GraphicsPath path;
        AddRoundedRect(path, x, y, w, h, 4.0f);
        g.FillPath(&bg, &path);
    };
    drawHover(g_RectPrev,      g_HoverAnim[0]);
    drawHover(g_RectPlayPause, g_HoverAnim[1]);
    drawHover(g_RectNext,      g_HoverAnim[2]);

    // === Text and icons via Direct2D + DirectWrite ===
    // Tight vertical stack matching the C# StackPanel: title line right
    // above artist line, the pair centered in the content band.
    const int titleLineH  = 14;  // ~ 11 DIP SemiBold + leading
    const int artistLineH = 13;  // ~ 10 DIP Regular + leading
    const int stackH      = titleLineH + artistLineH;
    int textBlockY = (contentH - stackH) / 2;
    int titleY     = textBlockY;
    int artistY    = textBlockY + titleLineH;
    if (g_pD2DFactory && g_pDWriteFactory) {
        ID2D1DCRenderTarget* pRT = nullptr;
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (SUCCEEDED(g_pD2DFactory->CreateDCRenderTarget(&rtProps, &pRT)) && pRT) {
            RECT bindRC = { 0, 0, width, height };
            if (SUCCEEDED(pRT->BindDC(hdc, &bindRC))) {
                // ClearType is invalid on a layered surface (needs known
                // opaque background); grayscale is the best option, with
                // custom rendering params tuned for sharper glyph edges.
                pRT->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                if (g_pRenderParams) pRT->SetTextRenderingParams(g_pRenderParams);
                pRT->BeginDraw();
                ID2D1SolidColorBrush* pWhite = nullptr;
                ID2D1SolidColorBrush* pDim   = nullptr;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &pWhite);
                float artistA = (float)g_Settings.artistAlpha / 100.0f;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, artistA), &pDim);

                if (g_pTitleFmt) {
                    D2D1_RECT_F r = D2D1::RectF((float)textX, (float)titleY,
                                                (float)textRight, (float)(titleY + titleLineH));
                    pRT->DrawText(m.title.c_str(), (UINT32)m.title.length(),
                        g_pTitleFmt, r, pWhite,
                        D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                }
                if (g_pArtistFmt && !m.artist.empty()) {
                    D2D1_RECT_F r = D2D1::RectF((float)textX, (float)artistY,
                                                (float)textRight, (float)(artistY + artistLineH));
                    pRT->DrawText(m.artist.c_str(), (UINT32)m.artist.length(),
                        g_pArtistFmt, r, pDim,
                        D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                }

                // Pick icon font/codepoints by user-selected style.
                // Vector style (2) handled in GDI+ block below.
                IDWriteTextFormat* fmtSmall = nullptr;
                IDWriteTextFormat* fmtBig   = nullptr;
                if (g_Settings.iconStyle == 1) {
                    fmtSmall = g_pFluentSmall;
                    fmtBig   = g_pFluentBig;
                } else if (g_Settings.iconStyle == 0) {
                    fmtSmall = g_pMdl2Small;
                    fmtBig   = g_pMdl2Big;
                }
                if (fmtSmall && fmtBig) {
                    auto drawIcon = [&](RECT r, const wchar_t* glyph, IDWriteTextFormat* fmt) {
                        D2D1_RECT_F rr = D2D1::RectF(
                            (float)r.left, (float)r.top,
                            (float)r.right, (float)r.bottom);
                        pRT->DrawText(glyph, 1, fmt, rr, pWhite,
                            D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                    };
                    drawIcon(g_RectPrev,      L"\xE892", fmtSmall);
                    drawIcon(g_RectPlayPause, m.isPlaying ? L"\xE769" : L"\xE768", fmtBig);
                    drawIcon(g_RectNext,      L"\xE893", fmtSmall);
                }

                if (pWhite) pWhite->Release();
                if (pDim)   pDim->Release();
                pRT->EndDraw();
            }
            pRT->Release();
        }
    }

    // Vector-primitive icons (style 2) — drawn with GDI+ FillPolygon.
    if (g_Settings.iconStyle == 2) {
        SolidBrush ib(Color(255, 255, 255, 255));
        auto drawVec = [&](RECT r, int icon) {
            REAL cx = (REAL)(r.left + (r.right - r.left) / 2);
            REAL cy = (REAL)(r.top  + (r.bottom - r.top) / 2);
            const REAL sz = 10.0f;
            const REAL hs = sz / 2.0f;
            const REAL bw = 1.6f;
            if (icon == 1) { // prev
                g.FillRectangle(&ib, cx - hs, cy - hs, bw, sz);
                PointF p[3] = {
                    PointF(cx + hs, cy - hs),
                    PointF(cx + hs, cy + hs),
                    PointF(cx - hs + bw + 1, cy)
                };
                g.FillPolygon(&ib, p, 3);
            } else if (icon == 2) { // play
                PointF p[3] = {
                    PointF(cx - hs + 1, cy - hs),
                    PointF(cx - hs + 1, cy + hs),
                    PointF(cx + hs,     cy)
                };
                g.FillPolygon(&ib, p, 3);
            } else if (icon == 3) { // pause
                REAL pbw = 2.4f;
                g.FillRectangle(&ib, cx - pbw - 1, cy - hs, pbw, sz);
                g.FillRectangle(&ib, cx + 1,       cy - hs, pbw, sz);
            } else if (icon == 4) { // next
                PointF p[3] = {
                    PointF(cx - hs,            cy - hs),
                    PointF(cx - hs,            cy + hs),
                    PointF(cx + hs - bw - 1,   cy)
                };
                g.FillPolygon(&ib, p, 3);
                g.FillRectangle(&ib, cx + hs - bw, cy - hs, bw, sz);
            }
        };
        drawVec(g_RectPrev,      1);
        drawVec(g_RectPlayPause, m.isPlaying ? 3 : 2);
        drawVec(g_RectNext,      4);
    }

    // Progress bar: inset to the album-art column on the left, mirror on
    // the right, 1-px gap above the taskbar bottom.
    const int pbX     = artX;           // aligned with album art left edge
    const int pbRight = width - artX;   // symmetric inset
    const int pbW     = (pbRight > pbX) ? (pbRight - pbX) : 0;
    const int pbY     = height - pbH - pbBottomGap;
    g_RectProgress = { pbX, pbY, pbRight, pbY + pbH };

    int fillW = 0;
    if (m.durationSec > 0) {
        double frac = m.positionSec / m.durationSec;
        if (frac < 0) frac = 0; else if (frac > 1) frac = 1;
        fillW = (int)(pbW * frac);
    }

    // Track: alpha-blended over background. Length depends on user choice:
    //   0 = full bar width (classic progress-bar look),
    //   1 = same length as the played portion (track just behind the accent).
    if (g_Settings.progressAlpha > 0) {
        int trackLen = (g_Settings.progressTrackLength == 1) ? fillW : pbW;
        if (trackLen > 0) {
            SolidBrush trackBrush(Color((BYTE)g_Settings.progressAlpha, 255, 255, 255));
            g.FillRectangle(&trackBrush, pbX, pbY, trackLen, pbH);
        }
    }
    // Accent (album-art tint, fallback Spotify-green).
    if (fillW > 0) {
        SolidBrush fillBrush(haveAccent ? accentColor : Color(255, 30, 215, 96));
        g.FillRectangle(&fillBrush, pbX, pbY, fillW, pbH);
    }
}

// ------ Hit-testing ------
static int HitTest(int x, int y) {
    auto in = [&](RECT r){ return x >= r.left && x < r.right && y >= r.top && y < r.bottom; };
    if (in(g_RectPrev))       return 1;
    if (in(g_RectPlayPause))  return 2;
    if (in(g_RectNext))       return 3;
    if (in(g_RectAlbumTitle)) return 4;
    return 0;
}

static void OpenSpotify() {
    ShellExecuteW(NULL, L"open", L"spotify:", NULL, NULL, SW_SHOWNORMAL);
}

// ------ Position sync with the taskbar ------
static bool IsTaskbarWindow(HWND hwnd) {
    WCHAR cls[64];
    if (!hwnd) return false;
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    return wcscmp(cls, L"Shell_TrayWnd") == 0;
}

static void CALLBACK TaskbarEventProc(HWINEVENTHOOK, DWORD, HWND hwnd,
                                      LONG, LONG, DWORD, DWORD) {
    if (!IsTaskbarWindow(hwnd) || !g_hMediaWindow) return;
    PostMessage(g_hMediaWindow, WM_APP + 10, 0, 0);
}

static void RegisterTaskbarHook(HWND hwnd) {
    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", nullptr);
    if (hTaskbar) {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hTaskbar, &pid);
        if (tid != 0) {
            g_TaskbarHook = SetWinEventHook(
                EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                nullptr, TaskbarEventProc, pid, tid,
                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
    }
    PostMessage(hwnd, WM_APP + 10, 0, 0);
}

// ------ Repaint via UpdateLayeredWindow ------
// We use UpdateLayeredWindow (with premultiplied per-pixel alpha) instead of
// SetLayeredWindowAttributes(LWA_ALPHA) + BitBlt. Reason: BitBlt loses the
// alpha channel when copying into the layered surface, so transparent pixels
// became opaque black and the DWM acrylic underneath was never visible.
static void Repaint(HWND hwnd) {
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h; // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(memDC);
        ReleaseDC(NULL, screenDC);
        return;
    }
    memset(bits, 0, (size_t)w * h * 4);
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    Paint(memDC, w, h);

    // Premultiply alpha — UpdateLayeredWindow with AC_SRC_ALPHA requires
    // pixels to be in premultiplied form (R*A/255, G*A/255, B*A/255, A).
    BYTE* p = (BYTE*)bits;
    for (int i = 0; i < w * h; i++) {
        BYTE a = p[3];
        if (a < 255) {
            p[0] = (BYTE)((p[0] * a + 127) / 255);
            p[1] = (BYTE)((p[1] * a + 127) / 255);
            p[2] = (BYTE)((p[2] * a + 127) / 255);
        }
        p += 4;
    }

    POINT  ptSrc = {0, 0};
    SIZE   size  = {w, h};
    POINT  ptDst = {rc.left, rc.top};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

// ------ Window proc ------
#define IDT_POLL_MEDIA 1001
#define IDT_PROGRESS   1002
#define APP_WM_CLOSE   (WM_APP + 100)

static LRESULT CALLBACK MediaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            UpdateAppearance(hwnd);
            // POLL_MEDIA (1 s): refresh title/artist/art and re-anchor the
            // SMTC position. PROGRESS (100 ms): just repaint with the
            // interpolated position (no SMTC call) — smooth bar at ~10 fps
            // without slamming SMTC.
            SetTimer(hwnd, IDT_POLL_MEDIA, 1000, NULL);
            // 30 ms (~33 fps) — smooth enough for hover fades and the
            // interpolated progress bar.
            SetTimer(hwnd, IDT_PROGRESS,     30, NULL);
            RegisterTaskbarHook(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        // WM_PAINT is unused — we drive painting via UpdateLayeredWindow in
        // the Repaint() helper below. Just acknowledge the paint message.
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            Repaint(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int h = HitTest(x, y);
            int newHover = (h == 1 || h == 2 || h == 3) ? h : 0;
            if (newHover != g_HoverButton) {
                g_HoverButton = newHover;
                Repaint(hwnd);
            }
            // Track leave so hover state clears.
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            if (g_HoverButton != 0) {
                g_HoverButton = 0;
                Repaint(hwnd);
            }
            return 0;

        case WM_LBUTTONUP: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int h = HitTest(x, y);
            if      (h == 1) SendMediaCommand(1);
            else if (h == 2) SendMediaCommand(2);
            else if (h == 3) SendMediaCommand(3);
            else if (h == 4) OpenSpotify();
            return 0;
        }

        case WM_TIMER:
            if (wParam == IDT_POLL_MEDIA) {
                UpdateMediaInfo();

                // Auto-hide on fullscreen apps / presentation mode.
                bool shouldHide = false;
                if (g_Settings.hideOnFullscreen) {
                    QUERY_USER_NOTIFICATION_STATE st;
                    if (SUCCEEDED(SHQueryUserNotificationState(&st))) {
                        if (st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN
                            || st == QUNS_PRESENTATION_MODE) shouldHide = true;
                    }
                }
                if (shouldHide && IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else if (!shouldHide && !IsWindowVisible(hwnd)) {
                    HWND tb = FindWindow(L"Shell_TrayWnd", nullptr);
                    if (tb && IsWindowVisible(tb)) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                }
                Repaint(hwnd);
            } else if (wParam == IDT_PROGRESS) {
                // Cheap progress-only repaint: interpolated position from
                // the SMTC anchor + wall clock, no fresh SMTC call.
                Repaint(hwnd);
            }
            return 0;

        case WM_APP + 10: {
            // Taskbar moved/resized — re-anchor the widget.
            HWND tb = FindWindow(L"Shell_TrayWnd", nullptr);
            if (!tb) break;

            if (!IsWindowVisible(tb)) {
                if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (!IsWindowVisible(hwnd)) {
                // Only restore if not currently suppressed by fullscreen.
                bool gameMode = false;
                if (g_Settings.hideOnFullscreen) {
                    QUERY_USER_NOTIFICATION_STATE st;
                    if (SUCCEEDED(SHQueryUserNotificationState(&st)))
                        if (st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN
                            || st == QUNS_PRESENTATION_MODE) gameMode = true;
                }
                if (!gameMode) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }

            RECT rc; GetWindowRect(tb, &rc);
            int tbHeight = rc.bottom - rc.top;
            int x = rc.left + g_Settings.leftPadding;
            int y = rc.top;
            int w = g_Settings.playerWidth;
            int h = tbHeight;

            RECT cur; GetWindowRect(hwnd, &cur);
            if (cur.left != x || cur.top != y
                || (cur.right - cur.left) != w || (cur.bottom - cur.top) != h) {
                SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                Repaint(hwnd);
            }
            return 0;
        }

        case WM_SETTINGCHANGE:
            UpdateAppearance(hwnd);
            Repaint(hwnd);
            return 0;

        case APP_WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_TaskbarHook) { UnhookWinEvent(g_TaskbarHook); g_TaskbarHook = nullptr; }
            {
                lock_guard<mutex> guard(g_MediaState.lock);
                if (g_MediaState.albumArt) { delete g_MediaState.albumArt; g_MediaState.albumArt = nullptr; }
            }
            g_SessionManager = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ------ Mod thread ------
static void MediaThread() {
    winrt::init_apartment();

    GdiplusStartupInput gpsi;
    ULONG_PTR gpToken;
    GdiplusStartup(&gpToken, &gpsi, NULL);
    InitD2DDWrite();

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = MediaWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = L"SpotifyTaskbarPlayer_Wh";
    wc.hCursor       = LoadCursor(NULL, IDC_HAND);
    RegisterClass(&wc);

    HMODULE hUser32 = GetModuleHandle(L"user32.dll");
    pCreateWindowInBand CreateWindowInBand = nullptr;
    if (hUser32)
        CreateWindowInBand = (pCreateWindowInBand)GetProcAddress(hUser32, "CreateWindowInBand");

    if (CreateWindowInBand) {
        g_hMediaWindow = CreateWindowInBand(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"SpotifyTaskbarPlayer",
            WS_POPUP | WS_VISIBLE,
            0, 0, g_Settings.playerWidth, 48,
            NULL, NULL, wc.hInstance, NULL,
            ZBID_IMMERSIVE_NOTIFICATION);
        if (g_hMediaWindow) Wh_Log(L"Window created in ZBID_IMMERSIVE_NOTIFICATION");
    }
    if (!g_hMediaWindow) {
        Wh_Log(L"CreateWindowInBand unavailable, falling back to CreateWindowEx");
        g_hMediaWindow = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"SpotifyTaskbarPlayer",
            WS_POPUP | WS_VISIBLE,
            0, 0, g_Settings.playerWidth, 48,
            NULL, NULL, wc.hInstance, NULL);
    }

    // NOTE: do NOT call SetLayeredWindowAttributes here — it's mutually
    // exclusive with UpdateLayeredWindow (which Repaint() uses to get real
    // per-pixel alpha so DWM acrylic shows through transparent pixels).

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterClass(wc.lpszClassName, wc.hInstance);
    ShutdownD2DDWrite();
    GdiplusShutdown(gpToken);
    winrt::uninit_apartment();
}

static std::thread* g_pMediaThread = nullptr;

// ------ Windhawk Tool Mod callbacks ------
BOOL WhTool_ModInit() {
    LoadSettings();
    g_Running = true;
    g_pMediaThread = new std::thread(MediaThread);
    return TRUE;
}

void WhTool_ModUninit() {
    g_Running = false;
    if (g_hMediaWindow) SendMessage(g_hMediaWindow, APP_WM_CLOSE, 0, 0);
    if (g_pMediaThread) {
        if (g_pMediaThread->joinable()) g_pMediaThread->join();
        delete g_pMediaThread;
        g_pMediaThread = nullptr;
    }
}

void WhTool_ModSettingsChanged() {
    LoadSettings();
    if (g_hMediaWindow) {
        SendMessage(g_hMediaWindow, WM_APP + 10, 0, 0);
        SendMessage(g_hMediaWindow, WM_SETTINGCHANGE, 0, 0);
    }
}

// ============================================================================
// Boilerplate: Windhawk tool-mod launcher.
// See https://github.com/ramensoftware/windhawk-mods/pull/1916 — tool mods
// don't hook into other processes, they run in a dedicated windhawk.exe.
// ============================================================================
bool   g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    bool isService = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) return FALSE;
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"-service") == 0) { isService = true; break; }
    for (int i = 1; i < argc - 1; i++)
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) isCurrentToolModProcess = true;
            break;
        }
    LocalFree(argv);

    if (isService) return FALSE;

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex = CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) ExitProcess(1);
        if (GetLastError() == ERROR_ALREADY_EXISTS) ExitProcess(1);
        if (!WhTool_ModInit()) ExitProcess(1);

        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);
        DWORD epRva = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* ep = (BYTE*)dosHeader + epRva;
        Wh_SetFunctionHook(ep, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }
    if (isToolModProcess) return FALSE;

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) return;

    WCHAR procPath[MAX_PATH];
    if (GetModuleFileName(nullptr, procPath, ARRAYSIZE(procPath)) == 0
        || GetModuleFileName(nullptr, procPath, ARRAYSIZE(procPath)) == ARRAYSIZE(procPath)) return;

    WCHAR cmdLine[MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(cmdLine, L"\"%s\" -tool-mod \"%s\"", procPath, WH_MOD_ID);

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcess(procPath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) return;
    WhTool_ModUninit();
    if (g_toolModProcessMutex) { CloseHandle(g_toolModProcessMutex); g_toolModProcessMutex = nullptr; }
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) return;
    WhTool_ModSettingsChanged();
}