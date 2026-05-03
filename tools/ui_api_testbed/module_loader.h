#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "shared.h"

#include <base/memory.h>
#include <windows.h>

namespace ui_api_testbed {

    inline constexpr size_t RELOAD_LOG_CAPACITY = 64u * 1024u;
    inline constexpr size_t RELOAD_LOG_LINE_COUNT = 1024u;
    inline constexpr uint32_t RELOAD_OVERLAY_HOLD_MS = 1000u;
    inline constexpr size_t RELOAD_WATCH_FILE_COUNT = 3u;

#if defined(_WIN32) && BASE_DEBUG
    enum class ReloadPhase : uint8_t {
        IDLE,
        COMPILING,
        RELOADING,
        COMPLETE,
        FAILED,
    };

    struct ReloadLogLine {
        uint32_t offset = 0u;
        uint32_t size = 0u;
        bool is_stderr = false;
    };

    struct ReloadOverlay {
        ReloadPhase phase = ReloadPhase::IDLE;
        char const* text = nullptr;
        size_t text_size = 0u;
        ReloadLogLine const* lines = nullptr;
        size_t line_count = 0u;
        bool truncated = false;
    };
#endif

    struct ModuleDll {
        HMODULE library = nullptr;
        ModuleApi const* api = nullptr;
        FILETIME write_time = {};
        wchar_t path[MAX_PATH * 4] = {};
    };

    struct TestbedModule {
#if defined(_WIN32) && BASE_DEBUG
        ModuleDll dll = {};
        void* storage[2] = {};
        size_t active_storage = 0u;
        uint32_t copy_index = 0u;
        PROCESS_INFORMATION rebuild_process = {};
        HANDLE rebuild_stdout = nullptr;
        HANDLE rebuild_stderr = nullptr;
        bool rebuild_running = false;
        bool rebuild_requested = false;
        bool runtime_valid = false;
        ReloadPhase reload_phase = ReloadPhase::IDLE;
        uint64_t reload_phase_ticks = 0u;
        char reload_log[RELOAD_LOG_CAPACITY] = {};
        ReloadLogLine reload_log_lines[RELOAD_LOG_LINE_COUNT] = {};
        size_t reload_log_size = 0u;
        size_t reload_log_line_count = 0u;
        bool reload_log_line_open = false;
        bool reload_log_truncated = false;
        FILETIME watched_write_times[RELOAD_WATCH_FILE_COUNT] = {};
        bool watched_valid[RELOAD_WATCH_FILE_COUNT] = {};
#else
        ModuleApi const* api = nullptr;
        void* storage = nullptr;
        bool runtime_valid = false;
#endif
    };

    auto init_testbed_module_storage(TestbedModule* module, Arena& arena) -> void;
    [[nodiscard]] auto
    load_testbed_module(TestbedModule* module, gui::render::Context render_context, HWND hwnd)
        -> bool;
    [[nodiscard]] auto
    update_testbed_module(TestbedModule* module, gui::render::Context render_context, HWND hwnd)
        -> bool;
    auto destroy_testbed_module(TestbedModule* module, gui::render::Context render_context) -> void;
    [[nodiscard]] auto testbed_module_api(TestbedModule const& module) -> ModuleApi const*;
    [[nodiscard]] auto testbed_module_storage(TestbedModule const& module) -> void*;
#if defined(_WIN32) && BASE_DEBUG
    [[nodiscard]] auto testbed_module_reload_overlay(TestbedModule const& module) -> ReloadOverlay;
    [[nodiscard]] auto testbed_module_reload_overlay_visible(TestbedModule const& module) -> bool;
#endif

} // namespace ui_api_testbed
#endif
