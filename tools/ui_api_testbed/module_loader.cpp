#include "module_loader.h"

#if defined(_WIN32)
#include "app.h"

#include <base/config.h>
#include <base/fmt.h>
#include <cstdint>

#ifndef UI_API_TESTBED_SOURCE_DIR
#define UI_API_TESTBED_SOURCE_DIR "."
#endif

#ifndef UI_API_TESTBED_BINARY_DIR
#define UI_API_TESTBED_BINARY_DIR "."
#endif

#ifndef UI_API_TESTBED_BUILD_CONFIG
#define UI_API_TESTBED_BUILD_CONFIG "Debug"
#endif

namespace ui_api_testbed {
    namespace render = gui::render;

#if BASE_DEBUG
    inline constexpr wchar_t const* WATCHED_RELOAD_FILES[] = {
        L"tools\\ui_api_testbed\\app.cpp",
        L"tools\\ui_api_testbed\\app.h",
        L"tools\\assets\\ui_api_testbed_texture.png",
    };
    inline constexpr size_t WATCHED_RELOAD_FILE_COUNT =
        sizeof(WATCHED_RELOAD_FILES) / sizeof(WATCHED_RELOAD_FILES[0]);
    static_assert(WATCHED_RELOAD_FILE_COUNT == RELOAD_WATCH_FILE_COUNT);

    [[nodiscard]] auto wide_length(wchar_t const* text) -> size_t {
        size_t size = 0u;
        while (text[size] != L'\0') {
            ++size;
        }
        return size;
    }

    [[nodiscard]] auto append_wide(wchar_t* path, size_t capacity, wchar_t const* text) -> bool {
        size_t const path_size = wide_length(path);
        size_t const text_size = wide_length(text);
        if (path_size + text_size >= capacity) {
            return false;
        }
        for (size_t index = 0u; index <= text_size; ++index) {
            path[path_size + index] = text[index];
        }
        return true;
    }

    [[nodiscard]] auto append_separator(wchar_t* path, size_t capacity) -> bool {
        size_t const size = wide_length(path);
        if (size == 0u || path[size - 1u] == L'\\' || path[size - 1u] == L'/') {
            return true;
        }
        return append_wide(path, capacity, L"\\");
    }

    [[nodiscard]] auto narrow_to_wide(char const* text, wchar_t* out, size_t capacity) -> bool {
        if (capacity == 0u || capacity > static_cast<size_t>(0x7fffffffu)) {
            return false;
        }
        int const written = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, static_cast<int>(capacity)
        );
        return written > 0;
    }

    [[nodiscard]] auto exe_dir(wchar_t* path, size_t capacity) -> bool {
        if (capacity == 0u || capacity > static_cast<size_t>(0xffffffffu)) {
            return false;
        }
        DWORD const length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(capacity));
        if (length == 0u || length >= capacity) {
            return false;
        }
        size_t dir_length = static_cast<size_t>(length);
        while (dir_length != 0u && path[dir_length - 1u] != L'\\' &&
               path[dir_length - 1u] != L'/') {
            --dir_length;
        }
        path[dir_length] = L'\0';
        return true;
    }

    [[nodiscard]] auto exe_relative_path(wchar_t* path, size_t capacity, wchar_t const* file_name)
        -> bool {
        return exe_dir(path, capacity) && append_wide(path, capacity, file_name);
    }

    [[nodiscard]] auto
    source_relative_path(wchar_t* path, size_t capacity, wchar_t const* relative_path) -> bool {
        return narrow_to_wide(UI_API_TESTBED_SOURCE_DIR, path, capacity) &&
               append_separator(path, capacity) && append_wide(path, capacity, relative_path);
    }

    [[nodiscard]] auto file_write_time(wchar_t const* path, FILETIME* out_time) -> bool {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
            return false;
        }
        *out_time = data.ftLastWriteTime;
        return true;
    }

    [[nodiscard]] auto same_file_time(FILETIME const& a, FILETIME const& b) -> bool {
        return CompareFileTime(&a, &b) == 0;
    }

    auto refresh_watched_files(TestbedModule* module) -> void {
        for (size_t index = 0u; index < WATCHED_RELOAD_FILE_COUNT; ++index) {
            wchar_t path[MAX_PATH * 4] = {};
            FILETIME write_time = {};
            module->watched_valid[index] =
                source_relative_path(
                    path, sizeof(path) / sizeof(path[0]), WATCHED_RELOAD_FILES[index]
                ) &&
                file_write_time(path, &write_time);
            module->watched_write_times[index] = write_time;
        }
    }

    [[nodiscard]] auto watched_files_changed(TestbedModule* module) -> bool {
        bool changed = false;
        for (size_t index = 0u; index < WATCHED_RELOAD_FILE_COUNT; ++index) {
            wchar_t path[MAX_PATH * 4] = {};
            FILETIME write_time = {};
            bool const valid = source_relative_path(
                                   path, sizeof(path) / sizeof(path[0]), WATCHED_RELOAD_FILES[index]
                               ) &&
                               file_write_time(path, &write_time);
            if (valid != module->watched_valid[index] ||
                (valid && !same_file_time(write_time, module->watched_write_times[index]))) {
                changed = true;
                module->watched_valid[index] = valid;
                module->watched_write_times[index] = write_time;
            }
        }
        return changed;
    }

    auto close_rebuild_process(TestbedModule* module) -> void {
        if (module->rebuild_process.hProcess != nullptr) {
            CloseHandle(module->rebuild_process.hProcess);
        }
        if (module->rebuild_process.hThread != nullptr) {
            CloseHandle(module->rebuild_process.hThread);
        }
        module->rebuild_process = {};
        module->rebuild_running = false;
    }

    [[nodiscard]] auto append_quoted(wchar_t* command, size_t capacity, wchar_t const* value)
        -> bool {
        return append_wide(command, capacity, L"\"") && append_wide(command, capacity, value) &&
               append_wide(command, capacity, L"\"");
    }

    [[nodiscard]] auto start_rebuild(TestbedModule* module) -> bool {
        if (module->rebuild_running) {
            module->rebuild_requested = true;
            return true;
        }

        wchar_t build_dir[MAX_PATH * 4] = {};
        wchar_t config[64] = {};
        if (!narrow_to_wide(
                UI_API_TESTBED_BINARY_DIR, build_dir, sizeof(build_dir) / sizeof(build_dir[0])
            ) ||
            !narrow_to_wide(
                UI_API_TESTBED_BUILD_CONFIG, config, sizeof(config) / sizeof(config[0])
            )) {
            fmt::eprintf("ui_api_testbed: build path is too long\n");
            return false;
        }

        wchar_t command[4096] = {};
        bool const command_ok =
            append_wide(command, sizeof(command) / sizeof(command[0]), L"cmake --build ") &&
            append_quoted(command, sizeof(command) / sizeof(command[0]), build_dir) &&
            append_wide(command, sizeof(command) / sizeof(command[0]), L" --config ") &&
            append_quoted(command, sizeof(command) / sizeof(command[0]), config) &&
            append_wide(
                command,
                sizeof(command) / sizeof(command[0]),
                L" --target ui_api_testbed_module --parallel"
            );
        if (!command_ok) {
            fmt::eprintf("ui_api_testbed: rebuild command is too long\n");
            return false;
        }

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        BOOL const created = CreateProcessW(
            nullptr,
            command,
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &module->rebuild_process
        );
        if (!created) {
            fmt::eprintf("ui_api_testbed: failed to start module rebuild: %lu\n", GetLastError());
            module->rebuild_process = {};
            return false;
        }

        module->rebuild_running = true;
        module->rebuild_requested = false;
        fmt::printf("ui_api_testbed: rebuilding hot module\n");
        return true;
    }

    auto unload_module_dll(ModuleDll* dll) -> void {
        if (dll->library != nullptr) {
            FreeLibrary(dll->library);
        }
        if (dll->path[0] != L'\0') {
            BASE_UNUSED(DeleteFileW(dll->path));
        }
        *dll = {};
    }

    [[nodiscard]] auto load_module_dll(
        TestbedModule* module, wchar_t const* source_path, FILETIME write_time, ModuleDll* out_dll
    ) -> bool {
        wchar_t live_name[96] = {};
        wsprintfW(
            live_name,
            L"ui_api_testbed_module_hot_%lu_%u.dll",
            GetCurrentProcessId(),
            static_cast<unsigned>(++module->copy_index)
        );
        if (!exe_relative_path(
                out_dll->path, sizeof(out_dll->path) / sizeof(out_dll->path[0]), live_name
            )) {
            fmt::eprintf("ui_api_testbed: module copy path is too long\n");
            return false;
        }
        if (!CopyFileW(source_path, out_dll->path, FALSE)) {
            fmt::eprintf("ui_api_testbed: CopyFileW failed: %lu\n", GetLastError());
            return false;
        }

        out_dll->library = LoadLibraryW(out_dll->path);
        if (out_dll->library == nullptr) {
            fmt::eprintf("ui_api_testbed: LoadLibraryW failed: %lu\n", GetLastError());
            unload_module_dll(out_dll);
            return false;
        }

        auto const get_api = reinterpret_cast<ModuleApi const* (*)()>(
            GetProcAddress(out_dll->library, "ui_api_testbed_get_module_api")
        );
        if (get_api == nullptr) {
            fmt::eprintf("ui_api_testbed: module API export is missing\n");
            unload_module_dll(out_dll);
            return false;
        }

        out_dll->api = get_api();
        if (out_dll->api == nullptr || out_dll->api->version != MODULE_API_VERSION ||
            out_dll->api->runtime_size > MODULE_STORAGE_SIZE ||
            out_dll->api->runtime_alignment > MODULE_STORAGE_ALIGNMENT ||
            out_dll->api->create == nullptr || out_dll->api->destroy == nullptr ||
            out_dll->api->render_frame == nullptr) {
            fmt::eprintf("ui_api_testbed: module API is incompatible\n");
            unload_module_dll(out_dll);
            return false;
        }

        out_dll->write_time = write_time;
        return true;
    }

    [[nodiscard]] auto activate_module(
        TestbedModule* module, render::Context render_context, HWND hwnd, ModuleDll* dll
    ) -> bool {
        size_t const next_storage = module->active_storage == 0u ? 1u : 0u;
        if (!dll->api->create(module->storage[next_storage], render_context, hwnd)) {
            fmt::eprintf("ui_api_testbed: module create failed\n");
            return false;
        }

        if (module->runtime_valid) {
            module->dll.api->destroy(module->storage[module->active_storage], render_context);
        }
        unload_module_dll(&module->dll);
        module->dll = *dll;
        dll->library = nullptr;
        dll->path[0] = L'\0';
        module->active_storage = next_storage;
        module->runtime_valid = true;
        return true;
    }

    [[nodiscard]] auto
    reload_module(TestbedModule* module, render::Context render_context, HWND hwnd, bool force)
        -> bool {
        wchar_t source_path[MAX_PATH * 4] = {};
        if (!exe_relative_path(
                source_path, sizeof(source_path) / sizeof(source_path[0]), MODULE_FILE_NAME
            )) {
            fmt::eprintf("ui_api_testbed: module path is too long\n");
            return false;
        }

        FILETIME write_time = {};
        if (!file_write_time(source_path, &write_time)) {
            fmt::eprintf("ui_api_testbed: module is missing\n");
            return false;
        }
        if (!force && module->runtime_valid && same_file_time(write_time, module->dll.write_time)) {
            return false;
        }

        ModuleDll dll = {};
        if (!load_module_dll(module, source_path, write_time, &dll)) {
            return false;
        }
        if (!activate_module(module, render_context, hwnd, &dll)) {
            unload_module_dll(&dll);
            return false;
        }

        fmt::printf("ui_api_testbed: %s module\n", force ? "loaded hot-reload" : "reloaded hot");
        return true;
    }

    [[nodiscard]] auto module_dll_exists() -> bool {
        wchar_t source_path[MAX_PATH * 4] = {};
        FILETIME write_time = {};
        return exe_relative_path(
                   source_path, sizeof(source_path) / sizeof(source_path[0]), MODULE_FILE_NAME
               ) &&
               file_write_time(source_path, &write_time);
    }

    [[nodiscard]] auto
    finish_rebuild(TestbedModule* module, render::Context render_context, HWND hwnd) -> bool {
        if (!module->rebuild_running) {
            return false;
        }
        DWORD const wait = WaitForSingleObject(module->rebuild_process.hProcess, 0u);
        if (wait == WAIT_TIMEOUT) {
            return false;
        }

        DWORD exit_code = 1u;
        BASE_UNUSED(GetExitCodeProcess(module->rebuild_process.hProcess, &exit_code));
        close_rebuild_process(module);
        if (exit_code != 0u) {
            fmt::eprintf("ui_api_testbed: hot module rebuild failed: %lu\n", exit_code);
            return false;
        }

        refresh_watched_files(module);
        return reload_module(module, render_context, hwnd, false);
    }

    [[nodiscard]] auto
    rebuild_module_now(TestbedModule* module, render::Context render_context, HWND hwnd) -> bool {
        if (!start_rebuild(module)) {
            return false;
        }
        WaitForSingleObject(module->rebuild_process.hProcess, INFINITE);
        return finish_rebuild(module, render_context, hwnd);
    }
#endif

    auto init_testbed_module_storage(TestbedModule* module, Arena& arena) -> void {
#if BASE_DEBUG
        module->storage[0] = arena.allocate_bytes(MODULE_STORAGE_SIZE, MODULE_STORAGE_ALIGNMENT);
        module->storage[1] = arena.allocate_bytes(MODULE_STORAGE_SIZE, MODULE_STORAGE_ALIGNMENT);
#else
        module->storage = arena.allocate_bytes(MODULE_STORAGE_SIZE, MODULE_STORAGE_ALIGNMENT);
#endif
    }

    [[nodiscard]] auto
    load_testbed_module(TestbedModule* module, render::Context render_context, HWND hwnd) -> bool {
#if BASE_DEBUG
        refresh_watched_files(module);
        if (module_dll_exists() && reload_module(module, render_context, hwnd, true)) {
            return true;
        }
        return rebuild_module_now(module, render_context, hwnd);
#else
        module->api = ui_api_testbed_module_api();
        if (module->api == nullptr || module->api->version != MODULE_API_VERSION ||
            module->api->runtime_size > MODULE_STORAGE_SIZE ||
            module->api->runtime_alignment > MODULE_STORAGE_ALIGNMENT) {
            fmt::eprintf("ui_api_testbed: direct module API is incompatible\n");
            return false;
        }
        module->runtime_valid = module->api->create(module->storage, render_context, hwnd);
        return module->runtime_valid;
#endif
    }

    [[nodiscard]] auto
    update_testbed_module(TestbedModule* module, render::Context render_context, HWND hwnd)
        -> bool {
#if BASE_DEBUG
        bool reloaded = finish_rebuild(module, render_context, hwnd);
        if (watched_files_changed(module)) {
            BASE_UNUSED(start_rebuild(module));
        }
        if (module->rebuild_requested && !module->rebuild_running) {
            BASE_UNUSED(start_rebuild(module));
        }
        return reloaded;
#else
        BASE_UNUSED(module);
        BASE_UNUSED(render_context);
        BASE_UNUSED(hwnd);
        return false;
#endif
    }

    auto destroy_testbed_module(TestbedModule* module, render::Context render_context) -> void {
#if BASE_DEBUG
        if (module->rebuild_running) {
            WaitForSingleObject(module->rebuild_process.hProcess, INFINITE);
            close_rebuild_process(module);
        }
        if (module->runtime_valid) {
            module->dll.api->destroy(module->storage[module->active_storage], render_context);
            module->runtime_valid = false;
        }
        unload_module_dll(&module->dll);
#else
        if (module->runtime_valid) {
            module->api->destroy(module->storage, render_context);
            module->runtime_valid = false;
        }
#endif
    }

    [[nodiscard]] auto testbed_module_api(TestbedModule const& module) -> ModuleApi const* {
#if BASE_DEBUG
        return module.dll.api;
#else
        return module.api;
#endif
    }

    [[nodiscard]] auto testbed_module_storage(TestbedModule const& module) -> void* {
#if BASE_DEBUG
        return module.storage[module.active_storage];
#else
        return module.storage;
#endif
    }

} // namespace ui_api_testbed
#endif
