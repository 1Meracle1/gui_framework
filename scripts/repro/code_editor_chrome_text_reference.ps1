param(
    [string]$workspace_path = '',
    [string]$artifact_dir = '',
    [string]$file_path = 'examples\code_editor\editor_render.cpp',
    [ValidateRange(8, 32)]
    [int]$font_size = 12,
    [int]$settle_ms = 1200,
    [string]$chrome_path = '',
    [int]$code_x = 0,
    [int]$padding_y = 0,
    [int]$crop_width = 620,
    [int]$crop_height = 190
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'repro_common.ps1')

if (-not ('ChromeRefWin32' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ChromeRefWin32 {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
}
"@
}

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($workspace_path -eq '') {
    $workspace_path = $repo_root
}
if ($artifact_dir -eq '') {
    $artifact_dir = Join-Path $repo_root "_codex_artifacts\code_editor_chrome_reference_${font_size}px"
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

$resolved_file_path = if ([System.IO.Path]::IsPathRooted($file_path)) {
    $file_path
} else {
    Join-Path $workspace_path $file_path
}
$resolved_file_path = (Resolve-Path $resolved_file_path).Path
$font_path = (Resolve-Path (Join-Path $repo_root 'third_party\source_code_pro\SourceCodePro-Regular.ttf')).Path

New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
$html_path = Join-Path $artifact_dir 'chrome_reference.html'
$png_path = Join-Path $artifact_dir 'chrome_reference.png'
$client_png_path = Join-Path $artifact_dir 'chrome_reference_client.png'
$crop_path = Join-Path $artifact_dir 'chrome_reference_crop_4x.png'
$diagnostics_path = Join-Path $artifact_dir 'chrome_reference_diagnostics.json'
$profile_path = Join-Path $artifact_dir 'chrome_profile'
$summary_path = Join-Path $artifact_dir 'summary.json'
Remove-Item -Force $html_path, $png_path, $client_png_path, $crop_path, $diagnostics_path, $summary_path -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $profile_path | Out-Null

function Html-Escape([string]$text) {
    return [System.Net.WebUtility]::HtmlEncode($text)
}

function Token-Class([string]$token) {
    $keywords = @(
        'alignas', 'auto', 'bool', 'break', 'case', 'class', 'const', 'constexpr', 'continue',
        'default', 'delete', 'do', 'else', 'enum', 'false', 'for', 'if', 'inline', 'namespace',
        'new', 'noexcept', 'nullptr', 'private', 'public', 'return', 'sizeof', 'static', 'struct',
        'switch', 'template', 'true', 'using', 'void', 'while', 'static_cast', 'bit_cast',
        'reinterpret_cast', 'typename', 'alignof', 'static_assert', 'final', 'friend'
    )
    $types = @('char', 'double', 'float', 'int', 'int32_t', 'size_t', 'uint8_t', 'uint32_t', 'uint64_t', 'uintptr_t', 'wchar_t')
    if ($keywords -contains $token) { return 'keyword' }
    if ($types -contains $token) { return 'type' }
    return 'text'
}

function Format-Code-Line([string]$line) {
    $trimmed = $line.TrimStart(' ', "`t")
    if ($trimmed.StartsWith('#')) {
        return '<span class="preprocessor">' + (Html-Escape $line) + '</span>'
    }

    $result = ''
    $index = 0
    while ($index -lt $line.Length) {
        $ch = $line[$index]
        if ($ch -eq ' ' -or $ch -eq "`t") {
            $start = $index
            while ($index -lt $line.Length -and ($line[$index] -eq ' ' -or $line[$index] -eq "`t")) {
                $index += 1
            }
            $result += Html-Escape $line.Substring($start, $index - $start)
        } elseif ($ch -eq '"' -or $ch -eq "'") {
            $quote = $ch
            $start = $index
            $index += 1
            while ($index -lt $line.Length) {
                if ($line[$index] -eq '\' -and $index + 1 -lt $line.Length) {
                    $index += 2
                } elseif ($line[$index] -eq $quote) {
                    $index += 1
                    break
                } else {
                    $index += 1
                }
            }
            $result += '<span class="string">' + (Html-Escape $line.Substring($start, $index - $start)) + '</span>'
        } elseif ($ch -match '[0-9]') {
            $start = $index
            while ($index -lt $line.Length -and $line[$index] -match "[A-Za-z0-9_.'']") {
                $index += 1
            }
            $result += '<span class="number">' + (Html-Escape $line.Substring($start, $index - $start)) + '</span>'
        } elseif ($ch -match '[A-Za-z_]') {
            $start = $index
            while ($index -lt $line.Length -and $line[$index] -match '[A-Za-z0-9_]') {
                $index += 1
            }
            $token = $line.Substring($start, $index - $start)
            $class = Token-Class $token
            if ($class -eq 'text') {
                $result += Html-Escape $token
            } else {
                $result += "<span class=`"$class`">$(Html-Escape $token)</span>"
            }
        } else {
            $result += '<span class="punctuation">' + (Html-Escape ([string]$ch)) + '</span>'
            $index += 1
        }
    }
    return $result
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse('127.0.0.1'), 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    return $port
}

function Start-DiagnosticsListener([int]$port, [string]$target) {
    return Start-Job -ArgumentList $port, $target -ScriptBlock {
        param([int]$port, [string]$target)
        $ErrorActionPreference = 'Stop'
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse('127.0.0.1'), $port)
        $listener.Start()
        try {
            $client = $listener.AcceptTcpClient()
            try {
                $stream = $client.GetStream()
                $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $false, 4096, $true)
                $content_length = 0
                while ($true) {
                    $line = $reader.ReadLine()
                    if ($line -eq $null -or $line -eq '') {
                        break
                    }
                    if ($line.StartsWith('Content-Length:', [System.StringComparison]::OrdinalIgnoreCase)) {
                        $content_length = [int]$line.Substring(15).Trim()
                    }
                }
                $buffer = New-Object char[] $content_length
                $read = 0
                while ($read -lt $content_length) {
                    $count = $reader.Read($buffer, $read, $content_length - $read)
                    if ($count -le 0) {
                        break
                    }
                    $read += $count
                }
                $body = [string]::new($buffer, 0, $read)
                [System.IO.File]::WriteAllText($target, $body, [System.Text.Encoding]::UTF8)
                $response = [System.Text.Encoding]::ASCII.GetBytes("HTTP/1.1 204 No Content`r`nAccess-Control-Allow-Origin: *`r`nContent-Length: 0`r`nConnection: close`r`n`r`n")
                $stream.Write($response, 0, $response.Length)
            } finally {
                $client.Close()
            }
        } finally {
            $listener.Stop()
        }
    }
}

function Wait-ChromeDiagnostics([string]$path, [object]$job) {
    for ($index = 0; $index -lt 120; $index += 1) {
        if (Test-Path $path) {
            try {
                return (Get-Content -Raw $path) | ConvertFrom-Json
            } catch {
                Start-Sleep -Milliseconds 50
            }
        }
        if ($job -ne $null -and $job.State -eq 'Failed') {
            Receive-Job $job -ErrorAction SilentlyContinue | Out-Null
            throw "Chrome diagnostics listener failed."
        }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out waiting for Chrome diagnostics: $path"
}

$source_lines = [System.IO.File]::ReadAllLines($resolved_file_path)
$line_height = 21.0 * $font_size / 12.0
$diagnostics_port = Get-FreeTcpPort
$diagnostics_url = "http://127.0.0.1:$diagnostics_port/diagnostics"
$diagnostics_job = Start-DiagnosticsListener $diagnostics_port $diagnostics_path
$font_url = ([System.Uri]$font_path).AbsoluteUri
$rows = ''
$line_count = [Math]::Min(16, $source_lines.Length)
for ($i = 0; $i -lt $line_count; ++$i) {
    $code = Format-Code-Line $source_lines[$i]
    $rows += "<div class=`"line`"><span class=`"code`">$code</span></div>"
}

$html = @"
<!doctype html>
<meta charset="utf-8">
<style>
@font-face {
    font-family: "Source Code Pro Local";
    src: url("$font_url") format("truetype");
}
html, body {
    margin: 0;
    width: 100%;
    height: 100%;
    overflow: hidden;
    background: #12171e;
}
.editor {
    box-sizing: border-box;
    width: ${crop_width}px;
    height: ${crop_height}px;
    padding: ${padding_y}px 0 0 0;
    background: #12171e;
    color: #e0e6ec;
    font: ${font_size}px/${line_height}px "Source Code Pro Local", monospace;
    overflow: hidden;
}
.line {
    position: relative;
    height: ${line_height}px;
}
.code {
    position: absolute;
    left: ${code_x}px;
    white-space: pre;
}
.keyword { color: #84b2ff; }
.type { color: #56d3b2; }
.string { color: #eeac69; }
.number { color: #ca9cff; }
.comment { color: #678470; }
.preprocessor { color: #ff7484; }
.punctuation { color: #99a6b5; }
.function { color: #dcdcaa; }
</style>
<div class="editor">$rows</div>
<script>
(async function() {
    await document.fonts.ready;
    await new Promise(function(resolve) { requestAnimationFrame(function() { requestAnimationFrame(resolve); }); });
    const editor = document.querySelector('.editor');
    const lineNodes = Array.from(document.querySelectorAll('.line'));
    const firstLine = lineNodes[0];
    const firstCode = firstLine ? firstLine.querySelector('.code') : null;
    const editorRect = editor.getBoundingClientRect();
    const firstLineRect = firstLine ? firstLine.getBoundingClientRect() : { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 };
    const firstCodeRect = firstCode ? firstCode.getBoundingClientRect() : firstLineRect;
    const editorStyle = getComputedStyle(editor);
    const codeStyle = firstCode ? getComputedStyle(firstCode) : editorStyle;
    function rect(value) {
        return {
            left: value.left,
            top: value.top,
            right: value.right,
            bottom: value.bottom,
            width: value.width,
            height: value.height
        };
    }
    function classColor(name) {
        const probe = document.createElement('span');
        probe.className = name;
        probe.textContent = 'x';
        probe.style.position = 'absolute';
        probe.style.visibility = 'hidden';
        probe.style.left = '-10000px';
        probe.style.top = '-10000px';
        editor.appendChild(probe);
        const color = getComputedStyle(probe).color;
        probe.remove();
        return color;
    }
    const diagnostics = {
        source: 'chrome',
        device_pixel_ratio: window.devicePixelRatio,
        inner_width: window.innerWidth,
        inner_height: window.innerHeight,
        font_size: $font_size,
        line_height: $line_height,
        font: editorStyle.font,
        font_family: editorStyle.fontFamily,
        font_smoothing: {
            webkit: editorStyle.webkitFontSmoothing || '',
            moz: editorStyle.MozOsxFontSmoothing || ''
        },
        editor_rect: rect(editorRect),
        first_line_rect: rect(firstLineRect),
        first_code_rect: rect(firstCodeRect),
        text_origin: {
            x: firstCodeRect.left - editorRect.left,
            y: firstCodeRect.top - editorRect.top,
            relative_crop_x: firstCodeRect.left - editorRect.left,
            relative_crop_y: firstCodeRect.top - editorRect.top
        },
        crop: {
            x: Math.round(editorRect.left),
            y: Math.round(editorRect.top),
            width: $crop_width,
            height: $crop_height,
            scale: 1,
            origin: 'text_origin'
        },
        visible_lines: {
            total: lineNodes.length,
            viewport_count: lineNodes.filter(function(line) {
                const value = line.getBoundingClientRect();
                return value.bottom > editorRect.top && value.top < editorRect.bottom;
            }).length
        },
        colors: {
            background: editorStyle.backgroundColor,
            text: editorStyle.color,
            keyword: classColor('keyword'),
            type: classColor('type'),
            string: classColor('string'),
            number: classColor('number'),
            comment: classColor('comment'),
            preprocessor: classColor('preprocessor'),
            punctuation: classColor('punctuation'),
            function: classColor('function')
        },
        contamination: {
            current_line_visible: false,
            caret_visible: false,
            selection_active: false,
            line_numbers_in_crop: false,
            crop_origin: 'text_origin'
        },
        lines: lineNodes.slice(0, 4).map(function(line, index) {
            const code = line.querySelector('.code');
            return {
                line: index,
                text: code ? code.textContent : '',
                line_rect: rect(line.getBoundingClientRect()),
                code_rect: rect(code ? code.getBoundingClientRect() : line.getBoundingClientRect())
            };
        })
    };
    fetch('$diagnostics_url', {
        method: 'POST',
        mode: 'no-cors',
        headers: { 'Content-Type': 'text/plain' },
        body: JSON.stringify(diagnostics)
    }).catch(function() {});
})();
</script>
"@
[System.IO.File]::WriteAllText($html_path, $html, [System.Text.Encoding]::UTF8)

function Start-ChromeReferenceSession([string]$url) {
    $stdout_path = Join-Path $artifact_dir 'stdout.txt'
    $stderr_path = Join-Path $artifact_dir 'stderr.txt'
    Remove-Item -Force $stdout_path, $stderr_path, $summary_path -ErrorAction SilentlyContinue
    $script:repro_stdout_builder = [System.Text.StringBuilder]::new()
    $script:repro_stderr_builder = [System.Text.StringBuilder]::new()
    $script:repro_summary = [ordered]@{
        status = 'unknown'
        phase = 'start'
        hwnd = $null
        exit_code = $null
        artifact_dir = $artifact_dir
        stdout_path = $stdout_path
        stderr_path = $stderr_path
        summary_path = $summary_path
        frame_dump_path = ''
        stdout_preview = ''
        stderr_preview = ''
        error = ''
    }

    $arguments = @(
        '--user-data-dir=' + $profile_path,
        '--no-first-run',
        '--no-default-browser-check',
        '--disable-background-networking',
        '--disable-features=Translate,AutofillServerCommunication',
        '--force-device-scale-factor=1',
        '--app=' + $url
    )
    $script:repro_process = Start-Process `
        -FilePath $chrome_path `
        -ArgumentList $arguments `
        -WorkingDirectory $workspace_path `
        -WindowStyle Normal `
        -PassThru
    $script:repro_started = $script:repro_process -ne $null
    if (-not $script:repro_started) {
        throw 'Failed to start Chrome.'
    }

    $hwnd = [IntPtr]::Zero
    for ($index = 0; $index -lt 100; $index += 1) {
        if ($script:repro_process.HasExited) {
            break
        }
        $script:repro_process.Refresh()
        if ($script:repro_process.MainWindowHandle -ne [IntPtr]::Zero) {
            $hwnd = $script:repro_process.MainWindowHandle
            break
        }
        $hwnd = Get-ReproMainWindowHandle $script:repro_process.Id
        if ($hwnd -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        throw 'Failed to get Chrome window handle.'
    }

    $script:repro_hwnd = $hwnd
    $script:repro_summary['hwnd'] = ('0x{0:X}' -f $hwnd.ToInt64())
    Focus-ReproWindow $hwnd 80 80 760 360
}

function Save-ClientImage([IntPtr]$handle, [string]$full_target, [string]$crop_target, [object]$diagnostics) {
    $rect = New-Object ChromeRefWin32+RECT
    [ChromeRefWin32]::GetClientRect($handle, [ref]$rect) | Out-Null
    $point = New-Object ChromeRefWin32+POINT
    $point.X = 0
    $point.Y = 0
    [ChromeRefWin32]::ClientToScreen($handle, [ref]$point) | Out-Null
    $full_width = $rect.Right - $rect.Left
    $full_height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($full_width, $full_height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($point.X, $point.Y, 0, 0, $bitmap.Size)
    $bitmap.Save($full_target, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop = $diagnostics.crop
    $x = [int]$crop.x
    $y = [int]$crop.y
    $width = [Math]::Min([int]$crop.width, $bitmap.Width - $x)
    $height = [Math]::Min([int]$crop.height, $bitmap.Height - $y)
    $crop = $bitmap.Clone(
        [System.Drawing.Rectangle]::new($x, $y, $width, $height),
        $bitmap.PixelFormat
    )
    $crop.Save($crop_target, [System.Drawing.Imaging.ImageFormat]::Png)
    $crop.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact 'chrome_client_png' $full_target
    Add-ReproArtifact 'chrome_png' $crop_target
}

function Save-ScaledCrop([string]$source, [string]$target) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($source)
    $scale = 4
    $copy = [System.Drawing.Bitmap]::new($bitmap.Width * $scale, $bitmap.Height * $scale)
    $graphics = [System.Drawing.Graphics]::FromImage($copy)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.DrawImage($bitmap, 0, 0, $copy.Width, $copy.Height)
    $copy.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $copy.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact 'chrome_crop' $target
}

try {
    $url = ([System.Uri]$html_path).AbsoluteUri
    Start-ChromeReferenceSession $url

    Add-ReproArtifact 'chrome_path' $chrome_path
    Add-ReproArtifact 'file_path' $resolved_file_path
    Add-ReproArtifact 'font_path' $font_path
    Add-ReproArtifact 'font_size' ([string]$font_size)
    Add-ReproArtifact 'line_height' ([string]$line_height)
    Add-ReproArtifact 'code_x' ([string]$code_x)
    Add-ReproArtifact 'padding_y' ([string]$padding_y)
    Add-ReproArtifact 'html_path' $html_path
    Start-Sleep -Milliseconds $settle_ms
    $diagnostics = Wait-ChromeDiagnostics $diagnostics_path $diagnostics_job
    $diagnostics | Add-Member -NotePropertyName window_diagnostics -NotePropertyValue (Get-ReproWindowDiagnostics $script:repro_hwnd)
    $diagnostics | ConvertTo-Json -Depth 16 | Set-Content -Path $diagnostics_path -Encoding utf8
    Add-ReproArtifact 'chrome_diagnostics' $diagnostics_path
    Save-ClientImage $script:repro_hwnd $client_png_path $png_path $diagnostics
    Save-ScaledCrop $png_path $crop_path
    Set-ReproStatus 'survived'
} catch {
    Set-ReproStatus 'script_error'
    if ($script:repro_summary -ne $null) {
        $script:repro_summary['error'] = ($_ | Out-String)
    } else {
        Write-Error ($_ | Out-String)
    }
} finally {
    if ($diagnostics_job -ne $null) {
        Stop-Job $diagnostics_job -ErrorAction SilentlyContinue
        Remove-Job $diagnostics_job -Force -ErrorAction SilentlyContinue
    }
    Complete-ReproSession
}
