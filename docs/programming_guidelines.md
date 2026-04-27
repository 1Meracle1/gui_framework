#Programming Guidelines

The project is a performance-oriented C++ codebase with a gamedev-style bias:
explicit data flow, predictable allocation, simple control flow, and low compile
times.

## Language And Toolchain

- The portable baseline is strict C++20.
- C++ extensions are disabled.
- RTTI and exceptions are disabled.
- Warnings are enabled for every target. Strict presets promote warnings to
  errors.
- clang-format, clang-tidy, and clangd are part of the normal workflow.

## Naming

- Namespaces: `snake_case`.
- Use namespace `gui` only for actual GUI framework API.
- Base, test, and benchmark support code stays outside `gui`.
- Files and directories: `snake_case`.
- Functions and variables: `snake_case`.
- Class member variables: `m_` prefix, no trailing underscore.
- Types: `UpperCamelCase`.
- Constants and macros: `UPPER_SNAKE_CASE`.
- CMake options: `UPPER_SNAKE_CASE`.

## C++ Style

- Prefer C-like C++ where it keeps codegen, debugging, and compile times clear.
- Put the public section of a class before private sections.
- In classes, place private methods first, then use a separate `private:`
  section for member variables.
- Use East const style: `int const* ptr`, not `int const* ptr`.
- Use trailing return types for functions and methods: `auto func() -> ReturnType`.
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
- Do not use exceptions. Return explicit result/error values.
- Do not use RTTI, `dynamic_cast`, or `typeid`.
- Avoid iostreams, STL containers, and template-heavy abstractions in production
  code unless they clearly improve the implementation.
- Prefer simple structs, free functions, fixed-size spans/views, and explicit
  ownership.
- Use constructors sparingly for invariant setup, not for hidden work.
- Keep headers lean and avoid unnecessary includes.
- Use `constexpr` and templates only when they pay for themselves.

## Memory

- Framework code should make allocation sites explicit.
- Custom allocators are a planned base subsystem.
- APIs should accept allocator/context parameters where long-lived or repeated
  allocation is expected.
- Avoid global allocation and static initialization with runtime side effects.

## Dependencies

- The project does not fetch dependencies.
- Any third-party code requires approval before use.
- Approved dependencies are vendored under `third_party/`.
- Small public-domain or single-header libraries can be considered when they
  replace real complexity and are easy to audit.

## Future Subsystems

The following areas are intentionally not scaffolded as empty modules yet:

- Memory allocators and arena primitives.
- Platform/window/input layer.
- UI layout, text, styling, and interaction systems.

Add them when there is a concrete first behavior to compile and test.
