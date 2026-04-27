#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>

#if BASE_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <Windows.h>
#include <DbgHelp.h>
// clang-format on
#elif BASE_PLATFORM_MACOS
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

    base::CrashHandler crash_handler = nullptr;

    [[nodiscard]] auto has_text(char const* text) -> bool {
        return text != nullptr && text[0] != '\0';
    }

    [[nodiscard]] auto safe_text(char const* text, char const* fallback) -> char const* {
        return has_text(text) ? text : fallback;
    }

#if BASE_PLATFORM_WINDOWS
    [[nodiscard]] auto exception_code_name(DWORD code) -> char const* {
        switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "array bounds exceeded";
        case EXCEPTION_BREAKPOINT:
            return "breakpoint";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "data type misalignment";
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            return "floating-point denormal operand";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "floating-point divide by zero";
        case EXCEPTION_FLT_INEXACT_RESULT:
            return "floating-point inexact result";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "floating-point invalid operation";
        case EXCEPTION_FLT_OVERFLOW:
            return "floating-point overflow";
        case EXCEPTION_FLT_STACK_CHECK:
            return "floating-point stack check";
        case EXCEPTION_FLT_UNDERFLOW:
            return "floating-point underflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR:
            return "in-page error";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer divide by zero";
        case EXCEPTION_INT_OVERFLOW:
            return "integer overflow";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "privileged instruction";
        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";
        default:
            return "unknown exception";
        }
    }

    [[nodiscard]] auto access_violation_action(ULONG_PTR action) -> char const* {
        switch (action) {
        case 0u:
            return "read";
        case 1u:
            return "write";
        case 8u:
            return "execute";
        default:
            return "access";
        }
    }

    auto initialize_symbols(HANDLE process) -> bool {
        static bool symbols_initialized = false;

        if (symbols_initialized) {
            return true;
        }

        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        symbols_initialized = SymInitialize(process, nullptr, TRUE) != FALSE;
        return symbols_initialized;
    }

    auto write_source_location_for_address(HANDLE process, DWORD64 address) -> bool {
        constexpr DWORD SYMBOL_NAME_CAPACITY = 1024u;
        alignas(SYMBOL_INFO) unsigned char
            symbol_buffer[sizeof(SYMBOL_INFO) + SYMBOL_NAME_CAPACITY] = {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = SYMBOL_NAME_CAPACITY;

        DWORD64 symbol_displacement = 0u;
        bool const found_symbol =
            SymFromAddr(process, address, &symbol_displacement, symbol) != FALSE;

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0u;
        bool const found_line =
            SymGetLineFromAddr64(process, address, &line_displacement, &line) != FALSE;

        if (found_line && has_text(line.FileName)) {
            fmt::eprintf(
                "  location: %s:%lu", line.FileName, static_cast<unsigned long>(line.LineNumber));

            if (found_symbol && has_text(symbol->Name)) {
                fmt::eprintf(" in %s", symbol->Name);
            }

            fmt::eprintf("\n");
            return true;
        }

        if (found_symbol && has_text(symbol->Name)) {
            fmt::eprintf("  symbol: %s + 0x%llx\n",
                         symbol->Name,
                         static_cast<unsigned long long>(symbol_displacement));
            return true;
        }

        return false;
    }

    auto write_symbolized_address(HANDLE process,
                                  DWORD64 address,
                                  unsigned frame_index,
                                  bool symbols_ready) -> void {
        if (!symbols_ready) {
            fmt::eprintf("  %02u: 0x%llx\n", frame_index, static_cast<unsigned long long>(address));
            return;
        }

        constexpr DWORD SYMBOL_NAME_CAPACITY = 1024u;
        alignas(SYMBOL_INFO) unsigned char
            symbol_buffer[sizeof(SYMBOL_INFO) + SYMBOL_NAME_CAPACITY] = {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = SYMBOL_NAME_CAPACITY;

        DWORD64 symbol_displacement = 0u;
        bool const found_symbol =
            SymFromAddr(process, address, &symbol_displacement, symbol) != FALSE;

        if (found_symbol) {
            fmt::eprintf("  %02u: %s + 0x%llx",
                         frame_index,
                         symbol->Name,
                         static_cast<unsigned long long>(symbol_displacement));
        } else {
            fmt::eprintf("  %02u: 0x%llx", frame_index, static_cast<unsigned long long>(address));
        }

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0u;
        bool const found_line =
            SymGetLineFromAddr64(process, address, &line_displacement, &line) != FALSE;

        if (found_line && has_text(line.FileName)) {
            fmt::eprintf(" (%s:%lu)", line.FileName, static_cast<unsigned long>(line.LineNumber));
        }

        fmt::eprintf("\n");
    }

    auto write_stack_trace() -> void {
        constexpr uint32_t MAX_STACK_FRAMES = 64u;
        constexpr uint32_t STACK_FRAMES_TO_SKIP = 2u;

        void* frames[MAX_STACK_FRAMES] = {};
        USHORT const frame_count =
            CaptureStackBackTrace(STACK_FRAMES_TO_SKIP, MAX_STACK_FRAMES, frames, nullptr);

        if (frame_count == 0u) {
            fmt::eprintf("stack backtrace unavailable\n");
            return;
        }

        HANDLE const process = GetCurrentProcess();
        bool const symbols_ready = initialize_symbols(process);

        fmt::eprintf("stack backtrace:\n");

        for (USHORT index = 0u; index < frame_count; ++index) {
            DWORD64 const address =
                static_cast<DWORD64>(reinterpret_cast<uintptr_t>(frames[index]));
            write_symbolized_address(process, address, static_cast<unsigned>(index), symbols_ready);
        }
    }

    auto write_stack_trace_from_context(CONTEXT const* context_record) -> void {
        if (context_record == nullptr) {
            write_stack_trace();
            return;
        }

        constexpr uint32_t MAX_STACK_FRAMES = 64u;

        HANDLE const process = GetCurrentProcess();
        HANDLE const thread = GetCurrentThread();
        bool const symbols_ready = initialize_symbols(process);
        CONTEXT context = *context_record;
        STACKFRAME64 frame = {};
        DWORD machine_type = 0u;

#if defined(_M_X64)
        machine_type = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
        machine_type = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = context.Eip;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
        machine_type = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = context.Pc;
        frame.AddrFrame.Offset = context.Fp;
        frame.AddrStack.Offset = context.Sp;
#else
        write_stack_trace();
        return;
#endif

        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        fmt::eprintf("stack backtrace:\n");

        DWORD64 previous_address = 0u;
        uint32_t printed_frame_count = 0u;

        for (uint32_t walk_count = 0u; walk_count < MAX_STACK_FRAMES; ++walk_count) {
            if (frame.AddrPC.Offset == 0u) {
                break;
            }

            if (frame.AddrPC.Offset != previous_address) {
                write_symbolized_address(
                    process, frame.AddrPC.Offset, printed_frame_count, symbols_ready);
                previous_address = frame.AddrPC.Offset;
                ++printed_frame_count;
            }

            bool const walked = StackWalk64(machine_type,
                                            process,
                                            thread,
                                            &frame,
                                            &context,
                                            nullptr,
                                            SymFunctionTableAccess64,
                                            SymGetModuleBase64,
                                            nullptr) != FALSE;

            if (!walked) {
                break;
            }
        }
    }

    auto write_native_exception_report(EXCEPTION_POINTERS const* exception_info) -> void {
        DWORD const code = exception_info != nullptr && exception_info->ExceptionRecord != nullptr
                               ? exception_info->ExceptionRecord->ExceptionCode
                               : 0u;
        void const* const address =
            exception_info != nullptr && exception_info->ExceptionRecord != nullptr
                ? exception_info->ExceptionRecord->ExceptionAddress
                : nullptr;

        fmt::eprintf("fatal runtime error: %s\n",
                     base::crash_reason_name(base::CrashReason::PROCESS_FAULT));
        fmt::eprintf("  exception: %s (0x%08lx)\n",
                     exception_code_name(code),
                     static_cast<unsigned long>(code));

        if (address != nullptr) {
            fmt::eprintf("  address: 0x%llx\n",
                         static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
        }

        if (exception_info != nullptr && exception_info->ExceptionRecord != nullptr &&
            code == EXCEPTION_ACCESS_VIOLATION &&
            exception_info->ExceptionRecord->NumberParameters >= 2u) {
            ULONG_PTR const action = exception_info->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR const fault_address =
                exception_info->ExceptionRecord->ExceptionInformation[1];
            fmt::eprintf("  detail: attempted to %s address 0x%llx\n",
                         access_violation_action(action),
                         static_cast<unsigned long long>(fault_address));
        }

        if (address != nullptr) {
            HANDLE const process = GetCurrentProcess();
            bool const symbols_ready = initialize_symbols(process);
            DWORD64 const instruction_address =
                static_cast<DWORD64>(reinterpret_cast<uintptr_t>(address));

            if (symbols_ready && !write_source_location_for_address(process, instruction_address)) {
                fmt::eprintf("  location: source unavailable for fault address\n");
            }
        }

#if BASE_DEBUG
        write_stack_trace_from_context(exception_info != nullptr ? exception_info->ContextRecord
                                                                 : nullptr);
#endif
    }

    auto WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_info) -> LONG {
        fmt::eprintf("\n");
        write_native_exception_report(exception_info);
        std::fflush(stderr);
        return EXCEPTION_EXECUTE_HANDLER;
    }
#elif BASE_PLATFORM_MACOS
    [[nodiscard]] auto signal_name(int signal_number) -> char const* {
        switch (signal_number) {
        case SIGBUS:
            return "bus error";
        case SIGFPE:
            return "floating-point exception";
        case SIGILL:
            return "illegal instruction";
        case SIGSEGV:
            return "segmentation fault";
        default:
            return "unknown signal";
        }
    }

    auto write_stack_trace() -> void {
        constexpr int MAX_STACK_FRAMES = 64;
        constexpr int STACK_FRAMES_TO_SKIP = 2;

        void* frames[MAX_STACK_FRAMES] = {};
        int const frame_count = backtrace(frames, MAX_STACK_FRAMES);

        if (frame_count <= STACK_FRAMES_TO_SKIP) {
            fmt::eprintf("stack backtrace unavailable\n");
            return;
        }

        fmt::eprintf("stack backtrace:\n");
        backtrace_symbols_fd(
            frames + STACK_FRAMES_TO_SKIP, frame_count - STACK_FRAMES_TO_SKIP, STDERR_FILENO);
    }

    auto native_signal_handler(int signal_number, siginfo_t* signal_info, void*) -> void {
        fmt::eprintf("\nfatal runtime error: %s\n",
                     base::crash_reason_name(base::CrashReason::PROCESS_FAULT));
        fmt::eprintf("  signal: %s (%d)\n", signal_name(signal_number), signal_number);

        if (signal_info != nullptr && signal_info->si_addr != nullptr) {
            fmt::eprintf("  address: %p\n", signal_info->si_addr);
        }

#if BASE_DEBUG
        write_stack_trace();
#endif

        std::fflush(stderr);
        _exit(128 + signal_number);
    }
#else
    auto write_stack_trace() -> void {
        fmt::eprintf("stack backtrace unavailable on this platform\n");
    }
#endif

    auto write_location(base::SourceLocation const& location) -> void {
        fmt::eprintf("  location: %s:%u",
                     safe_text(location.file, "<unknown>"),
                     static_cast<unsigned>(location.line));

        if (has_text(location.function)) {
            fmt::eprintf(" in %s", location.function);
        }

        fmt::eprintf("\n");
    }

    auto write_crash_report(base::CrashReport const& report) -> void {
        fmt::eprintf("fatal runtime error: %s\n", base::crash_reason_name(report.reason));

        if (has_text(report.message)) {
            fmt::eprintf("  message: %s\n", report.message);
        }

        if (has_text(report.expression)) {
            fmt::eprintf("  condition: %s\n", report.expression);
        }

        write_location(report.location);

#if BASE_DEBUG
        write_stack_trace();
#endif
    }

} // namespace

namespace base {

    void install_crash_handlers() {
        static bool crash_handlers_installed = false;
        if (crash_handlers_installed) {
            return;
        }
        crash_handlers_installed = true;

#if BASE_PLATFORM_WINDOWS
        BASE_UNUSED(SetUnhandledExceptionFilter(unhandled_exception_filter));
#elif BASE_PLATFORM_MACOS
        struct sigaction action = {};
        action.sa_sigaction = native_signal_handler;
        action.sa_flags = SA_SIGINFO | SA_RESETHAND;
        BASE_UNUSED(sigemptyset(&action.sa_mask));
        BASE_UNUSED(sigaction(SIGBUS, &action, nullptr));
        BASE_UNUSED(sigaction(SIGFPE, &action, nullptr));
        BASE_UNUSED(sigaction(SIGILL, &action, nullptr));
        BASE_UNUSED(sigaction(SIGSEGV, &action, nullptr));
#endif
    }

    void set_crash_handler(CrashHandler handler) {
        crash_handler = handler;
    }

    auto crash_reason_name(CrashReason reason) -> char const* {
        switch (reason) {
        case CrashReason::ASSERTION_FAILURE:
            return "assertion failed";
        case CrashReason::PANIC:
            return "panic";
        case CrashReason::UNREACHABLE_CODE:
            return "unreachable code reached";
        case CrashReason::PROCESS_FAULT:
            return "process fault";
        }

        return "unknown crash";
    }

    [[noreturn]] auto crash(CrashReport const& report) -> void {
        if (crash_handler != nullptr) {
            crash_handler(&report);
        }

        write_crash_report(report);
        std::fflush(stderr);
        std::abort();
    }

    [[noreturn]] auto
    panic(char const* message, char const* file, uint32_t line, char const* function) -> void {
        CrashReport const report = {
            CrashReason::PANIC,
            message,
            nullptr,
            {file, function, line},
        };

        crash(report);
    }

    [[noreturn]] auto
    unreachable(char const* message, char const* file, uint32_t line, char const* function)
        -> void {
        CrashReport const report = {
            CrashReason::UNREACHABLE_CODE,
            message,
            nullptr,
            {file, function, line},
        };

        crash(report);
    }

} // namespace base
