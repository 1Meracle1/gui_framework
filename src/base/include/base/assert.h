#pragma once

#include <base/crash.h>
#include <cstdint>

namespace base {

    using AssertHandler = void (*)(char const* expression, char const* file, uint32_t line);

    void set_assert_handler(AssertHandler handler);

    void handle_assert_failure(char const* expression, char const* file, uint32_t line);
    void handle_assert_failure(char const* expression,
                               char const* message,
                               char const* file,
                               uint32_t line,
                               char const* function);

} // namespace base

#define ASSERT(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ::base::handle_assert_failure(                                                         \
                #expression, nullptr, __FILE__, static_cast<uint32_t>(__LINE__), __func__);        \
        }                                                                                          \
    } while (false)

#define ASSERT_MSG(expression, message)                                                       \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ::base::handle_assert_failure(                                                         \
                #expression, (message), __FILE__, static_cast<uint32_t>(__LINE__), __func__);      \
        }                                                                                          \
    } while (false)
