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

## Memory And Containers

Framework-owned memory is allocated from `Arena`. Production code should not use
the C heap, owning `new`/`delete`, STL containers, or PMR default resources for
owned data. When an API asks for `MemoryResource*`, pass `arena.resource()`.
Fixed storage should use an API that explicitly accepts caller-provided backing
memory.

An arena defines the lifetime of everything allocated from it. Use persistent
context-owned arenas for cached or long-lived state, frame arenas for data that
dies at the end of a frame, caller-owned arenas for returned data, and
thread-local temporary arenas only for scratch data that does not escape the
scope or frame.

Base containers are trivially copyable headers over arena-owned backing memory:
`Vec`, `HashMap`, `StableHashMap`, `XarArray`, `XarFreelistArray`, and
`StringBuffer` do not own or free their backing allocations. Initialize them
with an explicit resource:

```cpp
Arena arena = {};
arena.init();

Vec<int> values = {};
DEBUG_ASSERT(values.init(64u, arena.resource()));
DEBUG_ASSERT(values.push_back(7));
```

Container assignment copies only the header. Use it when transferring a header
inside the same backing lifetime, then clear the source if the old name must stop
referring to that backing:

```cpp
Vec<int> transferred = values;
values = {};
```

Use `copy_from(other, arena.resource())` when data must be reallocated into a
different lifetime. It allocates destination backing, copies the contents, and
does not modify `other`:

```cpp
Arena copy_arena = {};
copy_arena.init();

Vec<int> copied = {};
DEBUG_ASSERT(copied.copy_from(transferred, copy_arena.resource()));
```

If the destination already referenced backing memory, `copy_from` replaces the
header and leaves the old backing for its arena to reclaim later.

Do not add container destructors, `destroy()`, move-only ownership, or
`take_from()` helpers. Dropping a container is just resetting its header with
`container = {};`; reclaiming backing memory is done by `arena.reset()`,
`arena.reset_to(marker)`, or `arena.destroy()`.

Container growth allocates new backing from the same resource and leaves old
backing for the arena to reclaim later. Reserve enough capacity when growing a
container repeatedly in a long-lived arena.

Container element types must be trivially copyable unless a container documents
otherwise. Store indices, handles, pointers, or arena-owned payloads instead of
embedding non-trivial ownership in container elements.