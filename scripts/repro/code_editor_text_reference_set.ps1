param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [ValidateSet('dwrite', 'freetype')]
    [string]$font_backend = 'dwrite',
    [int[]]$font_sizes = @(12, 13, 14),
    [int]$settle_ms = 1200,
    [string]$chrome_path = ''
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
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\visual_reference_text'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$command_log = Join-Path $artifact_dir 'commands.log'
$run_log = Join-Path $artifact_dir 'run_output.log'
$summary_path = Join-Path $artifact_dir 'summary.json'
$metrics_path = Join-Path $artifact_dir 'metrics.json'
$diagnostics_path = Join-Path $artifact_dir 'normalized_comparison_diagnostics.json'
$contact_path = Join-Path $artifact_dir 'text_contact_sheet.png'
Remove-Item -Force $command_log, $run_log, $summary_path, $metrics_path, $diagnostics_path, $contact_path -ErrorAction SilentlyContinue

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

function Read-JsonFile([string]$path) {
    return (Get-Content -Raw $path) | ConvertFrom-Json
}

function Round-Delta([double]$lhs, [double]$rhs) {
    return [Math]::Round($lhs - $rhs, 4)
}

function Convert-DiagnosticColor([object]$color) {
    if ($color -ne $null -and $color.hex -ne $null) {
        return ([string]$color.hex).ToLowerInvariant()
    }
    $text = [string]$color
    $match = [regex]::Match($text, 'rgba?\((\d+),\s*(\d+),\s*(\d+)')
    if (-not $match.Success) {
        return $text.ToLowerInvariant()
    }
    return ('#{0:x2}{1:x2}{2:x2}' -f [int]$match.Groups[1].Value, [int]$match.Groups[2].Value, [int]$match.Groups[3].Value)
}

function Compare-DiagnosticColors([object]$editor, [object]$chrome) {
    $keys = @('background', 'text', 'keyword', 'type', 'string', 'number', 'comment', 'preprocessor', 'punctuation')
    $mismatches = @()
    foreach ($key in $keys) {
        $editor_color = Convert-DiagnosticColor $editor.colors.$key
        $chrome_color = Convert-DiagnosticColor $chrome.colors.$key
        if ($editor_color -ne $chrome_color) {
            $mismatches += [ordered]@{
                key = $key
                editor = $editor_color
                chrome = $chrome_color
            }
        }
    }
    return [ordered]@{
        match = ($mismatches.Count -eq 0)
        mismatches = $mismatches
    }
}

function Compare-TextDiagnostics([int]$font_size, [object]$editor, [object]$chrome) {
    $editor_lines = @($editor.lines | ForEach-Object { $_.text })
    $chrome_lines = @($chrome.lines | ForEach-Object { $_.text })
    $line_limit = [Math]::Min($editor_lines.Count, $chrome_lines.Count)
    $content_matches = $true
    for ($index = 0; $index -lt $line_limit; ++$index) {
        if ($editor_lines[$index] -ne $chrome_lines[$index]) {
            $content_matches = $false
        }
    }
    $colors = Compare-DiagnosticColors $editor $chrome
    return [ordered]@{
        font_size = $font_size
        editor_diagnostics = $editor.path
        chrome_diagnostics = $chrome.path
        font_size_delta = Round-Delta ([double]$editor.font_size) ([double]$chrome.font_size)
        line_height_delta = Round-Delta ([double]$editor.line_height) ([double]$chrome.line_height)
        crop_width_match = ([int]$editor.crop.width -eq [int]$chrome.crop.width)
        crop_height_match = ([int]$editor.crop.height -eq [int]$chrome.crop.height)
        text_origin_relative_x_delta = Round-Delta ([double]$editor.text_origin.relative_crop_x) ([double]$chrome.text_origin.relative_crop_x)
        text_origin_relative_y_delta = Round-Delta ([double]$editor.text_origin.relative_crop_y) ([double]$chrome.text_origin.relative_crop_y)
        first_line_text_matches = ($editor_lines.Count -ne 0 -and $chrome_lines.Count -ne 0 -and $editor_lines[0] -eq $chrome_lines[0])
        dumped_lines_match = ($content_matches -and $editor_lines.Count -eq $chrome_lines.Count)
        colors_match = $colors.match
        color_mismatches = $colors.mismatches
        editor_contamination = $editor.contamination
        chrome_contamination = $chrome.contamination
        dpi = [ordered]@{
            editor_window = $editor.window_diagnostics.dpi.window
            chrome_window = $chrome.window_diagnostics.dpi.window
            editor_scale = $editor.window_diagnostics.dpi.scale
            chrome_device_pixel_ratio = $chrome.device_pixel_ratio
        }
    }
}

$text_script = Join-Path $PSScriptRoot 'code_editor_text_rendering_repro.ps1'
$chrome_script = Join-Path $PSScriptRoot 'code_editor_chrome_text_reference.ps1'
$policies = @(
    @{ name = 'sharp'; path = '01_sharp_crop_4x.png' },
    @{ name = 'smooth'; path = '02_smooth_crop_4x.png' },
    @{ name = 'lcd_smooth'; path = '03_lcd_smooth_crop_4x.png' },
    @{ name = 'lcd_sharp'; path = '04_lcd_sharp_crop_4x.png' }
)

$captures = @()
$image_metrics = @()
$comparisons = @()
$diagnostic_comparisons = @()
$sheet_items = @()

foreach ($font_size in $font_sizes) {
    $app_dir = Join-Path $artifact_dir "code_editor_${font_backend}_${font_size}px"
    $chrome_dir = Join-Path $artifact_dir "chrome_${font_size}px"

    $app_args = @(
        '-workspace_path', $workspace_path,
        '-exe_path', $exe_path,
        '-artifact_dir', $app_dir,
        '-file_path', $file_path,
        '-font_backend', $font_backend,
        '-font_size', [string]$font_size,
        '-settle_ms', [string]$settle_ms
    )
    Invoke-LoggedScript $text_script $app_args

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

    $chrome_crop = Join-Path $chrome_dir 'chrome_reference_crop_4x.png'
    $chrome_diag_path = Join-Path $chrome_dir 'chrome_reference_diagnostics.json'
    $lcd_sharp_diag_path = Join-Path $app_dir '04_lcd_sharp_text_diagnostics.json'
    $editor_diag = Read-JsonFile $lcd_sharp_diag_path
    $chrome_diag = Read-JsonFile $chrome_diag_path
    $editor_diag | Add-Member -NotePropertyName path -NotePropertyValue $lcd_sharp_diag_path -Force
    $chrome_diag | Add-Member -NotePropertyName path -NotePropertyValue $chrome_diag_path -Force
    $diagnostic_comparisons += Compare-TextDiagnostics $font_size $editor_diag $chrome_diag
    $sheet_items += [pscustomobject]@{ label = "${font_size}px chrome"; path = $chrome_crop }
    $image_metrics += Measure-ReproImage $chrome_crop

    foreach ($policy in $policies) {
        $crop = Join-Path $app_dir $policy['path']
        $sheet_items += [pscustomobject]@{ label = "${font_size}px $($policy['name'])"; path = $crop }
        $image_metrics += Measure-ReproImage $crop
        $comparisons += [ordered]@{
            font_size = $font_size
            candidate = $policy['name']
            metrics = Compare-ReproImages $chrome_crop $crop
        }
    }

    $captures += [ordered]@{
        font_size = $font_size
        code_editor_dir = $app_dir
        chrome_dir = $chrome_dir
        chrome_crop = $chrome_crop
        chrome_diagnostics = $chrome_diag_path
        lcd_sharp_crop = Join-Path $app_dir '04_lcd_sharp_crop_4x.png'
        lcd_sharp_diagnostics = $lcd_sharp_diag_path
    }
}

Save-ReproContactSheet $contact_path $sheet_items 5 620 190

$summary = [ordered]@{
    status = 'ok'
    artifact_dir = $artifact_dir
    command_log = $command_log
    run_log = $run_log
    contact_sheet = $contact_path
    metrics_path = $metrics_path
    diagnostics_path = $diagnostics_path
    font_backend = $font_backend
    font_sizes = $font_sizes
    captures = $captures
    comparisons = $comparisons
    diagnostic_comparisons = $diagnostic_comparisons
}

$metrics = [ordered]@{
    images = $image_metrics
    comparisons = $comparisons
}

$diagnostics = [ordered]@{
    artifact_dir = $artifact_dir
    font_backend = $font_backend
    font_sizes = $font_sizes
    comparisons = $diagnostic_comparisons
    exit_criteria = [ordered]@{
        same_crop_size = -not @($diagnostic_comparisons | Where-Object { -not $_.crop_width_match -or -not $_.crop_height_match }).Count
        same_text_origin = -not @($diagnostic_comparisons | Where-Object { $_.text_origin_relative_x_delta -ne 0.0 -or $_.text_origin_relative_y_delta -ne 0.0 }).Count
        same_line_height = -not @($diagnostic_comparisons | Where-Object { $_.line_height_delta -ne 0.0 }).Count
        same_first_line_text = -not @($diagnostic_comparisons | Where-Object { -not $_.first_line_text_matches }).Count
        same_colors = -not @($diagnostic_comparisons | Where-Object { -not $_.colors_match }).Count
        contamination_removed = -not @($diagnostic_comparisons | Where-Object {
            $_.editor_contamination.current_line_visible -or
            $_.editor_contamination.caret_visible -or
            $_.editor_contamination.selection_active -or
            $_.editor_contamination.line_numbers_in_crop -or
            $_.chrome_contamination.current_line_visible -or
            $_.chrome_contamination.caret_visible -or
            $_.chrome_contamination.selection_active -or
            $_.chrome_contamination.line_numbers_in_crop
        }).Count
    }
}

$metrics | ConvertTo-Json -Depth 8 | Set-Content -Path $metrics_path -Encoding utf8
$diagnostics | ConvertTo-Json -Depth 16 | Set-Content -Path $diagnostics_path -Encoding utf8
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summary_path -Encoding utf8
$summary | ConvertTo-Json -Depth 8
