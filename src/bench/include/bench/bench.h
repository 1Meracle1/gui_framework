#pragma once

#include <cstddef>
#include <cstdint>

namespace bench {

    using BenchFn = void (*)(void* user_data);

    struct BenchCase {
        char const* name;
        BenchFn fn;
        void* user_data;
        uint32_t iterations;
    };

    auto keep_alive(void const* value) -> void;
    auto run_benchmarks(BenchCase const* bench_cases, size_t bench_case_count) -> int;

} // namespace bench
