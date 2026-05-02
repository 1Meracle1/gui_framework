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

    inline constexpr size_t RELOAD_WATCH_FILE_COUNT = 3u;

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
        bool rebuild_running = false;
        bool rebuild_requested = false;
        bool runtime_valid = false;
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

} // namespace ui_api_testbed
#endif
