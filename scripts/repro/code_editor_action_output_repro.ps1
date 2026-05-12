param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repro_common.ps1')

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($exe_path -eq '') {
    $exe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\code_editor.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_action_output_repro'
}
if ($workspace_path -eq '') {
    $workspace_path = Join-Path $artifact_dir 'workspace'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
New-Item -ItemType Directory -Force -Path $workspace_path | Out-Null

$config_path = Join-Path $workspace_path 'code_editor.toml'
$action_path = Join-Path $workspace_path 'action_repro.ps1'
$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$launch_png = Join-Path $artifact_dir '01_launch.png'
$launch_frame = Join-Path $artifact_dir '01_launch_frame.json'
$running_png = Join-Path $artifact_dir '02_running.png'
$running_frame = Join-Path $artifact_dir '02_running_frame.json'
$finished_png = Join-Path $artifact_dir '03_finished.png'
$finished_frame = Join-Path $artifact_dir '03_finished_frame.json'
$scrolled_png = Join-Path $artifact_dir '04_scrolled.png'
$scrolled_frame = Join-Path $artifact_dir '04_scrolled_frame.json'
$horizontal_png = Join-Path $artifact_dir '05_horizontal.png'
$horizontal_frame = Join-Path $artifact_dir '05_horizontal_frame.json'
$focused_frame = Join-Path $artifact_dir '06_focused_frame.json'
Remove-Item -Force `
    $frame_dump, `
    $launch_png, $launch_frame, `
    $running_png, $running_frame, `
    $finished_png, $finished_frame, `
    $scrolled_png, $scrolled_frame, `
    $horizontal_png, $horizontal_frame, `
    $focused_frame `
    -ErrorAction SilentlyContinue

@'
for ($i = 1; $i -le 36; $i += 1) {
    $suffix = if ($i -eq 5) { ' ' + ('x' * 180) } else { '' }
    if (($i % 2) -eq 0) {
        [Console]::Error.WriteLine(('stderr {0:D2}{1}' -f $i, $suffix))
    } else {
        [Console]::Out.WriteLine(('stdout {0:D2}{1}' -f $i, $suffix))
    }
    Start-Sleep -Milliseconds 35
}
exit 7
'@ | Set-Content -Path $action_path -Encoding ASCII

@'
[actions.repro]
keybinding = "Ctrl+Shift+B"
command = "powershell -NoProfile -ExecutionPolicy Bypass -File action_repro.ps1"
'@ | Set-Content -Path $config_path -Encoding ASCII

function Send-ReproChord([int[]]$mods, [int]$vk) {
    foreach ($mod in $mods) {
        [ReproWin32]::PostMessage(
            $script:repro_hwnd,
            [ReproWin32]::WM_KEYDOWN,
            [IntPtr]$mod,
            [IntPtr]1
        ) | Out-Null
        Start-Sleep -Milliseconds 20
    }
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_KEYDOWN,
        [IntPtr]$vk,
        [IntPtr]1
    ) | Out-Null
    Start-Sleep -Milliseconds 40
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_KEYUP,
        [IntPtr]$vk,
        [IntPtr]0
    ) | Out-Null
    [Array]::Reverse($mods)
    foreach ($mod in $mods) {
        [ReproWin32]::PostMessage(
            $script:repro_hwnd,
            [ReproWin32]::WM_KEYUP,
            [IntPtr]$mod,
            [IntPtr]0
        ) | Out-Null
        Start-Sleep -Milliseconds 20
    }
}

function Send-ReproKeyState([int]$vk, [bool]$down) {
    $message = if ($down) { [ReproWin32]::WM_KEYDOWN } else { [ReproWin32]::WM_KEYUP }
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        $message,
        [IntPtr]$vk,
        [IntPtr]1
    ) | Out-Null
    Start-Sleep -Milliseconds 30
}

function Get-ReproOutputBody([string]$frame_path) {
    $frame = Read-ReproFrame $frame_path
    $matches = @($frame.boxes | Where-Object { $_.debug_name -eq 'workspace_action_output_body' })
    if ($matches.Count -ne 1) {
        throw "Expected one output body, found $($matches.Count)."
    }
    return $matches[0]
}

function Assert-ReproOutputOrder([string]$frame_path, [string[]]$texts) {
    $body = Get-ReproOutputBody $frame_path
    $joined = [string]$body.text
    $last = -1
    foreach ($text in $texts) {
        $index = $joined.IndexOf($text)
        if ($index -lt 0) {
            throw "Expected action output text not found: $text"
        }
        if ($index -le $last) {
            throw "Action output text was out of order: $text"
        }
        $last = $index
    }
}

function Assert-ReproOutputSelectableShape([string]$frame_path) {
    $frame = Read-ReproFrame $frame_path
    $body = Get-ReproOutputBody $frame_path
    if ($body.kind -ne 'selectable_label') {
        throw 'Output body must be the single selectable label.'
    }
    $extra = @($frame.boxes | Where-Object {
            $_.debug_name -eq 'workspace_action_output_selection' -or
            $_.debug_name -eq 'workspace_action_output_line'
        })
    if ($extra.Count -ne 0) {
        throw 'Output must not use per-line selectable or hidden selection boxes.'
    }
    if ($body.scroll -eq $null) {
        throw 'Output body must expose a scroll state.'
    }
}

function Assert-ReproOutputAtBottom([string]$frame_path) {
    $body = Get-ReproOutputBody $frame_path
    if ([double]$body.scroll.max_y -le 0.0) {
        throw 'Expected vertical overflow in output body.'
    }
    if ([double]$body.scroll.y -lt [double]$body.scroll.max_y - 1.0) {
        throw 'Expected output body to follow the bottom.'
    }
}

function Assert-ReproOutputScrolledUp([string]$frame_path) {
    $body = Get-ReproOutputBody $frame_path
    if ([double]$body.scroll.y -ge [double]$body.scroll.max_y - 1.0) {
        throw 'Expected output body to remain scrolled upward.'
    }
}

function Assert-ReproHorizontalOverflow([string]$frame_path) {
    $body = Get-ReproOutputBody $frame_path
    if ([double]$body.scroll.max_x -le 0.0) {
        throw 'Expected horizontal overflow in output body.'
    }
}

function Assert-ReproHorizontallyScrolled([string]$frame_path) {
    $body = Get-ReproOutputBody $frame_path
    if ([double]$body.scroll.x -le 0.0) {
        throw 'Expected output body to scroll horizontally.'
    }
}

try {
    Start-ReproSession `
        -workspace_path $workspace_path `
        -exe_path $exe_path `
        -artifact_dir $artifact_dir `
        -arguments @('--automation-dump-frame', $frame_dump, $workspace_path) `
        -frame_dump_path $frame_dump `
        -window_width 1320 `
        -window_height 860

    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'launch'
    Save-ReproScreenshot 'launch_png' $launch_png
    Copy-ReproFrameDump 'launch_frame' $launch_frame
    Assert-ReproBoxVisible 'editor_surface' $launch_frame | Out-Null

    Send-ReproChord @(0x11, 0x10) 0x42
    Start-Sleep -Milliseconds 700
    Set-ReproPhase 'running'
    Save-ReproScreenshot 'running_png' $running_png
    Copy-ReproFrameDump 'running_frame' $running_frame
    Assert-ReproBoxVisible 'workspace_action_popup' $running_frame | Out-Null
    Assert-ReproBoxVisible 'workspace_action_output_body' $running_frame | Out-Null
    Assert-ReproOutputOrder $running_frame @('stdout 01', 'stderr 02', 'stdout 03', 'stderr 04')
    Assert-ReproOutputSelectableShape $running_frame

    Start-Sleep -Milliseconds 1600
    Set-ReproPhase 'finished'
    Save-ReproScreenshot 'finished_png' $finished_png
    Copy-ReproFrameDump 'finished_frame' $finished_frame
    Assert-ReproFrameTextContains 'Return code: 7' $finished_frame | Out-Null
    Assert-ReproOutputOrder $finished_frame @(
        'stdout 01',
        'stderr 02',
        'stdout 35',
        'stderr 36'
    )
    Assert-ReproOutputSelectableShape $finished_frame
    Assert-ReproOutputAtBottom $finished_frame
    Assert-ReproHorizontalOverflow $finished_frame
    $output_box = Assert-ReproBoxVisible 'workspace_action_output_body' $finished_frame
    $return_box = Assert-ReproBoxVisible 'workspace_action_return_code' $finished_frame
    if ([double]$return_box.rect.min_y -le [double]$output_box.rect.max_y) {
        throw 'Return code was not rendered below the scrollable output body.'
    }

    $wheel_x = [int][Math]::Round(([double]$output_box.rect.min_x + [double]$output_box.rect.max_x) * 0.5)
    $wheel_y = [int][Math]::Round(([double]$output_box.rect.min_y + [double]$output_box.rect.max_y) * 0.5)
    Send-ReproWheel $wheel_x $wheel_y 10
    Start-Sleep -Milliseconds 500
    Set-ReproPhase 'scrolled_up'
    Save-ReproScreenshot 'scrolled_png' $scrolled_png
    Copy-ReproFrameDump 'scrolled_frame' $scrolled_frame
    Assert-ReproOutputScrolledUp $scrolled_frame
    Assert-ReproOutputSelectableShape $scrolled_frame

    Send-ReproKeyState 0x10 $true
    Send-ReproWheel $wheel_x $wheel_y -6
    Start-Sleep -Milliseconds 500
    Send-ReproKeyState 0x10 $false
    Start-Sleep -Milliseconds 500
    Set-ReproPhase 'horizontal_scroll'
    Save-ReproScreenshot 'horizontal_png' $horizontal_png
    Copy-ReproFrameDump 'horizontal_frame' $horizontal_frame
    Assert-ReproHorizontallyScrolled $horizontal_frame

    Send-ReproClick $wheel_x $wheel_y
    Start-Sleep -Milliseconds 6200
    Set-ReproPhase 'focused_after_hold'
    Copy-ReproFrameDump 'focused_frame' $focused_frame
    Assert-ReproBoxVisible 'workspace_action_popup' $focused_frame | Out-Null
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
