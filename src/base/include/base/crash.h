#pragma once

#include <base/config.h>
#include <cstdint>

namespace base {

    enum class CrashReason : uint8_t {
        ASSERTION_FAILURE,
        PANIC,
        UNREACHABLE_CODE,
        PROCESS_FAULT,
    };

    struct SourceLocation {
        char const* file;
        char const* function;
        uint32_t line;
    };

    struct CrashReport {
        CrashReason reason;
        char const* message;
        char const* expression;
        SourceLocation location;
    };

    using CrashHandler = void (*)(CrashReport const* report);

    void install_crash_handlers();
    void set_crash_handler(CrashHandler handler);

    [[nodiscard]] auto crash_reason_name(CrashReason reason) -> char const*;

    [[noreturn]] auto crash(CrashReport const& report) -> void;
    [[noreturn]] auto
    panic(char const* message, char const* file, uint32_t line, char const* function) -> void;
    [[noreturn]] auto
    unreachable(char const* message, char const* file, uint32_t line, char const* function) -> void;

} // namespace base

#define BASE_PANIC(message)                                                                        \
    do {                                                                                           \
        ::base::panic((message), __FILE__, static_cast<uint32_t>(__LINE__), __func__);             \
    } while (false)

#define BASE_UNREACHABLE(message)                                                                  \
    do {                                                                                           \
        ::base::unreachable((message), __FILE__, static_cast<uint32_t>(__LINE__), __func__);       \
    } while (false)
