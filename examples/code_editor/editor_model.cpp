#include "editor_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace code_editor {

    inline constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);
    inline constexpr EditorCommand EDITOR_COMMANDS[] = {
        {"write", "w", "Save the current file."},
        {"quit", "q", "Close the focused split."},
        {"buffer-close", "bc", "Close the current buffer."},
        {"open", "o", "Open a file from the indexed tree."},
        {"toggle-sidebar", "tree", "Toggle the file tree sidebar."},
        {"format", "fmt", "Format the current C/C++ file."},
        {"symbols", "sym", "Open document symbols."},
    };

    struct EditorPosition;

    struct EditorUndoEntry {
        EditorUndoEntry* previous = nullptr;
        char const* text = nullptr;
        size_t text_size = 0u;
        size_t cursor_line = 0u;
        size_t cursor_column = 0u;
        size_t selection_anchor_line = 0u;
        size_t selection_anchor_column = 0u;
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float scroll_y = 0.0f;
        bool selection_active = false;
    };

    auto clamp_cursor(EditorState& editor) -> void;
    auto sync_shared_panes(EditorState& editor) -> void;
    auto close_focused_split(EditorState& editor) -> void;
    auto refresh_editor_dirty(EditorState& editor) -> void;
    auto request_lsp(EditorState& editor, LspRequestKind kind, StrRef new_name = {}) -> void;
    auto open_lsp_rename(EditorState& editor) -> void;
    [[nodiscard]] auto word_range_at_position(
        EditorState const& editor,
        EditorPosition position,
        EditorPosition& start,
        EditorPosition& end
    ) -> bool;
    [[nodiscard]] auto split_in_direction(EditorState const& editor, char direction) -> size_t;

    [[nodiscard]] auto split_valid(EditorState const& editor, size_t split) -> bool {
        return split < editor.split_nodes.size();
    }

    [[nodiscard]] auto focused_pane_index(EditorState const& editor) -> size_t {
        if (!split_valid(editor, editor.focused_split)) {
            return 0u;
        }
        return editor.split_nodes[editor.focused_split].pane;
    }

    [[nodiscard]] auto pane_kind(EditorState const& editor, size_t pane_index) -> EditorPaneKind {
        return pane_index < editor.panes.size() && editor.panes[pane_index] != nullptr
                   ? editor.panes[pane_index]->kind
                   : EditorPaneKind::CODE;
    }

    [[nodiscard]] auto focused_pane_kind(EditorState const& editor) -> EditorPaneKind {
        return pane_kind(editor, focused_pane_index(editor));
    }

    [[nodiscard]] auto
    same_editor_file(StrRef lhs_name, StrRef lhs_path, StrRef rhs_name, StrRef rhs_path) -> bool;

    [[nodiscard]] auto editor_line_count(EditorState const& editor) -> size_t {
        return text_buffer_line_count(editor.text);
    }

    [[nodiscard]] auto editor_line(EditorState const& editor, size_t index) -> EditorLine {
        DEBUG_ASSERT(index < editor_line_count(editor));
        return text_buffer_line(editor.text, index);
    }

    [[nodiscard]] auto editor_line_text(EditorLine line) -> StrRef {
        return text_buffer_line_text(line);
    }

    auto insert_line(EditorState& editor, size_t line, StrRef text) -> void {
        size_t const insert_at = std::min(line, editor_line_count(editor));
        bool const append = insert_at == editor_line_count(editor);
        size_t const offset = !append ? text_buffer_position_to_offset(editor.text, {insert_at, 0u})
                                      : text_buffer_size(editor.text);
        text_buffer_insert(editor.text, offset, "\n");
        if (!text.empty()) {
            text_buffer_insert(editor.text, offset + (append ? 1u : 0u), text);
        }
    }

    auto set_editor_text_impl(EditorState& editor, StrRef text, bool clear_undo, bool clear_dirty)
        -> void {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        if (clear_undo) {
            editor.undo_stack = nullptr;
            editor.redo_stack = nullptr;
        }
        text_buffer_set(editor.text, text);
        editor.cursor_line = 0u;
        editor.cursor_column = 0u;
        editor.preferred_column = 0u;
        editor.selection_anchor_line = 0u;
        editor.selection_anchor_column = 0u;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        editor.scroll_y = 0.0f;
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
        editor.pending_line_number = 0u;
        editor.set_flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE, false);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.command_text_size = 0u;
        editor.command_selected = 0u;
        editor.command_text[0u] = '\0';
        editor.set_flag(EditorFlag::SAVE_REQUESTED, false);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
        close_editor_lsp_popup(editor);
        if (clear_dirty) {
            mark_editor_saved(editor);
        }
    }

    auto set_editor_text(EditorState& editor, StrRef text) -> void {
        set_editor_text_impl(editor, text, true, true);
    }

    auto init_text_storage(EditorText& text, Arena& arena) -> void {
        text_buffer_init(text, arena);
    }

    auto clone_text(EditorText const& source, EditorText& target) -> void {
        text_buffer_clone(source, target);
    }

    auto move_editor_to_pane(EditorState& editor, EditorPane& pane) -> void {
        pane.text = std::move(editor.text);
        pane.current_file_name = editor.current_file_name;
        pane.current_file_path = editor.current_file_path;
        pane.scratch_text = editor.scratch_text;
        pane.saved_text = editor.saved_text;
        pane.undo_stack = editor.undo_stack;
        pane.redo_stack = editor.redo_stack;
        pane.file_write_stamp = editor.file_write_stamp;
        pane.cursor_line = editor.cursor_line;
        pane.cursor_column = editor.cursor_column;
        pane.preferred_column = editor.preferred_column;
        pane.selection_anchor_line = editor.selection_anchor_line;
        pane.selection_anchor_column = editor.selection_anchor_column;
        pane.selection_mode = editor.selection_mode;
        pane.scroll_y = editor.scroll_y;
        pane.insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        pane.selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        pane.mouse_selecting = editor.flag(EditorFlag::MOUSE_SELECTING);
        pane.mouse_was_down = editor.flag(EditorFlag::MOUSE_WAS_DOWN);
        pane.dirty = editor.flag(EditorFlag::DIRTY);
        pane.external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        pane.file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
    }

    auto move_pane_to_editor(EditorState& editor, EditorPane& pane) -> void {
        editor.text = std::move(pane.text);
        editor.current_file_name = pane.current_file_name;
        editor.current_file_path = pane.current_file_path;
        editor.scratch_text = pane.scratch_text;
        editor.saved_text = pane.saved_text;
        editor.undo_stack = pane.undo_stack;
        editor.redo_stack = pane.redo_stack;
        editor.file_write_stamp = pane.file_write_stamp;
        editor.cursor_line = pane.cursor_line;
        editor.cursor_column = pane.cursor_column;
        editor.preferred_column = pane.preferred_column;
        editor.selection_anchor_line = pane.selection_anchor_line;
        editor.selection_anchor_column = pane.selection_anchor_column;
        editor.selection_mode = pane.selection_mode;
        editor.scroll_y = pane.scroll_y;
        editor.set_flag(EditorFlag::INSERT_MODE, pane.insert_mode);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, pane.selection_active);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, pane.mouse_selecting);
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, pane.mouse_was_down);
        editor.set_flag(EditorFlag::DIRTY, pane.dirty);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, pane.external_change_pending);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, pane.file_deleted_on_disk);
    }

    [[nodiscard]] auto pane_for_split(EditorState& editor, size_t split) -> EditorPane* {
        if (!split_valid(editor, split)) {
            return nullptr;
        }
        size_t const pane = editor.split_nodes[split].pane;
        return pane < editor.panes.size() ? editor.panes[pane] : nullptr;
    }

    [[nodiscard]] auto pane_for_split(EditorState const& editor, size_t split)
        -> EditorPane const* {
        if (!split_valid(editor, split)) {
            return nullptr;
        }
        size_t const pane = editor.split_nodes[split].pane;
        return pane < editor.panes.size() ? editor.panes[pane] : nullptr;
    }

    auto store_focused_pane(EditorState& editor) -> void {
        if (!editor.flag(EditorFlag::PANE_LOADED)) {
            return;
        }
        EditorPane* const pane = pane_for_split(editor, editor.focused_split);
        DEBUG_ASSERT(pane != nullptr);
        move_editor_to_pane(editor, *pane);
        editor.set_flag(EditorFlag::PANE_LOADED, false);
    }

    auto load_focused_pane(EditorState& editor) -> void {
        if (editor.flag(EditorFlag::PANE_LOADED)) {
            return;
        }
        EditorPane* const pane = pane_for_split(editor, editor.focused_split);
        DEBUG_ASSERT(pane != nullptr);
        move_pane_to_editor(editor, *pane);
        editor.set_flag(EditorFlag::PANE_LOADED, true);
    }

    [[nodiscard]] auto new_pane(EditorState& editor) -> EditorPane* {
        DEBUG_ASSERT(editor.arena != nullptr);
        EditorPane* const pane = arena_new<EditorPane>(*editor.arena);
        init_text_storage(pane->text, *editor.arena);
        bool const views_ok = pane->open_file_views.init(0u, editor.arena->resource());
        DEBUG_ASSERT(views_ok);
        (void)views_ok;
        bool const ok = editor.panes.push_back(pane);
        DEBUG_ASSERT(ok);
        (void)ok;
        return pane;
    }

    auto clone_active_to_pane(EditorState& editor, EditorPane& pane) -> void {
        pane.kind = focused_pane_kind(editor);
        clone_text(editor.text, pane.text);
        pane.current_file_name = editor.current_file_name;
        pane.current_file_path = editor.current_file_path;
        pane.scratch_text = editor.scratch_text;
        pane.saved_text = editor.saved_text;
        pane.undo_stack = editor.undo_stack;
        pane.redo_stack = editor.redo_stack;
        pane.file_write_stamp = editor.file_write_stamp;
        pane.cursor_line = editor.cursor_line;
        pane.cursor_column = editor.cursor_column;
        pane.preferred_column = editor.preferred_column;
        pane.selection_anchor_line = editor.selection_anchor_line;
        pane.selection_anchor_column = editor.selection_anchor_column;
        pane.selection_mode = editor.selection_mode;
        pane.scroll_y = editor.scroll_y;
        pane.insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        pane.selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        pane.mouse_selecting = false;
        pane.mouse_was_down = false;
        pane.dirty = editor.flag(EditorFlag::DIRTY);
        pane.external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        pane.file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
    }

    auto init_split_tree(EditorState& editor) -> void {
        if (!editor.split_nodes.empty()) {
            return;
        }

        EditorPane* const pane = new_pane(editor);
        move_editor_to_pane(editor, *pane);
        EditorSplitNode const root = {.pane = 0u};
        bool const ok = editor.split_nodes.push_back(root);
        DEBUG_ASSERT(ok);
        (void)ok;
        editor.root_split = 0u;
        editor.focused_split = 0u;
        editor.set_flag(EditorFlag::PANE_LOADED, false);
        load_focused_pane(editor);
    }

    auto init_editor(Arena& arena, EditorState& editor, StrRef text) -> void {
        bool const first_init = editor.text.arena == nullptr;
        if (first_init) {
            editor.arena = &arena;
            init_text_storage(editor.text, arena);

            bool const panes_ok = editor.panes.init(0u, arena.resource());
            DEBUG_ASSERT(panes_ok);
            (void)panes_ok;

            bool const splits_ok = editor.split_nodes.init(0u, arena.resource());
            DEBUG_ASSERT(splits_ok);
            (void)splits_ok;

            bool const open_ok = editor.open_files.init(0u, arena.resource());
            DEBUG_ASSERT(open_ok);
            (void)open_ok;
        }
        if (first_init) {
            set_editor_text(editor, text);
            editor.scratch_text = text;
        }
        init_split_tree(editor);
    }

    [[nodiscard]] auto same_open_file(OpenFile const& file, StrRef name, StrRef path) -> bool {
        if (!path.empty() || !file.path.empty()) {
            return file.path == path;
        }
        return file.name == name;
    }

    auto remember_open_file(EditorState& editor, StrRef name, StrRef path) -> void {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        DEBUG_ASSERT(editor.arena != nullptr);
        if (name.empty()) {
            return;
        }
        for (OpenFile const& file : editor.open_files) {
            if (same_open_file(file, name, path)) {
                return;
            }
        }
        bool const ok = editor.open_files.push_back({
            arena_copy_cstr(*editor.arena, name),
            path.empty() ? StrRef() : arena_copy_cstr(*editor.arena, path),
        });
        DEBUG_ASSERT(ok);
        (void)ok;
    }

    [[nodiscard]] auto find_open_file_view(EditorPane& pane, StrRef name, StrRef path)
        -> OpenFileViewState* {
        for (OpenFileViewState& view : pane.open_file_views) {
            if (same_editor_file(view.name, view.path, name, path)) {
                return &view;
            }
        }
        return nullptr;
    }

    auto reset_editor_file_view(EditorState& editor) -> void {
        editor.cursor_line = 0u;
        editor.cursor_column = 0u;
        editor.preferred_column = 0u;
        editor.selection_anchor_line = 0u;
        editor.selection_anchor_column = 0u;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.scroll_y = 0.0f;
        editor.set_flag(EditorFlag::INSERT_MODE, false);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, false);
    }

    auto store_focused_open_file_view(EditorState& editor) -> void {
        DEBUG_ASSERT(editor.arena != nullptr);
        if (editor.current_file_name.empty() || focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return;
        }
        EditorPane* const pane = pane_for_split(editor, editor.focused_split);
        if (pane == nullptr) {
            return;
        }

        OpenFileViewState* view =
            find_open_file_view(*pane, editor.current_file_name, editor.current_file_path);
        if (view == nullptr) {
            view = pane->open_file_views.push_back_and_get_ptr({
                arena_copy_cstr(*editor.arena, editor.current_file_name),
                editor.current_file_path.empty()
                    ? StrRef()
                    : arena_copy_cstr(*editor.arena, editor.current_file_path),
            });
            DEBUG_ASSERT(view != nullptr);
        }
        view->cursor_line = editor.cursor_line;
        view->cursor_column = editor.cursor_column;
        view->preferred_column = editor.preferred_column;
        view->selection_anchor_line = editor.selection_anchor_line;
        view->selection_anchor_column = editor.selection_anchor_column;
        view->selection_mode = editor.selection_mode;
        view->scroll_y = editor.scroll_y;
        view->insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        view->selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
    }

    [[nodiscard]] auto restore_focused_open_file_view(EditorState& editor, StrRef name, StrRef path)
        -> bool {
        reset_editor_file_view(editor);
        if (focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return false;
        }
        EditorPane* const pane = pane_for_split(editor, editor.focused_split);
        if (pane == nullptr) {
            return false;
        }
        OpenFileViewState const* const view = find_open_file_view(*pane, name, path);
        if (view == nullptr) {
            return false;
        }
        editor.cursor_line = view->cursor_line;
        editor.cursor_column = view->cursor_column;
        editor.preferred_column = view->preferred_column;
        editor.selection_anchor_line = view->selection_anchor_line;
        editor.selection_anchor_column = view->selection_anchor_column;
        editor.selection_mode = view->selection_mode;
        editor.scroll_y = view->scroll_y;
        editor.set_flag(EditorFlag::INSERT_MODE, view->insert_mode);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, view->selection_active);
        clamp_cursor(editor);
        return true;
    }

    [[nodiscard]] auto load_shared_editor_buffer(EditorState& editor, StrRef name, StrRef path)
        -> bool {
        size_t const focused_pane = focused_pane_index(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane || pane_kind(editor, index) != EditorPaneKind::CODE) {
                continue;
            }
            EditorPane* const pane = editor.panes[index];
            if (pane == nullptr ||
                !same_editor_file(name, path, pane->current_file_name, pane->current_file_path)) {
                continue;
            }

            clone_text(pane->text, editor.text);
            editor.current_file_name = pane->current_file_name;
            editor.current_file_path = pane->current_file_path;
            editor.scratch_text = pane->scratch_text;
            editor.saved_text = pane->saved_text;
            editor.undo_stack = pane->undo_stack;
            editor.redo_stack = pane->redo_stack;
            editor.file_write_stamp = pane->file_write_stamp;
            BASE_UNUSED(restore_focused_open_file_view(editor, name, path));
            editor.set_flag(EditorFlag::DIRTY, pane->dirty);
            editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, pane->external_change_pending);
            editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, pane->file_deleted_on_disk);
            clamp_cursor(editor);
            remember_open_file(editor, editor.current_file_name, editor.current_file_path);
            return true;
        }
        return false;
    }

    [[nodiscard]] auto
    same_editor_file(StrRef lhs_name, StrRef lhs_path, StrRef rhs_name, StrRef rhs_path) -> bool {
        if (!lhs_path.empty() || !rhs_path.empty()) {
            return lhs_path == rhs_path;
        }
        return lhs_name == rhs_name;
    }

    [[nodiscard]] auto pane_line_count(EditorPane const& pane) -> size_t {
        return text_buffer_line_count(pane.text);
    }

    [[nodiscard]] auto pane_line_size(EditorPane const& pane, size_t line) -> size_t {
        DEBUG_ASSERT(line < pane_line_count(pane));
        return text_buffer_line_size(pane.text, line);
    }

    auto clamp_pane_cursor(EditorPane& pane) -> void {
        if (pane_line_count(pane) == 0u) {
            return;
        }
        pane.cursor_line = std::min(pane.cursor_line, pane_line_count(pane) - 1u);
        pane.cursor_column = std::min(pane.cursor_column, pane_line_size(pane, pane.cursor_line));
        pane.preferred_column = std::min(pane.preferred_column, pane.cursor_column);
        pane.selection_anchor_line =
            std::min(pane.selection_anchor_line, pane_line_count(pane) - 1u);
        pane.selection_anchor_column = std::min(
            pane.selection_anchor_column, pane_line_size(pane, pane.selection_anchor_line)
        );
    }

    auto sync_shared_panes(EditorState& editor) -> void {
        if (!editor.flag(EditorFlag::PANE_LOADED) ||
            focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return;
        }

        size_t const focused_pane = focused_pane_index(editor);
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index == focused_pane || pane_kind(editor, index) != EditorPaneKind::CODE) {
                continue;
            }
            EditorPane* const pane = editor.panes[index];
            if (pane == nullptr || !same_editor_file(
                                       editor.current_file_name,
                                       editor.current_file_path,
                                       pane->current_file_name,
                                       pane->current_file_path
                                   )) {
                continue;
            }
            if (pane->text.revision != editor.text.revision) {
                clone_text(editor.text, pane->text);
            }
            pane->scratch_text = editor.scratch_text;
            pane->saved_text = editor.saved_text;
            pane->undo_stack = editor.undo_stack;
            pane->redo_stack = editor.redo_stack;
            pane->file_write_stamp = editor.file_write_stamp;
            pane->dirty = editor.flag(EditorFlag::DIRTY);
            pane->external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
            pane->file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
            clamp_pane_cursor(*pane);
        }
    }

    [[nodiscard]] auto split_leaf(EditorState const& editor, size_t split) -> bool {
        return split_valid(editor, split) &&
               editor.split_nodes[split].kind == EditorSplitKind::LEAF;
    }

    [[nodiscard]] auto first_leaf(EditorState const& editor, size_t split) -> size_t {
        if (!split_valid(editor, split)) {
            return INVALID_INDEX;
        }
        EditorSplitNode const& node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            return split;
        }
        return first_leaf(editor, node.first);
    }

    auto focus_editor_split(EditorState& editor, size_t split) -> void {
        if (!split_leaf(editor, split)) {
            return;
        }
        if (split == editor.focused_split) {
            sync_shared_panes(editor);
            clamp_cursor(editor);
            return;
        }
        sync_shared_panes(editor);
        store_focused_pane(editor);
        editor.focused_split = split;
        load_focused_pane(editor);
        clamp_cursor(editor);
    }

    auto set_editor_split_rect(EditorState& editor, size_t split, gui::Rect rect) -> void {
        if (split_valid(editor, split)) {
            editor.split_nodes[split].rect = rect;
        }
    }

    [[nodiscard]] auto editor_focused_pane(EditorState const& editor) -> size_t {
        return focused_pane_index(editor);
    }

    [[nodiscard]] auto editor_focused_pane_kind(EditorState const& editor) -> EditorPaneKind {
        return focused_pane_kind(editor);
    }

    [[nodiscard]] auto editor_split_pane_kind(EditorState const& editor, size_t split)
        -> EditorPaneKind {
        if (!split_valid(editor, split)) {
            return EditorPaneKind::CODE;
        }
        return pane_kind(editor, editor.split_nodes[split].pane);
    }

    [[nodiscard]] auto leaf_count(EditorState const& editor, size_t split) -> size_t {
        if (!split_valid(editor, split)) {
            return 0u;
        }
        EditorSplitNode const& node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            return 1u;
        }
        return leaf_count(editor, node.first) + leaf_count(editor, node.second);
    }

    [[nodiscard]] auto
    leaf_count_by_kind(EditorState const& editor, size_t split, EditorPaneKind kind) -> size_t {
        if (!split_valid(editor, split)) {
            return 0u;
        }
        EditorSplitNode const& node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            return pane_kind(editor, node.pane) == kind ? 1u : 0u;
        }
        return leaf_count_by_kind(editor, node.first, kind) +
               leaf_count_by_kind(editor, node.second, kind);
    }

    [[nodiscard]] auto editor_split_leaf_count(EditorState const& editor) -> size_t {
        return leaf_count(editor, editor.root_split);
    }

    [[nodiscard]] auto
    find_leaf_by_kind(EditorState const& editor, size_t split, EditorPaneKind kind) -> size_t {
        if (!split_valid(editor, split)) {
            return INVALID_INDEX;
        }
        EditorSplitNode const& node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            return pane_kind(editor, node.pane) == kind ? split : INVALID_INDEX;
        }
        size_t const first = find_leaf_by_kind(editor, node.first, kind);
        return first != INVALID_INDEX ? first : find_leaf_by_kind(editor, node.second, kind);
    }

    [[nodiscard]] auto filesystem_panel_visible(EditorState const& editor) -> bool {
        return find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::FILESYSTEM) !=
               INVALID_INDEX;
    }

    auto focus_first_code_split(EditorState& editor) -> void {
        size_t const split = find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::CODE);
        if (split != INVALID_INDEX) {
            focus_editor_split(editor, split);
        }
    }

    auto ensure_filesystem_panel(EditorState& editor) -> void {
        if (filesystem_panel_visible(editor) || editor.split_nodes.empty()) {
            editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, filesystem_panel_visible(editor));
            return;
        }

        size_t const root = editor.root_split;
        size_t const old_focus = editor.focused_split;
        EditorSplitNode old_root = editor.split_nodes[root];
        old_root.parent = root;

        size_t const old_child = editor.split_nodes.size();
        bool ok = editor.split_nodes.push_back(old_root);
        DEBUG_ASSERT(ok);
        (void)ok;
        if (split_valid(editor, old_root.first)) {
            editor.split_nodes[old_root.first].parent = old_child;
        }
        if (split_valid(editor, old_root.second)) {
            editor.split_nodes[old_root.second].parent = old_child;
        }

        EditorPane* const filesystem = new_pane(editor);
        filesystem->kind = EditorPaneKind::FILESYSTEM;
        size_t const filesystem_pane = editor.panes.size() - 1u;
        size_t const filesystem_child = editor.split_nodes.size();
        ok = editor.split_nodes.push_back({
            .parent = root,
            .pane = filesystem_pane,
            .rect = editor.split_nodes[root].rect,
        });
        DEBUG_ASSERT(ok);
        (void)ok;

        editor.split_nodes[root] = {
            .kind = EditorSplitKind::VERTICAL,
            .parent = INVALID_INDEX,
            .first = filesystem_child,
            .second = old_child,
            .ratio = std::clamp(
                editor.sidebar_width_percent, SIDEBAR_MIN_WIDTH_PERCENT, SIDEBAR_MAX_WIDTH_PERCENT
            ),
            .rect = editor.split_nodes[root].rect,
        };
        if (old_focus == root) {
            editor.focused_split = old_child;
        }
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, true);
    }

    auto close_filesystem_panels(EditorState& editor) -> void {
        size_t split = find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::FILESYSTEM);
        while (split != INVALID_INDEX && editor_split_leaf_count(editor) > 1u) {
            focus_editor_split(editor, split);
            close_focused_split(editor);
            split = find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::FILESYSTEM);
        }
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, false);
        if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
            focus_first_code_split(editor);
        }
    }

    auto toggle_filesystem_panel(EditorState& editor) -> void {
        if (filesystem_panel_visible(editor)) {
            close_filesystem_panels(editor);
        } else {
            ensure_filesystem_panel(editor);
        }
    }

    auto split_focused(EditorState& editor, EditorSplitKind kind) -> void {
        if (!split_leaf(editor, editor.focused_split)) {
            return;
        }
        load_focused_pane(editor);

        size_t const old_split = editor.focused_split;
        size_t const old_pane = editor.split_nodes[old_split].pane;
        EditorPane* const new_editor_pane = new_pane(editor);
        size_t const new_pane_index = editor.panes.size() - 1u;
        clone_active_to_pane(editor, *new_editor_pane);

        store_focused_pane(editor);

        size_t const first = editor.split_nodes.size();
        bool ok = editor.split_nodes.push_back({
            .parent = old_split,
            .pane = old_pane,
            .rect = editor.split_nodes[old_split].rect,
        });
        DEBUG_ASSERT(ok);
        (void)ok;

        size_t const second = editor.split_nodes.size();
        ok = editor.split_nodes.push_back({
            .parent = old_split,
            .pane = new_pane_index,
            .rect = editor.split_nodes[old_split].rect,
        });
        DEBUG_ASSERT(ok);
        (void)ok;

        EditorSplitNode& node = editor.split_nodes[old_split];
        node.kind = kind;
        node.first = first;
        node.second = second;
        node.pane = 0u;
        node.ratio = 0.5f;

        editor.focused_split = second;
        load_focused_pane(editor);
    }

    auto close_focused_split(EditorState& editor) -> void {
        if (!split_leaf(editor, editor.focused_split)) {
            return;
        }
        if (focused_pane_kind(editor) == EditorPaneKind::CODE &&
            leaf_count_by_kind(editor, editor.root_split, EditorPaneKind::CODE) <= 1u) {
            editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
            return;
        }
        if (editor.focused_split == editor.root_split) {
            return;
        }
        sync_shared_panes(editor);

        size_t const split = editor.focused_split;
        size_t const parent = editor.split_nodes[split].parent;
        if (!split_valid(editor, parent)) {
            return;
        }

        EditorSplitNode const parent_node = editor.split_nodes[parent];
        size_t const sibling = parent_node.first == split ? parent_node.second : parent_node.first;
        if (!split_valid(editor, sibling)) {
            return;
        }
        size_t const closed_pane = editor.split_nodes[split].pane;

        editor.split_nodes[parent] = editor.split_nodes[sibling];
        editor.split_nodes[parent].parent = parent_node.parent;
        if (split_valid(editor, editor.split_nodes[parent].first)) {
            editor.split_nodes[editor.split_nodes[parent].first].parent = parent;
        }
        if (split_valid(editor, editor.split_nodes[parent].second)) {
            editor.split_nodes[editor.split_nodes[parent].second].parent = parent;
        }
        DEBUG_ASSERT(closed_pane < editor.panes.size());
        editor.panes[closed_pane] = nullptr;

        editor.set_flag(EditorFlag::PANE_LOADED, false);
        editor.focused_split = first_leaf(editor, parent);
        load_focused_pane(editor);
        clamp_cursor(editor);
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, filesystem_panel_visible(editor));
    }

    [[nodiscard]] auto rect_valid(gui::Rect rect) -> bool {
        return rect.max.x > rect.min.x && rect.max.y > rect.min.y;
    }

    [[nodiscard]] auto rect_center(gui::Rect rect) -> gui::Vec2 {
        return {
            (rect.min.x + rect.max.x) * 0.5f,
            (rect.min.y + rect.max.y) * 0.5f,
        };
    }

    auto find_split_in_direction(
        EditorState const& editor,
        size_t split,
        gui::Rect current_rect,
        char direction,
        size_t& best_split,
        float& best_score
    ) -> void {
        if (!split_valid(editor, split)) {
            return;
        }

        EditorSplitNode const& node = editor.split_nodes[split];
        if (node.kind != EditorSplitKind::LEAF) {
            find_split_in_direction(
                editor, node.first, current_rect, direction, best_split, best_score
            );
            find_split_in_direction(
                editor, node.second, current_rect, direction, best_split, best_score
            );
            return;
        }
        if (split == editor.focused_split || !rect_valid(node.rect)) {
            return;
        }

        gui::Vec2 const from = rect_center(current_rect);
        gui::Vec2 const to = rect_center(node.rect);
        float directional = 0.0f;
        float perpendicular = 0.0f;
        if (direction == 'h') {
            if (to.x >= from.x) {
                return;
            }
            directional = from.x - to.x;
            perpendicular = std::abs(from.y - to.y);
        } else if (direction == 'l') {
            if (to.x <= from.x) {
                return;
            }
            directional = to.x - from.x;
            perpendicular = std::abs(from.y - to.y);
        } else if (direction == 'k') {
            if (to.y >= from.y) {
                return;
            }
            directional = from.y - to.y;
            perpendicular = std::abs(from.x - to.x);
        } else if (direction == 'j') {
            if (to.y <= from.y) {
                return;
            }
            directional = to.y - from.y;
            perpendicular = std::abs(from.x - to.x);
        }

        float const score = perpendicular * 1000.0f + directional;
        if (score < best_score) {
            best_score = score;
            best_split = split;
        }
    }

    auto focus_split_in_direction(EditorState& editor, char direction) -> void {
        size_t const best_split = split_in_direction(editor, direction);
        if (best_split != INVALID_INDEX) {
            focus_editor_split(editor, best_split);
        }
    }

    [[nodiscard]] auto split_in_direction(EditorState const& editor, char direction) -> size_t {
        if (!split_leaf(editor, editor.focused_split)) {
            return INVALID_INDEX;
        }
        gui::Rect const current_rect = editor.split_nodes[editor.focused_split].rect;
        if (!rect_valid(current_rect)) {
            return INVALID_INDEX;
        }

        size_t best_split = INVALID_INDEX;
        float best_score = 3.402823466e38f;
        find_split_in_direction(
            editor, editor.root_split, current_rect, direction, best_split, best_score
        );
        return best_split;
    }

    auto swap_split_in_direction(EditorState& editor, char direction) -> void {
        size_t const target = split_in_direction(editor, direction);
        if (target == INVALID_INDEX) {
            return;
        }

        sync_shared_panes(editor);
        store_focused_pane(editor);

        size_t const source = editor.focused_split;
        size_t const source_pane = editor.split_nodes[source].pane;
        editor.split_nodes[source].pane = editor.split_nodes[target].pane;
        editor.split_nodes[target].pane = source_pane;
        editor.focused_split = target;
        load_focused_pane(editor);
        clamp_cursor(editor);
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, filesystem_panel_visible(editor));
    }

    [[nodiscard]] auto copy_editor_text(EditorState& editor) -> StrRef {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        return text_buffer_copy(editor.text, *editor.text.arena);
    }

    auto mark_editor_saved(EditorState& editor) -> void {
        if (editor.text.arena == nullptr) {
            return;
        }
        editor.saved_text = copy_editor_text(editor);
        editor.set_flag(EditorFlag::DIRTY, false);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
    }

    auto refresh_editor_dirty(EditorState& editor) -> void {
        if (editor.text.arena == nullptr) {
            return;
        }
        editor.set_flag(EditorFlag::DIRTY, copy_editor_text(editor) != editor.saved_text);
        if (!editor.flag(EditorFlag::DIRTY)) {
            editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
        }
    }

    auto push_editor_snapshot(EditorState& editor, EditorUndoEntry*& stack) -> void {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        EditorUndoEntry* const entry = arena_new<EditorUndoEntry>(*editor.text.arena);
        StrRef const text = copy_editor_text(editor);
        entry->text = text.data();
        entry->text_size = text.size();
        entry->cursor_line = editor.cursor_line;
        entry->cursor_column = editor.cursor_column;
        entry->selection_anchor_line = editor.selection_anchor_line;
        entry->selection_anchor_column = editor.selection_anchor_column;
        entry->selection_mode = editor.selection_mode;
        entry->scroll_y = editor.scroll_y;
        entry->selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        entry->previous = stack;
        stack = entry;
    }

    auto save_editor_undo(EditorState& editor) -> void {
        push_editor_snapshot(editor, editor.undo_stack);
        editor.redo_stack = nullptr;
        editor.set_flag(EditorFlag::DIRTY, true);
    }

    auto restore_editor_snapshot(EditorState& editor, EditorUndoEntry const& entry) -> void {
        set_editor_text_impl(editor, StrRef(entry.text, entry.text_size), false, false);
        editor.cursor_line = entry.cursor_line;
        editor.cursor_column = entry.cursor_column;
        editor.preferred_column = editor.cursor_column;
        editor.selection_anchor_line = entry.selection_anchor_line;
        editor.selection_anchor_column = entry.selection_anchor_column;
        editor.selection_mode = entry.selection_mode;
        editor.scroll_y = entry.scroll_y;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, entry.selection_active);
        clamp_cursor(editor);
    }

    [[nodiscard]] auto
    restore_editor_stack(EditorState& editor, EditorUndoEntry*& from, EditorUndoEntry*& to)
        -> bool {
        if (from == nullptr) {
            return false;
        }

        push_editor_snapshot(editor, to);
        EditorUndoEntry const* const entry = from;
        from = entry->previous;
        restore_editor_snapshot(editor, *entry);
        refresh_editor_dirty(editor);
        return true;
    }

    [[nodiscard]] auto restore_editor_undo(EditorState& editor) -> bool {
        return restore_editor_stack(editor, editor.undo_stack, editor.redo_stack);
    }

    [[nodiscard]] auto restore_editor_redo(EditorState& editor) -> bool {
        return restore_editor_stack(editor, editor.redo_stack, editor.undo_stack);
    }

    auto save_scratch_file(EditorState& editor) -> void {
        if (editor.current_file_path.empty()) {
            editor.scratch_text = copy_editor_text(editor);
            mark_editor_saved(editor);
        }
    }

    auto open_scratch_file(EditorState& editor) -> void {
        set_editor_text(editor, editor.scratch_text);
        editor.current_file_name = SCRATCH_FILE_NAME;
        editor.current_file_path = {};
        editor.file_write_stamp = 0u;
        remember_open_file(editor, SCRATCH_FILE_NAME, {});
        BASE_UNUSED(restore_focused_open_file_view(editor, SCRATCH_FILE_NAME, {}));
    }

    [[nodiscard]] auto save_root_without_trailing_slash(StrRef path) -> StrRef {
        while (path.size() > 1u && (path.back() == '\\' || path.back() == '/') &&
               !(path.size() == 3u && path[1u] == ':')) {
            path.remove_suffix(1u);
        }
        return path;
    }

    auto clear_save_path_text(EditorState& editor) -> void {
        editor.save_path_text[0u] = '\0';
    }

    [[nodiscard]] auto append_save_path_text(EditorState& editor, StrRef text) -> bool {
        size_t const size = cstr_len(editor.save_path_text);
        if (text.size() >= SAVE_PATH_TEXT_CAPACITY ||
            size > SAVE_PATH_TEXT_CAPACITY - text.size() - 1u) {
            return false;
        }
        if (!text.empty()) {
            std::memcpy(editor.save_path_text + size, text.data(), text.size());
        }
        editor.save_path_text[size + text.size()] = '\0';
        return true;
    }

    auto open_save_path_popup(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::SAVE_REQUESTED, false);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, true);
        editor.save_path_error = EditorSavePathError::NONE;
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        clear_save_path_text(editor);

        StrRef const root = save_root_without_trailing_slash(editor.save_root_path);
        BASE_UNUSED(append_save_path_text(editor, root));
        if (!root.empty() && !root.ends_with('\\') && !root.ends_with('/')) {
            BASE_UNUSED(append_save_path_text(editor, "\\"));
        }
        if (editor.current_file_name != SCRATCH_FILE_NAME) {
            BASE_UNUSED(append_save_path_text(editor, editor.current_file_name));
        }
    }

    auto close_save_path_popup(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
    }

    auto request_editor_save(EditorState& editor) -> void {
        if (focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return;
        }
        if (editor.current_file_path.empty()) {
            open_save_path_popup(editor);
            return;
        }
        editor.set_flag(EditorFlag::SAVE_REQUESTED, true);
    }

    [[nodiscard]] auto line_size(EditorState const& editor, size_t line) -> size_t {
        return text_buffer_line_size(editor.text, line);
    }

    struct EditorPosition {
        size_t line = 0u;
        size_t column = 0u;
    };

    [[nodiscard]] auto text_position(EditorPosition position) -> EditorTextPosition {
        return {position.line, position.column};
    }

    [[nodiscard]] auto position_offset(EditorState const& editor, EditorPosition position)
        -> size_t {
        return text_buffer_position_to_offset(editor.text, text_position(position));
    }

    [[nodiscard]] auto position_less(EditorPosition lhs, EditorPosition rhs) -> bool {
        return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.column < rhs.column);
    }

    [[nodiscard]] auto same_position(EditorPosition lhs, EditorPosition rhs) -> bool {
        return lhs.line == rhs.line && lhs.column == rhs.column;
    }

    [[nodiscard]] auto cursor_position(EditorState const& editor) -> EditorPosition {
        return {editor.cursor_line, editor.cursor_column};
    }

    [[nodiscard]] auto previous_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition;

    [[nodiscard]] auto next_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition;

    [[nodiscard]] auto selection_anchor(EditorState const& editor) -> EditorPosition {
        return {editor.selection_anchor_line, editor.selection_anchor_column};
    }

    [[nodiscard]] auto clamp_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        position.line = std::min(position.line, editor_line_count(editor) - 1u);
        position.column = std::min(position.column, line_size(editor, position.line));
        return position;
    }

    [[nodiscard]] auto character_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        position = clamp_position(editor, position);
        size_t const size = line_size(editor, position.line);
        if (position.column == size && size != 0u) {
            position.column -= 1u;
        }
        return position;
    }

    auto clear_selection(EditorState& editor) -> void {
        editor.selection_anchor_line = editor.cursor_line;
        editor.selection_anchor_column = editor.cursor_column;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
    }

    auto clamp_selection(EditorState& editor) -> void {
        EditorPosition const anchor =
            clamp_position(editor, {editor.selection_anchor_line, editor.selection_anchor_column});
        editor.selection_anchor_line = anchor.line;
        editor.selection_anchor_column = anchor.column;
        editor.set_flag(
            EditorFlag::SELECTION_ACTIVE,
            editor.selection_mode != EditorSelectionMode::NONE ||
                (editor.flag(EditorFlag::SELECTION_ACTIVE) &&
                 !same_position(anchor, cursor_position(editor)))
        );
    }

    auto move_cursor_to(EditorState& editor, EditorPosition position, bool select) -> void {
        if (select && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            editor.selection_anchor_line = editor.cursor_line;
            editor.selection_anchor_column = editor.cursor_column;
        }
        position = clamp_position(editor, position);
        editor.cursor_line = position.line;
        editor.cursor_column = position.column;
        editor.preferred_column = editor.cursor_column;
        if (select) {
            editor.set_flag(
                EditorFlag::SELECTION_ACTIVE,
                editor.selection_mode != EditorSelectionMode::NONE ||
                    !same_position(selection_anchor(editor), position)
            );
        } else {
            clear_selection(editor);
        }
    }

    [[nodiscard]] auto editor_selection_range(EditorState const& editor) -> EditorSelectionRange {
        if (!editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            return {};
        }

        EditorPosition cursor = cursor_position(editor);
        EditorPosition anchor = selection_anchor(editor);
        if (editor.selection_mode == EditorSelectionMode::LINE) {
            size_t const start_line = std::min(cursor.line, anchor.line);
            size_t const end_line = std::max(cursor.line, anchor.line);
            if (end_line + 1u < editor_line_count(editor)) {
                return {
                    .start_line = start_line,
                    .start_column = 0u,
                    .end_line = end_line + 1u,
                    .end_column = 0u,
                    .active = true,
                    .full_line = true,
                };
            }
            return {
                .start_line = start_line,
                .start_column = 0u,
                .end_line = end_line,
                .end_column = line_size(editor, end_line),
                .active = true,
                .full_line = true,
            };
        }
        if (same_position(cursor, anchor)) {
            if (editor.selection_mode != EditorSelectionMode::CHARACTER) {
                return {};
            }
        }

        if (editor.selection_mode == EditorSelectionMode::CHARACTER) {
            cursor = character_position(editor, cursor);
            anchor = character_position(editor, anchor);
        }
        EditorPosition const start = position_less(cursor, anchor) ? cursor : anchor;
        EditorPosition end = position_less(cursor, anchor) ? anchor : cursor;
        if (editor.selection_mode == EditorSelectionMode::CHARACTER) {
            EditorPosition const next = next_position(editor, end);
            if (!same_position(next, end)) {
                end = next;
            }
        }
        return {
            .start_line = start.line,
            .start_column = start.column,
            .end_line = end.line,
            .end_column = end.column,
            .active = true,
        };
    }

    auto clamp_cursor(EditorState& editor) -> void {
        if (editor_line_count(editor) == 0u) {
            insert_line(editor, 0u, "");
        }
        editor.cursor_line = std::min(editor.cursor_line, editor_line_count(editor) - 1u);
        editor.cursor_column =
            std::min(editor.cursor_column, line_size(editor, editor.cursor_line));
        editor.preferred_column = std::min(editor.preferred_column, editor.cursor_column);
        clamp_selection(editor);
    }

    auto set_cursor(EditorState& editor, size_t line, size_t column) -> void {
        move_cursor_to(editor, {line, column}, false);
    }

    auto set_editor_cursor(EditorState& editor, size_t line, size_t column) -> void {
        set_cursor(editor, line, column);
    }

    auto move_vertical(EditorState& editor, int32_t delta, bool select) -> void {
        size_t line = editor.cursor_line;
        if (delta < 0) {
            size_t const amount = static_cast<size_t>(-delta);
            line = amount < line ? line - amount : 0u;
        } else {
            line = std::min(line + static_cast<size_t>(delta), editor_line_count(editor) - 1u);
        }
        EditorPosition position = {
            line, std::min(editor.preferred_column, line_size(editor, line))
        };
        if (select && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            editor.selection_anchor_line = editor.cursor_line;
            editor.selection_anchor_column = editor.cursor_column;
        }
        position = clamp_position(editor, position);
        editor.cursor_line = position.line;
        editor.cursor_column = position.column;
        if (select) {
            editor.set_flag(
                EditorFlag::SELECTION_ACTIVE,
                editor.selection_mode != EditorSelectionMode::NONE ||
                    !same_position(selection_anchor(editor), position)
            );
        } else {
            clear_selection(editor);
        }
    }

    auto move_half_split(EditorState& editor, int32_t direction) -> void {
        gui::Rect const rect = split_valid(editor, editor.focused_split)
                                   ? editor.split_nodes[editor.focused_split].rect
                                   : gui::Rect{};
        gui::Rect const content = editor_content_rect(rect);
        float const line_height = editor_line_height(editor);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        size_t const lines =
            std::max<size_t>(1u, static_cast<size_t>(visible_height / line_height * 0.5f));
        move_vertical(editor, static_cast<int32_t>(lines) * direction, false);
        editor.scroll_y += static_cast<float>(direction) * static_cast<float>(lines) * line_height;
        clamp_scroll(editor, rect);
    }

    auto move_left(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, previous_position(editor, cursor_position(editor)), select);
    }

    auto move_right(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, next_position(editor, cursor_position(editor)), select);
    }

    [[nodiscard]] auto at_text_begin(EditorPosition position) -> bool {
        return position.line == 0u && position.column == 0u;
    }

    [[nodiscard]] auto at_text_end(EditorState const& editor, EditorPosition position) -> bool {
        return position.line + 1u == editor_line_count(editor) &&
               position.column == line_size(editor, position.line);
    }

    [[nodiscard]] auto previous_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        position = clamp_position(editor, position);
        if (position.column != 0u) {
            position.column -= 1u;
        } else if (position.line != 0u) {
            position.line -= 1u;
            position.column = line_size(editor, position.line);
        }
        return position;
    }

    [[nodiscard]] auto next_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        position = clamp_position(editor, position);
        if (position.column < line_size(editor, position.line)) {
            position.column += 1u;
        } else if (position.line + 1u < editor_line_count(editor)) {
            position.line += 1u;
            position.column = 0u;
        }
        return position;
    }

    [[nodiscard]] auto text_word_char(EditorState const& editor, EditorPosition position) -> bool {
        position = clamp_position(editor, position);
        EditorLine const& line = editor_line(editor, position.line);
        if (position.column >= line.size) {
            return false;
        }
        uint8_t const ch = static_cast<uint8_t>(line.text[position.column]);
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch >= 0x80u;
    }

    [[nodiscard]] auto text_non_space_char(EditorState const& editor, EditorPosition position)
        -> bool {
        position = clamp_position(editor, position);
        EditorLine const& line = editor_line(editor, position.line);
        return position.column < line.size &&
               static_cast<uint8_t>(line.text[position.column]) > ' ';
    }

    [[nodiscard]] auto previous_run_start_position(
        EditorState const& editor,
        EditorPosition position,
        bool (*run_char)(EditorState const&, EditorPosition)
    ) -> EditorPosition {
        position = clamp_position(editor, position);
        while (!at_text_begin(position)) {
            EditorPosition const previous = previous_position(editor, position);
            if (run_char(editor, previous)) {
                break;
            }
            position = previous;
        }
        while (!at_text_begin(position)) {
            EditorPosition const previous = previous_position(editor, position);
            if (!run_char(editor, previous)) {
                break;
            }
            position = previous;
        }
        return position;
    }

    [[nodiscard]] auto next_run_start_position(
        EditorState const& editor,
        EditorPosition position,
        bool (*run_char)(EditorState const&, EditorPosition)
    ) -> EditorPosition {
        position = clamp_position(editor, position);
        while (!at_text_end(editor, position) && run_char(editor, position)) {
            position = next_position(editor, position);
        }
        while (!at_text_end(editor, position) && !run_char(editor, position)) {
            position = next_position(editor, position);
        }
        return position;
    }

    [[nodiscard]] auto next_run_end_position(
        EditorState const& editor,
        EditorPosition position,
        bool (*run_char)(EditorState const&, EditorPosition)
    ) -> EditorPosition {
        EditorPosition const original = clamp_position(editor, position);
        position = original;
        if (!at_text_end(editor, position) && run_char(editor, position)) {
            EditorPosition const next = next_position(editor, position);
            if (at_text_end(editor, next) || !run_char(editor, next)) {
                position = next;
            }
        }
        while (!at_text_end(editor, position) && !run_char(editor, position)) {
            position = next_position(editor, position);
        }
        if (at_text_end(editor, position)) {
            return original;
        }

        EditorPosition result = position;
        while (!at_text_end(editor, position) && run_char(editor, position)) {
            result = position;
            position = next_position(editor, position);
        }
        return result;
    }

    [[nodiscard]] auto previous_word_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return previous_run_start_position(editor, position, text_word_char);
    }

    [[nodiscard]] auto next_word_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return next_run_start_position(editor, position, text_word_char);
    }

    [[nodiscard]] auto next_word_end_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return next_run_end_position(editor, position, text_word_char);
    }

    [[nodiscard]] auto
    previous_big_word_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return previous_run_start_position(editor, position, text_non_space_char);
    }

    [[nodiscard]] auto next_big_word_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return next_run_start_position(editor, position, text_non_space_char);
    }

    [[nodiscard]] auto
    next_big_word_end_position(EditorState const& editor, EditorPosition position)
        -> EditorPosition {
        return next_run_end_position(editor, position, text_non_space_char);
    }

    auto move_word_left(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, previous_word_position(editor, cursor_position(editor)), select);
    }

    auto move_word_right(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, next_word_position(editor, cursor_position(editor)), select);
    }

    auto move_word_end(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, next_word_end_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_left(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, previous_big_word_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_right(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, next_big_word_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_end(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, next_big_word_end_position(editor, cursor_position(editor)), select);
    }

    auto erase_range(EditorState& editor, EditorPosition start, EditorPosition end) -> void {
        start = clamp_position(editor, start);
        end = clamp_position(editor, end);
        if (position_less(end, start)) {
            EditorPosition const temp = start;
            start = end;
            end = temp;
        }
        if (same_position(start, end)) {
            return;
        }

        text_buffer_erase(
            editor.text, position_offset(editor, start), position_offset(editor, end)
        );
        set_cursor(editor, start.line, start.column);
    }

    auto delete_line_at(EditorState& editor, size_t line) -> void {
        line = std::min(line, editor_line_count(editor) - 1u);
        if (editor_line_count(editor) == 1u) {
            text_buffer_erase(editor.text, 0u, text_buffer_size(editor.text));
            return;
        }

        EditorPosition start = {line, 0u};
        EditorPosition end = {};
        if (line + 1u < editor_line_count(editor)) {
            end = {line + 1u, 0u};
        } else {
            start = {line - 1u, line_size(editor, line - 1u)};
            end = {line, line_size(editor, line)};
        }
        text_buffer_erase(
            editor.text, position_offset(editor, start), position_offset(editor, end)
        );
    }

    [[nodiscard]] auto erase_selection(EditorState& editor) -> bool {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (!selection.active) {
            return false;
        }
        if (selection.full_line) {
            size_t const first = selection.start_line;
            size_t const end = selection.end_column == 0u && selection.end_line > first
                                   ? selection.end_line
                                   : selection.end_line + 1u;
            size_t count = std::min(end, editor_line_count(editor)) - first;
            while (count != 0u && editor_line_count(editor) > 1u) {
                delete_line_at(editor, std::min(first, editor_line_count(editor) - 1u));
                count -= 1u;
            }
            if (count != 0u) {
                text_buffer_erase(editor.text, 0u, text_buffer_size(editor.text));
            }
            set_cursor(editor, std::min(first, editor_line_count(editor) - 1u), 0u);
            return true;
        }
        erase_range(
            editor,
            {selection.start_line, selection.start_column},
            {selection.end_line, selection.end_column}
        );
        return true;
    }

    auto select_all(EditorState& editor) -> void {
        editor.selection_anchor_line = 0u;
        editor.selection_anchor_column = 0u;
        editor.cursor_line = editor_line_count(editor) - 1u;
        editor.cursor_column = line_size(editor, editor.cursor_line);
        editor.preferred_column = editor.cursor_column;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(
            EditorFlag::SELECTION_ACTIVE, editor.cursor_line != 0u || editor.cursor_column != 0u
        );
    }

    [[nodiscard]] auto copy_selected_text(EditorState& editor) -> StrRef {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (!selection.active) {
            return {};
        }
        DEBUG_ASSERT(editor.text.arena != nullptr);
        return text_buffer_copy_range(
            editor.text,
            *editor.text.arena,
            position_offset(editor, {selection.start_line, selection.start_column}),
            position_offset(editor, {selection.end_line, selection.end_column})
        );
    }

    auto insert_char(EditorState& editor, char value) -> void {
        (void)erase_selection(editor);
        char const text[] = {value};
        text_buffer_insert(
            editor.text, position_offset(editor, cursor_position(editor)), StrRef(text, 1u)
        );
        editor.cursor_column += 1u;
        editor.preferred_column = editor.cursor_column;
        clear_selection(editor);
    }

    auto insert_newline(EditorState& editor) -> void;

    auto insert_text(EditorState& editor, StrRef text) -> void {
        for (size_t index = 0u; index < text.size(); ++index) {
            char const ch = text[index];
            if (ch == '\r') {
                insert_newline(editor);
                if (index + 1u < text.size() && text[index + 1u] == '\n') {
                    index += 1u;
                }
            } else if (ch == '\n') {
                insert_newline(editor);
            } else if (ch == '\t' || static_cast<uint8_t>(ch) >= 32u) {
                insert_char(editor, ch);
            }
        }
    }

    auto insert_newline(EditorState& editor) -> void {
        (void)erase_selection(editor);
        text_buffer_insert(editor.text, position_offset(editor, cursor_position(editor)), "\n");
        set_cursor(editor, editor.cursor_line + 1u, 0u);
    }

    auto backspace(EditorState& editor) -> void {
        if (erase_selection(editor)) {
            return;
        }
        if (editor.cursor_column != 0u) {
            EditorPosition const end = cursor_position(editor);
            EditorPosition const start = previous_position(editor, end);
            text_buffer_erase(
                editor.text, position_offset(editor, start), position_offset(editor, end)
            );
            editor.cursor_column -= 1u;
            editor.preferred_column = editor.cursor_column;
            clear_selection(editor);
            return;
        }
        if (editor.cursor_line == 0u) {
            return;
        }

        size_t const current_line = editor.cursor_line;
        size_t const column = line_size(editor, current_line - 1u);
        text_buffer_erase(
            editor.text,
            position_offset(editor, {current_line - 1u, column}),
            position_offset(editor, {current_line, 0u})
        );
        set_cursor(editor, current_line - 1u, column);
    }

    auto delete_char(EditorState& editor) -> void {
        if (erase_selection(editor)) {
            return;
        }
        if (editor.cursor_column >= line_size(editor, editor.cursor_line)) {
            return;
        }
        EditorPosition const start = cursor_position(editor);
        EditorPosition const end = next_position(editor, start);
        text_buffer_erase(
            editor.text, position_offset(editor, start), position_offset(editor, end)
        );
        clamp_cursor(editor);
        clear_selection(editor);
    }

    auto backspace_word(EditorState& editor) -> void {
        if (erase_selection(editor)) {
            return;
        }
        erase_range(
            editor, previous_word_position(editor, cursor_position(editor)), cursor_position(editor)
        );
    }

    auto delete_word(EditorState& editor) -> void {
        if (erase_selection(editor)) {
            return;
        }
        erase_range(
            editor, cursor_position(editor), next_word_position(editor, cursor_position(editor))
        );
    }

    auto delete_line(EditorState& editor) -> void {
        delete_line_at(editor, editor.cursor_line);
        clamp_cursor(editor);
        clear_selection(editor);
    }

    auto open_line(EditorState& editor, size_t line) -> void {
        size_t const insert_at = std::min(line, editor_line_count(editor));
        insert_line(editor, insert_at, "");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
        editor.pending_line_number = 0u;
        editor.set_flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE, false);
        set_cursor(editor, insert_at, 0u);
    }

    auto zoom_font(EditorState& editor, float amount) -> void {
        editor.font_size =
            std::clamp(editor.font_size + amount, EDITOR_MIN_FONT_SIZE, EDITOR_MAX_FONT_SIZE);
    }

    [[nodiscard]] auto font_zoom_key(gui::KeyEvent const& event) -> bool {
        if ((event.mods & gui::KEY_MOD_CTRL) == 0u) {
            return false;
        }
        return event.key == gui::Key::PLUS || event.key == gui::Key::MINUS;
    }

    [[nodiscard]] auto font_zoom_text(gui::KeyEvent const& event) -> bool {
        if ((event.mods & gui::KEY_MOD_CTRL) == 0u) {
            return false;
        }
        return event.codepoint == '+' || event.codepoint == '-' || event.codepoint == '=';
    }

    [[nodiscard]] auto editor_file_search_text(EditorState const& editor) -> StrRef {
        return StrRef(editor.file_search_text, editor.file_search_text_size);
    }

    [[nodiscard]] auto editor_command_text(EditorState const& editor) -> StrRef {
        return StrRef(editor.command_text, editor.command_text_size);
    }

    [[nodiscard]] auto editor_command_count() -> size_t {
        return sizeof(EDITOR_COMMANDS) / sizeof(EDITOR_COMMANDS[0u]);
    }

    [[nodiscard]] auto editor_command(size_t index) -> EditorCommand {
        return index < editor_command_count() ? EDITOR_COMMANDS[index] : EditorCommand{};
    }

    [[nodiscard]] auto editor_selected_command(EditorState const& editor) -> EditorCommand {
        return editor_command(std::min(editor.command_selected, editor_command_count() - 1u));
    }

    [[nodiscard]] auto file_search_entry_text(FileTreeEntry const& entry) -> StrRef {
        return !entry.relative_path.empty() ? entry.relative_path : entry.name;
    }

    [[nodiscard]] auto file_search_fuzzy_score(StrRef text, StrRef query, int32_t& out_score)
        -> bool {
        if (query.empty()) {
            out_score = 0;
            return true;
        }

        size_t text_index = 0u;
        size_t previous = StrRef::NPOS;
        int32_t score = 0;
        for (char query_ch : query) {
            bool found = false;
            while (text_index < text.size()) {
                if (to_ascii_lower(text[text_index]) == to_ascii_lower(query_ch)) {
                    size_t const gap =
                        previous == StrRef::NPOS ? text_index : text_index - previous - 1u;
                    score += static_cast<int32_t>(gap * 4u);
                    if (text_index == 0u || text[text_index - 1u] == '\\' ||
                        text[text_index - 1u] == '/' || text[text_index - 1u] == '_' ||
                        text[text_index - 1u] == '-' || text[text_index - 1u] == '.') {
                        score -= 8;
                    }
                    previous = text_index;
                    text_index += 1u;
                    found = true;
                    break;
                }
                text_index += 1u;
            }
            if (!found) {
                return false;
            }
        }

        score += static_cast<int32_t>(text.size() - query.size());
        out_score = score;
        return true;
    }

    [[nodiscard]] auto
    file_search_match_less(EditorState const& editor, FileSearchMatch lhs, FileSearchMatch rhs)
        -> bool {
        if (lhs.score != rhs.score) {
            return lhs.score < rhs.score;
        }
        StrRef const lhs_text = file_search_entry_text(editor.tree_files[lhs.tree_file_index]);
        StrRef const rhs_text = file_search_entry_text(editor.tree_files[rhs.tree_file_index]);
        int const compare = lhs_text.compare_ignore_ascii_case(rhs_text);
        return compare != 0 ? compare < 0 : lhs.tree_file_index < rhs.tree_file_index;
    }

    [[nodiscard]] auto
    collect_file_search_matches(EditorState const& editor, Slice<FileSearchMatch> matches)
        -> size_t {
        size_t count = 0u;
        StrRef const query = editor_file_search_text(editor);
        for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
            FileTreeEntry const& entry = editor.tree_files[index];
            if (entry.is_directory) {
                continue;
            }

            FileSearchMatch match = {.tree_file_index = index};
            if (!file_search_fuzzy_score(file_search_entry_text(entry), query, match.score)) {
                continue;
            }

            if (count < matches.size()) {
                size_t insert = count;
                while (insert > 0u && file_search_match_less(editor, match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
                count += 1u;
            } else if (!matches.empty() && file_search_match_less(editor, match, matches.back())) {
                size_t insert = matches.size() - 1u;
                while (insert > 0u && file_search_match_less(editor, match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
            }
        }
        return count;
    }

    [[nodiscard]] auto buffer_search_entry_text(OpenFile const& file) -> StrRef {
        return !file.name.empty() ? file.name : file.path;
    }

    [[nodiscard]] auto buffer_search_match_less(BufferSearchMatch lhs, BufferSearchMatch rhs)
        -> bool {
        if (lhs.score != rhs.score) {
            return lhs.score < rhs.score;
        }
        return lhs.open_file_index < rhs.open_file_index;
    }

    [[nodiscard]] auto
    collect_buffer_search_matches(EditorState const& editor, Slice<BufferSearchMatch> matches)
        -> size_t {
        size_t count = 0u;
        StrRef const query = editor_file_search_text(editor);
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            BufferSearchMatch match = {.open_file_index = index};
            if (!file_search_fuzzy_score(
                    buffer_search_entry_text(editor.open_files[index]), query, match.score
                )) {
                continue;
            }

            if (count < matches.size()) {
                size_t insert = count;
                while (insert > 0u && buffer_search_match_less(match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
                count += 1u;
            } else if (!matches.empty() && buffer_search_match_less(match, matches.back())) {
                size_t insert = matches.size() - 1u;
                while (insert > 0u && buffer_search_match_less(match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
            }
        }
        return count;
    }

    [[nodiscard]] auto search_match_count(EditorState const& editor, bool buffers) -> size_t {
        if (buffers) {
            BufferSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
            return collect_buffer_search_matches(editor, matches);
        }
        FileSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
        return collect_file_search_matches(editor, matches);
    }

    auto clamp_search_selected(EditorState& editor, bool buffers) -> void {
        size_t const count = search_match_count(editor, buffers);
        editor.file_search_selected =
            count == 0u ? 0u : std::min(editor.file_search_selected, count - 1u);
    }

    auto open_file_search(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, true);
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.file_search_selected = 0u;
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
    }

    auto close_file_search(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_selected = 0u;
    }

    auto open_buffer_search(EditorState& editor) -> void {
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, true);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.file_search_selected = 0u;
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            OpenFile const& file = editor.open_files[index];
            if (same_editor_file(
                    file.name, file.path, editor.current_file_name, editor.current_file_path
                )) {
                editor.file_search_selected = index;
                break;
            }
        }
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
    }

    auto close_buffer_search(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.file_search_selected = 0u;
    }

    auto clear_command_line(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.command_text_size = 0u;
        editor.command_selected = 0u;
        editor.command_text[0u] = '\0';
    }

    auto select_command_match(EditorState& editor) -> void {
        StrRef const text = editor_command_text(editor).trim();
        if (text.empty()) {
            editor.command_selected =
                std::min(editor.command_selected, editor_command_count() - 1u);
            return;
        }
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const command = editor_command(index);
            if (command.name.starts_with_ignore_ascii_case(text) ||
                command.alias.starts_with_ignore_ascii_case(text)) {
                editor.command_selected = index;
                return;
            }
        }
    }

    auto set_command_text(EditorState& editor, StrRef text) -> void {
        editor.command_text_size = text.copy_to(editor.command_text, COMMAND_TEXT_CAPACITY - 1u);
        editor.command_text[editor.command_text_size] = '\0';
    }

    auto complete_command_line(EditorState& editor) -> void {
        EditorCommand const selected = editor_selected_command(editor);
        if (editor_command_text(editor).equals_ignore_ascii_case(selected.name)) {
            editor.command_selected = (editor.command_selected + 1u) % editor_command_count();
        }
        set_command_text(editor, editor_selected_command(editor).name);
    }

    auto open_command_line(EditorState& editor) -> void {
        clear_command_line(editor);
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, true);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
    }

    auto run_editor_command(EditorState& editor, size_t index) -> void {
        switch (index) {
        case 0u:
            request_editor_save(editor);
            break;
        case 1u:
            close_focused_split(editor);
            break;
        case 2u:
            editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, true);
            break;
        case 3u:
            open_file_search(editor);
            break;
        case 4u:
            toggle_filesystem_panel(editor);
            break;
        case 5u:
            request_lsp(editor, LspRequestKind::FORMATTING);
            break;
        case 6u:
            request_lsp(editor, LspRequestKind::DOCUMENT_SYMBOL);
            break;
        default:
            break;
        }
    }

    auto run_command_line(EditorState& editor) -> void {
        StrRef const text = editor_command_text(editor).trim();
        if (text.empty()) {
            clear_command_line(editor);
            return;
        }
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const command = editor_command(index);
            if (text.equals_ignore_ascii_case(command.name) ||
                text.equals_ignore_ascii_case(command.alias)) {
                run_editor_command(editor, index);
                break;
            }
        }
        clear_command_line(editor);
    }

    auto handle_command_line_event(EditorState& editor, gui::KeyEvent const& event) -> void {
        if (event.kind == gui::KeyEventKind::TEXT) {
            bool const text_input =
                (event.mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u;
            if (text_input && event.codepoint >= 32u && event.codepoint <= 126u &&
                editor.command_text_size + 1u < COMMAND_TEXT_CAPACITY) {
                editor.command_text[editor.command_text_size] = static_cast<char>(event.codepoint);
                editor.command_text_size += 1u;
                editor.command_text[editor.command_text_size] = '\0';
                select_command_match(editor);
            }
            return;
        }

        if (event.kind != gui::KeyEventKind::PRESS && event.kind != gui::KeyEventKind::REPEAT) {
            return;
        }

        if (event.key == gui::Key::ESCAPE) {
            clear_command_line(editor);
        } else if (event.key == gui::Key::ENTER) {
            run_command_line(editor);
        } else if (event.key == gui::Key::TAB) {
            complete_command_line(editor);
        } else if (event.key == gui::Key::BACKSPACE && editor.command_text_size != 0u) {
            editor.command_text_size -= 1u;
            editor.command_text[editor.command_text_size] = '\0';
            select_command_match(editor);
        } else if (event.key == gui::Key::BACKSPACE) {
            clear_command_line(editor);
        }
    }

    auto select_file_search_match(EditorState& editor) -> void {
        FileSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = collect_file_search_matches(editor, matches);
        if (count == 0u) {
            return;
        }
        editor.file_search_selected = std::min(editor.file_search_selected, count - 1u);
        editor.file_search_open_file = matches[editor.file_search_selected].tree_file_index;
        close_file_search(editor);
    }

    auto select_buffer_search_match(EditorState& editor) -> void {
        BufferSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = collect_buffer_search_matches(editor, matches);
        if (count == 0u) {
            return;
        }
        editor.file_search_selected = std::min(editor.file_search_selected, count - 1u);
        editor.buffer_search_open_file = matches[editor.file_search_selected].open_file_index;
        close_buffer_search(editor);
    }

    auto handle_search_event(EditorState& editor, gui::KeyEvent const& event, bool buffers)
        -> void {
        if (event.kind != gui::KeyEventKind::PRESS && event.kind != gui::KeyEventKind::REPEAT) {
            return;
        }
        switch (event.key) {
        case gui::Key::ESCAPE:
            if (buffers) {
                close_buffer_search(editor);
            } else {
                close_file_search(editor);
            }
            break;
        case gui::Key::ENTER:
            if (buffers) {
                select_buffer_search_match(editor);
            } else {
                select_file_search_match(editor);
            }
            break;
        case gui::Key::UP:
            if (editor.file_search_selected != 0u) {
                editor.file_search_selected -= 1u;
            }
            break;
        case gui::Key::DOWN: {
            size_t const count = search_match_count(editor, buffers);
            if (editor.file_search_selected + 1u < count) {
                editor.file_search_selected += 1u;
            }
            break;
        }
        default:
            break;
        }
    }

    [[nodiscard]] auto to_ascii_upper(char value) -> char {
        return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A')) : value;
    }

    [[nodiscard]] auto switch_ascii_case(char value) -> char {
        if (value >= 'a' && value <= 'z') {
            return to_ascii_upper(value);
        }
        return value >= 'A' && value <= 'Z' ? to_ascii_lower(value) : value;
    }

    auto clear_pending_line_number(EditorState& editor) -> void {
        editor.pending_line_number = 0u;
        editor.set_flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE, false);
    }

    auto enter_insert_at(EditorState& editor, EditorPosition position) -> void {
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
        clear_pending_line_number(editor);
        set_cursor(editor, position.line, position.column);
    }

    auto enter_insert_mode(EditorState& editor, size_t column) -> void {
        enter_insert_at(editor, {editor.cursor_line, column});
    }

    auto center_cursor(EditorState& editor, gui::Rect rect) -> void {
        gui::Rect const content = editor_content_rect(rect);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        float const line_height = editor_line_height(editor);
        float const cursor_center = (static_cast<float>(editor.cursor_line) + 0.5f) * line_height;
        editor.scroll_y = cursor_center - visible_height * 0.5f;
        clamp_scroll(editor, rect);
    }

    [[nodiscard]] auto visual_selecting(EditorState const& editor) -> bool {
        return editor.selection_mode != EditorSelectionMode::NONE;
    }

    auto set_visual_mode(EditorState& editor, EditorSelectionMode mode) -> void {
        if (editor.selection_mode == mode) {
            clear_selection(editor);
            return;
        }
        editor.selection_mode = mode;
        editor.selection_anchor_line = editor.cursor_line;
        editor.selection_anchor_column = editor.cursor_column;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, true);
    }

    [[nodiscard]] auto can_delete_char(EditorState const& editor) -> bool;
    [[nodiscard]] auto can_delete_line(EditorState const& editor) -> bool;
    auto copy_selection_to_clipboard(EditorState& editor, EditorClipboard clipboard) -> void;
    auto paste_from_clipboard(EditorState& editor, EditorClipboard clipboard) -> void;

    [[nodiscard]] auto selection_start(EditorSelectionRange selection) -> EditorPosition {
        return {selection.start_line, selection.start_column};
    }

    [[nodiscard]] auto selection_end(EditorSelectionRange selection) -> EditorPosition {
        return {selection.end_line, selection.end_column};
    }

    [[nodiscard]] auto
    selection_last_line(EditorState const& editor, EditorSelectionRange selection) -> size_t {
        size_t line = selection.end_line;
        if (selection.end_column == 0u && selection.end_line > selection.start_line) {
            line -= 1u;
        }
        return std::min(line, editor_line_count(editor) - 1u);
    }

    [[nodiscard]] auto selection_line_range(
        EditorState const& editor, EditorSelectionRange selection, size_t& first, size_t& last
    ) -> bool {
        if (!selection.active) {
            return false;
        }
        first = selection.start_line;
        last = selection_last_line(editor, selection);
        return first <= last;
    }

    auto transform_selection(EditorState& editor, char (*transform)(char)) -> void {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (!selection.active) {
            return;
        }
        save_editor_undo(editor);
        for (size_t line_index = selection.start_line; line_index <= selection.end_line;
             ++line_index) {
            EditorLine const line = editor_line(editor, line_index);
            size_t const start = line_index == selection.start_line ? selection.start_column : 0u;
            size_t const end = line_index == selection.end_line ? selection.end_column : line.size;
            if (end <= start) {
                continue;
            }
            DEBUG_ASSERT(editor.text.arena != nullptr);
            char* const replacement = arena_alloc<char>(*editor.text.arena, end - start);
            for (size_t index = start; index < end; ++index) {
                replacement[index - start] = transform(line.text[index]);
            }
            size_t const offset = position_offset(editor, {line_index, start});
            text_buffer_erase(editor.text, offset, position_offset(editor, {line_index, end}));
            text_buffer_insert(editor.text, offset, StrRef(replacement, end - start));
        }
        set_cursor(editor, selection.start_line, selection.start_column);
    }

    auto indent_line(EditorState& editor, size_t line_index) -> void {
        text_buffer_insert(editor.text, position_offset(editor, {line_index, 0u}), "    ");
    }

    auto unindent_line(EditorState& editor, size_t line_index) -> void {
        EditorLine const line = editor_line(editor, line_index);
        size_t count = 0u;
        if (line.size != 0u && line.text[0u] == '\t') {
            count = 1u;
        } else {
            while (count < 4u && count < line.size && line.text[count] == ' ') {
                count += 1u;
            }
        }
        if (count == 0u) {
            return;
        }
        text_buffer_erase(
            editor.text,
            position_offset(editor, {line_index, 0u}),
            position_offset(editor, {line_index, count})
        );
    }

    auto indent_selection(EditorState& editor, bool indent) -> void {
        EditorSelectionRange const selection = editor_selection_range(editor);
        size_t first = 0u;
        size_t last = 0u;
        if (!selection_line_range(editor, selection, first, last)) {
            return;
        }
        save_editor_undo(editor);
        for (size_t line_index = first; line_index <= last; ++line_index) {
            if (indent) {
                indent_line(editor, line_index);
            } else {
                unindent_line(editor, line_index);
            }
        }
        set_cursor(editor, first, 0u);
    }

    [[nodiscard]] auto delete_selection(EditorState& editor, EditorClipboard clipboard, bool yank)
        -> bool {
        if (!editor_selection_range(editor).active) {
            return false;
        }
        if (yank) {
            copy_selection_to_clipboard(editor, clipboard);
        }
        save_editor_undo(editor);
        BASE_UNUSED(erase_selection(editor));
        return true;
    }

    auto change_selection(EditorState& editor, EditorClipboard clipboard, bool yank) -> void {
        if (delete_selection(editor, clipboard, yank)) {
            enter_insert_at(editor, cursor_position(editor));
        }
    }

    auto replace_selection_with_char(EditorState& editor, char ch) -> void {
        if (editor_selection_range(editor).active) {
            save_editor_undo(editor);
            BASE_UNUSED(erase_selection(editor));
            insert_char(editor, ch);
        } else if (can_delete_char(editor)) {
            save_editor_undo(editor);
            delete_char(editor);
            insert_char(editor, ch);
        }
    }

    auto replace_selection_with_clipboard(EditorState& editor, EditorClipboard clipboard) -> void {
        if (!editor_selection_range(editor).active || clipboard.get_clipboard_text == nullptr ||
            editor.text.arena == nullptr) {
            return;
        }
        StrRef const text = clipboard.get_clipboard_text(clipboard.user_data, *editor.text.arena);
        if (text.empty()) {
            return;
        }
        save_editor_undo(editor);
        BASE_UNUSED(erase_selection(editor));
        insert_text(editor, text);
    }

    auto paste_around_selection(EditorState& editor, EditorClipboard clipboard, bool after)
        -> void {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (selection.active) {
            EditorPosition const position =
                after ? selection_end(selection) : selection_start(selection);
            set_cursor(editor, position.line, position.column);
        } else if (after) {
            set_cursor(
                editor,
                editor.cursor_line,
                std::min(editor.cursor_column + 1u, line_size(editor, editor.cursor_line))
            );
        }
        paste_from_clipboard(editor, clipboard);
    }

    auto
    handle_normal_char(EditorState& editor, char ch, gui::KeyMods mods, EditorClipboard clipboard)
        -> void {
        if (editor.flag(EditorFlag::PENDING_LSP)) {
            editor.set_flag(EditorFlag::PENDING_LSP, false);
            if (ch == 'n') {
                open_lsp_rename(editor);
            } else if (ch == 'a') {
                request_lsp(editor, LspRequestKind::CODE_ACTION);
            } else if (ch == 's') {
                request_lsp(editor, LspRequestKind::DOCUMENT_SYMBOL);
            }
            return;
        }
        if (editor.flag(EditorFlag::PENDING_R)) {
            editor.set_flag(EditorFlag::PENDING_R, false);
            replace_selection_with_char(editor, ch);
            return;
        }

        if (editor.flag(EditorFlag::PENDING_D)) {
            editor.set_flag(EditorFlag::PENDING_D, false);
            if (ch == 'd') {
                if (can_delete_line(editor)) {
                    save_editor_undo(editor);
                }
                delete_line(editor);
            }
            return;
        }
        if (editor.flag(EditorFlag::PENDING_G)) {
            editor.set_flag(EditorFlag::PENDING_G, false);
            if (ch == 'g') {
                move_cursor_to(editor, {0u, 0u}, visual_selecting(editor));
            } else if (ch == 'd') {
                request_lsp(editor, LspRequestKind::DEFINITION);
            } else if (ch == 'D') {
                request_lsp(editor, LspRequestKind::DECLARATION);
            } else if (ch == 'r') {
                request_lsp(editor, LspRequestKind::REFERENCES);
            }
            return;
        }
        if (editor.flag(EditorFlag::PENDING_Z)) {
            editor.set_flag(EditorFlag::PENDING_Z, false);
            if (ch == 'z') {
                center_cursor(editor, editor.split_nodes[editor.focused_split].rect);
            }
            return;
        }
        if (editor.flag(EditorFlag::PENDING_WINDOW)) {
            editor.set_flag(EditorFlag::PENDING_WINDOW, false);
            switch (ch) {
            case 'v':
                split_focused(editor, EditorSplitKind::VERTICAL);
                break;
            case 's':
                split_focused(editor, EditorSplitKind::HORIZONTAL);
                break;
            case 'h':
            case 'j':
            case 'k':
            case 'l':
                focus_split_in_direction(editor, ch);
                break;
            case 'H':
                swap_split_in_direction(editor, 'h');
                break;
            case 'J':
                swap_split_in_direction(editor, 'j');
                break;
            case 'K':
                swap_split_in_direction(editor, 'k');
                break;
            case 'L':
                swap_split_in_direction(editor, 'l');
                break;
            case 'q':
                close_focused_split(editor);
                break;
            default:
                break;
            }
            return;
        }
        if (editor.flag(EditorFlag::PENDING_LEADER)) {
            if (ch == ' ') {
                return;
            }
            editor.set_flag(EditorFlag::PENDING_LEADER, false);
            if (ch == 'e') {
                toggle_filesystem_panel(editor);
            } else if (ch == 'f') {
                open_file_search(editor);
            } else if (ch == 'b') {
                open_buffer_search(editor);
            } else if (ch == 'w') {
                editor.set_flag(EditorFlag::PENDING_WINDOW, true);
            } else if (ch == 'r' || ch == 'c' || ch == 's') {
                editor.set_flag(EditorFlag::PENDING_LSP, true);
            }
            return;
        }

        bool const alt = (mods & gui::KEY_MOD_ALT) != 0u &&
                         (mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_SUPER)) == 0u;
        if (!alt && (mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_SUPER)) != 0u) {
            return;
        }

        if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
            if (ch == ' ') {
                editor.set_flag(EditorFlag::PENDING_LEADER, true);
            }
            return;
        }

        if (alt) {
            if (ch == '`') {
                transform_selection(editor, to_ascii_upper);
            } else if (ch == 'd') {
                BASE_UNUSED(delete_selection(editor, clipboard, false));
            } else if (ch == 'c') {
                change_selection(editor, clipboard, false);
            }
            return;
        }

        if (visual_selecting(editor) && ch == 'x') {
            if (can_delete_char(editor)) {
                save_editor_undo(editor);
            }
            delete_char(editor);
            return;
        }

        size_t const line_number = editor.pending_line_number;
        bool const has_line_number = editor.flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE);
        if (ch >= '0' && ch <= '9' && (has_line_number || ch != '0')) {
            editor.pending_line_number = line_number * 10u + static_cast<size_t>(ch - '0');
            editor.set_flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE, true);
            return;
        }
        clear_pending_line_number(editor);

        bool const select = visual_selecting(editor);
        EditorSelectionRange const selection = editor_selection_range(editor);
        switch (ch) {
        case ':':
            open_command_line(editor);
            break;
        case 'K':
            request_lsp(editor, LspRequestKind::HOVER);
            break;
        case ' ':
            editor.set_flag(EditorFlag::PENDING_LEADER, true);
            break;
        case 'h':
            move_left(editor, select);
            break;
        case 'j':
            move_vertical(editor, 1, select);
            break;
        case 'k':
            move_vertical(editor, -1, select);
            break;
        case 'l':
            move_right(editor, select);
            break;
        case 'v':
            set_visual_mode(editor, EditorSelectionMode::CHARACTER);
            break;
        case 'V':
            set_visual_mode(editor, EditorSelectionMode::LINE);
            break;
        case 'i':
            enter_insert_at(
                editor, selection.active ? selection_start(selection) : cursor_position(editor)
            );
            break;
        case 'I':
            enter_insert_at(
                editor, {selection.active ? selection.start_line : editor.cursor_line, 0u}
            );
            break;
        case 'a':
            enter_insert_at(
                editor,
                selection.active
                    ? selection_end(selection)
                    : EditorPosition{
                          editor.cursor_line,
                          std::min(
                              editor.cursor_column + 1u, line_size(editor, editor.cursor_line)
                          ),
                      }
            );
            break;
        case 'A': {
            size_t const line =
                selection.active ? selection_last_line(editor, selection) : editor.cursor_line;
            enter_insert_at(editor, {line, line_size(editor, line)});
        } break;
        case 'o':
            save_editor_undo(editor);
            open_line(
                editor,
                selection.active ? selection_last_line(editor, selection) + 1u
                                 : editor.cursor_line + 1u
            );
            break;
        case 'O':
            save_editor_undo(editor);
            open_line(editor, selection.active ? selection.start_line : editor.cursor_line);
            break;
        case 'r':
            editor.set_flag(EditorFlag::PENDING_R, true);
            break;
        case 'R':
            replace_selection_with_clipboard(editor, clipboard);
            break;
        case '~':
            transform_selection(editor, switch_ascii_case);
            break;
        case '`':
            transform_selection(editor, to_ascii_lower);
            break;
        case 'u':
            BASE_UNUSED(restore_editor_undo(editor));
            break;
        case 'U':
            BASE_UNUSED(restore_editor_redo(editor));
            break;
        case 'y':
            copy_selection_to_clipboard(editor, clipboard);
            break;
        case 'p':
            paste_around_selection(editor, clipboard, true);
            break;
        case 'P':
            paste_around_selection(editor, clipboard, false);
            break;
        case '>':
            indent_selection(editor, true);
            break;
        case '<':
            indent_selection(editor, false);
            break;
        case 'x':
            if (can_delete_char(editor)) {
                save_editor_undo(editor);
            }
            delete_char(editor);
            break;
        case '0':
            move_cursor_to(editor, {editor.cursor_line, 0u}, select);
            break;
        case '$':
            move_cursor_to(
                editor, {editor.cursor_line, line_size(editor, editor.cursor_line)}, select
            );
            break;
        case 'w':
            move_word_right(editor, select);
            break;
        case 'b':
            move_word_left(editor, select);
            break;
        case 'e':
            move_word_end(editor, select);
            break;
        case 'W':
            move_big_word_right(editor, select);
            break;
        case 'B':
            move_big_word_left(editor, select);
            break;
        case 'E':
            move_big_word_end(editor, select);
            break;
        case 'g':
            editor.set_flag(EditorFlag::PENDING_G, true);
            break;
        case 'z':
            editor.set_flag(EditorFlag::PENDING_Z, true);
            break;
        case 'G':
            if (has_line_number) {
                move_cursor_to(
                    editor, {std::min(line_number, editor_line_count(editor)) - 1u, 0u}, select
                );
            } else {
                move_cursor_to(editor, {editor_line_count(editor) - 1u, 0u}, select);
            }
            break;
        case 'd':
            if (!delete_selection(editor, clipboard, true)) {
                editor.set_flag(EditorFlag::PENDING_D, true);
            }
            break;
        case 'c':
            change_selection(editor, clipboard, true);
            break;
        default:
            break;
        }
    }

    [[nodiscard]] auto shortcut_key(gui::KeyEvent const& event, gui::Key key) -> bool {
        return event.kind == gui::KeyEventKind::PRESS && event.key == key &&
               (event.mods & gui::KEY_MOD_CTRL) != 0u &&
               (event.mods & (gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u;
    }

    [[nodiscard]] auto can_backspace(EditorState const& editor) -> bool {
        return editor.flag(EditorFlag::SELECTION_ACTIVE) || editor.cursor_column != 0u ||
               editor.cursor_line != 0u;
    }

    [[nodiscard]] auto can_delete_char(EditorState const& editor) -> bool {
        return editor.flag(EditorFlag::SELECTION_ACTIVE) ||
               editor.cursor_column < line_size(editor, editor.cursor_line);
    }

    [[nodiscard]] auto can_backspace_word(EditorState const& editor) -> bool {
        return editor.flag(EditorFlag::SELECTION_ACTIVE) ||
               !same_position(
                   previous_word_position(editor, cursor_position(editor)), cursor_position(editor)
               );
    }

    [[nodiscard]] auto can_delete_word(EditorState const& editor) -> bool {
        return editor.flag(EditorFlag::SELECTION_ACTIVE) ||
               !same_position(
                   next_word_position(editor, cursor_position(editor)), cursor_position(editor)
               );
    }

    [[nodiscard]] auto can_delete_line(EditorState const& editor) -> bool {
        return editor_line_count(editor) > 1u || line_size(editor, editor.cursor_line) != 0u;
    }

    auto copy_selection_to_clipboard(EditorState& editor, EditorClipboard clipboard) -> void {
        if (clipboard.set_clipboard_text == nullptr) {
            return;
        }
        StrRef const text = copy_selected_text(editor);
        if (!text.empty()) {
            clipboard.set_clipboard_text(clipboard.user_data, text);
        }
    }

    auto paste_from_clipboard(EditorState& editor, EditorClipboard clipboard) -> void {
        if (clipboard.get_clipboard_text == nullptr || editor.text.arena == nullptr) {
            return;
        }
        StrRef const text = clipboard.get_clipboard_text(clipboard.user_data, *editor.text.arena);
        if (text.empty()) {
            return;
        }
        save_editor_undo(editor);
        insert_text(editor, text);
    }

    [[nodiscard]] auto editor_lsp_ready(EditorState const& editor) -> bool {
        if (editor.lsp_bridge == nullptr || editor.lsp_send_request == nullptr) {
            return false;
        }
        return editor.lsp_bridge->status == LspStatusKind::READY ||
               editor.lsp_bridge->status == LspStatusKind::WARNING;
    }

    [[nodiscard]] auto editor_lsp_file(EditorState const& editor) -> bool {
        return !editor.current_file_path.empty() && (lsp_cpp_file_name(editor.current_file_name) ||
                                                     lsp_cpp_file_name(editor.current_file_path));
    }

    auto send_lsp_request(EditorState& editor, LspEditorRequest const& request) -> void {
        if (editor.lsp_send_request != nullptr) {
            editor.lsp_send_request(editor.lsp_user_data, request);
        }
    }

    auto close_editor_lsp_popup(EditorState& editor) -> void {
        editor.lsp_popup = EditorLspPopupKind::NONE;
        editor.lsp_selected = 0u;
        editor.lsp_hover_selection = {};
        editor.lsp_rename_text_size = 0u;
        editor.lsp_rename_text[0u] = '\0';
        editor.lsp_rename_text_selected = false;
    }

    auto update_editor_lsp_document(EditorState& editor) -> void {
        if (editor.lsp_send_request == nullptr) {
            return;
        }

        bool const enabled = editor_lsp_ready(editor) && editor_lsp_file(editor) &&
                             editor_focused_pane_kind(editor) == EditorPaneKind::CODE;
        if (!enabled) {
            if (!editor.lsp_synced_path.empty()) {
                send_lsp_request(
                    editor, {.kind = LspRequestKind::DID_CLOSE, .path = editor.lsp_synced_path}
                );
                editor.lsp_synced_path = {};
                editor.lsp_synced_revision = 0u;
            }
            return;
        }

        bool const path_changed = editor.lsp_synced_path != editor.current_file_path;
        if (path_changed && !editor.lsp_synced_path.empty()) {
            send_lsp_request(
                editor, {.kind = LspRequestKind::DID_CLOSE, .path = editor.lsp_synced_path}
            );
        }

        if (!path_changed && editor.lsp_synced_revision == editor.text.revision) {
            return;
        }

        DEBUG_ASSERT(editor.text.arena != nullptr);
        StrRef const text = text_buffer_copy(editor.text, *editor.text.arena);
        send_lsp_request(
            editor,
            {
                .kind = path_changed ? LspRequestKind::DID_OPEN : LspRequestKind::DID_CHANGE,
                .path = editor.current_file_path,
                .text = text,
                .revision = editor.text.revision,
            }
        );
        editor.lsp_synced_path = arena_copy_cstr(*editor.arena, editor.current_file_path);
        editor.lsp_synced_revision = editor.text.revision;
    }

    [[nodiscard]] auto lsp_clamped_position(EditorState const& editor, LspPosition position)
        -> EditorPosition {
        if (position.line >= editor_line_count(editor)) {
            size_t const line = editor_line_count(editor) - 1u;
            return {line, line_size(editor, line)};
        }
        return {position.line, std::min(position.column, line_size(editor, position.line))};
    }

    [[nodiscard]] auto lsp_position_offset(EditorState const& editor, LspPosition position)
        -> size_t {
        return position_offset(editor, lsp_clamped_position(editor, position));
    }

    [[nodiscard]] auto
    apply_editor_lsp_text_edits(EditorState& editor, Slice<LspTextEdit const> edits) -> bool {
        if (edits.empty() || editor.text.arena == nullptr) {
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        Vec<LspTextEdit> sorted = {};
        BASE_UNUSED(sorted.init(edits.size(), temp.arena()->resource()));
        for (LspTextEdit const& edit : edits) {
            if ((edit.path.empty() || edit.path == editor.current_file_path) &&
                lsp_range_valid(edit.range)) {
                BASE_UNUSED(sorted.push_back(edit));
            }
        }
        if (sorted.empty()) {
            return false;
        }

        std::sort(sorted.begin(), sorted.end(), [](LspTextEdit const& a, LspTextEdit const& b) {
            if (a.range.start.line != b.range.start.line) {
                return a.range.start.line > b.range.start.line;
            }
            return a.range.start.column > b.range.start.column;
        });

        save_editor_undo(editor);
        for (LspTextEdit const& edit : sorted) {
            size_t const start = lsp_position_offset(editor, edit.range.start);
            size_t const end = lsp_position_offset(editor, edit.range.end);
            if (end < start) {
                continue;
            }
            text_buffer_erase(editor.text, start, end);
            if (!edit.new_text.empty()) {
                text_buffer_insert(editor.text, start, edit.new_text);
            }
        }
        set_cursor(
            editor,
            sorted[sorted.size() - 1u].range.start.line,
            sorted[sorted.size() - 1u].range.start.column
        );
        refresh_editor_dirty(editor);
        sync_shared_panes(editor);
        return true;
    }

    [[nodiscard]] auto current_lsp_range(EditorState const& editor) -> LspRange {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (selection.active) {
            return {
                .start = {selection.start_line, selection.start_column},
                .end = {selection.end_line, selection.end_column},
            };
        }
        return {
            .start = {editor.cursor_line, 0u},
            .end = {editor.cursor_line, line_size(editor, editor.cursor_line)},
        };
    }

    auto request_lsp(EditorState& editor, LspRequestKind kind, StrRef new_name) -> void {
        if (!editor_lsp_ready(editor) || !editor_lsp_file(editor)) {
            return;
        }
        update_editor_lsp_document(editor);
        if (editor.lsp_synced_path != editor.current_file_path) {
            return;
        }
        send_lsp_request(
            editor,
            {
                .kind = kind,
                .path = editor.current_file_path,
                .new_name = new_name,
                .position = {editor.cursor_line, editor.cursor_column},
                .range = current_lsp_range(editor),
                .revision = editor.text.revision,
            }
        );
    }

    auto open_lsp_rename(EditorState& editor) -> void {
        EditorPosition start = {};
        EditorPosition end = {};
        bool const has_word = word_range_at_position(editor, cursor_position(editor), start, end);
        close_editor_lsp_popup(editor);
        editor.lsp_popup = EditorLspPopupKind::RENAME;
        if (has_word) {
            StrRef const line = editor_line_text(editor_line(editor, start.line));
            StrRef const word = line.substr(start.column, end.column - start.column);
            editor.lsp_rename_text_size =
                word.copy_to(editor.lsp_rename_text, LSP_RENAME_TEXT_CAPACITY - 1u);
            editor.lsp_rename_text[editor.lsp_rename_text_size] = '\0';
            editor.lsp_rename_text_selected = editor.lsp_rename_text_size != 0u;
        }
    }

    [[nodiscard]] auto lsp_popup_count(EditorState const& editor) -> size_t {
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

    auto apply_lsp_completion(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->completions.empty()) {
            close_editor_lsp_popup(editor);
            return;
        }
        size_t const index =
            std::min(editor.lsp_selected, editor.lsp_bridge->completions.size() - 1u);
        LspCompletionItem const& item = editor.lsp_bridge->completions[index];
        if (item.has_edit) {
            LspTextEdit const edit = {
                .path = editor.current_file_path,
                .range = item.edit_range,
                .new_text = !item.insert_text.empty() ? item.insert_text : item.label,
            };
            BASE_UNUSED(apply_editor_lsp_text_edits(editor, Slice<LspTextEdit const>(&edit, 1u)));
        } else {
            save_editor_undo(editor);
            insert_text(editor, !item.insert_text.empty() ? item.insert_text : item.label);
            refresh_editor_dirty(editor);
            sync_shared_panes(editor);
        }
        close_editor_lsp_popup(editor);
    }

    auto accept_lsp_popup(EditorState& editor) -> void {
        switch (editor.lsp_popup) {
        case EditorLspPopupKind::COMPLETION:
            apply_lsp_completion(editor);
            break;
        case EditorLspPopupKind::LOCATIONS:
            editor.lsp_open_location_index = editor.lsp_selected;
            close_editor_lsp_popup(editor);
            break;
        case EditorLspPopupKind::CODE_ACTIONS:
            editor.lsp_apply_code_action_index = editor.lsp_selected;
            close_editor_lsp_popup(editor);
            break;
        case EditorLspPopupKind::SYMBOLS:
            editor.lsp_open_symbol_index = editor.lsp_selected;
            close_editor_lsp_popup(editor);
            break;
        case EditorLspPopupKind::RENAME:
            request_lsp(
                editor,
                LspRequestKind::RENAME,
                StrRef(editor.lsp_rename_text, editor.lsp_rename_text_size)
            );
            close_editor_lsp_popup(editor);
            break;
        default:
            close_editor_lsp_popup(editor);
            break;
        }
    }

    [[nodiscard]] auto handle_lsp_popup_event(EditorState& editor, gui::KeyEvent const& event)
        -> bool {
        if (editor.lsp_popup == EditorLspPopupKind::NONE) {
            return false;
        }
        if (editor.lsp_popup == EditorLspPopupKind::RENAME) {
            if (event.kind == gui::KeyEventKind::PRESS || event.kind == gui::KeyEventKind::REPEAT) {
                if (event.key == gui::Key::ESCAPE) {
                    close_editor_lsp_popup(editor);
                    return true;
                }
                if (event.key == gui::Key::TAB) {
                    accept_lsp_popup(editor);
                    return true;
                }
            }
            return true;
        }
        if (event.kind == gui::KeyEventKind::TEXT) {
            return true;
        }
        if (event.kind != gui::KeyEventKind::PRESS && event.kind != gui::KeyEventKind::REPEAT) {
            return true;
        }
        if (event.key == gui::Key::ESCAPE) {
            close_editor_lsp_popup(editor);
        } else if (event.key == gui::Key::ENTER || event.key == gui::Key::TAB) {
            accept_lsp_popup(editor);
        } else if (event.key == gui::Key::UP && editor.lsp_selected != 0u) {
            editor.lsp_selected -= 1u;
        } else if (event.key == gui::Key::DOWN) {
            size_t const count = lsp_popup_count(editor);
            if (editor.lsp_selected + 1u < count) {
                editor.lsp_selected += 1u;
            }
        }
        return true;
    }

    auto open_editor_lsp_locations(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->locations.empty()) {
            return;
        }
        close_editor_lsp_popup(editor);
        editor.lsp_popup = EditorLspPopupKind::LOCATIONS;
    }

    auto collapse_selection(EditorState& editor, bool end) -> bool {
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (!selection.active) {
            return false;
        }
        set_cursor(
            editor,
            end ? selection.end_line : selection.start_line,
            end ? selection.end_column : selection.start_column
        );
        return true;
    }

    auto handle_navigation_key(EditorState& editor, gui::KeyEvent const& event) -> bool {
        bool const select = (event.mods & gui::KEY_MOD_SHIFT) != 0u || visual_selecting(editor);
        bool const word = (event.mods & gui::KEY_MOD_CTRL) != 0u;
        switch (event.key) {
        case gui::Key::LEFT:
            if (!select && collapse_selection(editor, false)) {
                return true;
            }
            if (word) {
                move_word_left(editor, select);
            } else {
                move_left(editor, select);
            }
            return true;
        case gui::Key::RIGHT:
            if (!select && collapse_selection(editor, true)) {
                return true;
            }
            if (word) {
                move_word_right(editor, select);
            } else {
                move_right(editor, select);
            }
            return true;
        case gui::Key::UP:
            if (!select && collapse_selection(editor, false)) {
                return true;
            }
            move_vertical(editor, -1, select);
            return true;
        case gui::Key::DOWN:
            if (!select && collapse_selection(editor, true)) {
                return true;
            }
            move_vertical(editor, 1, select);
            return true;
        case gui::Key::HOME:
            move_cursor_to(
                editor, word ? EditorPosition{} : EditorPosition{editor.cursor_line, 0u}, select
            );
            return true;
        case gui::Key::END:
            if (word) {
                size_t const line = editor_line_count(editor) - 1u;
                move_cursor_to(editor, {line, line_size(editor, line)}, select);
            } else {
                move_cursor_to(
                    editor, {editor.cursor_line, line_size(editor, editor.cursor_line)}, select
                );
            }
            return true;
        default:
            return false;
        }
    }

    auto
    handle_key_event(EditorState& editor, gui::KeyEvent const& event, EditorClipboard clipboard)
        -> void {
        if (event.kind != gui::KeyEventKind::PRESS && event.kind != gui::KeyEventKind::REPEAT) {
            return;
        }
        if (event.key == gui::Key::ESCAPE) {
            editor.set_flag(EditorFlag::INSERT_MODE, false);
            editor.set_flag(EditorFlag::PENDING_LEADER, false);
            editor.set_flag(EditorFlag::PENDING_WINDOW, false);
            editor.set_flag(EditorFlag::PENDING_D, false);
            editor.set_flag(EditorFlag::PENDING_G, false);
            editor.set_flag(EditorFlag::PENDING_R, false);
            editor.set_flag(EditorFlag::PENDING_LSP, false);
            editor.set_flag(EditorFlag::PENDING_Z, false);
            clear_pending_line_number(editor);
            clamp_cursor(editor);
            clear_selection(editor);
            return;
        }
        if (font_zoom_key(event)) {
            zoom_font(
                editor, event.key == gui::Key::PLUS ? EDITOR_FONT_SIZE_STEP : -EDITOR_FONT_SIZE_STEP
            );
            return;
        }
        if (!editor.flag(EditorFlag::INSERT_MODE)) {
            clear_pending_line_number(editor);
        }
        if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
            if (event.key == gui::Key::SPACE) {
                editor.set_flag(EditorFlag::PENDING_LEADER, true);
            }
            return;
        }
        if (shortcut_key(event, gui::Key::SPACE)) {
            request_lsp(editor, LspRequestKind::COMPLETION);
            return;
        }
        if (shortcut_key(event, gui::Key::S)) {
            request_editor_save(editor);
            return;
        }
        if (shortcut_key(event, gui::Key::A)) {
            select_all(editor);
            return;
        }
        if (shortcut_key(event, gui::Key::C)) {
            copy_selection_to_clipboard(editor, clipboard);
            return;
        }
        if (shortcut_key(event, gui::Key::V)) {
            paste_from_clipboard(editor, clipboard);
            return;
        }
        if (shortcut_key(event, gui::Key::Z)) {
            BASE_UNUSED(restore_editor_undo(editor));
            return;
        }
        bool const ctrl_scroll = (event.mods & gui::KEY_MOD_CTRL) != 0u &&
                                 (event.mods & (gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u;
        if (ctrl_scroll && event.key == gui::Key::D) {
            move_half_split(editor, 1);
            return;
        }
        if (ctrl_scroll && event.key == gui::Key::U) {
            move_half_split(editor, -1);
            return;
        }

        if (editor.flag(EditorFlag::INSERT_MODE)) {
            if (handle_navigation_key(editor, event)) {
                return;
            }
            switch (event.key) {
            case gui::Key::ENTER:
                save_editor_undo(editor);
                insert_newline(editor);
                break;
            case gui::Key::TAB:
                save_editor_undo(editor);
                insert_text(editor, "    ");
                break;
            case gui::Key::BACKSPACE:
                if ((event.mods & gui::KEY_MOD_CTRL) != 0u) {
                    if (can_backspace_word(editor)) {
                        save_editor_undo(editor);
                    }
                    backspace_word(editor);
                } else {
                    if (can_backspace(editor)) {
                        save_editor_undo(editor);
                    }
                    backspace(editor);
                }
                break;
            case gui::Key::DELETE_KEY:
                if ((event.mods & gui::KEY_MOD_CTRL) != 0u) {
                    if (can_delete_word(editor)) {
                        save_editor_undo(editor);
                    }
                    delete_word(editor);
                } else {
                    if (can_delete_char(editor)) {
                        save_editor_undo(editor);
                    }
                    delete_char(editor);
                }
                break;
            default:
                break;
            }
            return;
        }

        if (handle_navigation_key(editor, event)) {
            return;
        }
        switch (event.key) {
        case gui::Key::SPACE:
            editor.set_flag(EditorFlag::PENDING_LEADER, true);
            break;
        case gui::Key::DELETE_KEY:
            if ((event.mods & gui::KEY_MOD_CTRL) != 0u) {
                if (can_delete_word(editor)) {
                    save_editor_undo(editor);
                }
                delete_word(editor);
            } else {
                if (can_delete_char(editor)) {
                    save_editor_undo(editor);
                }
                delete_char(editor);
            }
            break;
        default:
            break;
        }
    }

    auto process_editor_input(
        EditorState& editor, gui::InputState const& input, EditorClipboard clipboard
    ) -> void {
        load_focused_pane(editor);
        if (input.key_events == nullptr) {
            return;
        }
        if (editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }

        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (handle_lsp_popup_event(editor, event)) {
                continue;
            }
            if (editor.flag(EditorFlag::COMMAND_LINE_ACTIVE)) {
                handle_command_line_event(editor, event);
                continue;
            }
            if (editor.flag(EditorFlag::FILE_SEARCH_OPEN)) {
                handle_search_event(editor, event, false);
                continue;
            }
            if (editor.flag(EditorFlag::BUFFER_SEARCH_OPEN)) {
                handle_search_event(editor, event, true);
                continue;
            }
            if (event.kind == gui::KeyEventKind::TEXT) {
                if (font_zoom_text(event)) {
                    continue;
                }
                if (event.codepoint >= 32u && event.codepoint <= 126u) {
                    char const ch = static_cast<char>(event.codepoint);
                    if (editor.flag(EditorFlag::INSERT_MODE)) {
                        save_editor_undo(editor);
                        insert_char(editor, ch);
                        if (ch == '.' || ch == '>' || ch == ':') {
                            request_lsp(editor, LspRequestKind::COMPLETION);
                        }
                    } else {
                        handle_normal_char(editor, ch, event.mods, clipboard);
                    }
                }
            } else {
                handle_key_event(editor, event, clipboard);
            }
        }
        clamp_cursor(editor);
        if (editor.flag(EditorFlag::DIRTY)) {
            refresh_editor_dirty(editor);
        }
        sync_shared_panes(editor);
    }

    [[nodiscard]] auto point_in_rect(gui::Rect rect, gui::Vec2 point) -> bool {
        return point.x >= rect.min.x && point.x < rect.max.x && point.y >= rect.min.y &&
               point.y < rect.max.y;
    }

    [[nodiscard]] auto editor_scaled_font_size(EditorState const& editor, float base_size)
        -> float {
        return base_size * editor.font_size / EDITOR_FONT_SIZE;
    }

    [[nodiscard]] auto editor_line_height(EditorState const& editor) -> float {
        return editor_scaled_font_size(editor, EDITOR_LINE_HEIGHT);
    }

    [[nodiscard]] auto editor_content_rect(gui::Rect rect) -> gui::Rect {
        return {
            {rect.min.x + EDITOR_PADDING_X, rect.min.y + EDITOR_PADDING_Y},
            {rect.max.x - EDITOR_PADDING_X, rect.max.y - EDITOR_PADDING_Y},
        };
    }

    [[nodiscard]] auto editor_text_x(EditorState const& editor, gui::Rect rect) -> float {
        return editor_content_rect(rect).min.x + editor_scaled_font_size(editor, LINE_NUMBER_WIDTH);
    }

    [[nodiscard]] auto editor_max_scroll(EditorState const& editor, gui::Rect rect) -> float {
        gui::Rect const content = editor_content_rect(rect);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        float const content_height =
            static_cast<float>(editor_line_count(editor)) * editor_line_height(editor);
        return std::max(0.0f, content_height - visible_height);
    }

    auto clamp_scroll(EditorState& editor, gui::Rect rect) -> void {
        editor.scroll_y = std::clamp(editor.scroll_y, 0.0f, editor_max_scroll(editor, rect));
    }

    auto reveal_cursor(EditorState& editor, gui::Rect rect) -> void {
        gui::Rect const content = editor_content_rect(rect);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        float const line_height = editor_line_height(editor);
        float const line_top = static_cast<float>(editor.cursor_line) * line_height;
        float const line_bottom = line_top + line_height;
        if (line_top < editor.scroll_y) {
            editor.scroll_y = line_top;
        } else if (line_bottom > editor.scroll_y + visible_height) {
            editor.scroll_y = line_bottom - visible_height;
        }
        clamp_scroll(editor, rect);
    }

    [[nodiscard]] auto position_from_mouse(
        EditorState const& editor, gui::Rect rect, gui::Vec2 mouse, float char_width
    ) -> EditorPosition {
        gui::Rect const content = editor_content_rect(rect);
        float const y = std::max(0.0f, mouse.y - content.min.y + editor.scroll_y);
        float const line_height = editor_line_height(editor);
        size_t const line =
            std::min(editor_line_count(editor) - 1u, static_cast<size_t>(y / line_height));
        float const text_x = editor_text_x(editor, rect);
        float const column_x = std::max(0.0f, mouse.x - text_x);
        size_t const column = static_cast<size_t>(column_x / char_width + 0.5f);
        return clamp_position(editor, {line, column});
    }

    auto select_range(EditorState& editor, EditorPosition start, EditorPosition end) -> void {
        start = clamp_position(editor, start);
        end = clamp_position(editor, end);
        editor.selection_anchor_line = start.line;
        editor.selection_anchor_column = start.column;
        editor.cursor_line = end.line;
        editor.cursor_column = end.column;
        editor.preferred_column = editor.cursor_column;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, !same_position(start, end));
    }

    [[nodiscard]] auto word_range_at_position(
        EditorState const& editor,
        EditorPosition position,
        EditorPosition& start,
        EditorPosition& end
    ) -> bool {
        position = clamp_position(editor, position);
        if (!text_word_char(editor, position)) {
            if (position.column == 0u) {
                return false;
            }
            EditorPosition const previous = {position.line, position.column - 1u};
            if (!text_word_char(editor, previous)) {
                return false;
            }
            position = previous;
        }

        start = position;
        end = {position.line, position.column + 1u};
        while (start.column > 0u && text_word_char(editor, {start.line, start.column - 1u})) {
            start.column -= 1u;
        }
        while (end.column < line_size(editor, end.line) && text_word_char(editor, end)) {
            end.column += 1u;
        }
        return true;
    }

    auto update_cursor_from_mouse(
        EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width, bool select
    ) -> void {
        move_cursor_to(editor, position_from_mouse(editor, rect, mouse, char_width), select);
    }

    auto
    select_word_from_mouse(EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width)
        -> void {
        EditorPosition start = {};
        EditorPosition end = {};
        if (word_range_at_position(
                editor, position_from_mouse(editor, rect, mouse, char_width), start, end
            )) {
            select_range(editor, start, end);
        } else {
            move_cursor_to(editor, position_from_mouse(editor, rect, mouse, char_width), false);
        }
    }

    auto
    select_line_from_mouse(EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width)
        -> void {
        EditorPosition const position = position_from_mouse(editor, rect, mouse, char_width);
        select_range(
            editor, {position.line, 0u}, {position.line, line_size(editor, position.line)}
        );
    }

    [[nodiscard]] auto hash_bytes(uint64_t hash, void const* data, size_t size) -> uint64_t {
        auto const* bytes = static_cast<uint8_t const*>(data);
        for (size_t index = 0u; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] auto hash_text(uint64_t hash, EditorText const& text) -> uint64_t {
        size_t const size = text_buffer_size(text);
        size_t const line_count = text_buffer_line_count(text);
        hash = hash_bytes(hash, &size, sizeof(size));
        hash = hash_bytes(hash, &line_count, sizeof(line_count));
        hash = hash_bytes(hash, &text.revision, sizeof(text.revision));
        return hash;
    }

    [[nodiscard]] auto hash_pane(uint64_t hash, EditorPane const& pane) -> uint64_t {
        hash = hash_bytes(hash, &pane.kind, sizeof(pane.kind));
        hash = hash_text(hash, pane.text);
        hash = hash_bytes(hash, &pane.cursor_line, sizeof(pane.cursor_line));
        hash = hash_bytes(hash, &pane.cursor_column, sizeof(pane.cursor_column));
        hash = hash_bytes(hash, &pane.selection_anchor_line, sizeof(pane.selection_anchor_line));
        hash =
            hash_bytes(hash, &pane.selection_anchor_column, sizeof(pane.selection_anchor_column));
        hash = hash_bytes(hash, &pane.selection_mode, sizeof(pane.selection_mode));
        hash = hash_bytes(hash, &pane.selection_active, sizeof(pane.selection_active));
        hash = hash_bytes(hash, &pane.scroll_y, sizeof(pane.scroll_y));
        hash = hash_bytes(hash, &pane.insert_mode, sizeof(pane.insert_mode));
        size_t const name_size = pane.current_file_name.size();
        hash = hash_bytes(hash, &name_size, sizeof(name_size));
        hash = hash_bytes(hash, pane.current_file_name.data(), pane.current_file_name.size());
        size_t const path_size = pane.current_file_path.size();
        hash = hash_bytes(hash, &path_size, sizeof(path_size));
        hash = hash_bytes(hash, pane.current_file_path.data(), pane.current_file_path.size());
        size_t const saved_text_size = pane.saved_text.size();
        hash = hash_bytes(hash, &saved_text_size, sizeof(saved_text_size));
        hash = hash_bytes(hash, pane.saved_text.data(), pane.saved_text.size());
        hash = hash_bytes(hash, &pane.file_write_stamp, sizeof(pane.file_write_stamp));
        hash = hash_bytes(hash, &pane.dirty, sizeof(pane.dirty));
        hash =
            hash_bytes(hash, &pane.external_change_pending, sizeof(pane.external_change_pending));
        hash = hash_bytes(hash, &pane.file_deleted_on_disk, sizeof(pane.file_deleted_on_disk));
        return hash;
    }

    [[nodiscard]] auto editor_state_hash(EditorState const& editor) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        hash = hash_text(hash, editor.text);
        hash = hash_bytes(hash, &editor.cursor_line, sizeof(editor.cursor_line));
        hash = hash_bytes(hash, &editor.cursor_column, sizeof(editor.cursor_column));
        hash =
            hash_bytes(hash, &editor.selection_anchor_line, sizeof(editor.selection_anchor_line));
        hash = hash_bytes(
            hash, &editor.selection_anchor_column, sizeof(editor.selection_anchor_column)
        );
        hash = hash_bytes(hash, &editor.selection_mode, sizeof(editor.selection_mode));
        auto const& flag_words = editor.flags.words();
        hash =
            hash_bytes(hash, flag_words.data(), flag_words.size() * sizeof(EditorFlags::WordType));
        hash = hash_bytes(hash, &editor.font_size, sizeof(editor.font_size));
        hash = hash_bytes(hash, &editor.scroll_y, sizeof(editor.scroll_y));
        hash =
            hash_bytes(hash, &editor.sidebar_width_percent, sizeof(editor.sidebar_width_percent));
        hash =
            hash_bytes(hash, &editor.file_search_text_size, sizeof(editor.file_search_text_size));
        hash = hash_bytes(hash, editor.file_search_text, editor.file_search_text_size);
        hash = hash_bytes(hash, &editor.file_search_selected, sizeof(editor.file_search_selected));
        hash =
            hash_bytes(hash, &editor.file_search_open_file, sizeof(editor.file_search_open_file));
        hash = hash_bytes(
            hash, &editor.buffer_search_open_file, sizeof(editor.buffer_search_open_file)
        );
        hash = hash_bytes(hash, &editor.command_text_size, sizeof(editor.command_text_size));
        hash = hash_bytes(hash, &editor.command_selected, sizeof(editor.command_selected));
        hash = hash_bytes(hash, editor.command_text, editor.command_text_size);
        hash = hash_bytes(hash, &editor.lsp_popup, sizeof(editor.lsp_popup));
        hash = hash_bytes(hash, &editor.lsp_selected, sizeof(editor.lsp_selected));
        hash = hash_bytes(hash, &editor.lsp_hover_selection, sizeof(editor.lsp_hover_selection));
        hash = hash_bytes(hash, &editor.lsp_rename_text_size, sizeof(editor.lsp_rename_text_size));
        hash = hash_bytes(hash, editor.lsp_rename_text, editor.lsp_rename_text_size);
        hash = hash_bytes(
            hash, &editor.lsp_rename_text_selected, sizeof(editor.lsp_rename_text_selected)
        );
        hash = hash_bytes(
            hash,
            &editor.lsp_seen_completions_generation,
            sizeof(editor.lsp_seen_completions_generation)
        );
        hash = hash_bytes(
            hash, &editor.lsp_seen_hover_generation, sizeof(editor.lsp_seen_hover_generation)
        );
        hash = hash_bytes(
            hash,
            &editor.lsp_seen_locations_generation,
            sizeof(editor.lsp_seen_locations_generation)
        );
        hash = hash_bytes(
            hash,
            &editor.lsp_seen_code_actions_generation,
            sizeof(editor.lsp_seen_code_actions_generation)
        );
        hash = hash_bytes(
            hash, &editor.lsp_seen_symbols_generation, sizeof(editor.lsp_seen_symbols_generation)
        );
        hash = hash_bytes(
            hash,
            &editor.lsp_seen_text_edits_generation,
            sizeof(editor.lsp_seen_text_edits_generation)
        );
        hash = hash_bytes(hash, &editor.save_path_error, sizeof(editor.save_path_error));
        size_t const save_path_text_size = cstr_len(editor.save_path_text);
        hash = hash_bytes(hash, &save_path_text_size, sizeof(save_path_text_size));
        hash = hash_bytes(hash, editor.save_path_text, save_path_text_size);
        size_t const current_file_name_size = editor.current_file_name.size();
        hash = hash_bytes(hash, &current_file_name_size, sizeof(current_file_name_size));
        hash = hash_bytes(hash, editor.current_file_name.data(), editor.current_file_name.size());
        size_t const current_file_path_size = editor.current_file_path.size();
        hash = hash_bytes(hash, &current_file_path_size, sizeof(current_file_path_size));
        hash = hash_bytes(hash, editor.current_file_path.data(), editor.current_file_path.size());
        size_t const saved_text_size = editor.saved_text.size();
        hash = hash_bytes(hash, &saved_text_size, sizeof(saved_text_size));
        hash = hash_bytes(hash, editor.saved_text.data(), editor.saved_text.size());
        hash = hash_bytes(hash, &editor.file_write_stamp, sizeof(editor.file_write_stamp));
        size_t const scratch_text_size = editor.scratch_text.size();
        hash = hash_bytes(hash, &scratch_text_size, sizeof(scratch_text_size));
        hash = hash_bytes(hash, editor.scratch_text.data(), editor.scratch_text.size());
        size_t const open_file_count = editor.open_files.size();
        hash = hash_bytes(hash, &open_file_count, sizeof(open_file_count));
        for (OpenFile const& file : editor.open_files) {
            size_t const name_size = file.name.size();
            hash = hash_bytes(hash, &name_size, sizeof(name_size));
            hash = hash_bytes(hash, file.name.data(), file.name.size());
            size_t const path_size = file.path.size();
            hash = hash_bytes(hash, &path_size, sizeof(path_size));
            hash = hash_bytes(hash, file.path.data(), file.path.size());
            size_t const text_size = file.text.size();
            hash = hash_bytes(hash, &text_size, sizeof(text_size));
            hash = hash_bytes(hash, file.text.data(), file.text.size());
            size_t const file_saved_text_size = file.saved_text.size();
            hash = hash_bytes(hash, &file_saved_text_size, sizeof(file_saved_text_size));
            hash = hash_bytes(hash, file.saved_text.data(), file.saved_text.size());
            hash = hash_bytes(hash, &file.file_write_stamp, sizeof(file.file_write_stamp));
            hash = hash_bytes(hash, &file.cursor_line, sizeof(file.cursor_line));
            hash = hash_bytes(hash, &file.cursor_column, sizeof(file.cursor_column));
            hash = hash_bytes(hash, &file.preferred_column, sizeof(file.preferred_column));
            hash =
                hash_bytes(hash, &file.selection_anchor_line, sizeof(file.selection_anchor_line));
            hash = hash_bytes(
                hash, &file.selection_anchor_column, sizeof(file.selection_anchor_column)
            );
            hash = hash_bytes(hash, &file.selection_mode, sizeof(file.selection_mode));
            hash = hash_bytes(hash, &file.scroll_y, sizeof(file.scroll_y));
            hash = hash_bytes(hash, &file.text_valid, sizeof(file.text_valid));
            hash = hash_bytes(hash, &file.insert_mode, sizeof(file.insert_mode));
            hash = hash_bytes(hash, &file.selection_active, sizeof(file.selection_active));
            hash = hash_bytes(hash, &file.dirty, sizeof(file.dirty));
            hash = hash_bytes(
                hash, &file.external_change_pending, sizeof(file.external_change_pending)
            );
            hash = hash_bytes(hash, &file.file_deleted_on_disk, sizeof(file.file_deleted_on_disk));
        }
        size_t const tree_root_name_size = editor.tree_root_name.size();
        hash = hash_bytes(hash, &tree_root_name_size, sizeof(tree_root_name_size));
        hash = hash_bytes(hash, editor.tree_root_name.data(), editor.tree_root_name.size());
        size_t const save_root_path_size = editor.save_root_path.size();
        hash = hash_bytes(hash, &save_root_path_size, sizeof(save_root_path_size));
        hash = hash_bytes(hash, editor.save_root_path.data(), editor.save_root_path.size());
        for (FileTreeEntry const& entry : editor.tree_files) {
            hash = hash_bytes(hash, &entry.open, sizeof(entry.open));
        }
        hash = hash_bytes(hash, &editor.pending_line_number, sizeof(editor.pending_line_number));
        hash = hash_bytes(hash, &editor.root_split, sizeof(editor.root_split));
        hash = hash_bytes(hash, &editor.focused_split, sizeof(editor.focused_split));
        EditorPaneKind const active_kind = focused_pane_kind(editor);
        hash = hash_bytes(hash, &active_kind, sizeof(active_kind));
        size_t const split_count = editor.split_nodes.size();
        hash = hash_bytes(hash, &split_count, sizeof(split_count));
        for (EditorSplitNode const& split : editor.split_nodes) {
            hash = hash_bytes(hash, &split.kind, sizeof(split.kind));
            hash = hash_bytes(hash, &split.first, sizeof(split.first));
            hash = hash_bytes(hash, &split.second, sizeof(split.second));
            hash = hash_bytes(hash, &split.pane, sizeof(split.pane));
            hash = hash_bytes(hash, &split.ratio, sizeof(split.ratio));
            hash = hash_bytes(hash, &split.rect, sizeof(split.rect));
        }
        size_t const focused_pane = focused_pane_index(editor);
        size_t const pane_count = editor.panes.size();
        hash = hash_bytes(hash, &pane_count, sizeof(pane_count));
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            if (index != focused_pane && editor.panes[index] != nullptr) {
                hash = hash_pane(hash, *editor.panes[index]);
            }
        }
        return hash;
    }

} // namespace code_editor
