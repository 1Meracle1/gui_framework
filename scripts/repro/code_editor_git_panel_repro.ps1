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
. (Join-Path $PSScriptRoot 'repro_common.ps1')

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

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$frame_dump = Join-Path $artifact_dir 'current_frame.json'
$launch_png = Join-Path $artifact_dir '01_launch.png'
$launch_frame = Join-Path $artifact_dir '01_launch_frame.json'
$git_png = Join-Path $artifact_dir '02_git_panel.png'
$git_crop_png = Join-Path $artifact_dir '03_git_panel_left_crop.png'
$git_frame = Join-Path $artifact_dir '02_git_panel_frame.json'
$branches_png = Join-Path $artifact_dir '04_git_panel_branches.png'
$branches_crop_png = Join-Path $artifact_dir '05_git_panel_branches_left_crop.png'
$branches_frame = Join-Path $artifact_dir '04_git_panel_branches_frame.json'
$stage_png = Join-Path $artifact_dir '06_git_panel_stage.png'
$stage_crop_png = Join-Path $artifact_dir '07_git_panel_stage_left_crop.png'
$stage_frame = Join-Path $artifact_dir '06_git_panel_stage_frame.json'
$unstage_png = Join-Path $artifact_dir '08_git_panel_unstage.png'
$unstage_crop_png = Join-Path $artifact_dir '09_git_panel_unstage_left_crop.png'
$unstage_frame = Join-Path $artifact_dir '08_git_panel_unstage_frame.json'
$spaces_png = Join-Path $artifact_dir '10_git_panel_spaces.png'
$spaces_crop_png = Join-Path $artifact_dir '11_git_panel_spaces_left_crop.png'
$spaces_frame = Join-Path $artifact_dir '10_git_panel_spaces_frame.json'
$message_png = Join-Path $artifact_dir '12_git_panel_message.png'
$message_crop_png = Join-Path $artifact_dir '13_git_panel_message_left_crop.png'
$message_frame = Join-Path $artifact_dir '12_git_panel_message_frame.json'
$graph_png = Join-Path $artifact_dir '14_git_panel_graph.png'
$graph_crop_png = Join-Path $artifact_dir '15_git_panel_graph_left_crop.png'
$graph_frame = Join-Path $artifact_dir '14_git_panel_graph_frame.json'
$branches_scroll_png = Join-Path $artifact_dir '16_git_panel_branches_scrolled.png'
$branches_scroll_crop_png = Join-Path $artifact_dir '17_git_panel_branches_scrolled_left_crop.png'
$branches_scroll_frame = Join-Path $artifact_dir '16_git_panel_branches_scrolled_frame.json'
$commit_popup_png = Join-Path $artifact_dir '18_git_panel_commit_popup.png'
$commit_popup_frame = Join-Path $artifact_dir '18_git_panel_commit_popup_frame.json'
$commit_popup_drag_png = Join-Path $artifact_dir '19_git_panel_commit_popup_drag.png'
$commit_popup_drag_frame = Join-Path $artifact_dir '19_git_panel_commit_popup_drag_frame.json'
$commit_popup_release_png = Join-Path $artifact_dir '20_git_panel_commit_popup_release.png'
$commit_popup_release_frame = Join-Path $artifact_dir '20_git_panel_commit_popup_release_frame.json'
$second_commit_popup_png = Join-Path $artifact_dir '21_git_panel_second_commit_popup.png'
$second_commit_popup_frame = Join-Path $artifact_dir '21_git_panel_second_commit_popup_frame.json'
$third_commit_popup_png = Join-Path $artifact_dir '22_git_panel_third_commit_popup.png'
$third_commit_popup_frame = Join-Path $artifact_dir '22_git_panel_third_commit_popup_frame.json'
$fourth_commit_popup_png = Join-Path $artifact_dir '23_git_panel_fourth_commit_popup.png'
$fourth_commit_popup_frame = Join-Path $artifact_dir '23_git_panel_fourth_commit_popup_frame.json'
Remove-Item -Force @(
    $launch_png, $launch_frame, $git_png, $git_crop_png, $git_frame,
    $branches_png, $branches_crop_png, $branches_frame,
    $stage_png, $stage_crop_png, $stage_frame,
    $unstage_png, $unstage_crop_png, $unstage_frame,
    $spaces_png, $spaces_crop_png, $spaces_frame,
    $message_png, $message_crop_png, $message_frame,
    $graph_png, $graph_crop_png, $graph_frame,
    $branches_scroll_png, $branches_scroll_crop_png, $branches_scroll_frame,
    $commit_popup_png, $commit_popup_frame,
    $commit_popup_drag_png, $commit_popup_drag_frame,
    $commit_popup_release_png, $commit_popup_release_frame,
    $second_commit_popup_png, $second_commit_popup_frame,
    $third_commit_popup_png, $third_commit_popup_frame,
    $fourth_commit_popup_png, $fourth_commit_popup_frame
) -ErrorAction SilentlyContinue

function Save-GitMilestone(
    [string]$phase,
    [string]$png_name,
    [string]$png_path,
    [string]$frame_name,
    [string]$frame_path,
    [string]$expected_debug_name,
    [string]$crop_name = '',
    [string]$crop_path = ''
) {
    Set-ReproPhase $phase
    Save-ReproScreenshot $png_name $png_path
    if ($crop_path -ne '') {
        Save-ReproLeftCrop $crop_name $png_path $crop_path
    }
    Copy-ReproFrameDump $frame_name $frame_path
    Assert-ReproBoxVisible $expected_debug_name $frame_path | Out-Null
}

function Click-GitText([string]$text, [string]$frame_path, [int]$x_offset = 8) {
    $box = Assert-ReproFrameTextContains $text $frame_path
    $x = [int]([double]$box.rect.min_x + $x_offset)
    $y = [int](([double]$box.rect.min_y + [double]$box.rect.max_y) * 0.5)
    Send-ReproClick $x $y
    return $box
}

function Hover-GitCommit([string]$frame_path, [int]$index) {
    $frame = Read-ReproFrame $frame_path
    $commits = @($frame.boxes | Where-Object { $_.debug_name -eq 'git_commit_graph' } |
        Sort-Object { [double]$_.rect.min_y })
    if ($commits.Count -le $index) {
        throw "Expected Git commit graph row not found: $index"
    }
    $box = $commits[$index]
    $x = [int]([double]$box.rect.max_x + 70.0)
    $y = [int](([double]$box.rect.min_y + [double]$box.rect.max_y) * 0.5)
    Send-ReproMouseMove $x $y $false
    return $box
}

function Assert-GitPopupOverlaysSearch([string]$frame_path, [string]$label) {
    $popup = Assert-ReproBoxVisible 'git_commit_popup' $frame_path
    $search = Assert-ReproFrameTextContains 'Search commits' $frame_path
    $height = [double]$popup.rect.max_y - [double]$popup.rect.min_y
    if ($height -lt 64.0) {
        throw "Expected $label popup to have usable height, got $height"
    }
    $overlaps = [double]$popup.rect.min_y -lt [double]$search.rect.max_y -and
        [double]$popup.rect.max_y -gt [double]$search.rect.min_y
    if (!$overlaps) {
        throw "Expected $label popup to overlay Search commits"
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
        -window_height 900

    Start-Sleep -Milliseconds 1200
    Save-GitMilestone 'launch' 'launch_png' $launch_png 'launch_frame' $launch_frame 'editor_surface'

    Send-ReproKey 0x20
    Send-ReproChar 'g'
    Start-Sleep -Milliseconds 1800
    Save-GitMilestone `
        'git_panel' `
        'git_png' `
        $git_png `
        'git_frame' `
        $git_frame `
        'filesystem_surface' `
        'git_crop_png' `
        $git_crop_png
    Assert-ReproFrameTextContains 'Changes' $git_frame | Out-Null
    Assert-ReproFrameTextContains 'Graph' $git_frame | Out-Null

    if ($commit_popup_checks) {
        Click-GitText 'Graph' $git_frame | Out-Null
        Start-Sleep -Milliseconds 2600
        Save-GitMilestone `
            'graph_expanded' `
            'graph_png' `
            $graph_png `
            'graph_frame' `
            $graph_frame `
            'filesystem_surface' `
            'graph_crop_png' `
            $graph_crop_png

        Hover-GitCommit $graph_frame 0 | Out-Null
        Start-Sleep -Milliseconds 900
        Save-GitMilestone `
            'commit_popup' `
            'commit_popup_png' `
            $commit_popup_png `
            'commit_popup_frame' `
            $commit_popup_frame `
            'git_commit_popup'
        Assert-GitPopupOverlaysSearch $commit_popup_frame 'first commit'

        $popup = Assert-ReproBoxVisible 'git_commit_popup' $commit_popup_frame
        $popup_x = [int](([double]$popup.rect.min_x + [double]$popup.rect.max_x) * 0.5)
        $popup_y1 = [int]([double]$popup.rect.min_y + 20.0)
        $popup_y2 = [int]([double]$popup.rect.max_y - 20.0)
        Send-ReproMouseMove $popup_x $popup_y1 $false
        Start-Sleep -Milliseconds 120
        Send-ReproMouseDown $popup_x $popup_y1
        Start-Sleep -Milliseconds 120
        Send-ReproMouseMove $popup_x $popup_y2 $true
        Start-Sleep -Milliseconds 320
        Save-GitMilestone `
            'commit_popup_drag' `
            'commit_popup_drag_png' `
            $commit_popup_drag_png `
            'commit_popup_drag_frame' `
            $commit_popup_drag_frame `
            'git_commit_popup'
        Send-ReproMouseUp 526 274
        Start-Sleep -Milliseconds 500
        Save-GitMilestone `
            'commit_popup_drag_release' `
            'commit_popup_release_png' `
            $commit_popup_release_png `
            'commit_popup_release_frame' `
            $commit_popup_release_frame `
            'filesystem_surface'
        Hover-GitCommit $commit_popup_release_frame 1 | Out-Null
        Start-Sleep -Milliseconds 900
        Save-GitMilestone `
            'second_commit_popup' `
            'second_commit_popup_png' `
            $second_commit_popup_png `
            'second_commit_popup_frame' `
            $second_commit_popup_frame `
            'git_commit_popup'
        Assert-GitPopupOverlaysSearch $second_commit_popup_frame 'second commit'

        Hover-GitCommit $graph_frame 2 | Out-Null
        Start-Sleep -Milliseconds 900
        Save-GitMilestone `
            'third_commit_popup' `
            'third_commit_popup_png' `
            $third_commit_popup_png `
            'third_commit_popup_frame' `
            $third_commit_popup_frame `
            'git_commit_popup'
        Assert-GitPopupOverlaysSearch $third_commit_popup_frame 'third commit'

        Hover-GitCommit $graph_frame 3 | Out-Null
        Start-Sleep -Milliseconds 900
        Save-GitMilestone `
            'fourth_commit_popup' `
            'fourth_commit_popup_png' `
            $fourth_commit_popup_png `
            'fourth_commit_popup_frame' `
            $fourth_commit_popup_frame `
            'git_commit_popup'
        Assert-GitPopupOverlaysSearch $fourth_commit_popup_frame 'fourth commit'

        $status = if (Test-ReproExited) { 'exited' } else { 'survived' }
        Set-ReproStatus $status
        return
    }

    if ($branch_scroll_checks) {
        Click-GitText 'Graph' $git_frame | Out-Null
        Start-Sleep -Milliseconds 2200
        Save-GitMilestone `
            'graph' `
            'graph_png' `
            $graph_png `
            'graph_frame' `
            $graph_frame `
            'filesystem_surface' `
            'graph_crop_png' `
            $graph_crop_png

        Send-ReproClick 211 80
        Start-Sleep -Milliseconds 800
        Save-GitMilestone `
            'branches' `
            'branches_png' `
            $branches_png `
            'branches_frame' `
            $branches_frame `
            'filesystem_surface' `
            'branches_crop_png' `
            $branches_crop_png

        Send-ReproWheel 120 154 $branch_scroll_notches
        Start-Sleep -Milliseconds 800
        Save-GitMilestone `
            'branches_scrolled' `
            'branches_scroll_png' `
            $branches_scroll_png `
            'branches_scroll_frame' `
            $branches_scroll_frame `
            'filesystem_surface' `
            'branches_scroll_crop_png' `
            $branches_scroll_crop_png
        $status = if (Test-ReproExited) { 'exited' } else { 'survived' }
        Set-ReproStatus $status
        return
    }

    Send-ReproClick 128 88
    Start-Sleep -Milliseconds 800
    Save-GitMilestone `
        'branches' `
        'branches_png' `
        $branches_png `
        'branches_frame' `
        $branches_frame `
        'filesystem_surface' `
        'branches_crop_png' `
        $branches_crop_png
    Send-ReproClick 128 88
    Start-Sleep -Milliseconds 400

    if ($commit_button_checks) {
        Send-ReproClick 120 110
        Start-Sleep -Milliseconds 400
        Save-GitMilestone `
            'message_focus' `
            'stage_png' `
            $stage_png `
            'stage_frame' `
            $stage_frame `
            'filesystem_surface' `
            'stage_crop_png' `
            $stage_crop_png

        Send-ReproText "   "
        Start-Sleep -Milliseconds 800
        Save-GitMilestone `
            'spaces_message' `
            'spaces_png' `
            $spaces_png `
            'spaces_frame' `
            $spaces_frame `
            'filesystem_surface' `
            'spaces_crop_png' `
            $spaces_crop_png

        Send-ReproText "commit button ready"
        Start-Sleep -Milliseconds 800
        Save-GitMilestone `
            'valid_message' `
            'message_png' `
            $message_png `
            'message_frame' `
            $message_frame `
            'filesystem_surface' `
            'message_crop_png' `
            $message_crop_png
        Assert-ReproFrameTextContains 'Commit' $message_frame | Out-Null
    } else {
        Send-ReproChar 's'
        Start-Sleep -Milliseconds 1800
        Save-GitMilestone `
            'stage' `
            'stage_png' `
            $stage_png `
            'stage_frame' `
            $stage_frame `
            'filesystem_surface' `
            'stage_crop_png' `
            $stage_crop_png

        Send-ReproChar 'u'
        Start-Sleep -Milliseconds 1800
        Save-GitMilestone `
            'unstage' `
            'unstage_png' `
            $unstage_png `
            'unstage_frame' `
            $unstage_frame `
            'filesystem_surface' `
            'unstage_crop_png' `
            $unstage_crop_png
    }

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
