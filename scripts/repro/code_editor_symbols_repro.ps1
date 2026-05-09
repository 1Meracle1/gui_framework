param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$open_file = 'examples\code_editor\editor_render.cpp'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repro_common.ps1')

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($workspace_path -eq '') {
    $workspace_path = $repo_root
}
if ($exe_path -eq '') {
    $exe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\code_editor.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_symbols_repro'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$launch_png = Join-Path $artifact_dir '01_launch.png'
$launch_frame = Join-Path $artifact_dir '01_launch_frame.json'
$search_png = Join-Path $artifact_dir '02_file_search.png'
$search_frame = Join-Path $artifact_dir '02_file_search_frame.json'
$file_png = Join-Path $artifact_dir '03_file_open.png'
$file_frame = Join-Path $artifact_dir '03_file_open_frame.json'
$symbols_png = Join-Path $artifact_dir '04_symbols.png'
$symbols_frame = Join-Path $artifact_dir '04_symbols_frame.json'
Remove-Item -Force `
    $launch_png, $launch_frame, `
    $search_png, $search_frame, `
    $file_png, $file_frame, `
    $symbols_png, $symbols_frame `
    -ErrorAction SilentlyContinue

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
    Assert-ReproBoxVisible 'editor_surface' $launch_frame | Out-Null

    Send-ReproKey 0x20
    Send-ReproChar 'f'
    Start-Sleep -Milliseconds 800
    Set-ReproPhase 'file_search'
    Save-ReproScreenshot 'search_png' $search_png
    Copy-ReproFrameDump 'search_frame' $search_frame
    Assert-ReproBoxVisible 'file_search_modal' $search_frame | Out-Null

    Send-ReproText $open_file 20
    Start-Sleep -Milliseconds 600
    Send-ReproKey 0x0D
    Start-Sleep -Milliseconds 2500
    if (Test-ReproExited) {
        Set-ReproStatus 'crashed'
        Set-ReproPhase 'after_open_file'
    } else {
        Set-ReproPhase 'file_open'
        Save-ReproScreenshot 'file_png' $file_png
        Copy-ReproFrameDump 'file_frame' $file_frame
        Assert-ReproBoxVisible 'editor_surface' $file_frame | Out-Null

        Start-Sleep -Milliseconds 3000
        Send-ReproKey 0x20
        Send-ReproChar 's'
        for ($index = 0; $index -lt 70; $index += 1) {
            if (Test-ReproExited) {
                break
            }
            Start-Sleep -Milliseconds 100
        }
        if (Test-ReproExited) {
            Set-ReproStatus 'crashed'
            Set-ReproPhase 'symbols_request'
        } else {
            Set-ReproStatus 'survived'
            Set-ReproPhase 'symbols_visible'
            Save-ReproScreenshot 'symbols_png' $symbols_png
            Copy-ReproFrameDump 'symbols_frame' $symbols_frame
            Assert-ReproBoxVisible 'jump_list_modal' $symbols_frame | Out-Null
        }
    }
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
