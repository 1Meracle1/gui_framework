#include <bench/bench.h>
#include <gui/gui.h>

namespace {

    void query_version_string(void*) {
        bench::keep_alive(gui::version_string());
    }

    constexpr bench::BenchCase BENCHMARKS[] = {
        {"version_string", query_version_string, nullptr, 1000000u},
    };

} // namespace

int main() {
    constexpr auto BENCHMARK_COUNT = sizeof(BENCHMARKS) / sizeof(BENCHMARKS[0]);
    return bench::run_benchmarks(BENCHMARKS, BENCHMARK_COUNT);
}
