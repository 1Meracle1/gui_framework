#pragma once

#include <base/config.h>
#include <cstdint>

namespace base {

    using AssertHandler = void (*)(char const* expression, char const* file, uint32_t line);

    void set_assert_handler(AssertHandler handler);
    void handle_assert_failure(char const* expression, char const* file, uint32_t line);

} // namespace base

#define BASE_ASSERT(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ::base::handle_assert_failure(                                                         \
                #expression, __FILE__, static_cast<uint32_t>(__LINE__));                      \
        }                                                                                          \
    } while (false)
