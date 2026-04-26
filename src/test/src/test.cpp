#include <base/print.h>
#include <cstdio>
#include <test/test.h>

namespace test {

    namespace {

        Registration* first_registration = nullptr;
        Registration* last_registration = nullptr;

        auto test_case_name(TestCase const& test_case) -> char const* {
            return test_case.name != nullptr ? test_case.name : "<unnamed>";
        }

        auto run_test(TestCase const& test_case) -> bool {
            Context context = {};
            char const* const name = test_case_name(test_case);

            if (test_case.fn == nullptr) {
                base::eprintf("[test] %s: missing function\n", name);
                return false;
            }

            base::printf("[test] %s\n", name);
            test_case.fn(&context);

            if (context.failed_assertions != 0) {
                base::eprintf("[test] %s: %u failed expectation(s)\n",
                              name,
                              static_cast<unsigned>(context.failed_assertions));
                return false;
            }

            return true;
        }

        auto finish_test_run(size_t test_case_count, size_t failed_test_count) -> int {
            if (failed_test_count == 0) {
                base::printf("[test] all %zu test(s) passed\n", test_case_count);
                return 0;
            }

            base::eprintf("[test] %zu of %zu test(s) failed\n", failed_test_count, test_case_count);
            return 1;
        }

    } // namespace

    Registration::Registration(char const* name, TestFn fn) noexcept
        : test_case{name, fn}, next(nullptr) {
        if (last_registration != nullptr) {
            last_registration->next = this;
        } else {
            first_registration = this;
        }

        last_registration = this;
    }

    auto expect(Context* context,
                bool condition,
                char const* expression,
                char const* file,
                uint32_t line) -> bool {
        if (condition) {
            return true;
        }

        if (context != nullptr) {
            context->failed_assertions += 1;
        }

        base::eprintf(
            "expectation failed: %s (%s:%u)\n", expression, file, static_cast<unsigned>(line));
        return false;
    }

    auto run_tests(TestCase const* test_cases, size_t test_case_count) -> int {
        if (test_cases == nullptr && test_case_count != 0) {
            base::eprintf("test runner received a null test case array\n");
            return 1;
        }

        size_t failed_test_count = 0;

        for (size_t index = 0; index < test_case_count; ++index) {
            if (!run_test(test_cases[index])) {
                failed_test_count += 1;
            }
        }

        return finish_test_run(test_case_count, failed_test_count);
    }

    auto run_registered_tests() -> int {
        size_t test_case_count = 0;
        size_t failed_test_count = 0;

        for (Registration const* registration = first_registration; registration != nullptr;
             registration = registration->next) {
            if (!run_test(registration->test_case)) {
                failed_test_count += 1;
            }

            test_case_count += 1;
        }

        return finish_test_run(test_case_count, failed_test_count);
    }

} // namespace test
