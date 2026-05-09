param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [int]$branch_scroll_notches = -4,
    [switch]$branch_scroll_checks,
    [switch]$commit_button_checks,
    [switch]$commit_popup_checks
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
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_git_panel_repro'
}
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
public static class GitPanelWin32 {
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;
    public const uint WM_CHAR = 0x0102;
    public const uint WM_MOUSEMOVE = 0x0200;
    public const uint WM_LBUTTONDOWN = 0x0201;
    public const uint WM_LBUTTONUP = 0x0202;
    public const uint WM_MOUSEWHEEL = 0x020A;
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
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }
}
"@

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$launch_png = Join-Path $artifact_dir '01_launch.png'
$git_png = Join-Path $artifact_dir '02_git_panel.png'
$git_crop_png = Join-Path $artifact_dir '03_git_panel_left_crop.png'
$branches_png = Join-Path $artifact_dir '04_git_panel_branches.png'
$branches_crop_png = Join-Path $artifact_dir '05_git_panel_branches_left_crop.png'
$stage_png = Join-Path $artifact_dir '06_git_panel_stage.png'
$stage_crop_png = Join-Path $artifact_dir '07_git_panel_stage_left_crop.png'
$unstage_png = Join-Path $artifact_dir '08_git_panel_unstage.png'
$unstage_crop_png = Join-Path $artifact_dir '09_git_panel_unstage_left_crop.png'
$spaces_png = Join-Path $artifact_dir '10_git_panel_spaces.png'
$spaces_crop_png = Join-Path $artifact_dir '11_git_panel_spaces_left_crop.png'
$message_png = Join-Path $artifact_dir '12_git_panel_message.png'
$message_crop_png = Join-Path $artifact_dir '13_git_panel_message_left_crop.png'
$graph_png = Join-Path $artifact_dir '14_git_panel_graph.png'
$graph_crop_png = Join-Path $artifact_dir '15_git_panel_graph_left_crop.png'
$branches_scroll_png = Join-Path $artifact_dir '16_git_panel_branches_scrolled.png'
$branches_scroll_crop_png = Join-Path $artifact_dir '17_git_panel_branches_scrolled_left_crop.png'
$commit_popup_png = Join-Path $artifact_dir '18_git_panel_commit_popup.png'
$commit_popup_drag_png = Join-Path $artifact_dir '19_git_panel_commit_popup_drag.png'
$commit_popup_release_png = Join-Path $artifact_dir '20_git_panel_commit_popup_release.png'
$second_commit_popup_png = Join-Path $artifact_dir '21_git_panel_second_commit_popup.png'
$stdout_path = Join-Path $artifact_dir 'stdout.txt'
$stderr_path = Join-Path $artifact_dir 'stderr.txt'
Remove-Item -Force `
    $launch_png, $git_png, $git_crop_png, `
    $branches_png, $branches_crop_png, `
    $stage_png, $stage_crop_png, $unstage_png, $unstage_crop_png, `
    $spaces_png, $spaces_crop_png, $message_png, $message_crop_png, `
    $graph_png, $graph_crop_png, $branches_scroll_png, $branches_scroll_crop_png, `
    $commit_popup_png, $commit_popup_drag_png, $commit_popup_release_png, `
    $second_commit_popup_png, `
    $stdout_path, $stderr_path -ErrorAction SilentlyContinue

function Get-MainWindowHandle([int]$process_id) {
    $script:found = [IntPtr]::Zero
    $callback = [GitPanelWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $window_process_id = 0
        [GitPanelWin32]::GetWindowThreadProcessId($hWnd, [ref]$window_process_id) | Out-Null
        if ($window_process_id -eq [uint32]$lParam.ToInt32() -and
            [GitPanelWin32]::IsWindowVisible($hWnd)) {
            $script:found = $hWnd
            return $false
        }
        return $true
    }
    [GitPanelWin32]::EnumWindows($callback, [IntPtr]$process_id) | Out-Null
    return $script:found
}

function Focus-Window([IntPtr]$handle) {
    [GitPanelWin32]::ShowWindow($handle, 9) | Out-Null
    [GitPanelWin32]::SetWindowPos($handle, [IntPtr]::Zero, 80, 80, 1320, 900, 0) | Out-Null
    [GitPanelWin32]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 250
}

function Send-Key([IntPtr]$handle, [int]$vk) {
    [GitPanelWin32]::PostMessage($handle, [GitPanelWin32]::WM_KEYDOWN, [IntPtr]$vk, [IntPtr]1) |
        Out-Null
    Start-Sleep -Milliseconds 20
    [GitPanelWin32]::PostMessage($handle, [GitPanelWin32]::WM_KEYUP, [IntPtr]$vk, [IntPtr]0) |
        Out-Null
}

function Send-Char([IntPtr]$handle, [char]$char) {
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_CHAR,
        [IntPtr][int][char]$char,
        [IntPtr]1
    ) | Out-Null
}

function Send-Text([IntPtr]$handle, [string]$text) {
    foreach ($char in $text.ToCharArray()) {
        Send-Char $handle $char
        Start-Sleep -Milliseconds 15
    }
}

function Send-Click([IntPtr]$handle, [int]$x, [int]$y) {
    $lparam = ($y -shl 16) -bor ($x -band 0xffff)
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_MOUSEMOVE,
        [IntPtr]0,
        [IntPtr]$lparam
    ) | Out-Null
    Start-Sleep -Milliseconds 80
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_LBUTTONDOWN,
        [IntPtr]1,
        [IntPtr]$lparam
    ) | Out-Null
    Start-Sleep -Milliseconds 250
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_LBUTTONUP,
        [IntPtr]0,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-MouseMove([IntPtr]$handle, [int]$x, [int]$y, [bool]$down) {
    $lparam = ($y -shl 16) -bor ($x -band 0xffff)
    $wparam = if ($down) { [IntPtr]1 } else { [IntPtr]0 }
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_MOUSEMOVE,
        $wparam,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-MouseDown([IntPtr]$handle, [int]$x, [int]$y) {
    $lparam = ($y -shl 16) -bor ($x -band 0xffff)
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_LBUTTONDOWN,
        [IntPtr]1,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-MouseUp([IntPtr]$handle, [int]$x, [int]$y) {
    $lparam = ($y -shl 16) -bor ($x -band 0xffff)
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_LBUTTONUP,
        [IntPtr]0,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-Drag([IntPtr]$handle, [int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    Send-MouseMove $handle $x1 $y1 $false
    Start-Sleep -Milliseconds 120
    Send-MouseDown $handle $x1 $y1
    Start-Sleep -Milliseconds 120
    Send-MouseMove $handle $x2 $y2 $true
    Start-Sleep -Milliseconds 320
    Send-MouseUp $handle $x2 $y2
}

function Send-Wheel([IntPtr]$handle, [int]$x, [int]$y, [int]$notches) {
    $point = [GitPanelWin32+POINT]::new()
    $point.X = $x
    $point.Y = $y
    [GitPanelWin32]::ClientToScreen($handle, [ref]$point) | Out-Null
    $delta = $notches * 120
    $wparam = [IntPtr](($delta -shl 16) -band 0xffff0000)
    $lparam = [IntPtr](($point.Y -shl 16) -bor ($point.X -band 0xffff))
    [GitPanelWin32]::PostMessage(
        $handle,
        [GitPanelWin32]::WM_MOUSEWHEEL,
        $wparam,
        $lparam
    ) | Out-Null
}

function Save-Screenshot([IntPtr]$handle, [string]$path) {
    [GitPanelWin32]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 80
    $rect = New-Object GitPanelWin32+RECT
    [GitPanelWin32]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

function Save-LeftCrop([string]$source, [string]$target) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($source)
    $crop_width = [Math]::Min(430, $bitmap.Width)
    $crop_height = [Math]::Min(880, $bitmap.Height)
    $copy = $bitmap.Clone(
        [System.Drawing.Rectangle]::new(0, 0, $crop_width, $crop_height),
        $bitmap.PixelFormat
    )
    $copy.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $copy.Dispose()
    $bitmap.Dispose()
}

$summary = [ordered]@{
    status = 'unknown'
    phase = 'start'
    hwnd = $null
    exit_code = $null
    launch_png = $launch_png
    git_png = $git_png
    git_crop_png = $git_crop_png
    branches_png = $branches_png
    branches_crop_png = $branches_crop_png
    stage_png = $stage_png
    stage_crop_png = $stage_crop_png
    unstage_png = $unstage_png
    unstage_crop_png = $unstage_crop_png
    spaces_png = $spaces_png
    spaces_crop_png = $spaces_crop_png
    message_png = $message_png
    message_crop_png = $message_crop_png
    graph_png = $graph_png
    graph_crop_png = $graph_crop_png
    branches_scroll_png = $branches_scroll_png
    branches_scroll_crop_png = $branches_scroll_crop_png
    commit_popup_png = $commit_popup_png
    commit_popup_drag_png = $commit_popup_drag_png
    commit_popup_release_png = $commit_popup_release_png
    second_commit_popup_png = $second_commit_popup_png
    stdout_path = $stdout_path
    stderr_path = $stderr_path
    stdout_preview = ''
    stderr_preview = ''
    error = ''
}

$process = $null
$started = $false
try {
    $start_info = [System.Diagnostics.ProcessStartInfo]::new()
    $start_info.FileName = $exe_path
    $start_info.Arguments = ('"{0}"' -f $workspace_path)
    $start_info.WorkingDirectory = $workspace_path
    $start_info.UseShellExecute = $false
    $start_info.CreateNoWindow = $true
    $start_info.RedirectStandardOutput = $true
    $start_info.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start_info
    $started = $process.Start()
    if (-not $started) {
        throw 'Failed to start code_editor.exe.'
    }

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
    Start-Sleep -Milliseconds 1200
    $summary.phase = 'launch'
    Save-Screenshot $hwnd $launch_png

    Send-Key $hwnd 0x20
    Send-Char $hwnd 'g'
    Start-Sleep -Milliseconds 1800
    $summary.phase = 'git_panel'
    Save-Screenshot $hwnd $git_png
    Save-LeftCrop $git_png $git_crop_png

    if ($commit_popup_checks) {
        Send-Click $hwnd 35 274
        Start-Sleep -Milliseconds 2600
        $summary.phase = 'graph_expanded'
        Save-Screenshot $hwnd $graph_png
        Save-LeftCrop $graph_png $graph_crop_png

        Send-MouseMove $hwnd 128 299 $false
        Start-Sleep -Milliseconds 900
        $summary.phase = 'commit_popup'
        Save-Screenshot $hwnd $commit_popup_png

        Send-MouseMove $hwnd 526 83 $false
        Start-Sleep -Milliseconds 120
        Send-MouseDown $hwnd 526 83
        Start-Sleep -Milliseconds 120
        Send-MouseMove $hwnd 526 274 $true
        Start-Sleep -Milliseconds 320
        $summary.phase = 'commit_popup_drag'
        Save-Screenshot $hwnd $commit_popup_drag_png
        Send-MouseUp $hwnd 526 274
        Start-Sleep -Milliseconds 500
        $summary.phase = 'commit_popup_drag_release'
        Save-Screenshot $hwnd $commit_popup_release_png
        Send-MouseMove $hwnd 128 323 $false
        Start-Sleep -Milliseconds 900
        $summary.phase = 'second_commit_popup'
        Save-Screenshot $hwnd $second_commit_popup_png
        $summary.status = if ($process.HasExited) { 'exited' } else { 'survived' }
        return
    }

    if ($branch_scroll_checks) {
        Send-Click $hwnd 48 250
        Start-Sleep -Milliseconds 2200
        $summary.phase = 'graph'
        Save-Screenshot $hwnd $graph_png
        Save-LeftCrop $graph_png $graph_crop_png

        Send-Click $hwnd 211 80
        Start-Sleep -Milliseconds 800
        $summary.phase = 'branches'
        Save-Screenshot $hwnd $branches_png
        Save-LeftCrop $branches_png $branches_crop_png

        Send-Wheel $hwnd 120 154 $branch_scroll_notches
        Start-Sleep -Milliseconds 800
        $summary.phase = 'branches_scrolled'
        Save-Screenshot $hwnd $branches_scroll_png
        Save-LeftCrop $branches_scroll_png $branches_scroll_crop_png
        $summary.status = if ($process.HasExited) { 'exited' } else { 'survived' }
        return
    }

    Send-Click $hwnd 128 88
    Start-Sleep -Milliseconds 800
    $summary.phase = 'branches'
    Save-Screenshot $hwnd $branches_png
    Save-LeftCrop $branches_png $branches_crop_png
    Send-Click $hwnd 128 88
    Start-Sleep -Milliseconds 400

    if ($commit_button_checks) {
        Send-Click $hwnd 120 110
        Start-Sleep -Milliseconds 400
        $summary.phase = 'message_focus'
        Save-Screenshot $hwnd $stage_png
        Save-LeftCrop $stage_png $stage_crop_png

        Send-Text $hwnd "   "
        Start-Sleep -Milliseconds 800
        $summary.phase = 'spaces_message'
        Save-Screenshot $hwnd $spaces_png
        Save-LeftCrop $spaces_png $spaces_crop_png

        Send-Text $hwnd "commit button ready"
        Start-Sleep -Milliseconds 800
        $summary.phase = 'valid_message'
        Save-Screenshot $hwnd $message_png
        Save-LeftCrop $message_png $message_crop_png
    } else {
        Send-Char $hwnd 's'
        Start-Sleep -Milliseconds 1800
        $summary.phase = 'stage'
        Save-Screenshot $hwnd $stage_png
        Save-LeftCrop $stage_png $stage_crop_png

        Send-Char $hwnd 'u'
        Start-Sleep -Milliseconds 1800
        $summary.phase = 'unstage'
        Save-Screenshot $hwnd $unstage_png
        Save-LeftCrop $unstage_png $unstage_crop_png
    }

    $summary.status = if ($process.HasExited) { 'exited' } else { 'survived' }
}
catch {
    $summary.status = 'script_error'
    $summary.error = ($_ | Out-String)
}
finally {
    if ($process -ne $null -and $started) {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $summary.exit_code = $process.ExitCode
        [System.IO.File]::WriteAllText($stdout_path, $process.StandardOutput.ReadToEnd())
        [System.IO.File]::WriteAllText($stderr_path, $process.StandardError.ReadToEnd())
    }
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
