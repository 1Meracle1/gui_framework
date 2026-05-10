param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$open_file = 'examples\code_editor\app.cpp',
    [int]$lsp_wait_ms = 8000,
    [int]$j_count = 150,
    [int]$capture_from = 35,
    [int]$capture_to = 120
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
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\code_editor_scroll_repro'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
Get-ChildItem -Path $artifact_dir -File -ErrorAction SilentlyContinue | Remove-Item -Force

$frame_dump = Join-Path $artifact_dir 'current_frame.json'

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
    Save-ReproScreenshot 'launch_png' (Join-Path $artifact_dir '000_launch.png')
    Copy-ReproFrameDump 'launch_frame' (Join-Path $artifact_dir '000_launch_frame.json')
    Assert-ReproBoxVisible 'editor_surface' | Out-Null

    Send-ReproKey 0x20
    Send-ReproChar 'f'
    Start-Sleep -Milliseconds 800
    Send-ReproText $open_file 20
    Start-Sleep -Milliseconds 500
    Send-ReproKey 0x0D
    Start-Sleep -Milliseconds 2500
    Set-ReproPhase 'file_open'
    Save-ReproScreenshot 'file_png' (Join-Path $artifact_dir '001_file_open.png')
    Copy-ReproFrameDump 'file_frame' (Join-Path $artifact_dir '001_file_open_frame.json')
    Assert-ReproBoxVisible 'editor_surface' | Out-Null

    Start-Sleep -Milliseconds $lsp_wait_ms
    Set-ReproPhase 'lsp_ready_waited'
    Save-ReproScreenshot 'lsp_png' (Join-Path $artifact_dir '002_lsp_wait.png')
    Copy-ReproFrameDump 'lsp_frame' (Join-Path $artifact_dir '002_lsp_wait_frame.json')

    for ($index = 1; $index -le $j_count; $index += 1) {
        if (Test-ReproExited) {
            Set-ReproStatus 'crashed'
            Set-ReproPhase "j_$index"
            break
        }
        Send-ReproChar 'j'
        Start-Sleep -Milliseconds 35
        if ($index -ge $capture_from -and $index -le $capture_to) {
            $name = '{0:D3}_j_{1:D3}' -f ($index + 2), $index
            Save-ReproScreenshot "${name}_png" (Join-Path $artifact_dir "$name.png")
            Copy-ReproFrameDump "${name}_frame" (Join-Path $artifact_dir "$name`_frame.json")
        }
    }

    if (-not (Test-ReproExited)) {
        Set-ReproStatus 'survived'
        Set-ReproPhase 'scroll_done'
        Save-ReproScreenshot 'final_png' (Join-Path $artifact_dir '999_final.png')
        Copy-ReproFrameDump 'final_frame' (Join-Path $artifact_dir '999_final_frame.json')
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
