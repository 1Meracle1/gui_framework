# gui_framework

`gui_framework` is a custom GPU-oriented GUI framework written in C++20. It is
not a wrapper around native platform widgets. The project owns its low-level
memory model, text stack, draw command generation, renderer abstraction, and
immediate-mode GUI API.

The active runtime target is Windows with MSVC and Direct3D. macOS Clang
presets are kept in the build matrix, but the full renderer/window/font path is
not at Windows parity yet.

## Project Goals

- Small explicit C++20 APIs with no exceptions, RTTI, or compiler extensions.
- Arena-backed allocation and visible ownership boundaries.
- GPU-friendly draw and UI command streams.
- A renderer layer that can host D3D11, D3D12, and later non-Windows backends.
- No dependency fetching from CMake. Approved dependencies are vendored under
  `third_party/`.
- Practical examples that double as development testbeds.
- No-dependency test and benchmark harnesses.

## Current Status

The project has moved beyond the initial scaffold, but it is still an
experimental framework rather than a stable application toolkit.

| Area | Status |
| --- | --- |
| Base layer | Arena allocation, string/slice helpers, formatting, IO, unicode helpers, virtual memory, asserts, crash handling, and arena-backed containers are present. |
| Encoding | A compact JSON parser is available for config and tooling-style data. |
| Font provider | Windows backends cover DirectWrite and vendored FreeType. Non-Windows paths are stubs. |
| Font cache | Caches opened fonts, shaped text runs, glyph rasters, metrics, and text advance data. |
| Render | D3D11 and D3D12 backends support contexts, windows, buffers, textures, shaders, pipelines, bind groups, render passes, and presentation. |
| Draw | Records 2D primitives, paths, styled rectangles, images, text, layers, opacity, blur, drop shadows, and blend modes. |
| GUI | Immediate-mode layout, theming, widgets, scroll panels, fixed lists, tree nodes, tables, tabs, text inputs, hit testing, and debug metadata are implemented. The API is still expected to change. |
| Hot reload | Windows debug builds can run a stable host process and reload app/UI DLL modules after source changes. |
| Tests | CTest covers base, GUI, render, font provider, font cache, draw, and code editor support code. Windows also registers a D3D12 smoke test. |
| Benchmarks | A minimal benchmark harness exists; current coverage is intentionally small. |

## Requirements

- CMake 3.28+
- Visual Studio 2022/MSVC on Windows
- Clang on macOS
- Ninja for single-config Clang presets
- clangd, clang-format, and clang-tidy for normal development
- ccache, optional

The Windows `.bat` scripts set up the MSVC environment before invoking MSVC
presets, so they can be run from a normal shell.

## Build And Test

Windows defaults to `windows-msvc-debug`:

```powershell
.\build.bat
.\test.bat
.\run.bat
.\bench.bat
```

`run.bat` builds and launches `render_triangle_testbed` by default. Pass a
target name to run another executable:

```powershell
.\run.bat windows-msvc-debug ui_api_testbed
.\run.bat windows-msvc-debug code_editor
.\run.bat windows-msvc-debug render_effects_testbed
```

Windows Clang presets are useful for clangd and cross-compiler checks:

```powershell
.\build.bat windows-clang-debug
.\test.bat windows-clang-debug
```

Strict presets enable clang-tidy and warnings-as-errors:

```powershell
.\build.bat windows-msvc-strict
.\test.bat windows-msvc-strict
```

macOS defaults to `macos-clang-debug`:

```sh
./build.sh
./test.sh
./run.sh
./bench.sh
```

`run.sh` currently runs `gui_framework_info`. Use CMake directly for a specific
non-default executable on non-Windows platforms.

## Examples

| Target | Platform | Description |
| --- | --- | --- |
| `gui_framework_info` | Windows, macOS | Prints the framework version and compiler information. Useful as a quick build sanity check. |
| `render_triangle_testbed` | Windows | Minimal Win32 + D3D11 render loop with shader compilation, buffers, bind groups, resize handling, and presentation. |
| `render_dx12_clear_smoke` | Windows | D3D12 smoke executable that clears and presents, validates frame upload alignment, exercises texture/sampler binding, and reads back a pixel. Registered as a test on Windows. |
| `text_rendering_testbed` | Windows | DirectWrite/font-cache/draw-renderer smoke scene for system font text at several sizes. |
| `text_stage_probe` | Windows | Console probe that writes BMP artifacts for raw glyph masks, font-cache masks, atlas upload masks, and CPU-composited text. |
| `render_effects_testbed` | Windows | Deterministic visual grid for alpha overlap, group opacity, rounded borders, box shadow, blur, drop shadow, clipped layers, and blend modes. |
| `liquid_glass_testbed` | Windows | Shader and draw-overlay experiment for layered glass-style refraction, blur, highlights, and text overlay. |
| `ui_api_testbed` | Windows, macOS build path | Main immediate-mode GUI exercise app. Windows debug builds use a hot-reload module for UI source changes. |
| `repository_ui_testbed` | Windows | Repository browser-style UI exercise using embedded Codicons and the GUI/draw/render stack. |
| `code_editor` | Windows, macOS build path | Larger example editor with modal editing, file tree, Git sidebar, LSP messages, syntax support, splits, pickers, diagnostics, folding, config parsing, and hot reload on Windows debug builds. |

Example-specific notes live in:

- `examples/ui_api_testbed/README.md`
- `examples/code_editor/README.md`
- `docs/ui_api_examples.md`
- `docs/rendering_effects.md`

## Source Layout

- `src/base`: low-level memory, strings, containers, formatting, IO, asserts,
  crash handling, unicode, and virtual memory.
- `src/encoding`: compact JSON parsing helpers.
- `src/font_provider`: platform font access and glyph rasterization.
- `src/font_cache`: font and glyph cache built on the provider layer.
- `src/render`: renderer abstraction plus Windows D3D11/D3D12 backends.
- `src/draw`: immediate 2D draw commands, text commands, layers, and renderer.
- `src/gui`: public immediate-mode GUI API and hot-reload support.
- `src/test`: custom no-dependency test harness.
- `src/bench`: custom no-dependency benchmark harness.
- `tests`: test executables registered with CTest.
- `benchmarks`: benchmark executables.
- `examples`: smoke apps, visual probes, and larger UI examples.
- `docs`: project guidelines, test strategy, API examples, and rendering notes.
- `third_party`: approved vendored dependencies only.

## Dependencies

CMake does not fetch dependencies. Current vendored assets/dependencies include:

- `third_party/freetype`: FreeType, used by the optional Windows FreeType font
  provider backend.
- `third_party/source_code_pro`: Source Code Pro regular TTF, embedded by the
  code editor example.
- `third_party/codicons`: Codicon font assets used by the repository UI testbed.

## Memory And Containers

Framework-owned memory is allocated from `Arena`. Production code should not use
the C heap, owning `new`/`delete`, STL containers, or PMR default resources for
owned framework data.

Base containers such as `Vec`, `HashMap`, `StableHashMap`, `XarArray`,
`XarFreelistArray`, and `StringBuffer` are trivially copyable headers over
arena-owned backing memory. They do not own or free their backing allocations.

Initialize growing containers with an explicit resource:

```cpp
Arena arena = {};
arena.init();

Vec<int> values = {};
DEBUG_ASSERT(values.init(64u, arena.resource()));
DEBUG_ASSERT(values.push_back(7));
```

Container assignment copies the header only. Use `copy_from(other,
arena.resource())` when data must be copied into a different lifetime. Dropping a
container is just resetting its header with `container = {};`; memory is
reclaimed by the arena.

More detailed style, testing, and rendering guidance is in `docs/`.
