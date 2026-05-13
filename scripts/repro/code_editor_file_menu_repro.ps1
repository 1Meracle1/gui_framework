param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repro_common.ps1')

if (-not ('ReproMenuWin32' -as [type])) {
    Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ReproMenuWin32 {
    public const uint WM_CLOSE = 0x0010;
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextLengthW(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text, int maxCount);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder text, int maxCount);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@
}

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($workspace_path -eq '') {
    $workspace_path = Join-Path $repo_root '_codex_artifacts\code_editor_file_menu_repro\workspace'
}
if ($exe_path -eq '') {
    $exe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\code_editor.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_file_menu_repro'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
New-Item -ItemType Directory -Force -Path $workspace_path | Out-Null
$artifact_dir = (Resolve-Path $artifact_dir).Path
$workspace_path = (Resolve-Path $workspace_path).Path
$exe_path = (Resolve-Path $exe_path).Path

$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$launch_png = Join-Path $artifact_dir '01_launch.png'
$launch_frame = Join-Path $artifact_dir '01_launch_frame.json'
$menu_png = Join-Path $artifact_dir '02_menu.png'
$menu_frame = Join-Path $artifact_dir '02_menu_frame.json'
$new_file_hover_png = Join-Path $artifact_dir '02_new_file_hover.png'
$new_file_png = Join-Path $artifact_dir '03_new_file.png'
$new_file_frame = Join-Path $artifact_dir '03_new_file_frame.json'
$open_folder_hover_png = Join-Path $artifact_dir '03_open_folder_hover.png'
$dialog_png = Join-Path $artifact_dir '04_open_folder_dialog.png'
Remove-Item -Force `
    $frame_dump, `
    $launch_png, $launch_frame, `
    $menu_png, $menu_frame, `
    $new_file_hover_png, `
    $new_file_png, $new_file_frame, `
    $open_folder_hover_png, `
    $dialog_png `
    -ErrorAction SilentlyContinue

function Get-ReproText([IntPtr]$handle) {
    $length = [ReproMenuWin32]::GetWindowTextLengthW($handle)
    $builder = [System.Text.StringBuilder]::new($length + 1)
    [ReproMenuWin32]::GetWindowTextW($handle, $builder, $builder.Capacity) | Out-Null
    return $builder.ToString()
}

function Get-ReproClassName([IntPtr]$handle) {
    $builder = [System.Text.StringBuilder]::new(256)
    [ReproMenuWin32]::GetClassNameW($handle, $builder, $builder.Capacity) | Out-Null
    return $builder.ToString()
}

function Get-ReproBoxCenter($box) {
    return [ordered]@{
        x = [int][Math]::Round(([double]$box.rect.min_x + [double]$box.rect.max_x) * 0.5)
        y = [int][Math]::Round(([double]$box.rect.min_y + [double]$box.rect.max_y) * 0.5)
    }
}

function Click-ReproBox($box) {
    $center = Get-ReproBoxCenter $box
    Send-ReproClick $center.x $center.y
    Start-Sleep -Milliseconds 550
}

function Assert-ReproExactText([string]$text) {
    $frame = Read-ReproFrame
    $matches = @($frame.boxes | Where-Object {
            $_.text -eq $text -and
            ([double]$_.rect.max_x - [double]$_.rect.min_x) -gt 0.0 -and
            ([double]$_.rect.max_y - [double]$_.rect.min_y) -gt 0.0
        })
    if ($matches.Count -eq 0) {
        throw "Expected exact frame text not found: $text"
    }
    return $matches[0]
}

function Find-ReproDialog([string]$title) {
    for ($attempt = 0; $attempt -lt 50; $attempt += 1) {
        $script:file_menu_dialog = [IntPtr]::Zero
        $script:file_menu_dialog_title = ''
        $script:file_menu_dialog_class = ''
        $script:file_menu_dialog_pid = 0
        $callback = [ReproMenuWin32+EnumWindowsProc]{
            param([IntPtr]$hWnd, [IntPtr]$lParam)
            if (-not [ReproMenuWin32]::IsWindowVisible($hWnd)) {
                return $true
            }
            $text = Get-ReproText $hWnd
            if ($text -notlike "*$title*") {
                return $true
            }
            $window_process_id = 0
            [ReproMenuWin32]::GetWindowThreadProcessId($hWnd, [ref]$window_process_id) | Out-Null
            $script:file_menu_dialog = $hWnd
            $script:file_menu_dialog_title = $text
            $script:file_menu_dialog_class = Get-ReproClassName $hWnd
            $script:file_menu_dialog_pid = $window_process_id
            return $false
        }
        [ReproMenuWin32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
        if ($script:file_menu_dialog -ne [IntPtr]::Zero) {
            return $script:file_menu_dialog
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Folder picker dialog not found: $title"
}

try {
    Start-ReproSession `
        -workspace_path $workspace_path `
        -exe_path $exe_path `
        -artifact_dir $artifact_dir `
        -arguments @('--automation-dump-frame', $frame_dump, $workspace_path) `
        -frame_dump_path $frame_dump `
        -window_width 1500 `
        -window_height 980

    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'launch'
    Save-ReproScreenshot 'launch_png' $launch_png
    Copy-ReproFrameDump 'launch_frame' $launch_frame
    $file_button = Assert-ReproBoxVisible 'activity_file_menu' $launch_frame

    Click-ReproBox $file_button
    Set-ReproPhase 'menu_open'
    Save-ReproScreenshot 'menu_png' $menu_png
    Copy-ReproFrameDump 'menu_frame' $menu_frame
    Assert-ReproBoxVisible 'file_menu_popup' $menu_frame | Out-Null
    Assert-ReproFrameTextContains 'New File' $menu_frame | Out-Null
    Assert-ReproFrameTextContains 'Open Folder...' $menu_frame | Out-Null

    $new_file = Assert-ReproExactText 'New File'
    $new_file_center = Get-ReproBoxCenter $new_file
    Send-ReproMouseMove $new_file_center.x $new_file_center.y $false
    Start-Sleep -Milliseconds 600
    Save-ReproScreenshot 'new_file_hover_png' $new_file_hover_png
    Click-ReproBox $new_file
    Set-ReproPhase 'new_file_clicked'
    Save-ReproScreenshot 'new_file_png' $new_file_png
    Copy-ReproFrameDump 'new_file_frame' $new_file_frame
    Assert-ReproFrameTextContains 'scratch 2' $new_file_frame | Out-Null

    $file_button = Assert-ReproBoxVisible 'activity_file_menu' $new_file_frame
    Click-ReproBox $file_button
    Assert-ReproBoxVisible 'file_menu_popup' | Out-Null
    $open_folder = Assert-ReproExactText 'Open Folder...'
    $open_folder_center = Get-ReproBoxCenter $open_folder
    Send-ReproMouseMove $open_folder_center.x $open_folder_center.y $false
    Start-Sleep -Milliseconds 600
    Save-ReproScreenshot 'open_folder_hover_png' $open_folder_hover_png
    Click-ReproBox $open_folder
    Set-ReproPhase 'open_folder_clicked'
    $dialog = Find-ReproDialog 'Open Folder in Current Window'
    $script:repro_summary['dialog_hwnd'] = ('0x{0:X}' -f $dialog.ToInt64())
    $script:repro_summary['dialog_title'] = $script:file_menu_dialog_title
    $script:repro_summary['dialog_class'] = $script:file_menu_dialog_class
    $script:repro_summary['dialog_pid'] = $script:file_menu_dialog_pid
    Save-ReproScreenshot 'dialog_png' $dialog_png
    [ReproMenuWin32]::PostMessage($dialog, [ReproMenuWin32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) |
        Out-Null
    Start-Sleep -Milliseconds 500
    Set-ReproStatus 'survived'
} catch {
    Set-ReproStatus 'script_error'
    if ($script:repro_summary -ne $null) {
        $script:repro_summary['error'] = ($_ | Out-String)
    } else {
        Write-Error ($_ | Out-String)
    }
} finally {
    Complete-ReproSession
}
