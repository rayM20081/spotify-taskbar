# Copies the project mod source into Windhawk's ModsSource directory.
# Run this after the project file changes, then click Compile + Install in Windhawk.
# No admin required (one-time icacls grant gave the current user write access).

$src = Join-Path $PSScriptRoot "spotify-taskbar-player.wh.cpp"
$dst = "C:\ProgramData\Windhawk\ModsSource\local@spotify-taskbar-player.wh.cpp"

if (-not (Test-Path $src)) {
    Write-Error "Source file not found: $src"
    exit 1
}

Copy-Item -Path $src -Destination $dst -Force
$srcSize = (Get-Item $src).Length
$dstSize = (Get-Item $dst).Length
Write-Host "Synced: $srcSize bytes -> $dst (now $dstSize bytes)"
