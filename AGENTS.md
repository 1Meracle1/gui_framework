#AGENTS.md

## Top Priority Convention

This section has priority over every other convention in this file.

- Simplicity first: write the minimum code that solves the problem. Nothing
  speculative.
- Implement only what was asked. No features beyond the request.
- Do not add abstractions for single-use code.
- Do not add flexibility or configurability that was not requested.
- Do not add error handling for impossible scenarios.
- If you write 20 lines and it could be 5, rewrite it.
- Minimize code size aggressively: no pessimization, no bloat, no excess.
- Write compact, simple, clean, deeply thought-through code from first
  principles that surgically executes the intention.

## Project

`gui_framework` is a custom GPU-oriented, cross-platform GUI framework written
in C++20. The project targets Windows with MSVC and macOS with Clang.

## Build And Tooling Requirements

- Use CMake 3.28+.
- Support C++20 only for now.
- Keep `CMAKE_EXPORT_COMPILE_COMMANDS` enabled.
- Disable C++ compiler extensions.
- Disable RTTI and exceptions.
- Enable strong compiler warnings for every target.
- Strict builds enable warnings-as-errors and clang-tidy.
- Use clangd, clang-format, and clang-tidy.
- Use ccache where it is known to work;
plain MSVC `cl.exe` skips automatic
  ccache unless `CCACHE` is explicitly set.
- Do not add dependency fetches to CMake.

## Dependencies

- Prefer no external dependencies.
- Any dependency must be approved before use.
- Approved dependencies must be vendored directly under `third_party/`.
- Do not use package-manager downloads, `FetchContent`, CPM, vcpkg manifests, or
  similar dependency acquisition in project CMake.

## Source Layout

- `src/foundation`: generic low-level types, config, and assertions.
- `src/framework`: actual public GUI framework API.
- `src/test`: custom no-dependency test harness.
- `src/bench`: custom no-dependency benchmark harness.
- `tests`: test executables.
- `benchmarks`: benchmark executables.
- `examples`: small project utilities and smoke executables.
- `docs`: project guidelines and testing strategy.

Only actual GUI framework functionality belongs in namespace `gui`. Foundation,
test, and benchmark support code must not use namespace `gui`.

## Naming

- Files and directories use `snake_case`.
- Functions and variables use `snake_case`.
- Class member variables use an `m_` prefix and no trailing underscore;
struct member variables do not use the `m_` prefix.- Types use `UpperCamelCase`.- Constants and
    macros use `UPPER_SNAKE_CASE`.- Namespaces use `snake_case`.-
        CMake options use concise `UPPER_SNAKE_CASE`;
do not prefix everything with
  `GUI_`.

## C++ Style

- Prefer C-like C++ and gamedev-style explicitness.
- Put the public section of a class before private sections.
- In classes, place private methods first, then use a separate `private:`
  section for member variables.
- Use East const style: `int const* ptr`, not `int const* ptr`.
- Use trailing return types for functions and methods: `auto func() -> ReturnType`.
- Use short integral type names where available: `size_t`, `int8_t`, `int16_t`,
  `int32_t`, `int64_t`, `uint8_t`, `uint16_t`, `uint32_t`, and `uint64_t`, not
  `std::size_t`, `std::int8_t`, `std::int16_t`, `std::int32_t`, `std::int64_t`,
  `std::uint8_t`, `std::uint16_t`, `std::uint32_t`, or `std::uint64_t`.
- Use `std::min`, `std::max`, and `std::clamp` instead of inline ternary
  operators for min, max, and clamping logic.
- Prefer `StrRef` from the base layer for non-owning string parameters,
  string-return views, parsing, and comparisons. Use `char const*` only when a
  null-terminated C string is required at an API boundary, and avoid
  `std::string` or `std::string_view` in production interfaces unless ownership
  or interop makes them necessary.
- Use `fmt::printf`, `fmt::eprintf`, and `fmt::fprintf` from `base/fmt.h`
  for printf-style output in project code. Prefer `fmt::printf` for stdout,
  `fmt::eprintf` for stderr, and `fmt::fprintf` only for other `FILE*`
  streams. Use `fmt::wprintf` for generic `io::Writer` targets, and
  `fmt::bprintf`, `fmt::aprintf`, or `fmt::tprintf` for fixed-buffer,
  allocator-backed, or thread-temporary formatted text. These accept `StrRef`
  directly with `%s`, including slices and non-null-terminated text, so do not
  expand `StrRef` manually with `%.*s`, `.data()`, or `.size()`.
- Avoid hidden allocation, hidden control flow, and implicit ownership transfer.
- Validate external inputs at the public API boundary. Do not repeat the same
  null, handle, or range checks through every internal call after a caller has
  already established the invariant; use `DEBUG_ASSERT` for internal
  invariants when a debug-only check is useful. Use release-active `ASSERT` only
  for failures that must still crash in release builds.
- Do not use C heap allocation or owning `new`/`delete` in production code.
  Functions that allocate framework-owned data must take an explicit `Arena&`
  or allocate from an arena already owned by the context they operate on. Use
  `arena_alloc` and `arena_new`; arena allocation failure is not recoverable.
- Use thread-local temporary arenas for short-lived scratch data. Long-lived
  objects and cached data must live in caller-owned or context-owned arenas with
  explicit lifetime.
- Avoid iostreams, STL containers, RTTI, exceptions, and template-heavy
  abstractions in production code unless there is a clear codegen, usability,
  performance, or compile-time reason.
- Keep headers lean.
- Use custom allocators and custom foundational utilities as the project grows.

## Allocation, Lifetimes, And Base Containers

- Production allocation goes through `Arena`. Do not use C heap allocation,
  owning `new`/`delete`, STL containers, or PMR default resources for
  framework-owned data.
- `MemoryResource*` parameters exist so base containers can allocate from an
  explicit arena resource. Pass `arena.resource()`. For fixed storage, use an
  API that explicitly accepts caller-provided backing memory. Do not call
  `std::pmr::get_default_resource()` or rely on process global allocator state.
- The arena owns allocated bytes. Any container, `Slice`, `StrRef`, pointer, or
  handle into arena memory is valid only while that arena allocation remains
  valid.
- Use context-owned arenas for persistent framework state, frame arenas for one
  frame of data, caller-owned arenas for output that must outlive the call, and
  thread-local temporary arenas only for scratch data that does not escape the
  scope or frame.
- Use `ArenaTemp` or an arena marker when scratch allocations must be rolled back
  before the next full arena reset. Call `keep()` only when the data is
  intentionally promoted to the arena's remaining lifetime.
- Base containers such as `Vec`, `HashMap`, `StableHashMap`, `XarArray`,
  `XarFreelistArray`, and `StringBuffer` are small trivially copyable headers
  pointing at arena-owned backing memory. They do not own or free their backing
  allocations.
- Container element types must be trivially copyable unless a container's API
  explicitly says otherwise. Store indices, handles, pointers, or arena-owned
  payloads instead of embedding non-trivial ownership in container elements.
- Initialize arena-backed containers with an explicit resource before first
  growth, for example `values.init(capacity, arena.resource())`. Passing a null
  resource is a bug.
- Container growth allocates new backing from the same resource and leaves old
  backing for the arena to reclaim later. Reserve realistic capacity when a
  long-lived arena would otherwise accumulate repeated growth allocations.
- Direct assignment copies the container header only. It is the correct way to
  transfer a header to the same backing lifetime; clear the source with
  `source = {};` immediately after such a transfer when the source must no
  longer alias the backing memory.
- Use `copy_from(other, arena.resource())` when data must be copied into a
  different lifetime. `copy_from` allocates destination backing, copies the
  current contents, and leaves `other` unchanged. If the destination already
  pointed at backing memory, that old backing remains owned by its arena until
  the arena is reset or destroyed.
- Do not add `take_from`, move-only ownership, destructors, or `destroy()` to
  base containers. Dropping a container is just resetting its header with
  `container = {};`; freeing memory is the arena's job.
- Prefer typed zero initialization (`T value = {};`, `member = {};`) for
  clearing headers and plain structs. Use `std::memset` only for raw byte
  buffers or deliberately byte-filled storage.
- Do not return or store containers, slices, strings, or pointers backed by a
  temporary or frame arena unless the API name and documentation make that
  short lifetime explicit.

## Scripts

Use the root scripts for normal workflows:

- `build.bat` / `build.sh`
- `run.bat` / `run.sh`
- `test.bat` / `test.sh`
- `bench.bat` / `bench.sh`

On Windows, the `.bat` scripts must be runnable from a normal shell. They are
responsible for setting up the MSVC environment before invoking MSVC presets.

For Windows GUI bug reproduction:

- Put tracked repro helpers under `scripts/repro/`.
- Start from `scripts/repro/code_editor_symbols_repro.ps1` for `code_editor`
  window-driving automation.
- Drive the spawned application by its HWND with `PostMessage`; do not use
  global key injection when a targeted window-message path works.
- Send command keys with `WM_KEYDOWN` and `WM_KEYUP`, and send typed text with
  `WM_CHAR` to avoid duplicated characters.
- Capture `stdout`, `stderr`, and milestone screenshots under
  `_codex_artifacts/`, then inspect the screenshots to confirm the expected UI
  state.
- Keep repro scripts small and linear. Extend the existing sequence only as far
  as the reported bug requires.
- Read `scripts/repro/README.md` before extending a repro script.

## Verification

Before handing off changes, run the smallest relevant set of checks. For build
system or foundational changes, prefer:

- `clang-format --dry-run --Werror` over changed C++ files.
- `.\build.bat`
- `.\run.bat`
- `.\test.bat`
- `.\bench.bat`

If strict behavior changed, also validate a strict configuration with
`ENABLE_CLANG_TIDY=ON` and `WARNINGS_AS_ERRORS=ON`.
