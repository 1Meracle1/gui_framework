# Repro Scripts

This directory holds Windows GUI repro scripts that drive the spawned
application by HWND instead of global keyboard injection.

## Shared Helper

- `repro_common.ps1`

This helper owns process launch, HWND discovery, focused resize, targeted
keyboard/mouse/text messages, screenshots, left-panel crops, stdout/stderr
capture, frame dump copying, simple frame assertions, and JSON summaries.

Scenario scripts should dot-source the helper and keep only the bug-specific
steps.

## Current Scripts

- `code_editor_symbols_repro.ps1`
- `code_editor_git_panel_repro.ps1`
- `code_editor_text_rendering_repro.ps1`

## Usage

Build the debug executable first:

```powershell
.\build.bat windows-msvc-debug code_editor
```

Run the repro with defaults:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repro\code_editor_symbols_repro.ps1
```

Optional parameters:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repro\code_editor_symbols_repro.ps1 `
    -workspace_path D:\dev\cpp\gui_framework `
    -exe_path D:\dev\cpp\gui_framework\build\windows-msvc-debug\Debug\code_editor.exe `
    -artifact_dir D:\dev\cpp\gui_framework\_codex_artifacts\custom_repro `
    -open_file examples\code_editor\editor_render.cpp
```

## What The Script Does

1. Launches `code_editor.exe` with `UseShellExecute = $false`,
   `CreateNoWindow = $true`, and redirected `stdout` and `stderr`.
2. Resolves the top-level visible HWND for the spawned PID.
3. Focuses and sizes that window before sending input.
4. Sends command keys with `WM_KEYDOWN` and `WM_KEYUP`.
5. Sends typed text with `WM_CHAR`.
6. Saves milestone screenshots and writes `stdout.txt` and `stderr.txt`.
7. Copies the latest debug frame dump at milestones and asserts expected boxes
   or text markers.
8. Writes `summary.json` and prints the same JSON summary with status, phase,
   HWND, and artifact paths.

Artifacts are written under `_codex_artifacts\code_editor_symbols_repro\` by
default.

## Extension Rules

- Start from an existing scenario script when a Windows GUI repro needs
  deterministic input.
- Put shared launch/input/capture behavior in `repro_common.ps1`, not in a
  scenario script.
- Keep input targeted at the window HWND. Do not switch to global `SendKeys`,
  desktop automation, or physical keyboard simulation unless the app ignores
  window messages and you have already documented that failure.
- Use `Send-ReproKey` for non-text command keys such as `Space`, `Enter`,
  arrows, and modifiers.
- Use `Send-ReproText` and `Send-ReproChar` for literal text entry.
- Add a screenshot after each state transition that must be proven later.
- Add a frame dump and assertion for each state transition that has a stable
  `debug_name` or text marker. Use screenshots as evidence, not the primary
  success signal.
- Keep the command sequence in the `try` block linear and obvious. Do not
  introduce more scenario abstraction unless multiple repros actually need it.
- If timing is unstable, adjust the nearby `Start-Sleep` call instead of
  scattering retries everywhere.

## Frame Dumps

Debug Windows builds of `code_editor` support:

```powershell
code_editor.exe --automation-dump-frame <path> [path]
```

The host rewrites the dump after each rendered frame. Scenario scripts copy it
to milestone files such as `02_file_search_frame.json` before running
assertions.

## Learnings

- Sending printable characters through both key messages and character messages
  can duplicate text. `WM_CHAR` alone was the stable choice for typed text.
- A direct HWND target is much more reliable than global key injection for
  repeatable bug reports and avoids interfering with the rest of the desktop.
- A GUI process launched without a console can still have redirected
  `stdout` and `stderr`, but those streams may legitimately stay empty.
- Screenshots are necessary. A surviving process is not enough; the UI state
  must show that the requested panel or command actually appeared.
