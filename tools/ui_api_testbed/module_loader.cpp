#include "module_loader.h"

#if defined(_WIN32)
#include "app.h"

#include <algorithm>
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

    [[nodiscard]] auto text_length(char const* text) -> size_t {
        size_t size = 0u;
        while (text[size] != '\0') {
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

    auto set_reload_phase(TestbedModule* module, ReloadPhase phase) -> void {
        module->reload_phase = phase;
        module->reload_phase_ticks = GetTickCount64();
    }

    auto reset_reload_log(TestbedModule* module) -> void {
        module->reload_log_size = 0u;
        module->reload_log_line_count = 0u;
        module->reload_log_line_open = false;
        module->reload_log_truncated = false;
    }

    [[nodiscard]] auto append_reload_log_line(TestbedModule* module, bool is_stderr)
        -> ReloadLogLine* {
        if (module->reload_log_line_count >= RELOAD_LOG_LINE_COUNT) {
            module->reload_log_truncated = true;
            return nullptr;
        }

        ReloadLogLine* const line = module->reload_log_lines + module->reload_log_line_count;
        *line = {
            .offset = static_cast<uint32_t>(module->reload_log_size),
            .size = 0u,
            .is_stderr = is_stderr,
        };
        module->reload_log_line_count += 1u;
        module->reload_log_line_open = true;
        return line;
    }

    auto append_reload_output(TestbedModule* module, bool is_stderr, char const* text, size_t size)
        -> void {
        for (size_t index = 0u; index < size; ++index) {
            char const c = text[index];
            if (c == '\r') {
                continue;
            }

            if (!module->reload_log_line_open || module->reload_log_line_count == 0u ||
                module->reload_log_lines[module->reload_log_line_count - 1u].is_stderr !=
                    is_stderr) {
                if (append_reload_log_line(module, is_stderr) == nullptr) {
                    return;
                }
            }

            if (c == '\n') {
                module->reload_log_line_open = false;
                continue;
            }

            if (module->reload_log_size >= RELOAD_LOG_CAPACITY) {
                module->reload_log_truncated = true;
                return;
            }

            module->reload_log[module->reload_log_size] = c;
            module->reload_log_size += 1u;
            module->reload_log_lines[module->reload_log_line_count - 1u].size += 1u;
        }
    }

    auto append_reload_output(TestbedModule* module, bool is_stderr, char const* text) -> void {
        append_reload_output(module, is_stderr, text, text_length(text));
    }

    auto read_rebuild_pipe(TestbedModule* module, HANDLE pipe, bool is_stderr) -> void {
        if (pipe == nullptr) {
            return;
        }

        for (;;) {
            DWORD available = 0u;
            if (!PeekNamedPipe(pipe, nullptr, 0u, nullptr, &available, nullptr) ||
                available == 0u) {
                return;
            }

            char buffer[4096] = {};
            DWORD const read_size = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            DWORD bytes_read = 0u;
            if (!ReadFile(pipe, buffer, read_size, &bytes_read, nullptr) || bytes_read == 0u) {
                return;
            }
            append_reload_output(module, is_stderr, buffer, static_cast<size_t>(bytes_read));
        }
    }

    auto read_rebuild_output(TestbedModule* module) -> void {
        read_rebuild_pipe(module, module->rebuild_stdout, false);
        read_rebuild_pipe(module, module->rebuild_stderr, true);
    }

    auto close_handle(HANDLE* handle) -> void {
        if (*handle != nullptr) {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }

    [[nodiscard]] auto create_rebuild_pipe(HANDLE* out_read, HANDLE* out_write) -> bool {
        SECURITY_ATTRIBUTES attributes = {};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        if (!CreatePipe(out_read, out_write, &attributes, 0u)) {
            return false;
        }
        if (!SetHandleInformation(*out_read, HANDLE_FLAG_INHERIT, 0u)) {
            close_handle(out_read);
            close_handle(out_write);
            return false;
        }
        return true;
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
        close_handle(&module->rebuild_stdout);
        close_handle(&module->rebuild_stderr);
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

        reset_reload_log(module);
        set_reload_phase(module, ReloadPhase::COMPILING);
        append_reload_output(module, false, "ui_api_testbed: rebuilding hot module\n");

        HANDLE stdout_read = nullptr;
        HANDLE stdout_write = nullptr;
        HANDLE stderr_read = nullptr;
        HANDLE stderr_write = nullptr;
        if (!create_rebuild_pipe(&stdout_read, &stdout_write) ||
            !create_rebuild_pipe(&stderr_read, &stderr_write)) {
            close_handle(&stdout_read);
            close_handle(&stdout_write);
            close_handle(&stderr_read);
            close_handle(&stderr_write);
            set_reload_phase(module, ReloadPhase::FAILED);
            fmt::eprintf("ui_api_testbed: failed to create rebuild output pipe\n");
            append_reload_output(
                module, true, "ui_api_testbed: failed to capture rebuild output\n"
            );
            return false;
        }

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        if (startup.hStdInput == INVALID_HANDLE_VALUE) {
            startup.hStdInput = nullptr;
        }
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write;

        BOOL const created = CreateProcessW(
            nullptr,
            command,
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &module->rebuild_process
        );
        close_handle(&stdout_write);
        close_handle(&stderr_write);
        if (!created) {
            fmt::eprintf("ui_api_testbed: failed to start module rebuild: %lu\n", GetLastError());
            append_reload_output(module, true, "ui_api_testbed: failed to start module rebuild\n");
            close_handle(&stdout_read);
            close_handle(&stderr_read);
            module->rebuild_process = {};
            set_reload_phase(module, ReloadPhase::FAILED);
            return false;
        }

        module->rebuild_stdout = stdout_read;
        module->rebuild_stderr = stderr_read;
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
        read_rebuild_output(module);
        DWORD const wait = WaitForSingleObject(module->rebuild_process.hProcess, 0u);
        if (wait == WAIT_TIMEOUT) {
            return false;
        }

        read_rebuild_output(module);
        DWORD exit_code = 1u;
        BASE_UNUSED(GetExitCodeProcess(module->rebuild_process.hProcess, &exit_code));
        close_rebuild_process(module);
        if (exit_code != 0u) {
            fmt::eprintf("ui_api_testbed: hot module rebuild failed: %lu\n", exit_code);
            append_reload_output(module, true, "ui_api_testbed: hot module rebuild failed\n");
            set_reload_phase(module, ReloadPhase::FAILED);
            return false;
        }

        append_reload_output(module, false, "ui_api_testbed: build finished; reloading DLL\n");
        set_reload_phase(module, ReloadPhase::RELOADING);
        refresh_watched_files(module);
        bool const reloaded = reload_module(module, render_context, hwnd, false);
        append_reload_output(
            module,
            !reloaded,
            reloaded ? "ui_api_testbed: hot reload complete\n"
                     : "ui_api_testbed: DLL reload failed\n"
        );
        set_reload_phase(module, reloaded ? ReloadPhase::COMPLETE : ReloadPhase::FAILED);
        return reloaded;
    }

    [[nodiscard]] auto
    rebuild_module_now(TestbedModule* module, render::Context render_context, HWND hwnd) -> bool {
        if (!start_rebuild(module)) {
            return false;
        }
        while (WaitForSingleObject(module->rebuild_process.hProcess, 16u) == WAIT_TIMEOUT) {
            read_rebuild_output(module);
        }
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
            while (WaitForSingleObject(module->rebuild_process.hProcess, 16u) == WAIT_TIMEOUT) {
                read_rebuild_output(module);
            }
            read_rebuild_output(module);
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

#if BASE_DEBUG
    [[nodiscard]] auto testbed_module_reload_overlay(TestbedModule const& module) -> ReloadOverlay {
        return {
            .phase = module.reload_phase,
            .text = module.reload_log,
            .text_size = module.reload_log_size,
            .lines = module.reload_log_lines,
            .line_count = module.reload_log_line_count,
            .truncated = module.reload_log_truncated,
        };
    }

    [[nodiscard]] auto testbed_module_reload_overlay_visible(TestbedModule const& module) -> bool {
        if (module.rebuild_running || module.reload_phase == ReloadPhase::COMPILING ||
            module.reload_phase == ReloadPhase::RELOADING) {
            return true;
        }
        if (module.reload_phase == ReloadPhase::FAILED) {
            return true;
        }
        if (module.reload_phase == ReloadPhase::IDLE) {
            return false;
        }
        return GetTickCount64() - module.reload_phase_ticks <= RELOAD_OVERLAY_HOLD_MS;
    }
#endif

} // namespace ui_api_testbed
#endif
