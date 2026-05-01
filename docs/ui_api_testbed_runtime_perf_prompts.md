# UI API Testbed Runtime Performance Prompts

For each step, start a new chat and paste:

1. The full contents of `docs/ui_api_testbed_runtime_perf_context.md`.
2. Exactly one prompt from this file.

After every step, update `docs/ui_api_testbed_runtime_perf_context.md` with the
new measurement entry, findings, decisions, and next hypothesis ranking. Commit
after a step only when the user asks for that chat to commit.

## Step 1 - Baseline Sampling Profile

```text
Using the shared runtime performance context, establish a reliable baseline
profile for Windows MSVC debug `ui_api_testbed`.

Goal: capture a sampled CPU flamegraph or equivalent call-stack profile before
optimizing.

Scope:
- build `ui_api_testbed` with `.\build.bat windows-msvc-debug ui_api_testbed`
- run the actual executable at the default 1280x800 window size
- capture a sampled profile after warmup using an installed profiler if
  available: Visual Studio CPU Usage, Windows Performance Recorder/WPA, xperf,
  Superluminal, or another local call-stack sampler with symbols
- export or save the artifact under
  `D:\dev\cpp\gui_framework\build\perf\ui_api_testbed\`
- record the top inclusive stacks/functions and whether symbols resolved
- also record idle process CPU after a short warmup for comparison, but do not
  use CPU percentage alone as diagnosis
- inspect the current run loop and identify the narrowest places to add
  engine-style debug trace zones next
- update `docs/ui_api_testbed_runtime_perf_context.md` with the artifact path,
  profiler command/tool, CPU outcome, and top hypotheses

Do not optimize in this step. Only edit docs if profiling notes need to be
recorded. If no sampler is available, document that explicitly and make Step 2
the source of detailed manual trace data.
```

## Step 2 - Debug-Only Manual Trace Infrastructure

```text
Using the shared runtime performance context, add minimal opt-in debug-only
manual tracing to `ui_api_testbed`.

Goal: get engine-style timeline zones detailed enough to explain the sampled
profile without changing normal app behavior.

Scope:
- keep tracing local to `tools/ui_api_testbed.cpp` unless the smallest useful
  debug-only helper clearly belongs in `src/base`
- compile tracing only for debug builds or keep it inert in release
- add command-line gated tracing, off by default
- prefer simple zone macros or scope helpers in the style used by engines:
  `TRACE_SCOPE("ui_build")`, `TRACE_BEGIN/TRACE_END`, or similarly compact code
- use Windows high-resolution timing directly on the Windows path
- emit Chrome Trace JSON or another timeline format that can be loaded in
  Perfetto, Chrome tracing, or Speedscope
- collect nested zones for message pump, resize, input handling, theme setup,
  UI build, draw command recording, render submission, present, and idle/wait
  time if present
- include process CPU percentage over the trace window, normalized like Task
  Manager process CPU percentage across all logical processors
- include draw command counts already exposed by `gui::draw`
- support an auto-exit trace run if that is the smallest way to collect repeatable
  data
- print compact summaries with `fmt::printf`
- update the context file with how to run the trace and any overhead caveats

Validation:
- `clang-format --dry-run --Werror tools\ui_api_testbed.cpp`
- `.\build.bat windows-msvc-debug ui_api_testbed`
- run one traced idle sample, save the trace artifact, and record the output in
  the context file
```

## Step 3 - Correlate Sampled Profile And Manual Trace

```text
Using the shared runtime performance context and the tracing added in the
previous step, collect a manual trace and correlate it with the sampled profile.

Goal: decide what to optimize first from stack samples plus explicit stage
timings.

Scope:
- run a traced Windows MSVC debug idle sample after warmup
- capture or review a sampled CPU profile for the same scenario
- if tracing supports it, compare D3D debug layer on versus off; otherwise
  inspect the code and plan the smallest temporary or command-line switch needed
- record process CPU, frame count/FPS, stage mean/p95 timings, draw command
  counts, and top sampled stacks
- identify whether the dominant cost is continuous redraw frequency, a specific
  CPU stage, present/debug-layer behavior, or command/text volume
- update the context file with the measured ranking

Do not make broad optimizations in this step. A tiny measurement-only adjustment
is acceptable if it is needed to get trustworthy data.
```

## Step 4 - Idle Redraw And Message Wait Policy

```text
Using the shared runtime performance context, implement the smallest idle redraw
policy that the trace data supports.

Goal: stop burning CPU while the `ui_api_testbed` window is idle, without
breaking input, resize, popups, modal state, theme switching, scroll, or text
editing.

Scope:
- keep the first implementation local to `tools/ui_api_testbed.cpp`
- redraw immediately after input messages, resize, and state changes that the
  local app can observe
- sleep or wait for messages when no redraw is needed
- do not add a framework-level invalidation API unless local gating proves
  impossible
- keep tracing active enough to compare before/after stack profiles, timelines,
  idle CPU, and frame count
- update the context file with the new redraw policy and measured effect

Validation:
- `clang-format --dry-run --Werror tools\ui_api_testbed.cpp`
- `.\build.bat windows-msvc-debug ui_api_testbed`
- sampled profile or flamegraph before/after comparison
- traced idle timeline before/after comparison
- CPU outcome before/after comparison
- quick manual interaction pass: move mouse, click controls, scroll lists, type
  in an input, open popup/modal, resize the window
```

## Step 5 - Theme And UI Build Hotspot Cleanup

```text
Using the shared runtime performance context, optimize only measured CPU costs in
theme setup or UI build.

Goal: reduce per-redraw CPU time after idle rendering has been addressed.

Scope:
- use trace data to confirm this area is still worth optimizing
- use sampled stacks to confirm the trace hotspot maps to real CPU time
- likely candidates include avoiding per-frame theme/spec rebuilds unless the
  theme changes, reducing repeated local style setup, or removing obviously
  redundant work in `draw_ui`
- keep the code smaller or equally simple after the change
- avoid framework API changes unless a local fix would be uglier and the trace
  proves the shared path is hot
- update the context file with before/after timings and any rejected candidates

Validation:
- `clang-format --dry-run --Werror` over changed C++ files
- `.\build.bat windows-msvc-debug ui_api_testbed`
- run the traced scenario and sampled profile that showed the hotspot
- if shared gui code changes, run the smallest relevant gui tests
```

## Step 6 - Draw Or Renderer Submission Hotspot Cleanup

```text
Using the shared runtime performance context, optimize the draw/render stage only
if tracing shows it remains a meaningful CPU cost.

Goal: reduce renderer submission overhead without changing visuals.

Scope:
- inspect `src/draw` and `src/render` only around measured hot paths
- prefer deleting or tightening redundant work over adding caches
- if additional instrumentation is needed, keep it opt-in and compact
- do not change backend behavior speculatively
- verify D3D11 debug-layer behavior separately from app CPU work
- update the context file with before/after timings

Validation:
- `clang-format --dry-run --Werror` over changed C++ files
- `.\build.bat windows-msvc-debug ui_api_testbed`
- run the traced scenario and sampled profile that showed the hotspot
- run focused draw/render tests if shared code changed
```

## Step 7 - Text, Font, Table, Or List Hotspot Cleanup

```text
Using the shared runtime performance context, optimize text/font/table/list work
only if tracing identifies it as a remaining hot path.

Goal: reduce repeated idle or redraw work in the UI layer without weakening the
immediate-mode API.

Scope:
- inspect only the measured path: text measurement, font cache, virtual list,
  table sorting/filtering, or layout
- require sampled stacks or trace zones to point at that path before changing it
- avoid persistent caches unless the repeated cost is proven and the lifetime is
  obvious
- add focused tests if shared behavior changes
- keep visual output and interaction behavior stable
- update the context file with before/after timings and any new risks

Validation:
- `clang-format --dry-run --Werror` over changed C++ files
- `.\build.bat windows-msvc-debug ui_api_testbed`
- run the traced scenario and sampled profile that showed the hotspot
- run focused gui/font/draw tests if shared code changed
```

## Step 8 - Repeatable Profiling Probe

```text
Using the shared runtime performance context, make the performance measurement
and profiling flow repeatable enough for future regression checks.

Goal: avoid relying on memory of manual Task Manager readings or one-off
profiler sessions.

Scope:
- prefer the smallest existing mechanism: command-line trace mode, documented
  profiler commands, or a tiny script if that is much easier to reuse
- capture or document how to capture a sampled profile/flamegraph
- capture a manual trace artifact
- measure default idle debug behavior after warmup
- print or document the exact artifacts and metrics needed for comparison
- do not create a benchmark that claims pass/fail stability across machines
- update the context file with the canonical command and expected output shape

Validation:
- run the canonical probe at least once
- confirm the app starts, profiles/traces, exits, and leaves no process running
```

## Step 9 - Final Acceptance Pass

```text
Using the shared runtime performance context, perform the final performance
acceptance pass.

Goal: confirm the current implementation reaches the debug idle CPU target or
document the remaining measured blocker precisely.

Scope:
- run the canonical debug idle measurement
- run or review one sampled profile/flamegraph
- run one traced idle sample and save the artifact
- run a short manual interaction pass after any idle-wait optimization
- run release measurement for comparison if cheap
- inspect the context file and remove stale hypotheses that the data disproved
- keep only useful tracing knobs; delete temporary-only instrumentation
- update the context with final metrics, remaining risks, and next optional work

Validation:
- `clang-format --dry-run --Werror` over changed C++ files
- `.\build.bat windows-msvc-debug ui_api_testbed`
- smallest relevant tests for any shared code changed
- final sampled-profile summary, trace summary, and debug idle CPU outcome
  recorded in context
```
