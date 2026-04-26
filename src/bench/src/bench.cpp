#include <base/print.h>
#include <bench/bench.h>
#include <chrono>
#include <cstdio>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace bench {

    auto keep_alive(void const* value) -> void {
#if defined(_MSC_VER)
        void const* volatile volatile_value = value;
        (void)volatile_value;
        _ReadWriteBarrier();
#elif defined(__clang__) || defined(__GNUC__)
        asm volatile("" : : "g"(value) : "memory");
#else
        (void)value;
#endif
    }

    auto run_benchmarks(BenchCase const* bench_cases, size_t bench_case_count) -> int {
        if (bench_cases == nullptr && bench_case_count != 0) {
            base::eprintf("benchmark runner received a null benchmark case array\n");
            return 1;
        }

        using Clock = std::chrono::steady_clock;

        for (size_t index = 0; index < bench_case_count; ++index) {
            BenchCase const& bench_case = bench_cases[index];
            if (bench_case.fn == nullptr) {
                base::eprintf("[bench] %s: missing function\n", bench_case.name);
                return 1;
            }

            uint32_t const iterations = bench_case.iterations == 0 ? 1 : bench_case.iterations;
            auto const start = Clock::now();

            for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
                bench_case.fn(bench_case.user_data);
            }

            auto const end = Clock::now();
            auto const elapsed_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            auto const per_iteration_ns = elapsed_ns / static_cast<long long>(iterations);

            base::printf("[bench] %s: %lld ns total, %lld ns/iteration (%u iterations)\n",
                         bench_case.name,
                         static_cast<long long>(elapsed_ns),
                         static_cast<long long>(per_iteration_ns),
                         static_cast<unsigned>(iterations));
        }

        return 0;
    }

} // namespace bench
