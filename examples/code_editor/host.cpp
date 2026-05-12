#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.h"

#include <algorithm>
#include <atomic>
#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/hash_map.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/spsc_queue.h>
#include <base/vec.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <code_editor_hot_reload_manifest.h>
#include <dwmapi.h>
#include <gui/gui.h>
#include <gui/hot_reload_app.h>
#include <gui/hot_reload_overlay.h>
#include <render/render.h>
#include <shellapi.h>
#include <windows.h>
#endif

#if defined(_WIN32)
#include "lsp_client_win32.h"
#endif

#ifndef CODE_EDITOR_SOURCE_DIR
#define CODE_EDITOR_SOURCE_DIR "."
#endif

#ifndef CODE_EDITOR_BINARY_DIR
#define CODE_EDITOR_BINARY_DIR "."
#endif

#ifndef CODE_EDITOR_BUILD_CONFIG
#define CODE_EDITOR_BUILD_CONFIG "Debug"
#endif

namespace code_editor {

#if defined(_WIN32)
    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_code_editor";
    constexpr int CODE_EDITOR_ICON_ID = 101;
    constexpr DWORD DWM_ATTR_USE_IMMERSIVE_DARK_MODE = 20u;
    constexpr DWORD DWM_ATTR_BORDER_COLOR = 34u;
    constexpr DWORD DWM_ATTR_CAPTION_COLOR = 35u;
    constexpr DWORD DWM_ATTR_TEXT_COLOR = 36u;
    constexpr COLORREF WINDOW_HEADER_BACKGROUND = RGB(12, 15, 18);
    constexpr COLORREF WINDOW_HEADER_TEXT = RGB(230, 235, 239);
    constexpr size_t GIT_PATH_LINE_CAPACITY = MAX_PATH * 4u;
    constexpr size_t GIT_WORK_QUEUE_CAPACITY = 8u;
    constexpr size_t TREE_FETCH_QUEUE_CAPACITY = 2u;
    constexpr uint64_t TREE_OPERATION_WATCHER_IGNORE_MS = 1000u;

    struct TreeFetchRequest {
        uint64_t generation = 0u;
        size_t git_log_limit = GIT_LOG_MIN_LIMIT;
        bool load_git_log = false;
        StrRef root = {};
        Arena* arena = nullptr;
        uint32_t arena_index = 0u;
    };

    struct TreeFetchResult {
        uint64_t generation = 0u;
        Vec<FileTreeEntry> files = {};
        uint32_t arena_index = 0u;
        bool ok = false;
    };

    struct BackgroundWorker {
        SpscQueue<GitWorkRequest> git_requests = {};
        SpscQueue<GitWorkResult> git_results = {};
        SpscQueue<TreeFetchRequest> requests = {};
        SpscQueue<TreeFetchResult> results = {};
        Arena git_result_arena = {};
        HANDLE thread = nullptr;
        std::atomic<bool> stop_requested = false;
    };

    struct AppState {
        HWND hwnd = nullptr;
        StrRef window_cache_path = {};
        bool running = true;
        bool redraw_pending = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::Frame last_frame = {};
        gui::Id mouse_hit_id = {};
        gui::InputState input = {};
        FileDropRequest file_drop_request = {};
        gui::KeyEvent key_events[MAX_KEY_EVENTS_PER_FRAME] = {};
        gui::Vec2 last_click_pos = {};
        uint64_t last_click_ticks = 0u;
        uint32_t click_count = 0u;
        bool close_requested = false;
        bool close_confirmed = false;
    };

    struct RunOptions {
        StrRef initial_path = {};
        StrRef automation_dump_frame = {};
    };

    struct DirectoryWatcher {
        HANDLE directory = INVALID_HANDLE_VALUE;
        HANDLE event = nullptr;
        OVERLAPPED overlapped = {};
        uint8_t buffer[64u * 1024u] = {};
        bool pending = false;
    };

    struct LaunchDesc {
        struct TreeSnapshotEntry {
            StrRef relative_path = {};
            StrRef text = {};
            bool is_directory = false;
        };

        struct TreeHistoryEntry {
            TreeHistoryEntry* previous = nullptr;
            TreeOperationKind kind = TreeOperationKind::NONE;
            StrRef source_path = {};
            StrRef target_path = {};
            Slice<TreeSnapshotEntry> snapshot = {};
        };

        StrRef initial_text = {};
        StrRef initial_file_name = {};
        StrRef initial_file_path = {};
        StrRef tree_root_name = {};
        StrRef save_root_path = {};
        StrRef window_cache_path = {};
        Arena tree_arenas[2] = {};
        Arena tree_history_arena = {};
        Vec<FileTreeEntry> tree_files = {};
        BackgroundWorker worker = {};
        DirectoryWatcher tree_watcher = {};
        TreeHistoryEntry* tree_undo_stack = nullptr;
        TreeHistoryEntry* tree_redo_stack = nullptr;
        TreeOperationRequest tree_operation_request = {};
        TreeOperationResult tree_operation_result = {};
        uint32_t tree_arena_index = 0u;
        uint64_t file_change_generation = 0u;
        uint64_t file_change_ticks = 0u;
        uint64_t ignore_tree_change_ticks = 0u;
        uint64_t tree_fetch_generation = 0u;
        size_t git_log_limit = GIT_LOG_MIN_LIMIT;
        bool tree_change_pending = false;
        bool tree_fetch_pending = false;
        bool tree_fetch_refresh_requested = false;
        bool tree_loading = false;
        bool git_log_prefetched = false;
        bool initial_tree_refresh = false;
        bool initial_sidebar_visible = false;
    };

    AppState* global_app_state = nullptr;

    auto enable_process_dpi_awareness() -> void {
        if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE) {
            BASE_UNUSED(SetProcessDPIAware());
        }
    }

    auto request_redraw(AppState* state) -> void {
        if (state != nullptr) {
            state->redraw_pending = true;
        }
    }

    [[nodiscard]] auto lsp_generation_sum(LspBridge const* bridge) -> uint64_t {
        if (bridge == nullptr) {
            return 0u;
        }
        return bridge->status_generation + bridge->progress_generation +
               bridge->diagnostics_generation + bridge->completions_generation +
               bridge->hover_generation + bridge->locations_generation +
               bridge->code_actions_generation + bridge->symbols_generation +
               bridge->text_edits_generation + bridge->semantic_tokens_generation +
               bridge->folding_ranges_generation;
    }

    [[nodiscard]] auto frame_ready(gui::Frame const& frame) -> bool {
        return frame.box_info_count() != 0u;
    }

    [[nodiscard]] auto frame_hit_id(gui::Frame const& frame, gui::Vec2 pos) -> gui::Id {
        gui::BoxInfo const* const box = frame.hit_test(pos);
        return box != nullptr ? box->id : gui::Id{};
    }

    [[nodiscard]] auto file_drop_hits_editor_surface(AppState const* state, gui::Vec2 pos) -> bool {
        if (state == nullptr || !frame_ready(state->last_frame)) {
            return false;
        }
        gui::BoxInfo const* const box = state->last_frame.hit_test(pos);
        return box != nullptr && box->debug_name == "editor_surface";
    }

    [[nodiscard]] auto queue_file_drop(AppState* state, HDROP drop) -> bool {
        if (state == nullptr || drop == nullptr) {
            return false;
        }

        POINT point = {};
        BASE_UNUSED(DragQueryPoint(drop, &point));
        gui::Vec2 const pos = {static_cast<float>(point.x), static_cast<float>(point.y)};
        if (!file_drop_hits_editor_surface(state, pos) ||
            DragQueryFileW(drop, 0xffffffffu, nullptr, 0u) == 0u) {
            return false;
        }

        UINT const wide_size = DragQueryFileW(drop, 0u, nullptr, 0u);
        if (wide_size == 0u) {
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* const wide_path =
            arena_alloc<wchar_t>(*temp.arena(), static_cast<size_t>(wide_size) + 1u);
        UINT const copied = DragQueryFileW(drop, 0u, wide_path, wide_size + 1u);
        DWORD const attributes =
            copied != 0u ? GetFileAttributesW(wide_path) : INVALID_FILE_ATTRIBUTES;
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            return false;
        }

        StrRef path = {};
        if (!base::wide_to_utf8(wide_path, static_cast<int>(copied), *temp.arena(), path) ||
            path.empty() || path.size() >= FILE_DROP_PATH_CAPACITY) {
            return false;
        }

        std::memcpy(state->file_drop_request.path, path.data(), path.size());
        state->file_drop_request.path[path.size()] = '\0';
        state->file_drop_request.pos = pos;
        state->file_drop_request.generation += 1u;
        return true;
    }

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto lparam_x(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>(value & 0xffff));
    }

    [[nodiscard]] auto lparam_y(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto current_key_mods() -> gui::KeyMods {
        gui::KeyMods mods = gui::KEY_MOD_NONE;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SHIFT;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_CTRL;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_ALT;
        }
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SUPER;
        }
        return mods;
    }

    [[nodiscard]] auto watcher_valid(DirectoryWatcher const& watcher) -> bool {
        return watcher.directory != INVALID_HANDLE_VALUE && watcher.event != nullptr;
    }

    auto close_directory_watcher(DirectoryWatcher& watcher) -> void {
        if (watcher.directory != INVALID_HANDLE_VALUE) {
            if (watcher.pending) {
                BASE_UNUSED(CancelIoEx(watcher.directory, &watcher.overlapped));
                DWORD bytes = 0u;
                BASE_UNUSED(
                    GetOverlappedResult(watcher.directory, &watcher.overlapped, &bytes, TRUE)
                );
            }
            CloseHandle(watcher.directory);
        }
        if (watcher.event != nullptr) {
            CloseHandle(watcher.event);
        }
        watcher = {};
        watcher.directory = INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] auto start_directory_watch(DirectoryWatcher& watcher) -> bool {
        if (!watcher_valid(watcher)) {
            return false;
        }

        ResetEvent(watcher.event);
        watcher.overlapped = {};
        watcher.overlapped.hEvent = watcher.event;
        DWORD const filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                             FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                             FILE_NOTIFY_CHANGE_CREATION;
        BOOL const ok = ReadDirectoryChangesW(
            watcher.directory,
            watcher.buffer,
            static_cast<DWORD>(sizeof(watcher.buffer)),
            TRUE,
            filter,
            nullptr,
            &watcher.overlapped,
            nullptr
        );
        watcher.pending = ok || GetLastError() == ERROR_IO_PENDING;
        return watcher.pending;
    }

    [[nodiscard]] auto open_directory_watcher(StrRef path, DirectoryWatcher& watcher) -> bool {
        close_directory_watcher(watcher);
        if (path.empty()) {
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* wide_path = nullptr;
        int wide_count = 0;
        if (!base::utf8_to_wide(path, *temp.arena(), wide_path, wide_count)) {
            return false;
        }
        BASE_UNUSED(wide_count);

        watcher.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (watcher.event == nullptr) {
            close_directory_watcher(watcher);
            return false;
        }

        watcher.directory = CreateFileW(
            wide_path,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr
        );
        if (watcher.directory == INVALID_HANDLE_VALUE) {
            close_directory_watcher(watcher);
            return false;
        }

        if (!start_directory_watch(watcher)) {
            close_directory_watcher(watcher);
            return false;
        }
        return true;
    }

    [[nodiscard]] auto wide_ascii_lower(wchar_t value) -> wchar_t {
        return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
    }

    [[nodiscard]] auto wide_component_is_git(wchar_t const* text, size_t size) -> bool {
        return size == 4u && text[0u] == L'.' && wide_ascii_lower(text[1u]) == L'g' &&
               wide_ascii_lower(text[2u]) == L'i' && wide_ascii_lower(text[3u]) == L't';
    }

    [[nodiscard]] auto directory_change_is_git_metadata(FILE_NOTIFY_INFORMATION const& info)
        -> bool {
        wchar_t const* const text = info.FileName;
        size_t const size = static_cast<size_t>(info.FileNameLength) / sizeof(wchar_t);
        size_t begin = 0u;
        for (size_t index = 0u; index <= size; ++index) {
            bool const separator = index == size || text[index] == L'\\' || text[index] == L'/';
            if (!separator) {
                continue;
            }
            if (wide_component_is_git(text + begin, index - begin)) {
                return true;
            }
            begin = index + 1u;
        }
        return false;
    }

    [[nodiscard]] auto directory_change_affects_tree(uint8_t const* buffer, DWORD bytes) -> bool {
        if (bytes == 0u) {
            return true;
        }

        DWORD offset = 0u;
        while (offset < bytes) {
            auto const* info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer + offset);
            switch (info->Action) {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (!directory_change_is_git_metadata(*info)) {
                    return true;
                }
                break;
            default:
                break;
            }
            if (info->NextEntryOffset == 0u) {
                break;
            }
            offset += info->NextEntryOffset;
        }
        return false;
    }

    [[nodiscard]] auto consume_directory_change(DirectoryWatcher& watcher, bool& tree_changed)
        -> bool {
        tree_changed = false;
        if (!watcher_valid(watcher) || !watcher.pending ||
            WaitForSingleObject(watcher.event, 0u) != WAIT_OBJECT_0) {
            return false;
        }

        DWORD bytes = 0u;
        BOOL const ok = GetOverlappedResult(watcher.directory, &watcher.overlapped, &bytes, FALSE);
        watcher.pending = false;
        DWORD const error = ok ? ERROR_SUCCESS : GetLastError();
        tree_changed = ok ? directory_change_affects_tree(watcher.buffer, bytes)
                          : error != ERROR_OPERATION_ABORTED;
        BASE_UNUSED(start_directory_watch(watcher));
        return ok || error != ERROR_OPERATION_ABORTED;
    }

    [[nodiscard]] auto close_click(AppState const& state, gui::Vec2 pos, uint64_t ticks) -> bool {
        float const dx = pos.x > state.last_click_pos.x ? pos.x - state.last_click_pos.x
                                                        : state.last_click_pos.x - pos.x;
        float const dy = pos.y > state.last_click_pos.y ? pos.y - state.last_click_pos.y
                                                        : state.last_click_pos.y - pos.y;
        return ticks - state.last_click_ticks <= static_cast<uint64_t>(GetDoubleClickTime()) &&
               dx <= static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)) &&
               dy <= static_cast<float>(GetSystemMetrics(SM_CYDOUBLECLK));
    }

    auto push_mouse_down(AppState* state, gui::Vec2 pos, bool double_click) -> void {
        if (state == nullptr) {
            return;
        }

        uint64_t const ticks = GetTickCount64();
        state->input.mouse_down[0u] = true;
        state->input.mouse_pos = pos;
        if (double_click) {
            state->click_count = 2u;
            state->input.mouse_double_clicked[0u] = true;
        } else if (state->click_count == 2u && close_click(*state, pos, ticks)) {
            state->click_count = 3u;
            state->input.mouse_double_clicked[0u] = false;
            state->input.mouse_triple_clicked[0u] = true;
        } else {
            state->click_count = 1u;
        }
        state->last_click_pos = pos;
        state->last_click_ticks = ticks;
        request_redraw(state);
    }

    auto push_middle_mouse_down(AppState* state, gui::Vec2 pos) -> void {
        if (state == nullptr) {
            return;
        }
        state->input.mouse_down[1u] = true;
        state->input.mouse_pos = pos;
        request_redraw(state);
    }

    [[nodiscard]] auto hot_reload_desc(ModuleRuntimeContext* context) -> gui::HotReloadDesc {
        return {
            .label = "code_editor",
            .source_dir = CODE_EDITOR_SOURCE_DIR,
            .binary_dir = CODE_EDITOR_BINARY_DIR,
            .build_config = CODE_EDITOR_BUILD_CONFIG,
            .build_target = "code_editor_module",
            .api_export_name = "code_editor_get_module_api",
            .module_file_name = MODULE_FILE_NAME,
            .module_copy_prefix = "code_editor_module",
            .watched_files = HOT_RELOAD_WATCH_FILES,
            .storage_size = MODULE_STORAGE_SIZE,
            .storage_alignment = MODULE_STORAGE_ALIGNMENT,
            .user_data = context,
        };
    }

    [[nodiscard]] auto key_from_virtual_key(WPARAM value) -> gui::Key {
        switch (value) {
        case VK_TAB:
            return gui::Key::TAB;
        case VK_RETURN:
            return gui::Key::ENTER;
        case VK_ESCAPE:
            return gui::Key::ESCAPE;
        case VK_SPACE:
            return gui::Key::SPACE;
        case VK_LEFT:
            return gui::Key::LEFT;
        case VK_RIGHT:
            return gui::Key::RIGHT;
        case VK_UP:
            return gui::Key::UP;
        case VK_DOWN:
            return gui::Key::DOWN;
        case VK_HOME:
            return gui::Key::HOME;
        case VK_END:
            return gui::Key::END;
        case VK_BACK:
            return gui::Key::BACKSPACE;
        case VK_DELETE:
            return gui::Key::DELETE_KEY;
        case VK_OEM_PLUS:
        case VK_ADD:
            return gui::Key::PLUS;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            return gui::Key::MINUS;
        case VK_OEM_2:
        case VK_DIVIDE:
            return gui::Key::SLASH;
        case 'A':
            return gui::Key::A;
        case 'C':
            return gui::Key::C;
        case 'D':
            return gui::Key::D;
        case 'N':
            return gui::Key::N;
        case 'S':
            return gui::Key::S;
        case 'U':
            return gui::Key::U;
        case 'V':
            return gui::Key::V;
        case 'W':
            return gui::Key::W;
        case 'X':
            return gui::Key::X;
        case 'Z':
            return gui::Key::Z;
        default:
            return gui::Key::UNKNOWN;
        }
    }

    [[nodiscard]] auto key_down_kind(LPARAM lparam) -> gui::KeyEventKind {
        return (lparam & (1ll << 30)) != 0 ? gui::KeyEventKind::REPEAT : gui::KeyEventKind::PRESS;
    }

    auto push_key_event(AppState* state, gui::Key key, gui::KeyEventKind kind, gui::KeyMods mods)
        -> void {
        if (state == nullptr || key == gui::Key::UNKNOWN ||
            state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {.key = key, .kind = kind, .mods = mods};
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    auto push_text_event(AppState* state, uint32_t codepoint, gui::KeyMods mods) -> void {
        if (state == nullptr || state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {
            .kind = gui::KeyEventKind::TEXT, .mods = mods, .codepoint = codepoint
        };
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    static auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    [[nodiscard]] auto open_read_file(StrRef path) -> std::FILE* {
        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, path.data(), "rb") != 0) {
            return nullptr;
        }
#else
        file = std::fopen(path.data(), "rb");
#endif
        return file;
    }

    [[nodiscard]] auto open_write_file(StrRef path) -> std::FILE* {
        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, path.data(), "wb") != 0) {
            return nullptr;
        }
#else
        file = std::fopen(path.data(), "wb");
#endif
        return file;
    }

#if BASE_DEBUG
    [[nodiscard]] auto box_kind_name(gui::BoxKind kind) -> char const* {
        switch (kind) {
        case gui::BoxKind::ROOT:
            return "root";
        case gui::BoxKind::ROW:
            return "row";
        case gui::BoxKind::COLUMN:
            return "column";
        case gui::BoxKind::OVERLAY:
            return "overlay";
        case gui::BoxKind::POPUP:
            return "popup";
        case gui::BoxKind::MODAL:
            return "modal";
        case gui::BoxKind::LABEL:
            return "label";
        case gui::BoxKind::SELECTABLE_LABEL:
            return "selectable_label";
        case gui::BoxKind::BUTTON:
            return "button";
        case gui::BoxKind::CHECKBOX:
            return "checkbox";
        case gui::BoxKind::TOGGLE:
            return "toggle";
        case gui::BoxKind::SLIDER_FLOAT:
            return "slider_float";
        case gui::BoxKind::SPACER:
            return "spacer";
        case gui::BoxKind::SCROLL_PANEL:
            return "scroll_panel";
        case gui::BoxKind::LIST:
            return "list";
        case gui::BoxKind::INPUT_TEXT:
            return "input_text";
        case gui::BoxKind::INPUT_TEXT_MULTILINE:
            return "input_text_multiline";
        case gui::BoxKind::TABLE:
            return "table";
        case gui::BoxKind::TABLE_ROW:
            return "table_row";
        case gui::BoxKind::TABLE_HEADER_ROW:
            return "table_header_row";
        case gui::BoxKind::TABLE_CELL:
            return "table_cell";
        case gui::BoxKind::TABLE_HEADER_CELL:
            return "table_header_cell";
        case gui::BoxKind::TAB_VIEW:
            return "tab_view";
        case gui::BoxKind::TAB_BAR:
            return "tab_bar";
        case gui::BoxKind::TAB:
            return "tab";
        case gui::BoxKind::TAB_BODY:
            return "tab_body";
        case gui::BoxKind::IMAGE:
            return "image";
        case gui::BoxKind::ICON:
            return "icon";
        case gui::BoxKind::TREE_NODE:
            return "tree_node";
        case gui::BoxKind::RADIO_BUTTON:
            return "radio_button";
        case gui::BoxKind::COUNT:
            break;
        }
        return "unknown";
    }

    auto write_json_text(std::FILE* file, StrRef text) -> void {
        std::fputc('"', file);
        for (char const ch : text) {
            uint8_t const byte = static_cast<uint8_t>(ch);
            switch (ch) {
            case '"':
                fmt::fprintf(file, "\\\"");
                break;
            case '\\':
                fmt::fprintf(file, "\\\\");
                break;
            case '\n':
                fmt::fprintf(file, "\\n");
                break;
            case '\r':
                fmt::fprintf(file, "\\r");
                break;
            case '\t':
                fmt::fprintf(file, "\\t");
                break;
            default:
                if (byte < 0x20u) {
                    fmt::fprintf(file, "\\u%04x", static_cast<unsigned>(byte));
                } else {
                    std::fputc(ch, file);
                }
                break;
            }
        }
        std::fputc('"', file);
    }

    auto write_json_id(std::FILE* file, gui::Id id) -> void {
        fmt::fprintf(file, "\"0x%016llx\"", static_cast<unsigned long long>(id.value));
    }

    auto write_automation_frame_dump(StrRef path, gui::Frame const& frame) -> void {
        if (path.empty() || frame.box_info_count() == 0u) {
            return;
        }

        std::FILE* const file = open_write_file(path);
        if (file == nullptr) {
            return;
        }

        gui::BoxInfo const* const focused = frame.focused_box();
        fmt::fprintf(file, "{\"box_count\":%zu,\"boxes\":[\n", frame.box_info_count());
        for (size_t index = 0u; index < frame.box_info_count(); ++index) {
            gui::BoxInfo const* const box = frame.box_info(index);
            if (box == nullptr) {
                continue;
            }
            if (index != 0u) {
                fmt::fprintf(file, ",\n");
            }
            fmt::fprintf(file, "{\"index\":%zu,\"id\":", index);
            write_json_id(file, box->id);
            fmt::fprintf(file, ",\"parent_id\":");
            write_json_id(file, box->parent_id);
            fmt::fprintf(file, ",\"authored_id\":");
            write_json_id(file, box->authored_id);
            fmt::fprintf(file, ",\"kind\":\"%s\",\"text\":", box_kind_name(box->kind));
            write_json_text(file, box->text);
            fmt::fprintf(file, ",\"debug_name\":");
            write_json_text(file, box->debug_name);
            fmt::fprintf(
                file,
                ",\"rect\":{\"min_x\":%.3f,\"min_y\":%.3f,\"max_x\":%.3f,\"max_y\":%.3f}",
                static_cast<double>(box->rect.min.x),
                static_cast<double>(box->rect.min.y),
                static_cast<double>(box->rect.max.x),
                static_cast<double>(box->rect.max.y)
            );
            fmt::fprintf(
                file,
                ",\"focused\":%s,\"flags\":%u,\"duplicate_id\":%s,\"stable_id\":%s}",
                focused != nullptr && focused->id.value == box->id.value ? "true" : "false",
                static_cast<unsigned>(box->flags),
                box->duplicate_id ? "true" : "false",
                box->stable_id ? "true" : "false"
            );
        }
        fmt::fprintf(file, "\n]}\n");
        std::fclose(file);
    }
#endif

    [[nodiscard]] auto append_buffer(char* buffer, size_t capacity, size_t& size, StrRef text)
        -> bool {
        if (text.size() >= capacity || size > capacity - text.size() - 1u) {
            return false;
        }
        if (!text.empty()) {
            std::memcpy(buffer + size, text.data(), text.size());
            size += text.size();
        }
        buffer[size] = '\0';
        return true;
    }

    [[nodiscard]] auto cache_key_hash(StrRef key) -> uint64_t {
        uint64_t hash = 14695981039346656037ull;
        for (char const ch : key) {
            hash ^= static_cast<uint8_t>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    auto append_hex_u64(char*& out, uint64_t value) -> void {
        for (int32_t shift = 60; shift >= 0; shift -= 4) {
            *out++ = hex_digit(static_cast<uint8_t>((value >> shift) & 0x0fu));
        }
    }

    [[nodiscard]] auto
    consume_option_value(int argc, char** argv, int* index, char const* option, StrRef* out_value)
        -> bool {
        if (*index + 1 >= argc) {
            fmt::eprintf("%s requires a value\n", option);
            return false;
        }
        *index += 1;
        *out_value = StrRef(argv[*index]);
        return true;
    }

    [[nodiscard]] auto parse_run_options(int argc, char** argv, RunOptions* out_options) -> bool {
        for (int index = 1; index < argc; ++index) {
            char const* const arg = argv[index];
            if (std::strcmp(arg, "--automation-dump-frame") == 0) {
                if (!consume_option_value(
                        argc,
                        argv,
                        &index,
                        "--automation-dump-frame",
                        &out_options->automation_dump_frame
                    )) {
                    return false;
                }
            } else if (std::strncmp(arg, "--automation-dump-frame=", 24u) == 0) {
                out_options->automation_dump_frame = StrRef(arg + 24u);
            } else if (arg[0] == '-') {
                fmt::eprintf("usage: %s [--automation-dump-frame <path>] [path]\n", argv[0]);
                return false;
            } else if (out_options->initial_path.empty()) {
                out_options->initial_path = StrRef(arg);
            } else {
                fmt::eprintf("usage: %s [--automation-dump-frame <path>] [path]\n", argv[0]);
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto window_cache_path(Arena& arena, StrRef key) -> StrRef {
        char root[MAX_PATH * 4] = {};
        DWORD const root_size =
            GetEnvironmentVariableA("LOCALAPPDATA", root, static_cast<DWORD>(sizeof(root)));
        if (root_size == 0u || root_size >= sizeof(root)) {
            return {};
        }

        char parent[MAX_PATH * 4] = {};
        size_t parent_size = 0u;
        if (!append_buffer(parent, sizeof(parent), parent_size, StrRef(root, root_size)) ||
            !append_buffer(parent, sizeof(parent), parent_size, "\\gui_framework")) {
            return {};
        }
        BASE_UNUSED(CreateDirectoryA(parent, nullptr));

        char directory[MAX_PATH * 4] = {};
        size_t directory_size = 0u;
        if (!append_buffer(directory, sizeof(directory), directory_size, StrRef(parent)) ||
            !append_buffer(directory, sizeof(directory), directory_size, "\\code_editor")) {
            return {};
        }
        BASE_UNUSED(CreateDirectoryA(directory, nullptr));

        char path[MAX_PATH * 4] = {};
        size_t path_size = 0u;
        if (!append_buffer(path, sizeof(path), path_size, StrRef(directory)) ||
            !append_buffer(path, sizeof(path), path_size, "\\window_")) {
            return {};
        }
        char* out = path + path_size;
        append_hex_u64(out, cache_key_hash(key));
        path_size += 16u;
        if (!append_buffer(path, sizeof(path), path_size, ".txt")) {
            return {};
        }
        return arena_copy_cstr(arena, StrRef(path, path_size));
    }

    [[nodiscard]] auto read_cached_window_rect(StrRef path, RECT& out_rect) -> bool {
        std::FILE* const file = open_read_file(path);
        if (file == nullptr) {
            return false;
        }

        long x = 0;
        long y = 0;
        long width = 0;
        long height = 0;
#if defined(_MSC_VER)
        int const count = fscanf_s(file, "%ld %ld %ld %ld", &x, &y, &width, &height);
#else
        int const count = std::fscanf(file, "%ld %ld %ld %ld", &x, &y, &width, &height);
#endif
        std::fclose(file);
        if (count != 4 || width <= 0 || height <= 0) {
            return false;
        }

        out_rect.left = x;
        out_rect.top = y;
        out_rect.right = x + width;
        out_rect.bottom = y + height;
        return true;
    }

    auto restore_window_rect(HWND hwnd, StrRef path) -> void {
        RECT rect = {};
        if (!read_cached_window_rect(path, rect)) {
            return;
        }

        WINDOWPLACEMENT placement = {};
        placement.length = static_cast<UINT>(sizeof(placement));
        if (GetWindowPlacement(hwnd, &placement) == FALSE) {
            return;
        }
        placement.showCmd = SW_SHOWNORMAL;
        placement.rcNormalPosition = rect;
        BASE_UNUSED(SetWindowPlacement(hwnd, &placement));
    }

    auto save_window_rect(HWND hwnd, StrRef path) -> void {
        if (path.empty() || hwnd == nullptr || !IsWindow(hwnd)) {
            return;
        }

        WINDOWPLACEMENT placement = {};
        placement.length = static_cast<UINT>(sizeof(placement));
        if (GetWindowPlacement(hwnd, &placement) == FALSE) {
            return;
        }

        RECT const rect = placement.rcNormalPosition;
        std::FILE* const file = open_write_file(path);
        if (file == nullptr) {
            return;
        }
        BASE_UNUSED(
            fmt::fprintf(
                file,
                "%ld %ld %ld %ld\n",
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top
            )
        );
        std::fclose(file);
    }

    [[nodiscard]] auto window_client_size(HWND hwnd) -> render::SizeU32 {
        RECT rect = {};
        if (GetClientRect(hwnd, &rect) == FALSE) {
            return {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
        }
        return {
            static_cast<uint32_t>(std::max<LONG>(1, rect.right - rect.left)),
            static_cast<uint32_t>(std::max<LONG>(1, rect.bottom - rect.top)),
        };
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_DPICHANGED: {
            RECT const* const rect = reinterpret_cast<RECT const*>(lparam);
            if (rect != nullptr) {
                BASE_UNUSED(SetWindowPos(
                    hwnd,
                    nullptr,
                    rect->left,
                    rect->top,
                    rect->right - rect->left,
                    rect->bottom - rect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                ));
            }
            if (global_app_state != nullptr) {
                global_app_state->pending_size = window_client_size(hwnd);
                global_app_state->resize_pending = true;
                request_redraw(global_app_state);
            }
            return 0;
        }
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                    request_redraw(global_app_state);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                gui::Id const hit_id = frame_hit_id(global_app_state->last_frame, pos);
                bool const needs_frame = global_app_state->redraw_pending ||
                                         !frame_ready(global_app_state->last_frame) ||
                                         global_app_state->input.mouse_down[0u] ||
                                         global_app_state->input.mouse_down[1u] ||
                                         hit_id.value != global_app_state->mouse_hit_id.value;
                global_app_state->input.mouse_pos = pos;
                global_app_state->mouse_hit_id = hit_id;
                if (needs_frame) {
                    request_redraw(global_app_state);
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            push_mouse_down(global_app_state, {lparam_x(lparam), lparam_y(lparam)}, false);
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;
        case WM_LBUTTONDBLCLK:
            push_mouse_down(global_app_state, {lparam_x(lparam), lparam_y(lparam)}, true);
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;
        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[0u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
                request_redraw(global_app_state);
            }
            if ((global_app_state == nullptr || !global_app_state->input.mouse_down[1u]) &&
                GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        case WM_MBUTTONDOWN:
            push_middle_mouse_down(global_app_state, {lparam_x(lparam), lparam_y(lparam)});
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;
        case WM_MBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[1u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
                request_redraw(global_app_state);
            }
            if (global_app_state == nullptr || (!global_app_state->input.mouse_down[0u] &&
                                                !global_app_state->input.mouse_down[1u])) {
                if (GetCapture() == hwnd) {
                    ReleaseCapture();
                }
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                POINT point = {
                    static_cast<LONG>(lparam_x(lparam)),
                    static_cast<LONG>(lparam_y(lparam)),
                };
                BASE_UNUSED(ScreenToClient(hwnd, &point));
                global_app_state->input.mouse_pos = {
                    static_cast<float>(point.x), static_cast<float>(point.y)
                };
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 72.0f;
                global_app_state->input.key_mods = current_key_mods();
                request_redraw(global_app_state);
            }
            return 0;
        case WM_DROPFILES: {
            HDROP const drop = reinterpret_cast<HDROP>(wparam);
            if (queue_file_drop(global_app_state, drop)) {
                request_redraw(global_app_state);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            gui::Key const key = key_from_virtual_key(wparam);
            if (key != gui::Key::UNKNOWN) {
                push_key_event(global_app_state, key, key_down_kind(lparam), current_key_mods());
                request_redraw(global_app_state);
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        case WM_CHAR:
        case WM_SYSCHAR:
            if (global_app_state != nullptr && wparam >= 32u && wparam != 127u) {
                push_text_event(
                    global_app_state, static_cast<uint32_t>(wparam), current_key_mods()
                );
                request_redraw(global_app_state);
            }
            return 0;
        case WM_CLOSE:
            if (global_app_state != nullptr) {
                if (!global_app_state->close_confirmed) {
                    global_app_state->close_requested = true;
                    request_redraw(global_app_state);
                    return 0;
                }
                save_window_rect(hwnd, global_app_state->window_cache_path);
                global_app_state->running = false;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    auto apply_window_header_theme(HWND hwnd) -> void {
        BOOL const dark_mode = TRUE;
        COLORREF const border_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const caption_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const text_color = WINDOW_HEADER_TEXT;

        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd,
            DWM_ATTR_USE_IMMERSIVE_DARK_MODE,
            &dark_mode,
            static_cast<DWORD>(sizeof(dark_mode))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_BORDER_COLOR, &border_color, static_cast<DWORD>(sizeof(border_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_CAPTION_COLOR, &caption_color, static_cast<DWORD>(sizeof(caption_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_TEXT_COLOR, &text_color, static_cast<DWORD>(sizeof(text_color))
        ));
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state, StrRef window_cache) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hIcon = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(CODE_EDITOR_ICON_ID),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR | LR_SHARED
        ));
        window_class.hIconSm = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(CODE_EDITOR_ICON_ID),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR | LR_SHARED
        ));
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            return false;
        }

        HWND const hwnd = CreateWindowExW(
            0u,
            WINDOW_CLASS_NAME,
            L"Code Editor",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            return false;
        }

        apply_window_header_theme(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        restore_window_rect(hwnd, window_cache);
        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    auto destroy_testbed_window(AppState* app_state) -> void {
        if (app_state->hwnd != nullptr && IsWindow(app_state->hwnd)) {
            DragAcceptFiles(app_state->hwnd, FALSE);
            DestroyWindow(app_state->hwnd);
        }
        app_state->hwnd = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    }

    [[nodiscard]] auto read_file_text(Arena& arena, StrRef path, StrRef& out_text) -> bool {
        std::FILE* const file = open_read_file(path);
        if (file == nullptr) {
            return false;
        }
        bool ok = std::fseek(file, 0, SEEK_END) == 0;
        long const size = ok ? std::ftell(file) : -1l;
        ok = ok && size >= 0l && std::fseek(file, 0, SEEK_SET) == 0;
        if (ok && size != 0l) {
            char* const text = arena_alloc<char>(arena, static_cast<size_t>(size));
            size_t const read_size = std::fread(text, 1u, static_cast<size_t>(size), file);
            ok = read_size == static_cast<size_t>(size);
            out_text = ok ? StrRef(text, read_size) : StrRef();
        }
        std::fclose(file);
        return ok;
    }

    [[nodiscard]] auto path_without_trailing_slash(StrRef path) -> StrRef {
        while (path.size() > 1u && (path.back() == '\\' || path.back() == '/') &&
               !(path.size() == 3u && path[1u] == ':')) {
            path.remove_suffix(1u);
        }
        return path;
    }

    [[nodiscard]] auto path_leaf(StrRef path) -> StrRef {
        path = path_without_trailing_slash(path);
        size_t const slash = path.find_last_of("\\/");
        if (slash == StrRef::NPOS) {
            return path;
        }
        StrRef const leaf = path.substr(slash + 1u);
        return leaf.empty() ? path : leaf;
    }

    [[nodiscard]] auto path_parent(StrRef path) -> StrRef {
        path = path_without_trailing_slash(path);
        size_t const slash = path.find_last_of("\\/");
        if (slash == StrRef::NPOS) {
            return ".";
        }
        if (slash == 0u) {
            return path.prefix(1u);
        }
        if (slash == 2u && path[1u] == ':') {
            return path.prefix(3u);
        }
        return path.prefix(slash);
    }

    [[nodiscard]] auto path_is_directory(StrRef path) -> bool {
        DWORD const attributes = GetFileAttributesA(path.data());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
    }

    [[nodiscard]] auto full_path_cstr(Arena& arena, StrRef path) -> StrRef {
        char buffer[MAX_PATH * 4] = {};
        DWORD const size =
            GetFullPathNameA(path.data(), static_cast<DWORD>(sizeof(buffer)), buffer, nullptr);
        return size != 0u && size < sizeof(buffer) ? arena_copy_cstr(arena, StrRef(buffer, size))
                                                   : StrRef();
    }

    [[nodiscard]] auto current_directory_cstr(Arena& arena) -> StrRef {
        char buffer[MAX_PATH * 4] = {};
        DWORD const size = GetCurrentDirectoryA(static_cast<DWORD>(sizeof(buffer)), buffer);
        return size != 0u && size < sizeof(buffer) ? arena_copy_cstr(arena, StrRef(buffer, size))
                                                   : StrRef();
    }

    [[nodiscard]] auto child_path_cstr(Arena& arena, StrRef directory, StrRef name) -> StrRef {
        char path[MAX_PATH * 4] = {};
        size_t size = 0u;
        StrRef const trimmed_directory = path_without_trailing_slash(directory);
        if (!append_buffer(path, sizeof(path), size, trimmed_directory)) {
            return {};
        }
        if (!trimmed_directory.empty() && !trimmed_directory.ends_with('\\') &&
            !trimmed_directory.ends_with('/')) {
            if (!append_buffer(path, sizeof(path), size, "\\")) {
                return {};
            }
        }
        if (!append_buffer(path, sizeof(path), size, name)) {
            return {};
        }
        return arena_copy_cstr(arena, StrRef(path, size));
    }

    [[nodiscard]] auto host_path_exists(StrRef path) -> bool {
        return GetFileAttributesA(path.data()) != INVALID_FILE_ATTRIBUTES;
    }

    [[nodiscard]] auto joined_path(StrRef directory, StrRef name, char* buffer, size_t capacity)
        -> StrRef {
        size_t size = 0u;
        StrRef const trimmed_directory = path_without_trailing_slash(directory);
        if (!append_buffer(buffer, capacity, size, trimmed_directory)) {
            return {};
        }
        if (!name.empty()) {
            if (!trimmed_directory.empty() && !trimmed_directory.ends_with('\\') &&
                !trimmed_directory.ends_with('/')) {
                if (!append_buffer(buffer, capacity, size, "\\")) {
                    return {};
                }
            }
            if (!append_buffer(buffer, capacity, size, name)) {
                return {};
            }
        }
        return StrRef(buffer, size);
    }

    auto copy_tree_operation_path(char* target, StrRef source) -> void {
        size_t const size = std::min(source.size(), TREE_OPERATION_PATH_CAPACITY - 1u);
        if (size != 0u) {
            std::memcpy(target, source.data(), size);
        }
        target[size] = '\0';
    }

    auto
    clear_tree_operation_result(LaunchDesc& launch, uint64_t generation, TreeOperationKind kind)
        -> void {
        launch.tree_operation_result = {
            .generation = generation,
            .request_kind = kind,
        };
    }

    auto set_tree_operation_result(
        LaunchDesc& launch,
        uint64_t generation,
        TreeOperationKind request_kind,
        TreeOperationUpdateKind update_kind,
        bool success,
        StrRef source_path = {},
        StrRef target_path = {}
    ) -> void {
        launch.tree_operation_result = {
            .generation = generation,
            .request_kind = request_kind,
            .update_kind = update_kind,
            .success = success,
        };
        copy_tree_operation_path(launch.tree_operation_result.source_path, source_path);
        copy_tree_operation_path(launch.tree_operation_result.target_path, target_path);
    }

    [[nodiscard]] auto write_file_bytes(StrRef path, StrRef text) -> bool {
        std::FILE* const file = open_write_file(path);
        if (file == nullptr) {
            return false;
        }
        bool const ok =
            text.empty() || std::fwrite(text.data(), 1u, text.size(), file) == text.size();
        std::fclose(file);
        return ok;
    }

    [[nodiscard]] auto delete_tree_path_recursive(StrRef path) -> bool {
        if (!host_path_exists(path)) {
            return true;
        }
        if (!path_is_directory(path)) {
            return DeleteFileA(path.data()) != 0;
        }

        char search[MAX_PATH * 4] = {};
        StrRef const search_path = joined_path(path, "*", search, sizeof(search));
        if (search_path.empty()) {
            return false;
        }

        WIN32_FIND_DATAA find_data = {};
        HANDLE const find = FindFirstFileA(search_path.data(), &find_data);
        if (find == INVALID_HANDLE_VALUE) {
            return GetLastError() == ERROR_FILE_NOT_FOUND && RemoveDirectoryA(path.data()) != 0;
        }

        bool ok = true;
        do {
            StrRef const name(find_data.cFileName);
            if (name == "." || name == "..") {
                continue;
            }
            char child_buffer[MAX_PATH * 4] = {};
            StrRef const child_path = joined_path(path, name, child_buffer, sizeof(child_buffer));
            ok = !child_path.empty() && delete_tree_path_recursive(child_path);
        } while (ok && FindNextFileA(find, &find_data));

        FindClose(find);
        return ok && RemoveDirectoryA(path.data()) != 0;
    }

    [[nodiscard]] auto push_snapshot_entry(
        LaunchDesc& launch,
        Vec<LaunchDesc::TreeSnapshotEntry>& snapshot,
        StrRef relative_path,
        StrRef text,
        bool is_directory
    ) -> bool {
        return snapshot.push_back({
            .relative_path = arena_copy_cstr(launch.tree_history_arena, relative_path),
            .text = text,
            .is_directory = is_directory,
        });
    }

    [[nodiscard]] auto collect_tree_snapshot(
        LaunchDesc& launch,
        Vec<LaunchDesc::TreeSnapshotEntry>& snapshot,
        StrRef path,
        StrRef relative_path
    ) -> bool {
        bool const is_directory = path_is_directory(path);
        if (!is_directory) {
            StrRef text = {};
            return read_file_text(launch.tree_history_arena, path, text) &&
                   push_snapshot_entry(launch, snapshot, relative_path, text, false);
        }

        if (!push_snapshot_entry(launch, snapshot, relative_path, {}, true)) {
            return false;
        }

        char search[MAX_PATH * 4] = {};
        StrRef const search_path = joined_path(path, "*", search, sizeof(search));
        if (search_path.empty()) {
            return false;
        }

        WIN32_FIND_DATAA find_data = {};
        HANDLE const find = FindFirstFileA(search_path.data(), &find_data);
        if (find == INVALID_HANDLE_VALUE) {
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        }

        bool ok = true;
        do {
            StrRef const name(find_data.cFileName);
            if (name == "." || name == "..") {
                continue;
            }
            char child_path_buffer[MAX_PATH * 4] = {};
            StrRef const child_path =
                joined_path(path, name, child_path_buffer, sizeof(child_path_buffer));
            char child_relative_buffer[MAX_PATH * 4] = {};
            StrRef const child_relative = joined_path(
                relative_path, name, child_relative_buffer, sizeof(child_relative_buffer)
            );
            ok = !child_path.empty() && !child_relative.empty() &&
                 collect_tree_snapshot(launch, snapshot, child_path, child_relative);
        } while (ok && FindNextFileA(find, &find_data));

        FindClose(find);
        return ok;
    }

    [[nodiscard]] auto
    restore_tree_snapshot(StrRef root_path, Slice<LaunchDesc::TreeSnapshotEntry> snapshot) -> bool {
        for (LaunchDesc::TreeSnapshotEntry const& entry : snapshot) {
            char path_buffer[MAX_PATH * 4] = {};
            StrRef const path =
                entry.relative_path.empty()
                    ? joined_path(root_path, {}, path_buffer, sizeof(path_buffer))
                    : joined_path(root_path, entry.relative_path, path_buffer, sizeof(path_buffer));
            if (path.empty()) {
                return false;
            }
            if (entry.is_directory) {
                if (CreateDirectoryA(path.data(), nullptr) == 0 &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
                continue;
            }
            if (!write_file_bytes(path, entry.text)) {
                return false;
            }
        }
        return true;
    }

    auto clear_tree_redo(LaunchDesc& launch) -> void {
        launch.tree_redo_stack = nullptr;
    }

    auto push_tree_history_entry(
        LaunchDesc& launch,
        TreeOperationKind kind,
        StrRef source_path,
        StrRef target_path,
        Slice<LaunchDesc::TreeSnapshotEntry> snapshot = {}
    ) -> void {
        auto* const entry = arena_new<LaunchDesc::TreeHistoryEntry>(launch.tree_history_arena);
        entry->previous = launch.tree_undo_stack;
        entry->kind = kind;
        entry->source_path = arena_copy_cstr(launch.tree_history_arena, source_path);
        entry->target_path = arena_copy_cstr(launch.tree_history_arena, target_path);
        if (!snapshot.empty()) {
            auto* const entries = arena_alloc<LaunchDesc::TreeSnapshotEntry>(
                launch.tree_history_arena, snapshot.size()
            );
            for (size_t index = 0u; index < snapshot.size(); ++index) {
                entries[index] = snapshot[index];
            }
            entry->snapshot = {entries, snapshot.size()};
        }
        launch.tree_undo_stack = entry;
        clear_tree_redo(launch);
    }

    auto
    move_tree_history_entry(LaunchDesc::TreeHistoryEntry*& from, LaunchDesc::TreeHistoryEntry*& to)
        -> LaunchDesc::TreeHistoryEntry* {
        if (from == nullptr) {
            return nullptr;
        }
        LaunchDesc::TreeHistoryEntry* const entry = from;
        from = entry->previous;
        entry->previous = to;
        to = entry;
        return entry;
    }

    [[nodiscard]] auto old_tree_entry_open(Slice<FileTreeEntry> old_files, StrRef relative_path)
        -> bool {
        for (FileTreeEntry const& file : old_files) {
            if (file.is_directory && file.relative_path == relative_path) {
                return file.open;
            }
        }
        return false;
    }

    auto set_file_search_visible(Vec<FileTreeEntry>& files, bool visible) -> void {
        for (FileTreeEntry& file : files) {
            file.file_search_visible = visible;
        }
    }

    [[nodiscard]] auto tracked_path_less(StrRef lhs, StrRef rhs) -> bool {
        return lhs.compare_ignore_ascii_case(rhs) < 0;
    }

    [[nodiscard]] auto tracked_paths_contains(Slice<StrRef const> paths, StrRef path) -> bool {
        size_t begin = 0u;
        size_t end = paths.size();
        while (begin < end) {
            size_t const mid = begin + ((end - begin) / 2u);
            if (tracked_path_less(paths[mid], path)) {
                begin = mid + 1u;
            } else {
                end = mid;
            }
        }
        return begin < paths.size() && paths[begin].equals_ignore_ascii_case(path);
    }

    auto mark_file_search_ancestors(Vec<FileTreeEntry>& files, Slice<size_t const> stack) -> void {
        for (size_t const index : stack) {
            files[index].file_search_visible = true;
        }
    }

    auto mark_file_search_tracked_files(StrRef directory, Vec<FileTreeEntry>& files) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        Vec<StrRef> tracked_paths = {};
        if (!tracked_paths.init(256u, temp.arena()->resource())) {
            return;
        }

        StrRef const command = fmt::tprintf("git -C \"%s\" ls-files --cached 2>nul", directory);
        std::FILE* const pipe = _popen(command.data(), "r");
        if (pipe == nullptr) {
            return;
        }

        bool ok = true;
        char line[GIT_PATH_LINE_CAPACITY] = {};
        while (std::fgets(line, static_cast<int>(sizeof(line)), pipe) != nullptr) {
            StrRef const path = StrRef(line).trim_end_matches('\n').trim_end_matches('\r');
            for (size_t index = 0u; index < path.size(); ++index) {
                if (line[index] == '/') {
                    line[index] = '\\';
                }
            }
            if (ok && !tracked_paths.push_back(arena_copy_cstr(*temp.arena(), path))) {
                ok = false;
            }
        }
        if (_pclose(pipe) != 0 || !ok) {
            set_file_search_visible(files, true);
            return;
        }

        if (tracked_paths.size() > 1u) {
            std::sort(tracked_paths.begin(), tracked_paths.end(), tracked_path_less);
        }

        Vec<size_t> directory_stack = {};
        if (!directory_stack.init(32u, temp.arena()->resource())) {
            set_file_search_visible(files, true);
            return;
        }

        for (size_t index = 0u; index < files.size(); ++index) {
            FileTreeEntry& file = files[index];
            while (directory_stack.size() > file.depth) {
                BASE_UNUSED(directory_stack.pop());
            }

            file.file_search_visible =
                !file.is_directory &&
                tracked_paths_contains(tracked_paths.slice(), file.relative_path);
            if (file.is_directory) {
                if (!directory_stack.push_back(index)) {
                    set_file_search_visible(files, true);
                    return;
                }
            } else if (file.file_search_visible) {
                mark_file_search_ancestors(files, directory_stack.slice());
            }
        }
    }

    [[nodiscard]] auto append_tree_entry(
        Arena& arena,
        Vec<FileTreeEntry>& files,
        Slice<FileTreeEntry> old_files,
        StrRef directory,
        StrRef relative_directory,
        StrRef name,
        size_t depth,
        bool is_directory
    ) -> bool {
        StrRef const path = child_path_cstr(arena, directory, name);
        StrRef const relative_path = child_path_cstr(arena, relative_directory, name);
        return !path.empty() && !relative_path.empty() &&
               files.push_back({
                   .name = arena_copy_cstr(arena, name),
                   .path = path,
                   .relative_path = relative_path,
                   .depth = depth,
                   .is_directory = is_directory,
                   .open = is_directory && old_tree_entry_open(old_files, relative_path),
               });
    }

    [[nodiscard]] auto read_directory_files(
        Arena& arena,
        StrRef directory,
        StrRef relative_directory,
        Vec<FileTreeEntry>& files,
        size_t depth,
        Slice<FileTreeEntry> old_files = {}
    ) -> bool {
        StrRef const directory_cstr =
            arena_copy_cstr(arena, path_without_trailing_slash(directory));
        StrRef const search_path = child_path_cstr(arena, directory_cstr, "*");
        if (search_path.empty()) {
            return false;
        }

        Vec<FileTreeEntry> entries = {};
        if (!entries.init(32u, arena.resource())) {
            return false;
        }

        WIN32_FIND_DATAA find_data = {};
        HANDLE const find = FindFirstFileA(search_path.data(), &find_data);
        if (find == INVALID_HANDLE_VALUE) {
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        }

        do {
            StrRef const name(find_data.cFileName);
            if (name == "." || name == "..") {
                continue;
            }
            bool const is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
            if (is_directory && name.equals_ignore_ascii_case(".git")) {
                continue;
            }
            if (!append_tree_entry(
                    arena,
                    entries,
                    old_files,
                    directory_cstr,
                    relative_directory,
                    name,
                    depth,
                    is_directory
                )) {
                FindClose(find);
                return false;
            }
        } while (FindNextFileA(find, &find_data));

        FindClose(find);
        if (entries.size() > 1u) {
            std::sort(
                entries.begin(), entries.end(), [](FileTreeEntry const& a, FileTreeEntry const& b) {
                    if (a.is_directory != b.is_directory) {
                        return a.is_directory;
                    }
                    return a.name.compare_ignore_ascii_case(b.name) < 0;
                }
            );
        }

        for (FileTreeEntry entry : entries) {
            StrRef const path = entry.path;
            if (!files.push_back(entry)) {
                return false;
            }
            if (entry.is_directory) {
                BASE_UNUSED(read_directory_files(
                    arena, path, entry.relative_path, files, depth + 1u, old_files
                ));
            }
        }
        if (depth == 0u) {
            mark_file_search_tracked_files(directory_cstr, files);
        }
        return true;
    }

    [[nodiscard]] auto prepare_launch(Arena& arena, StrRef initial_path, LaunchDesc& launch)
        -> bool {
        launch.tree_arenas[0u].init();
        launch.tree_arenas[1u].init();
        launch.tree_history_arena.init();
        if (!launch.tree_files.init(32u, launch.tree_arenas[0u].resource())) {
            return false;
        }
        if (initial_path.empty()) {
            launch.save_root_path = current_directory_cstr(arena);
            launch.window_cache_path = window_cache_path(arena, launch.save_root_path);
            launch.tree_root_name = arena_copy_str(arena, path_leaf(launch.save_root_path));
            launch.initial_tree_refresh = true;
            return true;
        }

        StrRef const full_path = full_path_cstr(arena, initial_path);
        if (full_path.empty()) {
            return false;
        }

        StrRef const path = path_without_trailing_slash(full_path);
        launch.window_cache_path = window_cache_path(arena, path);
        if (path_is_directory(full_path)) {
            launch.save_root_path = path;
            launch.tree_root_name = arena_copy_str(arena, path_leaf(path));
            launch.initial_tree_refresh = true;
            return true;
        }

        if (!read_file_text(arena, full_path, launch.initial_text)) {
            fmt::eprintf("code_editor: failed to read %s\n", full_path);
            return false;
        }
        launch.initial_text = editor_display_text(arena, launch.initial_text);

        launch.initial_file_name = arena_copy_str(arena, path_leaf(path));
        launch.initial_file_path = path;
        StrRef const parent = path_parent(path);
        launch.save_root_path = parent;
        launch.tree_root_name = arena_copy_str(arena, path_leaf(parent));
        launch.initial_tree_refresh = true;
        return true;
    }

    auto apply_tree_open_state(Vec<FileTreeEntry>& files, Slice<FileTreeEntry> old_files) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        HashMap<StrRef, bool> open_dirs = {};
        if (!open_dirs.init(old_files.size(), temp.arena()->resource())) {
            return;
        }
        for (FileTreeEntry const& file : old_files) {
            if (file.is_directory && file.open) {
                BASE_UNUSED(open_dirs.set(file.relative_path, true));
            }
        }
        for (FileTreeEntry& file : files) {
            if (file.is_directory) {
                file.open = open_dirs.contains(file.relative_path);
            }
        }
    }

    [[nodiscard]] auto
    git_work_root(Arena& arena, GitWorkRequest const& request, StrRef& root, StrRef& message)
        -> bool {
        if (!request.root.empty()) {
            root = request.root;
            message = {};
            return true;
        }
        if (request.save_root.empty()) {
            message = "Not a Git repository.";
            return false;
        }
        return git_discover_root(arena, request.save_root, root, message);
    }

    [[nodiscard]] auto init_git_result_vecs(Arena& arena, GitWorkResult& result) -> bool {
        return result.status_items.init(0u, arena.resource()) &&
               result.commits.init(0u, arena.resource()) &&
               result.commit_files.init(0u, arena.resource()) &&
               result.branches.init(0u, arena.resource());
    }

    [[nodiscard]] auto load_git_status_path_result(
        Arena& arena, StrRef root, StrRef path, GitWorkResult& result, StrRef& message
    ) -> bool {
        StrRef status_message = {};
        if (git_load_status_path(arena, root, path, result.status_items, status_message)) {
            return true;
        }
        message = status_message;
        return false;
    }

    [[nodiscard]] auto execute_git_work(Arena& arena, GitWorkRequest const& request)
        -> GitWorkResult {
        GitWorkResult result = {
            .kind = request.kind,
            .generation = request.generation,
            .offset = request.offset,
            .count = request.count,
            .limit = request.limit,
            .scope = request.scope,
            .path = request.path,
            .commit_oid = request.commit_oid,
        };
        BASE_UNUSED(init_git_result_vecs(arena, result));

        StrRef root = {};
        StrRef message = {};
        if (!git_work_root(arena, request, root, message)) {
            result.root = root;
            result.message = message;
            return result;
        }
        result.root = root;

        switch (request.kind) {
        case GitWorkKind::REFRESH:
            result.ok =
                git_load_branches(arena, root, result.branches, result.current_branch, message) &&
                git_load_status(arena, root, result.status_items, message) &&
                git_load_pending_pull_count(arena, root, result.pending_pull_count, message) &&
                git_load_pending_push_count(arena, root, result.pending_push_count, message) &&
                git_load_operation_state(arena, root, result.operation_state, message);
            result.log_loaded = result.ok && request.count != 0u;
            if (result.log_loaded) {
                result.ok =
                    git_load_commits(arena, root, 0u, request.count, result.commits, message);
            }
            break;
        case GitWorkKind::COMMIT_PAGE:
            result.ok = git_load_commits(
                arena, root, request.offset, request.count + 1u, result.commits, message
            );
            break;
        case GitWorkKind::COMMIT_FILES:
            result.ok = git_load_commit_files(
                arena, root, request.commit_oid, result.commit_files, message
            );
            break;
        case GitWorkKind::STAGE:
            result.ok = git_stage_path(arena, root, request.path, message) &&
                        load_git_status_path_result(arena, root, request.path, result, message);
            break;
        case GitWorkKind::STAGE_ALL:
            result.ok = git_stage_all(arena, root, message);
            break;
        case GitWorkKind::UNSTAGE:
            result.ok = git_unstage_path(arena, root, request.path, message) &&
                        load_git_status_path_result(arena, root, request.path, result, message);
            break;
        case GitWorkKind::UNSTAGE_ALL:
            result.ok = git_unstage_all(arena, root, message);
            break;
        case GitWorkKind::COMMIT:
            result.ok = git_commit(arena, root, request.message_text, message);
            break;
        case GitWorkKind::PUSH:
            result.ok = git_push(arena, root, message);
            break;
        case GitWorkKind::PULL:
            result.ok = git_pull(arena, root, message);
            break;
        case GitWorkKind::FETCH:
            result.ok = git_fetch(arena, root, message);
            break;
        case GitWorkKind::MERGE_BRANCH:
            result.ok = git_merge_branch(arena, root, request.branch, message);
            break;
        case GitWorkKind::REBASE_BRANCH:
            result.ok = git_rebase_branch(arena, root, request.branch, message);
            break;
        case GitWorkKind::CHERRY_PICK:
            result.ok = git_cherry_pick(arena, root, request.branch, message);
            break;
        case GitWorkKind::MERGE_ABORT:
            result.ok = git_merge_abort(arena, root, message);
            break;
        case GitWorkKind::REBASE_CONTINUE:
            result.ok = git_rebase_continue(arena, root, message);
            break;
        case GitWorkKind::REBASE_ABORT:
            result.ok = git_rebase_abort(arena, root, message);
            break;
        case GitWorkKind::CHERRY_PICK_CONTINUE:
            result.ok = git_cherry_pick_continue(arena, root, message);
            break;
        case GitWorkKind::CHERRY_PICK_ABORT:
            result.ok = git_cherry_pick_abort(arena, root, message);
            break;
        case GitWorkKind::CHECKOUT_BRANCH:
            result.ok = git_checkout_branch(arena, root, request.branch, message);
            break;
        case GitWorkKind::OPEN_STATUS_DIFF:
            result.ok =
                git_status_patch(arena, root, request.scope, request.path, result.patch, message);
            break;
        case GitWorkKind::OPEN_COMMIT_DIFF:
            result.ok = git_commit_patch(
                arena, root, request.commit_oid, request.path, result.patch, message
            );
            break;
        case GitWorkKind::NONE:
        default:
            break;
        }
        result.message = message;
        return result;
    }

    auto WINAPI background_worker_thread(void* user_data) -> DWORD {
        auto* const worker = static_cast<BackgroundWorker*>(user_data);
        init_thread_temp_arenas();
        while (!worker->stop_requested.load(std::memory_order_acquire)) {
            GitWorkRequest git_request = {};
            if (worker->git_requests.pop(git_request)) {
                GitWorkResult const result =
                    execute_git_work(worker->git_result_arena, git_request);
                while (!worker->stop_requested.load(std::memory_order_acquire) &&
                       !worker->git_results.push(result)) {
                    Sleep(1u);
                }
                reset_thread_temp_arenas();
                continue;
            }

            TreeFetchRequest request = {};
            if (!worker->requests.pop(request)) {
                Sleep(1u);
                continue;
            }

            Vec<FileTreeEntry> files = {};
            bool const ok = request.arena != nullptr && !request.root.empty() &&
                            files.init(32u, request.arena->resource()) &&
                            read_directory_files(*request.arena, request.root, "", files, 0u);
            TreeFetchResult const result = {
                .generation = request.generation,
                .files = files,
                .arena_index = request.arena_index,
                .ok = ok,
            };
            while (!worker->stop_requested.load(std::memory_order_acquire) &&
                   !worker->results.push(result)) {
                Sleep(1u);
            }
            if (ok) {
                GitWorkRequest const refresh_request = {
                    .kind = GitWorkKind::REFRESH,
                    .count = request.load_git_log ? request.git_log_limit + 1u : 0u,
                    .limit = request.git_log_limit,
                    .save_root = request.root,
                };
                GitWorkResult const git_result =
                    execute_git_work(worker->git_result_arena, refresh_request);
                while (!worker->stop_requested.load(std::memory_order_acquire) &&
                       !worker->git_results.push(git_result)) {
                    Sleep(1u);
                }
            }
            reset_thread_temp_arenas();
        }
        shutdown_thread_temp_arenas();
        return 0u;
    }

    [[nodiscard]] auto create_background_worker(Arena& arena, BackgroundWorker& worker) -> bool {
        worker.stop_requested.store(false, std::memory_order_relaxed);
        if (!worker.git_requests.init(GIT_WORK_QUEUE_CAPACITY, arena.resource()) ||
            !worker.git_results.init(GIT_WORK_QUEUE_CAPACITY, arena.resource()) ||
            !worker.requests.init(TREE_FETCH_QUEUE_CAPACITY, arena.resource()) ||
            !worker.results.init(TREE_FETCH_QUEUE_CAPACITY, arena.resource())) {
            return false;
        }
        worker.git_result_arena.init();
        worker.thread = CreateThread(nullptr, 0u, background_worker_thread, &worker, 0u, nullptr);
        return worker.thread != nullptr;
    }

    auto destroy_background_worker(BackgroundWorker& worker) -> void {
        worker.stop_requested.store(true, std::memory_order_release);
        if (worker.thread != nullptr) {
            WaitForSingleObject(worker.thread, INFINITE);
            CloseHandle(worker.thread);
            worker.thread = nullptr;
        }
        worker.git_result_arena.destroy();
        worker.git_requests = {};
        worker.git_results = {};
        worker.requests = {};
        worker.results = {};
    }

    [[nodiscard]] auto git_log_limit_for_window_height(uint32_t height) -> size_t {
        size_t const rows = (static_cast<size_t>(height) + 23u) / 24u;
        return std::max(GIT_LOG_MIN_LIMIT, rows);
    }

    [[nodiscard]] auto request_launch_tree_refresh(LaunchDesc& launch) -> bool {
        if (launch.save_root_path.empty()) {
            return false;
        }
        launch.tree_loading = true;
        if (launch.tree_fetch_pending) {
            launch.tree_fetch_refresh_requested = true;
            return true;
        }

        uint32_t const old_index = launch.tree_arena_index;
        uint32_t const new_index = 1u - old_index;
        Arena& arena = launch.tree_arenas[new_index];
        arena.reset();

        TreeFetchRequest const request = {
            .generation = launch.tree_fetch_generation + 1u,
            .git_log_limit = launch.git_log_limit,
            .load_git_log = !launch.git_log_prefetched,
            .root = launch.save_root_path,
            .arena = &arena,
            .arena_index = new_index,
        };
        if (!launch.worker.requests.push(request)) {
            launch.tree_loading = false;
            return false;
        }

        launch.tree_fetch_generation = request.generation;
        launch.tree_fetch_pending = true;
        launch.git_log_prefetched = launch.git_log_prefetched || request.load_git_log;
        return true;
    }

    auto refresh_launch_tree(LaunchDesc& launch) -> void {
        BASE_UNUSED(request_launch_tree_refresh(launch));
    }

    [[nodiscard]] auto sync_launch_tree_fetch_results(LaunchDesc& launch) -> bool {
        bool changed = false;
        TreeFetchResult result = {};
        while (launch.worker.results.pop(result)) {
            if (result.generation != launch.tree_fetch_generation) {
                continue;
            }

            launch.tree_fetch_pending = false;
            if (result.ok) {
                uint32_t const old_index = launch.tree_arena_index;
                apply_tree_open_state(result.files, launch.tree_files.slice());
                launch.tree_files = result.files;
                launch.tree_arena_index = result.arena_index;
                launch.tree_arenas[old_index].reset();
                launch.file_change_generation += 1u;
            }

            if (launch.tree_fetch_refresh_requested) {
                launch.tree_fetch_refresh_requested = false;
                BASE_UNUSED(request_launch_tree_refresh(launch));
            } else {
                launch.tree_loading = false;
            }
            changed = true;
        }
        return changed;
    }

    [[nodiscard]] auto tree_file_index_by_path(Slice<FileTreeEntry const> files, StrRef path)
        -> size_t {
        for (size_t index = 0u; index < files.size(); ++index) {
            if (files[index].path.equals_ignore_ascii_case(path)) {
                return index;
            }
        }
        return files.size();
    }

    [[nodiscard]] auto tree_file_subtree_end(Slice<FileTreeEntry const> files, size_t index)
        -> size_t {
        if (index >= files.size()) {
            return files.size();
        }
        if (!files[index].is_directory) {
            return index + 1u;
        }

        size_t end = index + 1u;
        size_t const depth = files[index].depth;
        while (end < files.size() && files[end].depth > depth) {
            end += 1u;
        }
        return end;
    }

    [[nodiscard]] auto tree_file_less(FileTreeEntry const& a, FileTreeEntry const& b) -> bool {
        if (a.is_directory != b.is_directory) {
            return a.is_directory;
        }
        return a.name.compare_ignore_ascii_case(b.name) < 0;
    }

    [[nodiscard]] auto tree_file_parent_index(Slice<FileTreeEntry const> files, size_t index)
        -> size_t {
        if (index >= files.size() || files[index].depth == 0u) {
            return files.size();
        }

        size_t const depth = files[index].depth;
        for (size_t parent = index; parent > 0u;) {
            parent -= 1u;
            if (files[parent].is_directory && files[parent].depth + 1u == depth) {
                return parent;
            }
        }
        return files.size();
    }

    [[nodiscard]] auto
    tree_file_sorted_insert_index(Slice<FileTreeEntry const> files, size_t index, size_t end)
        -> size_t {
        size_t const parent = tree_file_parent_index(files, index);
        size_t const begin = parent < files.size() ? parent + 1u : 0u;
        size_t const stop =
            parent < files.size() ? tree_file_subtree_end(files, parent) : files.size();
        size_t const depth = files[index].depth;

        for (size_t at = begin; at < stop;) {
            if (at == index) {
                at = end;
                continue;
            }
            if (files[at].depth != depth) {
                at += 1u;
                continue;
            }
            if (tree_file_less(files[index], files[at])) {
                return at;
            }
            at = tree_file_subtree_end(files, at);
        }
        return stop;
    }

    [[nodiscard]] auto tree_file_child_insert_index(
        Slice<FileTreeEntry const> files,
        FileTreeEntry const& entry,
        size_t begin,
        size_t stop,
        size_t depth
    ) -> size_t {
        for (size_t at = begin; at < stop;) {
            if (files[at].depth != depth) {
                at += 1u;
                continue;
            }
            if (tree_file_less(entry, files[at])) {
                return at;
            }
            at = tree_file_subtree_end(files, at);
        }
        return stop;
    }

    auto move_tree_file_range(Vec<FileTreeEntry>& files, size_t begin, size_t end, size_t target)
        -> void {
        DEBUG_ASSERT(begin <= end && end <= files.size() && target <= files.size());
        if (target >= begin && target <= end) {
            return;
        }
        if (target < begin) {
            std::rotate(files.data() + target, files.data() + begin, files.data() + end);
            return;
        }
        std::rotate(files.data() + begin, files.data() + end, files.data() + target);
    }

    [[nodiscard]] auto
    insert_tree_file(Vec<FileTreeEntry>& files, size_t index, FileTreeEntry entry) -> bool {
        size_t const old_size = files.size();
        if (index > old_size || !files.resize(old_size + 1u)) {
            return false;
        }
        if (index < old_size) {
            std::memmove(
                files.data() + index + 1u, files.data() + index, (old_size - index) * sizeof(entry)
            );
        }
        files[index] = entry;
        return true;
    }

    auto remove_tree_file_range(Vec<FileTreeEntry>& files, size_t begin, size_t end) -> void {
        DEBUG_ASSERT(begin <= end && end <= files.size());

        size_t const count = end - begin;
        if (count == 0u) {
            return;
        }

        size_t const trailing_count = files.size() - end;
        if (trailing_count != 0u) {
            std::memmove(
                files.data() + begin, files.data() + end, trailing_count * sizeof(FileTreeEntry)
            );
        }
        BASE_UNUSED(files.resize(files.size() - count));
    }

    [[nodiscard]] auto tree_path_matches_or_contains(StrRef path, StrRef prefix) -> bool {
        prefix = path_without_trailing_slash(prefix);
        if (prefix.empty() || path.size() < prefix.size() ||
            !path.starts_with_ignore_ascii_case(prefix)) {
            return false;
        }
        return path.size() == prefix.size() || path[prefix.size()] == '\\' ||
               path[prefix.size()] == '/';
    }

    [[nodiscard]] auto path_relative_to_root(StrRef root, StrRef path) -> StrRef {
        root = path_without_trailing_slash(root);
        path = path_without_trailing_slash(path);
        if (path.size() <= root.size() || !path.starts_with_ignore_ascii_case(root) ||
            (path[root.size()] != '\\' && path[root.size()] != '/')) {
            return {};
        }
        return path.substr(root.size() + 1u);
    }

    [[nodiscard]] auto
    replace_path_prefix_cstr(Arena& arena, StrRef path, StrRef old_prefix, StrRef new_prefix)
        -> StrRef {
        old_prefix = path_without_trailing_slash(old_prefix);
        new_prefix = path_without_trailing_slash(new_prefix);
        if (!tree_path_matches_or_contains(path, old_prefix)) {
            return {};
        }

        StrRef const suffix =
            path.size() == old_prefix.size() ? StrRef{} : path.substr(old_prefix.size());
        char buffer[MAX_PATH * 4] = {};
        size_t size = 0u;
        if (!append_buffer(buffer, sizeof(buffer), size, new_prefix) ||
            !append_buffer(buffer, sizeof(buffer), size, suffix)) {
            return {};
        }
        return arena_copy_cstr(arena, StrRef(buffer, size));
    }

    [[nodiscard]] auto remove_launch_tree_path(LaunchDesc& launch, StrRef path) -> bool {
        size_t const index = tree_file_index_by_path(launch.tree_files.slice(), path);
        if (index >= launch.tree_files.size()) {
            return false;
        }

        size_t const end = tree_file_subtree_end(launch.tree_files.slice(), index);
        remove_tree_file_range(launch.tree_files, index, end);
        mark_file_search_tracked_files(launch.save_root_path, launch.tree_files);
        return true;
    }

    [[nodiscard]] auto insert_launch_tree_path(LaunchDesc& launch, StrRef path, bool directory)
        -> bool {
        path = path_without_trailing_slash(path);
        StrRef const relative_path = path_relative_to_root(launch.save_root_path, path);
        StrRef const name = path_leaf(path);
        if (relative_path.empty() || name.empty()) {
            return false;
        }

        size_t depth = 0u;
        size_t begin = 0u;
        size_t stop = launch.tree_files.size();
        StrRef const parent_path = path_parent(path);
        if (!parent_path.equals_ignore_ascii_case(
                path_without_trailing_slash(launch.save_root_path)
            )) {
            size_t const parent = tree_file_index_by_path(launch.tree_files.slice(), parent_path);
            if (parent >= launch.tree_files.size() || !launch.tree_files[parent].is_directory) {
                return false;
            }
            depth = launch.tree_files[parent].depth + 1u;
            begin = parent + 1u;
            stop = tree_file_subtree_end(launch.tree_files.slice(), parent);
        }

        Arena& arena = launch.tree_arenas[launch.tree_arena_index];
        FileTreeEntry const entry = {
            .name = arena_copy_cstr(arena, name),
            .path = arena_copy_cstr(arena, path),
            .relative_path = arena_copy_cstr(arena, relative_path),
            .depth = depth,
            .is_directory = directory,
        };
        size_t const index =
            tree_file_child_insert_index(launch.tree_files.slice(), entry, begin, stop, depth);
        if (!insert_tree_file(launch.tree_files, index, entry)) {
            return false;
        }
        mark_file_search_tracked_files(launch.save_root_path, launch.tree_files);
        return true;
    }

    [[nodiscard]] auto
    rename_launch_tree_path(LaunchDesc& launch, StrRef source_path, StrRef target_path) -> bool {
        size_t const index = tree_file_index_by_path(launch.tree_files.slice(), source_path);
        if (index >= launch.tree_files.size()) {
            return false;
        }

        source_path = path_without_trailing_slash(source_path);
        target_path = path_without_trailing_slash(target_path);
        StrRef const target_relative = path_relative_to_root(launch.save_root_path, target_path);
        StrRef const target_name = path_leaf(target_path);
        if (target_relative.empty() || target_name.empty()) {
            return false;
        }

        Arena& arena = launch.tree_arenas[launch.tree_arena_index];
        size_t const end = tree_file_subtree_end(launch.tree_files.slice(), index);
        StrRef const source_relative = launch.tree_files[index].relative_path;
        for (size_t at = index; at < end; ++at) {
            FileTreeEntry& file = launch.tree_files[at];
            file.path = replace_path_prefix_cstr(arena, file.path, source_path, target_path);
            file.relative_path = replace_path_prefix_cstr(
                arena, file.relative_path, source_relative, target_relative
            );
            if (file.path.empty() || file.relative_path.empty()) {
                return false;
            }
        }

        launch.tree_files[index].name = arena_copy_cstr(arena, target_name);
        size_t const target = tree_file_sorted_insert_index(launch.tree_files.slice(), index, end);
        move_tree_file_range(launch.tree_files, index, end, target);
        mark_file_search_tracked_files(launch.save_root_path, launch.tree_files);
        return true;
    }

    auto mark_launch_tree_changed_from_operation(LaunchDesc& launch) -> void {
        launch.file_change_generation += 1u;
        launch.file_change_ticks = 0u;
        launch.tree_change_pending = false;
        launch.ignore_tree_change_ticks = GetTickCount64() + TREE_OPERATION_WATCHER_IGNORE_MS;
        if (launch.tree_fetch_pending) {
            launch.tree_fetch_refresh_requested = true;
        }
    }

    [[nodiscard]] auto tree_operation_targets_root(LaunchDesc const& launch, StrRef path) -> bool {
        return !launch.save_root_path.empty() &&
               path.equals_ignore_ascii_case(path_without_trailing_slash(launch.save_root_path));
    }

    [[nodiscard]] auto create_empty_tree_path(StrRef path, bool directory) -> bool {
        if (directory) {
            return CreateDirectoryA(path.data(), nullptr) != 0;
        }
        return write_file_bytes(path, {});
    }

    auto refresh_launch_tree_from_operation(LaunchDesc& launch) -> void {
        refresh_launch_tree(launch);
    }

    auto remove_launch_tree_path_from_operation(LaunchDesc& launch, StrRef path) -> void {
        if (remove_launch_tree_path(launch, path)) {
            mark_launch_tree_changed_from_operation(launch);
            return;
        }
        refresh_launch_tree_from_operation(launch);
    }

    auto insert_launch_tree_path_from_operation(LaunchDesc& launch, StrRef path, bool directory)
        -> void {
        if (insert_launch_tree_path(launch, path, directory)) {
            mark_launch_tree_changed_from_operation(launch);
            return;
        }
        refresh_launch_tree_from_operation(launch);
    }

    auto rename_launch_tree_path_from_operation(
        LaunchDesc& launch, StrRef source_path, StrRef target_path
    ) -> void {
        if (rename_launch_tree_path(launch, source_path, target_path)) {
            mark_launch_tree_changed_from_operation(launch);
            return;
        }
        refresh_launch_tree_from_operation(launch);
    }

    [[nodiscard]] auto apply_tree_create(
        LaunchDesc& launch, TreeOperationKind kind, StrRef path, TreeOperationKind request_kind
    ) -> bool {
        if (path.empty() || host_path_exists(path)) {
            return false;
        }
        bool const directory = kind == TreeOperationKind::CREATE_DIRECTORY;
        if (!create_empty_tree_path(path, directory)) {
            return false;
        }

        LaunchDesc::TreeSnapshotEntry snapshot_entry = {.is_directory = directory};
        push_tree_history_entry(launch, kind, path, {}, {&snapshot_entry, 1u});
        set_tree_operation_result(
            launch,
            launch.tree_operation_request.generation,
            request_kind,
            TreeOperationUpdateKind::CREATE,
            true,
            path
        );
        insert_launch_tree_path_from_operation(launch, path, directory);
        return true;
    }

    [[nodiscard]] auto apply_tree_rename(
        LaunchDesc& launch, StrRef source_path, StrRef target_path, TreeOperationKind request_kind
    ) -> bool {
        if (source_path.empty() || target_path.empty() ||
            source_path.equals_ignore_ascii_case(target_path) || !host_path_exists(source_path) ||
            host_path_exists(target_path) || tree_operation_targets_root(launch, source_path)) {
            return false;
        }
        if (MoveFileExA(source_path.data(), target_path.data(), MOVEFILE_WRITE_THROUGH) == 0) {
            return false;
        }

        push_tree_history_entry(launch, TreeOperationKind::RENAME, source_path, target_path);
        set_tree_operation_result(
            launch,
            launch.tree_operation_request.generation,
            request_kind,
            TreeOperationUpdateKind::RENAME,
            true,
            source_path,
            target_path
        );
        rename_launch_tree_path_from_operation(launch, source_path, target_path);
        return true;
    }

    [[nodiscard]] auto
    apply_tree_delete(LaunchDesc& launch, StrRef path, TreeOperationKind request_kind) -> bool {
        if (path.empty() || !host_path_exists(path) || tree_operation_targets_root(launch, path)) {
            return false;
        }

        Vec<LaunchDesc::TreeSnapshotEntry> snapshot = {};
        if (!snapshot.init(8u, launch.tree_history_arena.resource()) ||
            !collect_tree_snapshot(launch, snapshot, path, {}) ||
            !delete_tree_path_recursive(path)) {
            return false;
        }

        push_tree_history_entry(launch, TreeOperationKind::REMOVE, path, {}, snapshot.slice());
        set_tree_operation_result(
            launch,
            launch.tree_operation_request.generation,
            request_kind,
            TreeOperationUpdateKind::REMOVE,
            true,
            path
        );
        remove_launch_tree_path_from_operation(launch, path);
        return true;
    }

    [[nodiscard]] auto apply_tree_undo(LaunchDesc& launch) -> bool {
        LaunchDesc::TreeHistoryEntry* const entry =
            move_tree_history_entry(launch.tree_undo_stack, launch.tree_redo_stack);
        if (entry == nullptr) {
            return false;
        }

        bool ok = false;
        TreeOperationUpdateKind update_kind = TreeOperationUpdateKind::NONE;
        StrRef source_path = {};
        StrRef target_path = {};
        switch (entry->kind) {
        case TreeOperationKind::RENAME:
            ok = MoveFileExA(
                     entry->target_path.data(), entry->source_path.data(), MOVEFILE_WRITE_THROUGH
                 ) != 0;
            update_kind = TreeOperationUpdateKind::RENAME;
            source_path = entry->target_path;
            target_path = entry->source_path;
            break;
        case TreeOperationKind::CREATE_FILE:
        case TreeOperationKind::CREATE_DIRECTORY:
            ok = delete_tree_path_recursive(entry->source_path);
            update_kind = TreeOperationUpdateKind::REMOVE;
            source_path = entry->source_path;
            break;
        case TreeOperationKind::REMOVE:
            ok = restore_tree_snapshot(entry->source_path, entry->snapshot);
            update_kind = TreeOperationUpdateKind::RESTORE;
            source_path = entry->source_path;
            break;
        default:
            break;
        }

        if (!ok) {
            BASE_UNUSED(move_tree_history_entry(launch.tree_redo_stack, launch.tree_undo_stack));
            return false;
        }

        set_tree_operation_result(
            launch,
            launch.tree_operation_request.generation,
            TreeOperationKind::UNDO,
            update_kind,
            true,
            source_path,
            target_path
        );
        if (update_kind == TreeOperationUpdateKind::RENAME) {
            rename_launch_tree_path_from_operation(launch, source_path, target_path);
        } else if (update_kind == TreeOperationUpdateKind::REMOVE) {
            remove_launch_tree_path_from_operation(launch, source_path);
        } else {
            refresh_launch_tree_from_operation(launch);
        }
        return true;
    }

    [[nodiscard]] auto apply_tree_redo(LaunchDesc& launch) -> bool {
        LaunchDesc::TreeHistoryEntry* const entry =
            move_tree_history_entry(launch.tree_redo_stack, launch.tree_undo_stack);
        if (entry == nullptr) {
            return false;
        }

        bool ok = false;
        TreeOperationUpdateKind update_kind = TreeOperationUpdateKind::NONE;
        StrRef source_path = {};
        StrRef target_path = {};
        switch (entry->kind) {
        case TreeOperationKind::RENAME:
            ok = MoveFileExA(
                     entry->source_path.data(), entry->target_path.data(), MOVEFILE_WRITE_THROUGH
                 ) != 0;
            update_kind = TreeOperationUpdateKind::RENAME;
            source_path = entry->source_path;
            target_path = entry->target_path;
            break;
        case TreeOperationKind::CREATE_FILE:
        case TreeOperationKind::CREATE_DIRECTORY:
            ok = restore_tree_snapshot(entry->source_path, entry->snapshot);
            update_kind = TreeOperationUpdateKind::CREATE;
            source_path = entry->source_path;
            break;
        case TreeOperationKind::REMOVE:
            ok = delete_tree_path_recursive(entry->source_path);
            update_kind = TreeOperationUpdateKind::REMOVE;
            source_path = entry->source_path;
            break;
        default:
            break;
        }

        if (!ok) {
            BASE_UNUSED(move_tree_history_entry(launch.tree_undo_stack, launch.tree_redo_stack));
            return false;
        }

        set_tree_operation_result(
            launch,
            launch.tree_operation_request.generation,
            TreeOperationKind::REDO,
            update_kind,
            true,
            source_path,
            target_path
        );
        if (update_kind == TreeOperationUpdateKind::CREATE) {
            insert_launch_tree_path_from_operation(
                launch, source_path, entry->kind == TreeOperationKind::CREATE_DIRECTORY
            );
        } else if (update_kind == TreeOperationUpdateKind::RENAME) {
            rename_launch_tree_path_from_operation(launch, source_path, target_path);
        } else if (update_kind == TreeOperationUpdateKind::REMOVE) {
            remove_launch_tree_path_from_operation(launch, source_path);
        } else {
            refresh_launch_tree_from_operation(launch);
        }
        return true;
    }

    [[nodiscard]] auto process_tree_operation_request(LaunchDesc& launch) -> bool {
        TreeOperationRequest const& request = launch.tree_operation_request;
        if (request.generation == 0u ||
            request.generation == launch.tree_operation_result.generation) {
            return false;
        }

        StrRef const source_path(request.source_path, cstr_len(request.source_path));
        StrRef const target_path(request.target_path, cstr_len(request.target_path));
        clear_tree_operation_result(launch, request.generation, request.kind);
        switch (request.kind) {
        case TreeOperationKind::RENAME:
            return apply_tree_rename(launch, source_path, target_path, request.kind);
        case TreeOperationKind::CREATE_FILE:
        case TreeOperationKind::CREATE_DIRECTORY:
            return apply_tree_create(launch, request.kind, source_path, request.kind);
        case TreeOperationKind::REMOVE:
            return apply_tree_delete(launch, source_path, request.kind);
        case TreeOperationKind::UNDO:
            return apply_tree_undo(launch);
        case TreeOperationKind::REDO:
            return apply_tree_redo(launch);
        default:
            return false;
        }
    }

    [[nodiscard]] auto process_launch_file_changes(LaunchDesc& launch) -> bool {
        uint64_t const ticks = GetTickCount64();
        bool tree_changed = false;
        if (consume_directory_change(launch.tree_watcher, tree_changed)) {
            if (tree_changed && ticks <= launch.ignore_tree_change_ticks) {
                tree_changed = false;
            }
            launch.file_change_ticks = ticks;
            launch.tree_change_pending = launch.tree_change_pending || tree_changed;
        }
        if (launch.file_change_ticks == 0u ||
            ticks - launch.file_change_ticks < HOT_RELOAD_POLL_MS) {
            return false;
        }
        tree_changed = launch.tree_change_pending;
        launch.file_change_ticks = 0u;
        launch.tree_change_pending = false;
        if (tree_changed) {
            refresh_launch_tree(launch);
        } else {
            launch.file_change_generation += 1u;
        }
        return true;
    }

    auto run_windowed(RunOptions const& options) -> int {
        Arena app_arena = {};
        app_arena.init();

        LaunchDesc launch = {};
        if (!prepare_launch(app_arena, options.initial_path, launch)) {
            return 1;
        }

        AppState app_state = {};
        app_state.window_cache_path = launch.window_cache_path;
        global_app_state = &app_state;
        if (!create_testbed_window(&app_state, launch.window_cache_path)) {
            global_app_state = nullptr;
            return 1;
        }

        render::Context render_context = {};
        render::ContextDesc context_desc = {};
        context_desc.backend = render::Backend::D3D12;
#if BASE_DEBUG
        context_desc.enable_debug_layer = true;
#endif
        render::Result result = render::create_context(app_arena, context_desc, render_context);
        if (render::result_failed(result)) {
            log_render_result("render::create_context", result);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        render::Window render_window = {};
        render::WindowDesc window_desc = {};
        window_desc.native_window = app_state.hwnd;
        window_desc.size = window_client_size(app_state.hwnd);
        window_desc.buffer_count = 2u;
        window_desc.present_mode = render::PresentMode::VSYNC;
        launch.git_log_limit = git_log_limit_for_window_height(window_desc.size.height);

        result = render::create_window(app_arena, render_context, window_desc, render_window);
        if (render::result_failed(result)) {
            log_render_result("render::create_window", result);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        bool const watching_files =
            open_directory_watcher(launch.save_root_path, launch.tree_watcher);
        LspClient lsp_client = {};
        bool const lsp_initialized = lsp_client_init(lsp_client);
        if (lsp_initialized) {
            BASE_UNUSED(
                lsp_client_start(lsp_client, launch.save_root_path, CODE_EDITOR_SOURCE_DIR)
            );
        }
#if BASE_DEBUG
        gui::HotReloadOverlay hot_reload_overlay = {};
        gui::HotReloadOverlayState hot_reload_overlay_state = {};
        bool const hot_reload_overlay_ready = gui::create_hot_reload_overlay(
            app_arena, render_context, app_state.hwnd, &hot_reload_overlay
        );
#endif
        ModuleRuntimeContext module_context = {
            .render_context = render_context,
            .native_window = app_state.hwnd,
            .initial_text = launch.initial_text,
            .initial_file_name = launch.initial_file_name,
            .initial_file_path = launch.initial_file_path,
            .tree_root_name = launch.tree_root_name,
            .save_root_path = launch.save_root_path,
            .tree_files = launch.tree_files.slice(),
            .shared_file_drop_request = &app_state.file_drop_request,
            .shared_tree_operation_request = &launch.tree_operation_request,
            .shared_tree_operation_result = &launch.tree_operation_result,
            .lsp_bridge = lsp_initialized ? lsp_client_bridge(lsp_client) : nullptr,
            .lsp_send_request = lsp_initialized ? lsp_client_send_editor_request : nullptr,
            .lsp_user_data = lsp_initialized ? &lsp_client : nullptr,
            .app_close_requested = &app_state.close_requested,
            .app_close_confirmed = &app_state.close_confirmed,
            .initial_sidebar_visible = launch.initial_sidebar_visible,
        };
        module_context.shared_tree_root_name = &module_context.tree_root_name;
        module_context.shared_tree_files = &module_context.tree_files;
        module_context.shared_tree_loading = &launch.tree_loading;
        module_context.shared_file_change_generation =
            watching_files ? &launch.file_change_generation : nullptr;
        module_context.shared_git_requests = &launch.worker.git_requests;
        module_context.shared_git_results = &launch.worker.git_results;
        gui::HotReloadDesc module_desc = hot_reload_desc(&module_context);
        gui::HotReloadAppModule module = {};
        gui::init_hot_reload_app_module(&module, module_desc, app_arena);
#if BASE_DEBUG
        bool const module_loaded = gui::load_hot_reload_app_module(&module, module_desc, nullptr);
#else
        bool const module_loaded =
            gui::load_hot_reload_app_module(&module, module_desc, code_editor_module_api());
#endif
        if (!module_loaded) {
            gui::destroy_hot_reload_app_module(&module, module_desc);
#if BASE_DEBUG
            if (hot_reload_overlay_ready) {
                gui::destroy_hot_reload_overlay(render_context, &hot_reload_overlay);
            }
#endif
            if (lsp_initialized) {
                lsp_client_shutdown(lsp_client);
            }
            close_directory_watcher(launch.tree_watcher);
            render::destroy_window(render_window);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        if (!create_background_worker(app_arena, launch.worker)) {
            gui::destroy_hot_reload_app_module(&module, module_desc);
#if BASE_DEBUG
            if (hot_reload_overlay_ready) {
                gui::destroy_hot_reload_overlay(render_context, &hot_reload_overlay);
            }
#endif
            if (lsp_initialized) {
                lsp_client_shutdown(lsp_client);
            }
            close_directory_watcher(launch.tree_watcher);
            render::destroy_window(render_window);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }
        if (launch.initial_tree_refresh) {
            BASE_UNUSED(request_launch_tree_refresh(launch));
        }

        uint64_t previous_ticks = GetTickCount64();
        while (app_state.running) {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    app_state.running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!app_state.running) {
                break;
            }

            if (app_state.resize_pending) {
                result =
                    render::resize_window(render_context, render_window, app_state.pending_size);
                if (render::result_failed(result)) {
                    log_render_result("render::resize_window", result);
                    break;
                }
                app_state.resize_pending = false;
                app_state.redraw_pending = true;
            }
            launch.git_log_limit =
                git_log_limit_for_window_height(render::window_size(render_window).height);

            if (gui::update_hot_reload_app_module(&module, module_desc)) {
                app_state.last_frame = {};
                app_state.mouse_hit_id = {};
                app_state.redraw_pending = true;
            }
#if BASE_DEBUG
            gui::HotReloadStatus reload_overlay = {};
            bool hot_reload_overlay_visible = false;
            if (hot_reload_overlay_ready) {
                reload_overlay = gui::hot_reload_app_module_status(module);
                bool const module_overlay_visible =
                    gui::hot_reload_app_module_status_visible(module);
                if (gui::update_hot_reload_overlay_state(
                        &hot_reload_overlay_state,
                        reload_overlay,
                        module_overlay_visible,
                        render::window_size(render_window),
                        &app_state.input
                    )) {
                    app_state.redraw_pending = true;
                }
                hot_reload_overlay_visible = hot_reload_overlay_state.visible;
            }
#endif
            if (sync_launch_tree_fetch_results(launch)) {
                module_context.tree_root_name = launch.tree_root_name;
                module_context.tree_files = launch.tree_files.slice();
                app_state.redraw_pending = true;
            }
            uint64_t const tree_operation_generation_before =
                launch.tree_operation_result.generation;
            BASE_UNUSED(process_tree_operation_request(launch));
            if (launch.tree_operation_result.generation != tree_operation_generation_before) {
                module_context.tree_root_name = launch.tree_root_name;
                module_context.tree_files = launch.tree_files.slice();
                app_state.redraw_pending = true;
            }
            if (process_launch_file_changes(launch)) {
                module_context.tree_root_name = launch.tree_root_name;
                module_context.tree_files = launch.tree_files.slice();
                app_state.redraw_pending = true;
            }
            uint64_t const lsp_generation_before =
                lsp_initialized ? lsp_generation_sum(lsp_client_bridge(lsp_client)) : 0u;
            if (lsp_initialized) {
                lsp_client_poll(lsp_client);
            }
            if (lsp_initialized &&
                lsp_generation_sum(lsp_client_bridge(lsp_client)) != lsp_generation_before) {
                app_state.redraw_pending = true;
            }

            if (!app_state.redraw_pending) {
                HANDLE wait_handles[4] = {};
                DWORD wait_handle_count = 0u;
                if (watcher_valid(launch.tree_watcher)) {
                    wait_handles[wait_handle_count] = launch.tree_watcher.event;
                    wait_handle_count += 1u;
                }
                if (lsp_initialized) {
                    if (lsp_client.stdin_io.pending) {
                        wait_handles[wait_handle_count] = lsp_client.stdin_io.event;
                        wait_handle_count += 1u;
                    }
                    if (lsp_client.stdout_io.pending) {
                        wait_handles[wait_handle_count] = lsp_client.stdout_io.event;
                        wait_handle_count += 1u;
                    }
                    if (lsp_client.stderr_io.pending) {
                        wait_handles[wait_handle_count] = lsp_client.stderr_io.event;
                        wait_handle_count += 1u;
                    }
                }
                DWORD const wait_ms = HOT_RELOAD_POLL_MS;
                DWORD const wait_result = MsgWaitForMultipleObjectsEx(
                    wait_handle_count,
                    wait_handle_count != 0u ? wait_handles : nullptr,
                    wait_ms,
                    QS_ALLINPUT,
                    MWMO_INPUTAVAILABLE
                );
                if (wait_result >= WAIT_OBJECT_0 &&
                    wait_result < WAIT_OBJECT_0 + wait_handle_count) {
                    continue;
                }
                if (wait_result == WAIT_TIMEOUT) {
                    app_state.redraw_pending = true;
                }
                continue;
            }

            uint64_t const ticks = GetTickCount64();
            float const delta_time = static_cast<float>(ticks - previous_ticks) * 0.001f;
            previous_ticks = ticks;
            app_state.redraw_pending = false;
            app_state.input.key_mods = current_key_mods();
            gui::InputState module_input = app_state.input;
#if BASE_DEBUG
            if (hot_reload_overlay_ready) {
                gui::build_hot_reload_overlay_commands(
                    &hot_reload_overlay,
                    render::window_size(render_window),
                    reload_overlay,
                    &hot_reload_overlay_state,
                    app_state.input,
                    delta_time
                );
                if (hot_reload_overlay_state.capture_input) {
                    module_input.scroll_delta_y = 0.0f;
                    module_input.mouse_down[0u] = false;
                    module_input.mouse_down[1u] = false;
                    module_input.mouse_double_clicked[0u] = false;
                    module_input.mouse_triple_clicked[0u] = false;
                    module_input.key_event_count = 0u;
                }
            }
#endif

            FrameResult const frame_result = gui::hot_reload_app_module_api(module)->render_frame(
                gui::hot_reload_app_module_storage(module),
                render_context,
                render_window,
                render::window_size(render_window),
                module_input,
                delta_time
            );
            app_state.last_frame = frame_result.frame;
            app_state.mouse_hit_id = frame_result.mouse_hit_id;
#if BASE_DEBUG
            write_automation_frame_dump(options.automation_dump_frame, app_state.last_frame);
#endif
            if (render::result_failed(frame_result.render_result)) {
                log_render_result("draw::render_commands_to_window", frame_result.render_result);
                break;
            }

#if BASE_DEBUG
            if (hot_reload_overlay_ready && hot_reload_overlay_visible) {
                result = gui::render_hot_reload_overlay(
                    &hot_reload_overlay, render_context, render_window
                );
                if (render::result_failed(result)) {
                    log_render_result("render_hot_reload_overlay", result);
                    break;
                }
            }
#endif

            result = render::present_window(render_context, render_window);
            app_state.redraw_pending = frame_result.redraw_pending;
            app_state.input.scroll_delta_y = 0.0f;
            app_state.input.mouse_double_clicked[0u] = false;
            app_state.input.mouse_triple_clicked[0u] = false;
            app_state.input.key_events = app_state.key_events;
            app_state.input.key_event_count = 0u;
            if (result == render::Result::OCCLUDED) {
                Sleep(16u);
            } else if (render::result_failed(result)) {
                log_render_result("render::present_window", result);
                break;
            }
        }

        destroy_background_worker(launch.worker);
        close_directory_watcher(launch.tree_watcher);
        gui::destroy_hot_reload_app_module(&module, module_desc);
#if BASE_DEBUG
        if (hot_reload_overlay_ready) {
            gui::destroy_hot_reload_overlay(render_context, &hot_reload_overlay);
        }
#endif
        if (lsp_initialized) {
            lsp_client_shutdown(lsp_client);
        }
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        save_window_rect(app_state.hwnd, launch.window_cache_path);
        destroy_testbed_window(&app_state);
        global_app_state = nullptr;
        return 0;
    }
#endif

} // namespace code_editor

auto main(int argc, char** argv) -> int {
    base::install_crash_handlers();

#if defined(_WIN32)
    code_editor::enable_process_dpi_awareness();

    code_editor::RunOptions options = {};
    if (!code_editor::parse_run_options(argc, argv, &options)) {
        shutdown_thread_temp_arenas();
        return 2;
    }
    int const result = code_editor::run_windowed(options);
#else
    BASE_UNUSED(argc);
    BASE_UNUSED(argv);
    int const result = code_editor::run_console_fallback();
#endif
    shutdown_thread_temp_arenas();
    return result;
}
