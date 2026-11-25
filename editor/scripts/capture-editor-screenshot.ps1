param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][string]$ProjectPath
)

$ErrorActionPreference = "Stop"

$executable = (Resolve-Path $ExecutablePath).Path
$project = (Resolve-Path $ProjectPath).Path
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class VantaWindowCapture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
}
"@

$env:VANTA_EDITOR_PROJECT = $project
$workingDirectory = Split-Path -Parent $executable
$process = Start-Process -FilePath $executable -WorkingDirectory $workingDirectory -PassThru
try {
    $deadline = (Get-Date).AddSeconds(45)
    $window = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) {
            throw "Editor exited before creating its window (exit code $($process.ExitCode))."
        }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $window = $process.MainWindowHandle
            break
        }
        Start-Sleep -Milliseconds 250
    }

    if ($window -eq [IntPtr]::Zero) {
        throw "Timed out waiting for the VantaEngine Editor window."
    }

    [VantaWindowCapture]::ShowWindow($window, 3) | Out-Null
    [VantaWindowCapture]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Seconds 3

    $rectangle = New-Object VantaWindowCapture+RECT
    if (-not [VantaWindowCapture]::GetWindowRect($window, [ref]$rectangle)) {
        throw "Could not query the editor window bounds."
    }

    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    if ($width -lt 900 -or $height -lt 600) {
        throw "Editor window is unexpectedly small: ${width}x${height}."
    }

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $rectangle.Left,
                $rectangle.Top,
                0,
                0,
                $bitmap.Size,
                [System.Drawing.CopyPixelOperation]::SourceCopy
            )
        }
        finally {
            $graphics.Dispose()
        }
        $bitmap.Save((Join-Path (Get-Location) $OutputPath), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }

    $captured = Get-Item $OutputPath
    if ($captured.Length -lt 10000) {
        throw "Screenshot is suspiciously small ($($captured.Length) bytes)."
    }
    Write-Host "Captured VantaEngine Editor screenshot: $($captured.FullName) ($($width)x$($height), $($captured.Length) bytes)"
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
