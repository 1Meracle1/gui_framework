#include <base/assert.h>
#include <cstdio>
#include <cstdlib>

namespace {

    base::AssertHandler assert_handler = nullptr;

} // namespace

namespace base {

    void set_assert_handler(AssertHandler handler) {
        assert_handler = handler;
    }

    void handle_assert_failure(char const* expression, char const* file, uint32_t line) {
        if (assert_handler != nullptr) {
            assert_handler(expression, file, line);
            return;
        }

        std::fprintf(stderr,
                     "assertion failed: %s (%s:%u)\n",
                     expression,
                     file,
                     static_cast<unsigned>(line));
        std::abort();
    }

} // namespace base
