#include <algorithm>
#include <base/fmt.h>
#include <cstring>
#include <gui/hot_reload.h>

#if GUI_HOT_RELOAD_AVAILABLE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gui {

    [[nodiscard]] auto
    hot_reload_api_valid(HotReloadApi const* api, size_t storage_size, size_t storage_alignment)
        -> bool {
        return api != nullptr && api->version == HOT_RELOAD_API_VERSION &&
               api->runtime_size <= storage_size && api->runtime_alignment <= storage_alignment &&
               api->create != nullptr && api->destroy != nullptr;
    }

#if GUI_HOT_RELOAD_AVAILABLE
    using HotReloadGetApiFn = auto (*)() -> void const*;

    struct HotReloadDll {
        HMODULE library = nullptr;
        void const* api = nullptr;
        FILETIME write_time = {};
        wchar_t path[MAX_PATH * 4] = {};
    };

    struct HotReloadState {
        HotReloadDll dll = {};
        void* storage[2] = {};
        size_t active_storage = 0u;
        uint32_t copy_index = 0u;
        PROCESS_INFORMATION rebuild_process = {};
        HANDLE rebuild_stdout = nullptr;
        HANDLE rebuild_stderr = nullptr;
        bool rebuild_running = false;
        bool rebuild_requested = false;
        bool runtime_valid = false;
        HotReloadPhase phase = HotReloadPhase::IDLE;
        uint64_t phase_ticks = 0u;
        char log[HOT_RELOAD_LOG_CAPACITY] = {};
        HotReloadLogLine log_lines[HOT_RELOAD_LOG_LINE_COUNT] = {};
        size_t log_size = 0u;
        size_t log_line_count = 0u;
        bool log_line_open = false;
        bool log_truncated = false;
        FILETIME* watched_write_times = nullptr;
        bool* watched_valid = nullptr;
        size_t watched_file_count = 0u;
    };

    [[nodiscard]] auto hot_reload_state(HotReloadModule* module) -> HotReloadState* {
        return static_cast<HotReloadState*>(module->handle);
    }

    [[nodiscard]] auto hot_reload_state(HotReloadModule const& module) -> HotReloadState const* {
        return static_cast<HotReloadState const*>(module.handle);
    }

    [[nodiscard]] auto hot_reload_api(void const* raw_api) -> HotReloadApi const* {
        return static_cast<HotReloadApi const*>(raw_api);
    }

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

    [[nodiscard]] auto narrow_to_wide(StrRef text, wchar_t* out, size_t capacity) -> bool {
        if (capacity == 0u || capacity > static_cast<size_t>(0x7fffffffu) ||
            text.size() > static_cast<size_t>(0x7fffffffu)) {
            return false;
        }
        if (text.empty()) {
            out[0] = L'\0';
            return true;
        }
        if (capacity == 1u) {
            return false;
        }

        int const written = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            out,
            static_cast<int>(capacity - 1u)
        );
        if (written <= 0) {
            return false;
        }
        out[written] = L'\0';
        return true;
    }

    [[nodiscard]] auto append_utf8(wchar_t* path, size_t capacity, StrRef text) -> bool {
        wchar_t wide[MAX_PATH * 4] = {};
        return narrow_to_wide(text, wide, sizeof(wide) / sizeof(wide[0])) &&
               append_wide(path, capacity, wide);
    }

    [[nodiscard]] auto copy_utf8_cstr(StrRef text, char* out, size_t capacity) -> bool {
        if (text.empty() || capacity == 0u || text.size() >= capacity) {
            return false;
        }
        for (size_t index = 0u; index < text.size(); ++index) {
            out[index] = text[index];
        }
        out[text.size()] = '\0';
        return true;
    }

    [[nodiscard]] auto path_is_absolute(StrRef path) -> bool {
        return (path.size() >= 2u && path[1u] == ':') || path.starts_with('\\') ||
               path.starts_with('/');
    }

    [[nodiscard]] auto exe_dir(wchar_t* path, size_t capacity) -> bool {
        if (capacity == 0u || capacity > static_cast<size_t>(0xffffffffu)) {
            return false;
        }
        DWORD const length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(capacity));
        if (length == 0u || static_cast<size_t>(length) >= capacity) {
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

    [[nodiscard]] auto exe_or_absolute_path(wchar_t* path, size_t capacity, StrRef file_name)
        -> bool {
        if (file_name.empty()) {
            return false;
        }
        if (path_is_absolute(file_name)) {
            return narrow_to_wide(file_name, path, capacity);
        }
        return exe_dir(path, capacity) && append_utf8(path, capacity, file_name);
    }

    [[nodiscard]] auto
    source_path(HotReloadDesc const& desc, wchar_t* path, size_t capacity, StrRef reload_path)
        -> bool {
        if (path_is_absolute(reload_path)) {
            return narrow_to_wide(reload_path, path, capacity);
        }
        return narrow_to_wide(desc.source_dir, path, capacity) &&
               append_separator(path, capacity) && append_utf8(path, capacity, reload_path);
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

    auto set_reload_phase(HotReloadState* state, HotReloadPhase phase) -> void {
        state->phase = phase;
        state->phase_ticks = GetTickCount64();
    }

    auto reset_reload_log(HotReloadState* state) -> void {
        state->log_size = 0u;
        state->log_line_count = 0u;
        state->log_line_open = false;
        state->log_truncated = false;
    }

    [[nodiscard]] auto append_reload_log_line(HotReloadState* state, bool is_stderr)
        -> HotReloadLogLine* {
        if (state->log_line_count >= HOT_RELOAD_LOG_LINE_COUNT) {
            state->log_truncated = true;
            return nullptr;
        }

        HotReloadLogLine* const line = state->log_lines + state->log_line_count;
        *line = {
            .offset = static_cast<uint32_t>(state->log_size),
            .size = 0u,
            .is_stderr = is_stderr,
        };
        state->log_line_count += 1u;
        state->log_line_open = true;
        return line;
    }

    auto append_reload_output(HotReloadState* state, bool is_stderr, StrRef text) -> void {
        for (size_t index = 0u; index < text.size(); ++index) {
            char const c = text[index];
            if (c == '\r') {
                continue;
            }

            if (!state->log_line_open || state->log_line_count == 0u ||
                state->log_lines[state->log_line_count - 1u].is_stderr != is_stderr) {
                if (append_reload_log_line(state, is_stderr) == nullptr) {
                    return;
                }
            }

            if (c == '\n') {
                state->log_line_open = false;
                continue;
            }

            if (state->log_size >= HOT_RELOAD_LOG_CAPACITY) {
                state->log_truncated = true;
                return;
            }

            state->log[state->log_size] = c;
            state->log_size += 1u;
            state->log_lines[state->log_line_count - 1u].size += 1u;
        }
    }

    auto append_reload_output(HotReloadState* state, bool is_stderr, char const* text) -> void {
        append_reload_output(state, is_stderr, StrRef(text, text_length(text)));
    }

    auto read_rebuild_pipe(HotReloadState* state, HANDLE pipe, bool is_stderr) -> void {
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
            append_reload_output(state, is_stderr, StrRef(buffer, static_cast<size_t>(bytes_read)));
        }
    }

    auto read_rebuild_output(HotReloadState* state) -> void {
        read_rebuild_pipe(state, state->rebuild_stdout, false);
        read_rebuild_pipe(state, state->rebuild_stderr, true);
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

    [[nodiscard]] auto watched_file_count(HotReloadState const* state, HotReloadDesc const& desc)
        -> size_t {
        return std::min(state->watched_file_count, desc.watched_files.size());
    }

    auto refresh_watched_files(HotReloadState* state, HotReloadDesc const& desc) -> void {
        size_t const count = watched_file_count(state, desc);
        for (size_t index = 0u; index < count; ++index) {
            wchar_t path[MAX_PATH * 4] = {};
            FILETIME write_time = {};
            state->watched_valid[index] =
                source_path(
                    desc, path, sizeof(path) / sizeof(path[0]), desc.watched_files[index]
                ) &&
                file_write_time(path, &write_time);
            state->watched_write_times[index] = write_time;
        }
    }

    [[nodiscard]] auto watched_files_changed(HotReloadState* state, HotReloadDesc const& desc)
        -> bool {
        bool changed = false;
        size_t const count = watched_file_count(state, desc);
        for (size_t index = 0u; index < count; ++index) {
            wchar_t path[MAX_PATH * 4] = {};
            FILETIME write_time = {};
            bool const valid =
                source_path(
                    desc, path, sizeof(path) / sizeof(path[0]), desc.watched_files[index]
                ) &&
                file_write_time(path, &write_time);
            if (valid != state->watched_valid[index] ||
                (valid && !same_file_time(write_time, state->watched_write_times[index]))) {
                changed = true;
                state->watched_valid[index] = valid;
                state->watched_write_times[index] = write_time;
            }
        }
        return changed;
    }

    auto close_rebuild_process(HotReloadState* state) -> void {
        if (state->rebuild_process.hProcess != nullptr) {
            CloseHandle(state->rebuild_process.hProcess);
        }
        if (state->rebuild_process.hThread != nullptr) {
            CloseHandle(state->rebuild_process.hThread);
        }
        close_handle(&state->rebuild_stdout);
        close_handle(&state->rebuild_stderr);
        state->rebuild_process = {};
        state->rebuild_running = false;
    }

    [[nodiscard]] auto append_quoted(wchar_t* command, size_t capacity, wchar_t const* value)
        -> bool {
        return append_wide(command, capacity, L"\"") && append_wide(command, capacity, value) &&
               append_wide(command, capacity, L"\"");
    }

    [[nodiscard]] auto start_rebuild(HotReloadState* state, HotReloadDesc const& desc) -> bool {
        if (state->rebuild_running) {
            state->rebuild_requested = true;
            return true;
        }

        wchar_t build_dir[MAX_PATH * 4] = {};
        wchar_t config[64] = {};
        wchar_t target[256] = {};
        if (!narrow_to_wide(desc.binary_dir, build_dir, sizeof(build_dir) / sizeof(build_dir[0])) ||
            !narrow_to_wide(desc.build_config, config, sizeof(config) / sizeof(config[0])) ||
            !narrow_to_wide(desc.build_target, target, sizeof(target) / sizeof(target[0]))) {
            fmt::eprintf("%s: build path is too long\n", desc.label);
            return false;
        }

        wchar_t command[4096] = {};
        bool const command_ok =
            append_wide(command, sizeof(command) / sizeof(command[0]), L"cmake --build ") &&
            append_quoted(command, sizeof(command) / sizeof(command[0]), build_dir) &&
            append_wide(command, sizeof(command) / sizeof(command[0]), L" --config ") &&
            append_quoted(command, sizeof(command) / sizeof(command[0]), config) &&
            append_wide(command, sizeof(command) / sizeof(command[0]), L" --target ") &&
            append_quoted(command, sizeof(command) / sizeof(command[0]), target) &&
            append_wide(command, sizeof(command) / sizeof(command[0]), L" --parallel");
        if (!command_ok) {
            fmt::eprintf("%s: rebuild command is too long\n", desc.label);
            return false;
        }

        reset_reload_log(state);
        set_reload_phase(state, HotReloadPhase::COMPILING);
        append_reload_output(state, false, desc.label);
        append_reload_output(state, false, ": rebuilding hot module\n");

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
            set_reload_phase(state, HotReloadPhase::FAILED);
            fmt::eprintf("%s: failed to create rebuild output pipe\n", desc.label);
            append_reload_output(state, true, desc.label);
            append_reload_output(state, true, ": failed to capture rebuild output\n");
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
            &state->rebuild_process
        );
        close_handle(&stdout_write);
        close_handle(&stderr_write);
        if (!created) {
            fmt::eprintf("%s: failed to start module rebuild: %lu\n", desc.label, GetLastError());
            append_reload_output(state, true, desc.label);
            append_reload_output(state, true, ": failed to start module rebuild\n");
            close_handle(&stdout_read);
            close_handle(&stderr_read);
            state->rebuild_process = {};
            set_reload_phase(state, HotReloadPhase::FAILED);
            return false;
        }

        state->rebuild_stdout = stdout_read;
        state->rebuild_stderr = stderr_read;
        state->rebuild_running = true;
        state->rebuild_requested = false;
        fmt::printf("%s: rebuilding hot module\n", desc.label);
        return true;
    }

    auto unload_module_dll(HotReloadDll* dll) -> void {
        if (dll->library != nullptr) {
            FreeLibrary(dll->library);
        }
        if (dll->path[0] != L'\0') {
            BASE_UNUSED(DeleteFileW(dll->path));
        }
        *dll = {};
    }

    [[nodiscard]] auto load_module_dll(
        HotReloadState* state,
        HotReloadDesc const& desc,
        wchar_t const* source_path,
        FILETIME write_time,
        HotReloadDll* out_dll
    ) -> bool {
        wchar_t copy_prefix[96] = {};
        wchar_t live_name[160] = {};
        if (!narrow_to_wide(
                desc.module_copy_prefix, copy_prefix, sizeof(copy_prefix) / sizeof(copy_prefix[0])
            )) {
            fmt::eprintf("%s: module copy prefix is too long\n", desc.label);
            return false;
        }
        wsprintfW(
            live_name,
            L"%ls_hot_%lu_%u.dll",
            copy_prefix,
            GetCurrentProcessId(),
            static_cast<unsigned>(++state->copy_index)
        );
        if (!exe_dir(out_dll->path, sizeof(out_dll->path) / sizeof(out_dll->path[0]))) {
            fmt::eprintf("%s: module copy path is too long\n", desc.label);
            return false;
        }
        if (!append_wide(
                out_dll->path, sizeof(out_dll->path) / sizeof(out_dll->path[0]), live_name
            )) {
            fmt::eprintf("%s: module copy path is too long\n", desc.label);
            return false;
        }
        if (!CopyFileW(source_path, out_dll->path, FALSE)) {
            fmt::eprintf("%s: CopyFileW failed: %lu\n", desc.label, GetLastError());
            return false;
        }

        out_dll->library = LoadLibraryW(out_dll->path);
        if (out_dll->library == nullptr) {
            fmt::eprintf("%s: LoadLibraryW failed: %lu\n", desc.label, GetLastError());
            unload_module_dll(out_dll);
            return false;
        }

        char export_name[256] = {};
        if (!copy_utf8_cstr(desc.api_export_name, export_name, sizeof(export_name))) {
            fmt::eprintf("%s: module API export name is too long\n", desc.label);
            unload_module_dll(out_dll);
            return false;
        }

        FARPROC const raw_get_api = GetProcAddress(out_dll->library, export_name);
        static_assert(sizeof(raw_get_api) == sizeof(HotReloadGetApiFn));
        HotReloadGetApiFn get_api = nullptr;
        std::memcpy(&get_api, &raw_get_api, sizeof(get_api));
        if (get_api == nullptr) {
            fmt::eprintf("%s: module API export is missing\n", desc.label);
            unload_module_dll(out_dll);
            return false;
        }

        out_dll->api = get_api();
        if (!hot_reload_api_valid(
                hot_reload_api(out_dll->api), desc.storage_size, desc.storage_alignment
            )) {
            fmt::eprintf("%s: module API is incompatible\n", desc.label);
            unload_module_dll(out_dll);
            return false;
        }

        out_dll->write_time = write_time;
        return true;
    }

    [[nodiscard]] auto
    activate_module(HotReloadState* state, HotReloadDesc const& desc, HotReloadDll* dll) -> bool {
        HotReloadApi const* const api = hot_reload_api(dll->api);
        size_t const next_storage = state->active_storage == 0u ? 1u : 0u;
        if (!api->create(state->storage[next_storage], desc.user_data)) {
            fmt::eprintf("%s: module create failed\n", desc.label);
            return false;
        }

        if (state->runtime_valid) {
            hot_reload_api(state->dll.api)
                ->destroy(state->storage[state->active_storage], desc.user_data);
        }
        unload_module_dll(&state->dll);
        state->dll = *dll;
        dll->library = nullptr;
        dll->path[0] = L'\0';
        state->active_storage = next_storage;
        state->runtime_valid = true;
        return true;
    }

    [[nodiscard]] auto reload_module(HotReloadState* state, HotReloadDesc const& desc, bool force)
        -> bool {
        wchar_t dll_path[MAX_PATH * 4] = {};
        if (!exe_or_absolute_path(
                dll_path, sizeof(dll_path) / sizeof(dll_path[0]), desc.module_file_name
            )) {
            fmt::eprintf("%s: module path is too long\n", desc.label);
            return false;
        }

        FILETIME write_time = {};
        if (!file_write_time(dll_path, &write_time)) {
            fmt::eprintf("%s: module is missing\n", desc.label);
            return false;
        }
        if (!force && state->runtime_valid && same_file_time(write_time, state->dll.write_time)) {
            return false;
        }

        HotReloadDll dll = {};
        if (!load_module_dll(state, desc, dll_path, write_time, &dll)) {
            return false;
        }
        if (!activate_module(state, desc, &dll)) {
            unload_module_dll(&dll);
            return false;
        }

        fmt::printf("%s: %s module\n", desc.label, force ? "loaded hot-reload" : "reloaded hot");
        return true;
    }

    [[nodiscard]] auto module_dll_exists(HotReloadDesc const& desc) -> bool {
        wchar_t dll_path[MAX_PATH * 4] = {};
        FILETIME write_time = {};
        return exe_or_absolute_path(
                   dll_path, sizeof(dll_path) / sizeof(dll_path[0]), desc.module_file_name
               ) &&
               file_write_time(dll_path, &write_time);
    }

    [[nodiscard]] auto finish_rebuild(HotReloadState* state, HotReloadDesc const& desc) -> bool {
        if (!state->rebuild_running) {
            return false;
        }
        read_rebuild_output(state);
        DWORD const wait = WaitForSingleObject(state->rebuild_process.hProcess, 0u);
        if (wait == WAIT_TIMEOUT) {
            return false;
        }

        read_rebuild_output(state);
        DWORD exit_code = 1u;
        BASE_UNUSED(GetExitCodeProcess(state->rebuild_process.hProcess, &exit_code));
        close_rebuild_process(state);
        if (exit_code != 0u) {
            fmt::eprintf("%s: hot module rebuild failed: %lu\n", desc.label, exit_code);
            append_reload_output(state, true, desc.label);
            append_reload_output(state, true, ": hot module rebuild failed\n");
            set_reload_phase(state, HotReloadPhase::FAILED);
            return false;
        }

        append_reload_output(state, false, desc.label);
        append_reload_output(state, false, ": build finished; reloading DLL\n");
        set_reload_phase(state, HotReloadPhase::RELOADING);
        refresh_watched_files(state, desc);
        bool const reloaded = reload_module(state, desc, false);
        append_reload_output(state, !reloaded, desc.label);
        append_reload_output(
            state, !reloaded, reloaded ? ": hot reload complete\n" : ": DLL reload failed\n"
        );
        set_reload_phase(state, reloaded ? HotReloadPhase::COMPLETE : HotReloadPhase::FAILED);
        return reloaded;
    }

    [[nodiscard]] auto rebuild_module_now(HotReloadState* state, HotReloadDesc const& desc)
        -> bool {
        if (!start_rebuild(state, desc)) {
            return false;
        }
        while (WaitForSingleObject(state->rebuild_process.hProcess, 16u) == WAIT_TIMEOUT) {
            read_rebuild_output(state);
        }
        return finish_rebuild(state, desc);
    }
#endif

    auto init_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc, Arena& arena)
        -> void {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState* const state = arena_new<HotReloadState>(arena);
        *state = {};
        state->storage[0] = arena.allocate_bytes(desc.storage_size, desc.storage_alignment);
        state->storage[1] = arena.allocate_bytes(desc.storage_size, desc.storage_alignment);
        state->watched_file_count = desc.watched_files.size();
        if (state->watched_file_count != 0u) {
            state->watched_write_times = arena_alloc<FILETIME>(arena, state->watched_file_count);
            state->watched_valid = arena_alloc<bool>(arena, state->watched_file_count);
        }
        module->handle = state;
#else
        BASE_UNUSED(module);
        BASE_UNUSED(desc);
        BASE_UNUSED(arena);
#endif
    }

    [[nodiscard]] auto load_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc)
        -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState* const state = hot_reload_state(module);
        if (state == nullptr) {
            return false;
        }
        refresh_watched_files(state, desc);
        if (module_dll_exists(desc) && reload_module(state, desc, true)) {
            return true;
        }
        return rebuild_module_now(state, desc);
#else
        BASE_UNUSED(module);
        BASE_UNUSED(desc);
        return false;
#endif
    }

    [[nodiscard]] auto update_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc)
        -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState* const state = hot_reload_state(module);
        if (state == nullptr) {
            return false;
        }
        bool const reloaded = finish_rebuild(state, desc);
        if (watched_files_changed(state, desc)) {
            BASE_UNUSED(start_rebuild(state, desc));
        }
        if (state->rebuild_requested && !state->rebuild_running) {
            BASE_UNUSED(start_rebuild(state, desc));
        }
        return reloaded;
#else
        BASE_UNUSED(module);
        BASE_UNUSED(desc);
        return false;
#endif
    }

    auto destroy_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc) -> void {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState* const state = hot_reload_state(module);
        if (state == nullptr) {
            return;
        }
        if (state->rebuild_running) {
            while (WaitForSingleObject(state->rebuild_process.hProcess, 16u) == WAIT_TIMEOUT) {
                read_rebuild_output(state);
            }
            read_rebuild_output(state);
            close_rebuild_process(state);
        }
        if (state->runtime_valid) {
            hot_reload_api(state->dll.api)
                ->destroy(state->storage[state->active_storage], desc.user_data);
            state->runtime_valid = false;
        }
        unload_module_dll(&state->dll);
#else
        BASE_UNUSED(module);
        BASE_UNUSED(desc);
#endif
    }

    [[nodiscard]] auto hot_reload_module_api(HotReloadModule const& module) -> void const* {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState const* const state = hot_reload_state(module);
        return state != nullptr ? state->dll.api : nullptr;
#else
        BASE_UNUSED(module);
        return nullptr;
#endif
    }

    [[nodiscard]] auto hot_reload_module_storage(HotReloadModule const& module) -> void* {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState const* const state = hot_reload_state(module);
        return state != nullptr ? state->storage[state->active_storage] : nullptr;
#else
        BASE_UNUSED(module);
        return nullptr;
#endif
    }

    [[nodiscard]] auto hot_reload_status(HotReloadModule const& module) -> HotReloadStatus {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState const* const state = hot_reload_state(module);
        if (state == nullptr) {
            return {};
        }
        return {
            .phase = state->phase,
            .text = state->log,
            .text_size = state->log_size,
            .lines = state->log_lines,
            .line_count = state->log_line_count,
            .truncated = state->log_truncated,
        };
#else
        BASE_UNUSED(module);
        return {};
#endif
    }

    [[nodiscard]] auto hot_reload_status_visible(HotReloadModule const& module) -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadState const* const state = hot_reload_state(module);
        if (state == nullptr) {
            return false;
        }
        if (state->rebuild_running || state->phase == HotReloadPhase::COMPILING ||
            state->phase == HotReloadPhase::RELOADING) {
            return true;
        }
        if (state->phase == HotReloadPhase::FAILED) {
            return true;
        }
        if (state->phase == HotReloadPhase::IDLE) {
            return false;
        }
        return GetTickCount64() - state->phase_ticks <= HOT_RELOAD_STATUS_HOLD_MS;
#else
        BASE_UNUSED(module);
        return false;
#endif
    }

} // namespace gui
