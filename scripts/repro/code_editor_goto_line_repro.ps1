param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$open_file = 'examples\code_editor\editor_model.cpp',
    [int]$target_line = 150
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
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_goto_line_repro'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
Get-ChildItem -Path $artifact_dir -File -ErrorAction SilentlyContinue | Remove-Item -Force

$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$text_diagnostics = Join-Path $artifact_dir 'current_text_diagnostics.json'
$local_app_data = Join-Path $artifact_dir 'local_app_data'
$config_dir = Join-Path $local_app_data 'gui_framework\code_editor'
$config_path = Join-Path $config_dir 'config.toml'
$initial_file_path =
    if ([System.IO.Path]::IsPathRooted($open_file)) { $open_file } else { Join-Path $workspace_path $open_file }

function Send-ReproNormalGotoLine([int]$line) {
    Send-ReproText ([string]$line) 60
    Send-ReproChar 'G'
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
    return $diagnostics
}

function Assert-ReproLineVisible([object]$diagnostics, [int]$line) {
    $target = [int64]($line - 1)
    $first = [int64]$diagnostics.visible_lines.first_line
    $count = [int64]$diagnostics.visible_lines.viewport_count
    $last = $first + $count - 1
    $script:repro_summary['target_line'] = $line
    $script:repro_summary['visible_first_line'] = $first + 1
    $script:repro_summary['visible_last_line'] = $last + 1
    if ($target -lt $first -or $target -gt $last) {
        throw "Expected line $line to be visible after ${line}G; visible 1-based range is $($first + 1)..$($last + 1)."
    }
}

$previous_text_diagnostics = $env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH
$previous_local_app_data = $env:LOCALAPPDATA
try {
    $env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH = $text_diagnostics
    New-Item -ItemType Directory -Force -Path $config_dir | Out-Null
    [System.IO.File]::WriteAllText($config_path, "[language-servers.clangd]`nenabled = false`n")
    $env:LOCALAPPDATA = $local_app_data

    Start-ReproSession `
        -workspace_path $workspace_path `
        -exe_path $exe_path `
        -artifact_dir $artifact_dir `
        -arguments @('--automation-dump-frame', $frame_dump, $initial_file_path) `
        -frame_dump_path $frame_dump `
        -window_width 1500 `
        -window_height 980

    Add-ReproArtifact 'text_diagnostics_path' $text_diagnostics
    Add-ReproArtifact 'config_path' $config_path

    Start-Sleep -Milliseconds 1200
    Set-ReproPhase 'file_open'
    Save-ReproScreenshot 'file_png' (Join-Path $artifact_dir '01_file_open.png')
    Copy-ReproFrameDump 'file_frame' (Join-Path $artifact_dir '01_file_open_frame.json')
    Copy-CodeEditorTextDiagnostics 'file_text_diagnostics' (Join-Path $artifact_dir '01_file_text_diagnostics.json') | Out-Null
    Assert-ReproBoxVisible 'editor_surface' | Out-Null

    Send-ReproClick 120 95
    Start-Sleep -Milliseconds 400
    Send-ReproKey 0x1B
    Start-Sleep -Milliseconds 300
    Send-ReproNormalGotoLine $target_line
    Start-Sleep -Milliseconds 900
    Set-ReproPhase 'goto_line'
    Save-ReproScreenshot 'goto_png' (Join-Path $artifact_dir '02_goto_line.png')
    Copy-ReproFrameDump 'goto_frame' (Join-Path $artifact_dir '02_goto_line_frame.json')
    $goto_diagnostics = Copy-CodeEditorTextDiagnostics 'goto_text_diagnostics' (Join-Path $artifact_dir '02_goto_line_text_diagnostics.json')
    Assert-ReproLineVisible $goto_diagnostics $target_line

    Set-ReproStatus 'survived'
} catch {
    Set-ReproStatus 'script_error'
    if ($script:repro_summary -ne $null) {
        $script:repro_summary['error'] = ($_ | Out-String)
    } else {
        Write-Error ($_ | Out-String)
    }
} finally {
    if ($previous_text_diagnostics -eq $null) {
        Remove-Item Env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH -ErrorAction SilentlyContinue
    } else {
        $env:CODE_EDITOR_TEXT_DIAGNOSTICS_PATH = $previous_text_diagnostics
    }
    if ($previous_local_app_data -eq $null) {
        Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
    } else {
        $env:LOCALAPPDATA = $previous_local_app_data
    }
    Complete-ReproSession
}
