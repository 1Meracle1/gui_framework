param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [int]$settle_ms = 6000
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
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_text_rendering'
}
$resolved_file_path = if ([System.IO.Path]::IsPathRooted($file_path)) {
    $file_path
} else {
    Join-Path $workspace_path $file_path
}
$resolved_file_path = (Resolve-Path $resolved_file_path).Path

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$sharp_png = Join-Path $artifact_dir '01_sharp.png'
$sharp_frame = Join-Path $artifact_dir '01_sharp_frame.json'
$smooth_png = Join-Path $artifact_dir '02_smooth.png'
$smooth_frame = Join-Path $artifact_dir '02_smooth_frame.json'
Remove-Item -Force $sharp_png, $sharp_frame, $smooth_png, $smooth_frame -ErrorAction SilentlyContinue

function Run-ReproCommand([string]$command) {
    Send-ReproChar ':'
    Start-Sleep -Milliseconds 200
    Send-ReproText $command
    Start-Sleep -Milliseconds 200
    Send-ReproKey 0x0D
}

try {
    Start-ReproSession `
        -workspace_path $workspace_path `
        -exe_path $exe_path `
        -artifact_dir $artifact_dir `
        -arguments @('--automation-dump-frame', $frame_dump, $resolved_file_path) `
        -frame_dump_path $frame_dump `
        -window_width 1500 `
        -window_height 980

    Add-ReproArtifact 'file_path' $resolved_file_path
    Start-Sleep -Milliseconds $settle_ms
    Set-ReproPhase 'sharp'
    Save-ReproScreenshot 'launch_png' $sharp_png
    Copy-ReproFrameDump 'launch_frame' $sharp_frame
    Assert-ReproBoxVisible 'editor_surface' $sharp_frame | Out-Null

    Run-ReproCommand 'set editor.raster-policy=smooth'
    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'smooth'
    Save-ReproScreenshot 'smooth_png' $smooth_png
    Copy-ReproFrameDump 'smooth_frame' $smooth_frame
    Assert-ReproBoxVisible 'editor_surface' $smooth_frame | Out-Null
    $status = if (Test-ReproExited) { 'exited' } else { 'survived' }
    Set-ReproStatus $status
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
