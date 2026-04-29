#pragma once

#include <base/config.h>
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

#define ASSERT(expression)                                                                         \
    do {                                                                                           \
        if (!(expression)) [[unlikely]] {                                                          \
            ::base::handle_assert_failure(                                                         \
                #expression, nullptr, __FILE__, static_cast<uint32_t>(__LINE__), __func__);        \
        }                                                                                          \
    } while (false)

#define ASSERT_MSG(expression, message)                                                            \
    do {                                                                                           \
        if (!(expression)) [[unlikely]] {                                                          \
            ::base::handle_assert_failure(                                                         \
                #expression, (message), __FILE__, static_cast<uint32_t>(__LINE__), __func__);      \
        }                                                                                          \
    } while (false)

#if BASE_DEBUG
#define DEBUG_ASSERT(expression) ASSERT(expression)
#define DEBUG_ASSERT_MSG(expression, message) ASSERT_MSG(expression, message)
#else
#define DEBUG_ASSERT(expression)                                                                   \
    do {                                                                                           \
        static_cast<void>(sizeof(static_cast<bool>(expression)));                                  \
    } while (false)

#define DEBUG_ASSERT_MSG(expression, message)                                                      \
    do {                                                                                           \
        static_cast<void>(sizeof(static_cast<bool>(expression)));                                  \
        static_cast<void>(sizeof(message));                                                        \
    } while (false)
#endif
