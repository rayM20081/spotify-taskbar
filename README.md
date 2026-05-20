# Spotify Taskbar Player

A native-feel media widget pinned to the Windows 11 taskbar. Album art, track title, controls, and a colored progress line that takes its tint from the cover — all rendered through DirectWrite + Direct2D for the same crispness as built-in Windows UI.

Works with Spotify and anything else that talks to the system media transport (SMTC): YouTube Music, Foobar, Tidal, Edge / Chrome video, Groove and local files.

![Screenshot](Assets/screenshot.png)

## How it works

The widget is a [Windhawk](https://windhawk.net) mod. Windhawk hosts the mod in its own process, which gives it the privileges needed to:

- Create the window in **`ZBID_IMMERSIVE_NOTIFICATION`** via `CreateWindowInBand`, the same z-band Action Center / volume flyouts use. No z-order race with `Shell_TrayWnd`, no flicker when Start opens.
- Render text and icons through **Direct2D + DirectWrite** at native quality.
- Read media state through **WinRT GSMTC** — `GlobalSystemMediaTransportControlsSessionManager`.
- Track the taskbar with **`SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE)`** so the widget moves with the taskbar (auto-hide, multi-monitor, resolution change).

## Install

1. Install [Windhawk](https://windhawk.net/) (one-time, ~10 MB, no telemetry, OSS — `ramensoftware/windhawk`).
2. In Windhawk, click **Create new mod**, paste the contents of [`windhawk-mod/spotify-taskbar-player.wh.cpp`](windhawk-mod/spotify-taskbar-player.wh.cpp).
3. Click **Compile** then **Save & Apply**. The widget appears on the taskbar.
4. Recommended: disable the built-in Widgets icon to free taskbar real estate:
   `Settings → Personalization → Taskbar → Widgets = Off`.

## Settings

Open the mod's **Settings** tab in Windhawk:

| Setting | Default | Notes |
| --- | --- | --- |
| Left padding | 0 | Distance from the taskbar's left edge (px) |
| Player width | 250 | Widget width (px) |
| Background tint alpha | 0 | 0 = fully transparent, 255 = opaque dark fill |
| Artist text opacity (%) | 70 | Brightness of the artist line |
| Progress bar thickness | 2 | Height in px (1–8) |
| Progress bar track alpha | 0 | Opacity of the unplayed track (0 = only colored part shows) |
| Progress bar track length | full | `full` (classic) or `played` (track stops where accent ends) |
| Progress bar bottom gap | 3 | Vertical distance from the widget bottom (px) |
| Spotify only | true | Filter SMTC to Spotify only |
| Hide on fullscreen | true | Auto-hide for games / fullscreen video |
| Icon style | mdl2 | `mdl2` / `fluent` / `vector` — pick your control-button look |

Settings live in `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@spotify-taskbar-player\Settings`. Editing the source schema sets new defaults; existing values persist across recompiles.

## Development workflow

The source is at [`windhawk-mod/spotify-taskbar-player.wh.cpp`](windhawk-mod/spotify-taskbar-player.wh.cpp). For local iteration, Windhawk reads from `C:\ProgramData\Windhawk\ModsSource\local@spotify-taskbar-player.wh.cpp`. The repo ships a one-line sync:

```powershell
& "windhawk-mod\sync.ps1"
```

After syncing, hit **Compile + Apply** in Windhawk. (First-time setup: take ownership of the destination file once via `icacls` so user-mode writes work — see `sync.ps1` header.)

The `.wh.cpp` is a single ~1.6 kLOC file using Win32 + WinRT + GDI+ + Direct2D + DirectWrite. No external dependencies beyond what Windhawk's bundled MinGW compiler provides.

## Features

- Album cover (32×32 rounded), track title (SemiBold), artist line
- Prev / Play-Pause / Next controls with fade-in/out hover state
- Click cover or title to open Spotify (`spotify:` URI)
- Progress bar with two layout modes (full track / matched length) and album-derived accent color
- Time-anchored position interpolation (`Position` + `LastUpdatedTime` — no per-second snap-back)
- Auto-hide on fullscreen apps via `SHQueryUserNotificationState`
- Live z-order through `ZBID_IMMERSIVE_NOTIFICATION` — no Shell_TrayWnd race
- Spotify-only source filter (toggle to allow any SMTC app)

## Limitations

- **Requires Windhawk.** The same widget shipped as a standalone `.exe` would fight Shell_TrayWnd for z-order on every focus change. Windhawk's process privileges are what make the immersive z-band available.
- **Single-monitor.** The widget anchors to the primary taskbar (`Shell_TrayWnd`).
- **Start menu covers it.** Like every taskbar widget on Win11, our band is below Start's. Once Start closes the widget reappears immediately.

## License

MIT.
