# Repro Scripts

This directory holds Windows GUI repro scripts that drive the spawned
application by HWND instead of global keyboard injection.

## Current Script

- `code_editor_symbols_repro.ps1`

This script reproduces the `code_editor` file-symbol flow used for the
`Space+f`, open-file, `Space+s` crash investigation.

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
7. Prints a JSON summary with status, phase, HWND, and artifact paths.

Artifacts are written under `_codex_artifacts\code_editor_symbols_repro\` by
default.

## Extension Rules

- Start from this script when a Windows GUI repro needs deterministic input.
- Keep input targeted at the window HWND. Do not switch to global `SendKeys`,
  desktop automation, or physical keyboard simulation unless the app ignores
  window messages and you have already documented that failure.
- Use `Send-Key` for non-text command keys such as `Space`, `Enter`, arrows,
  and modifiers.
- Use `Send-Text` and `Send-Char` for literal text entry.
- Add a screenshot after each state transition that must be proven later.
- Keep the command sequence in the `try` block linear and obvious. Do not
  introduce a generic action framework unless multiple repros actually need it.
- If timing is unstable, adjust the nearby `Start-Sleep` call instead of
  scattering retries everywhere.

## Learnings

- Sending printable characters through both key messages and character messages
  can duplicate text. `WM_CHAR` alone was the stable choice for typed text.
- A direct HWND target is much more reliable than global key injection for
  repeatable bug reports and avoids interfering with the rest of the desktop.
- A GUI process launched without a console can still have redirected
  `stdout` and `stderr`, but those streams may legitimately stay empty.
- Screenshots are necessary. A surviving process is not enough; the UI state
  must show that the requested panel or command actually appeared.
