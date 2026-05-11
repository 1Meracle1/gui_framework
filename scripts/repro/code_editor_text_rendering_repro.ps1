param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [ValidateSet('dwrite', 'freetype')]
    [string]$font_backend = 'dwrite',
    [ValidateRange(8, 32)]
    [int]$font_size = 12,
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
    if ($font_backend -eq 'freetype') {
        $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_text_rendering_freetype'
    } else {
        $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_text_rendering'
    }
    if ($font_size -ne 12) {
        $artifact_dir = "${artifact_dir}_${font_size}px"
    }
}
$resolved_file_path = if ([System.IO.Path]::IsPathRooted($file_path)) {
    $file_path
} else {
    Join-Path $workspace_path $file_path
}
$resolved_file_path = (Resolve-Path $resolved_file_path).Path

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$text_diagnostics = Join-Path $artifact_dir 'current_text_diagnostics.json'
$sharp_png = Join-Path $artifact_dir '01_sharp.png'
$sharp_frame = Join-Path $artifact_dir '01_sharp_frame.json'
$sharp_diag = Join-Path $artifact_dir '01_sharp_text_diagnostics.json'
$sharp_crop = Join-Path $artifact_dir '01_sharp_crop_4x.png'
$smooth_png = Join-Path $artifact_dir '02_smooth.png'
$smooth_frame = Join-Path $artifact_dir '02_smooth_frame.json'
$smooth_diag = Join-Path $artifact_dir '02_smooth_text_diagnostics.json'
$smooth_crop = Join-Path $artifact_dir '02_smooth_crop_4x.png'
$lcd_smooth_png = Join-Path $artifact_dir '03_lcd_smooth.png'
$lcd_smooth_frame = Join-Path $artifact_dir '03_lcd_smooth_frame.json'
$lcd_smooth_diag = Join-Path $artifact_dir '03_lcd_smooth_text_diagnostics.json'
$lcd_smooth_crop = Join-Path $artifact_dir '03_lcd_smooth_crop_4x.png'
$lcd_sharp_png = Join-Path $artifact_dir '04_lcd_sharp.png'
$lcd_sharp_frame = Join-Path $artifact_dir '04_lcd_sharp_frame.json'
$lcd_sharp_diag = Join-Path $artifact_dir '04_lcd_sharp_text_diagnostics.json'
$lcd_sharp_crop = Join-Path $artifact_dir '04_lcd_sharp_crop_4x.png'
Remove-Item -Force $text_diagnostics, $sharp_png, $sharp_frame, $sharp_diag, $sharp_crop, $smooth_png, $smooth_frame, $smooth_diag, $smooth_crop, $lcd_smooth_png, $lcd_smooth_frame, $lcd_smooth_diag, $lcd_smooth_crop, $lcd_sharp_png, $lcd_sharp_frame, $lcd_sharp_diag, $lcd_sharp_crop -ErrorAction SilentlyContinue

function Run-ReproCommand([string]$command) {
    Send-ReproChar ':'
    Start-Sleep -Milliseconds 200
    Send-ReproText $command
    Start-Sleep -Milliseconds 200
    Send-ReproKey 0x0D
}

function Save-CodeCrop([string]$name, [string]$source, [string]$target) {
    $diagnostics = Read-CodeEditorTextDiagnostics $text_diagnostics
    Save-ReproImageCrop `
        $name `
        $source `
        $target `
        ([int]$diagnostics.crop.x) `
        ([int]$diagnostics.crop.y) `
        ([int]$diagnostics.crop.width) `
        ([int]$diagnostics.crop.height) `
        4
}

function Read-CodeEditorTextDiagnostics([string]$path) {
    for ($index = 0; $index -lt 80; $index += 1) {
        if (Test-Path $path) {
            try {
                return (Get-Content -Raw $path) | ConvertFrom-Json
            } catch {
                Start-Sleep -Milliseconds 50
            }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Timed out waiting for valid text diagnostics: $path"
}

function Copy-CodeEditorTextDiagnostics([string]$name, [string]$target) {
    $diagnostics = Read-CodeEditorTextDiagnostics $text_diagnostics
    $diagnostics | Add-Member -NotePropertyName window_diagnostics -NotePropertyValue (Get-ReproWindowDiagnostics)
    $diagnostics | ConvertTo-Json -Depth 16 | Set-Content -Path $target -Encoding utf8
    Add-ReproArtifact $name $target
}

$previous_font_backend = $env:CODE_EDITOR_FONT_BACKEND
$previous_text_diagnostics = $env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH
$previous_text_reference_mode = $env:CODE_EDITOR_TEXT_REFERENCE_MODE
if ($font_backend -eq 'freetype') {
    $env:CODE_EDITOR_FONT_BACKEND = 'freetype'
} else {
    Remove-Item Env:CODE_EDITOR_FONT_BACKEND -ErrorAction SilentlyContinue
}
$env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH = $text_diagnostics
$env:CODE_EDITOR_TEXT_REFERENCE_MODE = '1'

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
    Add-ReproArtifact 'font_backend' $font_backend
    Add-ReproArtifact 'font_size' $font_size
    Add-ReproArtifact 'text_diagnostics_path' $text_diagnostics
    $script:repro_summary['window_diagnostics'] = Get-ReproWindowDiagnostics
    Start-Sleep -Milliseconds $settle_ms
    Run-ReproCommand "set editor.font-size=$font_size"
    Start-Sleep -Milliseconds 1200
    Run-ReproCommand 'set editor.raster-policy=sharp'
    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'sharp'
    Save-ReproClientScreenshot 'launch_png' $sharp_png
    Copy-CodeEditorTextDiagnostics 'launch_text_diagnostics' $sharp_diag
    Save-CodeCrop 'launch_crop' $sharp_png $sharp_crop
    Copy-ReproFrameDump 'launch_frame' $sharp_frame
    Assert-ReproBoxVisible 'editor_surface' $sharp_frame | Out-Null

    Run-ReproCommand 'set editor.raster-policy=smooth'
    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'smooth'
    Save-ReproClientScreenshot 'smooth_png' $smooth_png
    Copy-CodeEditorTextDiagnostics 'smooth_text_diagnostics' $smooth_diag
    Save-CodeCrop 'smooth_crop' $smooth_png $smooth_crop
    Copy-ReproFrameDump 'smooth_frame' $smooth_frame
    Assert-ReproBoxVisible 'editor_surface' $smooth_frame | Out-Null

    Run-ReproCommand 'set editor.raster-policy=lcd-smooth'
    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'lcd_smooth'
    Save-ReproClientScreenshot 'lcd_smooth_png' $lcd_smooth_png
    Copy-CodeEditorTextDiagnostics 'lcd_smooth_text_diagnostics' $lcd_smooth_diag
    Save-CodeCrop 'lcd_smooth_crop' $lcd_smooth_png $lcd_smooth_crop
    Copy-ReproFrameDump 'lcd_smooth_frame' $lcd_smooth_frame
    Assert-ReproBoxVisible 'editor_surface' $lcd_smooth_frame | Out-Null

    Run-ReproCommand 'set editor.raster-policy=lcd-sharp'
    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'lcd_sharp'
    Save-ReproClientScreenshot 'lcd_sharp_png' $lcd_sharp_png
    Copy-CodeEditorTextDiagnostics 'lcd_sharp_text_diagnostics' $lcd_sharp_diag
    Save-CodeCrop 'lcd_sharp_crop' $lcd_sharp_png $lcd_sharp_crop
    Copy-ReproFrameDump 'lcd_sharp_frame' $lcd_sharp_frame
    Assert-ReproBoxVisible 'editor_surface' $lcd_sharp_frame | Out-Null

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
    if ($previous_font_backend -eq $null) {
        Remove-Item Env:CODE_EDITOR_FONT_BACKEND -ErrorAction SilentlyContinue
    } else {
        $env:CODE_EDITOR_FONT_BACKEND = $previous_font_backend
    }
    if ($previous_text_diagnostics -eq $null) {
        Remove-Item Env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH -ErrorAction SilentlyContinue
    } else {
        $env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH = $previous_text_diagnostics
    }
    if ($previous_text_reference_mode -eq $null) {
        Remove-Item Env:CODE_EDITOR_TEXT_REFERENCE_MODE -ErrorAction SilentlyContinue
    } else {
        $env:CODE_EDITOR_TEXT_REFERENCE_MODE = $previous_text_reference_mode
    }
}
