#include "lsp_client_win32.h"

#if defined(_WIN32)

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/string_buffer.h>
#include <base/unicode.h>
#include <cstring>
#include <utility>

namespace code_editor {

    constexpr DWORD LSP_PIPE_BUFFER_SIZE = 64u * 1024u;
    constexpr DWORD LSP_WRITE_CHUNK_SIZE = 64u * 1024u;

    [[nodiscard]] auto handle_valid(HANDLE handle) -> bool {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    auto close_handle(HANDLE& handle) -> void {
        if (handle_valid(handle)) {
            CloseHandle(handle);
        }
        handle = nullptr;
    }

    [[nodiscard]] auto ensure_event(HANDLE& event) -> bool {
        if (event != nullptr) {
            return true;
        }
        event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return event != nullptr;
    }

    auto reset_overlapped(OVERLAPPED& overlapped, HANDLE event) -> void {
        ResetEvent(event);
        overlapped = {};
        overlapped.hEvent = event;
    }

    auto bridge_refresh(LspClient& client) -> void {
        client.bridge.diagnostics = client.diagnostics.slice();
        client.bridge.completions = client.completions.slice();
        client.bridge.locations = client.locations.slice();
        client.bridge.code_actions = client.code_actions.slice();
        client.bridge.symbols = client.symbols.slice();
        client.bridge.text_edits = client.text_edits.slice();
    }

    auto set_status(LspClient& client, LspStatusKind kind, StrRef text) -> void {
        client.bridge.status = kind;
        client.bridge.status_text = arena_copy_cstr(client.result_arena, text);
        client.bridge.status_generation += 1u;
    }

    auto set_progress(
        LspClient& client,
        StrRef title,
        StrRef message,
        bool active,
        bool has_percentage,
        size_t percentage
    ) -> void {
        if (title.empty()) {
            title = "clangd";
        }
        if (!active && message.empty()) {
            message = "done";
        }

        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer buffer = {};
        BASE_UNUSED(buffer.init(title.size() + message.size() + 64u, temp.arena()->resource()));
        BASE_UNUSED(buffer.write_string("LSP: "));
        BASE_UNUSED(buffer.write_string(title));
        if (!message.empty()) {
            BASE_UNUSED(buffer.write_string(" - "));
            BASE_UNUSED(buffer.write_string(message));
        }
        if (has_percentage) {
            BASE_UNUSED(buffer.write_string(" "));
            BASE_UNUSED(buffer.write_string(fmt::tprintf("%zu%%", percentage)));
        }

        client.bridge.progress_text = arena_copy_cstr(client.result_arena, buffer.str());
        client.bridge.progress_active = active;
        client.bridge.progress_generation += 1u;
    }

    [[nodiscard]] auto path_exists(StrRef path) -> bool {
        if (path.empty()) {
            return false;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const zpath = arena_copy_cstr(*temp.arena(), path);
        DWORD const attrs = GetFileAttributesA(zpath.data());
        return attrs != INVALID_FILE_ATTRIBUTES;
    }

    [[nodiscard]] auto lsp_path_is_directory(StrRef path) -> bool {
        if (path.empty()) {
            return false;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const zpath = arena_copy_cstr(*temp.arena(), path);
        DWORD const attrs = GetFileAttributesA(zpath.data());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0u;
    }

    [[nodiscard]] auto lsp_path_parent(StrRef path) -> StrRef {
        size_t const slash = path.find_last_of("\\/");
        if (slash == StrRef::NPOS) {
            return {};
        }
        if (slash == 2u && path[1u] == ':') {
            return path.prefix(3u);
        }
        return path.prefix(slash);
    }

    [[nodiscard]] auto path_join(Arena& arena, StrRef lhs, StrRef rhs) -> StrRef {
        StringBuffer buffer = {};
        BASE_UNUSED(buffer.init(lhs.size() + rhs.size() + 2u, arena.resource()));
        BASE_UNUSED(buffer.write_string(lhs));
        if (!lhs.empty() && !lhs.ends_with('\\') && !lhs.ends_with('/')) {
            BASE_UNUSED(buffer.write_byte('\\'));
        }
        BASE_UNUSED(buffer.write_string(rhs));
        return arena_copy_cstr(arena, buffer.str());
    }

    [[nodiscard]] auto compile_commands_file(Arena& arena, StrRef dir) -> StrRef {
        return path_join(arena, dir, "compile_commands.json");
    }

    [[nodiscard]] auto find_compile_commands_dir(Arena& arena, StrRef root, StrRef source)
        -> StrRef {
        StrRef const source_clangd = path_join(arena, path_join(arena, source, "build"), "clangd");
        if (path_exists(compile_commands_file(arena, source_clangd))) {
            return source_clangd;
        }

        StrRef current = root;
        for (;;) {
            if (path_exists(compile_commands_file(arena, current))) {
                return current;
            }
            StrRef const parent = lsp_path_parent(current);
            if (parent.empty() || parent == current) {
                break;
            }
            current = parent;
        }
        return {};
    }

    [[nodiscard]] auto make_wide(Arena& arena, StrRef text) -> wchar_t* {
        wchar_t* wide = nullptr;
        int wide_count = 0;
        if (!base::utf8_to_wide(text, arena, wide, wide_count)) {
            return nullptr;
        }
        BASE_UNUSED(wide_count);
        return wide;
    }

    [[nodiscard]] auto make_pipe_name(char (&name)[128]) -> bool {
        static uint32_t pipe_index = 0u;
        pipe_index += 1u;
        return fmt::snprintf(
                   name,
                   "\\\\.\\pipe\\gui_framework_code_editor_lsp_%lu_%u",
                   static_cast<unsigned long>(GetCurrentProcessId()),
                   pipe_index
               ) > 0;
    }

    [[nodiscard]] auto connect_named_pipe(HANDLE pipe) -> bool {
        if (ConnectNamedPipe(pipe, nullptr)) {
            return true;
        }
        return GetLastError() == ERROR_PIPE_CONNECTED;
    }

    [[nodiscard]] auto create_child_stdin_pipe(HANDLE& child_read, HANDLE& parent_write) -> bool {
        char name[128] = {};
        if (!make_pipe_name(name)) {
            return false;
        }

        SECURITY_ATTRIBUTES attributes = {};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        child_read = CreateNamedPipeA(
            name,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1u,
            LSP_PIPE_BUFFER_SIZE,
            LSP_PIPE_BUFFER_SIZE,
            0u,
            &attributes
        );
        if (!handle_valid(child_read)) {
            return false;
        }

        parent_write = CreateFileA(
            name,
            GENERIC_WRITE,
            0u,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr
        );
        if (!handle_valid(parent_write) || !connect_named_pipe(child_read)) {
            close_handle(child_read);
            close_handle(parent_write);
            return false;
        }
        return true;
    }

    [[nodiscard]] auto create_child_output_pipe(HANDLE& parent_read, HANDLE& child_write) -> bool {
        char name[128] = {};
        if (!make_pipe_name(name)) {
            return false;
        }

        parent_read = CreateNamedPipeA(
            name,
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1u,
            LSP_PIPE_BUFFER_SIZE,
            LSP_PIPE_BUFFER_SIZE,
            0u,
            nullptr
        );
        if (!handle_valid(parent_read)) {
            return false;
        }

        SECURITY_ATTRIBUTES attributes = {};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        child_write = CreateFileA(
            name, GENERIC_WRITE, 0u, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (!handle_valid(child_write) || !connect_named_pipe(parent_read)) {
            close_handle(parent_read);
            close_handle(child_write);
            return false;
        }
        return true;
    }

    auto finish_write(LspClient& client) -> void;

    auto pump_write(LspClient& client) -> void {
        LspPipeWrite& io = client.stdin_io;
        if (io.pending || !handle_valid(client.stdin_write) || io.event == nullptr) {
            return;
        }
        if (io.offset == io.bytes.size()) {
            io.bytes.clear();
            io.offset = 0u;
            if (!io.backlog.empty()) {
                io.bytes = std::move(io.backlog);
                BASE_UNUSED(io.backlog.init(4096u, client.arena.resource()));
            }
        }
        if (io.offset == io.bytes.size()) {
            return;
        }

        reset_overlapped(io.overlapped, io.event);
        DWORD const to_write =
            std::min<DWORD>(LSP_WRITE_CHUNK_SIZE, static_cast<DWORD>(io.bytes.size() - io.offset));
        DWORD written = 0u;
        if (WriteFile(
                client.stdin_write, io.bytes.data() + io.offset, to_write, &written, &io.overlapped
            )) {
            io.offset += written;
            return;
        }

        DWORD const error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            io.pending = true;
            return;
        }
        close_handle(client.stdin_write);
    }

    auto finish_write(LspClient& client) -> void {
        LspPipeWrite& io = client.stdin_io;
        if (!io.pending || WaitForSingleObject(io.event, 0u) != WAIT_OBJECT_0) {
            return;
        }

        DWORD written = 0u;
        io.pending = false;
        if (!GetOverlappedResult(client.stdin_write, &io.overlapped, &written, FALSE)) {
            close_handle(client.stdin_write);
            return;
        }
        io.offset += written;
        pump_write(client);
    }

    auto write_json(LspClient& client, StrRef json) -> void {
        if (!handle_valid(client.stdin_write)) {
            return;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer message = {};
        BASE_UNUSED(message.init(json.size() + 64u, temp.arena()->resource()));
        if (!lsp_write_json_rpc_message(message, json)) {
            return;
        }

        Vec<char>& queue =
            client.stdin_io.pending ? client.stdin_io.backlog : client.stdin_io.bytes;
        size_t const copied = queue.append(Slice<char const>(message.data(), message.size()));
        if (copied != message.size()) {
            close_handle(client.stdin_write);
            return;
        }
        pump_write(client);
    }

    [[nodiscard]] auto
    next_id(LspClient& client, LspRequestKind kind, StrRef path, LspPosition position = {})
        -> int32_t {
        int32_t const id = client.next_id++;
        BASE_UNUSED(client.pending.push_back({
            .id = id,
            .kind = kind,
            .path = arena_copy_cstr(client.arena, path),
            .position = position,
        }));
        return id;
    }

    [[nodiscard]] auto take_pending(LspClient& client, int32_t id) -> LspClientPendingRequest {
        for (size_t index = 0u; index < client.pending.size(); ++index) {
            if (client.pending[index].id == id) {
                LspClientPendingRequest const result = client.pending[index];
                client.pending.ordered_remove(index);
                return result;
            }
        }
        return {};
    }

    auto write_position(StringBuffer& json, StrRef text, LspPosition position) -> void {
        LspPosition const utf16 = lsp_position_byte_to_utf16(text, position);
        BASE_UNUSED(json.write_string("{\"line\":"));
        BASE_UNUSED(json.write_string(fmt::tprintf("%zu", utf16.line)));
        BASE_UNUSED(json.write_string(",\"character\":"));
        BASE_UNUSED(json.write_string(fmt::tprintf("%zu", utf16.column)));
        BASE_UNUSED(json.write_byte('}'));
    }

    auto write_range(StringBuffer& json, StrRef text, LspRange range) -> void {
        BASE_UNUSED(json.write_string("{\"start\":"));
        write_position(json, text, range.start);
        BASE_UNUSED(json.write_string(",\"end\":"));
        write_position(json, text, range.end);
        BASE_UNUSED(json.write_byte('}'));
    }

    auto write_diagnostic(StringBuffer& json, StrRef text, LspDiagnostic const& diagnostic)
        -> void {
        BASE_UNUSED(json.write_string("{\"range\":"));
        write_range(json, text, diagnostic.range);
        BASE_UNUSED(json.write_string(",\"severity\":"));
        BASE_UNUSED(
            json.write_string(fmt::tprintf("%u", static_cast<uint32_t>(diagnostic.severity)))
        );
        if (!diagnostic.code.empty()) {
            BASE_UNUSED(json.write_string(",\"code\":"));
            lsp_json_write_escaped_string(json, diagnostic.code);
        }
        if (!diagnostic.source.empty()) {
            BASE_UNUSED(json.write_string(",\"source\":"));
            lsp_json_write_escaped_string(json, diagnostic.source);
        }
        BASE_UNUSED(json.write_string(",\"message\":"));
        lsp_json_write_escaped_string(json, diagnostic.message);
        BASE_UNUSED(json.write_byte('}'));
    }

    [[nodiscard]] auto lsp_range_intersects(LspRange lhs, LspRange rhs) -> bool {
        return !lsp_position_less(lhs.end, rhs.start) && !lsp_position_less(rhs.end, lhs.start);
    }

    auto write_text_document_position_params(
        StringBuffer& json, LspClient& client, LspEditorRequest const& request
    ) -> void {
        StrRef const uri = client.current_uri;
        BASE_UNUSED(json.write_string("\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(json, uri);
        BASE_UNUSED(json.write_string("},\"position\":"));
        write_position(json, client.current_text, request.position);
    }

    auto send_request_json(
        LspClient& client,
        LspRequestKind kind,
        StrRef path,
        StrRef method,
        StrRef params,
        LspPosition position = {}
    ) -> void {
        int32_t const id = next_id(client, kind, path, position);
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer json = {};
        BASE_UNUSED(json.init(params.size() + 256u, temp.arena()->resource()));
        BASE_UNUSED(json.write_string("{\"jsonrpc\":\"2.0\",\"id\":"));
        BASE_UNUSED(json.write_string(fmt::tprintf("%d", id)));
        BASE_UNUSED(json.write_string(",\"method\":"));
        lsp_json_write_escaped_string(json, method);
        BASE_UNUSED(json.write_string(",\"params\":"));
        BASE_UNUSED(json.write_string(params));
        BASE_UNUSED(json.write_byte('}'));
        write_json(client, json.str());
    }

    auto send_notification_json(LspClient& client, StrRef method, StrRef params) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer json = {};
        BASE_UNUSED(json.init(params.size() + 192u, temp.arena()->resource()));
        BASE_UNUSED(json.write_string("{\"jsonrpc\":\"2.0\",\"method\":"));
        lsp_json_write_escaped_string(json, method);
        BASE_UNUSED(json.write_string(",\"params\":"));
        BASE_UNUSED(json.write_string(params));
        BASE_UNUSED(json.write_byte('}'));
        write_json(client, json.str());
    }

    auto send_initialize(LspClient& client) -> void {
        int32_t const id = next_id(client, LspRequestKind::NONE, {});
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const root_uri = lsp_path_to_file_uri(*temp.arena(), client.root_path);
        StringBuffer json = {};
        BASE_UNUSED(json.init(2048u, temp.arena()->resource()));
        BASE_UNUSED(json.write_string("{\"jsonrpc\":\"2.0\",\"id\":"));
        BASE_UNUSED(json.write_string(fmt::tprintf("%d", id)));
        BASE_UNUSED(json.write_string(
            ",\"method\":\"initialize\",\"params\":{\"processId\":null,\"rootUri\":"
        ));
        lsp_json_write_escaped_string(json, root_uri);
        BASE_UNUSED(json.write_string(
            ",\"capabilities\":{\"textDocument\":{\"synchronization\":{\"didSave\":false},"
            "\"completion\":{\"completionItem\":{\"snippetSupport\":false}},\"hover\":{},"
            "\"definition\":{},\"declaration\":{},\"references\":{},\"rename\":{},"
            "\"formatting\":{},\"codeAction\":{},\"documentSymbol\":{}}},"
            "\"trace\":\"off\"}}"
        ));
        write_json(client, json.str());
    }

    [[nodiscard]] auto lsp_client_init(LspClient& client) -> bool {
        client.arena.init();
        client.document_arena.init();
        client.message_arena.init();
        client.result_arena.init();
        if (!lsp_framer_init(client.framer, client.arena.resource()) ||
            !client.stdin_io.bytes.init(4096u, client.arena.resource()) ||
            !client.stdin_io.backlog.init(4096u, client.arena.resource()) ||
            !client.pending.init(32u, client.arena.resource()) ||
            !client.diagnostics.init(64u, client.result_arena.resource()) ||
            !client.completions.init(64u, client.result_arena.resource()) ||
            !client.locations.init(32u, client.result_arena.resource()) ||
            !client.code_actions.init(16u, client.result_arena.resource()) ||
            !client.symbols.init(64u, client.result_arena.resource()) ||
            !client.text_edits.init(64u, client.result_arena.resource())) {
            return false;
        }
        set_status(client, LspStatusKind::OFF, "LSP off");
        bridge_refresh(client);
        return true;
    }

    auto terminate_process(LspClient& client) -> void {
        if (client.process.hProcess != nullptr) {
            BASE_UNUSED(TerminateProcess(client.process.hProcess, 0u));
            WaitForSingleObject(client.process.hProcess, 250u);
        }
        if (client.process.hThread != nullptr) {
            CloseHandle(client.process.hThread);
        }
        if (client.process.hProcess != nullptr) {
            CloseHandle(client.process.hProcess);
        }
        client.process = {};
    }

    auto cancel_pending_io(HANDLE handle, OVERLAPPED& overlapped, bool& pending) -> void {
        if (!pending || !handle_valid(handle)) {
            pending = false;
            return;
        }

        BASE_UNUSED(CancelIoEx(handle, &overlapped));
        DWORD bytes = 0u;
        BASE_UNUSED(GetOverlappedResult(handle, &overlapped, &bytes, TRUE));
        pending = false;
    }

    auto lsp_client_shutdown(LspClient& client) -> void {
        if (client.started && handle_valid(client.stdin_write)) {
            write_json(
                client,
                "{\"jsonrpc\":\"2.0\",\"id\":999999,\"method\":\"shutdown\",\"params\":null}"
            );
            write_json(client, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}");
        }
        cancel_pending_io(client.stdin_write, client.stdin_io.overlapped, client.stdin_io.pending);
        cancel_pending_io(
            client.stdout_read, client.stdout_io.overlapped, client.stdout_io.pending
        );
        cancel_pending_io(
            client.stderr_read, client.stderr_io.overlapped, client.stderr_io.pending
        );
        close_handle(client.stdin_write);
        close_handle(client.stdout_read);
        close_handle(client.stderr_read);
        close_handle(client.stdin_io.event);
        close_handle(client.stdout_io.event);
        close_handle(client.stderr_io.event);
        terminate_process(client);
        client.stdin_io.bytes.destroy();
        client.stdin_io.backlog.destroy();
        client.pending.destroy();
        client.diagnostics.destroy();
        client.completions.destroy();
        client.locations.destroy();
        client.code_actions.destroy();
        client.symbols.destroy();
        client.text_edits.destroy();
        client.framer.bytes.destroy();
        client.result_arena.destroy();
        client.message_arena.destroy();
        client.document_arena.destroy();
        client.arena.destroy();
    }

    auto read_pipe_messages(LspClient& client) -> void;
    auto read_stderr(LspClient& client) -> void;

    [[nodiscard]] auto lsp_client_start(LspClient& client, StrRef root_path, StrRef source_path)
        -> bool {
        if (client.started) {
            return true;
        }
        if (root_path.empty()) {
            set_status(client, LspStatusKind::WARNING, "LSP: no workspace");
            return false;
        }

        client.root_path = arena_copy_cstr(client.arena, root_path);
        client.compile_commands_dir = arena_copy_cstr(
            client.arena, find_compile_commands_dir(client.arena, root_path, source_path)
        );

        if (!ensure_event(client.stdin_io.event) || !ensure_event(client.stdout_io.event) ||
            !ensure_event(client.stderr_io.event)) {
            set_status(client, LspStatusKind::FAILED, "LSP: IO event creation failed");
            return false;
        }

        HANDLE stdin_read = nullptr;
        HANDLE stdout_write = nullptr;
        HANDLE stderr_write = nullptr;
        if (!create_child_stdin_pipe(stdin_read, client.stdin_write) ||
            !create_child_output_pipe(client.stdout_read, stdout_write) ||
            !create_child_output_pipe(client.stderr_read, stderr_write)) {
            close_handle(stdin_read);
            close_handle(stdout_write);
            close_handle(stderr_write);
            close_handle(client.stdin_write);
            close_handle(client.stdout_read);
            close_handle(client.stderr_read);
            set_status(client, LspStatusKind::FAILED, "LSP: pipe creation failed");
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer command = {};
        BASE_UNUSED(command.init(1024u, temp.arena()->resource()));
        BASE_UNUSED(command.write_string("clangd --background-index"));
        if (!client.compile_commands_dir.empty()) {
            BASE_UNUSED(command.write_string(" --compile-commands-dir=\""));
            BASE_UNUSED(command.write_string(client.compile_commands_dir));
            BASE_UNUSED(command.write_byte('"'));
        }

        wchar_t* const wide_command = make_wide(*temp.arena(), command.str());
        wchar_t* const wide_root = make_wide(*temp.arena(), root_path);
        if (wide_command == nullptr || wide_root == nullptr) {
            close_handle(stdin_read);
            close_handle(stdout_write);
            close_handle(stderr_write);
            close_handle(client.stdin_write);
            close_handle(client.stdout_read);
            close_handle(client.stderr_read);
            set_status(client, LspStatusKind::FAILED, "LSP: bad workspace path");
            return false;
        }

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write;

        BOOL const created = CreateProcessW(
            nullptr,
            wide_command,
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            lsp_path_is_directory(root_path) ? wide_root : nullptr,
            &startup,
            &client.process
        );
        close_handle(stdin_read);
        close_handle(stdout_write);
        close_handle(stderr_write);
        if (!created) {
            close_handle(client.stdin_write);
            close_handle(client.stdout_read);
            close_handle(client.stderr_read);
            set_status(client, LspStatusKind::FAILED, "LSP: clangd not found on PATH");
            return false;
        }

        client.started = true;
        read_pipe_messages(client);
        read_stderr(client);
        set_status(
            client,
            client.compile_commands_dir.empty() ? LspStatusKind::WARNING : LspStatusKind::STARTING,
            client.compile_commands_dir.empty() ? "LSP: no compile_commands.json" : "LSP starting"
        );
        send_initialize(client);
        return true;
    }

    [[nodiscard]] auto json_member_string(LspJsonValue const* object, StrRef key, StrRef& out)
        -> bool {
        return lsp_json_string(lsp_json_object_get(object, key), out);
    }

    [[nodiscard]] auto json_member_size(LspJsonValue const* object, StrRef key, size_t& out)
        -> bool {
        return lsp_json_size(lsp_json_object_get(object, key), out);
    }

    [[nodiscard]] auto json_member_bool(LspJsonValue const* object, StrRef key, bool& out) -> bool {
        return lsp_json_bool(lsp_json_object_get(object, key), out);
    }

    auto parse_work_done_progress(LspClient& client, LspJsonValue const* params) -> void {
        LspJsonValue const* const value = lsp_json_object_get(params, "value");
        if (value == nullptr || value->kind != LspJsonKind::OBJECT) {
            return;
        }

        StrRef kind = {};
        StrRef title = {};
        StrRef message = {};
        size_t percentage = 0u;
        BASE_UNUSED(json_member_string(value, "kind", kind));
        BASE_UNUSED(json_member_string(value, "title", title));
        BASE_UNUSED(json_member_string(value, "message", message));
        bool const has_percentage = json_member_size(value, "percentage", percentage);
        set_progress(client, title, message, kind != "end", has_percentage, percentage);
    }

    auto parse_window_progress(LspClient& client, LspJsonValue const* params) -> void {
        if (params == nullptr || params->kind != LspJsonKind::OBJECT) {
            return;
        }

        StrRef title = {};
        StrRef message = {};
        bool done = false;
        size_t percentage = 0u;
        BASE_UNUSED(json_member_string(params, "title", title));
        BASE_UNUSED(json_member_string(params, "message", message));
        BASE_UNUSED(json_member_bool(params, "done", done));
        bool const has_percentage = json_member_size(params, "percentage", percentage);
        set_progress(client, title, message, !done, has_percentage, percentage);
    }

    [[nodiscard]] auto parse_position(LspJsonValue const* value, LspPosition& out) -> bool {
        return json_member_size(value, "line", out.line) &&
               json_member_size(value, "character", out.column);
    }

    [[nodiscard]] auto parse_range(LspJsonValue const* value, LspRange& out) -> bool {
        return parse_position(lsp_json_object_get(value, "start"), out.start) &&
               parse_position(lsp_json_object_get(value, "end"), out.end);
    }

    [[nodiscard]] auto copy_result(LspClient& client, StrRef text) -> StrRef {
        return arena_copy_cstr(client.result_arena, text);
    }

    [[nodiscard]] auto current_doc_text(LspClient const& client, StrRef path) -> StrRef {
        return path == client.current_path ? client.current_text : StrRef();
    }

    [[nodiscard]] auto parse_uri_path(LspClient& client, LspJsonValue const* value) -> StrRef {
        StrRef uri = {};
        if (!lsp_json_string(value, uri)) {
            return {};
        }
        ArenaTemp temp = begin_thread_temp_arena();
        return copy_result(client, lsp_file_uri_to_path(*temp.arena(), uri));
    }

    [[nodiscard]] auto
    parse_text_edit(LspClient& client, LspJsonValue const* value, StrRef path, LspTextEdit& out)
        -> bool {
        LspRange range = {};
        StrRef new_text = {};
        if (!parse_range(lsp_json_object_get(value, "range"), range) ||
            !json_member_string(value, "newText", new_text)) {
            return false;
        }
        StrRef const doc_text = current_doc_text(client, path);
        out = {
            .path = copy_result(client, path),
            .range = !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range,
            .new_text = copy_result(client, new_text),
        };
        return true;
    }

    auto append_text_edits(
        LspClient& client, Vec<LspTextEdit>& edits, LspJsonValue const* value, StrRef path
    ) -> void {
        if (value == nullptr || value->kind != LspJsonKind::ARRAY) {
            return;
        }
        for (LspJsonValue const* item : value->array) {
            LspTextEdit edit = {};
            if (parse_text_edit(client, item, path, edit)) {
                BASE_UNUSED(edits.push_back(edit));
            }
        }
    }

    auto append_workspace_edit(
        LspClient& client, Vec<LspTextEdit>& edits, LspJsonValue const* workspace_edit
    ) -> void {
        if (workspace_edit == nullptr || workspace_edit->kind != LspJsonKind::OBJECT) {
            return;
        }

        LspJsonValue const* const changes = lsp_json_object_get(workspace_edit, "changes");
        if (changes != nullptr && changes->kind == LspJsonKind::OBJECT) {
            for (LspJsonMember const& member : changes->object) {
                ArenaTemp temp = begin_thread_temp_arena();
                StrRef const path = lsp_file_uri_to_path(*temp.arena(), member.key);
                append_text_edits(client, edits, member.value, path);
            }
        }

        LspJsonValue const* const document_changes =
            lsp_json_object_get(workspace_edit, "documentChanges");
        if (document_changes == nullptr || document_changes->kind != LspJsonKind::ARRAY) {
            return;
        }
        for (LspJsonValue const* change : document_changes->array) {
            LspJsonValue const* const text_document = lsp_json_object_get(change, "textDocument");
            StrRef path = {};
            if (text_document != nullptr) {
                path = parse_uri_path(client, lsp_json_object_get(text_document, "uri"));
            }
            append_text_edits(client, edits, lsp_json_object_get(change, "edits"), path);
        }
    }

    [[nodiscard]] auto copy_edits_to_slice(LspClient& client, Vec<LspTextEdit> const& edits)
        -> Slice<LspTextEdit> {
        if (edits.empty()) {
            return {};
        }
        LspTextEdit* copy = arena_alloc<LspTextEdit>(client.result_arena, edits.size());
        std::memcpy(copy, edits.data(), edits.size() * sizeof(LspTextEdit));
        return Slice<LspTextEdit>(copy, edits.size());
    }

    auto parse_diagnostics(LspClient& client, LspJsonValue const* params) -> void {
        StrRef const path = parse_uri_path(client, lsp_json_object_get(params, "uri"));
        for (size_t index = 0u; index < client.diagnostics.size();) {
            if (client.diagnostics[index].path == path) {
                client.diagnostics.ordered_remove(index);
            } else {
                index += 1u;
            }
        }

        LspJsonValue const* const diagnostics = lsp_json_object_get(params, "diagnostics");
        if (diagnostics == nullptr || diagnostics->kind != LspJsonKind::ARRAY) {
            client.bridge.diagnostics_generation += 1u;
            bridge_refresh(client);
            return;
        }

        StrRef const doc_text = current_doc_text(client, path);
        for (LspJsonValue const* item : diagnostics->array) {
            LspRange range = {};
            StrRef code = {};
            StrRef source = {};
            StrRef message = {};
            size_t severity = 3u;
            if (!parse_range(lsp_json_object_get(item, "range"), range) ||
                !json_member_string(item, "message", message)) {
                continue;
            }
            BASE_UNUSED(json_member_string(item, "code", code));
            BASE_UNUSED(json_member_string(item, "source", source));
            BASE_UNUSED(json_member_size(item, "severity", severity));
            BASE_UNUSED(client.diagnostics.push_back({
                .path = copy_result(client, path),
                .range = !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range,
                .severity = static_cast<LspDiagnosticSeverity>(severity),
                .code = copy_result(client, code),
                .source = copy_result(client, source),
                .message = copy_result(client, message),
            }));
        }
        client.bridge.diagnostics_generation += 1u;
        bridge_refresh(client);
    }

    auto parse_completion_response(LspClient& client, LspJsonValue const* result, StrRef path)
        -> void {
        client.completions.clear();
        LspJsonValue const* items = result;
        if (result != nullptr && result->kind == LspJsonKind::OBJECT) {
            items = lsp_json_object_get(result, "items");
        }
        if (items == nullptr || items->kind != LspJsonKind::ARRAY) {
            client.bridge.completions_generation += 1u;
            bridge_refresh(client);
            return;
        }

        StrRef const doc_text = current_doc_text(client, path);
        size_t const limit = std::min<size_t>(items->array.size(), 64u);
        for (size_t index = 0u; index < limit; ++index) {
            LspJsonValue const* const item = items->array[index];
            StrRef label = {};
            StrRef detail = {};
            StrRef insert_text = {};
            if (!json_member_string(item, "label", label)) {
                continue;
            }
            BASE_UNUSED(json_member_string(item, "detail", detail));
            if (!json_member_string(item, "insertText", insert_text)) {
                insert_text = label;
            }

            LspCompletionItem completion = {
                .label = copy_result(client, label),
                .detail = copy_result(client, detail),
                .insert_text = copy_result(client, insert_text),
            };
            LspJsonValue const* const text_edit = lsp_json_object_get(item, "textEdit");
            if (text_edit != nullptr) {
                LspRange range = {};
                StrRef new_text = {};
                if (parse_range(lsp_json_object_get(text_edit, "range"), range)) {
                    completion.edit_range =
                        !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range;
                    completion.has_edit = true;
                }
                if (json_member_string(text_edit, "newText", new_text)) {
                    completion.insert_text = copy_result(client, new_text);
                }
            }
            BASE_UNUSED(client.completions.push_back(completion));
        }
        client.bridge.completions_generation += 1u;
        bridge_refresh(client);
    }

    auto append_marked_string(StringBuffer& buffer, LspJsonValue const* value) -> void {
        if (value == nullptr) {
            return;
        }
        if (value->kind == LspJsonKind::STRING) {
            BASE_UNUSED(buffer.write_string(value->string));
            return;
        }
        if (value->kind == LspJsonKind::OBJECT) {
            StrRef text = {};
            if (json_member_string(value, "value", text)) {
                BASE_UNUSED(buffer.write_string(text));
            }
        }
    }

    auto parse_hover_response(LspClient& client, LspJsonValue const* result, StrRef path) -> void {
        client.bridge.hover = {};
        if (result == nullptr || result->kind == LspJsonKind::NULL_VALUE) {
            client.bridge.hover_generation += 1u;
            return;
        }
        LspJsonValue const* const contents = lsp_json_object_get(result, "contents");
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer text = {};
        BASE_UNUSED(text.init(1024u, temp.arena()->resource()));
        if (contents != nullptr && contents->kind == LspJsonKind::ARRAY) {
            for (size_t index = 0u; index < contents->array.size(); ++index) {
                if (index != 0u) {
                    BASE_UNUSED(text.write_string("\n"));
                }
                append_marked_string(text, contents->array[index]);
            }
        } else {
            append_marked_string(text, contents);
        }
        LspRange range = {};
        BASE_UNUSED(parse_range(lsp_json_object_get(result, "range"), range));
        StrRef const doc_text = current_doc_text(client, path);
        client.bridge.hover = {
            .path = copy_result(client, path),
            .range = !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range,
            .text = copy_result(client, text.str()),
        };
        client.bridge.hover_generation += 1u;
    }

    auto append_location(
        LspClient& client, LspClientPendingRequest const& pending, LspJsonValue const* item
    ) -> void {
        StrRef path = parse_uri_path(client, lsp_json_object_get(item, "uri"));
        LspRange range = {};
        if (!parse_range(lsp_json_object_get(item, "range"), range)) {
            return;
        }
        StrRef const doc_text = current_doc_text(client, path);
        range = !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range;
        if (pending.kind == LspRequestKind::REFERENCES &&
            path.equals_ignore_ascii_case(pending.path) &&
            !lsp_position_less(pending.position, range.start) &&
            !lsp_position_less(range.end, pending.position)) {
            return;
        }
        BASE_UNUSED(client.locations.push_back({
            .path = copy_result(client, path),
            .range = range,
        }));
    }

    auto parse_locations_response(
        LspClient& client, LspJsonValue const* result, LspClientPendingRequest const& pending
    ) -> void {
        client.locations.clear();
        if (result != nullptr && result->kind == LspJsonKind::ARRAY) {
            for (LspJsonValue const* item : result->array) {
                append_location(client, pending, item);
            }
        } else if (result != nullptr && result->kind == LspJsonKind::OBJECT) {
            append_location(client, pending, result);
        }
        client.bridge.locations_kind = pending.kind;
        client.bridge.locations_generation += 1u;
        bridge_refresh(client);
    }

    auto parse_text_edits_response(
        LspClient& client, LspJsonValue const* result, LspRequestKind kind, StrRef path
    ) -> void {
        client.text_edits.clear();
        if (kind == LspRequestKind::FORMATTING && result != nullptr &&
            result->kind == LspJsonKind::ARRAY) {
            append_text_edits(client, client.text_edits, result, path);
        } else {
            append_workspace_edit(client, client.text_edits, result);
        }
        client.bridge.text_edits_generation += 1u;
        bridge_refresh(client);
    }

    auto parse_code_actions_response(LspClient& client, LspJsonValue const* result) -> void {
        client.code_actions.clear();
        if (result == nullptr || result->kind != LspJsonKind::ARRAY) {
            client.bridge.code_actions_generation += 1u;
            bridge_refresh(client);
            return;
        }
        size_t const limit = std::min<size_t>(result->array.size(), 32u);
        for (size_t index = 0u; index < limit; ++index) {
            LspJsonValue const* const item = result->array[index];
            StrRef title = {};
            StrRef kind = {};
            if (!json_member_string(item, "title", title)) {
                continue;
            }
            BASE_UNUSED(json_member_string(item, "kind", kind));
            Vec<LspTextEdit> edits = {};
            BASE_UNUSED(edits.init(8u, client.result_arena.resource()));
            append_workspace_edit(client, edits, lsp_json_object_get(item, "edit"));
            LspJsonValue const* const arguments = lsp_json_object_get(item, "arguments");
            if (arguments != nullptr && arguments->kind == LspJsonKind::ARRAY &&
                !arguments->array.empty()) {
                append_workspace_edit(client, edits, arguments->array[0u]);
            }
            BASE_UNUSED(client.code_actions.push_back({
                .title = copy_result(client, title),
                .kind = copy_result(client, kind),
                .edits = copy_edits_to_slice(client, edits),
            }));
        }
        client.bridge.code_actions_generation += 1u;
        bridge_refresh(client);
    }

    auto append_document_symbol(LspClient& client, LspJsonValue const* item) -> void {
        StrRef name = {};
        StrRef detail = {};
        size_t kind = 0u;
        LspRange range = {};
        LspRange selection_range = {};
        if (!json_member_string(item, "name", name)) {
            return;
        }
        BASE_UNUSED(json_member_string(item, "detail", detail));
        BASE_UNUSED(json_member_size(item, "kind", kind));
        if (!parse_range(lsp_json_object_get(item, "range"), range)) {
            BASE_UNUSED(parse_range(
                lsp_json_object_get(lsp_json_object_get(item, "location"), "range"), range
            ));
        }
        if (!parse_range(lsp_json_object_get(item, "selectionRange"), selection_range)) {
            selection_range = range;
        }
        StrRef const doc_text = current_doc_text(client, client.current_path);
        BASE_UNUSED(client.symbols.push_back({
            .name = copy_result(client, name),
            .detail = copy_result(client, detail),
            .range = !doc_text.empty() ? lsp_range_utf16_to_byte(doc_text, range) : range,
            .selection_range = !doc_text.empty()
                                   ? lsp_range_utf16_to_byte(doc_text, selection_range)
                                   : selection_range,
            .kind = static_cast<uint32_t>(kind),
        }));

        LspJsonValue const* const children = lsp_json_object_get(item, "children");
        if (children != nullptr && children->kind == LspJsonKind::ARRAY) {
            for (LspJsonValue const* child : children->array) {
                append_document_symbol(client, child);
            }
        }
    }

    auto parse_document_symbols_response(LspClient& client, LspJsonValue const* result) -> void {
        client.symbols.clear();
        if (result != nullptr && result->kind == LspJsonKind::ARRAY) {
            for (LspJsonValue const* item : result->array) {
                append_document_symbol(client, item);
            }
        }
        client.bridge.symbols_generation += 1u;
        bridge_refresh(client);
    }

    auto send_initialized(LspClient& client) -> void {
        send_notification_json(client, "initialized", "{}");
        set_status(
            client,
            client.compile_commands_dir.empty() ? LspStatusKind::WARNING : LspStatusKind::READY,
            client.compile_commands_dir.empty() ? "LSP ready, no compile DB" : "LSP ready"
        );
        client.initialized = true;
    }

    auto handle_response(LspClient& client, LspJsonValue const* root) -> void {
        int32_t id = 0;
        if (!lsp_json_int(lsp_json_object_get(root, "id"), id)) {
            return;
        }
        LspClientPendingRequest const pending = take_pending(client, id);
        LspJsonValue const* const result = lsp_json_object_get(root, "result");
        if (pending.kind == LspRequestKind::NONE) {
            send_initialized(client);
            return;
        }

        switch (pending.kind) {
        case LspRequestKind::COMPLETION:
            parse_completion_response(client, result, pending.path);
            break;
        case LspRequestKind::HOVER:
            parse_hover_response(client, result, pending.path);
            break;
        case LspRequestKind::DEFINITION:
        case LspRequestKind::DECLARATION:
        case LspRequestKind::REFERENCES:
            parse_locations_response(client, result, pending);
            break;
        case LspRequestKind::RENAME:
        case LspRequestKind::FORMATTING:
            parse_text_edits_response(client, result, pending.kind, pending.path);
            break;
        case LspRequestKind::CODE_ACTION:
            parse_code_actions_response(client, result);
            break;
        case LspRequestKind::DOCUMENT_SYMBOL:
            parse_document_symbols_response(client, result);
            break;
        default:
            break;
        }
    }

    auto handle_notification(LspClient& client, LspJsonValue const* root) -> void {
        StrRef method = {};
        if (!json_member_string(root, "method", method)) {
            return;
        }
        if (method == "textDocument/publishDiagnostics") {
            parse_diagnostics(client, lsp_json_object_get(root, "params"));
        } else if (method == "$/progress") {
            parse_work_done_progress(client, lsp_json_object_get(root, "params"));
        } else if (method == "window/progress") {
            parse_window_progress(client, lsp_json_object_get(root, "params"));
        }
    }

    auto handle_message(LspClient& client, StrRef message) -> void {
        LspJsonValue const* root = nullptr;
        if (!lsp_json_parse(client.message_arena, message, root)) {
            return;
        }
        if (lsp_json_object_get(root, "id") != nullptr &&
            lsp_json_object_get(root, "method") != nullptr) {
            int32_t id = 0;
            if (lsp_json_int(lsp_json_object_get(root, "id"), id)) {
                write_json(
                    client, fmt::tprintf("{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":null}", id)
                );
            }
            return;
        }
        if (lsp_json_object_get(root, "id") != nullptr) {
            handle_response(client, root);
        } else {
            handle_notification(client, root);
        }
    }

    [[nodiscard]] auto begin_read(HANDLE handle, LspPipeRead& io, DWORD& out_bytes) -> bool {
        if (io.pending || !handle_valid(handle) || io.event == nullptr) {
            return false;
        }

        reset_overlapped(io.overlapped, io.event);
        out_bytes = 0u;
        if (ReadFile(
                handle, io.buffer, static_cast<DWORD>(sizeof(io.buffer)), &out_bytes, &io.overlapped
            )) {
            return true;
        }

        DWORD const error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            io.pending = true;
        }
        return false;
    }

    [[nodiscard]] auto finish_read(HANDLE handle, LspPipeRead& io, DWORD& out_bytes) -> bool {
        if (!io.pending || WaitForSingleObject(io.event, 0u) != WAIT_OBJECT_0) {
            return false;
        }

        out_bytes = 0u;
        io.pending = false;
        return GetOverlappedResult(handle, &io.overlapped, &out_bytes, FALSE);
    }

    auto handle_framed_messages(LspClient& client) -> void {
        for (;;) {
            StrRef message = {};
            client.message_arena.reset();
            if (!lsp_framer_next_message(client.framer, client.message_arena, message)) {
                break;
            }
            handle_message(client, message);
        }
    }

    auto read_pipe_messages(LspClient& client) -> void {
        if (!handle_valid(client.stdout_read)) {
            return;
        }
        for (;;) {
            DWORD bytes_read = 0u;
            bool const read = client.stdout_io.pending
                                  ? finish_read(client.stdout_read, client.stdout_io, bytes_read)
                                  : begin_read(client.stdout_read, client.stdout_io, bytes_read);
            if (!read) {
                break;
            }
            if (bytes_read == 0u) {
                break;
            }
            BASE_UNUSED(
                lsp_framer_append(client.framer, StrRef(client.stdout_io.buffer, bytes_read))
            );
        }
        handle_framed_messages(client);
    }

    auto read_stderr(LspClient& client) -> void {
        if (!handle_valid(client.stderr_read)) {
            return;
        }
        for (;;) {
            DWORD bytes_read = 0u;
            bool const read = client.stderr_io.pending
                                  ? finish_read(client.stderr_read, client.stderr_io, bytes_read)
                                  : begin_read(client.stderr_read, client.stderr_io, bytes_read);
            if (!read || bytes_read == 0u) {
                break;
            }
        }
    }

    auto lsp_client_poll(LspClient& client) -> void {
        if (!client.started) {
            return;
        }
        DWORD const wait = WaitForSingleObject(client.process.hProcess, 0u);
        if (wait != WAIT_TIMEOUT) {
            set_status(client, LspStatusKind::FAILED, "LSP: clangd exited");
            client.started = false;
            return;
        }
        finish_write(client);
        pump_write(client);
        read_pipe_messages(client);
        read_stderr(client);
    }

    auto send_document_open(LspClient& client, LspEditorRequest const& request) -> void {
        client.document_arena.reset();
        client.current_path = arena_copy_cstr(client.document_arena, request.path);
        client.current_uri = lsp_path_to_file_uri(client.document_arena, request.path);
        client.current_text = arena_copy_str(client.document_arena, request.text);
        client.current_revision = request.revision;

        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(
            params.init(request.text.size() + request.path.size() + 256u, temp.arena()->resource())
        );
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, client.current_uri);
        BASE_UNUSED(params.write_string(",\"languageId\":\"cpp\",\"version\":"));
        BASE_UNUSED(params.write_string(
            fmt::tprintf("%llu", static_cast<unsigned long long>(request.revision))
        ));
        BASE_UNUSED(params.write_string(",\"text\":"));
        lsp_json_write_escaped_string(params, request.text);
        BASE_UNUSED(params.write_string("}}"));
        send_notification_json(client, "textDocument/didOpen", params.str());
    }

    auto send_document_change(LspClient& client, LspEditorRequest const& request) -> void {
        if (request.path != client.current_path) {
            send_document_open(client, request);
            return;
        }
        client.document_arena.reset();
        client.current_path = arena_copy_cstr(client.document_arena, request.path);
        client.current_uri = lsp_path_to_file_uri(client.document_arena, request.path);
        client.current_text = arena_copy_str(client.document_arena, request.text);
        client.current_revision = request.revision;

        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(
            params.init(request.text.size() + request.path.size() + 256u, temp.arena()->resource())
        );
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, client.current_uri);
        BASE_UNUSED(params.write_string(",\"version\":"));
        BASE_UNUSED(params.write_string(
            fmt::tprintf("%llu", static_cast<unsigned long long>(request.revision))
        ));
        BASE_UNUSED(params.write_string("},\"contentChanges\":[{\"text\":"));
        lsp_json_write_escaped_string(params, request.text);
        BASE_UNUSED(params.write_string("}]}"));
        send_notification_json(client, "textDocument/didChange", params.str());
    }

    auto send_document_close(LspClient& client, LspEditorRequest const& request) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const uri = lsp_path_to_file_uri(*temp.arena(), request.path);
        StringBuffer params = {};
        BASE_UNUSED(params.init(uri.size() + 96u, temp.arena()->resource()));
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, uri);
        BASE_UNUSED(params.write_string("}}"));
        send_notification_json(client, "textDocument/didClose", params.str());
    }

    auto send_position_request(
        LspClient& client, LspEditorRequest const& request, LspRequestKind kind, StrRef method
    ) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(params.init(512u, temp.arena()->resource()));
        BASE_UNUSED(params.write_byte('{'));
        write_text_document_position_params(params, client, request);
        if (kind == LspRequestKind::REFERENCES) {
            BASE_UNUSED(params.write_string(",\"context\":{\"includeDeclaration\":false}"));
        }
        BASE_UNUSED(params.write_byte('}'));
        send_request_json(client, kind, request.path, method, params.str(), request.position);
    }

    auto send_rename_request(LspClient& client, LspEditorRequest const& request) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(params.init(768u, temp.arena()->resource()));
        BASE_UNUSED(params.write_byte('{'));
        write_text_document_position_params(params, client, request);
        BASE_UNUSED(params.write_string(",\"newName\":"));
        lsp_json_write_escaped_string(params, request.new_name);
        BASE_UNUSED(params.write_byte('}'));
        send_request_json(
            client, LspRequestKind::RENAME, request.path, "textDocument/rename", params.str()
        );
    }

    auto send_formatting_request(LspClient& client, LspEditorRequest const& request) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(params.init(512u, temp.arena()->resource()));
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, client.current_uri);
        BASE_UNUSED(params.write_string("},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}"));
        send_request_json(
            client,
            LspRequestKind::FORMATTING,
            request.path,
            "textDocument/formatting",
            params.str()
        );
    }

    auto send_code_action_request(LspClient& client, LspEditorRequest const& request) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(params.init(1024u, temp.arena()->resource()));
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, client.current_uri);
        BASE_UNUSED(params.write_string("},\"range\":"));
        write_range(params, client.current_text, request.range);
        BASE_UNUSED(params.write_string(",\"context\":{\"diagnostics\":["));
        bool first = true;
        for (LspDiagnostic const& diagnostic : client.diagnostics) {
            if (diagnostic.path != request.path ||
                !lsp_range_intersects(diagnostic.range, request.range)) {
                continue;
            }
            if (!first) {
                BASE_UNUSED(params.write_byte(','));
            }
            write_diagnostic(params, client.current_text, diagnostic);
            first = false;
        }
        BASE_UNUSED(params.write_string("]}}"));
        send_request_json(
            client,
            LspRequestKind::CODE_ACTION,
            request.path,
            "textDocument/codeAction",
            params.str()
        );
    }

    auto send_document_symbol_request(LspClient& client, LspEditorRequest const& request) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StringBuffer params = {};
        BASE_UNUSED(params.init(256u, temp.arena()->resource()));
        BASE_UNUSED(params.write_string("{\"textDocument\":{\"uri\":"));
        lsp_json_write_escaped_string(params, client.current_uri);
        BASE_UNUSED(params.write_string("}}"));
        send_request_json(
            client,
            LspRequestKind::DOCUMENT_SYMBOL,
            request.path,
            "textDocument/documentSymbol",
            params.str()
        );
    }

    auto lsp_client_send_editor_request(void* user_data, LspEditorRequest const& request) -> void {
        auto* const client = static_cast<LspClient*>(user_data);
        if (client == nullptr || !client->started) {
            return;
        }
        if (!client->initialized && request.kind != LspRequestKind::DID_CLOSE) {
            return;
        }
        switch (request.kind) {
        case LspRequestKind::DID_OPEN:
            send_document_open(*client, request);
            break;
        case LspRequestKind::DID_CHANGE:
            send_document_change(*client, request);
            break;
        case LspRequestKind::DID_CLOSE:
            send_document_close(*client, request);
            break;
        case LspRequestKind::COMPLETION:
            send_position_request(*client, request, request.kind, "textDocument/completion");
            break;
        case LspRequestKind::HOVER:
            send_position_request(*client, request, request.kind, "textDocument/hover");
            break;
        case LspRequestKind::DEFINITION:
            send_position_request(*client, request, request.kind, "textDocument/definition");
            break;
        case LspRequestKind::DECLARATION:
            send_position_request(*client, request, request.kind, "textDocument/declaration");
            break;
        case LspRequestKind::REFERENCES:
            send_position_request(*client, request, request.kind, "textDocument/references");
            break;
        case LspRequestKind::RENAME:
            send_rename_request(*client, request);
            break;
        case LspRequestKind::FORMATTING:
            send_formatting_request(*client, request);
            break;
        case LspRequestKind::CODE_ACTION:
            send_code_action_request(*client, request);
            break;
        case LspRequestKind::DOCUMENT_SYMBOL:
            send_document_symbol_request(*client, request);
            break;
        default:
            break;
        }
    }

    [[nodiscard]] auto lsp_client_bridge(LspClient& client) -> LspBridge* {
        bridge_refresh(client);
        return &client.bridge;
    }

} // namespace code_editor

#endif
