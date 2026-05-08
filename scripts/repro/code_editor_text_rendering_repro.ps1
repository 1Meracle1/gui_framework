param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [int]$settle_ms = 6000
)

$ErrorActionPreference = 'Stop'

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($workspace_path -eq '') {
    $workspace_path = $repo_root
}
if ($exe_path -eq '') {
    $exe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\code_editor.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_text_rendering'
}
$resolved_file_path = if ([System.IO.Path]::IsPathRooted($file_path)) {
    $file_path
} else {
    Join-Path $workspace_path $file_path
}
$resolved_file_path = (Resolve-Path $resolved_file_path).Path

if (-not (Test-Path $workspace_path)) {
    throw "Workspace path not found: $workspace_path"
}
if (-not (Test-Path $exe_path)) {
    throw "Executable not found: $exe_path"
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;
    public const uint WM_CHAR = 0x0102;
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
}
"@

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$launch_png = Join-Path $artifact_dir '01_sharp.png'
$smooth_png = Join-Path $artifact_dir '02_smooth.png'
$stdout_path = Join-Path $artifact_dir 'stdout.txt'
$stderr_path = Join-Path $artifact_dir 'stderr.txt'
Remove-Item -Force $launch_png, $smooth_png, $stdout_path, $stderr_path -ErrorAction SilentlyContinue

function Get-MainWindowHandle([int]$process_id) {
    $script:found = [IntPtr]::Zero
    $callback = [Win32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $window_process_id = 0
        [Win32]::GetWindowThreadProcessId($hWnd, [ref]$window_process_id) | Out-Null
        if ($window_process_id -eq [uint32]$lParam.ToInt32() -and [Win32]::IsWindowVisible($hWnd)) {
            $script:found = $hWnd
            return $false
        }
        return $true
    }
    [Win32]::EnumWindows($callback, [IntPtr]$process_id) | Out-Null
    return $script:found
}

function Focus-Window([IntPtr]$handle) {
    [Win32]::ShowWindow($handle, 9) | Out-Null
    [Win32]::SetWindowPos($handle, [IntPtr]::Zero, 80, 80, 1500, 980, 0) | Out-Null
    [Win32]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 250
}

function Send-Key([IntPtr]$handle, [int]$vk) {
    [Win32]::PostMessage($handle, [Win32]::WM_KEYDOWN, [IntPtr]$vk, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 20
    [Win32]::PostMessage($handle, [Win32]::WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
}

function Send-Char([IntPtr]$handle, [char]$char) {
    [Win32]::PostMessage($handle, [Win32]::WM_CHAR, [IntPtr][int][char]$char, [IntPtr]1) |
        Out-Null
}

function Send-Text([IntPtr]$handle, [string]$text, [int]$delay_ms = 20) {
    foreach ($char in $text.ToCharArray()) {
        Send-Char $handle $char
        Start-Sleep -Milliseconds $delay_ms
    }
}

function Save-Screenshot([IntPtr]$handle, [string]$path) {
    $rect = New-Object Win32+RECT
    [Win32]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

function Run-Command([IntPtr]$handle, [string]$command) {
    Send-Char $handle ':'
    Start-Sleep -Milliseconds 200
    Send-Text $handle $command
    Start-Sleep -Milliseconds 200
    Send-Key $handle 0x0D
}

$summary = [ordered]@{
    status = 'unknown'
    phase = 'start'
    hwnd = $null
    exit_code = $null
    file_path = $resolved_file_path
    launch_png = $launch_png
    smooth_png = $smooth_png
    stdout_path = $stdout_path
    stderr_path = $stderr_path
    stdout_preview = ''
    stderr_preview = ''
    error = ''
}

$process = $null
$stdout_builder = [System.Text.StringBuilder]::new()
$stderr_builder = [System.Text.StringBuilder]::new()
$stdout_event = $null
$stderr_event = $null
try {
    $start_info = [System.Diagnostics.ProcessStartInfo]::new()
    $start_info.FileName = $exe_path
    $start_info.Arguments = ('"{0}"' -f $resolved_file_path)
    $start_info.WorkingDirectory = $workspace_path
    $start_info.UseShellExecute = $false
    $start_info.CreateNoWindow = $true
    $start_info.RedirectStandardOutput = $true
    $start_info.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start_info
    $process.EnableRaisingEvents = $true
    $stdout_event = Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$script:stdout_builder.AppendLine($EventArgs.Data)
        }
    }
    $stderr_event = Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$script:stderr_builder.AppendLine($EventArgs.Data)
        }
    }
    if (-not $process.Start()) {
        throw 'Failed to start code_editor.exe.'
    }
    $process.BeginOutputReadLine()
    $process.BeginErrorReadLine()

    $hwnd = [IntPtr]::Zero
    for ($index = 0; $index -lt 100; $index += 1) {
        if ($process.HasExited) {
            break
        }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $hwnd = $process.MainWindowHandle
            break
        }
        $hwnd = Get-MainWindowHandle $process.Id
        if ($hwnd -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        throw 'Failed to get main window handle.'
    }
    $summary.hwnd = ('0x{0:X}' -f $hwnd.ToInt64())

    Focus-Window $hwnd
    Start-Sleep -Milliseconds $settle_ms
    $summary.phase = 'sharp'
    Save-Screenshot $hwnd $launch_png

    Run-Command $hwnd 'set editor.raster-policy=smooth'
    Start-Sleep -Milliseconds 1200
    $summary.phase = 'smooth'
    Save-Screenshot $hwnd $smooth_png
    $summary.status = if ($process.HasExited) { 'exited' } else { 'survived' }
}
catch {
    $summary.status = 'script_error'
    $summary.error = ($_ | Out-String)
}
finally {
    if ($process -ne $null) {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $summary.exit_code = $process.ExitCode
    }
    if ($stdout_event -ne $null) {
        Unregister-Event -SourceIdentifier $stdout_event.Name
        Remove-Job -Id $stdout_event.Id -Force
    }
    if ($stderr_event -ne $null) {
        Unregister-Event -SourceIdentifier $stderr_event.Name
        Remove-Job -Id $stderr_event.Id -Force
    }
    [System.IO.File]::WriteAllText($stdout_path, $stdout_builder.ToString())
    [System.IO.File]::WriteAllText($stderr_path, $stderr_builder.ToString())
    Start-Sleep -Milliseconds 300
    if (Test-Path $stdout_path) {
        $stdout = [System.IO.File]::ReadAllText($stdout_path)
        $summary.stdout_preview =
            if ($stdout.Length -gt 2000) { $stdout.Substring(0, 2000) } else { $stdout }
    }
    if (Test-Path $stderr_path) {
        $stderr = [System.IO.File]::ReadAllText($stderr_path)
        $summary.stderr_preview =
            if ($stderr.Length -gt 2000) { $stderr.Substring(0, 2000) } else { $stderr }
    }
    $summary | ConvertTo-Json -Depth 4
}
