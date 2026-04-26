#pragma once

#include <cstddef>
#include <cstdint>

namespace test {

    struct Context {
        uint32_t failed_assertions;
    };

    using TestFn = void (*)(Context* context);

    struct TestCase {
        char const* name;
        TestFn fn;
    };

    bool expect(Context* context,
                bool condition,
                char const* expression,
                char const* file,
                uint32_t line);
    int run_tests(TestCase const* test_cases, size_t test_case_count);

} // namespace test

#define TEST_EXPECT(context, expression)                                                           \
    (::test::expect((context),                                                                     \
                    static_cast<bool>(expression),                                                 \
                    #expression,                                                                   \
                    __FILE__,                                                                      \
                    static_cast<uint32_t>(__LINE__)))
