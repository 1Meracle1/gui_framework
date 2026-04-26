# Testing Strategy

The project starts with custom no-dependency testing and benchmarking harnesses.
The goal is to keep the first layer simple, portable, and easy to debug.

## Test Harness

- `gui_test` provides a tiny test runner and assertion helpers.
- Tests are normal executables registered with CTest.
- Tests should avoid allocation unless allocation behavior is the subject under
  test.
- Keep test names stable and descriptive.
- Prefer focused tests over large scenario tests for low-level code.

## Benchmark Harness

- `gui_bench` provides a minimal benchmark runner.
- Benchmarks are built by default but are not registered as pass/fail tests.
- Benchmarks should report enough context to compare runs manually.
- Treat benchmark results as signals, not hard correctness gates.

## Presets

- Debug presets build and run normal tests.
- Strict presets enable clang-tidy and warnings-as-errors.
- Release presets are used for performance checks and benchmark runs.

## Acceptance For New Code

- New production code should have at least one direct test.
- Bug fixes should include a test that fails without the fix where practical.
- Shared base-layer changes should run both debug and strict presets.
- GPU/platform behavior should eventually get backend-specific smoke tests in
  addition to pure unit tests.
