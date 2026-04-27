# gui_framework

`gui_framework` is the starting point for a custom GPU-based, cross-platform GUI
framework targeting Windows and macOS.

The current scaffold is intentionally small: it sets up the build, tooling,
guidelines, and no-dependency test/benchmark harnesses before larger subsystems
are added.

## Requirements

- CMake 3.28+
- Ninja
- MSVC on Windows, from a Visual Studio Developer shell
- Clang on macOS
- clangd, clang-format, and clang-tidy
- ccache, optional but auto-detected

## Build

Windows with MSVC:

```powershell
.\build.bat
.\test.bat
.\run.bat
.\bench.bat
```

`run.bat` builds and launches `render_triangle_testbed` by default. Pass a
target name as the second argument to run another tool, for example
`.\run.bat windows-msvc-debug gui_framework_info`.

Windows with Clang, useful for local clangd/tooling checks:

```powershell
.\build.bat windows-clang-debug
.\test.bat windows-clang-debug
```

macOS with Clang:

```sh
./build.sh
./test.sh
./run.sh
./bench.sh
```

C++20 is the only supported language standard for now. Strict presets enable
clang-tidy and warnings-as-errors.

If a developer shell puts an older clang-tidy first on PATH, pin the tool:

```sh
cmake -S . -B build/strict -DENABLE_CLANG_TIDY=ON -DCLANG_TIDY=/path/to/clang-tidy
```

Plain MSVC `cl.exe` skips automatic ccache use unless a known-good launcher is
pinned with `CCACHE`.

## Layout

- `src/base`: reusable low-level building blocks such as config, asserts, and
  future containers, allocators, algorithms, and custom types.
- `src/render`: renderer abstraction with a Windows Direct3D 11 backend.
- `src/gui`: public GUI API.
- `src/test`: custom no-dependency test harness.
- `src/bench`: custom no-dependency benchmark harness.
- `tests`: test executables.
- `benchmarks`: benchmark executables.
- `docs`: project guidelines and testing strategy.
- `third_party`: approved vendored dependencies only.

No dependency is fetched by CMake.
