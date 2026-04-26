#include <bench/bench.h>
#include <gui/gui.h>

namespace {

    auto query_version_string(void*) -> void {
        bench::keep_alive(gui::version_string());
    }

    constexpr bench::BenchCase BENCHMARKS[] = {
        {"version_string", query_version_string, nullptr, 1000000u},
    };

} // namespace

auto main() -> int {
    constexpr auto BENCHMARK_COUNT = sizeof(BENCHMARKS) / sizeof(BENCHMARKS[0]);
    return bench::run_benchmarks(BENCHMARKS, BENCHMARK_COUNT);
}
