param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$stage_probe_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [int]$font_size = 12,
    [int]$settle_ms = 1200,
    [string]$chrome_path = '',
    [switch]$skip_chrome
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
if ($stage_probe_path -eq '') {
    $stage_probe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\text_stage_probe.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\recovery_phase5_gpu_composition'
}

$gpu_dir = Join-Path $artifact_dir "gpu_capture_${font_size}px"
$chrome_dir = Join-Path $artifact_dir "chrome_${font_size}px"
$stage_dir = Join-Path $artifact_dir 'stage_probe'
$summary_path = Join-Path $artifact_dir 'phase5_stage_summary.json'
$metrics_path = Join-Path $artifact_dir 'phase5_stage_metrics.json'
$contact_path = Join-Path $artifact_dir 'phase5_stage_contact_sheet.png'
$command_log = Join-Path $artifact_dir 'phase5_commands.txt'
$run_log = Join-Path $artifact_dir 'phase5_run_output.txt'

New-Item -ItemType Directory -Force -Path $artifact_dir, $gpu_dir, $chrome_dir, $stage_dir | Out-Null
Remove-Item -Force $summary_path, $metrics_path, $contact_path, $command_log, $run_log -ErrorAction SilentlyContinue

function Invoke-LoggedScript([string]$script_path, [string[]]$arguments) {
    $command = 'powershell -NoProfile -ExecutionPolicy Bypass -File ' +
        (Quote-ReproArgument $script_path) + ' ' + (Join-ReproArguments $arguments)
    Add-Content -Path $command_log -Value $command
    Add-Content -Path $run_log -Value ">>> $command"
    $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $script_path @arguments 2>&1
    $output | Out-File -FilePath $run_log -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $command"
    }
}

function Invoke-LoggedExe([string]$path, [string[]]$arguments) {
    $command = (Quote-ReproArgument $path) + ' ' + (Join-ReproArguments $arguments)
    Add-Content -Path $command_log -Value $command
    Add-Content -Path $run_log -Value ">>> $command"
    $output = & $path @arguments 2>&1
    $output | Out-File -FilePath $run_log -Append -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $command"
    }
}

function Read-JsonFile([string]$path) {
    return (Get-Content -Raw $path) | ConvertFrom-Json
}

function Save-FirstLineFromChromeClient([string]$name, [string]$source, [string]$diagnostics_path, [string]$target) {
    $diagnostics = Read-JsonFile $diagnostics_path
    $viewport_y = [int]([double]$diagnostics.window_diagnostics.client_rect.height - [double]$diagnostics.inner_height)
    if ($viewport_y -lt 0) {
        $viewport_y = 0
    }
    Save-ReproImageCrop `
        $name `
        $source `
        $target `
        0 `
        ($viewport_y + [int][Math]::Round([double]$diagnostics.first_line_rect.top)) `
        ([int]$diagnostics.crop.width) `
        ([int][Math]::Round([double]$diagnostics.line_height)) `
        1
}

function Save-FirstLineFromScaledCrop([string]$name, [string]$source, [string]$diagnostics_path, [string]$target) {
    $diagnostics = Read-JsonFile $diagnostics_path
    $width = [int]$diagnostics.crop.width
    $height = [int][Math]::Round([double]$diagnostics.line_height)
    $scale = 4
    $bitmap = [System.Drawing.Bitmap]::FromFile($source)
    $crop = $bitmap.Clone(
        [System.Drawing.Rectangle]::new(0, 0, $width * $scale, $height * $scale),
        $bitmap.PixelFormat
    )
    $copy = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($copy)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.DrawImage($crop, 0, 0, $width, $height)
    $copy.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $copy.Dispose()
    $crop.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact $name $target
}

$text_script = Join-Path $PSScriptRoot 'code_editor_text_rendering_repro.ps1'
$chrome_script = Join-Path $PSScriptRoot 'code_editor_chrome_text_reference.ps1'
$font_path = (Resolve-Path (Join-Path $repo_root 'third_party\source_code_pro\SourceCodePro-Regular.ttf')).Path

$text_args = @(
    '-workspace_path', $workspace_path,
    '-exe_path', $exe_path,
    '-artifact_dir', $gpu_dir,
    '-file_path', $file_path,
    '-font_backend', 'dwrite',
    '-font_size', [string]$font_size,
    '-settle_ms', [string]$settle_ms
)
Invoke-LoggedScript $text_script $text_args

if (-not $skip_chrome) {
    $chrome_args = @(
        '-workspace_path', $workspace_path,
        '-artifact_dir', $chrome_dir,
        '-file_path', $file_path,
        '-font_size', [string]$font_size,
        '-settle_ms', [string]$settle_ms
    )
    if ($chrome_path -ne '') {
        $chrome_args += @('-chrome_path', $chrome_path)
    }
    Invoke-LoggedScript $chrome_script $chrome_args
}

Invoke-LoggedExe $stage_probe_path @($stage_dir, $font_path)

$gpu_sharp_first_line = Join-Path $artifact_dir 'gpu_sharp_first_line.png'
$gpu_lcd_first_line = Join-Path $artifact_dir 'gpu_lcd_sharp_first_line.png'
$chrome_first_line = Join-Path $artifact_dir 'chrome_first_line.png'

Save-FirstLineFromScaledCrop `
    'gpu_sharp_first_line' `
    (Join-Path $gpu_dir '01_sharp_crop_4x.png') `
    (Join-Path $gpu_dir '01_sharp_text_diagnostics.json') `
    $gpu_sharp_first_line
Save-FirstLineFromScaledCrop `
    'gpu_lcd_sharp_first_line' `
    (Join-Path $gpu_dir '04_lcd_sharp_crop_4x.png') `
    (Join-Path $gpu_dir '04_lcd_sharp_text_diagnostics.json') `
    $gpu_lcd_first_line
if (-not $skip_chrome) {
    Save-FirstLineFromChromeClient `
        'chrome_first_line' `
        (Join-Path $chrome_dir 'chrome_reference_client.png') `
        (Join-Path $chrome_dir 'chrome_reference_diagnostics.json') `
        $chrome_first_line
}

$sharp_raw = Join-Path $stage_dir 'sharp_raw_mask.bmp'
$sharp_cache = Join-Path $stage_dir 'sharp_font_cache_mask.bmp'
$sharp_atlas = Join-Path $stage_dir 'sharp_atlas_upload_mask.bmp'
$sharp_cpu = Join-Path $stage_dir 'sharp_cpu_composite.bmp'
$lcd_raw = Join-Path $stage_dir 'lcd_sharp_raw_mask.bmp'
$lcd_cache = Join-Path $stage_dir 'lcd_sharp_font_cache_mask.bmp'
$lcd_atlas = Join-Path $stage_dir 'lcd_sharp_atlas_upload_mask.bmp'
$lcd_cpu = Join-Path $stage_dir 'lcd_sharp_cpu_composite.bmp'

$comparisons = @(
    [ordered]@{ stage = 'sharp_raw_vs_font_cache'; metrics = Compare-ReproImages $sharp_raw $sharp_cache },
    [ordered]@{ stage = 'sharp_font_cache_vs_atlas_upload'; metrics = Compare-ReproImages $sharp_cache $sharp_atlas },
    [ordered]@{ stage = 'sharp_cpu_vs_gpu'; metrics = Compare-ReproImages $sharp_cpu $gpu_sharp_first_line },
    [ordered]@{ stage = 'lcd_raw_vs_font_cache'; metrics = Compare-ReproImages $lcd_raw $lcd_cache },
    [ordered]@{ stage = 'lcd_font_cache_vs_atlas_upload'; metrics = Compare-ReproImages $lcd_cache $lcd_atlas },
    [ordered]@{ stage = 'lcd_cpu_vs_gpu'; metrics = Compare-ReproImages $lcd_cpu $gpu_lcd_first_line }
)
if (-not $skip_chrome) {
    $comparisons += @(
        [ordered]@{ stage = 'chrome_vs_lcd_cpu'; metrics = Compare-ReproImages $chrome_first_line $lcd_cpu },
        [ordered]@{ stage = 'chrome_vs_lcd_gpu'; metrics = Compare-ReproImages $chrome_first_line $gpu_lcd_first_line }
    )
}

$images = @(
    (Measure-ReproImage $sharp_raw),
    (Measure-ReproImage $sharp_cache),
    (Measure-ReproImage $sharp_atlas),
    (Measure-ReproImage $sharp_cpu),
    (Measure-ReproImage $gpu_sharp_first_line),
    (Measure-ReproImage $lcd_raw),
    (Measure-ReproImage $lcd_cache),
    (Measure-ReproImage $lcd_atlas),
    (Measure-ReproImage $lcd_cpu),
    (Measure-ReproImage $gpu_lcd_first_line)
)
if (-not $skip_chrome) {
    $images += (Measure-ReproImage $chrome_first_line)
}

$sheet_items = @(
    [pscustomobject]@{ label = 'sharp raw mask'; path = $sharp_raw },
    [pscustomobject]@{ label = 'sharp font-cache'; path = $sharp_cache },
    [pscustomobject]@{ label = 'sharp atlas upload'; path = $sharp_atlas },
    [pscustomobject]@{ label = 'sharp CPU'; path = $sharp_cpu },
    [pscustomobject]@{ label = 'sharp GPU'; path = $gpu_sharp_first_line },
    [pscustomobject]@{ label = 'LCD raw mask'; path = $lcd_raw },
    [pscustomobject]@{ label = 'LCD font-cache'; path = $lcd_cache },
    [pscustomobject]@{ label = 'LCD atlas upload'; path = $lcd_atlas },
    [pscustomobject]@{ label = 'LCD CPU'; path = $lcd_cpu },
    [pscustomobject]@{ label = 'LCD GPU'; path = $gpu_lcd_first_line }
)
if (-not $skip_chrome) {
    $sheet_items += [pscustomobject]@{ label = 'Chrome'; path = $chrome_first_line }
}
Save-ReproContactSheet $contact_path $sheet_items 5 620 21

$metrics = [ordered]@{
    images = $images
    comparisons = $comparisons
}
$summary = [ordered]@{
    status = 'ok'
    artifact_dir = $artifact_dir
    gpu_dir = $gpu_dir
    chrome_dir = $chrome_dir
    stage_dir = $stage_dir
    command_log = $command_log
    run_log = $run_log
    metrics_path = $metrics_path
    contact_sheet = $contact_path
    font_size = $font_size
    font_path = $font_path
    first_line = '#include "editor_render.h"'
    chrome_skipped = [bool]$skip_chrome
    comparisons = $comparisons
}

$metrics | ConvertTo-Json -Depth 12 | Set-Content -Path $metrics_path -Encoding utf8
$summary | ConvertTo-Json -Depth 12 | Set-Content -Path $summary_path -Encoding utf8
$summary | ConvertTo-Json -Depth 12
