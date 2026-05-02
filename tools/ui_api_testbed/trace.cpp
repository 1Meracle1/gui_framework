#include "trace.h"

#if defined(_WIN32)
#include <algorithm>
#include <base/fmt.h>
#include <cstring>

namespace ui_api_testbed {
    namespace render = gui::render;

#if BASE_DEBUG
    [[nodiscard]] auto trace_active(ManualTrace const* trace) -> bool {
        return trace != nullptr && trace->file != nullptr;
    }

    [[nodiscard]] auto trace_counter() -> int64_t {
        LARGE_INTEGER counter = {};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    [[nodiscard]] auto trace_us(ManualTrace const& trace, int64_t counter) -> uint64_t {
        return static_cast<uint64_t>(
            (counter - trace.start_counter) * 1000000ll / trace.frequency.QuadPart
        );
    }

    [[nodiscard]] auto trace_elapsed_ms(ManualTrace const& trace) -> double {
        int64_t const counter = trace_counter();
        return static_cast<double>(counter - trace.start_counter) * 1000.0 /
               static_cast<double>(trace.frequency.QuadPart);
    }

    [[nodiscard]] auto file_time_100ns(FILETIME time) -> uint64_t {
        return (static_cast<uint64_t>(time.dwHighDateTime) << 32u) |
               static_cast<uint64_t>(time.dwLowDateTime);
    }

    [[nodiscard]] auto process_cpu_100ns() -> uint64_t {
        FILETIME creation = {};
        FILETIME exit = {};
        FILETIME kernel = {};
        FILETIME user = {};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            return 0u;
        }
        return file_time_100ns(kernel) + file_time_100ns(user);
    }

    auto trace_separator(ManualTrace* trace) -> void {
        if (!trace->first_event) {
            fmt::fprintf(trace->file, ",\n");
        }
        trace->first_event = false;
    }

    auto trace_zone_event(ManualTrace* trace, char const* name, char phase) -> void {
        if (!trace_active(trace)) {
            return;
        }
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"%s\",\"cat\":\"zone\",\"ph\":\"%c\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u}",
            name,
            phase,
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid
        );
    }

    TraceScope::TraceScope(ManualTrace* trace, char const* name)
        : m_trace(trace_active(trace) ? trace : nullptr), m_name(name) {
        if (m_trace != nullptr) {
            trace_zone_event(m_trace, m_name, 'B');
        }
    }

    TraceScope::~TraceScope() {
        if (m_trace != nullptr) {
            trace_zone_event(m_trace, m_name, 'E');
        }
    }

    auto trace_instant_u32(
        ManualTrace* trace, char const* name, render::SizeU32 window_size, uint32_t debug_layer
    ) -> void {
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"%s\",\"cat\":\"meta\",\"ph\":\"i\",\"s\":\"p\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u,\"args\":{\"build\":\"windows-msvc-debug\","
            "\"debug_layer\":%u,\"window_width\":%u,\"window_height\":%u,"
            "\"logical_processors\":%u,\"timestamp_unit\":\"microseconds\"}}",
            name,
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid,
            debug_layer,
            window_size.width,
            window_size.height,
            trace->logical_processor_count
        );
    }

    [[nodiscard]] auto
    trace_start(ManualTrace* trace, char const* path, render::SizeU32 size, bool enable_debug_layer)
        -> bool {
        if (trace == nullptr || path == nullptr || path[0] == '\0') {
            return false;
        }

        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
            fmt::eprintf("ui_api_testbed trace: failed to open %s\n", path);
            return false;
        }

        *trace = {};
        trace->file = file;
        QueryPerformanceFrequency(&trace->frequency);
        trace->start_counter = trace_counter();
        trace->cpu_start_100ns = process_cpu_100ns();
        trace->pid = GetCurrentProcessId();
        trace->tid = GetCurrentThreadId();
        SYSTEM_INFO system_info = {};
        GetSystemInfo(&system_info);
        trace->logical_processor_count =
            std::max(static_cast<uint32_t>(system_info.dwNumberOfProcessors), 1u);

        fmt::fprintf(trace->file, "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n");
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":%u,\"tid\":0,"
            "\"args\":{\"name\":\"ui_api_testbed\"}}",
            trace->pid
        );
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":%u,\"tid\":%u,"
            "\"args\":{\"name\":\"main\"}}",
            trace->pid,
            trace->tid
        );
        trace_instant_u32(trace, "trace_start", size, enable_debug_layer ? 1u : 0u);
        fmt::printf("ui_api_testbed trace: recording %s\n", path);
        return true;
    }

    auto trace_draw_command_counts(ManualTrace* trace, DrawCommandCounts const& counts) -> void {
        if (!trace_active(trace)) {
            return;
        }

        size_t const frame_index = trace->frame_count;
        trace->frame_count += 1u;
        trace->command_sum += counts.command_count;
        trace->primitive_sum += counts.primitive_count;
        trace->batch_sum += counts.batch_count;
        trace->styled_rect_sum += counts.styled_rect_count;
        trace->text_sum += counts.text_count;
        trace->layer_sum += counts.layer_count;
        trace->command_max = std::max(trace->command_max, counts.command_count);

        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"draw_commands\",\"cat\":\"counters\",\"ph\":\"C\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u,\"args\":{\"frame\":%zu,\"total\":%zu,"
            "\"primitive\":%zu,\"primitive_batches\":%zu,\"styled_rects\":%zu,"
            "\"text\":%zu,\"layers\":%zu}}",
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid,
            frame_index,
            counts.command_count,
            counts.primitive_count,
            counts.batch_count,
            counts.styled_rect_count,
            counts.text_count,
            counts.layer_count
        );
    }

    [[nodiscard]] auto trace_average(size_t sum, size_t count) -> double {
        return count != 0u ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    }

    auto trace_finish(
        ManualTrace* trace, char const* path, render::SizeU32 size, bool enable_debug_layer
    ) -> void {
        if (!trace_active(trace)) {
            return;
        }

        int64_t const end_counter = trace_counter();
        uint64_t const cpu_end_100ns = process_cpu_100ns();
        double const duration_ms = static_cast<double>(end_counter - trace->start_counter) *
                                   1000.0 / static_cast<double>(trace->frequency.QuadPart);
        double const duration_100ns = duration_ms * 10000.0;
        double const cpu_percent =
            duration_100ns > 0.0
                ? static_cast<double>(cpu_end_100ns - trace->cpu_start_100ns) * 100.0 /
                      duration_100ns / static_cast<double>(trace->logical_processor_count)
                : 0.0;
        double const fps = duration_ms > 0.0
                               ? static_cast<double>(trace->frame_count) * 1000.0 / duration_ms
                               : 0.0;
        double const avg_commands = trace_average(trace->command_sum, trace->frame_count);
        double const avg_primitives = trace_average(trace->primitive_sum, trace->frame_count);
        double const avg_batches = trace_average(trace->batch_sum, trace->frame_count);
        double const avg_styled_rects = trace_average(trace->styled_rect_sum, trace->frame_count);
        double const avg_text = trace_average(trace->text_sum, trace->frame_count);
        double const avg_layers = trace_average(trace->layer_sum, trace->frame_count);

        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"trace_summary\",\"cat\":\"summary\",\"ph\":\"i\",\"s\":\"p\","
            "\"ts\":%llu,\"pid\":%u,\"tid\":%u,\"args\":{\"duration_ms\":%.2f,"
            "\"process_cpu_percent\":%.2f,\"frames\":%zu,\"fps\":%.2f,"
            "\"window_width\":%u,\"window_height\":%u,\"debug_layer\":%u,"
            "\"avg_commands\":%.2f,"
            "\"avg_primitives\":%.2f,\"avg_primitive_batches\":%.2f,"
            "\"avg_styled_rects\":%.2f,\"avg_text\":%.2f,\"avg_layers\":%.2f,"
            "\"max_commands\":%zu}}",
            static_cast<unsigned long long>(trace_us(*trace, end_counter)),
            trace->pid,
            trace->tid,
            duration_ms,
            cpu_percent,
            trace->frame_count,
            fps,
            size.width,
            size.height,
            enable_debug_layer ? 1u : 0u,
            avg_commands,
            avg_primitives,
            avg_batches,
            avg_styled_rects,
            avg_text,
            avg_layers,
            trace->command_max
        );
        fmt::fprintf(trace->file, "\n]}\n");
        std::fclose(trace->file);
        trace->file = nullptr;

        fmt::printf(
            "ui_api_testbed trace: wrote %s frames=%zu duration_ms=%.1f fps=%.1f "
            "process_cpu=%.2f%% commands_avg=%.1f max=%zu primitive_avg=%.1f "
            "styled_rect_avg=%.1f text_avg=%.1f layers_avg=%.1f\n",
            path,
            trace->frame_count,
            duration_ms,
            fps,
            cpu_percent,
            avg_commands,
            trace->command_max,
            avg_primitives,
            avg_styled_rects,
            avg_text,
            avg_layers
        );
    }

    [[nodiscard]] auto trace_input_message(UINT message) -> bool {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
            return true;
        default:
            return false;
        }
    }
#endif

    [[nodiscard]] auto parse_u64(char const* text, uint64_t* out_value) -> bool {
        if (text == nullptr || text[0] == '\0' || out_value == nullptr) {
            return false;
        }
        uint64_t value = 0u;
        for (char const* at = text; *at != '\0'; ++at) {
            if (*at < '0' || *at > '9') {
                return false;
            }
            value = value * 10u + static_cast<uint64_t>(*at - '0');
        }
        *out_value = value;
        return true;
    }

    [[nodiscard]] auto consume_trace_value(
        int argc, char** argv, int* index, char const* option, char const** out_value
    ) -> bool {
        if (*index + 1 >= argc) {
            fmt::eprintf("%s requires a value\n", option);
            return false;
        }
        *index += 1;
        *out_value = argv[*index];
        return true;
    }

    [[nodiscard]] auto parse_trace_options(int argc, char** argv, TraceOptions* out_options)
        -> bool {
        for (int index = 1; index < argc; ++index) {
            char const* const arg = argv[index];
            if (std::strncmp(arg, "--trace=", 8u) == 0) {
                out_options->path = arg + 8u;
            } else if (std::strcmp(arg, "--trace") == 0) {
                if (!consume_trace_value(argc, argv, &index, "--trace", &out_options->path)) {
                    return false;
                }
            } else if (std::strncmp(arg, "--trace-warmup-ms=", 18u) == 0) {
                if (!parse_u64(arg + 18u, &out_options->warmup_ms)) {
                    fmt::eprintf("invalid --trace-warmup-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--trace-warmup-ms") == 0) {
                char const* value = nullptr;
                if (!consume_trace_value(argc, argv, &index, arg, &value) ||
                    !parse_u64(value, &out_options->warmup_ms)) {
                    fmt::eprintf("invalid --trace-warmup-ms value\n");
                    return false;
                }
            } else if (std::strncmp(arg, "--trace-duration-ms=", 20u) == 0) {
                if (!parse_u64(arg + 20u, &out_options->duration_ms)) {
                    fmt::eprintf("invalid --trace-duration-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--trace-duration-ms") == 0) {
                char const* value = nullptr;
                if (!consume_trace_value(argc, argv, &index, arg, &value) ||
                    !parse_u64(value, &out_options->duration_ms)) {
                    fmt::eprintf("invalid --trace-duration-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--no-d3d-debug-layer") == 0) {
                out_options->enable_debug_layer = false;
            } else {
                fmt::eprintf(
                    "usage: ui_api_testbed [--trace <path>] [--trace-warmup-ms N] "
                    "[--trace-duration-ms N] [--no-d3d-debug-layer]\n"
                );
                return false;
            }
        }
        if (out_options->path == nullptr &&
            (out_options->warmup_ms != 0u || out_options->duration_ms != 0u)) {
            fmt::eprintf("--trace-warmup-ms and --trace-duration-ms require --trace\n");
            return false;
        }
        return true;
    }

    [[nodiscard]] auto trace_requested(TraceOptions const& options) -> bool {
        return options.path != nullptr || options.warmup_ms != 0u || options.duration_ms != 0u;
    }

} // namespace ui_api_testbed
#endif
