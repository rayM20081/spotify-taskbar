# Spotify Taskbar Player

Компактный медиа-плеер Windows 11, встроенный прямо в таскбар — туда где раньше был виджет погоды Microsoft. Показывает обложку, название трека и кнопки управления; работает с Spotify и любым другим источником, который пишет в системный SMTC (YouTube Music, Foobar, Tidal, локальные mp3 через Groove и т.д.).

## Скриншот

![Spotify Taskbar Player в действии](Assets/screenshot.png)

## Что внутри

- **SMTC-интеграция** — данные о треке и команды play/pause/next/prev через `GlobalSystemMediaTransportControlsSessionManager`. Не нужен Spotify Web API, не нужен OAuth.
- **Embedded в Shell_TrayWnd** — окно живёт как child системного таскбара, не моргает при кликах, остаётся видимым когда открыт Start menu.
- **Цвет акцента из обложки** — прогресс-полоска автоматически окрашивается в доминантный цвет альбома.
- **Пипетка цвета фона** — подбирает любой цвет с экрана, чтобы плеер визуально слился с твоим таскбаром.
- **Tray-иконка** — стандартное контекстное меню (открыть Spotify / автостарт / настройки / выход).
- **Без зависимости от античитов** — не использует `WDA_EXCLUDEFROMCAPTURE`, глобальных хуков или чтения памяти других процессов.

## Требования

- Windows 11 (22621 / 22H2 или новее)
- [Windows App Runtime 1.8](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads)
- [.NET 10 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/10.0) (для framework-dependent сборки)

## Установка

1. Выключи стандартный виджет погоды Microsoft:
   `Settings → Personalization → Taskbar → Taskbar items → Widgets = Off`
2. Запусти `SpotifyTaskbarPlayer.exe`.
3. Через tray-меню можно включить **Start with Windows** — плеер сам будет стартовать при входе.

## Настройки

Tray-иконка → правый клик → **Settings…**:

- **Только Spotify** — фильтр SMTC; off = показываются все источники.
- **Цвет акцента из обложки** — окрашивать прогресс под альбом.
- **Отступ слева** / **Ширина плеера** — позиция и размер.
- **Прозрачность** — opacity всего плеера.
- **Цвет фона + 🎯 пипетка** — подобрать цвет под свой таскбар.

Настройки сохраняются в `%AppData%\SpotifyTaskbarPlayer\settings.json`.

## Архитектура

Стек: **.NET 10 + WinUI 3 (WindowsAppSDK 1.8) unpackaged**.

```
SpotifyTaskbarPlayer/
├── App.xaml(.cs)                  # Application entry, settings load
├── MainWindow.xaml(.cs)           # Player UI, taskbar embedding
├── SettingsWindow.xaml(.cs)       # ColorPicker + eyedropper settings
├── Services/
│   ├── SmtcService.cs             # WinRT SMTC wrapper with timeline interpolation
│   ├── TaskbarTracker.cs          # SetWinEventHook on Shell_TrayWnd
│   ├── AlbumColorExtractor.cs     # Mean RGB + HSV saturation boost
│   ├── AutostartService.cs        # HKCU\...\Run toggle
│   ├── PlayerSettings.cs          # POCO
│   └── SettingsService.cs         # JSON persistence + Changed event
├── Views/
│   └── TrayIconHost.cs            # Native Win32 tray icon + popup menu
└── Interop/
    ├── User32.cs                  # P/Invoke surface
    ├── DwmApi.cs                  # DwmSetWindowAttribute
    ├── AccentApi.cs               # SetWindowCompositionAttribute (experimental)
    └── TrayInterop.cs             # Shell_NotifyIcon + TrackPopupMenu
```

## Ограничения

- **Прозрачный/blurred фон невозможен** — WinUI 3 рисует через DXGI swap chain, который не поддерживает прозрачность в child-window режиме. Используется пипетка для подбора solid-цвета под акрил таскбара.
- **Не виден поверх Start menu / Search / Widgets** — это by-design защита Win11 ZBID (Start живёт в `ZBID_IMMERSIVE_MOGO` выше `ZBID_DESKTOP`). Когда юзер закрывает Start — плеер возвращается.
- **Только primary monitor** — на secondary дисплеях с таскбаром (`Shell_SecondaryTrayWnd`) плеер не появится.

## Сборка из исходников

```powershell
dotnet build SpotifyTaskbarPlayer\SpotifyTaskbarPlayer.csproj -c Release -p:Platform=x64
```

Release-сборка для раздачи:

```powershell
dotnet publish SpotifyTaskbarPlayer\SpotifyTaskbarPlayer.csproj `
    -c Release -r win-x64 `
    --self-contained false
```

Готовая папка с exe и dll-ками — `bin\x64\Release\net10.0-windows10.0.22621.0\win-x64\publish\`. Копируется или архивируется целиком, у получателя должны быть установлены .NET 10 Desktop Runtime + WindowsAppRuntime 1.8.

> SingleFile-публикация для unpackaged WinUI 3 на .NET 10 нестабильна — падает с `0xC000027B` потому что `MICROSOFT_WINDOWSAPPRUNTIME_BASE_DIRECTORY` env var нужно установить до entry point, а с auto-generated `Main` это сделать нельзя без переписи bootstrap. Поэтому используется multi-file.

## Лицензия

MIT.
