#include <base/assert.h>

namespace {

    base::AssertHandler assert_handler = nullptr;

} // namespace

namespace base {

    void set_assert_handler(AssertHandler handler) {
        assert_handler = handler;
    }

    void handle_assert_failure(char const* expression, char const* file, uint32_t line) {
        handle_assert_failure(expression, nullptr, file, line, nullptr);
    }

    void handle_assert_failure(char const* expression,
                               char const* message,
                               char const* file,
                               uint32_t line,
                               char const* function) {
        if (assert_handler != nullptr) {
            assert_handler(expression, file, line);
            return;
        }

        CrashReport const report = {
            CrashReason::ASSERTION_FAILURE,
            message,
            expression,
            {file, function, line},
        };

        crash(report);
    }

} // namespace base
