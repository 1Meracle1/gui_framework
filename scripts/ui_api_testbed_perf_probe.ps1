param(
    [ValidateSet("idle", "mouse", "both")]
    [string]$Scenario = "idle"
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $root

$out_dir = Join-Path $root "build\perf\ui_api_testbed"
New-Item -ItemType Directory -Force -Path $out_dir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$exe = Join-Path $root "build\windows-msvc-debug\Debug\ui_api_testbed.exe"

function New-ProbePaths([string]$scenario_name) {
    @{
        Trace = Join-Path $out_dir ("step_08_debug_{0}_trace_{1}.json" -f $scenario_name, $stamp)
        Log = Join-Path $out_dir ("step_08_debug_{0}_probe_{1}.txt" -f $scenario_name, $stamp)
        Summary = Join-Path $out_dir ("step_08_debug_{0}_summary_{1}.txt" -f $scenario_name, $stamp)
    }
}

function Write-TraceSummary(
    [string]$scenario_name,
    [string]$trace,
    [string]$log,
    [string]$summary,
    [string[]]$extra_lines
) {
    $events = (Get-Content $trace -Raw | ConvertFrom-Json).traceEvents
    $starts = @{}
    $durations = @{}
    $trace_summary = $null

    foreach ($event in $events) {
        if ($event.cat -eq "zone") {
            $name = [string]$event.name
            if ($event.ph -eq "B") {
                if (!$starts.ContainsKey($name)) {
                    $starts[$name] = New-Object "System.Collections.Generic.Stack[double]"
                }
                $starts[$name].Push([double]$event.ts)
            } elseif ($event.ph -eq "E" -and $starts.ContainsKey($name) -and $starts[$name].Count -ne 0) {
                if (!$durations.ContainsKey($name)) {
                    $durations[$name] = New-Object "System.Collections.Generic.List[double]"
                }
                $durations[$name].Add(([double]$event.ts - $starts[$name].Pop()) / 1000.0)
            }
        } elseif ($event.name -eq "trace_summary") {
            $trace_summary = $event.args
        }
    }

    $culture = [Globalization.CultureInfo]::InvariantCulture
    $summary_lines = @("Scenario=$scenario_name", "Trace=$trace", "ProbeLog=$log", "Summary=$summary")
    if ($extra_lines -ne $null) {
        $summary_lines += $extra_lines
    }
    $zone_names = @(
        "frame",
        "ui_build",
        "theme_setup",
        "begin_ui_frame",
        "draw_ui",
        "end_ui_frame",
        "draw_command_recording",
        "gui_render_frame",
        "draw_render_commands_to_window",
        "present",
        "idle_wait",
        "pump_messages",
        "input_handling"
    )

    foreach ($name in $zone_names) {
        if (!$durations.ContainsKey($name)) {
            continue
        }
        $values = $durations[$name]
        $total = 0.0
        foreach ($value in $values) {
            $total += $value
        }
        $mean = $total / $values.Count
        $sorted = @($values | Sort-Object)
        $p95_index = [Math]::Ceiling($sorted.Count * 0.95) - 1
        if ($p95_index -lt 0) {
            $p95_index = 0
        }
        if ($p95_index -ge $sorted.Count) {
            $p95_index = $sorted.Count - 1
        }
        $summary_lines += [string]::Format(
            $culture,
            "{0} count={1} mean_ms={2:F3} p95_ms={3:F3}",
            $name,
            $values.Count,
            $mean,
            $sorted[$p95_index]
        )
    }

    if ($trace_summary -ne $null) {
        $summary_lines += [string]::Format(
            $culture,
            "summary duration_ms={0:F2} cpu={1:F2} frames={2} fps={3:F2} commands_avg={4:F2} text_avg={5:F2}",
            [double]$trace_summary.duration_ms,
            [double]$trace_summary.process_cpu_percent,
            [int]$trace_summary.frames,
            [double]$trace_summary.fps,
            [double]$trace_summary.avg_commands,
            [double]$trace_summary.avg_text
        )
    }

    $summary_lines | Set-Content -Encoding ASCII $summary
    $summary_lines | Add-Content -Encoding ASCII $log
    $summary_lines
}

function Invoke-IdleTrace {
    $paths = New-ProbePaths "idle"
    $run_output = & $exe --trace $paths.Trace --trace-warmup-ms 3000 --trace-duration-ms 5000 2>&1
    $run_exit = $LASTEXITCODE
    $run_output
    $run_output | Set-Content -Encoding ASCII $paths.Log
    if ($run_exit -ne 0) {
        exit $run_exit
    }
    Write-TraceSummary "idle" $paths.Trace $paths.Log $paths.Summary @()
}

function Add-MouseInputApi {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class UiApiTestbedPerfInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool ScreenToClient(IntPtr hWnd, ref POINT point);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam);
}
"@
}

function New-MouseLParam([int]$x, [int]$y) {
    [IntPtr]([int]((($y -band 0xffff) -shl 16) -bor ($x -band 0xffff)))
}

function Send-TestbedMouseMessage([IntPtr]$hwnd, [uint32]$message, [int]$screen_x, [int]$screen_y, [uint32]$wparam) {
    $point = New-Object "UiApiTestbedPerfInput+POINT"
    $point.X = $screen_x
    $point.Y = $screen_y
    if ([UiApiTestbedPerfInput]::ScreenToClient($hwnd, [ref]$point)) {
        [UiApiTestbedPerfInput]::PostMessageW(
            $hwnd,
            $message,
            [UIntPtr]$wparam,
            (New-MouseLParam $point.X $point.Y)
        ) | Out-Null
    }
}

function Invoke-MouseActivity([Diagnostics.Process]$process, [int]$milliseconds) {
    $hwnd = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100 -and !$process.HasExited; ++$attempt) {
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $hwnd = $process.MainWindowHandle
            break
        }
        Start-Sleep -Milliseconds 50
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        throw "ui_api_testbed window did not appear"
    }

    [UiApiTestbedPerfInput]::SetForegroundWindow($hwnd) | Out-Null
    $moves = 0
    $clicks = 0
    $end = [Environment]::TickCount64 + $milliseconds
    while ([Environment]::TickCount64 -lt $end -and !$process.HasExited) {
        $rect = New-Object "UiApiTestbedPerfInput+RECT"
        if ([UiApiTestbedPerfInput]::GetWindowRect($hwnd, [ref]$rect)) {
            $width = [Math]::Max($rect.Right - $rect.Left, 1)
            $height = [Math]::Max($rect.Bottom - $rect.Top, 1)
            $span_x = [Math]::Max($width - 240, 1)
            $span_y = [Math]::Max($height - 220, 1)
            $x = [Math]::Min($rect.Right - 20, $rect.Left + 120 + (($moves * 29) % $span_x))
            $y = [Math]::Min($rect.Bottom - 20, $rect.Top + 110 + (($moves * 17) % $span_y))
            [UiApiTestbedPerfInput]::SetCursorPos([int]$x, [int]$y) | Out-Null
            Send-TestbedMouseMessage $hwnd 0x0200 ([int]$x) ([int]$y) 0
            if (($moves % 30) -eq 0) {
                Send-TestbedMouseMessage $hwnd 0x0201 ([int]$x) ([int]$y) 1
                Start-Sleep -Milliseconds 8
                Send-TestbedMouseMessage $hwnd 0x0202 ([int]$x) ([int]$y) 0
                $clicks += 1
            }
            $moves += 1
        }
        Start-Sleep -Milliseconds 16
    }

    @("MouseMoves=$moves", "MouseClicks=$clicks")
}

function Invoke-MouseTrace {
    Add-MouseInputApi
    $paths = New-ProbePaths "mouse"

    $start_info = New-Object Diagnostics.ProcessStartInfo
    $start_info.FileName = $exe
    $start_info.Arguments = "--trace `"$($paths.Trace)`" --trace-warmup-ms 1000 --trace-duration-ms 5000"
    $start_info.WorkingDirectory = $root
    $start_info.UseShellExecute = $false
    $start_info.RedirectStandardOutput = $true
    $start_info.RedirectStandardError = $true

    $process = [Diagnostics.Process]::Start($start_info)
    $activity = @()
    try {
        $activity = Invoke-MouseActivity $process 6500
        if (!$process.WaitForExit(3000)) {
            Stop-Process -Id $process.Id -Force
            throw "ui_api_testbed did not exit after mouse trace"
        }
    } finally {
        if ($process -ne $null -and !$process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $run_output = @()
    if ($stdout.Length -ne 0) {
        $run_output += ($stdout -split "`r?`n" | Where-Object { $_.Length -ne 0 })
    }
    if ($stderr.Length -ne 0) {
        $run_output += ($stderr -split "`r?`n" | Where-Object { $_.Length -ne 0 })
    }
    $run_output
    $run_output | Set-Content -Encoding ASCII $paths.Log
    if ($process.ExitCode -ne 0) {
        exit $process.ExitCode
    }
    Write-TraceSummary "mouse" $paths.Trace $paths.Log $paths.Summary $activity
}

& (Join-Path $root "build.bat") windows-msvc-debug ui_api_testbed
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
if (!(Test-Path $exe)) {
    throw "missing $exe"
}

$scenarios = if ($Scenario -eq "both") { @("idle", "mouse") } else { @($Scenario) }
foreach ($scenario_name in $scenarios) {
    if ($scenario_name -eq "idle") {
        Invoke-IdleTrace
    } else {
        Invoke-MouseTrace
    }
}

$running = Get-Process ui_api_testbed -ErrorAction SilentlyContinue
if ($running -ne $null) {
    $running | Select-Object Id, ProcessName, Path
    exit 1
}

$cleanup = "NoProcessRunning=ui_api_testbed.exe"
foreach ($scenario_name in $scenarios) {
    $paths = New-ProbePaths $scenario_name
    $cleanup | Add-Content -Encoding ASCII $paths.Log
}
$cleanup
