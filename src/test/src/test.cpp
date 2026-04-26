#include <cstdio>
#include <test/test.h>

namespace test {

    bool expect(Context* context,
                bool condition,
                char const* expression,
                char const* file,
                uint32_t line) {
        if (condition) {
            return true;
        }

        if (context != nullptr) {
            context->failed_assertions += 1;
        }

        std::fprintf(stderr,
                     "expectation failed: %s (%s:%u)\n",
                     expression,
                     file,
                     static_cast<unsigned>(line));
        return false;
    }

    int run_tests(TestCase const* test_cases, size_t test_case_count) {
        if (test_cases == nullptr && test_case_count != 0) {
            std::fprintf(stderr, "test runner received a null test case array\n");
            return 1;
        }

        size_t failed_test_count = 0;

        for (size_t index = 0; index < test_case_count; ++index) {
            Context context = {};
            TestCase const& test_case = test_cases[index];

            if (test_case.fn == nullptr) {
                std::fprintf(stderr, "[test] %s: missing function\n", test_case.name);
                failed_test_count += 1;
                continue;
            }

            std::printf("[test] %s\n", test_case.name);
            test_case.fn(&context);

            if (context.failed_assertions != 0) {
                std::fprintf(stderr,
                             "[test] %s: %u failed expectation(s)\n",
                             test_case.name,
                             static_cast<unsigned>(context.failed_assertions));
                failed_test_count += 1;
            }
        }

        if (failed_test_count == 0) {
            std::printf("[test] all %zu test(s) passed\n", test_case_count);
            return 0;
        }

        std::fprintf(
            stderr, "[test] %zu of %zu test(s) failed\n", failed_test_count, test_case_count);
        return 1;
    }

} // namespace test
