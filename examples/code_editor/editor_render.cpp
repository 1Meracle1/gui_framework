#include "editor_render.h"

#include "syntax.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace code_editor {

    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;

    inline constexpr float TREE_INDENT_WIDTH = 16.0f;
    inline constexpr float TREE_ARROW_SLOT_WIDTH = 16.0f;
    inline constexpr float SIDEBAR_RESIZER_WIDTH = 10.0f;
    inline constexpr float EDITOR_SPLIT_GAP = 6.0f;
    inline constexpr float EDITOR_SPLIT_MIN_RATIO = 0.08f;
    inline constexpr float EDITOR_SPLIT_MAX_RATIO = 0.92f;
    inline constexpr float OPEN_TAB_HEIGHT = 28.0f;
    inline constexpr float OPEN_TAB_GAP = 6.0f;
    inline constexpr float OPEN_TAB_HEADER_PADDING = 6.0f;
    inline constexpr float OPEN_TAB_PADDING = 12.0f;
    inline constexpr float OPEN_TAB_CLOSE_SIZE = 18.0f;
    inline constexpr float FILE_SEARCH_ROW_HEIGHT = 27.0f;
    inline constexpr size_t FILE_SEARCH_PREVIEW_TEXT_CAPACITY = 32u * 1024u;
    inline constexpr size_t FILE_SEARCH_PREVIEW_COLUMN_LIMIT = 128u;
    inline constexpr size_t FILE_SEARCH_PREVIEW_TOKENS_PER_LINE = 64u;
    inline constexpr size_t FILE_SEARCH_PREVIEW_TOKEN_LIMIT = 384u;
    inline constexpr float COMMAND_OVERLAY_HEIGHT = 88.0f;
    inline constexpr float COMMAND_LIST_HEIGHT = 30.0f;
    inline constexpr char OVERWRITE_FILE_KEY = 'o';
    inline constexpr char RELOAD_FILE_KEY = 'r';
    inline constexpr char CLOSE_WITHOUT_SAVE_KEY = 'c';
    inline constexpr char SAVE_CHANGES_KEY = 's';
    inline constexpr char TREE_ARROW_OPEN[] = "\xEE\x9C\x8D";
    inline constexpr char TREE_ARROW_CLOSED[] = "\xEE\x9D\xAC";

    [[nodiscard]] auto sidebar_width(EditorState const& editor, float client_width) -> float {
        float const width = std::clamp(
            editor.sidebar_width_percent, SIDEBAR_MIN_WIDTH_PERCENT, SIDEBAR_MAX_WIDTH_PERCENT
        );
        return width * std::max(1.0f, client_width);
    }

    auto
    update_sidebar_resize(EditorState& editor, float client_width, gui::InputState const& input)
        -> void {
        if (!editor.flag(EditorFlag::SIDEBAR_RESIZING)) {
            return;
        }

        float const width = std::clamp(
            input.mouse_pos.x - editor.sidebar_resize_grab_x,
            client_width * SIDEBAR_MIN_WIDTH_PERCENT,
            client_width * SIDEBAR_MAX_WIDTH_PERCENT
        );
        editor.sidebar_width_percent = width / std::max(1.0f, client_width);
        editor.set_flag(EditorFlag::SIDEBAR_RESIZING, input.mouse_down[0u]);
    }

    [[nodiscard]] auto file_write_stamp(StrRef path) -> uint64_t {
        if (path.empty()) {
            return 0u;
        }
#if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExA(path.data(), GetFileExInfoStandard, &data)) {
            return 0u;
        }
        return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32u) |
               static_cast<uint64_t>(data.ftLastWriteTime.dwLowDateTime);
#else
        struct stat data = {};
        if (stat(path.data(), &data) != 0) {
            return 0u;
        }
        return static_cast<uint64_t>(data.st_mtime) ^ (static_cast<uint64_t>(data.st_size) << 32u);
#endif
    }

    [[nodiscard]] auto path_exists(StrRef path) -> bool {
        if (path.empty()) {
            return false;
        }
#if defined(_WIN32)
        return GetFileAttributesA(path.data()) != INVALID_FILE_ATTRIBUTES;
#else
        struct stat data = {};
        return stat(path.data(), &data) == 0;
#endif
    }

    [[nodiscard]] auto render_path_without_trailing_slash(StrRef path) -> StrRef {
        while (path.size() > 1u && (path.back() == '\\' || path.back() == '/') &&
               !(path.size() == 3u && path[1u] == ':')) {
            path.remove_suffix(1u);
        }
        return path;
    }

    [[nodiscard]] auto render_path_leaf(StrRef path) -> StrRef {
        path = render_path_without_trailing_slash(path);
        size_t const slash = path.find_last_of("\\/");
        if (slash == StrRef::NPOS) {
            return path;
        }
        StrRef const leaf = path.substr(slash + 1u);
        return leaf.empty() ? path : leaf;
    }

    [[nodiscard]] auto render_path_is_absolute(StrRef path) -> bool {
        return path.starts_with('/') || path.starts_with('\\') ||
               (path.size() >= 2u && path[1u] == ':');
    }

    [[nodiscard]] auto render_workspace_relative_path(StrRef root, StrRef path) -> StrRef {
        root = render_path_without_trailing_slash(root);
        if (root.empty() || path.size() <= root.size() ||
            !path.starts_with_ignore_ascii_case(root)) {
            return path;
        }
        char const separator = path[root.size()];
        return separator == '\\' || separator == '/' ? path.substr(root.size() + 1u) : path;
    }

    [[nodiscard]] auto append_path_buffer(char* buffer, size_t capacity, size_t& size, StrRef text)
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

    [[nodiscard]] auto open_tree_read_file(StrRef path) -> std::FILE* {
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

    [[nodiscard]] auto open_tree_write_file(StrRef path) -> std::FILE* {
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

    [[nodiscard]] auto read_tree_file_text(Arena& arena, StrRef path, StrRef& out_text) -> bool {
        std::FILE* const file = open_tree_read_file(path);
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

    [[nodiscard]] auto read_tree_file_display_text(Arena& arena, StrRef path, StrRef& out_text)
        -> bool {
        StrRef text = {};
        if (!read_tree_file_text(arena, path, text)) {
            return false;
        }
        out_text = editor_display_text(arena, text);
        return true;
    }

    [[nodiscard]] auto read_tree_file_preview_text(Arena& arena, StrRef path, StrRef& out_text)
        -> bool {
        std::FILE* const file = open_tree_read_file(path);
        if (file == nullptr) {
            return false;
        }
        char* const text = arena_alloc<char>(arena, FILE_SEARCH_PREVIEW_TEXT_CAPACITY);
        size_t const size = std::fread(text, 1u, FILE_SEARCH_PREVIEW_TEXT_CAPACITY, file);
        bool const ok = std::ferror(file) == 0;
        std::fclose(file);
        if (!ok) {
            return false;
        }
        out_text = editor_display_text(arena, StrRef(text, size));
        return true;
    }

    [[nodiscard]] auto write_tree_file_text(StrRef path, StrRef text) -> bool {
        std::FILE* const file = open_tree_write_file(path);
        if (file == nullptr) {
            return false;
        }
        bool const ok =
            text.empty() || std::fwrite(text.data(), 1u, text.size(), file) == text.size();
        std::fclose(file);
        return ok;
    }

    [[nodiscard]] auto editor_file_write_stamp(StrRef path) -> uint64_t {
        return file_write_stamp(path);
    }

    [[nodiscard]] auto editor_path_exists(StrRef path) -> bool {
        return path_exists(path);
    }

    [[nodiscard]] auto editor_write_text_file(StrRef path, StrRef text) -> bool {
        return write_tree_file_text(path, text);
    }

    [[nodiscard]] auto same_file(StrRef lhs_name, StrRef lhs_path, StrRef rhs_name, StrRef rhs_path)
        -> bool {
        if (!lhs_path.empty() || !rhs_path.empty()) {
            return lhs_path == rhs_path;
        }
        return lhs_name == rhs_name;
    }

    [[nodiscard]] auto find_open_file(EditorState& editor, StrRef name, StrRef path) -> OpenFile* {
        for (OpenFile& file : editor.open_files) {
            if (same_file(file.name, file.path, name, path)) {
                return &file;
            }
        }
        return nullptr;
    }

    auto set_open_file_deleted(EditorState& editor, StrRef name, StrRef path, bool deleted)
        -> void {
        OpenFile* const file = find_open_file(editor, name, path);
        if (file != nullptr) {
            file->file_deleted_on_disk = deleted;
        }
    }

    auto store_current_open_file(EditorState& editor) -> void {
        if (editor.current_file_name.empty() || editor.text.arena == nullptr) {
            return;
        }
        store_focused_open_file_view(editor);
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        OpenFile* const file =
            find_open_file(editor, editor.current_file_name, editor.current_file_path);
        if (file == nullptr) {
            return;
        }
        file->text = text_buffer_copy(editor.text, *editor.text.arena);
        file->saved_text = editor.saved_text;
        file->undo_stack = editor.undo_stack;
        file->redo_stack = editor.redo_stack;
        file->file_write_stamp = editor.file_write_stamp;
        file->cursor_line = editor.cursor_line;
        file->cursor_column = editor.cursor_column;
        file->preferred_column = editor.preferred_column;
        file->selection_anchor_line = editor.selection_anchor_line;
        file->selection_anchor_column = editor.selection_anchor_column;
        file->selection_mode = editor.selection_mode;
        file->scroll_x = editor.scroll_x;
        file->scroll_y = editor.scroll_y;
        file->text_valid = true;
        file->insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        file->selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        file->dirty = editor.flag(EditorFlag::DIRTY);
        file->external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        file->file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
    }

    auto load_open_file_buffer(EditorState& editor, OpenFile const& file) -> void {
        set_editor_text(editor, file.text);
        editor.current_file_name = file.name;
        editor.current_file_path = file.path;
        editor.saved_text = file.saved_text;
        editor.undo_stack = file.undo_stack;
        editor.redo_stack = file.redo_stack;
        editor.file_write_stamp = file.file_write_stamp;
        editor.cursor_line = file.cursor_line;
        editor.cursor_column = file.cursor_column;
        editor.preferred_column = file.preferred_column;
        editor.selection_anchor_line = file.selection_anchor_line;
        editor.selection_anchor_column = file.selection_anchor_column;
        editor.selection_mode = file.selection_mode;
        editor.scroll_x = file.scroll_x;
        editor.scroll_y = file.scroll_y;
        editor.set_flag(EditorFlag::INSERT_MODE, file.insert_mode);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, file.selection_active);
        editor.set_flag(EditorFlag::DIRTY, file.dirty);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, file.external_change_pending);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, file.file_deleted_on_disk);
        BASE_UNUSED(restore_focused_open_file_view(editor, file.name, file.path));
        remember_open_file(editor, file.name, file.path);
    }

    [[nodiscard]] auto
    open_file(EditorState& editor, StrRef name, StrRef path, bool store_current = true) -> bool {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        if (store_current) {
            store_current_open_file(editor);
        }
        if (same_file(name, path, editor.current_file_name, editor.current_file_path)) {
            touch_open_file(editor, name, path);
            return true;
        }
        if (load_shared_editor_buffer(editor, name, path)) {
            store_current_open_file(editor);
            touch_open_file(editor, name, path);
            return true;
        }
        OpenFile const* const file = find_open_file(editor, name, path);
        if (file != nullptr && file->text_valid) {
            load_open_file_buffer(editor, *file);
            touch_open_file(editor, name, path);
            return true;
        }
        if (path.empty()) {
            open_scratch_file(editor);
            return true;
        }
        save_scratch_file(editor);
        if (editor.current_file_path.empty()) {
            store_current_open_file(editor);
        }
        StrRef text = {};
        if (!read_tree_file_display_text(*editor.text.arena, path, text)) {
            fmt::eprintf("code_editor: failed to read %s\n", path);
            return false;
        }
        set_editor_text(editor, text);
        editor.current_file_name = arena_copy_cstr(*editor.arena, name);
        editor.current_file_path = arena_copy_cstr(*editor.arena, path);
        editor.file_write_stamp = file_write_stamp(path);
        editor.set_flag(EditorFlag::DIRTY, false);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        BASE_UNUSED(restore_focused_open_file_view(editor, name, path));
        remember_open_file(editor, name, path);
        store_current_open_file(editor);
        touch_open_file(editor, name, path);
        return true;
    }

    auto open_tree_file(EditorState& editor, FileTreeEntry const& file) -> void {
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    [[nodiscard]] auto editor_open_path(EditorState& editor, StrRef path) -> bool {
        StrRef name = render_path_leaf(path);
        if (name.empty()) {
            name = path;
        }
        return open_file(editor, name, path);
    }

    auto focus_code_split_for_open(EditorState& editor) -> void {
        if (editor_focused_pane_kind(editor) != EditorPaneKind::CODE) {
            focus_first_code_split(editor);
        }
    }

    [[nodiscard]] auto next_scratch_file_name(EditorState& editor) -> StrRef {
        char buffer[64] = {};
        for (size_t index = 1u;; ++index) {
            StrRef const name =
                index == 1u
                    ? SCRATCH_FILE_NAME
                    : fmt::bprintf(buffer, sizeof(buffer), "%s %zu", SCRATCH_FILE_NAME, index);
            if (find_open_file(editor, name, {}) == nullptr) {
                return arena_copy_cstr(*editor.arena, name);
            }
        }
    }

    auto open_new_scratch_file(EditorState& editor) -> void {
        if (editor.text.arena == nullptr || editor.arena == nullptr) {
            return;
        }
        focus_code_split_for_open(editor);
        store_current_open_file(editor);
        editor.current_file_name = next_scratch_file_name(editor);
        editor.current_file_path = {};
        editor.file_write_stamp = 0u;
        set_editor_text(editor, {});
        touch_open_file(editor, editor.current_file_name, {});
        store_current_open_file(editor);
    }

    auto reset_pane_text(EditorPane& pane, StrRef text, uint64_t stamp) -> void {
        text_buffer_set(pane.text, text);
        pane.undo_stack = nullptr;
        pane.redo_stack = nullptr;
        pane.saved_text = text_buffer_copy(pane.text, *pane.text.arena);
        pane.file_write_stamp = stamp;
        pane.cursor_line = 0u;
        pane.cursor_column = 0u;
        pane.preferred_column = 0u;
        pane.selection_anchor_line = 0u;
        pane.selection_anchor_column = 0u;
        pane.selection_mode = EditorSelectionMode::NONE;
        pane.scroll_x = 0.0f;
        pane.scroll_y = 0.0f;
        pane.insert_mode = false;
        pane.selection_active = false;
        pane.mouse_selecting = false;
        pane.mouse_was_down = false;
        pane.dirty = false;
        pane.external_change_pending = false;
        pane.file_deleted_on_disk = false;
    }

    auto sync_current_file_to_matching_panes(EditorState& editor) -> void {
        size_t const focused_pane = editor_focused_pane(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane) {
                continue;
            }
            EditorPane* const pane = editor.panes[index];
            if (pane == nullptr || pane->kind != EditorPaneKind::CODE ||
                !same_file(
                    editor.current_file_name,
                    editor.current_file_path,
                    pane->current_file_name,
                    pane->current_file_path
                )) {
                continue;
            }
            text_buffer_clone(editor.text, pane->text);
            pane->scratch_text = editor.scratch_text;
            pane->saved_text = editor.saved_text;
            pane->undo_stack = editor.undo_stack;
            pane->redo_stack = editor.redo_stack;
            pane->file_write_stamp = editor.file_write_stamp;
            pane->dirty = editor.flag(EditorFlag::DIRTY);
            pane->external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
            pane->file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        }
    }

    [[nodiscard]] auto reload_current_file_from_disk(EditorState& editor) -> bool {
        if (editor.current_file_path.empty() || editor.text.arena == nullptr) {
            return false;
        }
        StrRef text = {};
        if (!read_tree_file_display_text(*editor.text.arena, editor.current_file_path, text)) {
            return false;
        }
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        set_editor_text(editor, text);
        editor.file_write_stamp = stamp;
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        sync_current_file_to_matching_panes(editor);
        store_current_open_file(editor);
        return true;
    }

    [[nodiscard]] auto overwrite_current_file_to_disk(EditorState& editor) -> bool {
        if (editor.current_file_path.empty() || editor.text.arena == nullptr) {
            return false;
        }
        StrRef const text = text_buffer_copy(editor.text, *editor.text.arena);
        if (!write_tree_file_text(editor.current_file_path, text)) {
            return false;
        }
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        editor.file_write_stamp = stamp != 0u ? stamp : editor.file_write_stamp;
        mark_editor_saved(editor);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        sync_current_file_to_matching_panes(editor);
        store_current_open_file(editor);
        return true;
    }

    [[nodiscard]] auto resolve_save_path(EditorState const& editor, char* buffer, size_t capacity)
        -> StrRef {
        StrRef const input(editor.save_path_text, cstr_len(editor.save_path_text));
        if (input.empty()) {
            return {};
        }

        size_t size = 0u;
        if (render_path_is_absolute(input) || editor.save_root_path.empty()) {
            return append_path_buffer(buffer, capacity, size, input) ? StrRef(buffer, size)
                                                                     : StrRef();
        }

        StrRef const root = render_path_without_trailing_slash(editor.save_root_path);
        if (!append_path_buffer(buffer, capacity, size, root)) {
            return {};
        }
        if (!root.empty() && !root.ends_with('\\') && !root.ends_with('/')) {
            if (!append_path_buffer(buffer, capacity, size, "\\")) {
                return {};
            }
        }
        return append_path_buffer(buffer, capacity, size, input) ? StrRef(buffer, size) : StrRef();
    }

    [[nodiscard]] auto save_current_file_as(EditorState& editor, StrRef path) -> bool {
        if (path.empty() || editor.text.arena == nullptr || editor.arena == nullptr) {
            return false;
        }

        StrRef const text = text_buffer_copy(editor.text, *editor.text.arena);
        if (!write_tree_file_text(path, text)) {
            return false;
        }

        StrRef const old_name = editor.current_file_name;
        StrRef const old_path = editor.current_file_path;
        StrRef const saved_path = arena_copy_cstr(*editor.arena, path);
        StrRef saved_name = arena_copy_str(*editor.arena, render_path_leaf(saved_path));
        if (saved_name.empty()) {
            saved_name = saved_path;
        }

        editor.current_file_name = saved_name;
        editor.current_file_path = saved_path;
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        editor.file_write_stamp = stamp != 0u ? stamp : editor.file_write_stamp;
        mark_editor_saved(editor);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);

        OpenFile* const file = find_open_file(editor, old_name, old_path);
        if (file != nullptr) {
            file->name = editor.current_file_name;
            file->path = editor.current_file_path;
            file->file_deleted_on_disk = false;
        }
        sync_current_file_to_matching_panes(editor);
        store_current_open_file(editor);
        return true;
    }

    [[nodiscard]] auto save_path_from_popup(EditorState& editor) -> bool {
        char path[SAVE_PATH_TEXT_CAPACITY * 2u] = {};
        StrRef const resolved = resolve_save_path(editor, path, sizeof(path));
        if (resolved.empty()) {
            editor.save_path_error = EditorSavePathError::EMPTY;
            return false;
        }
        if (path_exists(resolved)) {
            editor.save_path_error = EditorSavePathError::EXISTS;
            return false;
        }
        OpenFile const* const open_file = find_open_file(editor, {}, resolved);
        if (open_file != nullptr &&
            !same_file(
                open_file->name, open_file->path, editor.current_file_name, editor.current_file_path
            )) {
            editor.save_path_error = EditorSavePathError::EXISTS;
            return false;
        }
        if (!save_current_file_as(editor, resolved)) {
            editor.save_path_error = EditorSavePathError::WRITE_FAILED;
            return false;
        }
        close_save_path_popup(editor);
        return true;
    }

    auto handle_editor_save_request(EditorState& editor) -> void {
        if (!editor.flag(EditorFlag::SAVE_REQUESTED)) {
            return;
        }
        editor.set_flag(EditorFlag::SAVE_REQUESTED, false);
        if (editor_focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return;
        }
        if (editor.current_file_path.empty()) {
            open_save_path_popup(editor);
            return;
        }
        if (!overwrite_current_file_to_disk(editor)) {
            fmt::eprintf("code_editor: failed to write %s\n", editor.current_file_path);
        }
    }

    auto close_current_file(EditorState& editor, bool force = false) -> void;

    auto handle_editor_write_quit_request(EditorState& editor) -> void {
        if (!editor.flag(EditorFlag::WRITE_QUIT_REQUESTED)) {
            return;
        }
        if (editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }
        if (editor_focused_pane_kind(editor) != EditorPaneKind::CODE) {
            editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);
            return;
        }
        if (editor.current_file_path.empty()) {
            open_save_path_popup(editor);
            return;
        }
        editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);
        if (!overwrite_current_file_to_disk(editor)) {
            fmt::eprintf("code_editor: failed to write %s\n", editor.current_file_path);
            return;
        }
        close_current_file(editor);
    }

    auto update_current_file_change(EditorState& editor) -> void {
        if (editor.current_file_path.empty()) {
            return;
        }
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        if (stamp == 0u) {
            if (editor.file_write_stamp != 0u) {
                editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, true);
                editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
                set_open_file_deleted(
                    editor, editor.current_file_name, editor.current_file_path, true
                );
            }
            return;
        }
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        set_open_file_deleted(editor, editor.current_file_name, editor.current_file_path, false);
        if (editor.file_write_stamp == 0u) {
            editor.file_write_stamp = stamp;
            return;
        }
        if (stamp == editor.file_write_stamp) {
            return;
        }
        if (editor.flag(EditorFlag::DIRTY)) {
            editor.file_write_stamp = stamp;
            editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, true);
            return;
        }
        BASE_UNUSED(reload_current_file_from_disk(editor));
    }

    auto update_pane_file_change(EditorState& editor, EditorPane& pane) -> void {
        if (pane.current_file_path.empty() || pane.text.arena == nullptr) {
            return;
        }
        uint64_t const stamp = file_write_stamp(pane.current_file_path);
        if (stamp == 0u) {
            if (pane.file_write_stamp != 0u) {
                pane.file_deleted_on_disk = true;
                pane.external_change_pending = false;
                set_open_file_deleted(editor, pane.current_file_name, pane.current_file_path, true);
            }
            return;
        }
        pane.file_deleted_on_disk = false;
        set_open_file_deleted(editor, pane.current_file_name, pane.current_file_path, false);
        if (pane.file_write_stamp == 0u) {
            pane.file_write_stamp = stamp;
            return;
        }
        if (stamp == pane.file_write_stamp) {
            return;
        }
        if (pane.dirty) {
            pane.file_write_stamp = stamp;
            pane.external_change_pending = true;
            return;
        }
        StrRef text = {};
        if (read_tree_file_display_text(*pane.text.arena, pane.current_file_path, text)) {
            reset_pane_text(pane, text, stamp);
            set_open_file_deleted(editor, pane.current_file_name, pane.current_file_path, false);
        }
    }

    [[nodiscard]] auto open_file_loaded(EditorState const& editor, OpenFile const& file) -> bool {
        if (same_file(file.name, file.path, editor.current_file_name, editor.current_file_path)) {
            return true;
        }
        size_t const focused_pane = editor_focused_pane(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane) {
                continue;
            }
            EditorPane const* const pane = editor.panes[index];
            if (pane != nullptr && pane->kind == EditorPaneKind::CODE &&
                same_file(file.name, file.path, pane->current_file_name, pane->current_file_path)) {
                return true;
            }
        }
        return false;
    }

    auto update_open_file_change(EditorState& editor, OpenFile& file) -> void {
        if (file.path.empty() || open_file_loaded(editor, file) || editor.arena == nullptr) {
            return;
        }
        uint64_t const stamp = file_write_stamp(file.path);
        if (stamp == 0u) {
            if (file.file_write_stamp != 0u) {
                file.file_deleted_on_disk = true;
                file.external_change_pending = false;
            }
            return;
        }
        file.file_deleted_on_disk = false;
        if (file.file_write_stamp == 0u) {
            file.file_write_stamp = stamp;
            return;
        }
        if (stamp == file.file_write_stamp) {
            return;
        }
        if (file.dirty) {
            file.file_write_stamp = stamp;
            file.external_change_pending = true;
            return;
        }
        StrRef text = {};
        if (read_tree_file_display_text(*editor.arena, file.path, text)) {
            file.text = text;
            file.saved_text = text;
            file.undo_stack = nullptr;
            file.redo_stack = nullptr;
            file.file_write_stamp = stamp;
            file.cursor_line = 0u;
            file.cursor_column = 0u;
            file.preferred_column = 0u;
            file.selection_anchor_line = 0u;
            file.selection_anchor_column = 0u;
            file.selection_mode = EditorSelectionMode::NONE;
            file.scroll_x = 0.0f;
            file.scroll_y = 0.0f;
            file.text_valid = true;
            file.insert_mode = false;
            file.selection_active = false;
            file.dirty = false;
            file.external_change_pending = false;
            file.file_deleted_on_disk = false;
        }
    }

    auto update_open_file_changes(EditorState& editor) -> void {
        update_current_file_change(editor);
        size_t const focused_pane = editor_focused_pane(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane) {
                continue;
            }
            EditorPane* const pane = editor.panes[index];
            if (pane != nullptr && pane->kind == EditorPaneKind::CODE) {
                update_pane_file_change(editor, *pane);
            }
        }
        for (OpenFile& file : editor.open_files) {
            update_open_file_change(editor, file);
        }
    }

    auto draw_token(
        draw::Context context,
        draw::TextStyle style,
        EditorLine const& line,
        size_t start,
        size_t end,
        float x,
        float y,
        float char_width
    ) -> void {
        if (end <= start) {
            return;
        }
        draw::draw_text(
            context,
            {std::round(x + char_width * static_cast<float>(start)), std::round(y)},
            style,
            StrRef(line.text + start, end - start),
            nullptr
        );
    }

    [[nodiscard]] auto syntax_token_color(Palette const& palette, SyntaxTokenKind kind)
        -> gui::Color {
        switch (kind) {
        case SyntaxTokenKind::TEXT:
            return palette.text;
        case SyntaxTokenKind::KEYWORD:
            return palette.keyword;
        case SyntaxTokenKind::TYPE:
            return palette.type;
        case SyntaxTokenKind::STRING:
            return palette.string;
        case SyntaxTokenKind::NUMBER:
            return palette.number;
        case SyntaxTokenKind::COMMENT:
            return palette.comment;
        case SyntaxTokenKind::PREPROCESSOR:
            return palette.preprocessor;
        case SyntaxTokenKind::PUNCTUATION:
            return palette.punctuation;
        case SyntaxTokenKind::FUNCTION:
            return palette.function;
        }
        return palette.text;
    }

    auto draw_syntax_line(
        draw::Context context,
        font_cache::Font font,
        SyntaxTokenizer tokenizer,
        Palette const& palette,
        EditorLine const& line,
        float x,
        float y,
        float font_size,
        font_provider::RasterPolicy raster_policy,
        float char_width
    ) -> void {
        draw::TextStyle style = {.font = font, .size = font_size, .raster_policy = raster_policy};
        StrRef const text = editor_line_text(line);
        size_t index = 0u;
        while (index < text.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, text, index);
            style.color = to_draw_color(syntax_token_color(palette, token.kind));
            draw_token(context, style, line, token.start, token.end, x, y, char_width);
            index = token.end;
        }
    }

    [[nodiscard]] auto semantic_tokens_for_editor(EditorState const& editor)
        -> Slice<LspSemanticToken const> {
        if (editor.lsp_bridge == nullptr ||
            editor.lsp_bridge->semantic_tokens_path != editor.current_file_path ||
            editor.lsp_bridge->semantic_tokens_revision != editor.text.revision) {
            return {};
        }
        return editor.lsp_bridge->semantic_tokens;
    }

    auto draw_semantic_line(
        draw::Context context,
        font_cache::Font font,
        Slice<LspSemanticToken const> tokens,
        Palette const& palette,
        EditorLine const& line,
        size_t line_index,
        float x,
        float y,
        float font_size,
        font_provider::RasterPolicy raster_policy,
        float char_width
    ) -> void {
        draw::TextStyle style = {.font = font, .size = font_size, .raster_policy = raster_policy};
        for (LspSemanticToken const& token : tokens) {
            if (token.range.start.line > line_index) {
                break;
            }
            if (token.range.start.line != line_index || token.range.end.line != line_index) {
                continue;
            }
            size_t const start = std::min(token.range.start.column, line.size);
            size_t const end = std::min(token.range.end.column, line.size);
            if (end <= start || token.kind == SyntaxTokenKind::TEXT) {
                continue;
            }
            style.color = to_draw_color(syntax_token_color(palette, token.kind));
            draw_token(context, style, line, start, end, x, y, char_width);
        }
    }

    [[nodiscard]] auto next_text_line(StrRef text, size_t& offset) -> StrRef {
        size_t const start = offset;
        while (offset < text.size() && text[offset] != '\n') {
            offset += 1u;
        }
        size_t end = offset;
        if (offset < text.size()) {
            offset += 1u;
        }
        if (end > start && text[end - 1u] == '\r') {
            end -= 1u;
        }
        return text.substr(start, end - start);
    }

    [[nodiscard]] auto search_line_match_at(StrRef line, StrRef query, size_t start) -> bool {
        if (start > line.size() || query.size() > line.size() - start) {
            return false;
        }
        for (size_t index = 0u; index < query.size(); ++index) {
            if (to_ascii_lower(line[start + index]) != to_ascii_lower(query[index])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto
    search_line_match(StrRef line, StrRef query, size_t start_column, size_t& out_start) -> bool {
        if (query.empty() || start_column > line.size() || query.size() > line.size()) {
            return false;
        }
        size_t const last_start = line.size() - query.size();
        if (start_column > last_start) {
            return false;
        }
        char const first = to_ascii_lower(query[0u]);
        for (size_t start = start_column; start <= last_start; ++start) {
            if (to_ascii_lower(line[start]) == first && search_line_match_at(line, query, start)) {
                out_start = start;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto global_search_file_text(
        EditorState& editor, Arena& arena, FileTreeEntry const& file, StrRef& out_text
    ) -> bool {
        if (same_file(file.name, file.path, editor.current_file_name, editor.current_file_path)) {
            out_text = text_buffer_copy(editor.text, arena);
            return true;
        }
        OpenFile* const open = find_open_file(editor, file.name, file.path);
        if (open != nullptr && open->text_valid) {
            out_text = open->text;
            return true;
        }
        return read_tree_file_display_text(arena, file.path, out_text);
    }

    auto refresh_global_search_results(EditorState& editor) -> void {
        editor.global_search_results.clear();
        StrRef const query = editor_file_search_text(editor);
        if (query.empty()) {
            editor.global_search_refresh_requested = false;
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        for (size_t file_index = 0u;
             file_index < editor.tree_files.size() &&
             editor.global_search_results.size() < GLOBAL_SEARCH_RESULT_LIMIT;
             ++file_index) {
            FileTreeEntry const& file = editor.tree_files[file_index];
            if (file.is_directory || !file.file_search_visible) {
                continue;
            }

            StrRef text = {};
            if (!global_search_file_text(editor, *temp.arena(), file, text)) {
                continue;
            }
            size_t offset = 0u;
            size_t line_index = 0u;
            while (offset < text.size() &&
                   editor.global_search_results.size() < GLOBAL_SEARCH_RESULT_LIMIT) {
                StrRef const line = next_text_line(text, offset);
                size_t column = 0u;
                while (search_line_match(line, query, column, column) &&
                       editor.global_search_results.size() < GLOBAL_SEARCH_RESULT_LIMIT) {
                    GlobalSearchResult result = {
                        .tree_file_index = file_index,
                        .line = line_index,
                        .column = column,
                    };
                    StrRef const line_text = line.trim();
                    result.line_text_size =
                        line_text.copy_to(result.line_text, GLOBAL_SEARCH_LINE_TEXT_CAPACITY - 1u);
                    result.line_text[result.line_text_size] = '\0';
                    bool const ok = editor.global_search_results.push_back(result);
                    DEBUG_ASSERT(ok);
                    (void)ok;
                    column += 1u;
                }
                line_index += 1u;
            }
        }
        editor.global_search_refresh_requested = false;
    }

    [[nodiscard]] auto global_search_row_text(
        EditorState const& editor, Arena& arena, GlobalSearchResult const& result
    ) -> StrRef {
        if (result.tree_file_index >= editor.tree_files.size()) {
            return {};
        }
        FileTreeEntry const& file = editor.tree_files[result.tree_file_index];
        StrRef const line(result.line_text, result.line_text_size);
        StrRef const path = file_search_entry_text(file);
        return line.empty() ? path : fmt::aprintf(arena.resource(), "%s  %s", path, line).str();
    }

    struct SourcePreviewMark {
        LspPosition cursor = {};
        LspRange selection = {};
        bool selection_active = false;
    };

    auto draw_source_preview_cursor(
        gui::Frame& ui, font_cache::Font font, Palette const& palette, float font_size
    ) -> void {
        float const width = std::max(1.0f, font_cache::text_advance(font, font_size, " "));
        ui.spacer({
            .layout = {.width = gui::px(width), .height = gui::fill()},
            .style = {.background = gui::color_alpha(palette.cursor, 0.45f)},
        });
    }

    auto draw_syntax_label_line(
        gui::Frame& ui,
        font_cache::Font font,
        SyntaxTokenizer tokenizer,
        Palette const& palette,
        StrRef line,
        float font_size,
        size_t line_index = 0u,
        SourcePreviewMark const* mark = nullptr,
        size_t column_limit = FILE_SEARCH_PREVIEW_COLUMN_LIMIT
    ) -> void {
        column_limit = std::max<size_t>(8u, column_limit);
        size_t selection_start = 0u;
        size_t selection_end = 0u;
        bool has_selection = false;
        if (mark != nullptr && mark->selection_active && line_index >= mark->selection.start.line &&
            line_index <= mark->selection.end.line) {
            selection_start =
                line_index == mark->selection.start.line ? mark->selection.start.column : 0u;
            selection_end =
                line_index == mark->selection.end.line ? mark->selection.end.column : line.size();
            selection_start = std::min(selection_start, line.size());
            selection_end = std::min(selection_end, line.size());
            has_selection = selection_start < selection_end;
        }
        bool const has_cursor = mark != nullptr && mark->cursor.line == line_index;
        size_t cursor_column =
            has_cursor ? std::min(mark->cursor.column, line.size()) : static_cast<size_t>(-1);
        if (line.empty()) {
            if (has_cursor) {
                draw_source_preview_cursor(ui, font, palette, font_size);
            } else {
                ui.spacer({.layout = {.width = gui::px(1.0f), .height = gui::fill()}});
            }
            return;
        }
        size_t const full_line_size = line.size();
        size_t visible_start = 0u;
        if ((has_selection || has_cursor) && full_line_size > column_limit) {
            size_t const target = has_selection ? selection_start : cursor_column;
            size_t const context = column_limit / 3u;
            visible_start =
                target > context ? std::min(target - context, full_line_size - column_limit) : 0u;
        }
        size_t const visible_size = std::min(column_limit, full_line_size - visible_start);
        line = line.slice(visible_start, visible_size);
        if (has_selection) {
            has_selection =
                selection_start < visible_start + visible_size && selection_end > visible_start;
            selection_start =
                has_selection ? std::max(selection_start, visible_start) - visible_start : 0u;
            selection_end = has_selection ? std::min(selection_end, visible_start + visible_size) -
                                                visible_start
                                          : 0u;
        }
        if (has_cursor) {
            cursor_column = cursor_column >= visible_start
                                ? std::min(cursor_column - visible_start, line.size())
                                : static_cast<size_t>(-1);
        }
        size_t index = 0u;
        size_t part_index = 0u;
        while (index < line.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, line, index);
            size_t start = token.start;
            while (start < token.end) {
                size_t end = token.end;
                if (has_selection && selection_start > start && selection_start < end) {
                    end = selection_start;
                }
                if (has_selection && selection_end > start && selection_end < end) {
                    end = selection_end;
                }
                if (has_cursor && cursor_column > start && cursor_column < end) {
                    end = cursor_column;
                }
                if (has_cursor && cursor_column + 1u > start && cursor_column + 1u < end) {
                    end = cursor_column + 1u;
                }
                bool const selected =
                    has_selection && start >= selection_start && start < selection_end;
                bool const cursor = has_cursor && start == cursor_column;
                gui::Color const background = cursor     ? gui::color_alpha(palette.cursor, 0.45f)
                                              : selected ? gui::color_alpha(palette.cursor, 0.28f)
                                                         : gui::Color{};
                ui.label(
                    gui::id("file_search_preview_token", part_index),
                    line.substr(start, end - start),
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .background = background,
                            .foreground = syntax_token_color(palette, token.kind),
                            .font = font,
                            .font_size = font_size,
                        },
                    }
                );
                start = end;
                part_index += 1u;
            }
            index = token.end;
        }
        if (has_cursor && cursor_column == line.size() &&
            visible_start + visible_size == full_line_size) {
            draw_source_preview_cursor(ui, font, palette, font_size);
        }
    }

    [[nodiscard]] auto lsp_same_path(StrRef lhs, StrRef rhs) -> bool {
        return lhs.equals_ignore_ascii_case(rhs);
    }

    [[nodiscard]] auto lsp_diagnostic_color(Palette const& palette, LspDiagnosticSeverity severity)
        -> gui::Color {
        switch (severity) {
        case LspDiagnosticSeverity::ERROR_DIAGNOSTIC:
            return palette.preprocessor;
        case LspDiagnosticSeverity::WARNING:
            return palette.string;
        case LspDiagnosticSeverity::INFORMATION:
            return palette.cursor;
        case LspDiagnosticSeverity::HINT:
            return palette.muted;
        }
        return palette.muted;
    }

    [[nodiscard]] auto lsp_diagnostic_count(EditorState const& editor) -> size_t {
        if (editor.lsp_bridge == nullptr) {
            return 0u;
        }
        size_t count = 0u;
        for (LspDiagnostic const& diagnostic : editor.lsp_bridge->diagnostics) {
            if (lsp_same_path(diagnostic.path, editor.current_file_path)) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto lsp_status_bar_text(EditorState const& editor) -> StrRef {
        LspBridge const* const bridge = editor.lsp_bridge;
        if (bridge == nullptr) {
            return {};
        }

        StrRef const name = bridge->server_name.empty() ? StrRef("LSP") : bridge->server_name;
        StrRef status = bridge->status_text.empty() ? StrRef("unknown") : bridge->status_text;
        if (bridge->progress_active && !bridge->progress_text.empty()) {
            status = bridge->progress_text;
        }

        size_t const diagnostics = lsp_diagnostic_count(editor);
        if (diagnostics == 0u) {
            return fmt::tprintf("%s: %s", name, status);
        }
        return fmt::tprintf("%s: %s (%zu)", name, status, diagnostics);
    }

    [[nodiscard]] auto lsp_position_equal(LspPosition lhs, LspPosition rhs) -> bool {
        return lhs.line == rhs.line && lhs.column == rhs.column;
    }

    [[nodiscard]] auto lsp_position_in_range(LspPosition position, LspRange range) -> bool {
        if (lsp_position_equal(range.start, range.end)) {
            return lsp_position_equal(position, range.start);
        }
        return !lsp_position_less(position, range.start) && lsp_position_less(position, range.end);
    }

    [[nodiscard]] auto lsp_diagnostic_at_cursor(EditorState const& editor) -> LspDiagnostic const* {
        if (editor.lsp_bridge == nullptr) {
            return nullptr;
        }
        LspPosition const cursor = {editor.cursor_line, editor.cursor_column};
        for (LspDiagnostic const& diagnostic : editor.lsp_bridge->diagnostics) {
            if (lsp_same_path(diagnostic.path, editor.current_file_path) &&
                !diagnostic.message.empty() && lsp_position_in_range(cursor, diagnostic.range)) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    auto draw_lsp_diagnostics_for_line(
        draw::Context context,
        EditorState const& editor,
        Palette const& palette,
        size_t line,
        float text_x,
        float text_min_x,
        float number_x,
        float y,
        float line_height,
        float char_width
    ) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        for (LspDiagnostic const& diagnostic : editor.lsp_bridge->diagnostics) {
            if (!lsp_same_path(diagnostic.path, editor.current_file_path) ||
                line < diagnostic.range.start.line || line > diagnostic.range.end.line) {
                continue;
            }
            size_t const line_size_value = editor_line(editor, line).size;
            size_t const start =
                line == diagnostic.range.start.line ? diagnostic.range.start.column : 0u;
            size_t const end =
                line == diagnostic.range.end.line ? diagnostic.range.end.column : line_size_value;
            gui::Color const color = lsp_diagnostic_color(palette, diagnostic.severity);
            draw::draw_rect_filled(
                context,
                {{number_x, y + 6.0f}, {number_x + 3.0f, y + line_height - 6.0f}},
                to_draw_color(color),
                1.0f
            );
            if (end > start) {
                float const x0 = text_x + char_width * static_cast<float>(start);
                float const x1 = text_x + char_width * static_cast<float>(end);
                if (x1 <= text_min_x) {
                    continue;
                }
                float const underline_y = y + line_height - 3.0f;
                draw::draw_line(
                    context,
                    {std::round(std::max(x0, text_min_x)), std::round(underline_y)},
                    {std::round(std::max(x0 + 2.0f, x1)), std::round(underline_y)},
                    to_draw_color(color),
                    1.5f
                );
            }
        }
    }

    auto draw_editor_selection(
        draw::Context context,
        EditorSelectionRange selection,
        EditorLine const& editor_line_value,
        size_t line,
        float text_x,
        float y,
        float line_height,
        float char_width,
        float max_x,
        Palette const& palette
    ) -> void {
        if (!selection.active || line < selection.start_line || line > selection.end_line) {
            return;
        }
        if (selection.full_line && selection.end_column == 0u &&
            selection.end_line > selection.start_line && line == selection.end_line) {
            return;
        }

        size_t const start = line == selection.start_line
                                 ? std::min(selection.start_column, editor_line_value.size)
                                 : 0u;
        size_t const end = line == selection.end_line
                               ? std::min(selection.end_column, editor_line_value.size)
                               : editor_line_value.size;
        bool const selects_newline = line < selection.end_line;
        if (start == end && !selects_newline && !selection.full_line) {
            return;
        }

        float const x0 =
            selection.full_line ? text_x : text_x + char_width * static_cast<float>(start);
        float const x1 =
            selection.full_line
                ? max_x
                : text_x + char_width * static_cast<float>(end + (selects_newline ? 1u : 0u));
        if (x0 >= max_x || x1 <= x0) {
            return;
        }
        draw::draw_rect_filled(
            context,
            {{std::round(x0), y}, {std::round(std::min(x1, max_x)), y + line_height}},
            to_draw_color(gui::color_alpha(palette.cursor, 0.28f)),
            0.0f
        );
    }

    [[nodiscard]] auto
    draw_cursor_column(EditorState const& editor, size_t line, size_t line_size, size_t column)
        -> size_t {
        if (editor.flag(EditorFlag::INSERT_MODE) || line_size == 0u) {
            return column;
        }
        if (column < line_size) {
            return column;
        }
        return line + 1u < editor_line_count(editor) ? line_size : line_size - 1u;
    }

    [[nodiscard]] auto editor_surface_id(size_t split) -> gui::Id {
        return gui::id("editor_surface", split);
    }

    [[nodiscard]] auto editor_split_id(size_t split) -> gui::Id {
        return gui::id("editor_split", split);
    }

    [[nodiscard]] auto editor_split_resizer_id(size_t split) -> gui::Id {
        return gui::id("editor_split_resizer", split);
    }

    [[nodiscard]] auto draw_editor_surface_rect(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Rect rect,
        gui::InputState const& input,
        Palette const& palette,
        bool apply_key_reveal,
        bool selection_visible
    ) -> bool {
        bool const scrolled = !editor.flag(EditorFlag::SIDEBAR_RESIZING) &&
                              input.scroll_delta_y != 0.0f && point_in_rect(rect, input.mouse_pos);
        bool const hovered = point_in_rect(rect, input.mouse_pos);
        bool const mouse_pressed = input.mouse_down[0u] && !editor.flag(EditorFlag::MOUSE_WAS_DOWN);
        bool const double_clicked =
            !editor.flag(EditorFlag::SIDEBAR_RESIZING) && hovered && input.mouse_double_clicked[0u];
        bool const triple_clicked =
            !editor.flag(EditorFlag::SIDEBAR_RESIZING) && hovered && input.mouse_triple_clicked[0u];
        bool const clicked = !editor.flag(EditorFlag::SIDEBAR_RESIZING) && mouse_pressed && hovered;
        bool const dragged = !editor.flag(EditorFlag::SIDEBAR_RESIZING) &&
                             editor.flag(EditorFlag::MOUSE_SELECTING) && input.mouse_down[0u];
        if (scrolled) {
            editor.scroll_y -= input.scroll_delta_y;
        }
        if (triple_clicked) {
            select_line_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (double_clicked) {
            select_word_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (clicked) {
            update_cursor_from_mouse(
                editor,
                rect,
                input.mouse_pos,
                char_width,
                (input.key_mods & gui::KEY_MOD_SHIFT) != 0u
            );
            editor.set_flag(EditorFlag::MOUSE_SELECTING, true);
        } else if (dragged) {
            update_cursor_from_mouse(editor, rect, input.mouse_pos, char_width, true);
        }
        if (!input.mouse_down[0u]) {
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        }
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, input.mouse_down[0u]);
        if (clicked || dragged || double_clicked || triple_clicked || apply_key_reveal) {
            reveal_cursor(editor, rect, char_width);
        } else {
            clamp_scroll(editor, rect);
        }

        gui::Rect const content = editor_content_rect(rect);
        draw::Rect const clip = {
            {content.min.x, content.min.y},
            {content.max.x, content.max.y},
        };
        draw::push_clip_rect(draw_context, clip);

        size_t const line_count = editor_line_count(editor);
        float const line_height = editor_line_height(editor);
        size_t const first_line =
            std::min(line_count - 1u, static_cast<size_t>(editor.scroll_y / line_height));
        float y = content.min.y - (editor.scroll_y - static_cast<float>(first_line) * line_height);
        float const text_x = editor_text_x(editor, rect);
        float const text_min_x = std::min(text_x + editor.scroll_x, content.max.x);
        draw::Rect const text_clip = {{text_min_x, content.min.y}, {content.max.x, content.max.y}};
        float const line_number_x = content.min.x;
        EditorSelectionRange const selection = editor_selection_range(editor);
        SyntaxTokenizer const tokenizer = syntax_tokenizer_for_file_name(editor.current_file_name);
        Slice<LspSemanticToken const> const semantic_tokens = semantic_tokens_for_editor(editor);
        size_t line = first_line;
        while (line < line_count && y < content.max.y) {
            if (selection_visible && line == editor.cursor_line) {
                draw::draw_rect_filled(
                    draw_context,
                    {{content.min.x, y}, {content.max.x, y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor_line, 0.72f)),
                    0.0f
                );
            }

            EditorLine const& text_line = editor_line(editor, line);
            draw::TextStyle number_style = {
                .font = editor_font,
                .size = editor.font_size,
                .raster_policy = editor.raster_policy,
                .color = to_draw_color(
                    selection_visible && line == editor.cursor_line ? palette.text : palette.faint
                ),
            };
            draw::draw_text(
                draw_context,
                {std::round(line_number_x), std::round(y - 2.0f)},
                number_style,
                fmt::tprintf("%4zu", line + 1u),
                nullptr
            );

            draw::push_clip_rect(draw_context, text_clip);
            if (selection_visible) {
                draw_editor_selection(
                    draw_context,
                    selection,
                    text_line,
                    line,
                    text_x,
                    y,
                    line_height,
                    char_width,
                    content.max.x,
                    palette
                );
            }
            if (selection_visible && !editor.flag(EditorFlag::INSERT_MODE) &&
                line == editor.cursor_line) {
                size_t const cursor_column =
                    draw_cursor_column(editor, line, text_line.size, editor.cursor_column);
                float const cursor_x0 =
                    std::round(text_x + char_width * static_cast<float>(cursor_column));
                float const cursor_x1 =
                    std::round(text_x + char_width * static_cast<float>(cursor_column + 1u));
                draw::draw_rect_filled(
                    draw_context,
                    {{cursor_x0, y}, {std::max(cursor_x0 + 1.0f, cursor_x1), y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor, 0.45f)),
                    2.0f
                );
            }
            draw_syntax_line(
                draw_context,
                editor_font,
                tokenizer,
                palette,
                text_line,
                text_x,
                y - 2.0f,
                editor.font_size,
                editor.raster_policy,
                char_width
            );
            draw_semantic_line(
                draw_context,
                editor_font,
                semantic_tokens,
                palette,
                text_line,
                line,
                text_x,
                y - 2.0f,
                editor.font_size,
                editor.raster_policy,
                char_width
            );
            draw::pop_clip_rect(draw_context);
            draw_lsp_diagnostics_for_line(
                draw_context,
                editor,
                palette,
                line,
                text_x,
                text_min_x,
                line_number_x,
                y,
                line_height,
                char_width
            );
            y += line_height;
            line += 1u;
        }

        float const cursor_x =
            std::round(text_x + char_width * static_cast<float>(editor.cursor_column));
        float const cursor_y =
            content.min.y + static_cast<float>(editor.cursor_line) * line_height - editor.scroll_y;
        if (selection_visible && editor.flag(EditorFlag::INSERT_MODE) &&
            cursor_y + line_height >= content.min.y && cursor_y < content.max.y) {
            draw::push_clip_rect(draw_context, text_clip);
            draw::draw_rect_filled(
                draw_context,
                {{cursor_x, std::round(cursor_y + 2.0f)},
                 {cursor_x + 2.0f, std::round(cursor_y + line_height - 2.0f)}},
                to_draw_color(palette.mode_insert),
                0.0f
            );
            draw::pop_clip_rect(draw_context);
        }

        draw::pop_clip_rect(draw_context);
        return clicked || double_clicked || triple_clicked;
    }

    auto draw_editor_split_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette,
        size_t split,
        size_t initial_focus,
        size_t& target_focus,
        bool selection_visible
    ) -> void {
        if (split >= editor.split_nodes.size()) {
            return;
        }

        EditorSplitNode const node = editor.split_nodes[split];
        if (node.kind != EditorSplitKind::LEAF) {
            gui::BoxKind const box_kind =
                node.kind == EditorSplitKind::VERTICAL ? gui::BoxKind::ROW : gui::BoxKind::COLUMN;
            gui::BoxInfo const* const box = ui.find_box(editor_split_id(split), box_kind);
            if (box != nullptr) {
                set_editor_split_rect(editor, split, box->rect);
            }
            draw_editor_split_surface(
                draw_context,
                editor_font,
                editor,
                char_width,
                ui,
                input,
                palette,
                node.first,
                initial_focus,
                target_focus,
                selection_visible
            );
            draw_editor_split_surface(
                draw_context,
                editor_font,
                editor,
                char_width,
                ui,
                input,
                palette,
                node.second,
                initial_focus,
                target_focus,
                selection_visible
            );
            return;
        }
        if (editor_split_pane_kind(editor, split) == EditorPaneKind::FILESYSTEM) {
            gui::BoxInfo const* const box =
                ui.find_box(editor_surface_id(split), gui::BoxKind::SCROLL_PANEL);
            if (box != nullptr) {
                set_editor_split_rect(editor, split, box->rect);
            }
            return;
        }

        gui::BoxInfo const* const box = ui.find_box(editor_surface_id(split), gui::BoxKind::ROW);
        if (box == nullptr) {
            return;
        }

        set_editor_split_rect(editor, split, box->rect);
        focus_editor_split(editor, split);
        bool const popup_open = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING) ||
                                editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        gui::InputState const surface_input = popup_open ? gui::InputState{} : input;
        bool const clicked = draw_editor_surface_rect(
            draw_context,
            editor_font,
            editor,
            char_width,
            box->rect,
            surface_input,
            palette,
            !popup_open && split == initial_focus && input.key_event_count != 0u,
            selection_visible
        );
        if (clicked) {
            target_focus = split;
        }
    }

    auto draw_command_overlay(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState const& editor,
        gui::Frame const& ui,
        Palette const& palette
    ) -> void {
        if (!editor.flag(EditorFlag::COMMAND_LINE_ACTIVE)) {
            return;
        }

        gui::BoxInfo const* const body = ui.find_box(gui::id("body"), gui::BoxKind::ROW);
        if (body == nullptr) {
            return;
        }

        float const pad = 8.0f;
        float const y0 =
            std::max(body->rect.min.y + pad, body->rect.max.y - COMMAND_OVERLAY_HEIGHT - pad);
        draw::Rect const panel = {
            {body->rect.min.x + pad, y0},
            {body->rect.max.x - pad, body->rect.max.y - pad},
        };
        draw::draw_rect_filled(draw_context, panel, to_draw_color(palette.panel), 6.0f);
        draw::draw_rect(draw_context, panel, to_draw_color(palette.border), 1.0f, 6.0f);
        draw::push_clip_rect(draw_context, panel);

        float const font_size = editor.font_size;
        draw::TextStyle text_style = {
            .font = editor_font,
            .size = font_size,
            .color = to_draw_color(palette.text),
        };
        EditorCommand const selected = editor_selected_command(editor);
        float const text_x = panel.min.x + 12.0f;
        float const text_y = panel.min.y + 10.0f;
        draw::draw_text(draw_context, {text_x, text_y}, text_style, selected.description, nullptr);
        if (!selected.alias.empty()) {
            draw::draw_text(
                draw_context,
                {text_x, text_y + 18.0f},
                text_style,
                fmt::tprintf("Aliases: %s", selected.alias),
                nullptr
            );
        }

        float const list_width = std::max(0.0f, panel.max.x - panel.min.x - 16.0f);
        float selected_max_x = 0.0f;
        float content_width = 0.0f;
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            float const width =
                font_cache::text_advance(editor_font, font_size, editor_command(index).name) +
                16.0f;
            if (index == editor.command_selected) {
                selected_max_x = content_width + width;
            }
            content_width += width;
            if (index + 1u < editor_command_count()) {
                content_width += 8.0f;
            }
        }
        float const scroll_x = std::clamp(
            selected_max_x - list_width, 0.0f, std::max(0.0f, content_width - list_width)
        );

        float x = panel.min.x + 8.0f - scroll_x;
        float const y = panel.max.y - COMMAND_LIST_HEIGHT - 2.0f;
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const command = editor_command(index);
            float const width =
                font_cache::text_advance(editor_font, font_size, command.name) + 16.0f;
            bool const active = index == editor.command_selected;
            if (active) {
                draw::Rect const item = {{x, y + 3.0f}, {x + width, y + 25.0f}};
                draw::draw_rect_filled(
                    draw_context, item, to_draw_color(palette.cursor_line), 4.0f
                );
                draw::draw_rect(draw_context, item, to_draw_color(palette.cursor), 1.0f, 4.0f);
            }
            text_style.color = to_draw_color(active ? palette.text : palette.muted);
            draw::draw_text(draw_context, {x + 8.0f, y + 2.0f}, text_style, command.name, nullptr);
            x += width + 8.0f;
        }

        draw::pop_clip_rect(draw_context);
    }

    auto draw_deleted_open_file_tab_marks(
        draw::Context draw_context,
        gui::Frame const& ui,
        EditorState const& editor,
        Palette const& palette
    ) -> void;

    auto draw_lsp_overlay(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        float char_width,
        gui::Frame const& ui,
        Palette const& palette
    ) -> void;

    [[nodiscard]] auto lsp_hover_popup_hit(
        gui::Frame const& ui, EditorState const& editor, gui::InputState const& input
    ) -> bool {
        if (editor.lsp_popup != EditorLspPopupKind::HOVER) {
            return false;
        }
        if (input.mouse_down[0u] &&
            editor.lsp_hover_selection.start != editor.lsp_hover_selection.end) {
            return true;
        }
        gui::BoxInfo const* const popup =
            ui.find_box(gui::id("lsp_hover_popup"), gui::BoxKind::POPUP);
        return popup != nullptr && point_in_rect(popup->rect, input.mouse_pos);
    }

    auto draw_editor_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette,
        bool selection_visible
    ) -> void {
        draw_deleted_open_file_tab_marks(draw_context, ui, editor, palette);
        size_t const initial_focus = editor.focused_split;
        size_t target_focus = initial_focus;
        draw_editor_split_surface(
            draw_context,
            editor_font,
            editor,
            char_width,
            ui,
            lsp_hover_popup_hit(ui, editor, input) ? gui::InputState{} : input,
            palette,
            editor.root_split,
            initial_focus,
            target_focus,
            selection_visible
        );
        focus_editor_split(editor, target_focus);
        draw_command_overlay(draw_context, editor_font, editor, ui, palette);
        draw_lsp_overlay(draw_context, editor_font, editor, char_width, ui, palette);
    }

    auto draw_tree_guide(gui::Frame& ui, Palette const& palette) -> void {
        if (auto slot = ui.column({
                .layout = {
                    .width = gui::px(TREE_INDENT_WIDTH),
                    .height = gui::fill(),
                    .align_x = gui::Align::CENTER,
                    .align_y = gui::Align::STRETCH,
                },
            })) {
            ui.spacer({
                .layout = {.width = gui::px(1.0f), .height = gui::fill()},
                .style = {.background = gui::color_alpha(palette.muted, 0.36f)},
            });
        }
    }

    auto draw_tree_file(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        FileTreeEntry const& file,
        size_t guide_count
    ) -> void {
        if (auto row = ui.row(
                gui::id(file.path),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(26.0f),
                        .align_y = gui::Align::STRETCH,
                    },
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (row.signal().clicked_left) {
                focus_code_split_for_open(editor);
                open_tree_file(editor, file);
            }
            bool const selected = editor.current_file_path == file.path;
            ui.label(
                file.name,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, 12.0f, 0.0f, 4.0f)},
                    .style = {
                        .role = selected ? gui::StyleRole::AUTO : gui::StyleRole::TEXT_MUTED,
                        .background = selected ? gui::rgb(34, 45, 58) : gui::Color{},
                        .foreground = selected ? palette.text : gui::Color{},
                        .radius = selected ? 5.0f : -1.0f,
                        .font_size = editor.font_size,
                    },
                }
            );
        }
    }

    auto draw_tree_folder(
        gui::Frame& ui,
        EditorState const& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        StrRef id_text,
        StrRef text,
        size_t guide_count,
        bool* open
    ) -> void {
        if (auto row = ui.row(
                gui::id(id_text),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(26.0f),
                        .align_y = gui::Align::STRETCH,
                    },
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (auto arrow_slot = ui.row({
                    .layout = {
                        .width = gui::px(TREE_ARROW_SLOT_WIDTH),
                        .height = gui::fill(),
                        .align_x = gui::Align::CENTER,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                bool const is_open = open != nullptr && *open;
                ui.label(
                    is_open ? TREE_ARROW_OPEN : TREE_ARROW_CLOSED,
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font = icon_font,
                            .font_size = editor_scaled_font_size(editor, 9.5f),
                        },
                    }
                );
            }
            ui.label(
                text,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, 8.0f, 0.0f, 4.0f)},
                    .style = {
                        .role = gui::StyleRole::TEXT_MUTED,
                        .font_size = editor.font_size,
                    },
                }
            );
            if (open != nullptr && row.signal().clicked_left) {
                *open = !*open;
            }
        }
    }

    auto draw_tree_entry(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        FileTreeEntry& entry
    ) -> void {
        size_t const guide_count = entry.depth + 1u;
        if (entry.is_directory) {
            draw_tree_folder(
                ui, editor, palette, icon_font, entry.path, entry.name, guide_count, &entry.open
            );
        } else {
            draw_tree_file(ui, editor, palette, entry, guide_count);
        }
    }

    auto draw_sidebar(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width
    ) -> void {
        float const width = sidebar_width(editor, client_width);
        if (auto sidebar = ui.scroll_panel(
                gui::id("sidebar"),
                {
                    .layout =
                        {
                            .width = gui::px(width),
                            .height = gui::fill(),
                            .padding = gui::insets(14.0f, 10.0f),
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {
                        .background = palette.panel,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            bool tree_open = editor.flag(EditorFlag::TREE_OPEN);
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &tree_open
            );
            editor.set_flag(EditorFlag::TREE_OPEN, tree_open);
            if (tree_open) {
                size_t closed_depth = static_cast<size_t>(-1);
                for (FileTreeEntry& entry : editor.tree_files) {
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    draw_tree_entry(ui, editor, palette, icon_font, entry);
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
        }
    }

    auto draw_filesystem_panel(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        size_t split,
        gui::Size width,
        gui::Size height,
        bool selection_visible
    ) -> void {
        bool const focused = selection_visible && split == editor.focused_split;
        if (auto sidebar = ui.scroll_panel(
                editor_surface_id(split),
                {
                    .layout =
                        {
                            .width = width,
                            .height = height,
                            .padding = gui::insets(14.0f, 10.0f),
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = palette.panel,
                            .border = focused ? palette.cursor : palette.border,
                            .border_thickness = focused ? 2.0f : 1.0f,
                            .radius = 8.0f,
                        },
                    .debug_name = "filesystem_surface",
                }
            )) {
            if (sidebar.signal().pressed_left) {
                focus_editor_split(editor, split);
            }
            if (editor.tree_root_name.empty()) {
                return;
            }
            bool tree_open = editor.flag(EditorFlag::TREE_OPEN);
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &tree_open
            );
            editor.set_flag(EditorFlag::TREE_OPEN, tree_open);
            if (tree_open) {
                size_t closed_depth = static_cast<size_t>(-1);
                for (FileTreeEntry& entry : editor.tree_files) {
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    draw_tree_entry(ui, editor, palette, icon_font, entry);
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
        }
    }

    auto draw_sidebar_resizer(
        gui::Frame& ui, EditorState& editor, float client_width, gui::InputState const& input
    ) -> void {
        if (auto resizer = ui.row(
                gui::id("sidebar_resizer"),
                {.layout = {.width = gui::px(SIDEBAR_RESIZER_WIDTH), .height = gui::fill()}}
            )) {
            gui::Signal const signal = resizer.signal();
            if (signal.pressed_left) {
                editor.set_flag(EditorFlag::SIDEBAR_RESIZING, true);
                editor.sidebar_resize_grab_x =
                    input.mouse_pos.x - sidebar_width(editor, client_width);
            }
        }
    }

    [[nodiscard]] auto open_file_selected(EditorState const& editor, OpenFile const& file) -> bool {
        return same_file(file.name, file.path, editor.current_file_name, editor.current_file_path);
    }

    [[nodiscard]] auto open_file_dirty(EditorState const& editor, OpenFile const& file) -> bool {
        if (same_file(file.name, file.path, editor.current_file_name, editor.current_file_path) &&
            editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
            return editor.flag(EditorFlag::DIRTY);
        }
        size_t const focused_pane = editor_focused_pane(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane) {
                continue;
            }
            EditorPane const* const pane = editor.panes[index];
            if (pane != nullptr && pane->kind == EditorPaneKind::CODE &&
                same_file(file.name, file.path, pane->current_file_name, pane->current_file_path)) {
                return pane->dirty;
            }
        }
        return file.dirty;
    }

    [[nodiscard]] auto open_file_deleted(EditorState const& editor, OpenFile const& file) -> bool {
        if (same_file(file.name, file.path, editor.current_file_name, editor.current_file_path) &&
            editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
            return editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        }
        size_t const focused_pane = editor_focused_pane(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane) {
                continue;
            }
            EditorPane const* const pane = editor.panes[index];
            if (pane != nullptr && pane->kind == EditorPaneKind::CODE &&
                same_file(file.name, file.path, pane->current_file_name, pane->current_file_path)) {
                return pane->file_deleted_on_disk;
            }
        }
        return file.file_deleted_on_disk;
    }

    [[nodiscard]] auto selected_open_file_index(EditorState const& editor, size_t& out_index)
        -> bool {
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            if (open_file_selected(editor, editor.open_files[index])) {
                out_index = index;
                return true;
            }
        }
        return false;
    }

    auto remove_open_file_view(EditorPane& pane, StrRef name, StrRef path) -> void {
        for (size_t index = 0u; index < pane.open_file_views.size(); ++index) {
            OpenFileViewState const& view = pane.open_file_views[index];
            if (same_file(view.name, view.path, name, path)) {
                pane.open_file_views.ordered_remove(index);
                return;
            }
        }
    }

    [[nodiscard]] auto most_recent_open_file_index(EditorState const& editor, size_t fallback)
        -> size_t {
        size_t result = fallback;
        uint64_t last_used = 0u;
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            if (editor.open_files[index].last_used > last_used) {
                result = index;
                last_used = editor.open_files[index].last_used;
            }
        }
        return result;
    }

    auto close_open_file(EditorState& editor, size_t index, bool force) -> void {
        if (index >= editor.open_files.size()) {
            return;
        }

        OpenFile const closing = editor.open_files[index];
        bool const selected = open_file_selected(editor, editor.open_files[index]);
        if (selected && !force) {
            save_scratch_file(editor);
        }
        editor.open_files.ordered_remove(index);
        for (EditorPane* pane : editor.panes) {
            if (pane != nullptr) {
                remove_open_file_view(*pane, closing.name, closing.path);
            }
        }
        if (editor.open_files.empty()) {
            editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
            return;
        }
        if (!selected) {
            return;
        }

        size_t const fallback_index = std::min(index, editor.open_files.size() - 1u);
        size_t const next_index = most_recent_open_file_index(editor, fallback_index);
        OpenFile const next = editor.open_files[next_index];
        if (!open_file(editor, next.name, next.path, false)) {
            open_scratch_file(editor);
        }
    }

    auto close_current_file(EditorState& editor, bool force) -> void {
        size_t index = 0u;
        if (!selected_open_file_index(editor, index)) {
            store_current_open_file(editor);
        }
        if (selected_open_file_index(editor, index)) {
            close_open_file(editor, index, force);
        } else if (editor.open_files.empty()) {
            editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
        }
    }

    auto focus_open_file_index(EditorState& editor, size_t index) -> void {
        if (index >= editor.open_files.size()) {
            return;
        }
        focus_code_split_for_open(editor);
        OpenFile const file = editor.open_files[index];
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    [[nodiscard]] auto first_dirty_open_file_index(EditorState& editor, size_t& out_index) -> bool {
        store_current_open_file(editor);
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            if (open_file_dirty(editor, editor.open_files[index])) {
                out_index = index;
                return true;
            }
        }
        return false;
    }

    auto request_close_app(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, false);
        if (editor.flag(EditorFlag::SAVE_PATH_OPEN) ||
            editor.close_intent != EditorCloseIntent::NONE) {
            return;
        }
        size_t index = 0u;
        if (first_dirty_open_file_index(editor, index)) {
            focus_open_file_index(editor, index);
            editor.close_intent = EditorCloseIntent::APP;
            return;
        }
        editor.set_flag(EditorFlag::CLOSE_APP_CONFIRMED, true);
    }

    auto request_close_current_file(EditorState& editor, bool force) -> void {
        if (!force && editor_focused_pane_kind(editor) == EditorPaneKind::CODE &&
            editor.flag(EditorFlag::DIRTY)) {
            editor.close_intent = EditorCloseIntent::BUFFER;
            return;
        }
        close_current_file(editor, force);
    }

    auto request_close_open_file(EditorState& editor, size_t index) -> void {
        if (index >= editor.open_files.size()) {
            return;
        }
        if (open_file_dirty(editor, editor.open_files[index])) {
            focus_open_file_index(editor, index);
            editor.close_intent = EditorCloseIntent::BUFFER;
            return;
        }
        close_open_file(editor, index, false);
    }

    auto continue_after_saved_close(EditorState& editor, EditorCloseIntent intent) -> void {
        editor.close_intent = EditorCloseIntent::NONE;
        if (intent == EditorCloseIntent::BUFFER) {
            close_current_file(editor);
        } else if (intent == EditorCloseIntent::APP) {
            request_close_app(editor);
        }
    }

    auto open_file_search_match(EditorState& editor, size_t tree_file_index) -> void {
        if (tree_file_index >= editor.tree_files.size()) {
            return;
        }
        FileTreeEntry const& file = editor.tree_files[tree_file_index];
        if (file.is_directory) {
            return;
        }
        expand_filesystem_tree_to_file(editor, tree_file_index);
        focus_code_split_for_open(editor);
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    auto open_buffer_search_match(EditorState& editor, size_t open_file_index) -> void {
        if (open_file_index >= editor.open_files.size()) {
            return;
        }
        focus_code_split_for_open(editor);
        OpenFile const file = editor.open_files[open_file_index];
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    [[nodiscard]] auto lsp_location_name(Arena& arena, LspLocation const& location) -> StrRef {
        return fmt::aprintf(
                   arena.resource(),
                   "%s:%zu:%zu",
                   location.path,
                   location.range.start.line + 1u,
                   location.range.start.column + 1u
        )
            .str();
    }

    auto record_current_jump(EditorState& editor) -> void {
        if (editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
            record_editor_jump(
                editor,
                editor.current_file_name,
                editor.current_file_path,
                editor.cursor_line,
                editor.cursor_column
            );
        }
    }

    auto center_current_cursor(EditorState& editor) -> void {
        if (editor.focused_split < editor.split_nodes.size()) {
            center_cursor(editor, editor.split_nodes[editor.focused_split].rect);
        }
    }

    auto open_lsp_location(EditorState& editor, LspLocation const& location) -> void {
        if (location.path.empty()) {
            return;
        }
        record_current_jump(editor);
        focus_code_split_for_open(editor);
        StrRef const name = render_path_leaf(location.path);
        if (open_file(editor, name.empty() ? location.path : name, location.path)) {
            set_editor_cursor(editor, location.range.start.line, location.range.start.column);
            center_current_cursor(editor);
            record_editor_jump(
                editor,
                name.empty() ? location.path : name,
                location.path,
                location.range.start.line,
                location.range.start.column
            );
        }
    }

    auto open_lsp_symbol(EditorState& editor, LspDocumentSymbol const& symbol) -> void {
        StrRef const path = symbol.path.empty() ? editor.current_file_path : symbol.path;
        if (path.empty()) {
            return;
        }
        record_current_jump(editor);
        focus_code_split_for_open(editor);
        StrRef const name = render_path_leaf(path);
        if (open_file(editor, name.empty() ? path : name, path)) {
            size_t const line = symbol.selection_range.start.line;
            size_t const column = symbol.selection_range.start.column;
            set_editor_cursor(editor, line, column);
            center_current_cursor(editor);
            record_editor_jump(editor, name.empty() ? path : name, path, line, column);
        }
    }

    auto open_lsp_diagnostic(EditorState& editor, LspDiagnostic const& diagnostic) -> void {
        if (diagnostic.path.empty()) {
            return;
        }
        record_current_jump(editor);
        focus_code_split_for_open(editor);
        StrRef const name = render_path_leaf(diagnostic.path);
        if (open_file(editor, name.empty() ? diagnostic.path : name, diagnostic.path)) {
            set_editor_cursor(editor, diagnostic.range.start.line, diagnostic.range.start.column);
            center_current_cursor(editor);
            record_editor_jump(
                editor,
                name.empty() ? diagnostic.path : name,
                diagnostic.path,
                diagnostic.range.start.line,
                diagnostic.range.start.column
            );
        }
    }

    auto open_recorded_jump(EditorState& editor, size_t index) -> void {
        if (index >= editor.jumps.size()) {
            return;
        }
        EditorJump const jump = editor.jumps[index];
        focus_code_split_for_open(editor);
        if (open_file(editor, jump.name, jump.path)) {
            set_editor_cursor(editor, jump.line, jump.column);
            center_current_cursor(editor);
            editor.jump_cursor = index;
        }
    }

    auto open_global_search_match(EditorState& editor, size_t index) -> void {
        if (index >= editor.global_search_results.size()) {
            return;
        }
        GlobalSearchResult const result = editor.global_search_results[index];
        if (result.tree_file_index >= editor.tree_files.size()) {
            return;
        }
        FileTreeEntry const& file = editor.tree_files[result.tree_file_index];
        if (file.is_directory) {
            return;
        }
        focus_code_split_for_open(editor);
        if (open_file(editor, file.name, file.path)) {
            set_editor_cursor(editor, result.line, result.column);
            center_current_cursor(editor);
        }
    }

    [[nodiscard]] auto find_or_add_open_file(EditorState& editor, StrRef path) -> OpenFile* {
        StrRef const name = render_path_leaf(path);
        remember_open_file(editor, name.empty() ? path : name, path);
        return find_open_file(editor, name.empty() ? path : name, path);
    }

    auto apply_lsp_edits_to_file(EditorState& editor, StrRef path, Slice<LspTextEdit const> edits)
        -> void {
        if (path.empty()) {
            return;
        }
        if (lsp_same_path(path, editor.current_file_path)) {
            BASE_UNUSED(apply_editor_lsp_text_edits(editor, edits));
            return;
        }
        if (editor.arena == nullptr) {
            return;
        }

        OpenFile* const file = find_or_add_open_file(editor, path);
        if (file == nullptr) {
            return;
        }

        StrRef text = file->text;
        if (!file->text_valid && !read_tree_file_display_text(*editor.arena, path, text)) {
            return;
        }
        StrRef const updated = lsp_apply_text_edits(*editor.arena, text, edits, path);
        file->text = updated;
        if (file->saved_text.empty()) {
            file->saved_text = text;
        }
        file->text_valid = true;
        file->dirty = updated != file->saved_text;
    }

    auto apply_lsp_workspace_edits(EditorState& editor, Slice<LspTextEdit const> edits) -> void {
        for (LspTextEdit const& edit : edits) {
            bool seen = false;
            for (LspTextEdit const& previous : edits.prefix(&edit - edits.data())) {
                if (lsp_same_path(previous.path, edit.path)) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                apply_lsp_edits_to_file(editor, edit.path, edits);
            }
        }
    }

    auto handle_lsp_pending_actions(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        if (editor.lsp_seen_text_edits_generation != editor.lsp_bridge->text_edits_generation) {
            editor.lsp_seen_text_edits_generation = editor.lsp_bridge->text_edits_generation;
            apply_lsp_workspace_edits(editor, editor.lsp_bridge->text_edits);
        }
        if (editor.lsp_open_location_index != LSP_NO_SELECTION) {
            size_t const index = editor.lsp_open_location_index;
            editor.lsp_open_location_index = LSP_NO_SELECTION;
            if (index < editor.lsp_bridge->locations.size()) {
                open_lsp_location(editor, editor.lsp_bridge->locations[index]);
            }
        }
        if (editor.lsp_open_symbol_index != LSP_NO_SELECTION) {
            size_t const index = editor.lsp_open_symbol_index;
            editor.lsp_open_symbol_index = LSP_NO_SELECTION;
            if (index < editor.lsp_bridge->symbols.size()) {
                open_lsp_symbol(editor, editor.lsp_bridge->symbols[index]);
            }
        }
        if (editor.lsp_open_diagnostic_index != LSP_NO_SELECTION) {
            size_t const index = editor.lsp_open_diagnostic_index;
            editor.lsp_open_diagnostic_index = LSP_NO_SELECTION;
            if (index < editor.lsp_bridge->diagnostics.size()) {
                open_lsp_diagnostic(editor, editor.lsp_bridge->diagnostics[index]);
            }
        }
        if (editor.lsp_apply_code_action_index != LSP_NO_SELECTION) {
            size_t const index = editor.lsp_apply_code_action_index;
            editor.lsp_apply_code_action_index = LSP_NO_SELECTION;
            if (index < editor.lsp_bridge->code_actions.size()) {
                apply_lsp_workspace_edits(editor, editor.lsp_bridge->code_actions[index].edits);
            }
        }
    }

    auto sync_lsp_result_popups(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        if (editor.lsp_seen_completions_generation != editor.lsp_bridge->completions_generation) {
            editor.lsp_seen_completions_generation = editor.lsp_bridge->completions_generation;
            if (!editor.lsp_bridge->completions.empty()) {
                editor.lsp_popup = EditorLspPopupKind::COMPLETION;
                editor.lsp_selected = 0u;
            }
        }
        if (editor.lsp_seen_hover_generation != editor.lsp_bridge->hover_generation) {
            editor.lsp_seen_hover_generation = editor.lsp_bridge->hover_generation;
            if (!editor.lsp_bridge->hover.text.empty()) {
                editor.lsp_popup = EditorLspPopupKind::HOVER;
                editor.lsp_selected = 0u;
                editor.lsp_hover_selection = {};
            }
        }
        if (editor.lsp_seen_locations_generation != editor.lsp_bridge->locations_generation) {
            editor.lsp_seen_locations_generation = editor.lsp_bridge->locations_generation;
            if (editor.lsp_bridge->locations_kind == LspRequestKind::REFERENCES) {
                open_editor_lsp_locations(editor);
            } else if (!editor.lsp_bridge->locations.empty()) {
                if (editor.lsp_bridge->locations.size() == 1u &&
                    (editor.lsp_bridge->locations_kind == LspRequestKind::DEFINITION ||
                     editor.lsp_bridge->locations_kind == LspRequestKind::DECLARATION)) {
                    editor.lsp_open_location_index = 0u;
                    close_editor_lsp_popup(editor);
                } else {
                    editor.lsp_popup = EditorLspPopupKind::LOCATIONS;
                    editor.lsp_selected = 0u;
                }
            }
        }
        if (editor.lsp_seen_code_actions_generation != editor.lsp_bridge->code_actions_generation) {
            editor.lsp_seen_code_actions_generation = editor.lsp_bridge->code_actions_generation;
            if (!editor.lsp_bridge->code_actions.empty()) {
                editor.lsp_popup = EditorLspPopupKind::CODE_ACTIONS;
                editor.lsp_selected = 0u;
            }
        }
        if (editor.lsp_seen_symbols_generation != editor.lsp_bridge->symbols_generation) {
            editor.lsp_seen_symbols_generation = editor.lsp_bridge->symbols_generation;
            if (!editor.lsp_bridge->symbols.empty()) {
                open_editor_lsp_symbols(
                    editor,
                    editor.lsp_bridge->symbols_kind == LspRequestKind::WORKSPACE_SYMBOL
                        ? EditorJumpListKind::LSP_WORKSPACE_SYMBOLS
                        : EditorJumpListKind::LSP_DOCUMENT_SYMBOLS
                );
            }
        }
    }

    auto draw_file_search_picker(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font editor_font,
        Palette const& palette,
        float client_width,
        float client_height,
        gui::InputState const& input,
        bool buffers
    ) -> void {
        float constexpr MAIN_PANEL_MARGIN = 10.0f;
        float constexpr DIALOG_GAP = 8.0f;
        float constexpr PANEL_PADDING = 10.0f;
        float constexpr QUERY_HEIGHT = 36.0f;
        float constexpr SHELL_PADDING = 12.0f;
        float constexpr SHELL_GAP = 10.0f;
        float constexpr HEADER_HEIGHT = 40.0f;
        float constexpr BOTTOM_BAR_HEIGHT = 30.0f;
        float const modal_margin_top =
            SHELL_PADDING + HEADER_HEIGHT + SHELL_GAP + MAIN_PANEL_MARGIN;
        float const modal_margin_bottom =
            SHELL_PADDING + SHELL_GAP + BOTTOM_BAR_HEIGHT + MAIN_PANEL_MARGIN;
        float const desired_margin_x = client_width * 0.1f;
        float const min_dialog_width = buffers ? 620.0f : 900.0f;
        float const max_margin_x = std::max(20.0f, (client_width - min_dialog_width) * 0.5f);
        float const modal_margin_x = std::clamp(desired_margin_x, 20.0f, max_margin_x);
        float const dialog_height =
            std::max(180.0f, client_height - modal_margin_top - modal_margin_bottom);

        editor.file_search_text_size = cstr_len(editor.file_search_text);
        ArenaTemp search_temp = begin_thread_temp_arena();
        FileSearchMatch* matches = nullptr;
        BufferSearchMatch* buffer_matches = nullptr;
        size_t match_count = 0u;
        size_t filtered_count = 0u;
        size_t total_count = 0u;

        if (!editor.file_search_mouse_known) {
            editor.file_search_mouse_pos = input.mouse_pos;
            editor.file_search_mouse_known = true;
        } else {
            bool const mouse_moved = input.mouse_pos.x != editor.file_search_mouse_pos.x ||
                                     input.mouse_pos.y != editor.file_search_mouse_pos.y;
            if (mouse_moved || input.scroll_delta_y != 0.0f || input.mouse_down[0u]) {
                editor.file_search_mouse_select = true;
            }
            editor.file_search_mouse_pos = input.mouse_pos;
        }

        if (auto modal = ui.modal(
                gui::id(buffers ? "buffer_search_modal" : "file_search_modal"),
                {
                    .layout =
                        {
                            .padding =
                                {
                                    modal_margin_top,
                                    modal_margin_x,
                                    modal_margin_bottom,
                                    modal_margin_x,
                                },
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::START,
                        },
                    .style = {.background = gui::rgba(0, 0, 0, 0)},
                    .debug_name = buffers ? "buffer_search_modal" : "file_search_modal",
                }
            )) {
            if (auto dialog = ui.row(
                    gui::id(buffers ? "buffer_search_dialog" : "file_search_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(dialog_height),
                                .padding = gui::insets(PANEL_PADDING),
                                .gap = DIALOG_GAP,
                                .align_y = gui::Align::STRETCH,
                            },
                        .style = {
                            .background = gui::color_alpha(palette.panel, 0.94f),
                            .border = gui::color_alpha(palette.cursor, 0.72f),
                            .border_thickness = 2.0f,
                            .radius = 8.0f,
                            .shadow = {
                                .offset = {0.0f, 0.0f},
                                .blur_radius = 26.0f,
                                .spread = 1.0f,
                                .color = gui::color_alpha(palette.cursor, 0.20f),
                            },
                        },
                    }
                )) {
                if (auto left = ui.column(
                        gui::id(buffers ? "buffer_search_left" : "file_search_left"),
                        {
                            .layout = {
                                .width = gui::fill(buffers ? 1.0f : 0.48f),
                                .height = gui::fill(),
                                .align_x = gui::Align::STRETCH,
                                .clip = true,
                            },
                        }
                    )) {
                    if (auto query = ui.row(
                            gui::id(buffers ? "buffer_search_query" : "file_search_query"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(QUERY_HEIGHT),
                                        .padding = gui::insets(0.0f, PANEL_PADDING),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style = {
                                    .background = palette.panel_raised,
                                    .border = palette.border,
                                    .border_thickness = 1.0f,
                                    .radius = 6.0f,
                                },
                            }
                        )) {
                        ui.label(
                            ">",
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {.foreground = palette.cursor, .font = editor_font},
                            }
                        );
                        gui::Id const input_id =
                            gui::id(buffers ? "buffer_search_input" : "file_search_input");
                        ui.request_focus(input_id);
                        gui::Signal const text_input = ui.input_text(
                            input_id,
                            "",
                            editor.file_search_text,
                            FILE_SEARCH_TEXT_CAPACITY,
                            gui::InputTextDesc{
                                .box =
                                    {
                                        .layout =
                                            {
                                                .width = gui::fill(),
                                                .height = gui::fill(),
                                                .padding = gui::insets(0.0f),
                                            },
                                        .style =
                                            {
                                                .background = gui::rgba(0, 0, 0, 0),
                                                .foreground = palette.text,
                                                .border = gui::rgba(0, 0, 0, 0),
                                                .border_thickness = 1.0f,
                                                .radius = 0.0f,
                                                .font = editor_font,
                                                .font_size = editor.font_size,
                                            },
                                    },
                                .ignore_input_on_focus = true,
                            }
                        );
                        if (text_input.changed) {
                            editor.file_search_text_size = cstr_len(editor.file_search_text);
                            editor.file_search_selected = 0u;
                            editor.file_search_mouse_select = false;
                            editor.file_search_reveal_selected = true;
                        }
                        filtered_count = file_search_filtered_count(editor, buffers);
                        total_count = file_search_total_count(editor, buffers);
                        if (filtered_count != 0u) {
                            if (buffers) {
                                buffer_matches = arena_alloc<BufferSearchMatch>(
                                    *search_temp.arena(), filtered_count
                                );
                                match_count = collect_buffer_search_matches(
                                    editor, slice(buffer_matches, filtered_count)
                                );
                            } else {
                                matches = arena_alloc<FileSearchMatch>(
                                    *search_temp.arena(), filtered_count
                                );
                                match_count = collect_file_search_matches(
                                    editor, slice(matches, filtered_count)
                                );
                            }
                        }
                        editor.file_search_selected =
                            match_count == 0u
                                ? 0u
                                : std::min(editor.file_search_selected, match_count - 1u);
                        ui.label(
                            fmt::tprintf("%zu/%zu", filtered_count, total_count),
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {
                                    .foreground = palette.text,
                                    .font = editor_font,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                    }

                    gui::Id const results_id =
                        gui::id(buffers ? "buffer_search_results" : "file_search_results");
                    if (editor.file_search_reveal_selected && match_count != 0u) {
                        ui.scroll_to_index(results_id, editor.file_search_selected);
                        editor.file_search_reveal_selected = false;
                    }
                    if (match_count == 0u) {
                        ui.label(
                            buffers
                                ? (editor.open_files.empty() ? "No open buffers"
                                                             : "No matching buffers")
                                : (total_count == 0u ? "No indexed files" : "No matching files"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                        .padding = gui::insets(0.0f, PANEL_PADDING),
                                    },
                                .style = {
                                    .foreground = palette.muted,
                                    .font = editor_font,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                    } else {
                        auto results = ui.list_fixed(
                            results_id,
                            {
                                .item_count = match_count,
                                .item_height = FILE_SEARCH_ROW_HEIGHT,
                                .box = {
                                    .layout = {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .align_x = gui::Align::STRETCH,
                                    },
                                },
                            }
                        );
                        for (size_t index = results.first; index < results.end; ++index) {
                            bool selected = index == editor.file_search_selected;
                            if (auto row = results.row(
                                    gui::id(
                                        buffers ? "buffer_search_result" : "file_search_result",
                                        index
                                    ),
                                    {
                                        .layout =
                                            {
                                                .padding = gui::insets(0.0f, PANEL_PADDING),
                                                .gap = 8.0f,
                                                .align_y = gui::Align::CENTER,
                                            },
                                        .style = {
                                            .background =
                                                selected ? palette.cursor_line : gui::Color{},
                                            .radius = selected ? 5.0f : -1.0f,
                                        },
                                    }
                                )) {
                                gui::Signal const signal = row.signal();
                                if (signal.hovered && editor.file_search_mouse_select) {
                                    editor.file_search_selected = index;
                                }
                                if (signal.clicked_left) {
                                    editor.file_search_selected = index;
                                    if (buffers) {
                                        open_buffer_search_match(
                                            editor, buffer_matches[index].open_file_index
                                        );
                                        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
                                    } else {
                                        open_file_search_match(
                                            editor, matches[index].tree_file_index
                                        );
                                        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
                                    }
                                }
                                selected = index == editor.file_search_selected;
                                ui.label(
                                    selected ? ">" : "",
                                    {
                                        .layout = {.width = gui::px(14.0f), .height = gui::fill()},
                                        .style = {
                                            .foreground = palette.cursor, .font = editor_font
                                        },
                                    }
                                );
                                StrRef text = {};
                                if (buffers) {
                                    OpenFile const& file =
                                        editor.open_files[buffer_matches[index].open_file_index];
                                    bool const current =
                                        editor_focused_pane_kind(editor) == EditorPaneKind::CODE &&
                                        open_file_selected(editor, file);
                                    text = fmt::tprintf(
                                        "%zu %c %s",
                                        buffer_matches[index].open_file_index + 1u,
                                        current ? '*' : ' ',
                                        !file.name.empty() ? file.name : file.path
                                    );
                                } else {
                                    FileTreeEntry const& file =
                                        editor.tree_files[matches[index].tree_file_index];
                                    text = file_search_entry_text(file);
                                }
                                ui.label(
                                    text,
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::fill()},
                                        .style = {
                                            .foreground = selected ? palette.text : palette.muted,
                                            .font = editor_font,
                                            .font_size = editor.font_size,
                                        },
                                    }
                                );
                            }
                        }
                    }
                }

                if (!buffers) {
                    if (auto preview = ui.column(
                            gui::id("file_search_preview"),
                            {
                                .layout = {
                                    .width = gui::fill(0.52f),
                                    .height = gui::fill(),
                                    .padding = gui::insets(PANEL_PADDING),
                                    .clip = true,
                                },
                            }
                        )) {
                        StrRef preview_text = {};
                        StrRef preview_name = {};
                        ArenaTemp temp = begin_thread_temp_arena();
                        if (match_count != 0u) {
                            FileTreeEntry const& file =
                                editor.tree_files[matches[editor.file_search_selected]
                                                      .tree_file_index];
                            preview_name = file.name;
                            BASE_UNUSED(
                                read_tree_file_preview_text(*temp.arena(), file.path, preview_text)
                            );
                        }
                        if (preview_text.empty()) {
                            ui.label(
                                match_count == 0u ? "No preview" : "",
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                        },
                                    .style = {
                                        .foreground = palette.muted,
                                        .font = editor_font,
                                        .font_size = editor.font_size,
                                    },
                                }
                            );
                        }
                        SyntaxTokenizer const tokenizer =
                            syntax_tokenizer_for_file_name(preview_name);
                        size_t offset = 0u;
                        size_t line_count = 0u;
                        float const line_height = editor_line_height(editor);
                        size_t const preview_line_limit = std::max<size_t>(
                            1u,
                            static_cast<size_t>(
                                (dialog_height - 2.0f * PANEL_PADDING) / line_height
                            )
                        );
                        while (line_count < preview_line_limit && offset < preview_text.size()) {
                            StrRef const line = next_text_line(preview_text, offset);
                            if (auto line_row = ui.row(
                                    gui::id("file_search_preview_line", line_count),
                                    {
                                        .layout = {
                                            .width = gui::children(),
                                            .height = gui::px(line_height),
                                            .align_y = gui::Align::CENTER,
                                        },
                                    }
                                )) {
                                draw_syntax_label_line(
                                    ui, editor_font, tokenizer, palette, line, editor.font_size
                                );
                            }
                            line_count += 1u;
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] auto jump_list_row_text(
        Arena& arena,
        size_t index,
        EditorJump const& jump,
        EditorJumpListKind kind,
        StrRef root_path
    ) -> StrRef {
        StrRef name = jump.name;
        if (name.empty()) {
            name = render_path_leaf(jump.path);
        }
        if (name.empty()) {
            name = jump.path;
        }
        if (kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS) {
            return name;
        }
        if (kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
            StrRef const path = render_workspace_relative_path(root_path, jump.path);
            return jump.path.empty() ? name
                                     : fmt::aprintf(arena.resource(), "%s  %s", name, path).str();
        }
        return fmt::aprintf(
                   arena.resource(),
                   "%zu %s:%zu:%zu",
                   index + 1u,
                   name,
                   jump.line + 1u,
                   jump.column + 1u
        )
            .str();
    }

    [[nodiscard]] auto render_jump_list_is_diagnostics(EditorJumpListKind kind) -> bool {
        return kind == EditorJumpListKind::LSP_FILE_DIAGNOSTICS ||
               kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS;
    }

    [[nodiscard]] auto lsp_diagnostic_row_text(
        EditorState const& editor, Arena& arena, LspDiagnostic const& diagnostic
    ) -> StrRef {
        StrRef const message =
            !diagnostic.message.empty() ? diagnostic.message : StrRef("diagnostic");
        if (editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS) {
            StrRef const path =
                render_workspace_relative_path(editor.save_root_path, diagnostic.path);
            return fmt::aprintf(
                       arena.resource(),
                       "%s:%zu:%zu  %s",
                       path,
                       diagnostic.range.start.line + 1u,
                       diagnostic.range.start.column + 1u,
                       message
            )
                .str();
        }
        return fmt::aprintf(
                   arena.resource(),
                   "%zu:%zu  %s",
                   diagnostic.range.start.line + 1u,
                   diagnostic.range.start.column + 1u,
                   message
        )
            .str();
    }

    [[nodiscard]] auto lsp_location_jump(LspLocation const& location) -> EditorJump {
        StrRef name = render_path_leaf(location.path);
        if (name.empty()) {
            name = location.path;
        }
        return {
            .name = name,
            .path = location.path,
            .line = location.range.start.line,
            .column = location.range.start.column,
        };
    }

    [[nodiscard]] auto lsp_symbol_jump(EditorState const& editor, LspDocumentSymbol const& symbol)
        -> EditorJump {
        StrRef const path = symbol.path.empty() ? editor.current_file_path : symbol.path;
        return {
            .name = symbol.name,
            .path = path,
            .line = symbol.selection_range.start.line,
            .column = symbol.selection_range.start.column,
        };
    }

    [[nodiscard]] auto lsp_diagnostic_jump(LspDiagnostic const& diagnostic) -> EditorJump {
        StrRef const name = render_path_leaf(diagnostic.path);
        return {
            .name = name.empty() ? diagnostic.path : name,
            .path = diagnostic.path,
            .line = diagnostic.range.start.line,
            .column = diagnostic.range.start.column,
        };
    }

    [[nodiscard]] auto jump_list_entry(EditorState const& editor, size_t index) -> EditorJump {
        if (editor.jump_list_kind == EditorJumpListKind::LSP_LOCATIONS &&
            editor.lsp_bridge != nullptr && index < editor.lsp_bridge->locations.size()) {
            return lsp_location_jump(editor.lsp_bridge->locations[index]);
        }
        if ((editor.jump_list_kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
             editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) &&
            editor.lsp_bridge != nullptr && index < editor.lsp_bridge->symbols.size()) {
            return lsp_symbol_jump(editor, editor.lsp_bridge->symbols[index]);
        }
        if ((editor.jump_list_kind == EditorJumpListKind::LSP_FILE_DIAGNOSTICS ||
             editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS) &&
            editor.lsp_bridge != nullptr && index < editor.lsp_bridge->diagnostics.size()) {
            return lsp_diagnostic_jump(editor.lsp_bridge->diagnostics[index]);
        }
        if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH &&
            index < editor.global_search_results.size()) {
            GlobalSearchResult const& result = editor.global_search_results[index];
            if (result.tree_file_index < editor.tree_files.size()) {
                FileTreeEntry const& file = editor.tree_files[result.tree_file_index];
                return {
                    .name = file.name,
                    .path = file.path,
                    .line = result.line,
                    .column = result.column,
                };
            }
        }
        if (index < editor.jumps.size()) {
            return editor.jumps[index];
        }
        return {};
    }

    [[nodiscard]] auto
    jump_preview_mark(EditorState const& editor, size_t index, EditorJump const& jump)
        -> SourcePreviewMark {
        SourcePreviewMark mark = {.cursor = {jump.line, jump.column}};
        if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH &&
            index < editor.global_search_results.size()) {
            GlobalSearchResult const& result = editor.global_search_results[index];
            size_t const size = editor_file_search_text(editor).size();
            mark.cursor = {result.line, result.column};
            mark.selection = {
                .start = mark.cursor,
                .end = {result.line, result.column + size},
            };
            mark.selection_active = size != 0u;
        } else if ((editor.jump_list_kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
                    editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) &&
                   editor.lsp_bridge != nullptr && index < editor.lsp_bridge->symbols.size()) {
            mark.selection = editor.lsp_bridge->symbols[index].selection_range;
            mark.selection_active = lsp_position_less(mark.selection.start, mark.selection.end);
            mark.cursor = mark.selection.start;
        } else if ((editor.jump_list_kind == EditorJumpListKind::LSP_FILE_DIAGNOSTICS ||
                    editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS) &&
                   editor.lsp_bridge != nullptr && index < editor.lsp_bridge->diagnostics.size()) {
            mark.selection = editor.lsp_bridge->diagnostics[index].range;
            mark.selection_active = lsp_position_less(mark.selection.start, mark.selection.end);
            mark.cursor = mark.selection.start;
        }
        return mark;
    }

    [[nodiscard]] auto jump_preview_text(
        EditorState& editor,
        Arena& arena,
        EditorJump const& jump,
        StrRef& out_name,
        StrRef& out_text
    ) -> bool {
        if (same_file(jump.name, jump.path, editor.current_file_name, editor.current_file_path)) {
            out_name = editor.current_file_name;
            out_text = text_buffer_copy(editor.text, arena);
            return true;
        }
        OpenFile* const open = find_open_file(editor, jump.name, jump.path);
        if (open != nullptr && open->text_valid) {
            out_name = open->name;
            out_text = open->text;
            return true;
        }
        if (jump.path.empty()) {
            return false;
        }
        out_name = render_path_leaf(jump.path);
        return read_tree_file_display_text(arena, jump.path, out_text);
    }

    auto draw_jump_source_preview(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font editor_font,
        Palette const& palette,
        EditorJump const& jump,
        SourcePreviewMark const& mark,
        float preview_width,
        float preview_height
    ) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef preview_name = {};
        StrRef preview_text = {};
        BASE_UNUSED(jump_preview_text(editor, *temp.arena(), jump, preview_name, preview_text));
        if (preview_text.empty()) {
            ui.label(
                "No preview",
                {
                    .layout = {.width = gui::fill(), .height = gui::px(FILE_SEARCH_ROW_HEIGHT)},
                    .style = {
                        .foreground = palette.muted,
                        .font = editor_font,
                        .font_size = editor.font_size,
                    },
                }
            );
            return;
        }

        float const line_height = editor_line_height(editor);
        size_t const preview_line_limit =
            std::max<size_t>(1u, static_cast<size_t>(preview_height / line_height));
        size_t const start_line = jump.line > 2u ? jump.line - 2u : 0u;
        size_t offset = 0u;
        size_t line_index = 0u;
        while (line_index < start_line && offset < preview_text.size()) {
            BASE_UNUSED(next_text_line(preview_text, offset));
            line_index += 1u;
        }

        SyntaxTokenizer const tokenizer = syntax_tokenizer_for_file_name(preview_name);
        float const char_width =
            std::max(1.0f, font_cache::text_advance(editor_font, editor.font_size, " "));
        size_t const column_limit =
            std::max<size_t>(8u, static_cast<size_t>(preview_width / char_width));
        size_t drawn = 0u;
        while (drawn < preview_line_limit && offset < preview_text.size()) {
            StrRef const line = next_text_line(preview_text, offset);
            if (auto line_row = ui.row(
                    gui::id("jump_list_preview_line", drawn),
                    {
                        .layout = {
                            .width = gui::children(),
                            .height = gui::px(line_height),
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                draw_syntax_label_line(
                    ui,
                    editor_font,
                    tokenizer,
                    palette,
                    line,
                    editor.font_size,
                    line_index,
                    &mark,
                    column_limit
                );
            }
            line_index += 1u;
            drawn += 1u;
        }
    }

    auto draw_jump_list_picker(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font editor_font,
        Palette const& palette,
        float client_width,
        float client_height,
        gui::InputState const& input
    ) -> void {
        float constexpr MAIN_PANEL_MARGIN = 10.0f;
        float constexpr DIALOG_GAP = 8.0f;
        float constexpr PANEL_PADDING = 10.0f;
        float constexpr QUERY_HEIGHT = 36.0f;
        float constexpr SHELL_PADDING = 12.0f;
        float constexpr SHELL_GAP = 10.0f;
        float constexpr HEADER_HEIGHT = 40.0f;
        float constexpr BOTTOM_BAR_HEIGHT = 30.0f;
        float const modal_margin_top =
            SHELL_PADDING + HEADER_HEIGHT + SHELL_GAP + MAIN_PANEL_MARGIN;
        float const modal_margin_bottom =
            SHELL_PADDING + SHELL_GAP + BOTTOM_BAR_HEIGHT + MAIN_PANEL_MARGIN;
        float const desired_margin_x = client_width * 0.1f;
        float const max_margin_x = std::max(20.0f, (client_width - 900.0f) * 0.5f);
        float const modal_margin_x = std::clamp(desired_margin_x, 20.0f, max_margin_x);
        float const dialog_height =
            std::max(180.0f, client_height - modal_margin_top - modal_margin_bottom);
        float const dialog_width = std::max(1.0f, client_width - 2.0f * modal_margin_x);
        float const preview_width = std::max(
            1.0f, (dialog_width - 2.0f * PANEL_PADDING - DIALOG_GAP) * 0.52f - 2.0f * PANEL_PADDING
        );

        bool const global_search = editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH;
        editor.file_search_text_size = cstr_len(editor.file_search_text);
        if (global_search && editor.global_search_refresh_requested) {
            refresh_global_search_results(editor);
        }
        ArenaTemp search_temp = begin_thread_temp_arena();
        JumpListMatch* matches = nullptr;
        size_t match_count = 0u;
        size_t filtered_count = 0u;
        size_t total_count = 0u;

        if (!editor.jump_list_mouse_known) {
            editor.file_search_mouse_pos = input.mouse_pos;
            editor.jump_list_mouse_known = true;
        } else {
            bool const mouse_moved = input.mouse_pos.x != editor.file_search_mouse_pos.x ||
                                     input.mouse_pos.y != editor.file_search_mouse_pos.y;
            if (mouse_moved || input.scroll_delta_y != 0.0f || input.mouse_down[0u]) {
                editor.jump_list_mouse_select = true;
            }
            editor.file_search_mouse_pos = input.mouse_pos;
        }

        if (auto modal = ui.modal(
                gui::id("jump_list_modal"),
                {
                    .layout =
                        {
                            .padding =
                                {
                                    modal_margin_top,
                                    modal_margin_x,
                                    modal_margin_bottom,
                                    modal_margin_x,
                                },
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::START,
                        },
                    .style = {.background = gui::rgba(0, 0, 0, 0)},
                    .debug_name = "jump_list_modal",
                }
            )) {
            if (auto dialog = ui.row(
                    gui::id("jump_list_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(dialog_height),
                                .padding = gui::insets(PANEL_PADDING),
                                .gap = DIALOG_GAP,
                                .align_y = gui::Align::STRETCH,
                            },
                        .style = {
                            .background = gui::color_alpha(palette.panel, 0.94f),
                            .border = gui::color_alpha(palette.cursor, 0.72f),
                            .border_thickness = 2.0f,
                            .radius = 8.0f,
                            .shadow = {
                                .offset = {0.0f, 0.0f},
                                .blur_radius = 26.0f,
                                .spread = 1.0f,
                                .color = gui::color_alpha(palette.cursor, 0.20f),
                            },
                        },
                    }
                )) {
                if (auto left = ui.column(
                        gui::id("jump_list_left"),
                        {
                            .layout = {
                                .width = gui::fill(0.48f),
                                .height = gui::fill(),
                                .align_x = gui::Align::STRETCH,
                                .clip = true,
                            },
                        }
                    )) {
                    if (auto query = ui.row(
                            gui::id("jump_list_query"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(QUERY_HEIGHT),
                                        .padding = gui::insets(0.0f, PANEL_PADDING),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style = {
                                    .background = palette.panel_raised,
                                    .border = palette.border,
                                    .border_thickness = 1.0f,
                                    .radius = 6.0f,
                                },
                            }
                        )) {
                        ui.label(
                            ">",
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {.foreground = palette.cursor, .font = editor_font},
                            }
                        );
                        if (global_search) {
                            ui.label(
                                editor_file_search_text(editor),
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = palette.text,
                                        .font = editor_font,
                                        .font_size = editor.font_size,
                                    },
                                }
                            );
                        } else {
                            gui::Id const input_id = gui::id("jump_list_input");
                            ui.request_focus(input_id);
                            gui::Signal const text_input = ui.input_text(
                                input_id,
                                "",
                                editor.file_search_text,
                                FILE_SEARCH_TEXT_CAPACITY,
                                gui::InputTextDesc{
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::fill(),
                                                    .height = gui::fill(),
                                                    .padding = gui::insets(0.0f),
                                                },
                                            .style =
                                                {
                                                    .background = gui::rgba(0, 0, 0, 0),
                                                    .foreground = palette.text,
                                                    .border = gui::rgba(0, 0, 0, 0),
                                                    .border_thickness = 1.0f,
                                                    .radius = 0.0f,
                                                    .font = editor_font,
                                                    .font_size = editor.font_size,
                                                },
                                        },
                                    .ignore_input_on_focus = true,
                                }
                            );
                            if (text_input.changed) {
                                editor.file_search_text_size = cstr_len(editor.file_search_text);
                                editor.jump_selected = 0u;
                                editor.jump_list_mouse_select = false;
                                editor.jump_list_reveal_selected = true;
                            }
                        }
                        filtered_count = jump_list_filtered_count(editor);
                        total_count = jump_list_total_count(editor);
                        if (filtered_count != 0u) {
                            matches =
                                arena_alloc<JumpListMatch>(*search_temp.arena(), filtered_count);
                            match_count =
                                collect_jump_list_matches(editor, slice(matches, filtered_count));
                        }
                        editor.jump_selected =
                            match_count == 0u ? 0u
                                              : std::min(editor.jump_selected, match_count - 1u);
                        ui.label(
                            fmt::tprintf("%zu/%zu", filtered_count, total_count),
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {
                                    .foreground = palette.text,
                                    .font = editor_font,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                    }

                    gui::Id const results_id = gui::id("jump_list_results");
                    if (editor.jump_list_reveal_selected && match_count != 0u) {
                        ui.scroll_to_index(results_id, editor.jump_selected);
                        editor.jump_list_reveal_selected = false;
                    }
                    if (match_count == 0u) {
                        char const* empty_text =
                            editor.jumps.empty() ? "No jumps recorded" : "No matching jumps";
                        if (editor.jump_list_kind == EditorJumpListKind::LSP_LOCATIONS) {
                            empty_text =
                                total_count == 0u ? "No locations found" : "No matching locations";
                        } else if (editor.jump_list_kind ==
                                       EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
                                   editor.jump_list_kind ==
                                       EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
                            empty_text =
                                total_count == 0u ? "No symbols found" : "No matching symbols";
                        } else if (render_jump_list_is_diagnostics(editor.jump_list_kind)) {
                            empty_text = total_count == 0u ? "No diagnostics found"
                                                           : "No matching diagnostics";
                        } else if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
                            empty_text = "No global search results";
                        }
                        ui.label(
                            empty_text,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                        .padding = gui::insets(0.0f, PANEL_PADDING),
                                    },
                                .style = {
                                    .foreground = palette.muted,
                                    .font = editor_font,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                    } else {
                        auto results = ui.list_fixed(
                            results_id,
                            {
                                .item_count = match_count,
                                .item_height = FILE_SEARCH_ROW_HEIGHT,
                                .box = {
                                    .layout = {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .align_x = gui::Align::STRETCH,
                                    },
                                },
                            }
                        );
                        ArenaTemp row_temp = begin_thread_temp_arena();
                        for (size_t index = results.first; index < results.end; ++index) {
                            bool selected = index == editor.jump_selected;
                            size_t const visible_index = index - results.first;
                            EditorJumpListKind const list_kind = editor.jump_list_kind;
                            EditorJump const jump =
                                jump_list_entry(editor, matches[index].jump_index);
                            if (auto row = results.row(
                                    gui::id("jump_list_result", visible_index),
                                    {
                                        .layout =
                                            {
                                                .padding = gui::insets(0.0f, PANEL_PADDING),
                                                .gap = 8.0f,
                                                .align_y = gui::Align::CENTER,
                                            },
                                        .style = {
                                            .background =
                                                selected ? palette.cursor_line : gui::Color{},
                                            .radius = selected ? 5.0f : -1.0f,
                                        },
                                    }
                                )) {
                                gui::Signal const signal = row.signal();
                                if (signal.hovered && editor.jump_list_mouse_select) {
                                    editor.jump_selected = index;
                                }
                                if (signal.clicked_left) {
                                    editor.jump_selected = index;
                                    if (list_kind == EditorJumpListKind::LSP_LOCATIONS) {
                                        editor.lsp_open_location_index = matches[index].jump_index;
                                    } else if (list_kind ==
                                                   EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
                                               list_kind ==
                                                   EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
                                        editor.lsp_open_symbol_index = matches[index].jump_index;
                                    } else if (render_jump_list_is_diagnostics(list_kind)) {
                                        editor.lsp_open_diagnostic_index =
                                            matches[index].jump_index;
                                    } else if (list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
                                        editor.global_search_open_index = matches[index].jump_index;
                                    } else {
                                        editor.jump_open_index = matches[index].jump_index;
                                    }
                                    close_jump_list(editor);
                                }
                                selected = index == editor.jump_selected;
                                ui.label(
                                    selected ? ">" : "",
                                    {
                                        .layout = {.width = gui::px(14.0f), .height = gui::fill()},
                                        .style = {
                                            .foreground = palette.cursor, .font = editor_font
                                        },
                                    }
                                );
                                gui::Color text_color = selected ? palette.text : palette.muted;
                                StrRef row_text = {};
                                if (list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
                                    row_text = global_search_row_text(
                                        editor,
                                        *row_temp.arena(),
                                        editor.global_search_results[matches[index].jump_index]
                                    );
                                } else if (render_jump_list_is_diagnostics(list_kind) &&
                                           editor.lsp_bridge != nullptr &&
                                           matches[index].jump_index <
                                               editor.lsp_bridge->diagnostics.size()) {
                                    LspDiagnostic const& diagnostic =
                                        editor.lsp_bridge->diagnostics[matches[index].jump_index];
                                    row_text = lsp_diagnostic_row_text(
                                        editor, *row_temp.arena(), diagnostic
                                    );
                                    text_color = lsp_diagnostic_color(palette, diagnostic.severity);
                                } else {
                                    row_text = jump_list_row_text(
                                        *row_temp.arena(),
                                        matches[index].jump_index,
                                        jump,
                                        list_kind,
                                        editor.save_root_path
                                    );
                                }
                                ui.label(
                                    gui::id("jump_list_result_text", visible_index),
                                    row_text,
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::fill()},
                                        .style = {
                                            .foreground = text_color,
                                            .font = editor_font,
                                            .font_size = editor.font_size,
                                        },
                                    }
                                );
                            }
                        }
                    }
                }

                if (auto preview = ui.column(
                        gui::id("jump_list_preview"),
                        {
                            .layout = {
                                .width = gui::fill(0.52f),
                                .height = gui::fill(),
                                .padding = gui::insets(PANEL_PADDING),
                                .clip = true,
                            },
                        }
                    )) {
                    if (match_count != 0u) {
                        size_t const entry_index = matches[editor.jump_selected].jump_index;
                        EditorJump const jump = jump_list_entry(editor, entry_index);
                        draw_jump_source_preview(
                            ui,
                            editor,
                            editor_font,
                            palette,
                            jump,
                            jump_preview_mark(editor, entry_index, jump),
                            preview_width,
                            dialog_height - 2.0f * PANEL_PADDING
                        );
                    }
                }
            }
        }
    }

    struct LspTextMetrics {
        float width = 0.0f;
        size_t lines = 0u;
    };

    inline constexpr float LSP_POPUP_PADDING_X = 10.0f;
    inline constexpr float LSP_POPUP_PADDING_Y = 8.0f;
    inline constexpr float LSP_POPUP_SCROLL_EPSILON = 2.0f;
    inline constexpr size_t LSP_HOVER_MAX_VISIBLE_LINES = 18u;

    [[nodiscard]] auto lsp_text_line_height(font_cache::Font font, float font_size) -> float {
        if (!font_cache::font_valid(font)) {
            return font_size * 1.25f;
        }
        font_provider::Metrics metrics = {};
        font_cache::metrics_from_font(font, font_size, metrics);
        return std::ceil(metrics.ascent + metrics.descent + 4.0f);
    }

    [[nodiscard]] auto
    measure_lsp_text(font_cache::Font font, float font_size, StrRef text, size_t max_lines)
        -> LspTextMetrics {
        LspTextMetrics metrics = {};
        size_t offset = 0u;
        while (offset < text.size() && metrics.lines < max_lines) {
            StrRef const line = next_text_line(text, offset);
            metrics.width =
                std::max(metrics.width, font_cache::text_advance(font, font_size, line));
            metrics.lines += 1u;
        }
        if (!text.empty() && text[text.size() - 1u] == '\n' && metrics.lines < max_lines) {
            metrics.lines += 1u;
        }
        if (metrics.lines == 0u) {
            metrics.lines = 1u;
        }
        return metrics;
    }

    auto draw_lsp_text_lines(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        Palette const& palette,
        draw::Rect panel,
        StrRef text,
        size_t max_lines
    ) -> void {
        draw::Rect const clip = {
            {panel.min.x + LSP_POPUP_PADDING_X, panel.min.y + LSP_POPUP_PADDING_Y},
            {panel.max.x - LSP_POPUP_PADDING_X, panel.max.y - LSP_POPUP_PADDING_Y},
        };
        draw::push_clip_rect(context, clip);
        draw::TextStyle style = {
            .font = font,
            .size = editor.font_size,
            .color = to_draw_color(palette.text),
        };
        float y = clip.min.y;
        float const line_height = lsp_text_line_height(font, editor.font_size);
        size_t offset = 0u;
        size_t line_count = 0u;
        while (offset < text.size() && line_count < max_lines) {
            StrRef const line = next_text_line(text, offset);
            draw::draw_text(context, {clip.min.x, y - 2.0f}, style, line, nullptr);
            y += line_height;
            line_count += 1u;
        }
        if (offset < text.size() && y < clip.max.y) {
            style.color = to_draw_color(palette.muted);
            draw::draw_text(context, {clip.min.x, y - 2.0f}, style, "...", nullptr);
        }
        draw::pop_clip_rect(context);
    }

    [[nodiscard]] auto focused_code_rect(EditorState const& editor, gui::Rect& out_rect) -> bool {
        if (editor.focused_split >= editor.split_nodes.size() ||
            editor_focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return false;
        }
        out_rect = editor.split_nodes[editor.focused_split].rect;
        return out_rect.max.x > out_rect.min.x && out_rect.max.y > out_rect.min.y;
    }

    [[nodiscard]] auto
    cursor_visible_in_rect(EditorState const& editor, gui::Rect rect, float char_width) -> bool {
        gui::Rect const content = editor_content_rect(rect);
        float const line_height = editor_line_height(editor);
        float const x =
            editor_text_x(editor, rect) + char_width * static_cast<float>(editor.cursor_column);
        float const text_min_x = editor_text_x(editor, rect) + editor.scroll_x;
        float const y =
            content.min.y + static_cast<float>(editor.cursor_line) * line_height - editor.scroll_y;
        float const width = editor.flag(EditorFlag::INSERT_MODE) ? 2.0f : char_width;
        return x >= text_min_x && x + width <= content.max.x && y >= content.min.y &&
               y + line_height <= content.max.y;
    }

    [[nodiscard]] auto lsp_anchor_panel(
        EditorState const& editor,
        gui::Rect rect,
        float char_width,
        size_t line,
        size_t column,
        float width,
        float height
    ) -> draw::Rect {
        gui::Rect const content = editor_content_rect(rect);
        float const line_height = editor_line_height(editor);
        float const text_x = editor_text_x(editor, rect);
        float x = text_x + char_width * static_cast<float>(column);
        float const line_top =
            content.min.y + static_cast<float>(line) * line_height - editor.scroll_y;
        float y = line_top + line_height + 3.0f;
        width = std::min(width, std::max(120.0f, content.max.x - content.min.x - 8.0f));
        height = std::min(height, std::max(48.0f, content.max.y - content.min.y - 8.0f));
        x = std::clamp(
            x, content.min.x + 4.0f, std::max(content.min.x + 4.0f, content.max.x - width - 4.0f)
        );
        if (y + height > content.max.y - 4.0f && line_top - height - 3.0f >= content.min.y) {
            y = line_top - height - 3.0f;
        }
        y = std::clamp(
            y, content.min.y + 4.0f, std::max(content.min.y + 4.0f, content.max.y - height - 4.0f)
        );
        return {{x, y}, {x + width, y + height}};
    }

    auto draw_lsp_panel(draw::Context context, Palette const& palette, draw::Rect panel) -> void {
        draw::draw_rect_filled(context, panel, to_draw_color(palette.panel_raised), 5.0f);
        draw::draw_rect(context, panel, to_draw_color(palette.border), 1.0f, 5.0f);
    }

    [[nodiscard]] auto
    lsp_wrapped_line_count(font_cache::Font font, float font_size, StrRef text, float wrap_width)
        -> size_t {
        size_t count = 0u;
        size_t offset = 0u;
        while (offset < text.size()) {
            StrRef const line = next_text_line(text, offset);
            float const width = font_cache::text_advance(font, font_size, line);
            count += std::max<size_t>(
                1u, static_cast<size_t>(std::ceil(width / std::max(1.0f, wrap_width)))
            );
        }
        if (!text.empty() && text[text.size() - 1u] == '\n') {
            count += 1u;
        }
        return std::max<size_t>(1u, count);
    }

    [[nodiscard]] auto
    lsp_hover_panel_rect(font_cache::Font font, EditorState const& editor, float char_width)
        -> draw::Rect {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->hover.text.empty()) {
            return {};
        }
        gui::Rect rect = {};
        if (!focused_code_rect(editor, rect)) {
            return {};
        }

        size_t line = editor.lsp_bridge->hover.range.start.line;
        size_t column = editor.lsp_bridge->hover.range.start.column;
        if (line >= editor_line_count(editor)) {
            line = editor.cursor_line;
            column = editor.cursor_column;
        }
        size_t constexpr MAX_LINES = static_cast<size_t>(-1);
        LspTextMetrics const metrics =
            measure_lsp_text(font, editor.font_size, editor.lsp_bridge->hover.text, MAX_LINES);
        gui::Rect const content = editor_content_rect(rect);
        float const max_width =
            std::max(120.0f, std::min(1040.0f, content.max.x - content.min.x - 8.0f));
        float const min_width = std::min(360.0f, max_width);
        float const width = std::clamp(
            metrics.width + 2.0f * LSP_POPUP_PADDING_X + LSP_POPUP_SCROLL_EPSILON,
            min_width,
            max_width
        );
        float const wrap_width = std::max(1.0f, width - 2.0f * LSP_POPUP_PADDING_X);
        float const line_height = lsp_text_line_height(font, editor.font_size);
        size_t const lines = lsp_wrapped_line_count(
            font, editor.font_size, editor.lsp_bridge->hover.text, wrap_width
        );
        float const height =
            static_cast<float>(std::min(lines, LSP_HOVER_MAX_VISIBLE_LINES)) * line_height +
            2.0f * LSP_POPUP_PADDING_Y + LSP_POPUP_SCROLL_EPSILON;
        draw::Rect const panel =
            lsp_anchor_panel(editor, rect, char_width, line, column, width, height);
        return panel;
    }

    [[nodiscard]] auto lsp_hover_key_action(gui::InputState const& input) -> bool {
        if (input.key_events == nullptr) {
            return false;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEventKind const kind = input.key_events[index].kind;
            if (kind == gui::KeyEventKind::PRESS || kind == gui::KeyEventKind::REPEAT ||
                kind == gui::KeyEventKind::TEXT) {
                return true;
            }
        }
        return false;
    }

    auto draw_lsp_hover_popup(
        gui::Frame& ui,
        font_cache::Font font,
        EditorState& editor,
        float char_width,
        Palette const& palette,
        gui::InputState const& input
    ) -> void {
        if (editor.lsp_popup != EditorLspPopupKind::HOVER || editor.lsp_bridge == nullptr ||
            editor.lsp_bridge->hover.text.empty()) {
            return;
        }

        draw::Rect const panel = lsp_hover_panel_rect(font, editor, char_width);
        float const width = panel.max.x - panel.min.x;
        float const height = panel.max.y - panel.min.y;
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }
        gui::Rect const popup_rect = {{panel.min.x, panel.min.y}, {panel.max.x, panel.max.y}};

        if (auto popup = ui.popup(
                gui::id("lsp_hover_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(width),
                            .height = gui::px(height),
                            .margin = gui::insets(panel.min.y, 0.0f, 0.0f, panel.min.x),
                            .padding = gui::insets(LSP_POPUP_PADDING_Y, LSP_POPUP_PADDING_X),
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 5.0f,
                        },
                    .debug_name = "lsp_hover_popup",
                }
            )) {
            gui::Signal const label = ui.selectable_label(
                gui::id("lsp_hover_text"),
                editor.lsp_bridge->hover.text,
                &editor.lsp_hover_selection,
                {
                    .layout = {.width = gui::fill(), .height = gui::fill(), .word_wrap = true},
                    .style = {
                        .background = gui::rgba(0, 0, 0, 0),
                        .foreground = palette.text,
                        .font = font,
                        .font_size = editor.font_size,
                    },
                }
            );
            bool const mouse_down =
                input.mouse_down[0u] || input.mouse_down[1u] || input.mouse_down[2u];
            if (lsp_hover_key_action(input) ||
                (mouse_down && !label.active && !point_in_rect(popup_rect, input.mouse_pos))) {
                close_editor_lsp_popup(editor);
            }
        }
    }

    auto draw_lsp_diagnostic_overlay(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        float char_width,
        Palette const& palette
    ) -> void {
        LspDiagnostic const* const diagnostic = lsp_diagnostic_at_cursor(editor);
        if (diagnostic == nullptr) {
            return;
        }
        gui::Rect rect = {};
        if (!focused_code_rect(editor, rect)) {
            return;
        }
        if (!cursor_visible_in_rect(editor, rect, char_width)) {
            return;
        }

        size_t constexpr MAX_LINES = static_cast<size_t>(-1);
        LspTextMetrics const metrics =
            measure_lsp_text(font, editor.font_size, diagnostic->message, MAX_LINES);
        float const line_height = lsp_text_line_height(font, editor.font_size);
        draw::Rect const panel = lsp_anchor_panel(
            editor,
            rect,
            char_width,
            editor.cursor_line,
            editor.cursor_column,
            std::clamp(metrics.width + 22.0f, 240.0f, 720.0f),
            static_cast<float>(metrics.lines) * line_height + 18.0f
        );
        draw_lsp_panel(context, palette, panel);
        draw_lsp_text_lines(context, font, editor, palette, panel, diagnostic->message, MAX_LINES);
    }

    [[nodiscard]] auto lsp_overlay_count(EditorState const& editor) -> size_t {
        if (editor.lsp_bridge == nullptr) {
            return 0u;
        }
        switch (editor.lsp_popup) {
        case EditorLspPopupKind::COMPLETION:
            return editor.lsp_bridge->completions.size();
        case EditorLspPopupKind::LOCATIONS:
            return editor.lsp_bridge->locations.size();
        case EditorLspPopupKind::CODE_ACTIONS:
            return editor.lsp_bridge->code_actions.size();
        case EditorLspPopupKind::SYMBOLS:
            return editor.lsp_bridge->symbols.size();
        default:
            return 0u;
        }
    }

    [[nodiscard]] auto lsp_overlay_title(EditorLspPopupKind popup) -> StrRef {
        switch (popup) {
        case EditorLspPopupKind::LOCATIONS:
            return "Locations";
        case EditorLspPopupKind::CODE_ACTIONS:
            return "Code actions";
        case EditorLspPopupKind::SYMBOLS:
            return "Symbols";
        default:
            return "LSP";
        }
    }

    auto draw_lsp_row(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        Palette const& palette,
        draw::Rect row,
        StrRef text,
        StrRef detail,
        bool selected
    ) -> void {
        if (selected) {
            draw::draw_rect_filled(
                context, row, to_draw_color(gui::color_alpha(palette.cursor_line, 0.92f)), 3.0f
            );
        }
        draw::TextStyle style = {
            .font = font,
            .size = editor.font_size,
            .color = to_draw_color(selected ? palette.text : palette.muted),
        };
        draw::Rect const clip = {{row.min.x + 9.0f, row.min.y}, {row.max.x - 9.0f, row.max.y}};
        draw::push_clip_rect(context, clip);
        draw::draw_text(
            context, {clip.min.x, row.min.y + 4.0f}, style, selected ? ">" : "", nullptr
        );
        style.color = to_draw_color(palette.text);
        draw::draw_text(context, {clip.min.x + 18.0f, row.min.y + 4.0f}, style, text, nullptr);
        if (!detail.empty()) {
            style.color = to_draw_color(palette.muted);
            draw::draw_text(
                context,
                {row.min.x + std::max(260.0f, (row.max.x - row.min.x) * 0.58f), row.min.y + 4.0f},
                style,
                detail,
                nullptr
            );
        }
        draw::pop_clip_rect(context);
    }

    auto draw_lsp_completion_overlay(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        float char_width,
        Palette const& palette
    ) -> void {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->completions.empty()) {
            return;
        }
        gui::Rect rect = {};
        if (!focused_code_rect(editor, rect)) {
            return;
        }

        float constexpr ROW_HEIGHT = 28.0f;
        size_t const count = editor.lsp_bridge->completions.size();
        size_t const rows = std::min<size_t>(count, 9u);
        size_t const first = editor.lsp_selected >= rows ? editor.lsp_selected + 1u - rows : 0u;
        draw::Rect const panel = lsp_anchor_panel(
            editor,
            rect,
            char_width,
            editor.cursor_line,
            editor.cursor_column,
            560.0f,
            ROW_HEIGHT * static_cast<float>(std::max<size_t>(1u, rows)) + 8.0f
        );
        draw_lsp_panel(context, palette, panel);
        draw::push_clip_rect(
            context,
            {{panel.min.x + 4.0f, panel.min.y + 4.0f}, {panel.max.x - 4.0f, panel.max.y - 4.0f}}
        );
        for (size_t row_index = 0u; row_index < rows; ++row_index) {
            size_t const index = first + row_index;
            LspCompletionItem const& item = editor.lsp_bridge->completions[index];
            draw::Rect const row = {
                {panel.min.x + 4.0f,
                 panel.min.y + 4.0f + ROW_HEIGHT * static_cast<float>(row_index)},
                {panel.max.x - 4.0f,
                 panel.min.y + 4.0f + ROW_HEIGHT * static_cast<float>(row_index + 1u)},
            };
            draw_lsp_row(
                context,
                font,
                editor,
                palette,
                row,
                item.label,
                item.detail,
                index == editor.lsp_selected
            );
        }
        draw::pop_clip_rect(context);
    }

    auto draw_lsp_center_overlay(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        gui::Frame const& ui,
        Palette const& palette
    ) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        size_t const count = lsp_overlay_count(editor);
        if (count == 0u) {
            return;
        }

        gui::BoxInfo const* const body = ui.find_box(gui::id("body"), gui::BoxKind::ROW);
        if (body == nullptr) {
            return;
        }

        float constexpr ROW_HEIGHT = 28.0f;
        float const body_width = body->rect.max.x - body->rect.min.x;
        float const body_height = body->rect.max.y - body->rect.min.y;
        size_t const rows = std::min<size_t>(count, 12u);
        size_t const first = editor.lsp_selected >= rows ? editor.lsp_selected + 1u - rows : 0u;
        float const width = std::min(860.0f, std::max(280.0f, body_width - 48.0f));
        float const height = 44.0f + ROW_HEIGHT * static_cast<float>(rows) + 12.0f;
        float const x = body->rect.min.x + std::max(0.0f, (body_width - width) * 0.5f);
        float const y = body->rect.min.y + std::max(0.0f, (body_height - height) * 0.5f);
        draw::Rect const panel = {{x, y}, {x + width, y + height}};
        draw_lsp_panel(context, palette, panel);

        draw::TextStyle style = {
            .font = font,
            .size = editor.font_size,
            .color = to_draw_color(palette.text),
        };
        draw::draw_text(
            context,
            {panel.min.x + 12.0f, panel.min.y + 10.0f},
            style,
            lsp_overlay_title(editor.lsp_popup),
            nullptr
        );

        draw::Rect const list_clip = {
            {panel.min.x + 8.0f, panel.min.y + 40.0f},
            {panel.max.x - 8.0f, panel.max.y - 8.0f},
        };
        draw::push_clip_rect(context, list_clip);
        ArenaTemp temp = begin_thread_temp_arena();
        for (size_t row_index = 0u; row_index < rows; ++row_index) {
            size_t const index = first + row_index;
            StrRef text = {};
            StrRef detail = {};
            if (editor.lsp_popup == EditorLspPopupKind::LOCATIONS) {
                text = lsp_location_name(*temp.arena(), editor.lsp_bridge->locations[index]);
            } else if (editor.lsp_popup == EditorLspPopupKind::CODE_ACTIONS) {
                LspCodeAction const& action = editor.lsp_bridge->code_actions[index];
                text = action.title;
                detail = action.kind;
            } else if (editor.lsp_popup == EditorLspPopupKind::SYMBOLS) {
                LspDocumentSymbol const& symbol = editor.lsp_bridge->symbols[index];
                text = fmt::tprintf("%s:%zu", symbol.name, symbol.selection_range.start.line + 1u);
                detail = symbol.detail;
            }
            draw::Rect const row = {
                {list_clip.min.x, list_clip.min.y + ROW_HEIGHT * static_cast<float>(row_index)},
                {list_clip.max.x,
                 list_clip.min.y + ROW_HEIGHT * static_cast<float>(row_index + 1u)},
            };
            draw_lsp_row(
                context, font, editor, palette, row, text, detail, index == editor.lsp_selected
            );
        }
        draw::pop_clip_rect(context);
    }

    auto draw_lsp_overlay(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        float char_width,
        gui::Frame const& ui,
        Palette const& palette
    ) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        if (editor.lsp_popup == EditorLspPopupKind::NONE) {
            draw_lsp_diagnostic_overlay(context, font, editor, char_width, palette);
            return;
        }
        if (editor.lsp_popup == EditorLspPopupKind::HOVER) {
            return;
        }
        if (editor.lsp_popup == EditorLspPopupKind::COMPLETION) {
            draw_lsp_completion_overlay(context, font, editor, char_width, palette);
        } else {
            draw_lsp_center_overlay(context, font, editor, ui, palette);
        }
    }

    [[nodiscard]] auto key_pressed(gui::InputState const& input, gui::Key key, bool repeat = false)
        -> bool {
        if (input.key_events == nullptr) {
            return false;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.key == key && (event.kind == gui::KeyEventKind::PRESS ||
                                     (repeat && event.kind == gui::KeyEventKind::REPEAT))) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto shortcut_pressed(gui::InputState const& input, gui::Key key) -> bool {
        if (input.key_events == nullptr) {
            return false;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.kind == gui::KeyEventKind::PRESS && event.key == key &&
                (event.mods & gui::KEY_MOD_CTRL) != 0u &&
                (event.mods & (gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto save_path_error_text(EditorSavePathError error) -> StrRef {
        switch (error) {
        case EditorSavePathError::EMPTY:
            return "Enter a file path.";
        case EditorSavePathError::EXISTS:
            return "A file already exists at that path.";
        case EditorSavePathError::WRITE_FAILED:
            return "Could not save to that path.";
        case EditorSavePathError::NONE:
        default:
            return {};
        }
    }

    [[nodiscard]] auto lsp_rename_popup_rect(
        EditorState const& editor, float char_width, float client_width, float client_height
    ) -> gui::Rect {
        gui::Rect code_rect = {};
        if (!focused_code_rect(editor, code_rect)) {
            return {{24.0f, 72.0f}, {std::min(client_width - 24.0f, 544.0f), 108.0f}};
        }

        gui::Rect const content = editor_content_rect(code_rect);
        float const line_height = editor_line_height(editor);
        float const height = 36.0f;
        float width = std::min(520.0f, std::max(240.0f, content.max.x - content.min.x - 8.0f));
        width = std::min(width, std::max(120.0f, client_width - 48.0f));

        float x = editor_text_x(editor, code_rect) +
                  char_width * static_cast<float>(editor.cursor_column);
        x = std::clamp(
            x, content.min.x + 4.0f, std::max(content.min.x + 4.0f, content.max.x - width - 4.0f)
        );

        float const line_top =
            content.min.y + static_cast<float>(editor.cursor_line) * line_height - editor.scroll_y;
        float y = line_top - height - 4.0f;
        if (y < content.min.y + 4.0f) {
            y = line_top + line_height + 4.0f;
        }
        y = std::clamp(
            y, content.min.y + 4.0f, std::max(content.min.y + 4.0f, content.max.y - height - 4.0f)
        );
        y = std::min(y, std::max(4.0f, client_height - height - 4.0f));
        return {{x, y}, {x + width, y + height}};
    }

    auto draw_lsp_rename_popup(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        float char_width,
        float client_width,
        float client_height
    ) -> void {
        if (editor.lsp_popup != EditorLspPopupKind::RENAME) {
            return;
        }

        gui::Rect const popup_rect =
            lsp_rename_popup_rect(editor, char_width, client_width, client_height);
        bool submit = false;
        if (auto popup = ui.popup(
                gui::id("lsp_rename_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(popup_rect.max.x - popup_rect.min.x),
                            .height = gui::px(popup_rect.max.y - popup_rect.min.y),
                            .margin = gui::insets(popup_rect.min.y, 0.0f, 0.0f, popup_rect.min.x),
                            .padding = gui::insets(0.0f, 10.0f),
                            .align_x = gui::Align::STRETCH,
                            .align_y = gui::Align::CENTER,
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .border = palette.cursor,
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                        },
                    .debug_name = "lsp_rename_popup",
                }
            )) {
            if (auto row = ui.row(
                    gui::id("lsp_rename_row"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .gap = 6.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                ui.label(
                    ">",
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {.foreground = palette.cursor},
                    }
                );
                gui::Id const input_id = gui::id("lsp_rename_input");
                if (editor.lsp_rename_text_selected) {
                    ui.clear_focus();
                }
                ui.request_focus(input_id);
                gui::Signal const input = ui.input_text(
                    input_id,
                    "",
                    editor.lsp_rename_text,
                    LSP_RENAME_TEXT_CAPACITY,
                    gui::InputTextDesc{
                        .box =
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .padding = gui::insets(0.0f),
                                    },
                                .style =
                                    {
                                        .background = gui::rgba(0, 0, 0, 0),
                                        .foreground = palette.text,
                                        .border = gui::rgba(0, 0, 0, 0),
                                        .border_thickness = 1.0f,
                                        .radius = 0.0f,
                                        .font_size = editor.font_size,
                                    },
                            },
                        .select_all_on_focus = editor.lsp_rename_text_selected,
                        .ignore_input_on_focus = true,
                    }
                );
                if (input.focused || input.changed) {
                    editor.lsp_rename_text_selected = false;
                }
                if (input.changed) {
                    editor.lsp_rename_text_size = cstr_len(editor.lsp_rename_text);
                }
                submit = input.activated;
            }
        }

        if (submit) {
            editor.lsp_rename_text_size = cstr_len(editor.lsp_rename_text);
            accept_lsp_popup(editor);
        }
    }

    auto draw_save_path_picker(
        gui::Frame& ui, EditorState& editor, Palette const& palette, gui::InputState const& input
    ) -> void {
        if (!editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }

        bool save = shortcut_pressed(input, gui::Key::S);
        bool cancel = key_pressed(input, gui::Key::ESCAPE);
        if (auto modal = ui.modal(
                gui::id("save_path_modal"),
                {
                    .layout =
                        {
                            .padding = gui::insets(42.0f, 24.0f, 24.0f, 24.0f),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::START,
                        },
                    .debug_name = "save_path_modal",
                }
            )) {
            if (auto dialog = ui.column(
                    gui::id("save_path_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::children(),
                                .max_width = gui::px(860.0f),
                                .padding = gui::insets(8.0f),
                                .gap = 8.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                        },
                    }
                )) {
                ui.label(
                    "Save file",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(28.0f),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {.foreground = palette.text},
                    }
                );

                ui.request_focus(gui::id("save_path_input"));
                gui::Signal const path = ui.input_text(
                    gui::id("save_path_input"),
                    "",
                    editor.save_path_text,
                    SAVE_PATH_TEXT_CAPACITY,
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(36.0f),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {
                            .background = palette.panel_raised,
                            .foreground = palette.text,
                            .border = editor.save_path_error == EditorSavePathError::NONE
                                          ? palette.cursor
                                          : palette.preprocessor,
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                        },
                    }
                );
                if (path.changed) {
                    editor.save_path_error = EditorSavePathError::NONE;
                }
                save = save || path.activated;

                StrRef const error = save_path_error_text(editor.save_path_error);
                ui.label(
                    error,
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(24.0f),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {
                            .foreground = error.empty() ? gui::Color{} : palette.preprocessor,
                            .font_size = editor.font_size,
                        },
                    }
                );

                if (auto buttons = ui.row(
                        gui::id("save_path_buttons"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(34.0f),
                                .gap = 8.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    gui::BoxDesc const button_desc = {
                        .layout =
                            {
                                .width = gui::px(92.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 12.0f),
                            },
                        .style = {
                            .background = palette.panel_raised,
                            .foreground = palette.text,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                            .font_size = editor.font_size,
                        },
                    };
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                    cancel =
                        ui.button(gui::id("save_path_cancel"), "Cancel", button_desc).activated ||
                        cancel;
                    save =
                        ui.button(gui::id("save_path_save"), "Save", button_desc).activated || save;
                }
            }
        }

        if (cancel) {
            editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);
            editor.close_intent = EditorCloseIntent::NONE;
            close_save_path_popup(editor);
        } else if (save) {
            EditorCloseIntent const close_intent = editor.close_intent;
            if (save_path_from_popup(editor) && close_intent != EditorCloseIntent::NONE) {
                continue_after_saved_close(editor, close_intent);
            } else if (!editor.flag(EditorFlag::SAVE_PATH_OPEN) &&
                       editor.flag(EditorFlag::WRITE_QUIT_REQUESTED)) {
                editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);
                close_current_file(editor);
            }
        }
    }

    struct OpenFileTabSignal {
        bool selected = false;
        bool closed = false;
    };

    [[nodiscard]] auto open_file_tab_label_id(size_t index) -> gui::Id {
        return gui::id("open_file_tab_label", index);
    }

    [[nodiscard]] auto draw_open_file_tab(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        OpenFile const& file,
        size_t index
    ) -> OpenFileTabSignal {
        OpenFileTabSignal result = {};
        bool const selected = open_file_selected(editor, file);
        bool const deleted = open_file_deleted(editor, file);
        if (auto tab = ui.row(
                gui::id("open_file_tab", index),
                {
                    .layout =
                        {
                            .width = gui::children(),
                            .height = gui::px(OPEN_TAB_HEIGHT),
                            .padding = gui::insets(0.0f, OPEN_TAB_PADDING),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {
                        .background = selected ? palette.panel_raised : palette.panel,
                        .border = gui::color_alpha(palette.text, selected ? 0.38f : 0.22f),
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                    },
                }
            )) {
            StrRef const label =
                open_file_dirty(editor, file) ? fmt::tprintf("*%s", file.name) : file.name;
            ui.label(
                open_file_tab_label_id(index),
                label,
                {
                    .layout = {.width = gui::text(), .height = gui::fill()},
                    .style = {
                        .foreground = deleted    ? palette.preprocessor
                                      : selected ? palette.text
                                                 : palette.muted,
                        .font_size = editor.font_size,
                    },
                }
            );
            if (auto close = ui.row(
                    gui::id("open_file_tab_close", index),
                    {
                        .layout = {
                            .width = gui::px(OPEN_TAB_CLOSE_SIZE),
                            .height = gui::px(OPEN_TAB_CLOSE_SIZE),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                result.closed = close.signal().clicked_left;
                ui.label(
                    "x",
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font_size = editor.font_size,
                        },
                    }
                );
            }
            result.selected = !result.closed && !selected && tab.signal().clicked_left;
        }
        return result;
    }

    auto draw_deleted_open_file_tab_marks(
        draw::Context draw_context,
        gui::Frame const& ui,
        EditorState const& editor,
        Palette const& palette
    ) -> void {
        gui::BoxInfo const* const tabs =
            ui.find_box(gui::id("open_file_tabs"), gui::BoxKind::SCROLL_PANEL);
        if (tabs == nullptr) {
            return;
        }
        draw::push_clip_rect(
            draw_context,
            {{tabs->rect.min.x, tabs->rect.min.y}, {tabs->rect.max.x, tabs->rect.max.y}}
        );
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            if (!open_file_deleted(editor, editor.open_files[index])) {
                continue;
            }
            gui::BoxInfo const* const label =
                ui.find_box(open_file_tab_label_id(index), gui::BoxKind::LABEL);
            if (label == nullptr) {
                continue;
            }
            float const y = std::round((label->rect.min.y + label->rect.max.y) * 0.5f + 1.0f);
            draw::draw_line(
                draw_context,
                {label->rect.min.x, y},
                {label->rect.max.x, y},
                to_draw_color(palette.preprocessor),
                1.4f
            );
        }
        draw::pop_clip_rect(draw_context);
    }

    [[nodiscard]] auto file_change_key_pressed(gui::InputState const& input, char key) -> bool {
        if (input.key_events == nullptr) {
            return false;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.kind != gui::KeyEventKind::TEXT ||
                (event.mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) != 0u) {
                continue;
            }
            if (to_ascii_lower(static_cast<char>(event.codepoint)) == key) {
                return true;
            }
        }
        return false;
    }

    auto draw_unsaved_close_popup(
        gui::Frame& ui, EditorState& editor, Palette const& palette, gui::InputState const& input
    ) -> void {
        if (editor.close_intent == EditorCloseIntent::NONE ||
            editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }

        bool discard = file_change_key_pressed(input, CLOSE_WITHOUT_SAVE_KEY);
        bool save = file_change_key_pressed(input, SAVE_CHANGES_KEY);
        if (auto popup = ui.popup(
                gui::id("unsaved_close_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(500.0f),
                            .height = gui::children(),
                            .padding = gui::insets(18.0f),
                            .gap = 14.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .border = gui::color_alpha(palette.preprocessor, 0.72f),
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 14.0f},
                                    .blur_radius = 34.0f,
                                    .spread = 2.0f,
                                    .color = gui::rgba(0, 0, 0, 120),
                                },
                        },
                    .debug_name = "unsaved_close_popup",
                }
            )) {
            ui.label(
                "Unsaved changes",
                {
                    .layout = {.width = gui::fill(), .height = gui::px(26.0f)},
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
            ui.label(
                fmt::tprintf("%s has unsaved changes.", editor.current_file_name),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(42.0f),
                            .word_wrap = true,
                        },
                    .style = {.foreground = palette.muted, .font_size = editor.font_size},
                }
            );
            if (auto buttons = ui.row(
                    gui::id("unsaved_close_buttons"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(38.0f),
                            .gap = 10.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                gui::BoxDesc const button_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                        },
                    .style = {
                        .background = palette.panel,
                        .foreground = palette.text,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                        .font_size = editor.font_size,
                    },
                };
                discard =
                    ui.button(
                          gui::id("unsaved_close_discard"), "[C] Close without changes", button_desc
                    )
                        .activated ||
                    discard;
                save = ui.button(gui::id("unsaved_close_save"), "[S] Save changes", button_desc)
                           .activated ||
                       save;
            }
        }

        EditorCloseIntent const intent = editor.close_intent;
        if (discard) {
            editor.close_intent = EditorCloseIntent::NONE;
            close_current_file(editor, true);
            if (intent == EditorCloseIntent::APP) {
                request_close_app(editor);
            }
        } else if (save && editor.current_file_path.empty()) {
            open_save_path_popup(editor);
        } else if (save) {
            if (!overwrite_current_file_to_disk(editor)) {
                fmt::eprintf("code_editor: failed to write %s\n", editor.current_file_path);
                return;
            }
            continue_after_saved_close(editor, intent);
        }
    }

    auto draw_file_deleted_popup(gui::Frame& ui, EditorState& editor, Palette const& palette)
        -> void {
        if (!editor.flag(EditorFlag::FILE_DELETED_ON_DISK) ||
            editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }

        bool close = false;
        bool save_as = false;
        if (auto popup = ui.popup(
                gui::id("file_deleted_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(500.0f),
                            .height = gui::children(),
                            .padding = gui::insets(18.0f),
                            .gap = 14.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .border = gui::color_alpha(palette.preprocessor, 0.72f),
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 14.0f},
                                    .blur_radius = 34.0f,
                                    .spread = 2.0f,
                                    .color = gui::rgba(0, 0, 0, 120),
                                },
                        },
                    .debug_name = "file_deleted_popup",
                }
            )) {
            ui.label(
                "File deleted on disk",
                {
                    .layout = {.width = gui::fill(), .height = gui::px(26.0f)},
                    .style = {
                        .foreground = palette.text,
                        .font_size = editor.font_size,
                    },
                }
            );
            ui.label(
                fmt::tprintf(
                    "%s was deleted on disk.\nClose this buffer or save it as a new file.",
                    editor.current_file_name
                ),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(42.0f),
                            .word_wrap = true,
                        },
                    .style = {
                        .foreground = palette.muted,
                        .font_size = editor.font_size,
                    },
                }
            );
            if (auto buttons = ui.row(
                    gui::id("file_deleted_buttons"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(38.0f),
                            .gap = 10.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                gui::BoxDesc const button_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                        },
                    .style = {
                        .background = palette.panel,
                        .foreground = palette.text,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                        .font_size = editor.font_size,
                    },
                };
                close = ui.button(gui::id("file_deleted_close"), "Close buffer", button_desc)
                            .activated ||
                        close;
                save_as =
                    ui.button(gui::id("file_deleted_save_as"), "Save as new file", button_desc)
                        .activated ||
                    save_as;
            }
        }

        if (close) {
            close_current_file(editor);
        } else if (save_as) {
            open_save_path_popup(editor);
        }
    }

    auto draw_file_change_popup(
        gui::Frame& ui, EditorState& editor, Palette const& palette, gui::InputState const& input
    ) -> void {
        if (!editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING) ||
            editor.flag(EditorFlag::FILE_DELETED_ON_DISK)) {
            return;
        }

        bool overwrite = file_change_key_pressed(input, OVERWRITE_FILE_KEY);
        bool reload = file_change_key_pressed(input, RELOAD_FILE_KEY);
        if (auto popup = ui.popup(
                gui::id("file_change_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(500.0f),
                            .height = gui::children(),
                            .padding = gui::insets(18.0f),
                            .gap = 14.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .border = gui::color_alpha(palette.cursor, 0.72f),
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 14.0f},
                                    .blur_radius = 34.0f,
                                    .spread = 2.0f,
                                    .color = gui::rgba(0, 0, 0, 120),
                                },
                        },
                    .debug_name = "file_change_popup",
                }
            )) {
            ui.label(
                "External edit detected",
                {
                    .layout = {.width = gui::fill(), .height = gui::px(26.0f)},
                    .style = {
                        .foreground = palette.text,
                        .font_size = editor.font_size,
                    },
                }
            );
            ui.label(
                fmt::tprintf(
                    "%s changed on disk while this buffer has unsaved edits.\nChoose which copy to "
                    "keep.",
                    editor.current_file_name
                ),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(42.0f),
                            .word_wrap = true,
                        },
                    .style = {
                        .foreground = palette.muted,
                        .font_size = editor.font_size,
                    },
                }
            );
            if (auto buttons = ui.row(
                    gui::id("file_change_buttons"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(38.0f),
                            .gap = 10.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                gui::BoxDesc const overwrite_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                        },
                    .style = {
                        .background = gui::color_alpha(palette.preprocessor, 0.18f),
                        .foreground = palette.text,
                        .border = gui::color_alpha(palette.preprocessor, 0.55f),
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                        .font_size = editor.font_size,
                    },
                };
                gui::BoxDesc const reload_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                        },
                    .style = {
                        .background = gui::color_alpha(palette.cursor, 0.22f),
                        .foreground = palette.text,
                        .border = gui::color_alpha(palette.cursor, 0.72f),
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                        .font_size = editor.font_size,
                    },
                };
                overwrite =
                    ui.button(
                          gui::id("file_change_overwrite"), "[O] Overwrite disk", overwrite_desc
                    )
                        .activated ||
                    overwrite;
                reload = ui.button(gui::id("file_change_reload"), "[R] Reload buffer", reload_desc)
                             .activated ||
                         reload;
            }
        }

        if (overwrite && !overwrite_current_file_to_disk(editor)) {
            fmt::eprintf("code_editor: failed to write %s\n", editor.current_file_path);
        } else if (reload && !reload_current_file_from_disk(editor)) {
            fmt::eprintf("code_editor: failed to reload %s\n", editor.current_file_path);
        }
    }

    auto draw_editor_split_resizer(
        gui::Frame& ui,
        EditorState& editor,
        gui::InputState const& input,
        size_t split,
        EditorSplitKind kind
    ) -> void {
        gui::BoxDesc const desc = {
            .layout = {
                .width =
                    kind == EditorSplitKind::VERTICAL ? gui::px(EDITOR_SPLIT_GAP) : gui::fill(),
                .height =
                    kind == EditorSplitKind::VERTICAL ? gui::fill() : gui::px(EDITOR_SPLIT_GAP),
            },
        };
        if (auto resizer = ui.row(editor_split_resizer_id(split), desc)) {
            gui::Signal const signal = resizer.signal();
            if (!signal.active || !input.mouse_down[0u] || split >= editor.split_nodes.size()) {
                return;
            }
            gui::Rect const rect = editor.split_nodes[split].rect;
            float const span = kind == EditorSplitKind::VERTICAL ? rect.max.x - rect.min.x
                                                                 : rect.max.y - rect.min.y;
            float const available = std::max(1.0f, span - EDITOR_SPLIT_GAP);
            float const mouse = kind == EditorSplitKind::VERTICAL ? input.mouse_pos.x - rect.min.x
                                                                  : input.mouse_pos.y - rect.min.y;
            editor.split_nodes[split].ratio = std::clamp(
                (mouse - EDITOR_SPLIT_GAP * 0.5f) / available,
                EDITOR_SPLIT_MIN_RATIO,
                EDITOR_SPLIT_MAX_RATIO
            );
            if (kind == EditorSplitKind::VERTICAL &&
                editor_split_pane_kind(editor, editor.split_nodes[split].first) ==
                    EditorPaneKind::FILESYSTEM) {
                editor.sidebar_width_percent = editor.split_nodes[split].ratio;
            }
        }
    }

    auto draw_editor_split_ui(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        gui::InputState const& input,
        size_t split,
        gui::Size width,
        gui::Size height,
        bool selection_visible
    ) -> void {
        if (split >= editor.split_nodes.size()) {
            return;
        }

        EditorSplitNode const node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            if (editor_split_pane_kind(editor, split) == EditorPaneKind::FILESYSTEM) {
                draw_filesystem_panel(
                    ui, editor, icon_font, palette, split, width, height, selection_visible
                );
                return;
            }
            bool const focused = selection_visible && split == editor.focused_split;
            if (auto editor_panel = ui.row(
                    editor_surface_id(split),
                    {
                        .layout =
                            {
                                .width = width,
                                .height = height,
                                .align_x = gui::Align::CENTER,
                                .align_y = gui::Align::CENTER,
                                .clip = true,
                            },
                        .style =
                            {
                                .background = palette.panel,
                                .border = focused ? palette.cursor : palette.border,
                                .border_thickness = focused ? 2.0f : 1.0f,
                                .radius = 8.0f,
                            },
                        .debug_name = "editor_surface",
                    }
                )) {
                if (editor_panel.signal().pressed_left) {
                    focus_editor_split(editor, split);
                }
                if (focused) {
                    if (editor.close_intent != EditorCloseIntent::NONE) {
                        draw_unsaved_close_popup(ui, editor, palette, input);
                    } else {
                        draw_file_deleted_popup(ui, editor, palette);
                        draw_file_change_popup(ui, editor, palette, input);
                    }
                }
            }
            return;
        }

        gui::BoxDesc const desc = {
            .layout = {
                .width = width,
                .height = height,
                .gap = 0.0f,
                .align_x = gui::Align::STRETCH,
                .align_y = gui::Align::STRETCH,
            },
        };
        float const ratio = std::clamp(
            editor.split_nodes[split].ratio, EDITOR_SPLIT_MIN_RATIO, EDITOR_SPLIT_MAX_RATIO
        );
        if (node.kind == EditorSplitKind::VERTICAL) {
            if (auto row = ui.row(editor_split_id(split), desc)) {
                draw_editor_split_ui(
                    ui,
                    editor,
                    icon_font,
                    palette,
                    input,
                    node.first,
                    gui::fill(ratio),
                    gui::fill(),
                    selection_visible
                );
                draw_editor_split_resizer(ui, editor, input, split, node.kind);
                draw_editor_split_ui(
                    ui,
                    editor,
                    icon_font,
                    palette,
                    input,
                    node.second,
                    gui::fill(1.0f - ratio),
                    gui::fill(),
                    selection_visible
                );
            }
        } else if (auto column = ui.column(editor_split_id(split), desc)) {
            draw_editor_split_ui(
                ui,
                editor,
                icon_font,
                palette,
                input,
                node.first,
                gui::fill(),
                gui::fill(ratio),
                selection_visible
            );
            draw_editor_split_resizer(ui, editor, input, split, node.kind);
            draw_editor_split_ui(
                ui,
                editor,
                icon_font,
                palette,
                input,
                node.second,
                gui::fill(),
                gui::fill(1.0f - ratio),
                selection_visible
            );
        }
    }

    auto draw_editor_ui(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font editor_font,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width,
        float client_height,
        float char_width,
        gui::InputState const& input
    ) -> void {
        sync_lsp_result_popups(editor);
        handle_lsp_pending_actions(editor);
        bool const picker_open = editor.flag(EditorFlag::FILE_SEARCH_OPEN) ||
                                 editor.flag(EditorFlag::BUFFER_SEARCH_OPEN) ||
                                 editor.flag(EditorFlag::JUMP_LIST_OPEN);
        if (editor.flag(EditorFlag::SIDEBAR_VISIBLE)) {
            ensure_filesystem_panel(editor);
        }
        update_sidebar_resize(editor, client_width, input);
        if (editor.flag(EditorFlag::NEW_SCRATCH_REQUESTED)) {
            open_new_scratch_file(editor);
            editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, false);
        }
        if (editor.flag(EditorFlag::CLOSE_CURRENT_REQUESTED)) {
            request_close_current_file(
                editor, editor.flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED)
            );
            editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, false);
            editor.set_flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED, false);
        }
        if (editor.flag(EditorFlag::CLOSE_APP_REQUESTED)) {
            request_close_app(editor);
        }
        if (editor.file_search_open_file != FILE_SEARCH_NO_FILE) {
            size_t const tree_file_index = editor.file_search_open_file;
            editor.file_search_open_file = FILE_SEARCH_NO_FILE;
            open_file_search_match(editor, tree_file_index);
        }
        if (editor.buffer_search_open_file != FILE_SEARCH_NO_FILE) {
            size_t const open_file_index = editor.buffer_search_open_file;
            editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
            open_buffer_search_match(editor, open_file_index);
        }
        if (editor.global_search_open_index != JUMP_LIST_NO_SELECTION) {
            size_t const index = editor.global_search_open_index;
            editor.global_search_open_index = JUMP_LIST_NO_SELECTION;
            open_global_search_match(editor, index);
        }
        if (editor.jump_open_index != JUMP_LIST_NO_SELECTION) {
            size_t const index = editor.jump_open_index;
            editor.jump_open_index = JUMP_LIST_NO_SELECTION;
            open_recorded_jump(editor, index);
        }
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        char const* mode = editor.flag(EditorFlag::INSERT_MODE) ? "INSERT" : "NORMAL";
        if (!editor.flag(EditorFlag::INSERT_MODE) &&
            editor.selection_mode == EditorSelectionMode::CHARACTER) {
            mode = "VISUAL";
        } else if (!editor.flag(EditorFlag::INSERT_MODE) &&
                   editor.selection_mode == EditorSelectionMode::LINE) {
            mode = "V-LINE";
        }
        gui::Color const mode_color =
            editor.flag(EditorFlag::INSERT_MODE) ? palette.mode_insert : palette.mode_normal;

        if (auto shell = ui.column(
                gui::id("app_shell"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(12.0f),
                            .gap = 10.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {.background = palette.shell},
                }
            )) {
            if (auto header = ui.row(
                    gui::id("header"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(40.0f),
                                .padding = gui::insets(0.0f, OPEN_TAB_HEADER_PADDING),
                                .gap = 6.0f,
                                .align_y = gui::Align::CENTER,
                                .clip = true,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 8.0f,
                        },
                    }
                )) {
                if (auto tabs = ui.scroll_panel(
                        gui::id("open_file_tabs"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(36.0f),
                                .align_y = gui::Align::CENTER,
                                .clip = true,
                                .scroll_x = true,
                                .show_scrollbars = false,
                            },
                        }
                    )) {
                    if (auto tab_row = ui.row(
                            gui::id("open_file_tab_row"),
                            {
                                .layout = {
                                    .width = gui::children(),
                                    .height = gui::px(OPEN_TAB_HEIGHT),
                                    .gap = OPEN_TAB_GAP,
                                    .align_y = gui::Align::CENTER,
                                },
                            }
                        )) {
                        size_t selected_index = static_cast<size_t>(-1);
                        size_t closed_index = static_cast<size_t>(-1);
                        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
                            OpenFileTabSignal const signal = draw_open_file_tab(
                                ui, editor, palette, editor.open_files[index], index
                            );
                            if (signal.closed) {
                                closed_index = index;
                            } else if (signal.selected) {
                                selected_index = index;
                            }
                        }
                        if (closed_index != static_cast<size_t>(-1)) {
                            request_close_open_file(editor, closed_index);
                        } else if (selected_index != static_cast<size_t>(-1)) {
                            focus_code_split_for_open(editor);
                            OpenFile const file = editor.open_files[selected_index];
                            BASE_UNUSED(open_file(editor, file.name, file.path));
                        }
                    }
                }
            }

            if (auto body = ui.row(
                    gui::id("body"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .align_y = gui::Align::STRETCH,
                        },
                    }
                )) {
                draw_editor_split_ui(
                    ui,
                    editor,
                    icon_font,
                    palette,
                    input,
                    editor.root_split,
                    gui::fill(),
                    gui::fill(),
                    !picker_open
                );
            }

            if (auto bottom = ui.scroll_panel(
                    gui::id("bottom_bar"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(30.0f),
                                .padding = gui::insets(0.0f, 12.0f),
                                .align_y = gui::Align::CENTER,
                                .scroll_x = true,
                                .show_scrollbars = false,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 8.0f,
                        },
                    }
                )) {
                if (auto row = ui.row(
                        gui::id("bottom_bar_row"),
                        {
                            .layout = {
                                .width = gui::children(),
                                .height = gui::fill(),
                                .gap = 7.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    ui.label(
                        mode,
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {
                                .foreground = mode_color,
                                .font_size = editor.font_size,
                            },
                        }
                    );
                    ui.label(
                        fmt::tprintf(
                            "Ln %zu, Col %zu", editor.cursor_line + 1u, editor.cursor_column + 1u
                        ),
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {
                                .foreground = palette.muted,
                                .font_size = editor.font_size,
                            },
                        }
                    );
                    ui.label(
                        fmt::tprintf("Font Size %.0f", editor.font_size),
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {
                                .foreground = palette.muted,
                                .font_size = editor.font_size,
                            },
                        }
                    );
                    gui::InputTextDesc const bottom_input_desc = {
                        .box =
                            {
                                .layout =
                                    {
                                        .width = gui::px(std::max(120.0f, client_width - 360.0f)),
                                        .height = gui::fill(),
                                        .padding = gui::insets(0.0f),
                                    },
                                .style =
                                    {
                                        .background = gui::rgba(0, 0, 0, 0),
                                        .foreground = palette.text,
                                        .border = gui::rgba(0, 0, 0, 0),
                                        .font = editor_font,
                                        .font_size = editor.font_size,
                                    },
                            },
                        .ignore_input_on_focus = true,
                    };
                    bool const bottom_search = editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE) ||
                                               editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE);
                    if (bottom_search) {
                        bool const global_search = editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE);
                        ui.label(
                            global_search ? ":global-search" : ":search",
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {
                                    .foreground = palette.text,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                        gui::Id const input_id = gui::id("bottom_bar_text_search_input");
                        ui.request_focus(input_id);
                        gui::Signal const text_input = ui.input_text(
                            input_id,
                            "",
                            editor.text_search_text,
                            TEXT_SEARCH_TEXT_CAPACITY,
                            bottom_input_desc
                        );
                        if (text_input.changed) {
                            editor.text_search_text_size = cstr_len(editor.text_search_text);
                            if (!global_search) {
                                BASE_UNUSED(update_text_search_selection(editor));
                            }
                        }
                        if (text_input.activated) {
                            if (global_search) {
                                finish_global_search(editor);
                            } else {
                                finish_text_search(editor);
                            }
                        } else if (key_pressed(input, gui::Key::ESCAPE)) {
                            editor.set_flag(
                                global_search ? EditorFlag::GLOBAL_SEARCH_ACTIVE
                                              : EditorFlag::TEXT_SEARCH_ACTIVE,
                                false
                            );
                        }
                    } else if (editor.flag(EditorFlag::COMMAND_LINE_ACTIVE)) {
                        bool const complete = key_pressed(input, gui::Key::TAB, true);
                        if (complete) {
                            complete_command_line(editor);
                            ui.clear_focus();
                        }
                        ui.label(
                            ":",
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {
                                    .foreground = palette.text,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                        gui::Id const input_id = gui::id("bottom_bar_command_input");
                        ui.request_focus(input_id);
                        gui::Signal const command_input = ui.input_text(
                            input_id,
                            "",
                            editor.command_text,
                            COMMAND_TEXT_CAPACITY,
                            bottom_input_desc
                        );
                        if (command_input.changed) {
                            editor.command_text_size = cstr_len(editor.command_text);
                            select_command_match(editor);
                        }
                        if (command_input.activated) {
                            run_command_line(editor);
                        } else if (key_pressed(input, gui::Key::ESCAPE)) {
                            clear_command_line(editor);
                        }
                    } else if (editor.lsp_bridge != nullptr) {
                        ui.label(
                            lsp_status_bar_text(editor),
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {
                                    .foreground = editor.lsp_bridge->status == LspStatusKind::FAILED
                                                      ? palette.preprocessor
                                                  : editor.lsp_bridge->progress_active
                                                      ? palette.cursor
                                                      : palette.muted,
                                    .font_size = editor.font_size,
                                },
                            }
                        );
                    }
                }
            }

            if (editor.flag(EditorFlag::FILE_SEARCH_OPEN)) {
                draw_file_search_picker(
                    ui, editor, editor_font, palette, client_width, client_height, input, false
                );
            }
            if (editor.flag(EditorFlag::BUFFER_SEARCH_OPEN)) {
                draw_file_search_picker(
                    ui, editor, editor_font, palette, client_width, client_height, input, true
                );
            }
            if (editor.flag(EditorFlag::JUMP_LIST_OPEN)) {
                draw_jump_list_picker(
                    ui, editor, editor_font, palette, client_width, client_height, input
                );
            }
            if (editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
                draw_save_path_picker(ui, editor, palette, input);
            }
            handle_editor_save_request(editor);
            handle_editor_write_quit_request(editor);
        }
        if (!editor.flag(EditorFlag::FILE_SEARCH_OPEN) &&
            !editor.flag(EditorFlag::BUFFER_SEARCH_OPEN) &&
            !editor.flag(EditorFlag::JUMP_LIST_OPEN) && !editor.flag(EditorFlag::SAVE_PATH_OPEN) &&
            editor.close_intent == EditorCloseIntent::NONE) {
            draw_lsp_hover_popup(ui, editor_font, editor, char_width, palette, input);
        }
        draw_lsp_rename_popup(ui, editor, palette, char_width, client_width, client_height);
    }

} // namespace code_editor
