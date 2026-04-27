#include <base/assert.h>
#include <base/fmt.h>
#include <csetjmp>
#include <cstdio>
#include <test/test.h>

namespace test {

    namespace {

        Registration* first_registration = nullptr;
        Registration* last_registration = nullptr;

        struct AssertRunState {
            Context* context;
            std::jmp_buf* jump_buffer;
        };

        AssertRunState* active_assert_run_state = nullptr;

        auto test_case_name(TestCase const& test_case) -> char const* {
            return test_case.name != nullptr ? test_case.name : "<unnamed>";
        }

        auto report_failure(Context* context,
                            char const* kind,
                            char const* expression,
                            char const* file,
                            uint32_t line) -> void {
            if (context != nullptr) {
                context->failed_assertions += 1;
            }

            fmt::eprintf(
                "%s failed: %s (%s:%u)\n", kind, expression, file, static_cast<unsigned>(line));
        }

        [[noreturn]] auto
        test_assert_handler(char const* expression, char const* file, uint32_t line) -> void {
            report_failure(active_assert_run_state->context, "assertion", expression, file, line);
            std::longjmp(*active_assert_run_state->jump_buffer, 1);
        }

        auto run_test_function(TestCase const& test_case, Context* context) -> void {
            std::jmp_buf jump_buffer;
            AssertRunState state = {context, &jump_buffer};
            AssertRunState* const previous_state = active_assert_run_state;

            active_assert_run_state = &state;
            base::set_assert_handler(test_assert_handler);

#if BASE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
            if (setjmp(jump_buffer) == 0) {
                test_case.fn(context);
            }
#if BASE_COMPILER_MSVC
#pragma warning(pop)
#endif

            active_assert_run_state = previous_state;
            base::set_assert_handler(previous_state != nullptr ? test_assert_handler : nullptr);
        }

        auto run_test(TestCase const& test_case) -> bool {
            Context context = {};
            char const* const name = test_case_name(test_case);

            if (test_case.fn == nullptr) {
                fmt::eprintf("[test] %s: missing function\n", name);
                return false;
            }

            fmt::printf("[test] %s\n", name);
            run_test_function(test_case, &context);

            if (context.failed_assertions != 0) {
                fmt::eprintf("[test] %s: %u failure(s)\n",
                             name,
                             static_cast<unsigned>(context.failed_assertions));
                return false;
            }

            return true;
        }

        auto finish_test_run(size_t test_case_count, size_t failed_test_count) -> int {
            if (failed_test_count == 0) {
                fmt::printf("[test] all %zu test(s) passed\n", test_case_count);
                return 0;
            }

            fmt::eprintf("[test] %zu of %zu test(s) failed\n", failed_test_count, test_case_count);
            return 1;
        }

    } // namespace

    Registration::Registration(char const* name, TestFn fn) : test_case{name, fn}, next(nullptr) {
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

        report_failure(context, "expectation", expression, file, line);
        return false;
    }

    auto run_tests(TestCase const* test_cases, size_t test_case_count) -> int {
        if (test_cases == nullptr && test_case_count != 0) {
            fmt::eprintf("test runner received a null test case array\n");
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
