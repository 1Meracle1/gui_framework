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
    inline constexpr float OPEN_TAB_FONT_SIZE = 13.0f;
    inline constexpr float FILE_SEARCH_ROW_HEIGHT = 27.0f;
    inline constexpr float COMMAND_OVERLAY_HEIGHT = 88.0f;
    inline constexpr float COMMAND_LIST_HEIGHT = 30.0f;
    inline constexpr char OVERWRITE_FILE_KEY = 'o';
    inline constexpr char RELOAD_FILE_KEY = 'r';
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
        if (!editor.sidebar_resizing) {
            return;
        }

        float const width = std::clamp(
            input.mouse_pos.x - editor.sidebar_resize_grab_x,
            client_width * SIDEBAR_MIN_WIDTH_PERCENT,
            client_width * SIDEBAR_MAX_WIDTH_PERCENT
        );
        editor.sidebar_width_percent = width / std::max(1.0f, client_width);
        editor.sidebar_resizing = input.mouse_down[0u];
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
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        OpenFile* const file =
            find_open_file(editor, editor.current_file_name, editor.current_file_path);
        if (file == nullptr) {
            return;
        }
        file->text = text_buffer_copy(editor.text, *editor.text.arena);
        file->saved_text = editor.saved_text;
        file->file_write_stamp = editor.file_write_stamp;
        file->text_valid = true;
        file->dirty = editor.dirty;
        file->external_change_pending = editor.external_change_pending;
        file->file_deleted_on_disk = editor.file_deleted_on_disk;
    }

    auto load_open_file_buffer(EditorState& editor, OpenFile const& file) -> void {
        set_editor_text(editor, file.text);
        editor.current_file_name = file.name;
        editor.current_file_path = file.path;
        editor.saved_text = file.saved_text;
        editor.file_write_stamp = file.file_write_stamp;
        editor.dirty = file.dirty;
        editor.external_change_pending = file.external_change_pending;
        editor.file_deleted_on_disk = file.file_deleted_on_disk;
        remember_open_file(editor, file.name, file.path);
    }

    [[nodiscard]] auto
    open_file(EditorState& editor, StrRef name, StrRef path, bool store_current = true) -> bool {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        if (store_current) {
            store_current_open_file(editor);
        }
        if (load_shared_editor_buffer(editor, name, path)) {
            store_current_open_file(editor);
            return true;
        }
        OpenFile const* const file = find_open_file(editor, name, path);
        if (file != nullptr && file->text_valid) {
            load_open_file_buffer(editor, *file);
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
        editor.dirty = false;
        editor.external_change_pending = false;
        editor.file_deleted_on_disk = false;
        remember_open_file(editor, name, path);
        store_current_open_file(editor);
        return true;
    }

    auto open_tree_file(EditorState& editor, FileTreeEntry const& file) -> void {
        BASE_UNUSED(open_file(editor, file.name, file.path));
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
            pane->dirty = editor.dirty;
            pane->external_change_pending = editor.external_change_pending;
            pane->file_deleted_on_disk = editor.file_deleted_on_disk;
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
        editor.file_deleted_on_disk = false;
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
        editor.file_deleted_on_disk = false;
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
        editor.file_deleted_on_disk = false;

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

    auto save_path_from_popup(EditorState& editor) -> void {
        char path[SAVE_PATH_TEXT_CAPACITY * 2u] = {};
        StrRef const resolved = resolve_save_path(editor, path, sizeof(path));
        if (resolved.empty()) {
            editor.save_path_error = EditorSavePathError::EMPTY;
            return;
        }
        if (path_exists(resolved)) {
            editor.save_path_error = EditorSavePathError::EXISTS;
            return;
        }
        OpenFile const* const open_file = find_open_file(editor, {}, resolved);
        if (open_file != nullptr &&
            !same_file(
                open_file->name, open_file->path, editor.current_file_name, editor.current_file_path
            )) {
            editor.save_path_error = EditorSavePathError::EXISTS;
            return;
        }
        if (!save_current_file_as(editor, resolved)) {
            editor.save_path_error = EditorSavePathError::WRITE_FAILED;
            return;
        }
        close_save_path_popup(editor);
    }

    auto handle_editor_save_request(EditorState& editor) -> void {
        if (!editor.save_requested) {
            return;
        }
        editor.save_requested = false;
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

    auto update_current_file_change(EditorState& editor) -> void {
        if (editor.current_file_path.empty()) {
            return;
        }
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        if (stamp == 0u) {
            if (editor.file_write_stamp != 0u) {
                editor.file_deleted_on_disk = true;
                editor.external_change_pending = false;
                set_open_file_deleted(
                    editor, editor.current_file_name, editor.current_file_path, true
                );
            }
            return;
        }
        editor.file_deleted_on_disk = false;
        set_open_file_deleted(editor, editor.current_file_name, editor.current_file_path, false);
        if (editor.file_write_stamp == 0u) {
            editor.file_write_stamp = stamp;
            return;
        }
        if (stamp == editor.file_write_stamp) {
            return;
        }
        if (editor.dirty) {
            editor.file_write_stamp = stamp;
            editor.external_change_pending = true;
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
            file.file_write_stamp = stamp;
            file.text_valid = true;
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
        float char_width
    ) -> void {
        draw::TextStyle style = {.font = font, .size = font_size};
        StrRef const text = editor_line_text(line);
        size_t index = 0u;
        while (index < text.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, text, index);
            style.color = to_draw_color(syntax_token_color(palette, token.kind));
            draw_token(context, style, line, token.start, token.end, x, y, char_width);
            index = token.end;
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
        bool apply_key_reveal
    ) -> bool {
        bool const scrolled = !editor.sidebar_resizing && input.scroll_delta_y != 0.0f &&
                              point_in_rect(rect, input.mouse_pos);
        bool const hovered = point_in_rect(rect, input.mouse_pos);
        bool const mouse_pressed = input.mouse_down[0u] && !editor.mouse_was_down;
        bool const double_clicked =
            !editor.sidebar_resizing && hovered && input.mouse_double_clicked[0u];
        bool const triple_clicked =
            !editor.sidebar_resizing && hovered && input.mouse_triple_clicked[0u];
        bool const clicked = !editor.sidebar_resizing && mouse_pressed && hovered;
        bool const dragged =
            !editor.sidebar_resizing && editor.mouse_selecting && input.mouse_down[0u];
        if (scrolled) {
            editor.scroll_y -= input.scroll_delta_y;
        }
        if (triple_clicked) {
            select_line_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.mouse_selecting = false;
        } else if (double_clicked) {
            select_word_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.mouse_selecting = false;
        } else if (clicked) {
            update_cursor_from_mouse(
                editor,
                rect,
                input.mouse_pos,
                char_width,
                (input.key_mods & gui::KEY_MOD_SHIFT) != 0u
            );
            editor.mouse_selecting = true;
        } else if (dragged) {
            update_cursor_from_mouse(editor, rect, input.mouse_pos, char_width, true);
        }
        if (!input.mouse_down[0u]) {
            editor.mouse_selecting = false;
        }
        editor.mouse_was_down = input.mouse_down[0u];
        if (clicked || dragged || double_clicked || triple_clicked || apply_key_reveal) {
            reveal_cursor(editor, rect);
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
        float const line_number_x = content.min.x;
        EditorSelectionRange const selection = editor_selection_range(editor);
        SyntaxTokenizer const tokenizer = syntax_tokenizer_for_file_name(editor.current_file_name);
        size_t line = first_line;
        while (line < line_count && y < content.max.y) {
            if (line == editor.cursor_line) {
                draw::draw_rect_filled(
                    draw_context,
                    {{content.min.x, y}, {content.max.x, y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor_line, 0.72f)),
                    0.0f
                );
            }

            EditorLine const& text_line = editor_line(editor, line);
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
            if (!editor.insert_mode && line == editor.cursor_line) {
                size_t const cursor_column =
                    text_line.size == 0u ? 0u : std::min(editor.cursor_column, text_line.size - 1u);
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
            draw::TextStyle number_style = {
                .font = editor_font,
                .size = editor.font_size,
                .color = to_draw_color(line == editor.cursor_line ? palette.text : palette.faint),
            };
            draw::draw_text(
                draw_context,
                {std::round(line_number_x), std::round(y - 2.0f)},
                number_style,
                fmt::tprintf("%4zu", line + 1u),
                nullptr
            );
            draw_syntax_line(
                draw_context,
                editor_font,
                tokenizer,
                palette,
                text_line,
                text_x,
                y - 2.0f,
                editor.font_size,
                char_width
            );
            y += line_height;
            line += 1u;
        }

        float const cursor_x =
            std::round(text_x + char_width * static_cast<float>(editor.cursor_column));
        float const cursor_y =
            content.min.y + static_cast<float>(editor.cursor_line) * line_height - editor.scroll_y;
        if (editor.insert_mode && cursor_y + line_height >= content.min.y &&
            cursor_y < content.max.y) {
            draw::draw_rect_filled(
                draw_context,
                {{cursor_x, std::round(cursor_y + 2.0f)},
                 {cursor_x + 2.0f, std::round(cursor_y + line_height - 2.0f)}},
                to_draw_color(palette.mode_insert),
                0.0f
            );
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
        size_t& target_focus
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
                target_focus
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
                target_focus
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
        bool const popup_open = editor.external_change_pending || editor.file_deleted_on_disk;
        gui::InputState const surface_input = popup_open ? gui::InputState{} : input;
        bool const clicked = draw_editor_surface_rect(
            draw_context,
            editor_font,
            editor,
            char_width,
            box->rect,
            surface_input,
            palette,
            !popup_open && split == initial_focus && input.key_event_count != 0u
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
        if (!editor.command_line_active) {
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

        float const font_size = editor_scaled_font_size(editor, 12.0f);
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

        float x = panel.min.x + 8.0f;
        float const y = panel.max.y - COMMAND_LIST_HEIGHT + 3.0f;
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const command = editor_command(index);
            float const width =
                font_cache::text_advance(editor_font, font_size, command.name) + 16.0f;
            bool const active = index == editor.command_selected;
            if (active) {
                draw::Rect const item = {{x, y}, {x + width, y + 22.0f}};
                draw::draw_rect_filled(
                    draw_context, item, to_draw_color(palette.cursor_line), 4.0f
                );
                draw::draw_rect(draw_context, item, to_draw_color(palette.cursor), 1.0f, 4.0f);
            }
            text_style.color = to_draw_color(active ? palette.text : palette.muted);
            draw::draw_text(draw_context, {x + 8.0f, y + 3.0f}, text_style, command.name, nullptr);
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

    auto draw_editor_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette
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
            input,
            palette,
            editor.root_split,
            initial_focus,
            target_focus
        );
        focus_editor_split(editor, target_focus);
        draw_command_overlay(draw_context, editor_font, editor, ui, palette);
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
                focus_first_code_split(editor);
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &editor.tree_open
            );
            if (editor.tree_open) {
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
        gui::Size height
    ) -> void {
        bool const focused = split == editor.focused_split;
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
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &editor.tree_open
            );
            if (editor.tree_open) {
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
                editor.sidebar_resizing = true;
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
            return editor.dirty;
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
            return editor.file_deleted_on_disk;
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

    auto close_open_file(EditorState& editor, size_t index) -> void {
        if (index >= editor.open_files.size()) {
            return;
        }

        bool const selected = open_file_selected(editor, editor.open_files[index]);
        if (selected) {
            save_scratch_file(editor);
        }
        editor.open_files.ordered_remove(index);
        if (!selected) {
            return;
        }
        if (editor.open_files.empty()) {
            open_scratch_file(editor);
            return;
        }

        size_t const next_index = std::min(index, editor.open_files.size() - 1u);
        OpenFile const next = editor.open_files[next_index];
        if (!open_file(editor, next.name, next.path, false)) {
            open_scratch_file(editor);
        }
    }

    auto close_current_file(EditorState& editor) -> void {
        size_t index = 0u;
        if (selected_open_file_index(editor, index)) {
            close_open_file(editor, index);
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
        focus_first_code_split(editor);
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    auto draw_file_search_picker(
        gui::Frame& ui, EditorState& editor, Palette const& palette, float client_height
    ) -> void {
        float constexpr modal_margin = 24.0f;
        float constexpr dialog_padding = 8.0f;
        float constexpr dialog_gap = 8.0f;
        float constexpr query_height = 36.0f;
        float const results_height = std::min(
            FILE_SEARCH_ROW_HEIGHT * static_cast<float>(FILE_SEARCH_RESULT_LIMIT),
            std::max(
                FILE_SEARCH_ROW_HEIGHT,
                client_height - 2.0f * (modal_margin + dialog_padding) - query_height - dialog_gap
            )
        );

        FileSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const match_count = collect_file_search_matches(editor, matches);
        editor.file_search_selected =
            match_count == 0u ? 0u : std::min(editor.file_search_selected, match_count - 1u);

        if (auto modal = ui.modal(
                gui::id("file_search_modal"),
                {
                    .layout =
                        {
                            .padding = gui::insets(modal_margin),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::CENTER,
                        },
                    .debug_name = "file_search_modal",
                }
            )) {
            if (auto dialog = ui.column(
                    gui::id("file_search_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::children(),
                                .max_width = gui::px(860.0f),
                                .padding = gui::insets(dialog_padding),
                                .gap = dialog_gap,
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
                if (auto query = ui.row(
                        gui::id("file_search_query"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(query_height),
                                    .padding = gui::insets(0.0f, 10.0f),
                                    .gap = 6.0f,
                                    .align_y = gui::Align::CENTER,
                                },
                            .style = {
                                .background = palette.panel_raised,
                                .border = palette.cursor,
                                .border_thickness = 1.0f,
                                .radius = 4.0f,
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
                    ui.label(
                        editor_file_search_text(editor),
                        {
                            .layout = {.width = gui::fill(), .height = gui::fill()},
                            .style = {.foreground = palette.text},
                        }
                    );
                    ui.label(
                        fmt::tprintf(
                            "%zu/%zu",
                            match_count == 0u ? 0u : editor.file_search_selected + 1u,
                            match_count
                        ),
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {.foreground = palette.muted},
                        }
                    );
                }

                if (auto results = ui.scroll_panel(
                        gui::id("file_search_results"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(results_height),
                                .align_x = gui::Align::STRETCH,
                            },
                        }
                    )) {
                    if (match_count == 0u) {
                        ui.label(
                            editor.tree_files.empty() ? "No indexed files" : "No matching files",
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                        .padding = gui::insets(0.0f, 10.0f),
                                    },
                                .style = {.foreground = palette.muted},
                            }
                        );
                    }
                    for (size_t index = 0u; index < match_count; ++index) {
                        FileTreeEntry const& file =
                            editor.tree_files[matches[index].tree_file_index];
                        bool const selected = index == editor.file_search_selected;
                        if (auto row = ui.row(
                                gui::id("file_search_result", index),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                            .padding = gui::insets(0.0f, 10.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .style = {
                                        .background = selected ? palette.cursor_line : gui::Color{},
                                        .radius = selected ? 4.0f : -1.0f,
                                    },
                                }
                            )) {
                            if (row.signal().clicked_left) {
                                editor.file_search_selected = index;
                                open_file_search_match(editor, matches[index].tree_file_index);
                                editor.file_search_open = false;
                            }
                            ui.label(
                                selected ? ">" : "",
                                {
                                    .layout = {.width = gui::px(14.0f), .height = gui::fill()},
                                    .style = {.foreground = palette.cursor},
                                }
                            );
                            ui.label(
                                file_search_entry_text(file),
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = selected ? palette.text : palette.muted,
                                        .font_size = editor_scaled_font_size(editor, 12.5f),
                                    },
                                }
                            );
                        }
                    }
                }
            }
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

    auto draw_save_path_picker(
        gui::Frame& ui, EditorState& editor, Palette const& palette, gui::InputState const& input
    ) -> void {
        if (!editor.save_path_open) {
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
                            .font_size = editor_scaled_font_size(editor, 12.0f),
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
                            .font_size = editor_scaled_font_size(editor, 12.0f),
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
            close_save_path_popup(editor);
        } else if (save) {
            save_path_from_popup(editor);
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
                        .font_size = editor_scaled_font_size(editor, OPEN_TAB_FONT_SIZE),
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
                            .font_size = editor_scaled_font_size(editor, 12.0f),
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

    auto draw_file_deleted_popup(gui::Frame& ui, EditorState& editor, Palette const& palette)
        -> void {
        if (!editor.file_deleted_on_disk || editor.save_path_open) {
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
                        .font_size = editor_scaled_font_size(editor, 15.0f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
        if (!editor.external_change_pending || editor.file_deleted_on_disk) {
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
                        .font_size = editor_scaled_font_size(editor, 15.0f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
                        .font_size = editor_scaled_font_size(editor, 12.5f),
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
        gui::Size height
    ) -> void {
        if (split >= editor.split_nodes.size()) {
            return;
        }

        EditorSplitNode const node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            if (editor_split_pane_kind(editor, split) == EditorPaneKind::FILESYSTEM) {
                draw_filesystem_panel(ui, editor, icon_font, palette, split, width, height);
                return;
            }
            bool const focused = split == editor.focused_split;
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
                    draw_file_deleted_popup(ui, editor, palette);
                    draw_file_change_popup(ui, editor, palette, input);
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
                    ui, editor, icon_font, palette, input, node.first, gui::fill(ratio), gui::fill()
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
                    gui::fill()
                );
            }
        } else if (auto column = ui.column(editor_split_id(split), desc)) {
            draw_editor_split_ui(
                ui, editor, icon_font, palette, input, node.first, gui::fill(), gui::fill(ratio)
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
                gui::fill(1.0f - ratio)
            );
        }
    }

    auto draw_editor_ui(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width,
        float client_height,
        gui::InputState const& input
    ) -> void {
        if (editor.sidebar_visible) {
            ensure_filesystem_panel(editor);
        }
        update_sidebar_resize(editor, client_width, input);
        if (editor.close_current_requested) {
            close_current_file(editor);
            editor.close_current_requested = false;
        }
        if (editor.file_search_open_file != FILE_SEARCH_NO_FILE) {
            size_t const tree_file_index = editor.file_search_open_file;
            editor.file_search_open_file = FILE_SEARCH_NO_FILE;
            open_file_search_match(editor, tree_file_index);
        }
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        char const* mode = editor.insert_mode ? "INSERT" : "NORMAL";
        if (!editor.insert_mode && editor.selection_mode == EditorSelectionMode::CHARACTER) {
            mode = "VISUAL";
        } else if (!editor.insert_mode && editor.selection_mode == EditorSelectionMode::LINE) {
            mode = "V-LINE";
        }
        gui::Color const mode_color =
            editor.insert_mode ? palette.mode_insert : palette.mode_normal;

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
                            close_open_file(editor, closed_index);
                        } else if (selected_index != static_cast<size_t>(-1)) {
                            focus_first_code_split(editor);
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
                    gui::fill()
                );
            }

            float const status_ratio = std::clamp(
                editor.sidebar_width_percent, SIDEBAR_MIN_WIDTH_PERCENT, SIDEBAR_MAX_WIDTH_PERCENT
            );
            if (auto bottom = ui.row(
                    gui::id("bottom_bar"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(30.0f),
                            .gap = EDITOR_SPLIT_GAP,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                gui::BoxDesc const status_panel = {
                    .layout =
                        {
                            .width = gui::fill(status_ratio),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                            .gap = 7.0f,
                            .align_y = gui::Align::CENTER,
                            .clip = true,
                        },
                    .style = {
                        .background = palette.panel,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                };
                if (auto status = ui.row(gui::id("status"), status_panel)) {
                    ui.label(
                        mode,
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {
                                .foreground = mode_color,
                                .font_size = editor_scaled_font_size(editor, 12.0f),
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
                                .font_size = editor_scaled_font_size(editor, 12.0f),
                            },
                        }
                    );
                    ui.label(
                        "UTF-8",
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {
                                .foreground = palette.muted,
                                .font_size = editor_scaled_font_size(editor, 12.0f),
                            },
                        }
                    );
                }

                gui::BoxDesc const command_panel = {
                    .layout =
                        {
                            .width = gui::fill(1.0f - status_ratio),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 12.0f),
                            .align_y = gui::Align::CENTER,
                            .clip = true,
                        },
                    .style = {
                        .background = palette.panel,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                };
                if (auto command = ui.row(gui::id("command_line"), command_panel)) {
                    if (editor.command_line_active) {
                        ui.label(
                            fmt::tprintf(":%s", editor.command_text),
                            {
                                .layout = {.width = gui::fill(), .height = gui::fill()},
                                .style = {
                                    .foreground = palette.text,
                                    .font_size = editor_scaled_font_size(editor, 12.0f),
                                },
                            }
                        );
                    }
                }
            }

            if (editor.file_search_open) {
                draw_file_search_picker(ui, editor, palette, client_height);
            }
            if (editor.save_path_open) {
                draw_save_path_picker(ui, editor, palette, input);
            }
            handle_editor_save_request(editor);
        }
    }

} // namespace code_editor
