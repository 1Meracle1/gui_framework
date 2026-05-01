# UI API Testbed Liquid Glass Context

Paste this context at the start of every new chat that works on the gradual
Liquid Glass migration for `ui_api_testbed`.

## Goal

Improve `tools/ui_api_testbed.cpp` until it feels like a polished iOS/macOS 26
style application using Liquid Glass principles.

Keep the implementation incremental. Each chat should make one bounded visual
improvement, run the actual `ui_api_testbed`, capture a screenshot, inspect the
screenshot, fix visible problems, and only finish after the result is validated.

## Project Constraints

- Workspace: `D:\dev\cpp\gui_framework`.
- Primary file for this migration: `tools/ui_api_testbed.cpp`.
- Do not make framework-level API changes unless the current prompt explicitly
  requires it.
- Keep changes small and local.
- Prefer existing `gui` and `draw` primitives: style roles, colors, borders,
  radius, shadows, layout spacing, and simple backdrop drawing.
- Do not add external dependencies.
- Use `clang-format` on changed C++ files.
- Preserve the custom immediate-mode UI API style already used in the file.

## Design Direction

Use Apple Liquid Glass guidance as the design target:

- Liquid Glass belongs mainly on functional layers: navigation, toolbar,
  controls, popups, and modal chrome.
- Content areas should remain legible and calmer, closer to standard material.
- Use the effect sparingly. Avoid making every nested content box highly
  transparent.
- Prefer soft translucent fills, clear borders, larger rounded shapes, and
  restrained shadows.
- Keep reflections and highlights much lower than `tools/liquid_glass_testbed.cpp`.
- Clear glass should appear only over visually rich backdrop areas.
- Prioritize readable text and predictable app layout over glass spectacle.

## Current Visual Baseline

The current attached screenshot shows:

- A dark translucent app with a multicolor geometric backdrop.
- A full-width top header with `UI API Testbed` and a `Reset` button.
- A rounded tab strip below the header.
- A left sidebar with a virtualized asset list.
- A top controls row with checkbox, toggle, slider, read-only checkbox, input,
  disabled button, popup button, and modal button.
- Content panels for body text, preview, table, popup, and log.

The baseline is readable, but still feels closer to a dark demo panel than a
great macOS/iOS 26 app. The biggest visual opportunities are hierarchy, polish,
material restraint, spacing, and component-specific treatment.

## Validation Contract

Every migration step must do all of the following before final response:

1. Build the focused target:

   ```bat
   .\build.bat windows-msvc-debug ui_api_testbed
   ```

2. Run the actual app, not a mocked UI. Prefer launching the built executable in
   the background so the chat can capture and close it:

   ```powershell
   $exe = "D:\dev\cpp\gui_framework\build\windows-msvc-debug\Debug\ui_api_testbed.exe"
   $p = Start-Process -FilePath $exe -PassThru
   Start-Sleep -Seconds 2
   ```

3. Capture a screenshot of the application window itself. Save screenshots
   under:

   ```text
   D:\dev\cpp\gui_framework\build\ui_api_testbed_screens\
   ```

   Use this PowerShell pattern when the validation needs to manipulate the UI
   before capture. The click coordinates are client-area pixels relative to the
   top-left of the `ui_api_testbed` window. This example switches to the
   `Samples` tab, captures only the app window, closes the app, and prints the
   screenshot path:

   ```powershell
   $exe = "D:\dev\cpp\gui_framework\build\windows-msvc-debug\Debug\ui_api_testbed.exe"
   $dir = "D:\dev\cpp\gui_framework\build\ui_api_testbed_screens"
   New-Item -ItemType Directory -Force -Path $dir | Out-Null
   $path = Join-Path $dir "samples_tab_XX_window.png"

   $p = Start-Process -FilePath $exe -PassThru
   Start-Sleep -Seconds 2
   $p.Refresh()

   Add-Type @"
   using System;
   using System.Runtime.InteropServices;
   public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
   public static class User32UiApiTestbedCapture {
       [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
       [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
       [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
   }
   "@

   $hwnd = $p.MainWindowHandle
   [User32UiApiTestbedCapture]::SetForegroundWindow($hwnd) | Out-Null
   Start-Sleep -Milliseconds 300

   $click_x = 170
   $click_y = 75
   $lparam = [IntPtr](($click_y -shl 16) -bor $click_x)
   [User32UiApiTestbedCapture]::SendMessageW($hwnd, 0x0201, [IntPtr]1, $lparam) | Out-Null
   Start-Sleep -Milliseconds 160
   [User32UiApiTestbedCapture]::SendMessageW($hwnd, 0x0202, [IntPtr]0, $lparam) | Out-Null
   Start-Sleep -Milliseconds 800

   $rect = New-Object RECT
   [User32UiApiTestbedCapture]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
   Add-Type -AssemblyName System.Drawing
   $width = $rect.Right - $rect.Left
   $height = $rect.Bottom - $rect.Top
   $bmp = New-Object System.Drawing.Bitmap $width, $height
   $gfx = [System.Drawing.Graphics]::FromImage($bmp)
   $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($width, $height)))
   $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
   $gfx.Dispose()
   $bmp.Dispose()

   if ($p -and !$p.HasExited) { Stop-Process -Id $p.Id }
   $path
   ```

   `SendMessageW` is preferred for testbed clicks because it sends mouse input
   directly to the app window. Use `0x0201` for `WM_LBUTTONDOWN` and `0x0202`
   for `WM_LBUTTONUP`. The screenshot must show the actual running
   `ui_api_testbed` window.

4. Inspect the screenshot before finishing. Use visual inspection tools
   available in the chat, such as `view_image`, when a file path screenshot is
   produced.

5. Repeat edit/build/run/screenshot if any of these are visible:

   - Text overlaps, clips, or becomes hard to read.
   - The toolbar does not fit at the default `1280x800` window size.
   - Important controls disappear into glass.
   - The UI becomes noisy from too much transparency or reflection.
   - The screenshot is blank, stale, or not the actual app.

6. Close the app after capturing, for example:

   ```powershell
   if ($p -and !$p.HasExited) { Stop-Process -Id $p.Id }
   ```

7. Final response must include:

   - Changed files.
   - Screenshot path.
   - What was visually validated.
   - Commands/checks run.

## Minimum Checks

Run at least:

```bat
clang-format --dry-run --Werror tools\ui_api_testbed.cpp
.\build.bat windows-msvc-debug ui_api_testbed
```

If the step touches shared framework code, also run the smallest relevant test
target.
