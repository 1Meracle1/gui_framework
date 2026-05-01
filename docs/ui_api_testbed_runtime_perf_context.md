# UI API Testbed Runtime Performance Context

Paste this context at the start of every new chat that works on tracing,
runtime performance analysis, or idle CPU optimization for `ui_api_testbed`.
Update this file at the end of every step with new measurements, decisions,
changed files, and remaining hypotheses.

## Goal

Reduce idle CPU usage for the Windows MSVC debug `ui_api_testbed` from the
observed 8-9% range toward 1-2%, without hiding the real cost behind release
builds or broad speculative rewrites.

The work must be evidence-driven:

- capture a sampled flamegraph or equivalent stack profile first,
- add debug-only manual trace zones to explain engine/app stages,
- optimize the largest measured costs,
- recapture profiles and traces after every change,
- keep each chat small enough to review and commit independently.

## Workspace

- Root: `D:\dev\cpp\gui_framework`.
- Primary executable: `ui_api_testbed`.
- Primary source today: `tools/ui_api_testbed.cpp`.
- Supporting systems likely to matter: `src/gui`, `src/draw`, `src/render`,
  `src/font_cache`, and Windows message/render loop code.

## Hard Constraints

- Keep the implementation minimal and local until measurements prove shared
  framework changes are needed.
- No external dependencies.
- No package-manager downloads.
- Preserve C++20, no exceptions, no RTTI, no STL-heavy tracing framework.
- Prefer project primitives: `Arena`, `StrRef`, `fmt::printf`, `fmt::eprintf`,
  and existing draw/render/gui APIs.
- Do not add broad configurability. Every knob must support profiling,
  tracing, or a verified optimization.
- Do not change visual design unless a measured performance issue requires it.
- Use `clang-format` on changed C++ files.

## Current Starting Facts

- `tools/ui_api_testbed.cpp` currently runs a continuous Windows render loop.
- The loop drains messages with `PeekMessageW`, rebuilds the UI, records draw
  commands, submits to the renderer, presents with VSYNC, and repeats.
- Debug builds currently enable the D3D debug layer in `run_windowed`.
- No reusable tracing or profiling utility was found in the repo at planning
  time.
- Existing draw command counters are available through `gui::draw` APIs.

## Profiling Contract

CPU percentage is an outcome metric, not the main diagnostic tool. Every
performance step must record enough profiling context that later chats can
compare results. Add the result to this file.

Minimum run context:

- Date and machine if known.
- Git commit or short working-tree description.
- Preset, target, and config.
- Window size and state.
- Theme and visible tab/state.
- Debug layer on/off when applicable.
- Profile or trace artifact path.
- Run length and warmup length.
- Whether the app was idle, hovered, occluded, minimized, or interacted with.

Minimum profiling artifacts:

- A sampled CPU flamegraph or equivalent call-stack profile when available.
  Acceptable tools include installed Visual Studio CPU Usage, Windows
  Performance Recorder/WPA, `xperf`, Superluminal, or another local sampler that
  preserves symbols and call stacks.
- A debug-only manual trace once implemented. Prefer a simple engine-style zone
  recorder that can emit Chrome Trace JSON or another timeline format readable
  by Perfetto, Chrome tracing, or Speedscope.
- A short textual hotspot summary from the sampled profile:
  top functions/stacks, inclusive cost, exclusive cost where available, and
  whether symbols were resolved.

Sampling profile requirements:

- Capture after warmup, with symbols resolved where the profiler supports it.
- Prefer wall-clock CPU samples for the `ui_api_testbed.exe` process only.
- Save the raw profiler artifact when possible, not just a screenshot.
- Export a flamegraph, call tree, or top-down view when the tool supports it.
- Repeat the same scenario after optimizations so before/after stacks are
  comparable.

Manual trace design direction:

- Debug-only and opt-in. Normal app runs should not write trace files.
- Minimal hot-path overhead: simple timestamp capture, fixed or arena-backed
  event storage, no per-zone heap allocation.
- Use explicit zones, not broad per-frame timers only. Required first-pass
  zones are:
  `frame`, `pump_messages`, `resize`, `theme_setup`, `begin_ui_frame`,
  `draw_ui`, `end_ui_frame`, `draw_begin_frame`, `draw_backdrop`,
  `gui_render_frame`, `draw_end_frame`, `render_begin_frame`,
  `draw_render_commands_to_window`, `present`, and `idle_wait` once waiting
  exists.
- Emit enough metadata to interpret the trace: process id, thread id if cheap,
  frame index, timestamp units, build preset/config, window size, debug layer
  state, and command counts.
- Prefer Chrome Trace JSON because it is easy to inspect in Perfetto or Chrome
  tracing without adding a dependency.

Minimum metrics:

- Sustained process CPU percentage after warmup, normalized like Task Manager
  process CPU percentage across all logical processors. This is for comparison
  and acceptance, not hotspot diagnosis.
- Frame count and approximate FPS.
- Mean and p95 frame CPU time when available.
- Mean and p95 stage timings once tracing exists:
  message pump, resize, UI build, draw command recording, render submission,
  present, idle wait.
- Draw command counts:
  total, primitive, styled rect, text, layers.
- Notes on GPU/driver/debug-layer behavior if observed.

Preferred CPU outcome command shape on Windows:

```powershell
.\build.bat windows-msvc-debug ui_api_testbed
$exe = "D:\dev\cpp\gui_framework\build\windows-msvc-debug\Debug\ui_api_testbed.exe"
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 3
$first = Get-Process -Id $p.Id
$cpu0 = $first.CPU
$t0 = Get-Date
Start-Sleep -Seconds 10
$second = Get-Process -Id $p.Id
$elapsed = ((Get-Date) - $t0).TotalSeconds
$cpu = (($second.CPU - $cpu0) / $elapsed) * 100.0 / [Environment]::ProcessorCount
"ProcessCpuPercent=$([math]::Round($cpu, 2))"
if (!$p.HasExited) { Stop-Process -Id $p.Id }
```

This CPU command is not enough by itself for any optimization step. Keep the
profiling tool, trace command, output path, and short hotspot summary in this
file with each result.

Preferred artifact location:

```text
D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\
```

Use descriptive names such as:

```text
step_01_debug_idle_sample.etl
step_01_debug_idle_flamegraph.html
step_02_debug_idle_trace.json
```

If no sampling profiler is installed or callable from the shell, document that
blocker explicitly and proceed with manual debug-only tracing. Do not use CPU
percentage alone to choose an optimization.

## Acceptance Target

The main target is the default Windows MSVC debug build, default 1280x800
window, default dark Testbed tab, no user input after warmup:

- sustained process CPU near 1-2% on the user's machine,
- sampled profile and manual trace show the idle cost is understood,
- no missed input after idle sleeping or redraw throttling,
- no broken resize, popup, modal, theme switch, text input, hover, scroll, or
  clipboard behavior,
- no broad performance-only complexity that outlives its usefulness.

Release measurements are useful for comparison, but do not replace the debug
acceptance target.

## Working Hypotheses

Rank these with data before optimizing:

Step 3 ranking after correlating the sampled profile with manual trace timings:

1. Continuous idle redraw is the first optimization target. The default visible
   idle window still renders about 70-73 FPS with no input, spending about
   13-14 ms of CPU-side frame work every frame and about 8.6-8.8% normalized
   process CPU.
2. Per-redraw UI build and draw command recording are the dominant stage costs.
   `ui_build` is about 13.0 ms mean / 14.1 ms p95, dominated by
   `draw_command_recording` and `gui_render_frame` at about 7.2-7.4 ms mean.
   The sampled profile maps that stage to text measurement/rasterization and
   layout.
3. Command/text volume is the main per-redraw sub-cost to revisit after idle
   redraw is gated: each idle frame records 175 commands, including 78 text
   commands and 87 styled rects.
4. The D3D debug layer is secondary. Turning it off reduces
   `draw_render_commands_to_window` from about 1.11 ms to 0.54 ms mean, but
   measured process CPU did not improve in the 5s trace window.
5. Present, message pumping, and theme setup are not first-order blockers in
   the current idle trace. `present` is about 0.07-0.08 ms mean and
   `theme_setup` is about 0.02 ms mean.

## Living Measurement Log

Add one entry per completed step. Keep entries short but comparable.

| Step | Date | Build | Scenario | Artifacts | CPU | Hotspots / timings | Notes |
| ---- | ---- | ----- | -------- | --------- | --- | ------------------ | ----- |
| 0 | 2026-05-01 | planning only | no run yet | none | unknown | none | Context and prompt plan created. |
| 1 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, no input after 3s warmup | `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`; merged ETL `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl`; stack report `build\perf\ui_api_testbed\step_01_debug_idle_xperf_stack_butterfly_merged.html`; summaries `step_01_debug_idle_top_inclusive_functions.txt` and `step_01_debug_idle_top_modules.txt` | 8.61% no profiler over 10.01s; 8.23% during VS CPU sample over 12.02s, 12 logical processors | Top inclusive: `run_windowed` 96.59%, `build_ui_commands` 88.63%, `gui::render_frame` 50.23%, text advance chain about 47%, text raster chain about 32%, `gui::end_frame` 25.99%, layout about 24.75%. Top exclusive modules: app 42.03%, DWrite 22.94%, kernel 11.62%, Win32k 5.62%, D3D debug layer 2.11%. | Local app symbols resolved after `xperf -merge`; OS/driver/DWrite symbols mostly unresolved. WPR/xperf were installed, but `wpr.exe -start CPU -filemode` failed before capture with `0xc5585011` ("Failed to enable the policy to profile system performance"). |
| 2 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked and local trace edits | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, auto-exit manual trace after 3s warmup and 5s capture | `build\perf\ui_api_testbed\step_02_debug_idle_trace.json` Chrome Trace JSON, 968,213 bytes | 8.51% over 5.00s, normalized with `GetProcessTimes`/QPC across 12 logical processors | 354 frames, 70.8 fps. Avg draw counts: 175 total, 29 primitive commands, 10 primitive batches, 87 styled rects, 78 text, 0 layers. Trace contains `frame`, `pump_messages`, `theme_setup`, `begin_ui_frame`, `draw_ui`, `end_ui_frame`, `draw_command_recording`, `draw_begin_frame`, `draw_backdrop`, `gui_render_frame`, `draw_end_frame`, `render_begin_frame`, `draw_render_commands_to_window`, `present`, draw command counters, and summary metadata. | Idle run had no input messages, so `input_handling` did not appear. No `idle_wait` appeared because the visible VSYNC loop does not sleep outside present. |
| 3 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `0a2ebf4` with measurement-only `--no-d3d-debug-layer` trace option in the working tree | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, no input after 3s warmup and 5s capture; compared debug layer on and off | Debug on `build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_on.json`; debug off `build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_off.json`; reviewed Step 1 sampled profile artifacts | Debug on: 8.57%, 350 frames, 69.8 FPS. Debug off: 8.75%, 366 frames, 73.2 FPS. 12 logical processors. | Debug on means/p95: frame 14.318/15.420 ms, `ui_build` 13.070/14.140 ms, `draw_command_recording` 7.406/8.184 ms, `gui_render_frame` 7.187/7.973 ms, `end_ui_frame` 3.893/4.196 ms, `draw_ui` 1.648/1.808 ms, `draw_render_commands_to_window` 1.108/1.172 ms, `present` 0.080/0.090 ms. Debug off means/p95: frame 13.655/14.803 ms, `ui_build` 12.988/13.988 ms, `draw_command_recording` 7.345/8.257 ms, `gui_render_frame` 7.126/8.037 ms, `draw_render_commands_to_window` 0.535/0.561 ms, `present` 0.072/0.089 ms. Draw counts unchanged: 175 total, 29 primitive, 10 batches, 87 styled rects, 78 text, 0 layers. Step 1 sampled stacks still correlate: `build_ui_commands`, `gui::render_frame`, text advance/raster, and layout dominate. | Measured ranking: optimize idle redraw frequency first. Then, if redraw cost still matters, optimize per-redraw `gui_render_frame` text/layout/command volume. D3D debug layer is measurable in renderer submission but not the dominant idle CPU driver; present, pump, and theme setup are small. |
| 4 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, working tree with local idle redraw gate in `tools/ui_api_testbed.cpp` | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, no input after 3s warmup; local message/state gated redraw policy | Trace `build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate.json`; trace summary `step_04_debug_idle_trace_after_redraw_gate_summary.txt`; CPU `step_04_debug_idle_cpu_after_redraw_gate.txt`; interaction screenshots `step_04_interaction_window_after.png` and `step_04_interaction_scroll_after.png` | Trace CPU: 0.03% over 5.00s. Independent CPU: 0.03% over 10.02s, 12 logical processors. Before comparison: Step 3 debug-layer-on idle trace was 8.57%, 350 frames, 69.8 FPS. | After idle gate: 0 rendered frames, 0 FPS, draw counts all 0 during the idle capture. Trace has 5 message/wait cycles: `idle_wait` mean/p95 999.121/2383.063 ms, `pump_messages` mean/p95 0.995/3.819 ms. The sampled before profile remains the relevant hotspot baseline: it showed continuous redraw cost in `build_ui_commands`/`gui::render_frame`; after the gate there is no comparable hot redraw stack during idle because the app is waiting. | DiagnosticsHub after-profile attempts failed before creating a session with "Value does not fall within the expected range" in attach and launch modes. Direct xperf retry with `SysProf` failed with `Access is denied`. Manual real-window interaction pass covered mouse move, theme switch, list scroll, text editing, popup, modal, and resize without a crash or missed visible update. |

Step 1 commands:

- Build: `.\build.bat windows-msvc-debug ui_api_testbed`.
- WPR attempt after launching the app and warming for 3 seconds:
  `wpr.exe -start CPU -filemode`; failed with `0xc5585011`.
- Profiler capture: Visual Studio DiagnosticsHub CPU Usage Low config,
  attached after warmup with
  `VSDiagnostics.exe start 21 /attach:<pid> /loadConfig:<VS>\Collector\AgentConfigs\CpuUsageLow.json /scratchLocation:build\perf\ui_api_testbed\step_01_vs_cpu_scratch /symbolCachePath:build\perf\ui_api_testbed\symbols`,
  then
  `VSDiagnostics.exe stop 21 /output:build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`.
- Export/summary:
  `VSDiagnostics.exe expandDiagSession build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`,
  `xperf.exe -merge <expanded>\sc.user_aux.etl build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl`,
  `xperf.exe -i build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl -symbols -a stack -process ui_api_testbed.exe -butterfly 5`,
  and
  `xperf.exe -i build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl -symbols -a profile -detail`.
- CPU outcome without profiler: launched
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe`, warmed for 3 seconds,
  sampled process CPU for 10.01 seconds, and saved
  `build\perf\ui_api_testbed\step_01_debug_idle_cpu_no_profiler.txt`.

Step 2 commands:

- Format check: `clang-format --dry-run --Werror tools\ui_api_testbed.cpp`.
- Build: `.\build.bat windows-msvc-debug ui_api_testbed`.
- Trace run:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_02_debug_idle_trace.json --trace-warmup-ms 3000 --trace-duration-ms 5000`.
- Trace stdout:
  `ui_api_testbed trace: recording build\perf\ui_api_testbed\step_02_debug_idle_trace.json`
  and
  `ui_api_testbed trace: wrote build\perf\ui_api_testbed\step_02_debug_idle_trace.json frames=354 duration_ms=5000.6 fps=70.8 process_cpu=8.51% commands_avg=175.0 max=175 primitive_avg=29.0 styled_rect_avg=87.0 text_avg=78.0 layers_avg=0.0`.
- Artifact sanity check:
  `Get-Content build\perf\ui_api_testbed\step_02_debug_idle_trace.json -Raw | ConvertFrom-Json`;
  grouped event names confirmed the expected zone, counter, and summary events.

Step 2 trace usage:

```powershell
.\build.bat windows-msvc-debug ui_api_testbed
build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_02_debug_idle_trace.json --trace-warmup-ms 3000 --trace-duration-ms 5000
```

`--trace <path>` enables Chrome Trace JSON output. `--trace-warmup-ms` delays
trace start; `--trace-duration-ms` auto-exits after the capture window. Without
`--trace`, no trace file is written. Release builds ignore trace flags and print
that debug-only tracing is disabled. Debug builds without `--trace` still pass
through cheap scope objects and active checks in the instrumented regions; the
file I/O and JSON formatting happen only while tracing is active.
`--no-d3d-debug-layer` is a measurement-only debug-build switch for comparing
the default D3D11 debug layer against the same run with the layer disabled. Trace
metadata records `debug_layer` as `1` or `0`.

Step 3 commands:

- Format check: `clang-format --dry-run --Werror tools\ui_api_testbed.cpp`.
- Build: `.\build.bat windows-msvc-debug ui_api_testbed`.
- Trace with D3D debug layer on:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_on.json --trace-warmup-ms 3000 --trace-duration-ms 5000`.
- Trace with D3D debug layer off:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_off.json --trace-warmup-ms 3000 --trace-duration-ms 5000 --no-d3d-debug-layer`.
- Sampled profile review:
  `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`,
  `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl`,
  `build\perf\ui_api_testbed\step_01_debug_idle_top_inclusive_functions.txt`,
  and `build\perf\ui_api_testbed\step_01_debug_idle_top_modules.txt`.
- Stage timing summary: parsed both Step 3 Chrome Trace JSON files by pairing
  `B`/`E` zone events and computing mean and p95 in milliseconds.

Step 4 redraw policy:

- Keep redraw gating local to `tools/ui_api_testbed.cpp`.
- `AppState::redraw_pending` starts true for initial paint.
- Win32 input and resize messages that the local app handles set
  `redraw_pending`.
- A rendered frame hashes `TestbedState` before and after UI building; if local
  app state changed during the immediate-mode frame, the loop renders one
  follow-up frame immediately.
- When no redraw is pending, the loop records `idle_wait` and calls
  `MsgWaitForMultipleObjectsEx(..., QS_ALLINPUT, MWMO_INPUTAVAILABLE)`.
- Debug trace auto-start and auto-exit still work while idle by using a
  trace-aware wait timeout.

Step 4 commands:

- Format check: `clang-format --dry-run --Werror tools\ui_api_testbed.cpp`.
- Build: `.\build.bat windows-msvc-debug ui_api_testbed`.
- Idle trace:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate.json --trace-warmup-ms 3000 --trace-duration-ms 5000`.
- Trace stdout:
  `ui_api_testbed trace: wrote build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate.json frames=0 duration_ms=5000.8 fps=0.0 process_cpu=0.03% commands_avg=0.0 max=0 primitive_avg=0.0 styled_rect_avg=0.0 text_avg=0.0 layers_avg=0.0`.
- Trace summary: parsed
  `build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate.json`
  into
  `build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate_summary.txt`.
- CPU outcome without profiler: launched
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe`, warmed for 3 seconds,
  sampled process CPU for 10.02 seconds, and saved
  `build\perf\ui_api_testbed\step_04_debug_idle_cpu_after_redraw_gate.txt`
  with `ProcessCpuPercent=0.03`.
- Sampled profile attempts:
  `VSDiagnostics.exe start 25 /attach:<pid> /loadConfig:<VS>\Team Tools\DiagnosticsHub\Collector\AgentConfigs\CpuUsageLow.json /scratchLocation:build\perf\ui_api_testbed\step_04_vs_cpu_scratch_retry /symbolCachePath:build\perf\ui_api_testbed\symbols`
  failed before creating a session with "Value does not fall within the expected
  range"; `VSDiagnostics.exe start 26 /launch:<ui_api_testbed.exe> ...` failed
  the same way; `xperf.exe -on SysProf -stackwalk profile` failed with
  `Access is denied`.
- Interaction pass: real-window PowerShell input automation moved the mouse,
  toggled theme, edited single-line and multiline text, opened popup and modal,
  resized the window, and captured
  `build\perf\ui_api_testbed\step_04_interaction_window_after.png`. A targeted
  list scroll check captured
  `build\perf\ui_api_testbed\step_04_interaction_scroll_after.png`.

## Decision Log

- 2026-05-01: Use a staged prompt-driven workflow. The first implementation
  should collect a sampled profile, then add local debug-only `ui_api_testbed`
  tracing before adding shared framework APIs.
- 2026-05-01: CPU percentage is not sufficient for diagnosis. Use it only as an
  outcome metric after stack profiles and manual trace data identify the cost.
- 2026-05-01: Use Visual Studio DiagnosticsHub CPU Usage for the Step 1 baseline
  on this machine because WPR could not start the system CPU profile. Keep the
  raw `.diagsession`, expanded ETL, merged ETL, and xperf stack report for
  comparison.
- 2026-05-01: The narrowest first manual trace zones should stay local to
  `tools/ui_api_testbed.cpp`: wrap the outer `frame`, `pump_messages`, `resize`,
  `render_begin_frame`, `draw_render_commands_to_window`, and `present` in
  `run_windowed`; wrap `theme_setup`, `begin_ui_frame`, `draw_ui`,
  `end_ui_frame`, `draw_begin_frame`, `draw_backdrop`, `gui_render_frame`, and
  `draw_end_frame` inside `build_ui_commands`.
- 2026-05-01: Added the first local debug-only trace writer in
  `tools/ui_api_testbed.cpp`. It streams Chrome Trace JSON, uses
  `QueryPerformanceCounter` and `GetProcessTimes`, supports
  `--trace`, `--trace-warmup-ms`, and `--trace-duration-ms`, emits draw command
  counters per frame, and prints a compact summary with Task Manager-style
  process CPU percentage.
- 2026-05-01: Added a local measurement-only `--no-d3d-debug-layer` option to
  `ui_api_testbed` so the same traced debug executable can compare default D3D11
  debug-layer behavior against a disabled layer without source edits.
- 2026-05-01: Step 3 trace correlation ranks continuous idle redraw frequency
  first. The largest per-redraw stage is `ui_build`, especially
  `draw_command_recording`/`gui_render_frame`, and the sampled profile maps that
  work to text measurement/rasterization and layout. D3D debug-layer overhead is
  real but secondary; present, pump, and theme setup are small.
- 2026-05-01: Implemented the first idle redraw policy locally in
  `tools/ui_api_testbed.cpp`. It redraws after handled input messages, resize,
  and local app state mutations, then waits for messages while idle. The default
  debug idle outcome dropped from Step 3's 8.57% CPU / 350 frames in 5 seconds
  to 0.03% CPU / 0 rendered frames in the same trace window.

## Current Best Diagnosis

Step 4 confirms the first idle redraw gate should stay local for now. The app no
longer redraws continuously while unchanged: the 5s idle trace after warmup
recorded 0 rendered frames and 0.03% process CPU, down from Step 3's 350 frames
and 8.57% CPU with the D3D debug layer on.

The Step 1 sampled profile remains the before-hotspot reference for active
redraw cost: `build_ui_commands`, `gui::render_frame`, text
measurement/rasterization, and layout dominate when frames are actually drawn.
After the idle gate, unchanged visible idle time is spent in `idle_wait`, so
text/layout optimization should wait until an active or interaction scenario
shows it still matters.

## Current Open Questions

- Does any future animated UI state need an explicit local redraw request?
- If active interaction still feels expensive, which active scenario should be
  profiled next now that idle redraw is fixed?
- DiagnosticsHub and xperf were blocked for the after sampled profile in this
  session; retry from an elevated profiler shell if a post-gate stack artifact
  is required.

## Handoff Rules For Each New Chat

1. Paste this full file first.
2. Paste exactly one prompt from
   `docs/ui_api_testbed_runtime_perf_prompts.md`.
3. Do not start later prompts until the earlier step is measured and this file
   is updated.
4. Keep edits bounded to the prompt.
5. If the step completes and the user asks for commits, make one commit for the
   step, including this updated context file.

## Required Final Response For Each Step

Include:

- changed files,
- exact checks, profiler commands, trace commands, and artifact paths,
- hotspot and metric summary before/after when applicable,
- context file updates made,
- whether the step is ready to commit.
