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

Step 1 ranking after the first sampled profile: continuous idle rendering is
the lead hypothesis. Repeated per-frame text measurement/rasterization and
layout are the strongest measured sub-costs. The D3D debug layer is visible but
smaller in this sample. Theme setup and present/synchronization still need
manual trace zones before they can be ranked cleanly.

1. The app burns CPU because it renders continuously even when there is no
   input, resize, animation, or dirty UI state.
2. The D3D debug layer contributes meaningful debug idle cost.
3. Rebuilding the full theme and applying it every frame costs more than it
   should.
4. Text measurement, font cache work, or table/list layout repeats expensive
   work every frame.
5. Draw command recording or renderer upload/submission is heavier than needed
   for an unchanged idle frame.
6. Present or synchronization behavior is causing extra CPU wakeups.

## Living Measurement Log

Add one entry per completed step. Keep entries short but comparable.

| Step | Date | Build | Scenario | Artifacts | CPU | Hotspots / timings | Notes |
| ---- | ---- | ----- | -------- | --------- | --- | ------------------ | ----- |
| 0 | 2026-05-01 | planning only | no run yet | none | unknown | none | Context and prompt plan created. |
| 1 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, no input after 3s warmup | `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`; merged ETL `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl`; stack report `build\perf\ui_api_testbed\step_01_debug_idle_xperf_stack_butterfly_merged.html`; summaries `step_01_debug_idle_top_inclusive_functions.txt` and `step_01_debug_idle_top_modules.txt` | 8.61% no profiler over 10.01s; 8.23% during VS CPU sample over 12.02s, 12 logical processors | Top inclusive: `run_windowed` 96.59%, `build_ui_commands` 88.63%, `gui::render_frame` 50.23%, text advance chain about 47%, text raster chain about 32%, `gui::end_frame` 25.99%, layout about 24.75%. Top exclusive modules: app 42.03%, DWrite 22.94%, kernel 11.62%, Win32k 5.62%, D3D debug layer 2.11%. | Local app symbols resolved after `xperf -merge`; OS/driver/DWrite symbols mostly unresolved. WPR/xperf were installed, but `wpr.exe -start CPU -filemode` failed before capture with `0xc5585011` ("Failed to enable the policy to profile system performance"). |
| 2 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked and local trace edits | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, auto-exit manual trace after 3s warmup and 5s capture | `build\perf\ui_api_testbed\step_02_debug_idle_trace.json` Chrome Trace JSON, 968,213 bytes | 8.51% over 5.00s, normalized with `GetProcessTimes`/QPC across 12 logical processors | 354 frames, 70.8 fps. Avg draw counts: 175 total, 29 primitive commands, 10 primitive batches, 87 styled rects, 78 text, 0 layers. Trace contains `frame`, `pump_messages`, `theme_setup`, `begin_ui_frame`, `draw_ui`, `end_ui_frame`, `draw_command_recording`, `draw_begin_frame`, `draw_backdrop`, `gui_render_frame`, `draw_end_frame`, `render_begin_frame`, `draw_render_commands_to_window`, `present`, draw command counters, and summary metadata. | Idle run had no input messages, so `input_handling` did not appear. No `idle_wait` appeared because the visible VSYNC loop does not sleep outside present. |

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

## Current Best Diagnosis

Step 1 confirms the idle window keeps spending CPU inside the continuous frame
loop. The hottest inclusive app path is `run_windowed -> build_ui_commands`.
Within that path, sampled cost clusters around `gui::render_frame`/`render_box`,
text measurement (`text_advance`, `font_cache::text_advance`,
`font_provider::text_advance`, DWrite), text rasterization, and
`gui::end_frame` layout.

Step 2 confirms the local manual trace path works and CPU remains comparable to
the Step 1 idle baseline at 8.51%. The default idle frame records 175 draw
commands every frame, including 78 text commands and 87 styled rects, with no
layers. The next step should inspect or summarize the trace timings to rank
`ui_build`, draw command recording, renderer submission, and `present`; do not
optimize from CPU percentage alone.

## Current Open Questions

- How much of the remaining debug idle CPU is app code versus D3D debug layer
  overhead after stage timings separate UI build, draw submission, and present?
- Does VSYNC present block cheaply, or does the loop still spend meaningful CPU
  between presents? Step 2 now has zones to answer this, but stage timing
  summaries have not been computed yet.
- Is there any hidden animation, caret, hover, or time-dependent style that
  requires continuous frames?
- Can idle redraw be gated locally in `ui_api_testbed`, or does the framework
  need a minimal "needs repaint" signal?
- Which stage remains hot after idle redraw is fixed?

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
