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
- `tools`: small project utilities and smoke executables.
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
  already established the invariant; use `ASSERT` for internal invariants when a
  check is still useful.
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

## Scripts

Use the root scripts for normal workflows:

- `build.bat` / `build.sh`
- `run.bat` / `run.sh`
- `test.bat` / `test.sh`
- `bench.bat` / `bench.sh`

On Windows, the `.bat` scripts must be runnable from a normal shell. They are
responsible for setting up the MSVC environment before invoking MSVC presets.

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
