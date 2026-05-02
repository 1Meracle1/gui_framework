#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "shared.h"

#include <base/config.h>
#include <cstdint>
#include <cstdio>
#include <draw/draw.h>
#include <render/render.h>
#include <windows.h>

namespace ui_api_testbed {

    struct TraceOptions {
        char const* path = nullptr;
        uint64_t warmup_ms = 0u;
        uint64_t duration_ms = 0u;
        bool enable_debug_layer = true;
    };

    struct ManualTrace {
#if BASE_DEBUG
        std::FILE* file = nullptr;
        LARGE_INTEGER frequency = {};
        int64_t start_counter = 0;
        uint64_t cpu_start_100ns = 0u;
        uint32_t pid = 0u;
        uint32_t tid = 0u;
        uint32_t logical_processor_count = 1u;
        size_t frame_count = 0u;
        size_t command_sum = 0u;
        size_t primitive_sum = 0u;
        size_t batch_sum = 0u;
        size_t styled_rect_sum = 0u;
        size_t text_sum = 0u;
        size_t layer_sum = 0u;
        size_t command_max = 0u;
        bool first_event = true;
#endif
    };

#if BASE_DEBUG
    [[nodiscard]] auto trace_active(ManualTrace const* trace) -> bool;
    [[nodiscard]] auto trace_elapsed_ms(ManualTrace const& trace) -> double;

    class TraceScope final {
      public:
        TraceScope(ManualTrace* trace, char const* name);
        ~TraceScope();

      private:
        ManualTrace* m_trace = nullptr;
        char const* m_name = nullptr;
    };

#define UI_TRACE_JOIN_INNER(a, b) a##b
#define UI_TRACE_JOIN(a, b) UI_TRACE_JOIN_INNER(a, b)
#define TRACE_SCOPE(trace, name)                                                                   \
    ui_api_testbed::TraceScope UI_TRACE_JOIN(trace_scope_, __LINE__)((trace), (name))

    [[nodiscard]] auto trace_start(
        ManualTrace* trace, char const* path, gui::render::SizeU32 size, bool enable_debug_layer
    ) -> bool;
    auto trace_draw_command_counts(ManualTrace* trace, DrawCommandCounts const& counts) -> void;
    auto trace_finish(
        ManualTrace* trace, char const* path, gui::render::SizeU32 size, bool enable_debug_layer
    ) -> void;
    [[nodiscard]] auto trace_input_message(UINT message) -> bool;
#else
#define TRACE_SCOPE(trace, name) BASE_UNUSED(trace)
#endif

    [[nodiscard]] auto parse_trace_options(int argc, char** argv, TraceOptions* out_options)
        -> bool;
    [[nodiscard]] auto trace_requested(TraceOptions const& options) -> bool;

} // namespace ui_api_testbed
#endif
