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

Canonical repeatable probes on Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario mouse
powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario both
```

The default probe builds `windows-msvc-debug` `ui_api_testbed`, runs the idle
debug trace after a 3s warmup for a 5s capture, writes timestamped artifacts
under `build\perf\ui_api_testbed\`, prints the trace metrics, parses zone
mean/p95 timings into a summary text file, and fails if any `ui_api_testbed`
process remains alive. `-Scenario mouse` runs a 1s warmup plus 5s active trace
while the script moves the pointer in shrinking circles over the testbed client
area and posts matching client mouse move messages to the window.
`-Scenario both` captures idle first, then active mouse movement.

Expected output shape:

```text
ui_api_testbed trace: wrote <trace.json> frames=<N> duration_ms=<ms> fps=<fps> process_cpu=<percent>% commands_avg=<N> max=<N> primitive_avg=<N> styled_rect_avg=<N> text_avg=<N> layers_avg=<N>
Scenario=<idle|mouse>
Trace=<trace.json>
ProbeLog=<stdout.txt>
Summary=<summary.txt>
MouseMoves=<N>
MouseClicks=0
<zone> count=<N> mean_ms=<ms> p95_ms=<ms>
summary duration_ms=<ms> cpu=<percent> frames=<N> fps=<fps> commands_avg=<N> text_avg=<N>
NoProcessRunning=ui_api_testbed.exe
```

Use the probe as a comparable artifact generator, not a pass/fail benchmark.
For a sampled profile/flamegraph, use the Step 1 DiagnosticsHub/xperf command
sequence below when the local profiler is working, save the raw profiler
artifact and exported stack/flamegraph under `build\perf\ui_api_testbed\`, and
record profiler failures as stdout/stderr artifacts if the tool cannot capture.

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

## Final Findings

1. The debug idle target is met on this machine. The pre-gate sampled profile
   showed continuous redraw spending 8.23-8.61% CPU in `run_windowed`,
   `build_ui_commands`, `gui::render_frame`, text advance/rasterization, and
   layout. The final default debug idle trace now records 0 rendered frames,
   0.03% CPU, and one long `idle_wait` across the 5s capture.
2. Active interaction still has meaningful per-redraw cost, but it is no longer
   an idle blocker. The final interaction trace rendered 116 frames in 5.02s at
   3.94% CPU while synthetic client mouse messages drove redraws. The dominant
   active stages were `gui_render_frame` and `draw_command_recording`.
3. Text/layout remains the next optional active-redraw target if interaction
   performance becomes important. Step 5 removed the measured selectable-label
   pointer hit-test cost, and Step 7 reduced wrapped text line-breaking cost.
   Command volume is still about 175 commands with 78 text commands per redraw.
4. Draw/render submission is secondary. Step 6 measured it and rejected compact
   redundant-state changes because traces did not improve; no draw/render source
   change was kept.
5. Present, message pumping, and theme setup are not current blockers. Theme
   setup stayed near 0.02 ms in earlier active traces, and the final idle trace
   spends the acceptance window waiting.
6. Post-gate sampled profiling remains a tooling risk, not an implementation
   blocker. DiagnosticsHub and xperf failed in prior after-profile attempts; the
   retained Step 1 sampled profile is still the resolved stack evidence for the
   removed continuous-redraw cost.

## Living Measurement Log

Add one entry per completed step. Keep entries short but comparable.

| Step | Date | Build | Scenario | Artifacts | CPU | Hotspots / timings | Notes |
| ---- | ---- | ----- | -------- | --------- | --- | ------------------ | ----- |
| 0 | 2026-05-01 | planning only | no run yet | none | unknown | none | Context and prompt plan created. |
| 1 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, no input after 3s warmup | `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu.diagsession`; merged ETL `build\perf\ui_api_testbed\step_01_debug_idle_vs_cpu_merged.etl`; stack report `build\perf\ui_api_testbed\step_01_debug_idle_xperf_stack_butterfly_merged.html`; summaries `step_01_debug_idle_top_inclusive_functions.txt` and `step_01_debug_idle_top_modules.txt` | 8.61% no profiler over 10.01s; 8.23% during VS CPU sample over 12.02s, 12 logical processors | Top inclusive: `run_windowed` 96.59%, `build_ui_commands` 88.63%, `gui::render_frame` 50.23%, text advance chain about 47%, text raster chain about 32%, `gui::end_frame` 25.99%, layout about 24.75%. Top exclusive modules: app 42.03%, DWrite 22.94%, kernel 11.62%, Win32k 5.62%, D3D debug layer 2.11%. | Local app symbols resolved after `xperf -merge`; OS/driver/DWrite symbols mostly unresolved. WPR/xperf were installed, but `wpr.exe -start CPU -filemode` failed before capture with `0xc5585011` ("Failed to enable the policy to profile system performance"). |
| 2 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `7348779` with perf docs untracked and local trace edits | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, auto-exit manual trace after 3s warmup and 5s capture | `build\perf\ui_api_testbed\step_02_debug_idle_trace.json` Chrome Trace JSON, 968,213 bytes | 8.51% over 5.00s, normalized with `GetProcessTimes`/QPC across 12 logical processors | 354 frames, 70.8 fps. Avg draw counts: 175 total, 29 primitive commands, 10 primitive batches, 87 styled rects, 78 text, 0 layers. Trace contains `frame`, `pump_messages`, `theme_setup`, `begin_ui_frame`, `draw_ui`, `end_ui_frame`, `draw_command_recording`, `draw_begin_frame`, `draw_backdrop`, `gui_render_frame`, `draw_end_frame`, `render_begin_frame`, `draw_render_commands_to_window`, `present`, draw command counters, and summary metadata. | Idle run had no input messages, so `input_handling` did not appear. No `idle_wait` appeared because the visible VSYNC loop does not sleep outside present. |
| 3 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `0a2ebf4` with measurement-only `--no-d3d-debug-layer` trace option in the working tree | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, no input after 3s warmup and 5s capture; compared debug layer on and off | Debug on `build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_on.json`; debug off `build\perf\ui_api_testbed\step_03_debug_idle_trace_debug_layer_off.json`; reviewed Step 1 sampled profile artifacts | Debug on: 8.57%, 350 frames, 69.8 FPS. Debug off: 8.75%, 366 frames, 73.2 FPS. 12 logical processors. | Debug on means/p95: frame 14.318/15.420 ms, `ui_build` 13.070/14.140 ms, `draw_command_recording` 7.406/8.184 ms, `gui_render_frame` 7.187/7.973 ms, `end_ui_frame` 3.893/4.196 ms, `draw_ui` 1.648/1.808 ms, `draw_render_commands_to_window` 1.108/1.172 ms, `present` 0.080/0.090 ms. Debug off means/p95: frame 13.655/14.803 ms, `ui_build` 12.988/13.988 ms, `draw_command_recording` 7.345/8.257 ms, `gui_render_frame` 7.126/8.037 ms, `draw_render_commands_to_window` 0.535/0.561 ms, `present` 0.072/0.089 ms. Draw counts unchanged: 175 total, 29 primitive, 10 batches, 87 styled rects, 78 text, 0 layers. Step 1 sampled stacks still correlate: `build_ui_commands`, `gui::render_frame`, text advance/raster, and layout dominate. | Measured ranking: optimize idle redraw frequency first. Then, if redraw cost still matters, optimize per-redraw `gui_render_frame` text/layout/command volume. D3D debug layer is measurable in renderer submission but not the dominant idle CPU driver; present, pump, and theme setup are small. |
| 4 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, working tree with local idle redraw gate in `tools/ui_api_testbed.cpp` | Default 1280x800 window, visible idle dark Testbed tab, D3D11 VSYNC, debug layer on, no input after 3s warmup; local message/state gated redraw policy | Trace `build\perf\ui_api_testbed\step_04_debug_idle_trace_after_redraw_gate.json`; trace summary `step_04_debug_idle_trace_after_redraw_gate_summary.txt`; CPU `step_04_debug_idle_cpu_after_redraw_gate.txt`; interaction screenshots `step_04_interaction_window_after.png` and `step_04_interaction_scroll_after.png` | Trace CPU: 0.03% over 5.00s. Independent CPU: 0.03% over 10.02s, 12 logical processors. Before comparison: Step 3 debug-layer-on idle trace was 8.57%, 350 frames, 69.8 FPS. | After idle gate: 0 rendered frames, 0 FPS, draw counts all 0 during the idle capture. Trace has 5 message/wait cycles: `idle_wait` mean/p95 999.121/2383.063 ms, `pump_messages` mean/p95 0.995/3.819 ms. The sampled before profile remains the relevant hotspot baseline: it showed continuous redraw cost in `build_ui_commands`/`gui::render_frame`; after the gate there is no comparable hot redraw stack during idle because the app is waiting. | DiagnosticsHub after-profile attempts failed before creating a session with "Value does not fall within the expected range" in attach and launch modes. Direct xperf retry with `SysProf` failed with `Access is denied`. Manual real-window interaction pass covered mouse move, theme switch, list scroll, text editing, popup, modal, and resize without a crash or missed visible update. |
| 5 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, working tree with shared selectable-label pointer-hit-test gate in `src\gui\src\gui.cpp` | Default 1280x800 window, visible dark Testbed tab, D3D11 VSYNC, debug layer on, 1s warmup and 5s trace while synthetic mouse movement triggered active redraws after the Step 4 idle gate | Before trace `build\perf\ui_api_testbed\step_05_active_trace_before_theme_cache.json`; before summary `step_05_active_trace_before_theme_cache.summary.txt`; after trace `build\perf\ui_api_testbed\step_05_active_trace_after_selectable_pointer_gate.json`; after summary `step_05_active_trace_after_selectable_pointer_gate.summary.txt`; reviewed Step 1 sampled stack report `step_01_debug_idle_top_inclusive_functions.txt` | Before: 4.57%, 173 frames, 34.53 FPS. After: 3.83%, 174 frames, 34.80 FPS. Draw counts unchanged at 175 commands, 78 text, 87 styled rects. | Before means/p95: `ui_build` 14.068/17.242 ms, `draw_ui` 2.351/2.514 ms, `theme_setup` 0.020/0.025 ms, `gui_render_frame` 7.371/9.908 ms. After means/p95: `ui_build` 11.475/12.554 ms, `draw_ui` 0.225/0.238 ms, `theme_setup` 0.020/0.032 ms, `gui_render_frame` 7.138/8.163 ms. Step 1 sampled stacks mapped the `draw_ui` cost to `apply_pointer_text_selection`/`text_index_from_mouse` at about 9.6% inclusive. | Changed `apply_pointer_text_selection` to call `text_index_from_mouse` only when a selectable label is pressed, double/triple clicked, actively dragging, or released. Rejected theme/spec caching because `theme_setup` stayed about 0.020 ms. Rejected local style initializer churn because no sampled stack identified it and draw command counts were unchanged. DiagnosticsHub attach and launch retries both failed to create an after sampled profile with "Value does not fall within the expected range"; no `.diagsession` was produced. Full `gui_tests.exe` was run because the harness has no filter; selectable-label tests passed, but the executable reported failures in unrelated tab/table/dense-controls cases. |
| 6 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, clean source tree after rejected draw/render probes | Default 1280x800 window, visible dark Testbed tab, D3D11 VSYNC, 1s warmup and 5s trace while synthetic mouse movement triggered active redraws after the Step 4 idle gate; compared D3D11 debug layer on/off | Before traces `build\perf\ui_api_testbed\step_06_active_trace_before_debug_layer_on.json` and `_off.json`; summaries with matching `.summary.txt`; rejected-probe traces `step_06_active_trace_after_debug_layer_on.json`, `step_06_active_trace_after_sampler_debug_layer_on.json`, and `step_06_active_trace_after_begin_unbind_delete_debug_layer_on.json`; reviewed Step 1 sampled profile detail | Before debug on: 3.62%, 170 frames, 33.96 FPS. Before debug off: 3.75%, 166 frames, 33.20 FPS. Rejected probes stayed in the same range: sampler-once debug on 3.98%, begin-unbind deletion debug on 3.67%. | Before debug on means/p95: `draw_render_commands_to_window` 1.112/1.179 ms, `gui_render_frame` 7.208/7.855 ms. Before debug off: `draw_render_commands_to_window` 0.538/0.629 ms, `gui_render_frame` 7.346/8.653 ms. Sampler-once was 1.124/1.188 ms debug on and 0.536/0.624 ms debug off. Begin-pass unbind deletion was 1.129/1.308 ms debug on and 0.560/0.802 ms debug off. | Inspected measured draw/render paths only. Tried and reverted draw-layer redundant pipeline/scissor tracking, draw-layer sampler-once binding, and D3D11 begin-pass redundant unbind deletion because traces showed noise or regressions. Step 1 sampled profile detail listed draw/render submission functions at 0.00-0.01%, with `render_commands_to_window` at 0.00%, so the sampled profile still points away from renderer submission as the next optimization target. |
| 7 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, working tree with wrapped text first-overflow search in `src\gui\src\gui.cpp` | Default 1280x800 window, visible dark Testbed tab, D3D11 VSYNC, debug layer on, 1s warmup and 5s trace while synthetic mouse movement triggered active redraws after the Step 4 idle gate | Before trace `build\perf\ui_api_testbed\step_06_active_trace_before_debug_layer_on.json`; after trace `build\perf\ui_api_testbed\step_07_active_trace_after_wrap_search.json`; after summary `step_07_active_trace_after_wrap_search.summary.txt`; sampled profile attempt log `step_07_active_sample_after_wrap_search.stdout.txt` | Before: 3.62%, 170 frames, 33.96 FPS. After: 3.04%, 172 frames, 34.33 FPS. Draw counts unchanged at 175 commands, 78 text, 87 styled rects. | Before means/p95: `ui_build` 11.719/12.616 ms, `end_ui_frame` 3.930/3.975 ms, `gui_render_frame` 7.208/7.855 ms. After means/p95: `ui_build` 7.062/9.784 ms, `end_ui_frame` 0.786/1.100 ms, `gui_render_frame` 5.691/8.041 ms. | Step 1 sampled stacks pointed at `next_text_line`/`text_advance`; Step 7 changes wrapped line breaking from per-character prefix measurement to exponential/binary first-overflow search, preserving the existing whitespace skip/backtrack rule. New risk: after sampled stack is still unavailable because DiagnosticsHub failed again with "Value does not fall within the expected range"; no `.diagsession` was produced, and p95 trace timings remain noisier than means. |
| 8 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `72fbd13` with local Step 8 probe script | Canonical default idle probe: 1280x800 visible dark Testbed tab, D3D11 VSYNC, debug layer on, 3s warmup and 5s trace capture | Trace `build\perf\ui_api_testbed\step_08_debug_idle_trace_20260501_200937.json`; stdout `step_08_debug_idle_probe_20260501_200937.txt`; summary `step_08_debug_idle_summary_20260501_200937.txt` | 0.00% over 5013.67 ms, 0 rendered frames, 0.00 FPS | Summary: `frame` count 1 mean/p95 5013.579/5013.579 ms, `idle_wait` count 1 mean/p95 5013.524/5013.524 ms, `pump_messages` count 1 mean/p95 0.007/0.007 ms, commands/text averages 0.00. | Canonical command `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1` built the target, ran the trace, wrote artifacts, exited cleanly, and printed `NoProcessRunning=ui_api_testbed.exe`. The probe intentionally does not assert cross-machine thresholds and does not replace sampled stack profiles. |
| 8a | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `72fbd13` with local Step 8 probe script | Canonical active mouse/click probe: 1280x800 visible dark Testbed tab, D3D11 VSYNC, debug layer on, 1s warmup and 5s trace capture while the script moved and clicked the mouse | Trace `build\perf\ui_api_testbed\step_08_debug_mouse_trace_20260501_200855.json`; stdout `step_08_debug_mouse_probe_20260501_200855.txt`; summary `step_08_debug_mouse_summary_20260501_200855.txt` | 1.61% over 5007.39 ms, 187 rendered frames, 37.34 FPS | Mouse activity: 212 moves, 8 clicks. Means/p95: `ui_build` 6.744/7.735 ms, `draw_command_recording` 5.675/6.191 ms, `gui_render_frame` 5.458/5.950 ms, `draw_render_commands_to_window` 1.132/1.178 ms, `present` 0.174/0.272 ms, `input_handling` 0.018/0.031 ms. Commands/text averages 175.72/78.00. | Command `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario mouse` built the target, ran the active trace, wrote artifacts, exited cleanly, and printed `NoProcessRunning=ui_api_testbed.exe`. |
| 9 | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `72fbd13` plus probe-script reliability fix in the working tree | Final default idle acceptance: 1280x800 visible dark Testbed tab, D3D11 VSYNC, debug layer on, 3s warmup and 5s trace capture | Trace `build\perf\ui_api_testbed\step_08_debug_idle_trace_20260501_201624.json`; stdout `step_08_debug_idle_probe_20260501_201624.txt`; summary `step_08_debug_idle_summary_20260501_201624.txt`; sampled-profile review `step_09_sampled_profile_review_20260501_201624.txt` | 0.03% over 5010.91 ms, 0 rendered frames, 0.00 FPS | Summary: `frame` count 1 mean/p95 5010.587/5010.587 ms, `idle_wait` count 1 mean/p95 5010.456/5010.456 ms, `pump_messages` count 1 mean/p95 0.035/0.035 ms, commands/text averages 0.00. Sampled profile review: Step 1 profile had `run_windowed` 96.59%, `build_ui_commands` 88.63%, `gui::render_frame` 50.23%, text advance chain about 47%, text raster chain about 32%, layout about 24.75%. | Final debug idle target accepted on this machine. The app is waiting during unchanged idle, so the old hot continuous-redraw stack is gone from the acceptance window. |
| 9a | 2026-05-01 | `windows-msvc-debug` `ui_api_testbed`, commit `72fbd13` plus probe-script reliability fix in the working tree | Final active interaction probe: 1280x800 visible dark Testbed tab, D3D11 VSYNC, debug layer on, 1s warmup and 5s trace capture while the script moved the pointer and posted client mouse move/click messages | Trace `build\perf\ui_api_testbed\step_08_debug_mouse_trace_20260501_201624.json`; stdout `step_08_debug_mouse_probe_20260501_201624.txt`; summary `step_08_debug_mouse_summary_20260501_201624.txt` | 3.94% over 5021.90 ms, 116 rendered frames, 23.10 FPS | Mouse activity: 197 moves, 7 clicks. Means/p95: `ui_build` 16.756/36.024 ms, `draw_command_recording` 14.160/31.484 ms, `gui_render_frame` 13.638/30.432 ms, `draw_render_commands_to_window` 2.491/6.095 ms, `present` 0.316/0.739 ms, `input_handling` 0.030/0.089 ms. Commands/text averages 175.00/78.00. | A first rerun using only global cursor movement produced 0 rendered frames because foreground/z-order blocked the synthetic input. The probe now posts targeted client mouse messages to the testbed window, so it validates the idle-wait redraw path reliably and no longer clicks unrelated foreground windows. |
| 9b | 2026-05-01 | `windows-msvc-release` `ui_api_testbed`, commit `72fbd13` plus probe-script reliability fix in the working tree | Release comparison: visible idle after 3s warmup, 10.02s CPU sample, launched with `--no-d3d-debug-layer` | CPU artifact `build\perf\ui_api_testbed\step_09_release_idle_cpu_20260501_201911.txt` | 0.00% over 10.02s, 12 logical processors | No trace; release tracing is debug-only. | Release comparison is also idle-wait bound. |

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

Step 5 commands:

- Baseline rebuild before code change:
  `.\build.bat windows-msvc-debug ui_api_testbed`.
- Active-redraw before trace:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_05_active_trace_before_theme_cache.json --trace-warmup-ms 1000 --trace-duration-ms 5000`
  while a PowerShell `SetCursorPos` loop moved the mouse over the window every
  16 ms.
- Before summary:
  `build\perf\ui_api_testbed\step_05_active_trace_before_theme_cache.summary.txt`.
- Format check after code change:
  `clang-format --dry-run --Werror src\gui\src\gui.cpp`.
- Build after code change:
  `.\build.bat windows-msvc-debug ui_api_testbed`.
- Active-redraw after trace:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_05_active_trace_after_selectable_pointer_gate.json --trace-warmup-ms 1000 --trace-duration-ms 5000`
  with the same synthetic mouse movement loop.
- After summary:
  `build\perf\ui_api_testbed\step_05_active_trace_after_selectable_pointer_gate.summary.txt`.
- Sampled profile attempts after code change:
  DiagnosticsHub CPU Usage Low attach to `ui_api_testbed.exe` and launch mode
  both failed with "Value does not fall within the expected range"; `stop`
  reported the session did not exist and no `.diagsession` was produced.
- Focused shared GUI validation:
  `.\build.bat windows-msvc-debug gui_tests`, then
  `build\windows-msvc-debug\Debug\gui_tests.exe`. The selectable-label cases
  passed, but the all-in-one executable returned 1 because
  `tab_view_adds_closes_and_moves_app_owned_tabs`,
  `table_desc_sorts_rows_by_cell_text`, and
  `dense_controls_panel_renders_only_batchable_styled_rects_without_font`
  failed.

Step 6 commands:

- Baseline rebuild: `.\build.bat windows-msvc-debug ui_api_testbed`.
- Active-redraw before traces:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_06_active_trace_before_debug_layer_on.json --trace-warmup-ms 1000 --trace-duration-ms 5000`
  and the same command with `--no-d3d-debug-layer` writing
  `step_06_active_trace_before_debug_layer_off.json`, while a PowerShell
  `SetCursorPos` loop moved the mouse over the window every 16 ms.
- Rejected draw-layer pipeline/scissor tracking traces:
  `step_06_active_trace_after_debug_layer_on.json` and
  `step_06_active_trace_after_debug_layer_off.json`.
- Rejected draw-layer sampler-once traces:
  `step_06_active_trace_after_sampler_debug_layer_on.json` and
  `step_06_active_trace_after_sampler_debug_layer_off.json`.
- Rejected D3D11 begin-pass unbind deletion traces:
  `step_06_active_trace_after_begin_unbind_delete_debug_layer_on.json` and
  `step_06_active_trace_after_begin_unbind_delete_debug_layer_off.json`.
- Each trace was summarized into a matching `.summary.txt` file by pairing
  Chrome Trace `B`/`E` zone events and computing mean/p95 timings.
- Sampled profile review:
  `rg -n -i "draw::|render_commands|render_d3d11|bind_group" build\perf\ui_api_testbed\step_01_debug_idle_top_inclusive_functions.txt build\perf\ui_api_testbed\step_01_debug_idle_xperf_profile_detail_merged.txt build\perf\ui_api_testbed\step_01_debug_idle_top_modules.txt`.
  The profile detail listed `render_commands_to_window` and D3D11 bind helpers
  near 0.00%, so no new sampled renderer hotspot was found.
- Final source rebuild after reverting rejected probes:
  `.\build.bat windows-msvc-debug ui_api_testbed`.

Step 7 commands:

- Profile evidence reviewed before changing:
  `build\perf\ui_api_testbed\step_01_debug_idle_top_inclusive_functions.txt`
  and `step_01_debug_idle_xperf_profile_detail_merged.txt`, which put
  `next_text_line`/`text_advance` on the hot `gui::render_frame` stack.
- Format check: `clang-format --dry-run --Werror src\gui\src\gui.cpp`.
- Build: `.\build.bat windows-msvc-debug ui_api_testbed`.
- Focused shared GUI validation: `.\build.bat windows-msvc-debug gui_tests`,
  then `build\windows-msvc-debug\Debug\gui_tests.exe`. The wrapping and
  selectable-label cases passed, but the all-in-one executable still returned 1
  for the same unrelated
  `tab_view_adds_closes_and_moves_app_owned_tabs`,
  `table_desc_sorts_rows_by_cell_text`, and
  `dense_controls_panel_renders_only_batchable_styled_rects_without_font`
  failures documented in Step 5.
- Active-redraw after trace:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_07_active_trace_after_wrap_search.json --trace-warmup-ms 1000 --trace-duration-ms 5000`
  while a PowerShell `SetCursorPos` loop moved the mouse over the window every
  16 ms.
- After summary:
  `build\perf\ui_api_testbed\step_07_active_trace_after_wrap_search.summary.txt`.
- Sampled profile attempt:
  DiagnosticsHub CPU Usage Low attach to the active `ui_api_testbed.exe`
  scenario wrote
  `build\perf\ui_api_testbed\step_07_active_sample_after_wrap_search.stdout.txt`
  but failed to create a session with "Value does not fall within the expected
  range"; no
  `build\perf\ui_api_testbed\step_07_active_sample_after_wrap_search.diagsession`
  was produced.

Step 8 commands:

- Canonical idle probe:
  `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1`.
- Canonical active mouse/click probe:
  `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario mouse`.
- Combined probe:
  `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario both`.
- The probe internally ran:
  `.\build.bat windows-msvc-debug ui_api_testbed`, then
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_08_debug_idle_trace_<timestamp>.json --trace-warmup-ms 3000 --trace-duration-ms 5000`.
- The active mouse/click probe internally ran:
  `build\windows-msvc-debug\Debug\ui_api_testbed.exe --trace build\perf\ui_api_testbed\step_08_debug_mouse_trace_<timestamp>.json --trace-warmup-ms 1000 --trace-duration-ms 5000`, then moved the pointer over the window roughly every 16 ms and clicked every 30 moves until the trace exited.
  In Step 9 the probe was tightened to post `WM_MOUSEMOVE`,
  `WM_LBUTTONDOWN`, and `WM_LBUTTONUP` directly to the testbed window after
  converting each screen point to client coordinates. This keeps the active
  probe valid when foreground focus or z-order blocks global cursor input.
- Idle validation run output:
  `ui_api_testbed trace: wrote D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\step_08_debug_idle_trace_20260501_200937.json frames=0 duration_ms=5013.7 fps=0.0 process_cpu=0.00% commands_avg=0.0 max=0 primitive_avg=0.0 styled_rect_avg=0.0 text_avg=0.0 layers_avg=0.0`.
- Idle parsed summary:
  `build\perf\ui_api_testbed\step_08_debug_idle_summary_20260501_200937.txt`
  with `summary duration_ms=5013.67 cpu=0.00 frames=0 fps=0.00 commands_avg=0.00 text_avg=0.00`.
- Active mouse/click validation run output:
  `ui_api_testbed trace: wrote D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\step_08_debug_mouse_trace_20260501_200855.json frames=187 duration_ms=5007.4 fps=37.3 process_cpu=1.61% commands_avg=175.7 max=176 primitive_avg=29.0 styled_rect_avg=87.7 text_avg=78.0 layers_avg=0.0`.
- Active parsed summary:
  `build\perf\ui_api_testbed\step_08_debug_mouse_summary_20260501_200855.txt`
  with `MouseMoves=212`, `MouseClicks=8`, and `summary duration_ms=5007.39 cpu=1.61 frames=187 fps=37.34 commands_avg=175.72 text_avg=78.00`.
- Process cleanup check:
  the probe printed `NoProcessRunning=ui_api_testbed.exe`, and a separate
  `Get-Process ui_api_testbed -ErrorAction SilentlyContinue` returned no
  process.
- Sampled profile capture remains manual because DiagnosticsHub/xperf were
  previously flaky on this machine. Use the Step 1 DiagnosticsHub/xperf command
  sequence when a new stack artifact is needed, save raw/exported artifacts
  under `build\perf\ui_api_testbed\`, and keep any profiler failure stdout as a
  comparison artifact.

Step 9 final acceptance commands:

- Canonical combined debug probe:
  `powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario both`.
- Idle trace output:
  `ui_api_testbed trace: wrote D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\step_08_debug_idle_trace_20260501_201624.json frames=0 duration_ms=5010.9 fps=0.0 process_cpu=0.03% commands_avg=0.0 max=0 primitive_avg=0.0 styled_rect_avg=0.0 text_avg=0.0 layers_avg=0.0`.
- Idle parsed summary:
  `build\perf\ui_api_testbed\step_08_debug_idle_summary_20260501_201624.txt`
  with `summary duration_ms=5010.91 cpu=0.03 frames=0 fps=0.00 commands_avg=0.00 text_avg=0.00`.
- Active interaction output after the probe reliability fix:
  `ui_api_testbed trace: wrote D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\step_08_debug_mouse_trace_20260501_201624.json frames=116 duration_ms=5021.9 fps=23.1 process_cpu=3.94% commands_avg=175.0 max=175 primitive_avg=29.0 styled_rect_avg=87.0 text_avg=78.0 layers_avg=0.0`.
- Active parsed summary:
  `build\perf\ui_api_testbed\step_08_debug_mouse_summary_20260501_201624.txt`
  with `MouseMoves=197`, `MouseClicks=7`, and
  `summary duration_ms=5021.90 cpu=3.94 frames=116 fps=23.10 commands_avg=175.00 text_avg=78.00`.
- Sampled-profile review artifact:
  `build\perf\ui_api_testbed\step_09_sampled_profile_review_20260501_201624.txt`.
- Release comparison build:
  `.\build.bat windows-msvc-release ui_api_testbed`.
- Release comparison CPU sample:
  launched
  `build\windows-msvc-release\Release\ui_api_testbed.exe --no-d3d-debug-layer`,
  warmed for 3s, sampled process CPU for 10.02s, and saved
  `build\perf\ui_api_testbed\step_09_release_idle_cpu_20260501_201911.txt`
  with `ProcessCpuPercent=0`.
- Process cleanup check:
  the combined debug probe printed `NoProcessRunning=ui_api_testbed.exe`.

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
- 2026-05-01: Step 5 active-redraw trace kept `ui_build` worth optimizing, but
  not `theme_setup`: synthetic mouse redraws measured `draw_ui` at 2.351 ms
  mean before the change and `theme_setup` at only 0.020 ms. The sampled Step 1
  stack mapped the `draw_ui` cost to selectable-label pointer hit testing, so
  `src\gui\src\gui.cpp` now computes the mouse text index only during actual
  pointer selection.
- 2026-05-01: Step 6 kept no draw/render source change. Active trace still
  measured renderer submission, but compact redundant-state candidates did not
  reduce `draw_render_commands_to_window`, and the sampled profile detail kept
  draw/render submission functions near zero. Keep the next optimization focus
  on measured `gui_render_frame` text/font/layout work unless a new trace shows
  a different draw/render hotspot.
- 2026-05-01: Step 7 optimized only the measured text/layout path. The sampled
  stack had `next_text_line` and `text_advance` on the hot `gui::render_frame`
  path, so wrapped text now searches for the first overflowing prefix instead
  of measuring every prefix. No persistent cache was added.
- 2026-05-01: Step 8 added `scripts\ui_api_testbed_perf_probe.ps1` as the
  canonical repeatable debug idle and active mouse/click probe. It records
  trace/stdout/summary artifacts and a process cleanup check, but deliberately
  does not encode a pass/fail threshold because the numbers are
  machine-dependent.
- 2026-05-01: Step 9 accepted the default debug idle target on this machine:
  final canonical idle measured 0.03% CPU, 0 rendered frames, and a single long
  `idle_wait` over 5.01s. The probe's active interaction path now posts client
  mouse messages directly to the testbed window so it is not dependent on
  foreground focus.

## Current Best Diagnosis

The debug idle target is accepted on this machine. The final 5s default idle
trace after warmup recorded 0 rendered frames and 0.03% process CPU, down from
Step 3's 350 frames and 8.57% CPU with the D3D debug layer on. The release
comparison with `--no-d3d-debug-layer` measured 0.00% over 10.02s.

Step 5 confirms active redraw still has meaningful per-frame CPU work after the
idle gate. The first measured UI-build cleanup reduced `draw_ui` from 2.351 ms
mean to 0.225 ms mean and reduced `ui_build` from 14.068 ms mean to 11.475 ms
mean in the synthetic active-redraw trace.

Step 7 reduced the remaining measured text/layout path without a persistent
cache. Replacing the wrapped text per-character prefix scan reduced active
`ui_build` from Step 6's 11.719 ms mean to 7.062 ms mean, `end_ui_frame` from
3.930 ms mean to 0.786 ms mean, and `gui_render_frame` from 7.208 ms mean to
5.691 ms mean. Command volume did not change: 175 commands, 78 text commands,
and 87 styled rects.

Step 6 confirms draw/render submission is measurable but not the best next
target. The active redraw trace measured `draw_render_commands_to_window` at
1.112 ms mean with the D3D11 debug layer and 0.538 ms without it, while
`gui_render_frame` stayed around 7.2-7.3 ms. Removing redundant draw-layer
pipeline/scissor submissions, binding the sampler once, and deleting begin-pass
D3D11 unbind calls did not improve the trace, so those source changes were
reverted.

Theme setup is not a current target. Step 5 measured it at about 0.020 ms mean
before and after the change, so caching theme/spec rebuilds is not worth adding
code for now.

Step 8 makes the current default idle measurement repeatable with one command:
`powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1`.
Active mouse/click measurement is repeatable with
`powershell -ExecutionPolicy Bypass -File .\scripts\ui_api_testbed_perf_probe.ps1 -Scenario mouse`.
Use the emitted trace JSON, probe stdout, parsed summary, mouse move/click
counts, and process-cleanup line as the canonical comparison artifacts for
future regression checks.

## Remaining Risks And Optional Work

- If future UI work adds animation or timers, it must add an explicit local
  redraw request; the current idle policy correctly sleeps when state and input
  are unchanged.
- If active interaction still feels expensive, profile the remaining
  `gui_render_frame` text raster/text command volume with a working sampler
  before changing it. The final active probe validates redraw wakeups, but its
  timings are synthetic and more variable than the idle acceptance metric.
- DiagnosticsHub and xperf were blocked for after sampled profiles in this
  session; retry from an elevated profiler shell only if a new post-gate stack
  artifact is required.
- Investigate the current unrelated `gui_tests.exe` failures before relying on
  full GUI test executable success as a validation signal.

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
