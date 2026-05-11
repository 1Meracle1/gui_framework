$ErrorActionPreference = 'Stop'

if (-not ('ReproWin32' -as [type])) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ReproWin32 {
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;
    public const uint WM_CHAR = 0x0102;
    public const uint WM_MOUSEMOVE = 0x0200;
    public const uint WM_LBUTTONDOWN = 0x0201;
    public const uint WM_LBUTTONUP = 0x0202;
    public const uint WM_MOUSEWHEEL = 0x020A;
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
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
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
}
"@
}

$script:repro_process = $null
$script:repro_started = $false
$script:repro_hwnd = [IntPtr]::Zero
$script:repro_stdout_builder = $null
$script:repro_stderr_builder = $null
$script:repro_stdout_event = $null
$script:repro_stderr_event = $null
$script:repro_summary = $null
$script:repro_frame_dump_path = ''

function Quote-ReproArgument([string]$arg) {
    if ($arg -eq '') {
        return '""'
    }
    if ($arg -notmatch '[\s"]') {
        return $arg
    }

    $result = '"'
    $slashes = 0
    foreach ($char in $arg.ToCharArray()) {
        if ($char -eq '\') {
            $slashes += 1
        } elseif ($char -eq '"') {
            if ($slashes -ne 0) {
                $result += ('\' * ($slashes * 2))
                $slashes = 0
            }
            $result += '\"'
        } else {
            if ($slashes -ne 0) {
                $result += ('\' * $slashes)
                $slashes = 0
            }
            $result += $char
        }
    }
    if ($slashes -ne 0) {
        $result += ('\' * ($slashes * 2))
    }
    $result += '"'
    return $result
}

function Join-ReproArguments([string[]]$arguments) {
    $parts = @()
    foreach ($arg in $arguments) {
        $parts += (Quote-ReproArgument $arg)
    }
    return ($parts -join ' ')
}

function Add-ReproArtifact([string]$name, [string]$path) {
    if ($script:repro_summary -eq $null) {
        return
    }
    $script:repro_summary[$name] = $path
}

function Set-ReproPhase([string]$phase) {
    if ($script:repro_summary -eq $null) {
        return
    }
    $script:repro_summary['phase'] = $phase
}

function Set-ReproStatus([string]$status) {
    if ($script:repro_summary -eq $null) {
        return
    }
    $script:repro_summary['status'] = $status
}

function Test-ReproExited {
    return $script:repro_process -eq $null -or $script:repro_process.HasExited
}

function Get-ReproMainWindowHandle([int]$process_id) {
    $script:repro_found_hwnd = [IntPtr]::Zero
    $callback = [ReproWin32+EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $window_process_id = 0
        [ReproWin32]::GetWindowThreadProcessId($hWnd, [ref]$window_process_id) | Out-Null
        if ($window_process_id -eq [uint32]$lParam.ToInt32() -and
            [ReproWin32]::IsWindowVisible($hWnd)) {
            $script:repro_found_hwnd = $hWnd
            return $false
        }
        return $true
    }
    [ReproWin32]::EnumWindows($callback, [IntPtr]$process_id) | Out-Null
    return $script:repro_found_hwnd
}

function Focus-ReproWindow(
    [IntPtr]$handle,
    [int]$x = 80,
    [int]$y = 80,
    [int]$width = 1320,
    [int]$height = 900
) {
    [ReproWin32]::ShowWindow($handle, 9) | Out-Null
    [ReproWin32]::SetWindowPos($handle, [IntPtr](-1), $x, $y, $width, $height, 0x0040) | Out-Null
    [ReproWin32]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 250
}

function Get-ReproWindowDiagnostics([IntPtr]$handle = [IntPtr]::Zero) {
    if ($handle -eq [IntPtr]::Zero) {
        $handle = $script:repro_hwnd
    }
    $window = New-Object ReproWin32+RECT
    $client = New-Object ReproWin32+RECT
    [ReproWin32]::GetWindowRect($handle, [ref]$window) | Out-Null
    [ReproWin32]::GetClientRect($handle, [ref]$client) | Out-Null
    $origin = New-Object ReproWin32+POINT
    $origin.X = 0
    $origin.Y = 0
    [ReproWin32]::ClientToScreen($handle, [ref]$origin) | Out-Null
    $window_dpi = [ReproWin32]::GetDpiForWindow($handle)
    $system_dpi = [ReproWin32]::GetDpiForSystem()
    return [ordered]@{
        window_rect = [ordered]@{
            left = $window.Left
            top = $window.Top
            right = $window.Right
            bottom = $window.Bottom
            width = $window.Right - $window.Left
            height = $window.Bottom - $window.Top
        }
        client_rect = [ordered]@{
            left = $client.Left
            top = $client.Top
            right = $client.Right
            bottom = $client.Bottom
            width = $client.Right - $client.Left
            height = $client.Bottom - $client.Top
            screen_x = $origin.X
            screen_y = $origin.Y
        }
        dpi = [ordered]@{
            system = $system_dpi
            window = $window_dpi
            scale = [Math]::Round([double]$window_dpi / 96.0, 4)
        }
    }
}

function Start-ReproSession {
    param(
        [string]$workspace_path,
        [string]$exe_path,
        [string]$artifact_dir,
        [string[]]$arguments,
        [string]$frame_dump_path = '',
        [int]$window_x = 80,
        [int]$window_y = 80,
        [int]$window_width = 1320,
        [int]$window_height = 900
    )

    if (-not (Test-Path $workspace_path)) {
        throw "Workspace path not found: $workspace_path"
    }
    if (-not (Test-Path $exe_path)) {
        throw "Executable not found: $exe_path"
    }

    New-Item -ItemType Directory -Force -Path $artifact_dir | Out-Null
    $stdout_path = Join-Path $artifact_dir 'stdout.txt'
    $stderr_path = Join-Path $artifact_dir 'stderr.txt'
    $summary_path = Join-Path $artifact_dir 'summary.json'
    Remove-Item -Force $stdout_path, $stderr_path, $summary_path -ErrorAction SilentlyContinue
    if ($frame_dump_path -ne '') {
        Remove-Item -Force $frame_dump_path -ErrorAction SilentlyContinue
    }

    $script:repro_frame_dump_path = $frame_dump_path
    $script:repro_summary = [ordered]@{
        status = 'unknown'
        phase = 'start'
        hwnd = $null
        exit_code = $null
        artifact_dir = $artifact_dir
        stdout_path = $stdout_path
        stderr_path = $stderr_path
        summary_path = $summary_path
        frame_dump_path = $frame_dump_path
        stdout_preview = ''
        stderr_preview = ''
        error = ''
    }

    $script:repro_stdout_builder = [System.Text.StringBuilder]::new()
    $script:repro_stderr_builder = [System.Text.StringBuilder]::new()

    $start_info = [System.Diagnostics.ProcessStartInfo]::new()
    $start_info.FileName = $exe_path
    $start_info.Arguments = Join-ReproArguments $arguments
    $start_info.WorkingDirectory = $workspace_path
    $start_info.UseShellExecute = $false
    $start_info.CreateNoWindow = $true
    $start_info.RedirectStandardOutput = $true
    $start_info.RedirectStandardError = $true

    $script:repro_process = [System.Diagnostics.Process]::new()
    $script:repro_process.StartInfo = $start_info
    $script:repro_process.EnableRaisingEvents = $true
    $script:repro_stdout_event = Register-ObjectEvent -InputObject $script:repro_process -EventName OutputDataReceived -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$script:repro_stdout_builder.AppendLine($EventArgs.Data)
        }
    }
    $script:repro_stderr_event = Register-ObjectEvent -InputObject $script:repro_process -EventName ErrorDataReceived -Action {
        if ($EventArgs.Data -ne $null) {
            [void]$script:repro_stderr_builder.AppendLine($EventArgs.Data)
        }
    }
    $script:repro_started = $script:repro_process.Start()
    if (-not $script:repro_started) {
        throw 'Failed to start application.'
    }
    $script:repro_process.BeginOutputReadLine()
    $script:repro_process.BeginErrorReadLine()

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
        throw 'Failed to get main window handle.'
    }

    $script:repro_hwnd = $hwnd
    $script:repro_summary['hwnd'] = ('0x{0:X}' -f $hwnd.ToInt64())
    Focus-ReproWindow $hwnd $window_x $window_y $window_width $window_height
}

function Send-ReproKey([int]$vk) {
    [ReproWin32]::PostMessage($script:repro_hwnd, [ReproWin32]::WM_KEYDOWN, [IntPtr]$vk, [IntPtr]1) |
        Out-Null
    Start-Sleep -Milliseconds 20
    [ReproWin32]::PostMessage($script:repro_hwnd, [ReproWin32]::WM_KEYUP, [IntPtr]$vk, [IntPtr]0) |
        Out-Null
}

function Send-ReproChar([char]$char) {
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_CHAR,
        [IntPtr][int][char]$char,
        [IntPtr]1
    ) | Out-Null
}

function Send-ReproText([string]$text, [int]$delay_ms = 20) {
    foreach ($char in $text.ToCharArray()) {
        Send-ReproChar $char
        Start-Sleep -Milliseconds $delay_ms
    }
}

function Get-ReproLParam([int]$x, [int]$y) {
    return ($y -shl 16) -bor ($x -band 0xffff)
}

function Send-ReproMouseMove([int]$x, [int]$y, [bool]$down = $false) {
    $lparam = Get-ReproLParam $x $y
    $wparam = if ($down) { [IntPtr]1 } else { [IntPtr]0 }
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_MOUSEMOVE,
        $wparam,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-ReproMouseDown([int]$x, [int]$y) {
    $lparam = Get-ReproLParam $x $y
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_LBUTTONDOWN,
        [IntPtr]1,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-ReproMouseUp([int]$x, [int]$y) {
    $lparam = Get-ReproLParam $x $y
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_LBUTTONUP,
        [IntPtr]0,
        [IntPtr]$lparam
    ) | Out-Null
}

function Send-ReproClick([int]$x, [int]$y) {
    Send-ReproMouseMove $x $y $false
    Start-Sleep -Milliseconds 80
    Send-ReproMouseDown $x $y
    Start-Sleep -Milliseconds 250
    Send-ReproMouseUp $x $y
}

function Send-ReproDrag([int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    Send-ReproMouseMove $x1 $y1 $false
    Start-Sleep -Milliseconds 120
    Send-ReproMouseDown $x1 $y1
    Start-Sleep -Milliseconds 120
    Send-ReproMouseMove $x2 $y2 $true
    Start-Sleep -Milliseconds 320
    Send-ReproMouseUp $x2 $y2
}

function Send-ReproWheel([int]$x, [int]$y, [int]$notches) {
    $point = [ReproWin32+POINT]::new()
    $point.X = $x
    $point.Y = $y
    [ReproWin32]::ClientToScreen($script:repro_hwnd, [ref]$point) | Out-Null
    $delta = $notches * 120
    $wparam = [IntPtr](($delta -shl 16) -band 0xffff0000)
    $lparam = [IntPtr](($point.Y -shl 16) -bor ($point.X -band 0xffff))
    [ReproWin32]::PostMessage(
        $script:repro_hwnd,
        [ReproWin32]::WM_MOUSEWHEEL,
        $wparam,
        $lparam
    ) | Out-Null
}

function Save-ReproScreenshot([string]$name, [string]$path) {
    [ReproWin32]::SetForegroundWindow($script:repro_hwnd) | Out-Null
    Start-Sleep -Milliseconds 80
    $rect = New-Object ReproWin32+RECT
    [ReproWin32]::GetWindowRect($script:repro_hwnd, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact $name $path
}

function Save-ReproClientScreenshot([string]$name, [string]$path) {
    [ReproWin32]::SetForegroundWindow($script:repro_hwnd) | Out-Null
    Start-Sleep -Milliseconds 80
    $rect = New-Object ReproWin32+RECT
    [ReproWin32]::GetClientRect($script:repro_hwnd, [ref]$rect) | Out-Null
    $point = New-Object ReproWin32+POINT
    $point.X = 0
    $point.Y = 0
    [ReproWin32]::ClientToScreen($script:repro_hwnd, [ref]$point) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($point.X, $point.Y, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact $name $path
}

function Save-ReproImageCrop(
    [string]$name,
    [string]$source,
    [string]$target,
    [int]$x,
    [int]$y,
    [int]$width,
    [int]$height,
    [int]$scale = 1
) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($source)
    $crop_rect = [System.Drawing.Rectangle]::new(
        $x,
        $y,
        [Math]::Min($width, $bitmap.Width - $x),
        [Math]::Min($height, $bitmap.Height - $y)
    )
    $crop = $bitmap.Clone($crop_rect, $bitmap.PixelFormat)
    if ($scale -eq 1) {
        $crop.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    } else {
        $copy = [System.Drawing.Bitmap]::new($crop.Width * $scale, $crop.Height * $scale)
        $graphics = [System.Drawing.Graphics]::FromImage($copy)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $graphics.DrawImage($crop, 0, 0, $copy.Width, $copy.Height)
        $copy.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $copy.Dispose()
    }
    $crop.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact $name $target
}

function Measure-ReproImage([string]$path) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($path)
    $width = $bitmap.Width
    $height = $bitmap.Height
    $count = [int64]$width * [int64]$height
    $sum_r = [int64]0
    $sum_g = [int64]0
    $sum_b = [int64]0
    $non_bg = [int64]0
    $bg = $bitmap.GetPixel(0, 0)
    for ($y = 0; $y -lt $height; ++$y) {
        for ($x = 0; $x -lt $width; ++$x) {
            $pixel = $bitmap.GetPixel($x, $y)
            $sum_r += $pixel.R
            $sum_g += $pixel.G
            $sum_b += $pixel.B
            $bg_delta = [Math]::Abs($pixel.R - $bg.R) +
                [Math]::Abs($pixel.G - $bg.G) +
                [Math]::Abs($pixel.B - $bg.B)
            if ($bg_delta -gt 12) {
                $non_bg += 1
            }
        }
    }
    $bitmap.Dispose()
    return [ordered]@{
        path = $path
        width = $width
        height = $height
        mean_r = [Math]::Round([double]$sum_r / [double]$count, 4)
        mean_g = [Math]::Round([double]$sum_g / [double]$count, 4)
        mean_b = [Math]::Round([double]$sum_b / [double]$count, 4)
        non_background_pct = [Math]::Round(([double]$non_bg * 100.0) / [double]$count, 4)
    }
}

function Compare-ReproImages([string]$reference, [string]$candidate) {
    $a = [System.Drawing.Bitmap]::FromFile($reference)
    $b = [System.Drawing.Bitmap]::FromFile($candidate)
    $width = [Math]::Min($a.Width, $b.Width)
    $height = [Math]::Min($a.Height, $b.Height)
    $count = [int64]$width * [int64]$height
    $sum = [int64]0
    $sum_sq = [double]0.0
    $changed = [int64]0
    $max_delta = 0
    for ($y = 0; $y -lt $height; ++$y) {
        for ($x = 0; $x -lt $width; ++$x) {
            $pa = $a.GetPixel($x, $y)
            $pb = $b.GetPixel($x, $y)
            $dr = [Math]::Abs($pa.R - $pb.R)
            $dg = [Math]::Abs($pa.G - $pb.G)
            $db = [Math]::Abs($pa.B - $pb.B)
            $pixel_sum = $dr + $dg + $db
            $sum += $pixel_sum
            $sum_sq += ($dr * $dr) + ($dg * $dg) + ($db * $db)
            $max_delta = [Math]::Max($max_delta, [Math]::Max($dr, [Math]::Max($dg, $db)))
            if ($pixel_sum -ne 0) {
                $changed += 1
            }
        }
    }
    $a.Dispose()
    $b.Dispose()
    return [ordered]@{
        reference = $reference
        candidate = $candidate
        width = $width
        height = $height
        mean_rgb_abs_sum = [Math]::Round([double]$sum / [double]$count, 4)
        rms_channel_delta = [Math]::Round([Math]::Sqrt($sum_sq / ([double]$count * 3.0)), 4)
        max_channel_delta = $max_delta
        changed_pct = [Math]::Round(([double]$changed * 100.0) / [double]$count, 4)
    }
}

function Save-ReproContactSheet(
    [string]$target,
    [object[]]$items,
    [int]$columns,
    [int]$thumb_width,
    [int]$thumb_height
) {
    $label_height = 24
    $rows = [Math]::Ceiling([double]$items.Count / [double]$columns)
    $sheet = [System.Drawing.Bitmap]::new($columns * $thumb_width, $rows * ($thumb_height + $label_height))
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $graphics.Clear([System.Drawing.Color]::FromArgb(18, 23, 30))
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $font = [System.Drawing.Font]::new('Consolas', 10.0)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(224, 230, 236))
    for ($index = 0; $index -lt $items.Count; ++$index) {
        $item = $items[$index]
        $column = $index % $columns
        $row = [Math]::Floor($index / $columns)
        $x = $column * $thumb_width
        $y = $row * ($thumb_height + $label_height)
        $graphics.DrawString([string]$item.label, $font, $brush, $x + 4, $y + 4)
        $image = [System.Drawing.Bitmap]::FromFile([string]$item.path)
        $graphics.DrawImage($image, $x, $y + $label_height, $thumb_width, $thumb_height)
        $image.Dispose()
    }
    $sheet.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $brush.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $sheet.Dispose()
}

function Save-ReproLeftCrop([string]$name, [string]$source, [string]$target) {
    $bitmap = [System.Drawing.Bitmap]::FromFile($source)
    $crop_width = [Math]::Min(430, $bitmap.Width)
    $crop_height = [Math]::Min(880, $bitmap.Height)
    $copy = $bitmap.Clone(
        [System.Drawing.Rectangle]::new(0, 0, $crop_width, $crop_height),
        $bitmap.PixelFormat
    )
    $copy.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $copy.Dispose()
    $bitmap.Dispose()
    Add-ReproArtifact $name $target
}

function Read-ReproFrameRaw([string]$path = '') {
    $source = if ($path -ne '') { $path } else { $script:repro_frame_dump_path }
    if ($source -eq '') {
        throw 'No frame dump path configured.'
    }
    for ($index = 0; $index -lt 80; $index += 1) {
        if (Test-Path $source) {
            try {
                $raw = [System.IO.File]::ReadAllText($source)
                $null = $raw | ConvertFrom-Json
                return $raw
            } catch {
                Start-Sleep -Milliseconds 50
            }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Timed out waiting for valid frame dump: $source"
}

function Read-ReproFrame([string]$path = '') {
    return (Read-ReproFrameRaw $path) | ConvertFrom-Json
}

function Copy-ReproFrameDump([string]$name, [string]$target) {
    $raw = Read-ReproFrameRaw
    [System.IO.File]::WriteAllText($target, $raw)
    Add-ReproArtifact $name $target
}

function Assert-ReproBoxDebugName([string]$debug_name, [string]$frame_path = '') {
    $frame = Read-ReproFrame $frame_path
    $matches = @($frame.boxes | Where-Object { $_.debug_name -eq $debug_name })
    if ($matches.Count -eq 0) {
        throw "Expected box debug_name not found: $debug_name"
    }
    return $matches[0]
}

function Assert-ReproBoxVisible([string]$debug_name, [string]$frame_path = '') {
    $box = Assert-ReproBoxDebugName $debug_name $frame_path
    $width = [double]$box.rect.max_x - [double]$box.rect.min_x
    $height = [double]$box.rect.max_y - [double]$box.rect.min_y
    if ($width -le 0.0 -or $height -le 0.0) {
        throw "Expected visible box has empty rect: $debug_name"
    }
    return $box
}

function Assert-ReproFrameTextContains([string]$text, [string]$frame_path = '') {
    $frame = Read-ReproFrame $frame_path
    $matches = @($frame.boxes | Where-Object { $_.text -ne $null -and $_.text.Contains($text) })
    if ($matches.Count -eq 0) {
        throw "Expected frame text not found: $text"
    }
    return $matches[0]
}

function Assert-ReproFocusedDebugName([string]$debug_name, [string]$frame_path = '') {
    $frame = Read-ReproFrame $frame_path
    $matches = @($frame.boxes | Where-Object { $_.focused -and $_.debug_name -eq $debug_name })
    if ($matches.Count -eq 0) {
        throw "Expected focused box debug_name not found: $debug_name"
    }
    return $matches[0]
}

function Complete-ReproSession {
    if ($script:repro_summary -eq $null) {
        return
    }

    if ($script:repro_process -ne $null -and $script:repro_started) {
        if (-not $script:repro_process.HasExited) {
            $script:repro_process.Kill()
            $script:repro_process.WaitForExit()
        }
        $script:repro_summary['exit_code'] = $script:repro_process.ExitCode
    }

    if ($script:repro_stdout_event -ne $null) {
        Unregister-Event -SourceIdentifier $script:repro_stdout_event.Name -ErrorAction SilentlyContinue
        Remove-Job -Id $script:repro_stdout_event.Id -Force -ErrorAction SilentlyContinue
    }
    if ($script:repro_stderr_event -ne $null) {
        Unregister-Event -SourceIdentifier $script:repro_stderr_event.Name -ErrorAction SilentlyContinue
        Remove-Job -Id $script:repro_stderr_event.Id -Force -ErrorAction SilentlyContinue
    }

    [System.IO.File]::WriteAllText($script:repro_summary['stdout_path'], $script:repro_stdout_builder.ToString())
    [System.IO.File]::WriteAllText($script:repro_summary['stderr_path'], $script:repro_stderr_builder.ToString())

    $stdout = [System.IO.File]::ReadAllText($script:repro_summary['stdout_path'])
    $stderr = [System.IO.File]::ReadAllText($script:repro_summary['stderr_path'])
    $script:repro_summary['stdout_preview'] =
        if ($stdout.Length -gt 2000) { $stdout.Substring(0, 2000) } else { $stdout }
    $script:repro_summary['stderr_preview'] =
        if ($stderr.Length -gt 2000) { $stderr.Substring(0, 2000) } else { $stderr }

    if ($script:repro_summary['status'] -eq 'unknown') {
        $script:repro_summary['status'] = if (Test-ReproExited) { 'exited' } else { 'survived' }
    }

    $json = $script:repro_summary | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($script:repro_summary['summary_path'], $json)
    $json
    if ($script:repro_summary['status'] -eq 'script_error') {
        exit 1
    }
}
