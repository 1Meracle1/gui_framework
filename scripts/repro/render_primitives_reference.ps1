param(
    [string]$workspace_path = '',
    [string]$exe_path = '',
    [string]$artifact_dir = '',
    [string]$chrome_path = '',
    [double]$time_seconds = 1.25,
    [int]$settle_ms = 1200,
    [int]$repeat_delay_ms = 250
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repro_common.ps1')

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($workspace_path -eq '') {
    $workspace_path = $repo_root
}
if ($exe_path -eq '') {
    $exe_path = Join-Path $repo_root 'build\windows-msvc-debug\Debug\render_effects_testbed.exe'
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root '_codex_artifacts\visual_reference_primitives'
}
if ($chrome_path -eq '') {
    $paths = @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
        "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
    )
    foreach ($path in $paths) {
        if ($path -and (Test-Path $path)) {
            $chrome_path = $path
            break
        }
    }
}
if ($chrome_path -eq '' -or -not (Test-Path $chrome_path)) {
    throw 'Google Chrome was not found.'
}

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$summary_path = Join-Path $artifact_dir 'summary.json'
$metrics_path = Join-Path $artifact_dir 'metrics.json'
$residuals_path = Join-Path $artifact_dir 'primitive_residuals.json'
$command_log = Join-Path $artifact_dir 'commands.log'
$contact_path = Join-Path $artifact_dir 'primitive_contact_sheet.png'
$chrome_html = Join-Path $artifact_dir 'chrome_canvas_reference.html'
$chrome_png = Join-Path $artifact_dir 'chrome_canvas_reference.png'
$chrome_contact_path = Join-Path $artifact_dir 'chrome_canvas_contact_sheet.png'
$comparison_contact_path = Join-Path $artifact_dir 'primitive_comparison_contact_sheet.png'
$first_png = Join-Path $artifact_dir 'render_effects_fixed_time_01.png'
$second_png = Join-Path $artifact_dir 'render_effects_fixed_time_02.png'
Remove-Item -Force $summary_path, $metrics_path, $residuals_path, $command_log, $contact_path, $chrome_html, $chrome_png, $chrome_contact_path, $comparison_contact_path, $first_png, $second_png -ErrorAction SilentlyContinue

$arguments = @("--automation-time-seconds=$time_seconds")
$command = (Quote-ReproArgument $exe_path) + ' ' + (Join-ReproArguments $arguments)
Set-Content -Path $command_log -Value @(
    $command
    ((Quote-ReproArgument $chrome_path) + ' --headless=new --disable-gpu --force-device-scale-factor=1 --hide-scrollbars --run-all-compositor-stages-before-draw --window-size=1304,741 --screenshot=' + (Quote-ReproArgument $chrome_png) + ' ' + (Quote-ReproArgument ([System.Uri]::new($chrome_html).AbsoluteUri)))
) -Encoding utf8

$tiles = @(
    @{ name = 'alpha_overlap'; x = 40; y = 36; width = 260; height = 248; primitive_type = 'analytic rounded rectangles'; render_path = 'root analytic primitive source-over' },
    @{ name = 'group_opacity'; x = 340; y = 36; width = 260; height = 248; primitive_type = 'analytic rounded rectangles'; render_path = 'offscreen layer opacity' },
    @{ name = 'rounded_border'; x = 640; y = 36; width = 260; height = 248; primitive_type = 'analytic rounded rect border'; render_path = 'styled rect analytic coverage' },
    @{ name = 'box_shadow'; x = 940; y = 36; width = 260; height = 248; primitive_type = 'styled rounded rect shadow'; render_path = 'styled rect shadow and border' },
    @{ name = 'blur_layer'; x = 40; y = 340; width = 260; height = 248; primitive_type = 'offscreen blur layer'; render_path = 'layer filter blur with analytic primitives' },
    @{ name = 'drop_shadow'; x = 340; y = 340; width = 260; height = 248; primitive_type = 'drop shadow mixed primitives'; render_path = 'drop-shadow layer plus triangle/path primitives' },
    @{ name = 'general_fill_path'; x = 396; y = 460; width = 100; height = 74; primitive_type = 'concave fill path'; render_path = 'CPU triangulated path inside drop-shadow layer' },
    @{ name = 'small_curve_mask'; x = 520; y = 526; width = 52; height = 44; primitive_type = 'coverage-mask curved path'; render_path = 'coverage mask atlas path fallback' },
    @{ name = 'clipped_layer'; x = 640; y = 340; width = 260; height = 248; primitive_type = 'rounded clip layer'; render_path = 'offscreen clipped layer and analytic border' },
    @{ name = 'blend_modes'; x = 940; y = 340; width = 260; height = 248; primitive_type = 'layer blend modes'; render_path = 'offscreen layer blend pipeline' }
)

function Save-ReproDifferenceImage([string]$reference, [string]$candidate, [string]$target) {
    $a = [System.Drawing.Bitmap]::FromFile($reference)
    $b = [System.Drawing.Bitmap]::FromFile($candidate)
    $width = [Math]::Min($a.Width, $b.Width)
    $height = [Math]::Min($a.Height, $b.Height)
    $diff = [System.Drawing.Bitmap]::new($width, $height)
    for ($y = 0; $y -lt $height; ++$y) {
        for ($x = 0; $x -lt $width; ++$x) {
            $pa = $a.GetPixel($x, $y)
            $pb = $b.GetPixel($x, $y)
            $r = [Math]::Min(255, [Math]::Abs($pa.R - $pb.R) * 4)
            $g = [Math]::Min(255, [Math]::Abs($pa.G - $pb.G) * 4)
            $blue = [Math]::Min(255, [Math]::Abs($pa.B - $pb.B) * 4)
            $diff.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $r, $g, $blue))
        }
    }
    $diff.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $diff.Dispose()
    $a.Dispose()
    $b.Dispose()
}

function Average-Metric([object[]]$items, [string]$field) {
    if ($items.Count -eq 0) {
        return 0.0
    }
    $sum = 0.0
    foreach ($item in $items) {
        $sum += [double]$item.metrics[$field]
    }
    return [Math]::Round($sum / [double]$items.Count, 4)
}

function New-ResidualSummary([object[]]$residuals, [string]$field) {
    $groups = @()
    $names = @($residuals | ForEach-Object { $_[$field] } | Sort-Object -Unique)
    foreach ($name in $names) {
        $items = @($residuals | Where-Object { $_[$field] -eq $name })
        $groups += [ordered]@{
            name = $name
            count = $items.Count
            mean_rgb_abs_sum = Average-Metric $items 'mean_rgb_abs_sum'
            rms_channel_delta = Average-Metric $items 'rms_channel_delta'
            changed_pct = Average-Metric $items 'changed_pct'
        }
    }
    return $groups
}

function Write-ChromeCanvasReference([string]$path, [double]$time_seconds) {
    $html = @"
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<style>
html, body { margin: 0; padding: 0; background: rgb(9, 11, 14); overflow: hidden; }
canvas { display: block; width: 1304px; height: 741px; }
</style>
</head>
<body>
<canvas id="scene" width="1304" height="741"></canvas>
<script>
const W = 1304;
const H = 741;
const TIME = $time_seconds;
const canvas = document.getElementById('scene');
const ctx = canvas.getContext('2d');

function c(r, g, b, a) {
  return 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' + Math.round(b * 255) + ',' + a + ')';
}
function wave(time, speed, phase) { return Math.sin((time * speed) + phase); }
function rect(x0, y0, x1, y1) { return { min: { x: x0, y: y0 }, max: { x: x1, y: y1 } }; }
function tileRect(column, row) {
  const x = 40 + column * 300;
  const y = 36 + row * 304;
  return rect(x, y, x + 260, y + 248);
}
function inset(r, amount) {
  return rect(r.min.x + amount, r.min.y + amount, r.max.x - amount, r.max.y - amount);
}
function offset(r, amount) {
  return rect(r.min.x + amount.x, r.min.y + amount.y, r.max.x + amount.x, r.max.y + amount.y);
}
function pathRoundedRect(g, r, radius) {
  const w = r.max.x - r.min.x;
  const h = r.max.y - r.min.y;
  g.beginPath();
  g.roundRect(r.min.x, r.min.y, w, h, Math.max(0, radius));
}
function fillRoundedRect(g, r, color, radius) {
  pathRoundedRect(g, r, radius);
  g.fillStyle = color;
  g.fill();
}
function strokeRoundedRect(g, r, color, thickness, radius) {
  pathRoundedRect(g, r, radius);
  g.strokeStyle = color;
  g.lineWidth = thickness;
  g.stroke();
}
function fillRoundedRectRing(g, outer, inner, color, outerRadius, innerRadius) {
  g.beginPath();
  g.roundRect(outer.min.x, outer.min.y, outer.max.x - outer.min.x, outer.max.y - outer.min.y, Math.max(0, outerRadius));
  g.roundRect(inner.min.x, inner.min.y, inner.max.x - inner.min.x, inner.max.y - inner.min.y, Math.max(0, innerRadius));
  g.fillStyle = color;
  g.fill('evenodd');
}
function drawStyledRectBody(g, r, style) {
  const borderThickness = Math.max(style.borderThickness || 0, 0);
  if (borderThickness > 0) {
    const inner = inset(r, borderThickness);
    fillRoundedRectRing(g, r, inner, style.border, style.radius, Math.max(style.radius - borderThickness, 0));
    if (inner.max.x > inner.min.x && inner.max.y > inner.min.y) {
      fillRoundedRect(g, inner, style.fill, Math.max(style.radius - borderThickness, 0));
    }
  } else {
    fillRoundedRect(g, r, style.fill, style.radius);
  }
}
function drawStyledRect(g, r, style) {
  g.save();
  if (style.shadow) {
    g.shadowOffsetX = style.shadow.offsetX;
    g.shadowOffsetY = style.shadow.offsetY;
    g.shadowBlur = style.shadow.blur;
    g.shadowColor = style.shadow.color;
    fillRoundedRect(g, r, style.fill, style.radius);
  }
  g.restore();
  drawStyledRectBody(g, r, style);
}
function drawTile(g, r) {
  drawStyledRect(g, r, {
    fill: c(0.075, 0.09, 0.105, 1.0),
    border: c(0.28, 0.34, 0.36, 1.0),
    borderThickness: 1,
    radius: 10
  });
}
function drawCircle(g, x, y, radius, color) {
  g.beginPath();
  g.arc(x, y, radius, 0, Math.PI * 2);
  g.fillStyle = color;
  g.fill();
}
function drawTriangle(g, a, b, cpt, color) {
  g.beginPath();
  g.moveTo(a.x, a.y);
  g.lineTo(b.x, b.y);
  g.lineTo(cpt.x, cpt.y);
  g.closePath();
  g.fillStyle = color;
  g.fill();
}
function drawConcaveFillPath(g, origin, color) {
  g.beginPath();
  g.moveTo(origin.x + 0, origin.y + 0);
  g.lineTo(origin.x + 78, origin.y + 0);
  g.lineTo(origin.x + 78, origin.y + 54);
  g.lineTo(origin.x + 39, origin.y + 30);
  g.lineTo(origin.x + 0, origin.y + 54);
  g.closePath();
  g.fillStyle = color;
  g.fill();
}
function drawSmallCurveMaskShape(g, origin, color) {
  g.beginPath();
  g.moveTo(origin.x + 0, origin.y + 18);
  g.quadraticCurveTo(origin.x + 12, origin.y - 6, origin.x + 24, origin.y + 18);
  g.lineTo(origin.x + 12, origin.y + 32);
  g.closePath();
  g.fillStyle = color;
  g.fill();
}
function layerCanvas() {
  const offscreen = document.createElement('canvas');
  offscreen.width = W;
  offscreen.height = H;
  return offscreen;
}
function drawAlphaOverlap(g, tile, time) {
  drawTile(g, tile);
  const r = inset(tile, 46);
  const redX = wave(time, 0.46, 0.0) * 10;
  const blueY = wave(time, 0.38, 1.8) * 8;
  const goldX = wave(time, 0.34, 3.4) * 7;
  const goldY = wave(time, 0.30, 2.6) * 5;
  fillRoundedRect(g, rect(r.min.x + redX, r.min.y, r.max.x - 34 + redX, r.max.y), c(0.95, 0.22, 0.18, 0.58), 18);
  fillRoundedRect(g, rect(r.min.x + 54, r.min.y + 34 + blueY, r.max.x, r.max.y - 22 + blueY), c(0.12, 0.62, 1.0, 0.58), 18);
  fillRoundedRect(g, rect(r.min.x + 96 + goldX, r.min.y + 72 + goldY, r.max.x - 22 + goldX, r.max.y + 10 + goldY), c(0.98, 0.74, 0.18, 0.58), 18);
}
function drawGroupOpacity(g, tile, time) {
  drawTile(g, tile);
  const layer = inset(tile, 34);
  const opacity = 0.56 + wave(time, 0.28, 1.2) * 0.08;
  const slide = wave(time, 0.36, 0.6) * 8;
  const off = layerCanvas();
  const o = off.getContext('2d');
  fillRoundedRect(o, rect(layer.min.x + 12 + slide, layer.min.y + 16, layer.min.x + 136 + slide, layer.max.y - 16), c(0.95, 0.22, 0.18, 1.0), 20);
  fillRoundedRect(o, rect(layer.min.x + 74 - slide, layer.min.y + 44, layer.max.x - 10 - slide, layer.max.y - 8), c(0.12, 0.62, 1.0, 1.0), 20);
  g.save();
  g.globalAlpha *= opacity;
  g.drawImage(off, 0, 0);
  g.restore();
}
function drawRoundedBorder(g, tile, time) {
  drawTile(g, tile);
  drawStyledRect(g, inset(tile, 46), {
    fill: c(0.12, 0.42, 0.50, 0.92),
    border: c(0.98, 0.80, 0.22, 1.0),
    borderThickness: 7 + wave(time, 0.34, 2.0),
    radius: 32 + wave(time, 0.30, 0.4) * 6
  });
}
function drawBoxShadow(g, tile, time) {
  drawTile(g, tile);
  const lift = wave(time, 0.38, 0.0) * 6;
  drawStyledRect(g, offset(inset(tile, 58), { x: 0, y: -lift }), {
    fill: c(0.92, 0.95, 0.98, 1.0),
    border: c(0.12, 0.18, 0.20, 0.55),
    borderThickness: 2,
    radius: 24,
    shadow: { offsetX: 18, offsetY: 20 - lift * 0.65, blur: 22, color: c(0.0, 0.0, 0.0, 0.44) }
  });
}
function drawBlur(g, tile, time) {
  drawTile(g, tile);
  const layer = inset(tile, 38);
  const sweep = wave(time, 0.32, 0.9) * 10;
  const off = layerCanvas();
  const o = off.getContext('2d');
  fillRoundedRect(o, offset(inset(layer, 34), { x: sweep, y: 0 }), c(0.20, 0.76, 1.0, 0.92), 8);
  drawCircle(o, layer.min.x + 134 - sweep, layer.min.y + 92, 56, c(0.98, 0.32, 0.52, 0.9));
  fillRoundedRect(o, rect(layer.min.x + 86, layer.min.y + 112 + sweep * 0.35, layer.max.x - 22, layer.max.y - 18 + sweep * 0.35), c(0.98, 0.76, 0.22, 0.86), 12);
  g.save();
  g.filter = 'blur(7px)';
  g.drawImage(off, 0, 0);
  g.restore();
}
function drawDropShadow(g, tile, time) {
  drawTile(g, tile);
  const layer = inset(tile, 48);
  const drift = wave(time, 0.34, 2.4) * 6;
  const off = layerCanvas();
  const o = off.getContext('2d');
  drawCircle(o, layer.min.x + 74 + drift, layer.min.y + 70, 48, c(0.28, 0.92, 0.55, 0.95));
  drawTriangle(o, { x: layer.min.x + 124 - drift, y: layer.min.y + 38 }, { x: layer.max.x - 18 - drift, y: layer.min.y + 120 }, { x: layer.min.x + 104 - drift, y: layer.max.y - 16 }, c(0.22, 0.48, 0.96, 0.95));
  drawConcaveFillPath(o, { x: layer.min.x + 18, y: layer.max.y - 70 }, c(0.98, 0.78, 0.20, 0.92));
  g.save();
  g.shadowOffsetX = 18 + drift;
  g.shadowOffsetY = 16;
  g.shadowBlur = 9;
  g.shadowColor = c(0.0, 0.0, 0.0, 0.55);
  g.drawImage(off, 0, 0);
  g.restore();
  g.drawImage(off, 0, 0);
  for (let index = 0; index < 4; ++index) {
    const x = tile.min.x + 38 + index * 28;
    drawCircle(g, x, tile.max.y - 24, 8, c(0.92, 0.96, 1.0, 0.86));
  }
  drawSmallCurveMaskShape(g, { x: tile.max.x - 66, y: tile.max.y - 42 }, c(0.98, 0.46, 0.68, 0.88));
}
function drawClippedLayer(g, tile, time) {
  drawTile(g, tile);
  const layer = inset(tile, 38);
  const slide = wave(time, 0.26, 0.2) * 16;
  g.save();
  pathRoundedRect(g, layer, 36);
  g.clip();
  fillRoundedRect(g, layer, c(0.95, 0.96, 0.90, 1.0), 0);
  fillRoundedRect(g, rect(layer.min.x - 34 + slide, layer.min.y + 24, layer.max.x + 34 + slide, layer.min.y + 74), c(0.92, 0.20, 0.24, 0.9), 0);
  fillRoundedRect(g, rect(layer.min.x - 30 - slide, layer.min.y + 94, layer.max.x + 48 - slide, layer.min.y + 142), c(0.12, 0.62, 1.0, 0.9), 0);
  drawCircle(g, layer.max.x - 22 + slide * 0.2, layer.max.y - 16, 54, c(0.98, 0.78, 0.20, 0.88));
  g.restore();
  strokeRoundedRect(g, layer, c(0.92, 0.95, 0.98, 0.9), 2, 36);
}
function drawBlendSwatch(g, r, blendMode, color, time, phase) {
  fillRoundedRect(g, r, c(0.02, 0.04, 0.055, 1.0), 7);
  fillRoundedRect(g, inset(r, 12), c(0.14, 0.50, 0.95, 1.0), 5);
  drawCircle(g, r.min.x + 56, r.min.y + 72, 38, c(0.98, 0.78, 0.18, 1.0));
  g.save();
  g.globalCompositeOperation = blendMode;
  const drift = wave(time, 0.30, phase) * 7;
  fillRoundedRect(g, offset(inset(r, 30), { x: 16 + drift, y: 14 }), color, 8);
  g.restore();
}
function drawBlendModes(g, tile, time) {
  drawTile(g, tile);
  const area = inset(tile, 28);
  const normal = rect(area.min.x, area.min.y, area.min.x + 92, area.min.y + 86);
  drawBlendSwatch(g, normal, 'source-over', c(0.94, 0.20, 0.48, 0.78), time, 0.0);
  drawBlendSwatch(g, offset(normal, { x: 110, y: 0 }), 'lighter', c(0.94, 0.20, 0.48, 0.78), time, 1.1);
  drawBlendSwatch(g, offset(normal, { x: 0, y: 104 }), 'multiply', c(0.94, 0.20, 0.48, 0.78), time, 2.2);
  drawBlendSwatch(g, offset(normal, { x: 110, y: 104 }), 'screen', c(0.94, 0.20, 0.48, 0.78), time, 3.3);
}

ctx.fillStyle = c(0.035, 0.045, 0.055, 1.0);
ctx.fillRect(0, 0, W, H);
drawAlphaOverlap(ctx, tileRect(0, 0), TIME);
drawGroupOpacity(ctx, tileRect(1, 0), TIME);
drawRoundedBorder(ctx, tileRect(2, 0), TIME);
drawBoxShadow(ctx, tileRect(3, 0), TIME);
drawBlur(ctx, tileRect(0, 1), TIME);
drawDropShadow(ctx, tileRect(1, 1), TIME);
drawClippedLayer(ctx, tileRect(2, 1), TIME);
drawBlendModes(ctx, tileRect(3, 1), TIME);
</script>
</body>
</html>
"@
    Set-Content -Path $path -Value $html -Encoding utf8
}

function Save-ChromeCanvasReference([string]$html_path, [string]$png_path) {
    Write-ChromeCanvasReference $html_path $time_seconds
    $uri = [System.Uri]::new($html_path).AbsoluteUri
    $chrome_args = @(
        '--headless=new',
        '--disable-gpu',
        '--force-device-scale-factor=1',
        '--hide-scrollbars',
        '--run-all-compositor-stages-before-draw',
        '--window-size=1304,741',
        "--screenshot=$png_path",
        $uri
    )
    & $chrome_path @chrome_args | Out-Null
    if (-not (Test-Path $png_path)) {
        throw "Chrome primitive reference was not written: $png_path"
    }
}

try {
    Start-ReproSession `
        -workspace_path $workspace_path `
        -exe_path $exe_path `
        -artifact_dir $artifact_dir `
        -arguments $arguments `
        -window_width 1320 `
        -window_height 780

    Add-ReproArtifact 'time_seconds' ([string]$time_seconds)
    Start-Sleep -Milliseconds $settle_ms
    Save-ReproClientScreenshot 'render_effects_fixed_time_01' $first_png
    Start-Sleep -Milliseconds $repeat_delay_ms
    Save-ReproClientScreenshot 'render_effects_fixed_time_02' $second_png
    Save-ChromeCanvasReference $chrome_html $chrome_png

    $sheet_items = @()
    $chrome_sheet_items = @()
    $comparison_sheet_items = @()
    $image_metrics = @()
    $reference_metrics = @()
    $residuals = @()
    foreach ($tile in $tiles) {
        $x = [int]$tile['x']
        $y = [int]$tile['y']
        $width = [int]$tile['width']
        $height = [int]$tile['height']
        $crop_path = Join-Path $artifact_dir ("tile_$($tile['name']).png")
        $reference_crop_path = Join-Path $artifact_dir ("chrome_tile_$($tile['name']).png")
        $diff_path = Join-Path $artifact_dir ("diff_tile_$($tile['name']).png")
        Save-ReproImageCrop ($tile['name']) $first_png $crop_path $x $y $width $height
        Save-ReproImageCrop "chrome_$($tile['name'])" $chrome_png $reference_crop_path $x $y $width $height
        Save-ReproDifferenceImage $reference_crop_path $crop_path $diff_path
        $sheet_items += [pscustomobject]@{ label = $tile['name']; path = $crop_path }
        $chrome_sheet_items += [pscustomobject]@{ label = $tile['name']; path = $reference_crop_path }
        $comparison_sheet_items += [pscustomobject]@{ label = "$($tile['name']) app"; path = $crop_path }
        $comparison_sheet_items += [pscustomobject]@{ label = "$($tile['name']) chrome"; path = $reference_crop_path }
        $comparison_sheet_items += [pscustomobject]@{ label = "$($tile['name']) diff"; path = $diff_path }
        $image_metrics += Measure-ReproImage $crop_path
        $reference_metrics += Measure-ReproImage $reference_crop_path
        $comparison = Compare-ReproImages $reference_crop_path $crop_path
        $residuals += [ordered]@{
            name = $tile['name']
            primitive_type = $tile['primitive_type']
            render_path = $tile['render_path']
            reference_path = $reference_crop_path
            candidate_path = $crop_path
            diff_path = $diff_path
            metrics = $comparison
        }
    }
    Save-ReproContactSheet $contact_path $sheet_items 4 260 248
    Save-ReproContactSheet $chrome_contact_path $chrome_sheet_items 4 260 248
    Save-ReproContactSheet $comparison_contact_path $comparison_sheet_items 3 260 248

    $repeat_metrics = Compare-ReproImages $first_png $second_png
    $full_comparison = Compare-ReproImages $chrome_png $first_png
    $residual_summary = [ordered]@{
        primitive_type = New-ResidualSummary $residuals 'primitive_type'
        render_path = New-ResidualSummary $residuals 'render_path'
    }
    $metrics = [ordered]@{
        full_image = Measure-ReproImage $first_png
        chrome_reference = Measure-ReproImage $chrome_png
        chrome_comparison = $full_comparison
        repeat_comparison = $repeat_metrics
        tiles = $image_metrics
        chrome_tiles = $reference_metrics
        residuals = $residuals
        residual_summary = $residual_summary
    }
    $metrics | ConvertTo-Json -Depth 8 | Set-Content -Path $metrics_path -Encoding utf8
    [ordered]@{
        chrome_comparison = $full_comparison
        residuals = $residuals
        summary = $residual_summary
    } | ConvertTo-Json -Depth 8 | Set-Content -Path $residuals_path -Encoding utf8

    $script:repro_summary['metrics_path'] = $metrics_path
    $script:repro_summary['residuals_path'] = $residuals_path
    $script:repro_summary['contact_sheet'] = $contact_path
    $script:repro_summary['chrome_reference'] = $chrome_png
    $script:repro_summary['chrome_contact_sheet'] = $chrome_contact_path
    $script:repro_summary['comparison_contact_sheet'] = $comparison_contact_path
    $script:repro_summary['command_log'] = $command_log
    $script:repro_summary['repeat_metrics'] = $repeat_metrics
    $script:repro_summary['chrome_comparison'] = $full_comparison
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
