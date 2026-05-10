#include "editor_model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace code_editor {

    inline constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);
    inline constexpr EditorCommand EDITOR_COMMANDS[] = {
        {"write", "w", "Save the current file."},
        {"quit", "q", "Close the focused split."},
        {"new", "n", "Create a scratch buffer."},
        {"write-quit", "wq", "Save and close the current buffer."},
        {"quit-force", "q!", "Close the current buffer without saving."},
        {"buffer-close", "bc", "Close the current buffer."},
        {"open", "o", "Open a file from the indexed tree."},
        {"toggle-sidebar", "tree", "Toggle the file tree sidebar."},
        {"format", "fmt", "Format the current C/C++ file."},
        {"search", "s", "Search the current buffer."},
        {"symbols", "sym", "Open document symbols."},
        {"fold-toggle", "za", "Toggle the current scope fold."},
        {"fold-close", "zc", "Fold the current scope."},
        {"fold-open", "zo", "Open the fold at the current line."},
        {"fold-close-all", "zM", "Fold all known scopes."},
        {"fold-open-all", "zR", "Open all folds."},
        {"jumps", "jl", "Open jump list."},
        {"jump-back", "jb", "Jump to previous recorded location."},
        {"jump-forward", "jf", "Jump to next recorded location."},
        {"toggle-raster-policy", "rp", "Toggle text raster policy."},
        {"global-search", "gs", "Search indexed workspace files."},
        {"config-open", "co", "Open the active config file: local override first, then global."},
        {"config-reload", "cr", "Reload global/local config files and reapply session overrides."},
        {"set", "cfg", "Apply a session override, for example set editor.font-size=14."},
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
        Vec<EditorCursor> extra_cursors = {};
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float scroll_x = 0.0f;
        float scroll_y = 0.0f;
        bool selection_active = false;
    };

    auto clear_extra_cursors(EditorState& editor) -> void;
    auto clamp_cursor(EditorState& editor) -> void;
    auto sync_shared_panes(EditorState& editor) -> void;
    auto close_focused_split(EditorState& editor) -> void;
    auto refresh_editor_dirty(EditorState& editor) -> void;
    auto request_lsp(EditorState& editor, LspRequestKind kind, StrRef new_name = {}) -> void;
    auto open_lsp_rename(EditorState& editor) -> void;
    auto close_jump_list(EditorState& editor) -> void;
    auto clear_config_request(EditorState& editor) -> void;
    auto clear_pending_line_number(EditorState& editor) -> void;
    auto set_text_search_text(EditorState& editor, StrRef text) -> void;
    auto jump_list_previous(EditorState& editor) -> void;
    auto jump_list_next(EditorState& editor) -> void;
    auto close_current_fold(EditorState& editor) -> void;
    auto open_current_fold(EditorState& editor) -> void;
    auto toggle_current_fold(EditorState& editor) -> void;
    auto close_all_folds(EditorState& editor) -> void;
    auto open_all_folds(EditorState& editor) -> void;
    auto move_filesystem_tree_cursor(EditorState& editor, int32_t direction) -> void;
    [[nodiscard]] auto tree_edit_active(EditorState const& editor) -> bool;
    auto cancel_tree_edit(EditorState& editor) -> void;
    [[nodiscard]] auto commit_tree_edit(EditorState& editor) -> bool;
    auto queue_tree_delete(EditorState& editor) -> void;
    auto queue_tree_history(EditorState& editor, bool redo) -> void;
    [[nodiscard]] auto word_range_at_position(
        EditorState const& editor,
        EditorPosition position,
        EditorPosition& start,
        EditorPosition& end
    ) -> bool;
    [[nodiscard]] auto
    find_leaf_by_kind(EditorState const& editor, size_t split, EditorPaneKind kind) -> size_t;
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

    auto remember_focused_code_split(EditorState& editor) -> void {
        if (split_valid(editor, editor.focused_split) &&
            pane_kind(editor, editor.split_nodes[editor.focused_split].pane) ==
                EditorPaneKind::CODE) {
            editor.last_code_split = editor.focused_split;
        }
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

    auto clear_extra_cursors(EditorState& editor) -> void {
        editor.extra_cursors.clear();
        editor.set_flag(EditorFlag::MULTI_CURSOR_DRAGGING, false);
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
        clear_extra_cursors(editor);
        editor.folded_ranges.clear();
        editor.folded_revision = 0u;
        editor.selection_anchor_line = 0u;
        editor.selection_anchor_column = 0u;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        editor.set_flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN, false);
        editor.scroll_x = 0.0f;
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
        close_jump_list(editor);
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
        editor.text_search_text_size = 0u;
        editor.text_search_origin_line = 0u;
        editor.text_search_text[0u] = '\0';
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.command_text_size = 0u;
        editor.command_selected = 0u;
        editor.command_text[0u] = '\0';
        clear_config_request(editor);
        editor.set_flag(EditorFlag::SAVE_REQUESTED, false);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, false);
        editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);
        editor.set_flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED, false);
        editor.close_intent = EditorCloseIntent::NONE;
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

    auto clone_text(EditorText const& source, EditorText& target, Arena& arena) -> void {
        text_buffer_clone(source, target, arena);
    }

    auto take_text(EditorText& target, EditorText& source) -> void {
        target = source;
        source = {};
    }

    auto move_editor_to_pane(EditorState& editor, EditorPane& pane) -> void {
        take_text(pane.text, editor.text);
        pane.git_diff = editor.git_diff;
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
        bool const cursors_ok =
            pane.extra_cursors.copy_from(editor.extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            pane.folded_ranges.copy_from(editor.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        pane.selection_anchor_line = editor.selection_anchor_line;
        pane.selection_anchor_column = editor.selection_anchor_column;
        pane.folded_revision = editor.folded_revision;
        pane.selection_mode = editor.selection_mode;
        pane.scroll_x = editor.scroll_x;
        pane.scroll_y = editor.scroll_y;
        pane.insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        pane.selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        pane.mouse_selecting = editor.flag(EditorFlag::MOUSE_SELECTING);
        pane.mouse_was_down = editor.flag(EditorFlag::MOUSE_WAS_DOWN);
        pane.multi_cursor_dragging = editor.flag(EditorFlag::MULTI_CURSOR_DRAGGING);
        pane.middle_mouse_was_down = editor.flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN);
        pane.dirty = editor.flag(EditorFlag::DIRTY);
        pane.external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        pane.file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        pane.git_diff_side_by_side = editor.git_diff_side_by_side;
        pane.view_kind = editor.view_kind;
    }

    auto move_pane_to_editor(EditorState& editor, EditorPane& pane) -> void {
        take_text(editor.text, pane.text);
        editor.git_diff = pane.git_diff;
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
        bool const cursors_ok =
            editor.extra_cursors.copy_from(pane.extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            editor.folded_ranges.copy_from(pane.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        editor.selection_anchor_line = pane.selection_anchor_line;
        editor.selection_anchor_column = pane.selection_anchor_column;
        editor.folded_revision = pane.folded_revision;
        editor.selection_mode = pane.selection_mode;
        editor.scroll_x = pane.scroll_x;
        editor.scroll_y = pane.scroll_y;
        editor.set_flag(EditorFlag::INSERT_MODE, pane.insert_mode);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, pane.selection_active);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, pane.mouse_selecting);
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, pane.mouse_was_down);
        editor.set_flag(EditorFlag::MULTI_CURSOR_DRAGGING, pane.multi_cursor_dragging);
        editor.set_flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN, pane.middle_mouse_was_down);
        editor.set_flag(EditorFlag::DIRTY, pane.dirty);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, pane.external_change_pending);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, pane.file_deleted_on_disk);
        editor.git_diff_side_by_side = pane.git_diff_side_by_side;
        editor.view_kind = pane.view_kind;
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
        bool const cursors_ok = pane->extra_cursors.init(0u, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok = pane->folded_ranges.init(0u, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
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
        clone_text(editor.text, pane.text, *editor.arena);
        pane.git_diff = editor.git_diff;
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
        bool const folds_ok =
            pane.folded_ranges.copy_from(editor.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        pane.selection_anchor_line = editor.selection_anchor_line;
        pane.selection_anchor_column = editor.selection_anchor_column;
        pane.folded_revision = editor.folded_revision;
        pane.selection_mode = editor.selection_mode;
        pane.scroll_x = editor.scroll_x;
        pane.scroll_y = editor.scroll_y;
        pane.insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        pane.selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        pane.mouse_selecting = false;
        pane.mouse_was_down = false;
        pane.dirty = editor.flag(EditorFlag::DIRTY);
        pane.external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        pane.file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        pane.git_diff_side_by_side = editor.git_diff_side_by_side;
        pane.view_kind = editor.view_kind;
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
        editor.last_code_split = 0u;
        editor.set_flag(EditorFlag::PANE_LOADED, false);
        load_focused_pane(editor);
    }

    auto init_editor(Arena& arena, EditorState& editor, StrRef text) -> void {
        bool const first_init = editor.text.arena == nullptr;
        if (first_init) {
            editor.arena = &arena;
            init_text_storage(editor.text, arena);

            bool const cursors_ok = editor.extra_cursors.init(0u, arena.resource());
            DEBUG_ASSERT(cursors_ok);
            (void)cursors_ok;

            bool const folds_ok = editor.folded_ranges.init(0u, arena.resource());
            DEBUG_ASSERT(folds_ok);
            (void)folds_ok;

            bool const panes_ok = editor.panes.init(0u, arena.resource());
            DEBUG_ASSERT(panes_ok);
            (void)panes_ok;

            bool const splits_ok = editor.split_nodes.init(0u, arena.resource());
            DEBUG_ASSERT(splits_ok);
            (void)splits_ok;

            bool const open_ok = editor.open_files.init(0u, arena.resource());
            DEBUG_ASSERT(open_ok);
            (void)open_ok;

            bool const git_status_ok = editor.git_status_items.init(0u, arena.resource());
            DEBUG_ASSERT(git_status_ok);
            (void)git_status_ok;

            bool const git_commits_ok = editor.git_commits.init(0u, arena.resource());
            DEBUG_ASSERT(git_commits_ok);
            (void)git_commits_ok;

            bool const git_commit_files_ok = editor.git_commit_files.init(0u, arena.resource());
            DEBUG_ASSERT(git_commit_files_ok);
            (void)git_commit_files_ok;

            bool const git_branches_ok = editor.git_branches.init(0u, arena.resource());
            DEBUG_ASSERT(git_branches_ok);
            (void)git_branches_ok;

            bool const git_commit_text_ok = editor.git_commit_text.init(0u, arena.resource());
            DEBUG_ASSERT(git_commit_text_ok);
            (void)git_commit_text_ok;

            bool const jumps_ok = editor.jumps.init(0u, arena.resource());
            DEBUG_ASSERT(jumps_ok);
            (void)jumps_ok;

            bool const global_search_ok = editor.global_search_results.init(0u, arena.resource());
            DEBUG_ASSERT(global_search_ok);
            (void)global_search_ok;
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

    auto touch_open_file(EditorState& editor, StrRef name, StrRef path) -> void {
        if (name.empty()) {
            return;
        }
        remember_open_file(editor, name, path);
        for (OpenFile& file : editor.open_files) {
            if (same_open_file(file, name, path)) {
                file.last_used = ++editor.buffer_use_stamp;
                return;
            }
        }
    }

    [[nodiscard]] auto find_tree_file_index(EditorState const& editor, StrRef path) -> size_t {
        if (path.empty()) {
            return INVALID_INDEX;
        }
        for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
            if (editor.tree_files[index].path == path) {
                return index;
            }
        }
        return INVALID_INDEX;
    }

    [[nodiscard]] auto tree_entry_visible(EditorState const& editor, size_t tree_file_index)
        -> bool {
        if (!editor.flag(EditorFlag::TREE_OPEN) || tree_file_index >= editor.tree_files.size()) {
            return false;
        }
        size_t depth = editor.tree_files[tree_file_index].depth;
        for (size_t index = tree_file_index; index > 0u && depth > 0u;) {
            --index;
            FileTreeEntry const& entry = editor.tree_files[index];
            if (!entry.is_directory || entry.depth >= depth) {
                continue;
            }
            if (!entry.open) {
                return false;
            }
            depth = entry.depth;
        }
        return true;
    }

    auto set_tree_cursor(EditorState& editor, size_t tree_cursor) -> void {
        editor.tree_cursor =
            tree_cursor < editor.tree_files.size() ? tree_cursor : TREE_CURSOR_ROOT;
        editor.tree_cursor_reveal = true;
    }

    auto clamp_filesystem_tree_cursor(EditorState& editor) -> void {
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            return;
        }
        if (editor.tree_cursor >= editor.tree_files.size() ||
            !tree_entry_visible(editor, editor.tree_cursor)) {
            set_tree_cursor(editor, TREE_CURSOR_ROOT);
        }
    }

    auto expand_filesystem_tree_to_file(EditorState& editor, size_t tree_file_index) -> void {
        if (tree_file_index >= editor.tree_files.size() ||
            editor.tree_files[tree_file_index].is_directory) {
            return;
        }

        editor.set_flag(EditorFlag::TREE_OPEN, true);
        size_t depth = editor.tree_files[tree_file_index].depth;
        for (size_t index = tree_file_index; index > 0u && depth > 0u;) {
            --index;
            FileTreeEntry& entry = editor.tree_files[index];
            if (!entry.is_directory || entry.depth >= depth) {
                continue;
            }
            entry.open = true;
            depth = entry.depth;
        }
        set_tree_cursor(editor, tree_file_index);
    }

    auto select_current_file_in_filesystem_tree(EditorState& editor) -> void {
        size_t const tree_file_index = find_tree_file_index(editor, editor.current_file_path);
        if (tree_file_index != INVALID_INDEX) {
            expand_filesystem_tree_to_file(editor, tree_file_index);
        }
    }

    [[nodiscard]] auto preferred_code_split_for_open(EditorState const& editor) -> size_t {
        if (split_valid(editor, editor.focused_split) &&
            pane_kind(editor, editor.split_nodes[editor.focused_split].pane) ==
                EditorPaneKind::CODE) {
            return editor.focused_split;
        }
        if (split_valid(editor, editor.last_code_split) &&
            pane_kind(editor, editor.split_nodes[editor.last_code_split].pane) ==
                EditorPaneKind::CODE) {
            return editor.last_code_split;
        }
        return find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::CODE);
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
        clear_extra_cursors(editor);
        editor.folded_ranges.clear();
        editor.folded_revision = 0u;
        editor.selection_anchor_line = 0u;
        editor.selection_anchor_column = 0u;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.scroll_x = 0.0f;
        editor.scroll_y = 0.0f;
        editor.set_flag(EditorFlag::INSERT_MODE, false);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
        editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, false);
        editor.set_flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN, false);
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
        bool const cursors_ok =
            view->extra_cursors.copy_from(editor.extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            view->folded_ranges.copy_from(editor.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        view->selection_anchor_line = editor.selection_anchor_line;
        view->selection_anchor_column = editor.selection_anchor_column;
        view->folded_revision = editor.folded_revision;
        view->selection_mode = editor.selection_mode;
        view->scroll_x = editor.scroll_x;
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
        bool const cursors_ok =
            editor.extra_cursors.copy_from(view->extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            editor.folded_ranges.copy_from(view->folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        editor.selection_anchor_line = view->selection_anchor_line;
        editor.selection_anchor_column = view->selection_anchor_column;
        editor.folded_revision = view->folded_revision;
        editor.selection_mode = view->selection_mode;
        editor.scroll_x = view->scroll_x;
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

            clone_text(pane->text, editor.text, *editor.arena);
            editor.git_diff = pane->git_diff;
            editor.current_file_name = pane->current_file_name;
            editor.current_file_path = pane->current_file_path;
            editor.scratch_text = pane->scratch_text;
            editor.saved_text = pane->saved_text;
            editor.undo_stack = pane->undo_stack;
            editor.redo_stack = pane->redo_stack;
            editor.file_write_stamp = pane->file_write_stamp;
            bool const folds_ok =
                editor.folded_ranges.copy_from(pane->folded_ranges, editor.arena->resource());
            DEBUG_ASSERT(folds_ok);
            (void)folds_ok;
            editor.folded_revision = pane->folded_revision;
            BASE_UNUSED(restore_focused_open_file_view(editor, name, path));
            editor.set_flag(EditorFlag::DIRTY, pane->dirty);
            editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, pane->external_change_pending);
            editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, pane->file_deleted_on_disk);
            editor.git_diff_side_by_side = pane->git_diff_side_by_side;
            editor.view_kind = pane->view_kind;
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
        size_t extra_count = 0u;
        for (size_t index = 0u; index < pane.extra_cursors.size(); ++index) {
            EditorCursor cursor = pane.extra_cursors[index];
            cursor.line = std::min(cursor.line, pane_line_count(pane) - 1u);
            cursor.column = std::min(cursor.column, pane_line_size(pane, cursor.line));
            cursor.preferred_column = std::min(cursor.preferred_column, cursor.column);
            cursor.selection_anchor_line =
                std::min(cursor.selection_anchor_line, pane_line_count(pane) - 1u);
            cursor.selection_anchor_column = std::min(
                cursor.selection_anchor_column, pane_line_size(pane, cursor.selection_anchor_line)
            );
            bool duplicate = cursor.line == pane.cursor_line && cursor.column == pane.cursor_column;
            for (size_t other = 0u; other < extra_count && !duplicate; ++other) {
                duplicate = cursor.line == pane.extra_cursors[other].line &&
                            cursor.column == pane.extra_cursors[other].column;
            }
            if (!duplicate) {
                pane.extra_cursors[extra_count] = cursor;
                extra_count += 1u;
            }
        }
        bool const resize_ok = pane.extra_cursors.resize(extra_count);
        DEBUG_ASSERT(resize_ok);
        (void)resize_ok;
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
                clone_text(editor.text, pane->text, *editor.arena);
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
            remember_focused_code_split(editor);
            sync_shared_panes(editor);
            clamp_cursor(editor);
            if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
                clamp_filesystem_tree_cursor(editor);
            }
            return;
        }
        remember_focused_code_split(editor);
        sync_shared_panes(editor);
        store_focused_pane(editor);
        editor.focused_split = split;
        load_focused_pane(editor);
        remember_focused_code_split(editor);
        clamp_cursor(editor);
        if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
            clamp_filesystem_tree_cursor(editor);
        }
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
        set_tree_cursor(editor, TREE_CURSOR_ROOT);
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

    auto open_files_sidebar(EditorState& editor) -> void {
        if (filesystem_panel_visible(editor) && editor.sidebar_tab == EditorSidebarTab::FILES) {
            close_filesystem_panels(editor);
            return;
        }
        editor.sidebar_tab = EditorSidebarTab::FILES;
        ensure_filesystem_panel(editor);
    }

    auto open_git_sidebar(EditorState& editor) -> void {
        if (filesystem_panel_visible(editor) && editor.sidebar_tab == EditorSidebarTab::GIT) {
            close_filesystem_panels(editor);
            return;
        }
        editor.sidebar_tab = EditorSidebarTab::GIT;
        editor.git_selection_focused = false;
        editor.git_control_focused = false;
        editor.git_refresh_requested = true;
        ensure_filesystem_panel(editor);
        size_t const split =
            find_leaf_by_kind(editor, editor.root_split, EditorPaneKind::FILESYSTEM);
        if (split != INVALID_INDEX) {
            focus_editor_split(editor, split);
        }
    }

    auto set_filesystem_panel_visible(EditorState& editor, bool visible) -> void {
        if (visible) {
            ensure_filesystem_panel(editor);
        } else {
            close_filesystem_panels(editor);
        }
    }

    enum class GitVisibleRowKind : uint8_t {
        NONE,
        STAGED_HEADER,
        CHANGES_HEADER,
        GRAPH_HEADER,
        STATUS,
        COMMIT,
        COMMIT_FILE,
    };

    struct GitVisibleRow {
        GitVisibleRowKind kind = GitVisibleRowKind::NONE;
        size_t index = 0u;
    };

    [[nodiscard]] auto git_commit_file_matches(GitCommitFile const& file, GitCommit const& commit)
        -> bool {
        return file.commit_oid == commit.oid;
    }

    [[nodiscard]] auto git_model_status_scope_count(EditorState const& editor, GitStatusScope scope)
        -> size_t {
        size_t count = 0u;
        for (GitStatusItem const& item : editor.git_status_items) {
            if (item.scope == scope) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto git_model_status_change_count(EditorState const& editor) -> size_t {
        return git_model_status_scope_count(editor, GitStatusScope::UNSTAGED) +
               git_model_status_scope_count(editor, GitStatusScope::UNTRACKED);
    }

    [[nodiscard]] auto git_model_commit_search_query(EditorState const& editor) -> StrRef {
        return StrRef(editor.git_commit_search_text).trim();
    }

    [[nodiscard]] auto git_model_commit_visible(EditorState const& editor, GitCommit const& commit)
        -> bool {
        return git_commit_matches_search(commit, git_model_commit_search_query(editor));
    }

    [[nodiscard]] auto git_visible_row_count(EditorState const& editor) -> size_t {
        size_t count = 1u;
        size_t const staged_count = git_model_status_scope_count(editor, GitStatusScope::STAGED);
        if (staged_count != 0u) {
            count += 1u;
            if (editor.git_staged_open) {
                count += staged_count;
            }
        }
        size_t const change_count = git_model_status_change_count(editor);
        if (change_count != 0u || editor.git_status_items.empty()) {
            count += 1u;
            if (editor.git_changes_open) {
                count += change_count;
            }
        }
        if (!editor.git_graph_open) {
            return count;
        }
        for (GitCommit const& commit : editor.git_commits) {
            if (!git_model_commit_visible(editor, commit)) {
                continue;
            }
            count += 1u;
            if (!commit.open) {
                continue;
            }
            for (GitCommitFile const& file : editor.git_commit_files) {
                if (git_commit_file_matches(file, commit)) {
                    count += 1u;
                }
            }
        }
        return count;
    }

    [[nodiscard]] auto git_visible_status_row(
        EditorState const& editor, size_t target, size_t& row, GitStatusScope scope
    ) -> GitVisibleRow {
        for (size_t index = 0u; index < editor.git_status_items.size(); ++index) {
            if (editor.git_status_items[index].scope != scope) {
                continue;
            }
            if (row == target) {
                return {GitVisibleRowKind::STATUS, index};
            }
            row += 1u;
        }
        return {};
    }

    [[nodiscard]] auto git_visible_row(EditorState const& editor, size_t target) -> GitVisibleRow {
        size_t row = 0u;
        size_t const staged_count = git_model_status_scope_count(editor, GitStatusScope::STAGED);
        if (staged_count != 0u) {
            if (row == target) {
                return {GitVisibleRowKind::STAGED_HEADER, 0u};
            }
            row += 1u;
            if (editor.git_staged_open) {
                GitVisibleRow const result =
                    git_visible_status_row(editor, target, row, GitStatusScope::STAGED);
                if (result.kind != GitVisibleRowKind::NONE) {
                    return result;
                }
            }
        }
        size_t const change_count = git_model_status_change_count(editor);
        if (change_count != 0u || editor.git_status_items.empty()) {
            if (row == target) {
                return {GitVisibleRowKind::CHANGES_HEADER, 0u};
            }
            row += 1u;
            if (editor.git_changes_open) {
                GitVisibleRow result =
                    git_visible_status_row(editor, target, row, GitStatusScope::UNSTAGED);
                if (result.kind != GitVisibleRowKind::NONE) {
                    return result;
                }
                result = git_visible_status_row(editor, target, row, GitStatusScope::UNTRACKED);
                if (result.kind != GitVisibleRowKind::NONE) {
                    return result;
                }
            }
        }
        if (row == target) {
            return {GitVisibleRowKind::GRAPH_HEADER, 0u};
        }
        row += 1u;
        if (!editor.git_graph_open) {
            return {};
        }
        for (size_t commit_index = 0u; commit_index < editor.git_commits.size(); ++commit_index) {
            GitCommit const& commit = editor.git_commits[commit_index];
            if (!git_model_commit_visible(editor, commit)) {
                continue;
            }
            if (row == target) {
                return {GitVisibleRowKind::COMMIT, commit_index};
            }
            row += 1u;
            if (!commit.open) {
                continue;
            }
            for (size_t file_index = 0u; file_index < editor.git_commit_files.size();
                 ++file_index) {
                if (!git_commit_file_matches(editor.git_commit_files[file_index], commit)) {
                    continue;
                }
                if (row == target) {
                    return {GitVisibleRowKind::COMMIT_FILE, file_index};
                }
                row += 1u;
            }
        }
        return {};
    }

    auto clamp_git_selection(EditorState& editor) -> void {
        size_t const count = git_visible_row_count(editor);
        if (count == 0u) {
            editor.git_selected = 0u;
        } else if (editor.git_selected >= count) {
            editor.git_selected = count - 1u;
        }
        if (editor.git_commit_popup >= editor.git_commits.size()) {
            editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
        }
    }

    auto open_git_commit_popup(EditorState& editor) -> void {
        clamp_git_selection(editor);
        GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
        editor.git_commit_popup =
            row.kind == GitVisibleRowKind::COMMIT ? row.index : GIT_COMMIT_POPUP_NONE;
        editor.git_commit_popup_selection = {};
        editor.git_commit_popup_keyboard = editor.git_commit_popup != GIT_COMMIT_POPUP_NONE;
        editor.git_commit_popup_mouse_known = false;
    }

    auto close_git_commit_popup(EditorState& editor) -> void {
        editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
        editor.git_commit_popup_selection = {};
        editor.git_commit_popup_keyboard = false;
        editor.git_commit_popup_mouse_known = false;
    }

    auto request_more_git_commits_if_needed(EditorState& editor) -> void {
        if (!editor.git_graph_open || !editor.git_commits_more || editor.git_commits_loading ||
            editor.git_commits.empty()) {
            return;
        }
        GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
        GitCommit const& last_commit = editor.git_commits[editor.git_commits.size() - 1u];
        bool const selected_last_commit =
            row.kind == GitVisibleRowKind::COMMIT && row.index + 1u == editor.git_commits.size();
        bool selected_last_commit_file = false;
        if (row.kind == GitVisibleRowKind::COMMIT_FILE) {
            selected_last_commit_file =
                editor.git_commit_files[row.index].commit_oid == last_commit.oid;
        }
        if (selected_last_commit || selected_last_commit_file) {
            editor.git_commit_load_more_requested = true;
        }
    }

    auto move_git_selection(EditorState& editor, int32_t direction) -> void {
        size_t const count = git_visible_row_count(editor);
        if (count == 0u) {
            editor.git_selected = 0u;
            return;
        }
        if (direction > 0) {
            editor.git_selected =
                std::min(editor.git_selected + static_cast<size_t>(direction), count - 1u);
        } else {
            size_t const delta = static_cast<size_t>(-direction);
            editor.git_selected = delta > editor.git_selected ? 0u : editor.git_selected - delta;
        }
        close_git_commit_popup(editor);
        editor.git_selection_focused = true;
        editor.git_cursor_reveal = true;
        request_more_git_commits_if_needed(editor);
    }

    [[nodiscard]] auto git_staged_change_count(EditorState const& editor) -> size_t {
        return git_model_status_scope_count(editor, GitStatusScope::STAGED);
    }

    auto set_git_request(EditorState& editor, GitRequest request) -> void {
        if (editor.arena == nullptr) {
            return;
        }
        if (!request.path.empty()) {
            request.path = arena_copy_cstr(*editor.arena, request.path);
        }
        if (!request.old_path.empty()) {
            request.old_path = arena_copy_cstr(*editor.arena, request.old_path);
        }
        if (!request.commit_oid.empty()) {
            request.commit_oid = arena_copy_cstr(*editor.arena, request.commit_oid);
        }
        if (!request.message.empty()) {
            request.message = arena_copy_cstr(*editor.arena, request.message);
        }
        if (!request.branch.empty()) {
            request.branch = arena_copy_cstr(*editor.arena, request.branch);
        }
        editor.git_request = request;
    }

    auto submit_git_commit(EditorState& editor) -> void {
        if (git_staged_change_count(editor) == 0u) {
            editor.git_status_text = "Stage changes before committing.";
            return;
        }
        StrRef const message = editor.git_commit_text.str().trim();
        if (message.empty()) {
            editor.git_status_text = "Commit message required.";
            return;
        }
        set_git_request(editor, {.kind = GitRequestKind::COMMIT, .message = message});
        editor.git_commit_text.reset();
    }

    auto set_git_diff_view_text(EditorState& editor) -> void {
        if (editor.arena == nullptr || editor.text.arena == nullptr) {
            return;
        }
        StrRef const text = git_render_diff_document(
            *editor.text.arena, editor.git_diff, editor.git_diff_side_by_side
        );
        set_editor_text(editor, text);
        editor.saved_text = text;
        editor.view_kind = EditorViewKind::GIT_DIFF;
        editor.set_flag(EditorFlag::DIRTY, false);
        editor.set_flag(EditorFlag::INSERT_MODE, false);
    }

    auto activate_git_selection(EditorState& editor) -> void {
        close_git_commit_popup(editor);
        clamp_git_selection(editor);
        GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
        if (row.kind == GitVisibleRowKind::STAGED_HEADER) {
            editor.git_staged_open = !editor.git_staged_open;
        } else if (row.kind == GitVisibleRowKind::CHANGES_HEADER) {
            editor.git_changes_open = !editor.git_changes_open;
        } else if (row.kind == GitVisibleRowKind::GRAPH_HEADER) {
            editor.git_graph_open = !editor.git_graph_open;
        } else if (row.kind == GitVisibleRowKind::STATUS) {
            GitStatusItem const& item = editor.git_status_items[row.index];
            set_git_request(
                editor,
                {
                    .kind = GitRequestKind::OPEN_STATUS_DIFF,
                    .scope = item.scope,
                    .path = item.path,
                    .old_path = item.old_path,
                }
            );
        } else if (row.kind == GitVisibleRowKind::COMMIT) {
            editor.git_commits[row.index].open = !editor.git_commits[row.index].open;
        } else if (row.kind == GitVisibleRowKind::COMMIT_FILE) {
            GitCommitFile const& file = editor.git_commit_files[row.index];
            set_git_request(
                editor,
                {
                    .kind = GitRequestKind::OPEN_COMMIT_DIFF,
                    .path = file.path,
                    .old_path = file.old_path,
                    .commit_oid = file.commit_oid,
                }
            );
        }
    }

    auto handle_git_normal_char(EditorState& editor, char ch) -> void {
        clamp_git_selection(editor);
        editor.set_flag(EditorFlag::PENDING_G, false);
        if (ch != 'K') {
            close_git_commit_popup(editor);
        }
        switch (ch) {
        case ' ':
            editor.set_flag(EditorFlag::PENDING_LEADER, true);
            break;
        case 'K':
            open_git_commit_popup(editor);
            break;
        case '\r':
            activate_git_selection(editor);
            break;
        case 'r':
            editor.git_refresh_requested = true;
            editor.git_log_refresh_requested = true;
            break;
        case 'P':
            set_git_request(editor, {.kind = GitRequestKind::PULL});
            break;
        case 's': {
            GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
            if (row.kind != GitVisibleRowKind::STATUS) {
                break;
            }
            GitStatusItem const& item = editor.git_status_items[row.index];
            if (item.scope == GitStatusScope::UNSTAGED || item.scope == GitStatusScope::UNTRACKED) {
                set_git_request(editor, {.kind = GitRequestKind::STAGE, .path = item.path});
            }
        } break;
        case 'u': {
            GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
            if (row.kind != GitVisibleRowKind::STATUS) {
                break;
            }
            GitStatusItem const& item = editor.git_status_items[row.index];
            if (item.scope == GitStatusScope::STAGED) {
                set_git_request(editor, {.kind = GitRequestKind::UNSTAGE, .path = item.path});
            }
        } break;
        case 'p':
            set_git_request(editor, {.kind = GitRequestKind::PUSH});
            break;
        default:
            break;
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
        remember_focused_code_split(editor);
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
        remember_focused_code_split(editor);
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
        remember_focused_code_split(editor);
        clamp_cursor(editor);
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, filesystem_panel_visible(editor));
    }

    [[nodiscard]] auto copy_editor_text(EditorState& editor) -> StrRef {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        return text_buffer_copy(editor.text, *editor.text.arena);
    }

    [[nodiscard]] auto tree_edit_active(EditorState const& editor) -> bool {
        return editor.tree_edit_mode != TreeEditMode::NONE;
    }

    auto copy_tree_operation_path(char* target, StrRef source) -> void {
        size_t const size = std::min(source.size(), TREE_OPERATION_PATH_CAPACITY - 1u);
        if (size != 0u) {
            std::memcpy(target, source.data(), size);
        }
        target[size] = '\0';
    }

    [[nodiscard]] auto trim_tree_path(StrRef path) -> StrRef {
        while (path.size() > 1u && (path.back() == '\\' || path.back() == '/') &&
               !(path.size() == 3u && path[1u] == ':')) {
            path.remove_suffix(1u);
        }
        return path;
    }

    [[nodiscard]] auto tree_path_parent(StrRef path) -> StrRef {
        path = trim_tree_path(path);
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

    [[nodiscard]] auto append_tree_path(char* buffer, size_t capacity, size_t& size, StrRef text)
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

    [[nodiscard]] auto
    joined_tree_path(StrRef directory, StrRef name, char* buffer, size_t capacity) -> StrRef {
        size_t size = 0u;
        StrRef const trimmed_directory = trim_tree_path(directory);
        if (!append_tree_path(buffer, capacity, size, trimmed_directory)) {
            return {};
        }
        if (!name.empty()) {
            if (!trimmed_directory.empty() && !trimmed_directory.ends_with('\\') &&
                !trimmed_directory.ends_with('/')) {
                if (!append_tree_path(buffer, capacity, size, "\\")) {
                    return {};
                }
            }
            if (!append_tree_path(buffer, capacity, size, name)) {
                return {};
            }
        }
        return StrRef(buffer, size);
    }

    [[nodiscard]] auto tree_name_valid(StrRef text) -> bool {
        if (text.empty() || text == "." || text == "..") {
            return false;
        }
        for (char const ch : text) {
            switch (ch) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                return false;
            default:
                break;
            }
        }
        return true;
    }

    [[nodiscard]] auto queue_tree_operation(
        EditorState& editor, TreeOperationKind kind, StrRef source_path, StrRef target_path = {}
    ) -> bool {
        if (editor.shared_tree_operation_request == nullptr || editor.tree_operation_pending) {
            return false;
        }
        editor.tree_operation_generation += 1u;
        *editor.shared_tree_operation_request = {
            .generation = editor.tree_operation_generation,
            .kind = kind,
        };
        copy_tree_operation_path(editor.shared_tree_operation_request->source_path, source_path);
        copy_tree_operation_path(editor.shared_tree_operation_request->target_path, target_path);
        editor.tree_operation_pending = true;
        return true;
    }

    auto begin_tree_edit(EditorState& editor, TreeEditMode mode, StrRef text, size_t column)
        -> void {
        set_editor_text(editor, text);
        mark_editor_saved(editor);
        editor.tree_edit_mode = mode;
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_line = 0u;
        editor.cursor_column = std::min(column, text.size());
        editor.preferred_column = editor.cursor_column;
    }

    auto cancel_tree_edit(EditorState& editor) -> void {
        editor.tree_edit_mode = TreeEditMode::NONE;
        editor.set_flag(EditorFlag::INSERT_MODE, false);
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, false);
    }

    [[nodiscard]] auto tree_create_parent_path(EditorState const& editor) -> StrRef {
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            return editor.save_root_path;
        }
        if (editor.tree_cursor >= editor.tree_files.size()) {
            return {};
        }
        FileTreeEntry const& entry = editor.tree_files[editor.tree_cursor];
        return entry.is_directory ? entry.path : tree_path_parent(entry.path);
    }

    auto begin_tree_rename(EditorState& editor, size_t column) -> void {
        if (editor.tree_operation_pending || editor.tree_cursor == TREE_CURSOR_ROOT ||
            editor.tree_cursor >= editor.tree_files.size()) {
            return;
        }
        begin_tree_edit(
            editor, TreeEditMode::RENAME, editor.tree_files[editor.tree_cursor].name, column
        );
    }

    auto begin_tree_create(EditorState& editor, TreeEditMode mode) -> void {
        if (editor.tree_operation_pending || tree_create_parent_path(editor).empty()) {
            return;
        }
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            editor.set_flag(EditorFlag::TREE_OPEN, true);
            editor.tree_cursor_reveal = true;
        } else if (editor.tree_cursor < editor.tree_files.size() &&
                   editor.tree_files[editor.tree_cursor].is_directory) {
            editor.tree_files[editor.tree_cursor].open = true;
            editor.tree_cursor_reveal = true;
        }
        begin_tree_edit(editor, mode, {}, 0u);
    }

    [[nodiscard]] auto commit_tree_edit(EditorState& editor) -> bool {
        if (!tree_edit_active(editor) || editor.text.arena == nullptr) {
            cancel_tree_edit(editor);
            return false;
        }

        StrRef const name = copy_editor_text(editor);
        if (!tree_name_valid(name)) {
            return false;
        }

        char path_buffer[TREE_OPERATION_PATH_CAPACITY] = {};
        switch (editor.tree_edit_mode) {
        case TreeEditMode::RENAME: {
            if (editor.tree_cursor == TREE_CURSOR_ROOT ||
                editor.tree_cursor >= editor.tree_files.size()) {
                cancel_tree_edit(editor);
                return false;
            }
            FileTreeEntry const& entry = editor.tree_files[editor.tree_cursor];
            if (name == entry.name) {
                cancel_tree_edit(editor);
                return true;
            }
            StrRef const target_path = joined_tree_path(
                tree_path_parent(entry.path), name, path_buffer, sizeof(path_buffer)
            );
            if (target_path.empty() ||
                !queue_tree_operation(editor, TreeOperationKind::RENAME, entry.path, target_path)) {
                return false;
            }
        } break;
        case TreeEditMode::CREATE_FILE:
        case TreeEditMode::CREATE_DIRECTORY: {
            StrRef const parent_path = tree_create_parent_path(editor);
            StrRef const target_path =
                joined_tree_path(parent_path, name, path_buffer, sizeof(path_buffer));
            TreeOperationKind const kind = editor.tree_edit_mode == TreeEditMode::CREATE_DIRECTORY
                                               ? TreeOperationKind::CREATE_DIRECTORY
                                               : TreeOperationKind::CREATE_FILE;
            if (target_path.empty() || !queue_tree_operation(editor, kind, target_path)) {
                return false;
            }
        } break;
        case TreeEditMode::NONE:
            return false;
        }

        cancel_tree_edit(editor);
        return true;
    }

    auto queue_tree_delete(EditorState& editor) -> void {
        if (editor.tree_operation_pending || editor.tree_cursor == TREE_CURSOR_ROOT ||
            editor.tree_cursor >= editor.tree_files.size()) {
            return;
        }
        BASE_UNUSED(queue_tree_operation(
            editor, TreeOperationKind::REMOVE, editor.tree_files[editor.tree_cursor].path
        ));
    }

    auto queue_tree_history(EditorState& editor, bool redo) -> void {
        if (editor.tree_operation_pending || tree_edit_active(editor)) {
            return;
        }
        BASE_UNUSED(queue_tree_operation(
            editor, redo ? TreeOperationKind::REDO : TreeOperationKind::UNDO, {}
        ));
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
        bool const cursors_ok =
            entry->extra_cursors.copy_from(editor.extra_cursors, editor.text.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        entry->selection_anchor_line = editor.selection_anchor_line;
        entry->selection_anchor_column = editor.selection_anchor_column;
        entry->selection_mode = editor.selection_mode;
        entry->scroll_x = editor.scroll_x;
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
        bool const cursors_ok =
            editor.extra_cursors.copy_from(entry.extra_cursors, editor.text.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        editor.selection_anchor_line = entry.selection_anchor_line;
        editor.selection_anchor_column = entry.selection_anchor_column;
        editor.selection_mode = entry.selection_mode;
        editor.scroll_x = entry.scroll_x;
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
            if (editor.current_file_name != SCRATCH_FILE_NAME) {
                return;
            }
            editor.scratch_text = copy_editor_text(editor);
            mark_editor_saved(editor);
        }
    }

    auto open_scratch_file(EditorState& editor) -> void {
        set_editor_text(editor, editor.scratch_text);
        editor.git_diff = {};
        editor.git_diff_side_by_side = true;
        editor.view_kind = EditorViewKind::TEXT;
        editor.current_file_name = SCRATCH_FILE_NAME;
        editor.current_file_path = {};
        editor.file_write_stamp = 0u;
        touch_open_file(editor, SCRATCH_FILE_NAME, {});
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
        close_jump_list(editor);
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
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
        if (editor.view_kind == EditorViewKind::GIT_DIFF) {
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

    [[nodiscard]] auto line_has_trailing_newline(EditorState const& editor, size_t line) -> bool {
        return line + 1u < editor_line_count(editor);
    }

    [[nodiscard]] auto fold_range_valid(EditorState const& editor, EditorFoldRange range) -> bool {
        return range.start_line < range.end_line && range.end_line < editor_line_count(editor);
    }

    [[nodiscard]] auto folds_active(EditorState const& editor) -> bool {
        return editor.folded_revision == editor.text.revision && !editor.folded_ranges.empty();
    }

    [[nodiscard]] auto lsp_folding_ranges(EditorState const& editor)
        -> Slice<LspFoldingRange const> {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->folding_ranges.empty()) {
            return {};
        }
        if (!editor.lsp_bridge->folding_ranges_path.empty() &&
            editor.lsp_bridge->folding_ranges_path != editor.current_file_path) {
            return {};
        }
        if (editor.lsp_bridge->folding_ranges_revision != 0u &&
            editor.lsp_bridge->folding_ranges_revision != editor.text.revision) {
            return {};
        }
        return editor.lsp_bridge->folding_ranges;
    }

    [[nodiscard]] auto
    editor_fold_from_lsp(EditorState const& editor, LspFoldingRange range, EditorFoldRange& out)
        -> bool {
        size_t const line_count = editor_line_count(editor);
        out = {
            .start_line = std::min(range.start_line, line_count - 1u),
            .end_line = std::min(range.end_line, line_count - 1u),
        };
        return fold_range_valid(editor, out);
    }

    [[nodiscard]] auto fold_contains_line(EditorFoldRange range, size_t line) -> bool {
        return range.start_line <= line && line <= range.end_line;
    }

    [[nodiscard]] auto
    folded_range_starting_at_line(EditorState const& editor, size_t line, EditorFoldRange& out)
        -> bool {
        if (!folds_active(editor)) {
            return false;
        }

        EditorFoldRange const* it = std::lower_bound(
            editor.folded_ranges.begin(),
            editor.folded_ranges.end(),
            line,
            [](EditorFoldRange range, size_t value) { return range.start_line < value; }
        );
        for (; it != editor.folded_ranges.end(); ++it) {
            EditorFoldRange const range = *it;
            if (range.start_line != line) {
                break;
            }
            if (fold_range_valid(editor, range)) {
                out = range;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto
    lsp_range_starting_at_line(EditorState const& editor, size_t line, EditorFoldRange& out)
        -> bool {
        Slice<LspFoldingRange const> const ranges = lsp_folding_ranges(editor);
        LspFoldingRange const* it = std::lower_bound(
            ranges.begin(), ranges.end(), line, [](LspFoldingRange range, size_t value) {
                return range.start_line < value;
            }
        );
        for (; it != ranges.end(); ++it) {
            LspFoldingRange const range = *it;
            if (range.start_line != line) {
                break;
            }
            if (editor_fold_from_lsp(editor, range, out)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto folded_header_for_line(EditorState const& editor, size_t line) -> size_t {
        if (!folds_active(editor)) {
            return line;
        }

        EditorFoldRange const* const begin = editor.folded_ranges.begin();
        EditorFoldRange const* it = std::lower_bound(
            begin, editor.folded_ranges.end(), line, [](EditorFoldRange range, size_t value) {
                return range.start_line < value;
            }
        );
        while (it != begin) {
            --it;
            EditorFoldRange const range = *it;
            if (fold_range_valid(editor, range) && range.start_line < line &&
                line <= range.end_line) {
                return range.start_line;
            }
            if (range.end_line < line) {
                break;
            }
        }
        return line;
    }

    [[nodiscard]] auto editor_line_hidden(EditorState const& editor, size_t line) -> bool {
        return folded_header_for_line(editor, line) != line;
    }

    [[nodiscard]] auto editor_line_folded(EditorState const& editor, size_t line) -> bool {
        EditorFoldRange range = {};
        return folded_range_starting_at_line(editor, line, range);
    }

    [[nodiscard]] auto
    fold_range_starting_at_line(EditorState const& editor, size_t line, EditorFoldRange& out)
        -> bool {
        return folded_range_starting_at_line(editor, line, out) ||
               lsp_range_starting_at_line(editor, line, out);
    }

    [[nodiscard]] auto editor_fold_info(EditorState const& editor, size_t line) -> EditorFoldInfo {
        EditorFoldRange range = {};
        if (editor_line_hidden(editor, line) || !fold_range_starting_at_line(editor, line, range)) {
            return {};
        }

        bool const folded = editor_line_folded(editor, line);
        return {
            .foldable = true,
            .folded = folded,
            .hidden_line_count = folded ? range.end_line - range.start_line : 0u,
        };
    }

    [[nodiscard]] auto editor_line_foldable(EditorState const& editor, size_t line) -> bool {
        return editor_fold_info(editor, line).foldable;
    }

    [[nodiscard]] auto editor_fold_hidden_line_count(EditorState const& editor, size_t line)
        -> size_t {
        return editor_fold_info(editor, line).hidden_line_count;
    }

    [[nodiscard]] auto editor_visible_line_count(EditorState const& editor) -> size_t {
        size_t const line_count = editor_line_count(editor);
        if (!folds_active(editor)) {
            return std::max<size_t>(1u, line_count);
        }

        size_t count = 0u;
        size_t line = 0u;
        for (EditorFoldRange const range : editor.folded_ranges) {
            if (!fold_range_valid(editor, range) || range.end_line < line) {
                continue;
            }
            if (range.start_line >= line) {
                count += range.start_line - line + 1u;
            }
            line = range.end_line + 1u;
            if (line >= line_count) {
                break;
            }
        }
        count += line < line_count ? line_count - line : 0u;
        return std::max<size_t>(1u, count);
    }

    [[nodiscard]] auto editor_visible_line_at(EditorState const& editor, size_t index) -> size_t {
        size_t const line_count = editor_line_count(editor);
        if (!folds_active(editor)) {
            return std::min(index, line_count - 1u);
        }

        size_t visible = 0u;
        size_t line = 0u;
        for (EditorFoldRange const range : editor.folded_ranges) {
            if (!fold_range_valid(editor, range) || range.end_line < line) {
                continue;
            }
            if (range.start_line > line) {
                size_t const span = range.start_line - line;
                if (index < visible + span) {
                    return line + index - visible;
                }
                visible += span;
            }
            if (index == visible) {
                return range.start_line;
            }
            visible += 1u;
            line = range.end_line + 1u;
            if (line >= line_count) {
                break;
            }
        }
        if (line < line_count && index < visible + line_count - line) {
            return line + index - visible;
        }
        return line_count - 1u;
    }

    [[nodiscard]] auto editor_next_visible_line(EditorState const& editor, size_t line) -> size_t {
        EditorFoldRange range = {};
        if (folded_range_starting_at_line(editor, line, range)) {
            return range.end_line + 1u;
        }
        return line + 1u;
    }

    [[nodiscard]] auto editor_visible_line_index(EditorState const& editor, size_t line) -> size_t {
        line = folded_header_for_line(editor, std::min(line, editor_line_count(editor) - 1u));
        if (!folds_active(editor)) {
            return line;
        }

        size_t visible = line;
        for (EditorFoldRange const range : editor.folded_ranges) {
            if (!fold_range_valid(editor, range)) {
                continue;
            }
            if (range.start_line >= line) {
                break;
            }
            visible -= range.end_line - range.start_line;
        }
        return visible;
    }

    auto sort_folded_ranges(EditorState& editor) -> void {
        if (editor.folded_ranges.size() < 2u) {
            return;
        }
        std::sort(editor.folded_ranges.begin(), editor.folded_ranges.end(), [](auto lhs, auto rhs) {
            if (lhs.start_line != rhs.start_line) {
                return lhs.start_line < rhs.start_line;
            }
            return lhs.end_line > rhs.end_line;
        });
    }

    auto prepare_folds_for_current_revision(EditorState& editor) -> void {
        if (editor.folded_revision == editor.text.revision) {
            return;
        }
        editor.folded_ranges.clear();
        editor.folded_revision = editor.text.revision;
    }

    auto add_fold_range(EditorState& editor, EditorFoldRange range) -> bool {
        if (!fold_range_valid(editor, range)) {
            return false;
        }
        prepare_folds_for_current_revision(editor);
        if (editor_line_hidden(editor, range.start_line)) {
            return false;
        }
        size_t count = 0u;
        for (size_t index = 0u; index < editor.folded_ranges.size(); ++index) {
            EditorFoldRange const existing = editor.folded_ranges[index];
            if (existing.start_line == range.start_line) {
                return false;
            }
            if (existing.start_line > range.start_line && existing.end_line <= range.end_line) {
                continue;
            }
            editor.folded_ranges[count] = existing;
            count += 1u;
        }
        BASE_UNUSED(editor.folded_ranges.resize(count));
        bool const ok = editor.folded_ranges.push_back(range);
        DEBUG_ASSERT(ok);
        (void)ok;
        sort_folded_ranges(editor);
        return true;
    }

    auto remove_fold_at_line(EditorState& editor, size_t line) -> bool {
        prepare_folds_for_current_revision(editor);
        line = folded_header_for_line(editor, line);
        for (size_t index = 0u; index < editor.folded_ranges.size(); ++index) {
            if (editor.folded_ranges[index].start_line == line) {
                editor.folded_ranges.ordered_remove(index);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto current_fold_range(EditorState const& editor, EditorFoldRange& out) -> bool {
        if (fold_range_starting_at_line(editor, editor.cursor_line, out) &&
            editor_line_folded(editor, editor.cursor_line)) {
            return true;
        }

        bool found = false;
        size_t best_size = static_cast<size_t>(-1);
        for (LspFoldingRange const range : lsp_folding_ranges(editor)) {
            EditorFoldRange fold = {};
            if (!editor_fold_from_lsp(editor, range, fold) ||
                !fold_contains_line(fold, editor.cursor_line)) {
                continue;
            }
            size_t const size = fold.end_line - fold.start_line;
            if (!found || size < best_size) {
                out = fold;
                best_size = size;
                found = true;
            }
        }
        return found;
    }

    auto request_folding_ranges(EditorState& editor) -> void {
        request_lsp(editor, LspRequestKind::FOLDING_RANGE);
    }

    auto close_current_fold(EditorState& editor) -> void {
        EditorFoldRange range = {};
        if (current_fold_range(editor, range)) {
            clear_extra_cursors(editor);
            BASE_UNUSED(add_fold_range(editor, range));
        } else {
            request_folding_ranges(editor);
        }
    }

    auto open_current_fold(EditorState& editor) -> void {
        BASE_UNUSED(remove_fold_at_line(editor, editor.cursor_line));
    }

    auto toggle_current_fold(EditorState& editor) -> void {
        if (editor_line_folded(editor, editor.cursor_line)) {
            open_current_fold(editor);
        } else {
            close_current_fold(editor);
        }
    }

    auto close_all_folds(EditorState& editor) -> void {
        Slice<LspFoldingRange const> const ranges = lsp_folding_ranges(editor);
        if (ranges.empty()) {
            request_folding_ranges(editor);
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        EditorFoldRange* const folds = arena_alloc<EditorFoldRange>(*temp.arena(), ranges.size());
        size_t count = 0u;
        for (LspFoldingRange const range : ranges) {
            EditorFoldRange fold = {};
            if (editor_fold_from_lsp(editor, range, fold)) {
                folds[count] = fold;
                count += 1u;
            }
        }
        if (count > 1u) {
            std::sort(folds, folds + count, [](auto lhs, auto rhs) {
                if (lhs.start_line != rhs.start_line) {
                    return lhs.start_line < rhs.start_line;
                }
                return lhs.end_line > rhs.end_line;
            });
        }

        editor.folded_ranges.clear();
        editor.folded_revision = editor.text.revision;
        for (size_t index = 0u; index < count; ++index) {
            if (!editor_line_hidden(editor, folds[index].start_line)) {
                BASE_UNUSED(editor.folded_ranges.push_back(folds[index]));
            }
        }
    }

    auto open_all_folds(EditorState& editor) -> void {
        editor.folded_ranges.clear();
        editor.folded_revision = editor.text.revision;
    }

    auto toggle_editor_fold_at_line(EditorState& editor, size_t line) -> void {
        if (editor_line_folded(editor, line)) {
            BASE_UNUSED(remove_fold_at_line(editor, line));
            return;
        }
        EditorFoldRange range = {};
        if (fold_range_starting_at_line(editor, line, range)) {
            clear_extra_cursors(editor);
            BASE_UNUSED(add_fold_range(editor, range));
        } else {
            request_folding_ranges(editor);
        }
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
    [[nodiscard]] auto has_extra_cursors(EditorState const& editor) -> bool;
    auto move_all_cursors_vertical(EditorState& editor, int32_t delta, bool select) -> void;

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
        if (position.column == size && size != 0u &&
            !line_has_trailing_newline(editor, position.line)) {
            position.column -= 1u;
        }
        return position;
    }

    auto clear_selection(EditorState& editor) -> void {
        editor.selection_anchor_line = editor.cursor_line;
        editor.selection_anchor_column = editor.cursor_column;
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            EditorCursor& cursor = editor.extra_cursors[index];
            cursor.selection_anchor_line = cursor.line;
            cursor.selection_anchor_column = cursor.column;
        }
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

    [[nodiscard]] auto
    selection_range(EditorState const& editor, EditorPosition cursor, EditorPosition anchor)
        -> EditorSelectionRange {
        if (!editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            return {};
        }

        cursor = clamp_position(editor, cursor);
        anchor = clamp_position(editor, anchor);
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

    [[nodiscard]] auto editor_selection_range(EditorState const& editor) -> EditorSelectionRange {
        return selection_range(editor, cursor_position(editor), selection_anchor(editor));
    }

    [[nodiscard]] auto editor_extra_selection_range(EditorState const& editor, size_t index)
        -> EditorSelectionRange {
        if (index >= editor.extra_cursors.size()) {
            return {};
        }
        EditorCursor const cursor = editor.extra_cursors[index];
        return selection_range(
            editor,
            {cursor.line, cursor.column},
            {cursor.selection_anchor_line, cursor.selection_anchor_column}
        );
    }

    auto clamp_cursor(EditorState& editor) -> void {
        if (editor_line_count(editor) == 0u) {
            insert_line(editor, 0u, "");
        }
        editor.cursor_line = std::min(editor.cursor_line, editor_line_count(editor) - 1u);
        editor.cursor_column =
            std::min(editor.cursor_column, line_size(editor, editor.cursor_line));
        editor.preferred_column = std::min(editor.preferred_column, editor.cursor_column);
        size_t extra_count = 0u;
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            EditorCursor cursor = editor.extra_cursors[index];
            cursor.line = std::min(cursor.line, editor_line_count(editor) - 1u);
            cursor.column = std::min(cursor.column, line_size(editor, cursor.line));
            cursor.preferred_column = std::min(cursor.preferred_column, cursor.column);
            cursor.selection_anchor_line =
                std::min(cursor.selection_anchor_line, editor_line_count(editor) - 1u);
            cursor.selection_anchor_column = std::min(
                cursor.selection_anchor_column, line_size(editor, cursor.selection_anchor_line)
            );
            bool duplicate =
                cursor.line == editor.cursor_line && cursor.column == editor.cursor_column;
            for (size_t other = 0u; other < extra_count && !duplicate; ++other) {
                duplicate = cursor.line == editor.extra_cursors[other].line &&
                            cursor.column == editor.extra_cursors[other].column;
            }
            if (!duplicate) {
                editor.extra_cursors[extra_count] = cursor;
                extra_count += 1u;
            }
        }
        bool const resize_ok = editor.extra_cursors.resize(extra_count);
        DEBUG_ASSERT(resize_ok);
        (void)resize_ok;
        clamp_selection(editor);
    }

    auto set_cursor(EditorState& editor, size_t line, size_t column) -> void {
        move_cursor_to(editor, {line, column}, false);
    }

    auto set_editor_cursor(EditorState& editor, size_t line, size_t column) -> void {
        clear_extra_cursors(editor);
        set_cursor(editor, line, column);
    }

    auto move_vertical(EditorState& editor, int32_t delta, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_vertical(editor, delta, select);
            return;
        }
        size_t visible = editor_visible_line_index(editor, editor.cursor_line);
        if (delta < 0) {
            size_t const amount = static_cast<size_t>(-delta);
            visible = amount < visible ? visible - amount : 0u;
        } else {
            visible = std::min(
                visible + static_cast<size_t>(delta), editor_visible_line_count(editor) - 1u
            );
        }
        size_t const line = editor_visible_line_at(editor, visible);
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

    struct EditorCursorEdit {
        size_t offset = 0u;
        size_t anchor_offset = 0u;
        size_t preferred_column = 0u;
        bool primary = false;
    };

    [[nodiscard]] auto has_extra_cursors(EditorState const& editor) -> bool {
        return !editor.extra_cursors.empty();
    }

    [[nodiscard]] auto cursor_edit_count(EditorState const& editor) -> size_t {
        return editor.extra_cursors.size() + 1u;
    }

    [[nodiscard]] auto collect_cursors(EditorState const& editor, EditorCursorEdit* cursors)
        -> size_t {
        size_t count = 1u;
        cursors[0u] = {
            .offset = position_offset(editor, cursor_position(editor)),
            .anchor_offset = position_offset(editor, selection_anchor(editor)),
            .preferred_column = editor.preferred_column,
            .primary = true,
        };
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            EditorCursor const cursor = editor.extra_cursors[index];
            EditorPosition const position = clamp_position(editor, {cursor.line, cursor.column});
            size_t const offset = position_offset(editor, position);
            bool duplicate = cursors[0u].offset == offset;
            for (size_t other = 1u; other < count && !duplicate; ++other) {
                duplicate = cursors[other].offset == offset;
            }
            if (!duplicate) {
                cursors[count] = {
                    .offset = offset,
                    .anchor_offset = position_offset(
                        editor, {cursor.selection_anchor_line, cursor.selection_anchor_column}
                    ),
                    .preferred_column = cursor.preferred_column,
                };
                count += 1u;
            }
        }
        return count;
    }

    auto sort_cursors_by_offset(EditorCursorEdit* cursors, size_t count, bool descending) -> void {
        std::sort(
            cursors, cursors + count, [descending](EditorCursorEdit lhs, EditorCursorEdit rhs) {
                return descending ? lhs.offset > rhs.offset : lhs.offset < rhs.offset;
            }
        );
    }

    [[nodiscard]] auto selection_active(EditorCursorEdit const* cursors, size_t count) -> bool {
        for (size_t index = 0u; index < count; ++index) {
            if (cursors[index].offset != cursors[index].anchor_offset) {
                return true;
            }
        }
        return false;
    }

    auto store_cursors(
        EditorState& editor, EditorCursorEdit* cursors, size_t count, bool clear_selection_value
    ) -> void {
        sort_cursors_by_offset(cursors, count, false);
        size_t primary = 0u;
        for (size_t index = 0u; index < count; ++index) {
            if (cursors[index].primary) {
                primary = index;
                break;
            }
        }

        EditorTextPosition const primary_position =
            text_buffer_offset_to_position(editor.text, cursors[primary].offset);
        editor.cursor_line = primary_position.line;
        editor.cursor_column = primary_position.column;
        editor.preferred_column = cursors[primary].preferred_column;
        EditorTextPosition const primary_anchor =
            clear_selection_value
                ? primary_position
                : text_buffer_offset_to_position(editor.text, cursors[primary].anchor_offset);
        editor.selection_anchor_line = primary_anchor.line;
        editor.selection_anchor_column = primary_anchor.column;
        editor.extra_cursors.clear();
        bool const reserve_ok = editor.extra_cursors.reserve(count - 1u);
        DEBUG_ASSERT(reserve_ok);
        (void)reserve_ok;
        for (size_t index = 0u; index < count; ++index) {
            if (index == primary) {
                continue;
            }
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            if (position.line == editor.cursor_line && position.column == editor.cursor_column) {
                continue;
            }
            EditorTextPosition const anchor =
                clear_selection_value
                    ? position
                    : text_buffer_offset_to_position(editor.text, cursors[index].anchor_offset);
            bool const push_ok = editor.extra_cursors.push_back({
                .line = position.line,
                .column = position.column,
                .preferred_column = cursors[index].preferred_column,
                .selection_anchor_line = anchor.line,
                .selection_anchor_column = anchor.column,
            });
            DEBUG_ASSERT(push_ok);
            (void)push_ok;
        }
        if (clear_selection_value) {
            clear_selection(editor);
        }
        clamp_cursor(editor);
        if (!clear_selection_value) {
            editor.set_flag(
                EditorFlag::SELECTION_ACTIVE,
                editor.selection_mode != EditorSelectionMode::NONE ||
                    selection_active(cursors, count)
            );
        }
    }

    auto move_all_cursors_by(
        EditorState& editor, EditorPosition (*move)(EditorState const&, EditorPosition), bool select
    ) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const text_position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            EditorPosition const position =
                move(editor, {text_position.line, text_position.column});
            cursors[index].offset = position_offset(editor, position);
            cursors[index].preferred_column = position.column;
        }
        store_cursors(editor, cursors, count, !select);
    }

    auto move_all_cursors_vertical(EditorState& editor, int32_t delta, bool select) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const text_position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            size_t line = text_position.line;
            if (delta < 0) {
                size_t const amount = static_cast<size_t>(-delta);
                line = amount < line ? line - amount : 0u;
            } else {
                line = std::min(line + static_cast<size_t>(delta), editor_line_count(editor) - 1u);
            }
            size_t const column =
                std::min(cursors[index].preferred_column, line_size(editor, line));
            cursors[index].offset = position_offset(editor, {line, column});
        }
        store_cursors(editor, cursors, count, !select);
    }

    auto move_all_cursors_line_start(EditorState& editor, bool select) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            cursors[index].offset = position_offset(editor, {position.line, 0u});
            cursors[index].preferred_column = 0u;
        }
        store_cursors(editor, cursors, count, !select);
    }

    auto move_all_cursors_line_end(EditorState& editor, bool select) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            size_t const column = line_size(editor, position.line);
            cursors[index].offset = position_offset(editor, {position.line, column});
            cursors[index].preferred_column = column;
        }
        store_cursors(editor, cursors, count, !select);
    }

    auto move_left(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, previous_position, select);
            return;
        }
        move_cursor_to(editor, previous_position(editor, cursor_position(editor)), select);
    }

    auto move_right(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, next_position, select);
            return;
        }
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
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, previous_word_position, select);
            return;
        }
        move_cursor_to(editor, previous_word_position(editor, cursor_position(editor)), select);
    }

    auto move_word_right(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, next_word_position, select);
            return;
        }
        move_cursor_to(editor, next_word_position(editor, cursor_position(editor)), select);
    }

    auto move_word_end(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, next_word_end_position, select);
            return;
        }
        move_cursor_to(editor, next_word_end_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_left(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, previous_big_word_position, select);
            return;
        }
        move_cursor_to(editor, previous_big_word_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_right(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, next_big_word_position, select);
            return;
        }
        move_cursor_to(editor, next_big_word_position(editor, cursor_position(editor)), select);
    }

    auto move_big_word_end(EditorState& editor, bool select) -> void {
        if (has_extra_cursors(editor)) {
            move_all_cursors_by(editor, next_big_word_end_position, select);
            return;
        }
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

    [[nodiscard]] auto erase_all_selections(EditorState& editor) -> bool;

    [[nodiscard]] auto erase_selection(EditorState& editor) -> bool {
        if (has_extra_cursors(editor) && editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            return erase_all_selections(editor);
        }
        EditorSelectionRange const selection = editor_selection_range(editor);
        if (!selection.active) {
            return false;
        }
        clear_extra_cursors(editor);
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
        clear_extra_cursors(editor);
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

    auto adjust_inserted_offsets(
        EditorCursorEdit* cursors, size_t processed_count, size_t offset, size_t size
    ) -> void {
        for (size_t index = 0u; index < processed_count; ++index) {
            if (cursors[index].offset >= offset) {
                cursors[index].offset += size;
            }
        }
    }

    auto adjust_erased_offsets(
        EditorCursorEdit* cursors, size_t processed_count, size_t start, size_t end
    ) -> void {
        size_t const size = end - start;
        for (size_t index = 0u; index < processed_count; ++index) {
            if (cursors[index].offset > start) {
                cursors[index].offset =
                    cursors[index].offset < end ? start : cursors[index].offset - size;
            }
        }
    }

    auto refresh_cursor_columns(EditorState const& editor, EditorCursorEdit* cursors, size_t count)
        -> void {
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            cursors[index].preferred_column = position.column;
        }
    }

    struct SelectionEdit {
        size_t start = 0u;
        size_t end = 0u;
        size_t anchor = 0u;
        size_t preferred_column = 0u;
        bool primary = false;
    };

    auto add_selection_edit(
        EditorState const& editor,
        SelectionEdit* edits,
        size_t& count,
        EditorSelectionRange selection,
        size_t preferred_column,
        bool primary
    ) -> void {
        if (!selection.active) {
            return;
        }
        edits[count] = {
            .start = position_offset(editor, {selection.start_line, selection.start_column}),
            .end = position_offset(editor, {selection.end_line, selection.end_column}),
            .anchor = position_offset(editor, {selection.start_line, selection.start_column}),
            .preferred_column = preferred_column,
            .primary = primary,
        };
        count += 1u;
    }

    [[nodiscard]] auto collect_selection_edits(EditorState const& editor, SelectionEdit* edits)
        -> size_t {
        size_t count = 0u;
        add_selection_edit(
            editor, edits, count, editor_selection_range(editor), editor.cursor_column, true
        );
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            add_selection_edit(
                editor,
                edits,
                count,
                editor_extra_selection_range(editor, index),
                editor.extra_cursors[index].column,
                false
            );
        }
        return count;
    }

    auto sort_selection_edits_descending(SelectionEdit* edits, size_t count) -> void {
        std::sort(edits, edits + count, [](SelectionEdit lhs, SelectionEdit rhs) {
            return lhs.start > rhs.start;
        });
    }

    auto selection_edits_to_cursors(
        EditorState const& editor, SelectionEdit const* edits, size_t count, EditorCursorEdit* out
    ) -> void {
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, edits[index].start);
            out[index] = {
                .offset = edits[index].start,
                .anchor_offset = edits[index].start,
                .preferred_column = position.column,
                .primary = edits[index].primary,
            };
        }
    }

    auto replace_all_selections_with_text(EditorState& editor, StrRef text) -> bool {
        ArenaTemp temp = begin_thread_temp_arena();
        SelectionEdit* const edits =
            arena_alloc<SelectionEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_selection_edits(editor, edits);
        if (count == 0u) {
            return false;
        }

        sort_selection_edits_descending(edits, count);
        size_t limit = text_buffer_size(editor.text);
        for (size_t index = 0u; index < count; ++index) {
            edits[index].end = std::min(edits[index].end, limit);
            if (edits[index].start < edits[index].end) {
                text_buffer_erase(editor.text, edits[index].start, edits[index].end);
                for (size_t previous = 0u; previous < index; ++previous) {
                    if (edits[previous].start > edits[index].start) {
                        edits[previous].start -= edits[index].end - edits[index].start;
                    }
                }
            }
            text_buffer_insert(editor.text, edits[index].start, text);
            for (size_t previous = 0u; previous < index; ++previous) {
                if (edits[previous].start >= edits[index].start) {
                    edits[previous].start += text.size();
                }
            }
            edits[index].start += text.size();
            limit = edits[index].start - text.size();
        }

        EditorCursorEdit* const cursors = arena_alloc<EditorCursorEdit>(*temp.arena(), count);
        selection_edits_to_cursors(editor, edits, count, cursors);
        store_cursors(editor, cursors, count, true);
        return true;
    }

    [[nodiscard]] auto erase_all_selections(EditorState& editor) -> bool {
        return replace_all_selections_with_text(editor, {});
    }

    auto insert_text_at_all_cursors(EditorState& editor, StrRef text) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        sort_cursors_by_offset(cursors, count, true);
        for (size_t index = 0u; index < count; ++index) {
            size_t const offset = cursors[index].offset;
            text_buffer_insert(editor.text, offset, text);
            adjust_inserted_offsets(cursors, index, offset, text.size());
            cursors[index].offset = offset + text.size();
        }
        refresh_cursor_columns(editor, cursors, count);
        store_cursors(editor, cursors, count, true);
    }

    auto insert_newline_at_all_cursors(EditorState& editor) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        sort_cursors_by_offset(cursors, count, true);
        for (size_t index = 0u; index < count; ++index) {
            size_t const offset = cursors[index].offset;
            text_buffer_insert(editor.text, offset, "\n");
            adjust_inserted_offsets(cursors, index, offset, 1u);
            cursors[index].offset = offset + 1u;
        }
        refresh_cursor_columns(editor, cursors, count);
        store_cursors(editor, cursors, count, true);
    }

    auto backspace_at_all_cursors(EditorState& editor) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        sort_cursors_by_offset(cursors, count, true);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            EditorPosition const end = {position.line, position.column};
            EditorPosition const start = previous_position(editor, end);
            if (same_position(start, end)) {
                continue;
            }
            size_t const start_offset = position_offset(editor, start);
            size_t const end_offset = position_offset(editor, end);
            text_buffer_erase(editor.text, start_offset, end_offset);
            adjust_erased_offsets(cursors, index, start_offset, end_offset);
            cursors[index].offset = start_offset;
        }
        refresh_cursor_columns(editor, cursors, count);
        store_cursors(editor, cursors, count, true);
    }

    auto delete_char_at_all_cursors(EditorState& editor) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor));
        size_t const count = collect_cursors(editor, cursors);
        sort_cursors_by_offset(cursors, count, true);
        for (size_t index = 0u; index < count; ++index) {
            EditorTextPosition const position =
                text_buffer_offset_to_position(editor.text, cursors[index].offset);
            EditorPosition const start = {position.line, position.column};
            EditorPosition const end = next_position(editor, start);
            if (same_position(start, end)) {
                continue;
            }
            size_t const start_offset = position_offset(editor, start);
            size_t const end_offset = position_offset(editor, end);
            text_buffer_erase(editor.text, start_offset, end_offset);
            adjust_erased_offsets(cursors, index, start_offset, end_offset);
            cursors[index].offset = start_offset;
        }
        refresh_cursor_columns(editor, cursors, count);
        store_cursors(editor, cursors, count, true);
    }

    auto insert_char(EditorState& editor, char value) -> void {
        if (has_extra_cursors(editor) && editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            replace_all_selections_with_text(editor, StrRef(&value, 1u));
            return;
        }
        if (has_extra_cursors(editor) && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            insert_text_at_all_cursors(editor, StrRef(&value, 1u));
            return;
        }
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
        if (has_extra_cursors(editor) && editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            replace_all_selections_with_text(editor, "\n");
            return;
        }
        if (has_extra_cursors(editor) && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            insert_newline_at_all_cursors(editor);
            return;
        }
        (void)erase_selection(editor);
        text_buffer_insert(editor.text, position_offset(editor, cursor_position(editor)), "\n");
        set_cursor(editor, editor.cursor_line + 1u, 0u);
    }

    auto backspace(EditorState& editor) -> void {
        if (has_extra_cursors(editor) && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            backspace_at_all_cursors(editor);
            return;
        }
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
        if (has_extra_cursors(editor) && !editor.flag(EditorFlag::SELECTION_ACTIVE)) {
            delete_char_at_all_cursors(editor);
            return;
        }
        if (erase_selection(editor)) {
            return;
        }
        size_t const size = line_size(editor, editor.cursor_line);
        if (editor.cursor_column > size ||
            (editor.cursor_column == size &&
             !line_has_trailing_newline(editor, editor.cursor_line))) {
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
        if (has_extra_cursors(editor)) {
            clear_extra_cursors(editor);
        }
        if (erase_selection(editor)) {
            return;
        }
        erase_range(
            editor, previous_word_position(editor, cursor_position(editor)), cursor_position(editor)
        );
    }

    auto delete_word(EditorState& editor) -> void {
        if (has_extra_cursors(editor)) {
            clear_extra_cursors(editor);
        }
        if (erase_selection(editor)) {
            return;
        }
        erase_range(
            editor, cursor_position(editor), next_word_position(editor, cursor_position(editor))
        );
    }

    auto delete_line(EditorState& editor) -> void {
        clear_extra_cursors(editor);
        delete_line_at(editor, editor.cursor_line);
        clamp_cursor(editor);
        clear_selection(editor);
    }

    auto open_line(EditorState& editor, size_t line) -> void {
        clear_extra_cursors(editor);
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

    auto toggle_raster_policy(EditorState& editor) -> void {
        editor.raster_policy =
            editor.raster_policy == gui::font_provider::RasterPolicy::SHARP_HINTED
                ? gui::font_provider::RasterPolicy::SMOOTH_HINTED
                : gui::font_provider::RasterPolicy::SHARP_HINTED;
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

    [[nodiscard]] auto lsp_completion_text_trigger(char ch) -> bool {
        return is_ascii_alphanumeric(ch) || ch == '_' || ch == '.' || ch == '>' || ch == ':';
    }

    [[nodiscard]] auto editor_file_search_text(EditorState const& editor) -> StrRef {
        return StrRef(editor.file_search_text, editor.file_search_text_size);
    }

    [[nodiscard]] auto editor_command_text(EditorState const& editor) -> StrRef {
        return StrRef(editor.command_text, editor.command_text_size);
    }

    [[nodiscard]] auto editor_text_search_text(EditorState const& editor) -> StrRef {
        return StrRef(editor.text_search_text, editor.text_search_text_size);
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

    struct ParsedCommandLine {
        StrRef name = {};
        StrRef args = {};
    };

    [[nodiscard]] auto parse_command_line(StrRef text) -> ParsedCommandLine {
        text = text.trim();
        size_t const separator = text.find_first_of(" \t");
        if (separator == StrRef::NPOS) {
            return {.name = text};
        }
        return {
            .name = text.prefix(separator),
            .args = text.substr(separator + 1u).trim(),
        };
    }

    [[nodiscard]] auto file_search_entry_text(FileTreeEntry const& entry) -> StrRef {
        return !entry.relative_path.empty() ? entry.relative_path : entry.name;
    }

    [[nodiscard]] auto file_search_path_separator(char value) -> bool {
        return value == '\\' || value == '/';
    }

    [[nodiscard]] auto file_search_boundary(char value) -> bool {
        return file_search_path_separator(value) || value == '_' || value == '-' || value == '.' ||
               is_ascii_whitespace(value);
    }

    [[nodiscard]] auto file_search_char_equal(char lhs, char rhs) -> bool {
        if (file_search_path_separator(lhs) && file_search_path_separator(rhs)) {
            return true;
        }
        return to_ascii_lower(lhs) == to_ascii_lower(rhs);
    }

    [[nodiscard]] auto file_search_query_is_path_like(StrRef query) -> bool {
        if (query.find_first_of("\\/") != StrRef::NPOS) {
            return true;
        }
        size_t part_count = 0u;
        for (StrRef const part : query.split_ascii_whitespace()) {
            BASE_UNUSED(part);
            part_count += 1u;
            if (part_count > 1u) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto file_search_name_prefix_match(StrRef name, StrRef query) -> bool {
        return query.size() <= name.size() &&
               name.prefix(query.size()).equals_ignore_ascii_case(query);
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
        size_t query_size = 0u;
        for (StrRef const part : query.split_ascii_whitespace()) {
            query_size += part.size();
            for (char query_ch : part) {
                bool found = false;
                while (text_index < text.size()) {
                    if (file_search_char_equal(text[text_index], query_ch)) {
                        size_t const gap =
                            previous == StrRef::NPOS ? text_index : text_index - previous - 1u;
                        score += static_cast<int32_t>(gap * 4u);
                        if (text_index == 0u || file_search_boundary(text[text_index - 1u])) {
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
        }

        if (query_size == 0u) {
            out_score = 0;
            return true;
        }

        score += static_cast<int32_t>(text.size() - query_size);
        out_score = score;
        return true;
    }

    [[nodiscard]] auto
    file_search_folder_score(StrRef path, StrRef query, bool exact, int32_t& out_score) -> bool {
        bool matched = false;
        size_t start = 0u;
        while (start < path.size()) {
            size_t const slash = path.find_first_of("\\/", start);
            if (slash == StrRef::NPOS) {
                break;
            }

            StrRef const folder = path.substr(start, slash - start);
            int32_t score = 0;
            bool const found = exact ? folder.equals_ignore_ascii_case(query)
                                     : file_search_fuzzy_score(folder, query, score);
            if (found && (!matched || score < out_score)) {
                out_score = score;
                matched = true;
            }
            start = slash + 1u;
        }
        return matched;
    }

    [[nodiscard]] auto
    file_search_match(FileTreeEntry const& entry, StrRef query, FileSearchMatch& match) -> bool {
        StrRef const path = file_search_entry_text(entry);
        StrRef const name = !entry.name.empty() ? entry.name : path;
        bool const path_like = file_search_query_is_path_like(query);
        if (!query.empty() && file_search_name_prefix_match(name, query)) {
            match.priority = 0u;
            match.score = 0;
            return true;
        }
        if (path_like && file_search_fuzzy_score(path, query, match.score)) {
            match.priority = 1u;
            return true;
        }
        if (file_search_fuzzy_score(name, query, match.score)) {
            match.priority = path_like ? 2u : 1u;
            return true;
        }
        if (!path_like && !query.empty() &&
            file_search_folder_score(path, query, true, match.score)) {
            match.priority = 2u;
            return true;
        }
        if (!path_like && file_search_folder_score(path, query, false, match.score)) {
            match.priority = 3u;
            return true;
        }
        return false;
    }

    [[nodiscard]] auto
    file_search_match_less(EditorState const& editor, FileSearchMatch lhs, FileSearchMatch rhs)
        -> bool {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        FileTreeEntry const& lhs_entry = editor.tree_files[lhs.tree_file_index];
        FileTreeEntry const& rhs_entry = editor.tree_files[rhs.tree_file_index];
        if (lhs_entry.depth != rhs_entry.depth) {
            return lhs_entry.depth < rhs_entry.depth;
        }
        if (lhs.score != rhs.score) {
            return lhs.score < rhs.score;
        }
        StrRef const lhs_text = file_search_entry_text(lhs_entry);
        StrRef const rhs_text = file_search_entry_text(rhs_entry);
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
            if (entry.is_directory || !entry.file_search_visible) {
                continue;
            }

            FileSearchMatch match = {.tree_file_index = index};
            if (!file_search_match(entry, query, match)) {
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

    [[nodiscard]] auto
    buffer_search_match(OpenFile const& file, StrRef query, BufferSearchMatch& match) -> bool {
        StrRef const name = buffer_search_entry_text(file);
        int32_t best_score = 0;
        bool matched = false;
        if (file_search_query_is_path_like(query) && !file.path.empty() &&
            file_search_fuzzy_score(file.path, query, best_score)) {
            matched = true;
        }
        int32_t name_score = 0;
        if (file_search_fuzzy_score(name, query, name_score) &&
            (!matched || name_score < best_score)) {
            best_score = name_score;
            matched = true;
        }
        match.score = best_score;
        return matched;
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
            if (!buffer_search_match(editor.open_files[index], query, match)) {
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

    [[nodiscard]] auto
    jump_list_match_text(size_t index, StrRef name, StrRef path, StrRef query, JumpListMatch& match)
        -> bool {
        match = {.jump_index = index};
        if (query.empty()) {
            return true;
        }
        char number_buffer[32] = {};
        auto const number_end =
            std::to_chars(number_buffer, number_buffer + sizeof(number_buffer), index + 1u).ptr;
        StrRef const number(number_buffer, number_end);
        if (file_search_fuzzy_score(number, query, match.score)) {
            match.priority = 0u;
            return true;
        }

        int32_t best_score = 0;
        bool matched = false;
        if (file_search_fuzzy_score(name, query, best_score)) {
            matched = true;
        }
        int32_t path_score = 0;
        if (!path.empty() && file_search_fuzzy_score(path, query, path_score) &&
            (!matched || path_score < best_score)) {
            best_score = path_score;
            matched = true;
        }
        match.score = best_score;
        match.priority = 1u;
        return matched;
    }

    [[nodiscard]] auto
    jump_list_match(size_t index, EditorJump const& jump, StrRef query, JumpListMatch& match)
        -> bool {
        return jump_list_match_text(index, jump.name, jump.path, query, match);
    }

    [[nodiscard]] auto
    jump_list_match(size_t index, LspLocation const& location, StrRef query, JumpListMatch& match)
        -> bool {
        return jump_list_match_text(index, location.path, {}, query, match);
    }

    [[nodiscard]] auto jump_list_match(
        size_t index, LspDocumentSymbol const& symbol, StrRef query, JumpListMatch& match
    ) -> bool {
        match = {.jump_index = index};
        if (query.empty()) {
            return true;
        }

        int32_t best_score = 0;
        bool matched = false;
        if (file_search_fuzzy_score(symbol.name, query, best_score)) {
            matched = true;
        }
        int32_t detail_score = 0;
        if (!symbol.detail.empty() && file_search_fuzzy_score(symbol.detail, query, detail_score) &&
            (!matched || detail_score < best_score)) {
            best_score = detail_score;
            matched = true;
        }
        int32_t path_score = 0;
        if (!symbol.path.empty() && file_search_fuzzy_score(symbol.path, query, path_score) &&
            (!matched || path_score < best_score)) {
            best_score = path_score;
            matched = true;
        }
        match.score = best_score;
        return matched;
    }

    [[nodiscard]] auto jump_list_match(
        size_t index, LspDiagnostic const& diagnostic, StrRef query, JumpListMatch& match
    ) -> bool {
        return jump_list_match_text(index, diagnostic.message, diagnostic.path, query, match);
    }

    [[nodiscard]] auto jump_list_match_less(JumpListMatch lhs, JumpListMatch rhs) -> bool {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        if (lhs.score != rhs.score) {
            return lhs.score < rhs.score;
        }
        return lhs.jump_index < rhs.jump_index;
    }

    [[nodiscard]] auto jump_list_is_diagnostics(EditorJumpListKind kind) -> bool {
        return kind == EditorJumpListKind::LSP_FILE_DIAGNOSTICS ||
               kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS;
    }

    [[nodiscard]] auto
    jump_list_diagnostic_visible(EditorState const& editor, LspDiagnostic const& diagnostic)
        -> bool {
        return editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS ||
               diagnostic.path.equals_ignore_ascii_case(editor.current_file_path);
    }

    [[nodiscard]] auto jump_list_diagnostic_index(EditorState const& editor, size_t source_index)
        -> size_t {
        DEBUG_ASSERT(editor.lsp_bridge != nullptr);
        if (editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS) {
            return source_index;
        }
        size_t visible = 0u;
        for (size_t index = 0u; index < editor.lsp_bridge->diagnostics.size(); ++index) {
            if (!jump_list_diagnostic_visible(editor, editor.lsp_bridge->diagnostics[index])) {
                continue;
            }
            if (visible == source_index) {
                return index;
            }
            visible += 1u;
        }
        return LSP_NO_SELECTION;
    }

    [[nodiscard]] auto jump_list_source_count(EditorState const& editor) -> size_t {
        if (editor.jump_list_kind == EditorJumpListKind::LSP_LOCATIONS) {
            return editor.lsp_bridge != nullptr ? editor.lsp_bridge->locations.size() : 0u;
        }
        if (editor.jump_list_kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
            editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
            return editor.lsp_bridge != nullptr ? editor.lsp_bridge->symbols.size() : 0u;
        }
        if (jump_list_is_diagnostics(editor.jump_list_kind)) {
            if (editor.lsp_bridge == nullptr) {
                return 0u;
            }
            size_t count = 0u;
            for (LspDiagnostic const& diagnostic : editor.lsp_bridge->diagnostics) {
                if (jump_list_diagnostic_visible(editor, diagnostic)) {
                    count += 1u;
                }
            }
            return count;
        }
        if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
            return editor.global_search_results.size();
        }
        return editor.jumps.size();
    }

    [[nodiscard]] auto
    jump_list_source_match(EditorState const& editor, size_t index, JumpListMatch& match) -> bool {
        StrRef const query = editor_file_search_text(editor);
        if (editor.jump_list_kind == EditorJumpListKind::LSP_LOCATIONS) {
            DEBUG_ASSERT(editor.lsp_bridge != nullptr);
            return jump_list_match(index, editor.lsp_bridge->locations[index], query, match);
        }
        if (editor.jump_list_kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
            editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
            DEBUG_ASSERT(editor.lsp_bridge != nullptr);
            return jump_list_match(index, editor.lsp_bridge->symbols[index], query, match);
        }
        if (jump_list_is_diagnostics(editor.jump_list_kind)) {
            DEBUG_ASSERT(editor.lsp_bridge != nullptr);
            size_t const diagnostic_index = jump_list_diagnostic_index(editor, index);
            if (diagnostic_index == LSP_NO_SELECTION) {
                return false;
            }
            return jump_list_match(
                diagnostic_index, editor.lsp_bridge->diagnostics[diagnostic_index], query, match
            );
        }
        if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
            match = {.jump_index = index};
            return true;
        }
        return jump_list_match(index, editor.jumps[index], query, match);
    }

    [[nodiscard]] auto
    collect_jump_list_matches(EditorState const& editor, Slice<JumpListMatch> matches) -> size_t {
        size_t count = 0u;
        size_t const source_count = jump_list_source_count(editor);
        for (size_t index = 0u; index < source_count; ++index) {
            JumpListMatch match = {};
            if (!jump_list_source_match(editor, index, match)) {
                continue;
            }

            if (count < matches.size()) {
                size_t insert = count;
                while (insert > 0u && jump_list_match_less(match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
                count += 1u;
            } else if (!matches.empty() && jump_list_match_less(match, matches.back())) {
                size_t insert = matches.size() - 1u;
                while (insert > 0u && jump_list_match_less(match, matches[insert - 1u])) {
                    matches[insert] = matches[insert - 1u];
                    insert -= 1u;
                }
                matches[insert] = match;
            }
        }
        return count;
    }

    [[nodiscard]] auto file_search_total_count(EditorState const& editor, bool buffers) -> size_t {
        if (buffers) {
            return editor.open_files.size();
        }
        size_t count = 0u;
        for (FileTreeEntry const& entry : editor.tree_files) {
            if (!entry.is_directory && entry.file_search_visible) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto file_search_filtered_count(EditorState const& editor, bool buffers)
        -> size_t {
        StrRef const query = editor_file_search_text(editor);
        size_t count = 0u;
        if (buffers) {
            for (OpenFile const& file : editor.open_files) {
                BufferSearchMatch match = {};
                if (buffer_search_match(file, query, match)) {
                    count += 1u;
                }
            }
            return count;
        }

        for (FileTreeEntry const& entry : editor.tree_files) {
            if (entry.is_directory || !entry.file_search_visible) {
                continue;
            }
            FileSearchMatch match = {};
            if (file_search_match(entry, query, match)) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto jump_list_total_count(EditorState const& editor) -> size_t {
        return jump_list_source_count(editor);
    }

    [[nodiscard]] auto jump_list_filtered_count(EditorState const& editor) -> size_t {
        size_t count = 0u;
        size_t const source_count = jump_list_source_count(editor);
        for (size_t index = 0u; index < source_count; ++index) {
            JumpListMatch match = {};
            if (jump_list_source_match(editor, index, match)) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto search_match_count(EditorState const& editor, bool buffers) -> size_t {
        return file_search_filtered_count(editor, buffers);
    }

    auto clamp_search_selected(EditorState& editor, bool buffers) -> void {
        size_t const count = search_match_count(editor, buffers);
        editor.file_search_selected =
            count == 0u ? 0u : std::min(editor.file_search_selected, count - 1u);
    }

    auto open_file_search(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, true);
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        close_jump_list(editor);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.file_search_selected = 0u;
        editor.file_search_mouse_known = false;
        editor.file_search_mouse_select = false;
        editor.file_search_reveal_selected = true;
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
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
        editor.file_search_mouse_select = false;
    }

    auto open_buffer_search(EditorState& editor) -> void {
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, true);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        close_jump_list(editor);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.file_search_selected = 0u;
        editor.file_search_mouse_known = false;
        editor.file_search_mouse_select = false;
        editor.file_search_reveal_selected = true;
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
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
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
        editor.file_search_mouse_select = false;
    }

    auto open_jump_list_picker(EditorState& editor, EditorJumpListKind kind) -> void {
        editor.jump_list_kind = kind;
        editor.set_flag(EditorFlag::JUMP_LIST_OPEN, true);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.file_search_text_size = 0u;
        editor.file_search_text[0u] = '\0';
        editor.jump_selected = 0u;
        editor.jump_list_mouse_known = false;
        editor.jump_list_mouse_select = false;
        editor.jump_list_reveal_selected = true;
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        editor.global_search_open_index = JUMP_LIST_NO_SELECTION;
        editor.lsp_open_diagnostic_index = LSP_NO_SELECTION;
        editor.global_search_refresh_requested = false;
        if (kind == EditorJumpListKind::GLOBAL_SEARCH) {
            editor.global_search_results.clear();
            editor.global_search_refresh_requested = true;
        }
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
        close_editor_lsp_popup(editor);
    }

    auto open_editor_jump_list(EditorState& editor) -> void {
        open_jump_list_picker(editor, EditorJumpListKind::HISTORY);
        editor.jump_selected =
            editor.jump_cursor != JUMP_LIST_NO_SELECTION ? editor.jump_cursor : 0u;
        editor.jump_selected =
            editor.jumps.empty() ? 0u : std::min(editor.jump_selected, editor.jumps.size() - 1u);
    }

    auto open_editor_global_search(EditorState& editor) -> void {
        set_text_search_text(editor, {});
        clear_selection(editor);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, true);
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        close_jump_list(editor);
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

    auto close_jump_list(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::JUMP_LIST_OPEN, false);
        editor.jump_selected = 0u;
        editor.jump_list_mouse_select = false;
        editor.jump_list_kind = EditorJumpListKind::HISTORY;
    }

    auto clear_config_request(EditorState& editor) -> void {
        editor.config_request = EditorConfigRequestKind::NONE;
        editor.config_request_text_size = 0u;
        editor.config_request_text[0u] = '\0';
    }

    auto clear_command_line(EditorState& editor) -> void {
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.command_text_size = 0u;
        editor.command_selected = 0u;
        editor.command_text[0u] = '\0';
    }

    auto select_command_match(EditorState& editor) -> void {
        ParsedCommandLine const command = parse_command_line(editor_command_text(editor));
        if (command.name.empty()) {
            editor.command_selected =
                std::min(editor.command_selected, editor_command_count() - 1u);
            return;
        }
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const candidate = editor_command(index);
            if (candidate.name.starts_with_ignore_ascii_case(command.name) ||
                candidate.alias.starts_with_ignore_ascii_case(command.name)) {
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
        ParsedCommandLine const command = parse_command_line(editor_command_text(editor));
        if (!command.args.empty()) {
            return;
        }
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
        close_jump_list(editor);
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
    }

    struct TextSearchMatch {
        EditorPosition start = {};
        EditorPosition end = {};
    };

    [[nodiscard]] auto direct_line_match_at(StrRef line, StrRef query, size_t start) -> bool {
        if (query.size() > line.size() - start) {
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
    direct_line_match(StrRef line, StrRef query, size_t column, size_t& out_start) -> bool {
        if (query.empty() || query.size() > line.size()) {
            return false;
        }
        size_t const last_start = line.size() - query.size();
        if (column > last_start) {
            return false;
        }
        char const first = to_ascii_lower(query[0u]);
        for (size_t start = column; start <= last_start; ++start) {
            if (to_ascii_lower(line[start]) == first && direct_line_match_at(line, query, start)) {
                out_start = start;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto
    direct_line_match_before(StrRef line, StrRef query, size_t column, size_t& out_start) -> bool {
        if (query.empty() || query.size() > line.size()) {
            return false;
        }
        size_t const end_column = std::min(column, line.size());
        if (end_column < query.size()) {
            return false;
        }
        char const first = to_ascii_lower(query[0u]);
        size_t start = end_column - query.size() + 1u;
        while (start != 0u) {
            start -= 1u;
            if (to_ascii_lower(line[start]) == first && direct_line_match_at(line, query, start)) {
                out_start = start;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto find_text_search_match(
        EditorState const& editor, EditorPosition origin, bool wrap, TextSearchMatch& match
    ) -> bool {
        StrRef const query = editor_text_search_text(editor);
        size_t const line_count = editor_line_count(editor);
        origin = clamp_position(editor, origin);
        size_t const step_count = wrap ? line_count : line_count - origin.line;
        for (size_t step = 0u; step < step_count; ++step) {
            size_t const line_index = (origin.line + step) % line_count;
            StrRef const line = editor_line_text(editor_line(editor, line_index));
            size_t const column = step == 0u ? origin.column : 0u;
            size_t start = 0u;
            if (direct_line_match(line, query, column, start)) {
                match = {{line_index, start}, {line_index, start + query.size()}};
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto find_previous_text_search_match(
        EditorState const& editor, EditorPosition origin, TextSearchMatch& match
    ) -> bool {
        StrRef const query = editor_text_search_text(editor);
        size_t const line_count = editor_line_count(editor);
        origin = clamp_position(editor, origin);
        for (size_t step = 0u; step < line_count; ++step) {
            size_t const line_index = (origin.line + line_count - step) % line_count;
            StrRef const line = editor_line_text(editor_line(editor, line_index));
            size_t const column = step == 0u ? origin.column : line.size();
            size_t start = 0u;
            if (direct_line_match_before(line, query, column, start)) {
                match = {{line_index, start}, {line_index, start + query.size()}};
                return true;
            }
        }
        return false;
    }

    auto select_text_search_match(EditorState& editor, TextSearchMatch match) -> void {
        editor.selection_anchor_line = match.start.line;
        editor.selection_anchor_column = match.start.column;
        editor.cursor_line = match.end.line;
        editor.cursor_column = match.end.column;
        editor.preferred_column = editor.cursor_column;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, true);
        if (split_valid(editor, editor.focused_split)) {
            center_cursor(editor, editor.split_nodes[editor.focused_split].rect);
        }
    }

    [[nodiscard]] auto update_text_search_selection(EditorState& editor) -> bool {
        if (editor_text_search_text(editor).empty()) {
            clear_selection(editor);
            return false;
        }
        TextSearchMatch match = {};
        if (!find_text_search_match(editor, {editor.text_search_origin_line, 0u}, false, match)) {
            clear_selection(editor);
            return false;
        }
        select_text_search_match(editor, match);
        return true;
    }

    auto set_text_search_text(EditorState& editor, StrRef text) -> void {
        editor.text_search_text_size =
            text.copy_to(editor.text_search_text, TEXT_SEARCH_TEXT_CAPACITY - 1u);
        editor.text_search_text[editor.text_search_text_size] = '\0';
    }

    auto open_text_search(EditorState& editor) -> void {
        if (focused_pane_kind(editor) != EditorPaneKind::CODE) {
            return;
        }
        set_text_search_text(editor, {});
        clear_selection(editor);
        editor.text_search_origin_line = editor.cursor_line;
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, true);
        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
        editor.set_flag(EditorFlag::COMMAND_LINE_ACTIVE, false);
        editor.set_flag(EditorFlag::FILE_SEARCH_OPEN, false);
        editor.file_search_open_file = FILE_SEARCH_NO_FILE;
        editor.set_flag(EditorFlag::BUFFER_SEARCH_OPEN, false);
        editor.buffer_search_open_file = FILE_SEARCH_NO_FILE;
        close_jump_list(editor);
        editor.set_flag(EditorFlag::SAVE_PATH_OPEN, false);
        editor.save_path_error = EditorSavePathError::NONE;
        editor.set_flag(EditorFlag::PENDING_LEADER, false);
        editor.set_flag(EditorFlag::PENDING_WINDOW, false);
        editor.set_flag(EditorFlag::PENDING_D, false);
        editor.set_flag(EditorFlag::PENDING_G, false);
        editor.set_flag(EditorFlag::PENDING_R, false);
        editor.set_flag(EditorFlag::PENDING_LSP, false);
        editor.set_flag(EditorFlag::PENDING_Z, false);
        editor.pending_line_number = 0u;
        editor.set_flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE, false);
    }

    auto finish_text_search(EditorState& editor) -> void {
        BASE_UNUSED(update_text_search_selection(editor));
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);
    }

    auto finish_global_search(EditorState& editor) -> void {
        StrRef const query = editor_text_search_text(editor);
        if (query.empty()) {
            editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
            return;
        }
        open_jump_list_picker(editor, EditorJumpListKind::GLOBAL_SEARCH);
        editor.file_search_text_size =
            query.copy_to(editor.file_search_text, FILE_SEARCH_TEXT_CAPACITY - 1u);
        editor.file_search_text[editor.file_search_text_size] = '\0';
        editor.global_search_refresh_requested = true;
    }

    auto submit_text_search(EditorState& editor, StrRef text) -> void {
        open_text_search(editor);
        set_text_search_text(editor, text);
        finish_text_search(editor);
    }

    auto repeat_text_search(EditorState& editor, bool reverse) -> void {
        if (editor_text_search_text(editor).empty()) {
            return;
        }
        EditorSelectionRange const selection = editor_selection_range(editor);
        EditorPosition const origin = selection.active
                                          ? EditorPosition{
                                                reverse ? selection.start_line : selection.end_line,
                                                reverse ? selection.start_column
                                                        : selection.end_column,
                                            }
                                          : cursor_position(editor);
        TextSearchMatch match = {};
        bool const found = reverse ? find_previous_text_search_match(editor, origin, match)
                                   : find_text_search_match(editor, origin, true, match);
        if (found) {
            select_text_search_match(editor, match);
        }
    }

    auto set_config_request(EditorState& editor, EditorConfigRequestKind kind, StrRef text = {})
        -> void {
        editor.config_request = kind;
        editor.config_request_text_size =
            text.copy_to(editor.config_request_text, COMMAND_TEXT_CAPACITY - 1u);
        editor.config_request_text[editor.config_request_text_size] = '\0';
    }

    auto run_editor_command(EditorState& editor, size_t index, StrRef args) -> void {
        switch (index) {
        case 0u:
            request_editor_save(editor);
            break;
        case 1u:
            close_focused_split(editor);
            break;
        case 2u:
            editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, true);
            break;
        case 3u:
            editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, true);
            break;
        case 4u:
            editor.set_flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED, true);
            editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, true);
            break;
        case 5u:
            editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, true);
            break;
        case 6u:
            open_file_search(editor);
            break;
        case 7u:
            toggle_filesystem_panel(editor);
            break;
        case 8u:
            request_lsp(editor, LspRequestKind::FORMATTING);
            break;
        case 9u:
            open_text_search(editor);
            break;
        case 10u:
            request_lsp(editor, LspRequestKind::DOCUMENT_SYMBOL);
            break;
        case 11u:
            toggle_current_fold(editor);
            break;
        case 12u:
            close_current_fold(editor);
            break;
        case 13u:
            open_current_fold(editor);
            break;
        case 14u:
            close_all_folds(editor);
            break;
        case 15u:
            open_all_folds(editor);
            break;
        case 16u:
            open_editor_jump_list(editor);
            break;
        case 17u:
            jump_list_previous(editor);
            break;
        case 18u:
            jump_list_next(editor);
            break;
        case 19u:
            toggle_raster_policy(editor);
            break;
        case 20u:
            open_editor_global_search(editor);
            break;
        case 21u:
            set_config_request(editor, EditorConfigRequestKind::OPEN);
            break;
        case 22u:
            set_config_request(editor, EditorConfigRequestKind::RELOAD);
            break;
        case 23u:
            set_config_request(editor, EditorConfigRequestKind::OVERRIDE, args);
            break;
        default:
            break;
        }
    }

    auto run_command_line(EditorState& editor) -> void {
        ParsedCommandLine const command_line = parse_command_line(editor_command_text(editor));
        if (command_line.name.empty()) {
            clear_command_line(editor);
            return;
        }
        for (size_t index = 0u; index < editor_command_count(); ++index) {
            EditorCommand const command = editor_command(index);
            if (command_line.name.equals_ignore_ascii_case(command.name) ||
                command_line.name.equals_ignore_ascii_case(command.alias)) {
                run_editor_command(editor, index, command_line.args);
                break;
            }
        }
        clear_command_line(editor);
    }

    auto select_file_search_match(EditorState& editor) -> void {
        size_t const count = file_search_filtered_count(editor, false);
        if (count == 0u) {
            return;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        FileSearchMatch* const matches = arena_alloc<FileSearchMatch>(*temp.arena(), count);
        size_t const match_count = collect_file_search_matches(editor, slice(matches, count));
        if (match_count == 0u) {
            return;
        }
        editor.file_search_selected = std::min(editor.file_search_selected, match_count - 1u);
        editor.file_search_open_file = matches[editor.file_search_selected].tree_file_index;
        expand_filesystem_tree_to_file(editor, editor.file_search_open_file);
        close_file_search(editor);
    }

    auto select_buffer_search_match(EditorState& editor) -> void {
        size_t const count = file_search_filtered_count(editor, true);
        if (count == 0u) {
            return;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        BufferSearchMatch* const matches = arena_alloc<BufferSearchMatch>(*temp.arena(), count);
        size_t const match_count = collect_buffer_search_matches(editor, slice(matches, count));
        if (match_count == 0u) {
            return;
        }
        editor.file_search_selected = std::min(editor.file_search_selected, match_count - 1u);
        editor.buffer_search_open_file = matches[editor.file_search_selected].open_file_index;
        close_buffer_search(editor);
    }

    auto select_jump_list_match(EditorState& editor) -> void {
        size_t const count = jump_list_filtered_count(editor);
        if (count == 0u) {
            return;
        }
        ArenaTemp temp = begin_thread_temp_arena();
        JumpListMatch* const matches = arena_alloc<JumpListMatch>(*temp.arena(), count);
        size_t const match_count = collect_jump_list_matches(editor, slice(matches, count));
        if (match_count == 0u) {
            return;
        }
        editor.jump_selected = std::min(editor.jump_selected, match_count - 1u);
        if (editor.jump_list_kind == EditorJumpListKind::LSP_LOCATIONS) {
            editor.lsp_open_location_index = matches[editor.jump_selected].jump_index;
        } else if (editor.jump_list_kind == EditorJumpListKind::LSP_DOCUMENT_SYMBOLS ||
                   editor.jump_list_kind == EditorJumpListKind::LSP_WORKSPACE_SYMBOLS) {
            editor.lsp_open_symbol_index = matches[editor.jump_selected].jump_index;
        } else if (jump_list_is_diagnostics(editor.jump_list_kind)) {
            editor.lsp_open_diagnostic_index = matches[editor.jump_selected].jump_index;
        } else if (editor.jump_list_kind == EditorJumpListKind::GLOBAL_SEARCH) {
            editor.global_search_open_index = matches[editor.jump_selected].jump_index;
        } else {
            editor.jump_open_index = matches[editor.jump_selected].jump_index;
        }
        close_jump_list(editor);
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
                editor.file_search_mouse_select = false;
                editor.file_search_reveal_selected = true;
            }
            break;
        case gui::Key::DOWN: {
            size_t const count = search_match_count(editor, buffers);
            if (editor.file_search_selected + 1u < count) {
                editor.file_search_selected += 1u;
                editor.file_search_mouse_select = false;
                editor.file_search_reveal_selected = true;
            }
            break;
        }
        default:
            break;
        }
    }

    auto handle_jump_list_event(EditorState& editor, gui::KeyEvent const& event) -> void {
        if (event.kind != gui::KeyEventKind::PRESS && event.kind != gui::KeyEventKind::REPEAT) {
            return;
        }
        switch (event.key) {
        case gui::Key::ESCAPE:
            close_jump_list(editor);
            break;
        case gui::Key::ENTER:
            select_jump_list_match(editor);
            break;
        case gui::Key::UP:
            if (editor.jump_selected != 0u) {
                editor.jump_selected -= 1u;
                editor.jump_list_mouse_select = false;
                editor.jump_list_reveal_selected = true;
            }
            break;
        case gui::Key::DOWN: {
            size_t const count = jump_list_filtered_count(editor);
            if (editor.jump_selected + 1u < count) {
                editor.jump_selected += 1u;
                editor.jump_list_mouse_select = false;
                editor.jump_list_reveal_selected = true;
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

    [[nodiscard]] auto tree_cursor_parent(EditorState const& editor) -> size_t {
        if (editor.tree_cursor >= editor.tree_files.size()) {
            return TREE_CURSOR_ROOT;
        }
        size_t const depth = editor.tree_files[editor.tree_cursor].depth;
        if (depth == 0u) {
            return TREE_CURSOR_ROOT;
        }
        for (size_t index = editor.tree_cursor; index > 0u;) {
            --index;
            FileTreeEntry const& entry = editor.tree_files[index];
            if (entry.is_directory && entry.depth < depth) {
                return index;
            }
        }
        return TREE_CURSOR_ROOT;
    }

    [[nodiscard]] auto first_visible_tree_entry(EditorState const& editor) -> size_t {
        for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
            if (tree_entry_visible(editor, index)) {
                return index;
            }
        }
        return TREE_CURSOR_ROOT;
    }

    [[nodiscard]] auto last_visible_tree_entry(EditorState const& editor) -> size_t {
        for (size_t index = editor.tree_files.size(); index > 0u;) {
            --index;
            if (tree_entry_visible(editor, index)) {
                return index;
            }
        }
        return TREE_CURSOR_ROOT;
    }

    auto move_filesystem_tree_cursor(EditorState& editor, int32_t direction) -> void {
        clamp_filesystem_tree_cursor(editor);
        if (direction > 0) {
            if (editor.tree_cursor == TREE_CURSOR_ROOT) {
                set_tree_cursor(editor, first_visible_tree_entry(editor));
                return;
            }
            for (size_t index = editor.tree_cursor + 1u; index < editor.tree_files.size();
                 ++index) {
                if (tree_entry_visible(editor, index)) {
                    set_tree_cursor(editor, index);
                    return;
                }
            }
            return;
        }
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            return;
        }
        for (size_t index = editor.tree_cursor; index > 0u;) {
            --index;
            if (tree_entry_visible(editor, index)) {
                set_tree_cursor(editor, index);
                return;
            }
        }
        set_tree_cursor(editor, TREE_CURSOR_ROOT);
    }

    auto cycle_filesystem_tree_cursor(EditorState& editor, int32_t direction) -> void {
        clamp_filesystem_tree_cursor(editor);
        size_t const before = editor.tree_cursor;
        move_filesystem_tree_cursor(editor, direction);
        if (editor.tree_cursor == before) {
            set_tree_cursor(
                editor, direction > 0 ? TREE_CURSOR_ROOT : last_visible_tree_entry(editor)
            );
        }
    }

    auto expand_filesystem_tree_cursor(EditorState& editor) -> void {
        clamp_filesystem_tree_cursor(editor);
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            editor.set_flag(EditorFlag::TREE_OPEN, true);
            editor.tree_cursor_reveal = true;
            return;
        }
        FileTreeEntry& entry = editor.tree_files[editor.tree_cursor];
        if (entry.is_directory) {
            entry.open = true;
            editor.tree_cursor_reveal = true;
        }
    }

    auto collapse_filesystem_tree_cursor(EditorState& editor, bool move_parent) -> void {
        clamp_filesystem_tree_cursor(editor);
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            editor.set_flag(EditorFlag::TREE_OPEN, false);
            editor.tree_cursor_reveal = true;
            return;
        }
        FileTreeEntry& entry = editor.tree_files[editor.tree_cursor];
        if (entry.is_directory && entry.open) {
            entry.open = false;
            editor.tree_cursor_reveal = true;
            return;
        }
        if (move_parent) {
            set_tree_cursor(editor, tree_cursor_parent(editor));
        }
    }

    auto open_filesystem_tree_cursor(EditorState& editor) -> void {
        clamp_filesystem_tree_cursor(editor);
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            if (!editor.flag(EditorFlag::TREE_OPEN)) {
                editor.set_flag(EditorFlag::TREE_OPEN, true);
                editor.tree_cursor_reveal = true;
            }
            return;
        }
        FileTreeEntry const& entry = editor.tree_files[editor.tree_cursor];
        if (entry.is_directory) {
            expand_filesystem_tree_cursor(editor);
            return;
        }
        editor.file_search_open_file = editor.tree_cursor;
    }

    auto activate_filesystem_tree_cursor(EditorState& editor) -> void {
        clamp_filesystem_tree_cursor(editor);
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            editor.set_flag(EditorFlag::TREE_OPEN, !editor.flag(EditorFlag::TREE_OPEN));
            editor.tree_cursor_reveal = true;
            return;
        }
        FileTreeEntry& entry = editor.tree_files[editor.tree_cursor];
        if (entry.is_directory) {
            entry.open = !entry.open;
            editor.tree_cursor_reveal = true;
            return;
        }
        editor.file_search_open_file = editor.tree_cursor;
    }

    auto handle_filesystem_normal_char(EditorState& editor, char ch) -> void {
        editor.set_flag(EditorFlag::PENDING_G, false);
        if (editor.flag(EditorFlag::PENDING_D)) {
            editor.set_flag(EditorFlag::PENDING_D, false);
            if (ch == 'd') {
                queue_tree_delete(editor);
            }
            return;
        }
        switch (ch) {
        case ' ':
            editor.set_flag(EditorFlag::PENDING_LEADER, true);
            break;
        case 'i':
        case 'I':
            begin_tree_rename(editor, 0u);
            break;
        case 'a':
        case 'A':
            if (editor.tree_cursor < editor.tree_files.size()) {
                begin_tree_rename(editor, editor.tree_files[editor.tree_cursor].name.size());
            }
            break;
        case 'o':
            begin_tree_create(editor, TreeEditMode::CREATE_FILE);
            break;
        case 'O':
            begin_tree_create(editor, TreeEditMode::CREATE_DIRECTORY);
            break;
        case 'd':
            editor.set_flag(EditorFlag::PENDING_D, true);
            break;
        case 'u':
            queue_tree_history(editor, false);
            break;
        case 'U':
            queue_tree_history(editor, true);
            break;
        default:
            break;
        }
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
        float const cursor_center =
            (static_cast<float>(editor_visible_line_index(editor, editor.cursor_line)) + 0.5f) *
            line_height;
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
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            EditorCursor& cursor = editor.extra_cursors[index];
            cursor.selection_anchor_line = cursor.line;
            cursor.selection_anchor_column = cursor.column;
        }
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, true);
    }

    auto add_cursor_line(EditorState& editor, int32_t direction) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), cursor_edit_count(editor) + 1u);
        size_t count = collect_cursors(editor, cursors);
        sort_cursors_by_offset(cursors, count, false);

        size_t const edge = direction > 0 ? count - 1u : 0u;
        EditorTextPosition const edge_position =
            text_buffer_offset_to_position(editor.text, cursors[edge].offset);
        size_t line = edge_position.line;
        if (direction < 0) {
            if (line == 0u) {
                return;
            }
            line -= 1u;
        } else {
            if (line + 1u >= editor_line_count(editor)) {
                return;
            }
            line += 1u;
        }

        size_t const preferred = cursors[edge].preferred_column;
        size_t const column = std::min(preferred, line_size(editor, line));
        size_t const offset = position_offset(editor, {line, column});
        for (size_t index = 0u; index < count; ++index) {
            if (cursors[index].offset == offset) {
                return;
            }
        }

        cursors[count] = {.offset = offset, .preferred_column = preferred};
        count += 1u;
        store_cursors(editor, cursors, count, true);
    }

    [[nodiscard]] auto multi_cursor_key(gui::KeyEvent const& event, int32_t& direction) -> bool {
        if ((event.mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT)) !=
            (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT)) {
            return false;
        }
        if ((event.mods & gui::KEY_MOD_SUPER) != 0u) {
            return false;
        }
        if (event.key == gui::Key::UP) {
            direction = -1;
            return true;
        }
        if (event.key == gui::Key::DOWN) {
            direction = 1;
            return true;
        }
        return false;
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

    [[nodiscard]] auto line_comment_column(EditorLine line) -> size_t {
        size_t column = 0u;
        while (column < line.size && (line.text[column] == ' ' || line.text[column] == '\t')) {
            column += 1u;
        }
        return column;
    }

    auto move_current_line_start(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, {editor.cursor_line, 0u}, select);
    }

    auto move_current_line_end(EditorState& editor, bool select) -> void {
        move_cursor_to(editor, {editor.cursor_line, line_size(editor, editor.cursor_line)}, select);
    }

    [[nodiscard]] auto line_commented(EditorLine line) -> bool {
        size_t const column = line_comment_column(line);
        return column + 1u < line.size && line.text[column] == '/' && line.text[column + 1u] == '/';
    }

    auto toggle_line_comments(EditorState& editor) -> void {
        EditorSelectionRange const selection = editor_selection_range(editor);
        size_t first = editor.cursor_line;
        size_t last = editor.cursor_line;
        if (selection.active) {
            first = selection.start_line;
            last = selection_last_line(editor, selection);
        }

        bool uncomment = true;
        for (size_t line_index = first; line_index <= last; ++line_index) {
            uncomment = uncomment && line_commented(editor_line(editor, line_index));
        }

        size_t cursor_column = editor.cursor_column;
        EditorLine const cursor_line = editor_line(editor, editor.cursor_line);
        size_t const cursor_comment = line_comment_column(cursor_line);
        if (uncomment && line_commented(cursor_line)) {
            size_t end = cursor_comment + 2u;
            if (end < cursor_line.size && cursor_line.text[end] == ' ') {
                end += 1u;
            }
            if (cursor_column >= end) {
                cursor_column -= end - cursor_comment;
            } else if (cursor_column > cursor_comment) {
                cursor_column = cursor_comment;
            }
        } else if (!uncomment && cursor_column >= cursor_comment) {
            cursor_column += 3u;
        }

        save_editor_undo(editor);
        for (size_t line_index = first; line_index <= last; ++line_index) {
            EditorLine const line = editor_line(editor, line_index);
            size_t const column = line_comment_column(line);
            if (uncomment) {
                if (!line_commented(line)) {
                    continue;
                }
                size_t end = column + 2u;
                if (end < line.size && line.text[end] == ' ') {
                    end += 1u;
                }
                text_buffer_erase(
                    editor.text,
                    position_offset(editor, {line_index, column}),
                    position_offset(editor, {line_index, end})
                );
            } else {
                text_buffer_insert(
                    editor.text, position_offset(editor, {line_index, column}), "// "
                );
            }
        }

        if (selection.active) {
            set_cursor(editor, first, 0u);
        } else {
            set_cursor(editor, editor.cursor_line, cursor_column);
        }
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
                open_files_sidebar(editor);
            } else if (ch == 'g') {
                open_git_sidebar(editor);
            } else if (ch == 'f') {
                open_file_search(editor);
            } else if (ch == 'b') {
                open_buffer_search(editor);
            } else if (ch == 'd') {
                open_editor_lsp_diagnostics(editor, EditorJumpListKind::LSP_FILE_DIAGNOSTICS);
            } else if (ch == 'D') {
                open_editor_lsp_diagnostics(editor, EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS);
            } else if (ch == '/') {
                open_editor_global_search(editor);
            } else if (ch == 'j') {
                open_editor_jump_list(editor);
            } else if (ch == 'o') {
                jump_list_previous(editor);
            } else if (ch == 'i') {
                jump_list_next(editor);
            } else if (ch == 'n') {
                editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, true);
            } else if (ch == 'w') {
                editor.set_flag(EditorFlag::PENDING_WINDOW, true);
            } else if (ch == 's') {
                request_lsp(editor, LspRequestKind::DOCUMENT_SYMBOL);
            } else if (ch == 'S') {
                request_lsp(editor, LspRequestKind::WORKSPACE_SYMBOL);
            } else if (ch == 'r' || ch == 'c') {
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
            clear_pending_line_number(editor);
            editor.set_flag(EditorFlag::PENDING_LSP, false);
            editor.set_flag(EditorFlag::PENDING_R, false);
            editor.set_flag(EditorFlag::PENDING_Z, false);
            if (!alt) {
                if (editor.sidebar_tab == EditorSidebarTab::GIT) {
                    handle_git_normal_char(editor, ch);
                } else {
                    handle_filesystem_normal_char(editor, ch);
                }
            }
            return;
        }

        if (editor.flag(EditorFlag::PENDING_LSP)) {
            editor.set_flag(EditorFlag::PENDING_LSP, false);
            if (ch == 'n') {
                open_lsp_rename(editor);
            } else if (ch == 'a') {
                request_lsp(editor, LspRequestKind::CODE_ACTION);
            } else if (ch == 's') {
                request_lsp(editor, LspRequestKind::DOCUMENT_SYMBOL);
            } else if (ch == 'j') {
                open_editor_jump_list(editor);
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
            if (editor.view_kind == EditorViewKind::GIT_DIFF) {
                if (ch == 'g') {
                    clear_extra_cursors(editor);
                    move_cursor_to(editor, {0u, 0u}, visual_selecting(editor));
                }
                return;
            }
            if (ch == 'g') {
                clear_extra_cursors(editor);
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
            if (editor.view_kind == EditorViewKind::GIT_DIFF) {
                if (ch == 'z') {
                    center_cursor(editor, editor.split_nodes[editor.focused_split].rect);
                }
                return;
            }
            if (ch == 'z') {
                center_cursor(editor, editor.split_nodes[editor.focused_split].rect);
            } else if (ch == 'a') {
                toggle_current_fold(editor);
            } else if (ch == 'c') {
                close_current_fold(editor);
            } else if (ch == 'o') {
                open_current_fold(editor);
            } else if (ch == 'M') {
                close_all_folds(editor);
            } else if (ch == 'R') {
                open_all_folds(editor);
            }
            return;
        }

        if (focused_pane_kind(editor) == EditorPaneKind::CODE &&
            editor.view_kind == EditorViewKind::GIT_DIFF) {
            bool const select = visual_selecting(editor);
            switch (ch) {
            case ':':
                open_command_line(editor);
                break;
            case ',':
                clear_extra_cursors(editor);
                clear_selection(editor);
                break;
            case '/':
                open_text_search(editor);
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
            case '0':
                move_current_line_start(editor, select);
                break;
            case '$':
                move_current_line_end(editor, select);
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
            case 'G':
                clear_extra_cursors(editor);
                move_cursor_to(editor, {editor_line_count(editor) - 1u, 0u}, select);
                break;
            case 'z':
                editor.set_flag(EditorFlag::PENDING_Z, true);
                break;
            case 'v':
                set_visual_mode(editor, EditorSelectionMode::CHARACTER);
                break;
            case 'V':
                set_visual_mode(editor, EditorSelectionMode::LINE);
                break;
            case 'y':
                copy_selection_to_clipboard(editor, clipboard);
                break;
            case 'n':
                repeat_text_search(editor, false);
                break;
            case 'N':
                repeat_text_search(editor, true);
                break;
            case 'u':
                editor.git_diff_side_by_side = !editor.git_diff_side_by_side;
                set_git_diff_view_text(editor);
                break;
            default:
                break;
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
        case ',':
            clear_extra_cursors(editor);
            clear_selection(editor);
            break;
        case '/':
            open_text_search(editor);
            break;
        case '*':
            if (selection.active) {
                ArenaTemp temp = begin_thread_temp_arena();
                StrRef const text = text_buffer_copy_range(
                    editor.text,
                    *temp.arena(),
                    position_offset(editor, selection_start(selection)),
                    position_offset(editor, selection_end(selection))
                );
                submit_text_search(editor, text);
            }
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
            if (has_extra_cursors(editor) && !selection.active) {
                move_all_cursors_line_start(editor, false);
                enter_insert_at(editor, cursor_position(editor));
                break;
            }
            enter_insert_at(
                editor, {selection.active ? selection.start_line : editor.cursor_line, 0u}
            );
            break;
        case 'a':
            if (has_extra_cursors(editor) && !selection.active) {
                move_all_cursors_by(editor, next_position, false);
                enter_insert_at(editor, cursor_position(editor));
                break;
            }
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
            if (has_extra_cursors(editor) && !selection.active) {
                move_all_cursors_line_end(editor, false);
                enter_insert_at(editor, cursor_position(editor));
                break;
            }
            size_t const line =
                selection.active ? selection_last_line(editor, selection) : editor.cursor_line;
            enter_insert_at(editor, {line, line_size(editor, line)});
        } break;
        case 'C':
            add_cursor_line(editor, 1);
            break;
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
        case 'n':
            repeat_text_search(editor, false);
            break;
        case 'N':
            repeat_text_search(editor, true);
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
            if (has_extra_cursors(editor)) {
                move_all_cursors_line_start(editor, select);
            } else {
                move_cursor_to(editor, {editor.cursor_line, 0u}, select);
            }
            break;
        case '$':
            if (has_extra_cursors(editor)) {
                move_all_cursors_line_end(editor, select);
            } else {
                move_cursor_to(
                    editor, {editor.cursor_line, line_size(editor, editor.cursor_line)}, select
                );
            }
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
            clear_extra_cursors(editor);
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
            if (selection.active) {
                change_selection(editor, clipboard, true);
            } else {
                if (can_delete_char(editor)) {
                    save_editor_undo(editor);
                    delete_char(editor);
                }
                enter_insert_at(editor, cursor_position(editor));
            }
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

    [[nodiscard]] auto line_comment_shortcut_key(gui::KeyEvent const& event) -> bool {
        return event.kind == gui::KeyEventKind::PRESS && event.key == gui::Key::SLASH &&
               event.mods == gui::KEY_MOD_CTRL;
    }

    [[nodiscard]] auto can_backspace(EditorState const& editor) -> bool {
        if (editor.flag(EditorFlag::SELECTION_ACTIVE) || editor.cursor_column != 0u ||
            editor.cursor_line != 0u) {
            return true;
        }
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            if (editor.extra_cursors[index].column != 0u ||
                editor.extra_cursors[index].line != 0u) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto can_delete_char(EditorState const& editor) -> bool {
        if (editor.flag(EditorFlag::SELECTION_ACTIVE) ||
            editor.cursor_column < line_size(editor, editor.cursor_line) ||
            (editor.cursor_column == line_size(editor, editor.cursor_line) &&
             line_has_trailing_newline(editor, editor.cursor_line))) {
            return true;
        }
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            EditorCursor const cursor = editor.extra_cursors[index];
            if (cursor.column < line_size(editor, cursor.line) ||
                (cursor.column == line_size(editor, cursor.line) &&
                 line_has_trailing_newline(editor, cursor.line))) {
                return true;
            }
        }
        return false;
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
        return editor.view_kind != EditorViewKind::GIT_DIFF && !editor.current_file_path.empty() &&
               (lsp_cpp_file_name(editor.current_file_name) ||
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
        send_lsp_request(
            editor,
            {
                .kind = LspRequestKind::SEMANTIC_TOKENS,
                .path = editor.current_file_path,
                .revision = editor.text.revision,
            }
        );
        send_lsp_request(
            editor,
            {
                .kind = LspRequestKind::FOLDING_RANGE,
                .path = editor.current_file_path,
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

    [[nodiscard]] auto lsp_position_after_text(LspPosition start, StrRef text) -> LspPosition {
        LspPosition position = start;
        for (char ch : text) {
            if (ch == '\n') {
                position.line += 1u;
                position.column = 0u;
            } else {
                position.column += 1u;
            }
        }
        return position;
    }

    [[nodiscard]] auto lsp_position_add(LspPosition start, LspPosition relative) -> LspPosition {
        if (relative.line == 0u) {
            return {start.line, start.column + relative.column};
        }
        return {start.line + relative.line, relative.column};
    }

    [[nodiscard]] auto
    lsp_transform_position_after_edit(LspPosition position, LspTextEdit const& edit)
        -> LspPosition {
        if (lsp_position_less(position, edit.range.end)) {
            return position;
        }
        LspPosition const end = lsp_position_after_text(edit.range.start, edit.new_text);
        if (position.line == edit.range.end.line) {
            return {end.line, end.column + position.column - edit.range.end.column};
        }
        return {end.line + position.line - edit.range.end.line, position.column};
    }

    auto set_lsp_completion_selection(EditorState& editor, LspRange range, bool select) -> void {
        EditorPosition const start = lsp_clamped_position(editor, range.start);
        EditorPosition const end = lsp_clamped_position(editor, range.end);
        clear_extra_cursors(editor);
        editor.selection_anchor_line = start.line;
        editor.selection_anchor_column = start.column;
        editor.cursor_line = end.line;
        editor.cursor_column = end.column;
        editor.preferred_column = end.column;
        editor.selection_mode = EditorSelectionMode::NONE;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, select && !same_position(start, end));
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

    [[nodiscard]] auto
    same_editor_jump(EditorJump const& jump, StrRef name, StrRef path, size_t line, size_t column)
        -> bool {
        return same_editor_file(jump.name, jump.path, name, path) && jump.line == line &&
               jump.column == column;
    }

    auto
    record_editor_jump(EditorState& editor, StrRef name, StrRef path, size_t line, size_t column)
        -> void {
        if (editor.arena == nullptr || (name.empty() && path.empty())) {
            return;
        }
        if (name.empty()) {
            name = path;
        }
        if (!editor.jumps.empty()) {
            size_t const cursor = editor.jump_cursor != JUMP_LIST_NO_SELECTION
                                      ? editor.jump_cursor
                                      : editor.jumps.size() - 1u;
            if (cursor + 1u < editor.jumps.size()) {
                BASE_UNUSED(editor.jumps.resize(cursor + 1u));
            }
            if (same_editor_jump(
                    editor.jumps[editor.jumps.size() - 1u], name, path, line, column
                )) {
                editor.jump_cursor = editor.jumps.size() - 1u;
                return;
            }
        }
        if (editor.jumps.size() == JUMP_LIST_LIMIT) {
            editor.jumps.ordered_remove(0u);
            if (editor.jump_cursor != JUMP_LIST_NO_SELECTION && editor.jump_cursor != 0u) {
                editor.jump_cursor -= 1u;
            }
        }
        bool const ok = editor.jumps.push_back({
            arena_copy_cstr(*editor.arena, name),
            path.empty() ? StrRef() : arena_copy_cstr(*editor.arena, path),
            line,
            column,
        });
        DEBUG_ASSERT(ok);
        (void)ok;
        editor.jump_cursor = editor.jumps.size() - 1u;
    }

    auto jump_list_previous(EditorState& editor) -> void {
        if (editor.jumps.empty()) {
            return;
        }
        if (editor.jump_cursor == JUMP_LIST_NO_SELECTION) {
            editor.jump_cursor = editor.jumps.size() - 1u;
        }
        if (editor.jump_cursor == 0u) {
            return;
        }
        editor.jump_cursor -= 1u;
        editor.jump_open_index = editor.jump_cursor;
    }

    auto jump_list_next(EditorState& editor) -> void {
        if (editor.jumps.empty()) {
            return;
        }
        if (editor.jump_cursor == JUMP_LIST_NO_SELECTION) {
            editor.jump_cursor = editor.jumps.size() - 1u;
        }
        if (editor.jump_cursor + 1u >= editor.jumps.size()) {
            return;
        }
        editor.jump_cursor += 1u;
        editor.jump_open_index = editor.jump_cursor;
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

    struct EditorLspCompletionEdit {
        LspTextEdit edit = {};
        bool primary = false;
    };

    [[nodiscard]] auto
    completion_edit_path_matches(EditorState const& editor, LspTextEdit const& edit) -> bool {
        return edit.path.empty() || edit.path == editor.current_file_path;
    }

    auto apply_lsp_completion(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr || editor.lsp_bridge->completions.empty()) {
            close_editor_lsp_popup(editor);
            return;
        }
        size_t const index =
            std::min(editor.lsp_selected, editor.lsp_bridge->completions.size() - 1u);
        LspCompletionItem const& item = editor.lsp_bridge->completions[index];

        ArenaTemp temp = begin_thread_temp_arena();
        StrRef insert_text = !item.insert_text.empty() ? item.insert_text : item.label;
        LspSnippetExpansion expansion = {};
        if (item.is_snippet) {
            expansion = lsp_expand_snippet(*temp.arena(), insert_text);
            insert_text = expansion.text;
        } else {
            LspPosition const end = lsp_position_after_text({}, insert_text);
            expansion.selection = {end, end};
        }

        EditorSelectionRange const selection = editor_selection_range(editor);
        LspRange const primary_range =
            item.has_edit   ? item.edit_range
            : selection.active
                ? LspRange{
                      {selection.start_line, selection.start_column},
                      {selection.end_line, selection.end_column}
                  }
                : LspRange{
                      {editor.cursor_line, editor.cursor_column},
                      {editor.cursor_line, editor.cursor_column}
                  };
        LspRange target = {
            lsp_position_add(primary_range.start, expansion.selection.start),
            lsp_position_add(primary_range.start, expansion.selection.end),
        };

        Vec<EditorLspCompletionEdit> edits = {};
        BASE_UNUSED(edits.init(item.additional_edits.size() + 1u, temp.arena()->resource()));
        for (LspTextEdit const& edit : item.additional_edits) {
            if (completion_edit_path_matches(editor, edit) && lsp_range_valid(edit.range)) {
                BASE_UNUSED(edits.push_back({edit, false}));
            }
        }
        BASE_UNUSED(edits.push_back({
            {
                .path = editor.current_file_path,
                .range = primary_range,
                .new_text = insert_text,
            },
            true,
        }));

        std::sort(edits.begin(), edits.end(), [](auto const& a, auto const& b) {
            if (a.edit.range.start.line != b.edit.range.start.line) {
                return a.edit.range.start.line > b.edit.range.start.line;
            }
            return a.edit.range.start.column > b.edit.range.start.column;
        });

        save_editor_undo(editor);
        bool target_active = false;
        for (EditorLspCompletionEdit const& completion_edit : edits) {
            LspTextEdit const& edit = completion_edit.edit;
            size_t const start = lsp_position_offset(editor, edit.range.start);
            size_t const end = lsp_position_offset(editor, edit.range.end);
            if (end < start) {
                continue;
            }
            text_buffer_erase(editor.text, start, end);
            if (!edit.new_text.empty()) {
                text_buffer_insert(editor.text, start, edit.new_text);
            }
            if (completion_edit.primary) {
                target_active = true;
            } else if (target_active) {
                target.start = lsp_transform_position_after_edit(target.start, edit);
                target.end = lsp_transform_position_after_edit(target.end, edit);
            }
        }

        if (target_active) {
            set_lsp_completion_selection(editor, target, expansion.has_selection);
        }
        refresh_editor_dirty(editor);
        sync_shared_panes(editor);
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
            if (editor.lsp_popup == EditorLspPopupKind::COMPLETION) {
                close_editor_lsp_popup(editor);
                return false;
            }
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

    [[nodiscard]] auto
    git_control_leader_input(EditorState const& editor, gui::InputState const& input) -> bool {
        if (editor.flag(EditorFlag::PENDING_LEADER) || editor.flag(EditorFlag::PENDING_WINDOW) ||
            editor.flag(EditorFlag::PENDING_LSP)) {
            return true;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.key == gui::Key::SPACE &&
                (event.kind == gui::KeyEventKind::PRESS ||
                 event.kind == gui::KeyEventKind::REPEAT) &&
                (event.mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u) {
                return true;
            }
        }
        return false;
    }

    auto open_editor_lsp_locations(EditorState& editor) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        open_jump_list_picker(editor, EditorJumpListKind::LSP_LOCATIONS);
    }

    auto open_editor_lsp_symbols(EditorState& editor, EditorJumpListKind kind) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        open_jump_list_picker(editor, kind);
    }

    auto open_editor_lsp_diagnostics(EditorState& editor, EditorJumpListKind kind) -> void {
        if (editor.lsp_bridge == nullptr) {
            return;
        }
        open_jump_list_picker(editor, kind);
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
            if (has_extra_cursors(editor) && !word) {
                move_all_cursors_line_start(editor, select);
                return true;
            }
            if (!select && has_extra_cursors(editor)) {
                if (word) {
                    clear_extra_cursors(editor);
                    move_cursor_to(editor, {}, false);
                }
                return true;
            }
            move_cursor_to(
                editor, word ? EditorPosition{} : EditorPosition{editor.cursor_line, 0u}, select
            );
            return true;
        case gui::Key::END:
            if (has_extra_cursors(editor) && !word) {
                move_all_cursors_line_end(editor, select);
                return true;
            }
            if (!select && has_extra_cursors(editor)) {
                if (word) {
                    clear_extra_cursors(editor);
                    size_t const line = editor_line_count(editor) - 1u;
                    move_cursor_to(editor, {line, line_size(editor, line)}, false);
                }
                return true;
            }
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
            bool const collapse_extra_cursors =
                !editor.flag(EditorFlag::INSERT_MODE) && !editor.flag(EditorFlag::SELECTION_ACTIVE);
            if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM &&
                tree_edit_active(editor)) {
                cancel_tree_edit(editor);
            }
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
            if (collapse_extra_cursors) {
                clear_extra_cursors(editor);
            }
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
            bool const ctrl = (event.mods & gui::KEY_MOD_CTRL) != 0u &&
                              (event.mods & (gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u;
            if (editor.sidebar_tab == EditorSidebarTab::GIT) {
                if (editor.git_control_focused && event.key == gui::Key::ENTER) {
                    return;
                }
                if (event.key == gui::Key::ENTER) {
                    activate_git_selection(editor);
                    return;
                }
                if (event.key == gui::Key::SPACE) {
                    close_git_commit_popup(editor);
                    editor.set_flag(EditorFlag::PENDING_LEADER, true);
                    return;
                }
                switch (event.key) {
                case gui::Key::UP:
                    move_git_selection(editor, -1);
                    return;
                case gui::Key::DOWN:
                    move_git_selection(editor, 1);
                    return;
                case gui::Key::LEFT: {
                    close_git_commit_popup(editor);
                    GitVisibleRow const row = git_visible_row(editor, editor.git_selected);
                    if (row.kind == GitVisibleRowKind::STAGED_HEADER) {
                        editor.git_staged_open = false;
                    } else if (row.kind == GitVisibleRowKind::CHANGES_HEADER) {
                        editor.git_changes_open = false;
                    } else if (row.kind == GitVisibleRowKind::GRAPH_HEADER) {
                        editor.git_graph_open = false;
                    } else if (row.kind == GitVisibleRowKind::COMMIT) {
                        editor.git_commits[row.index].open = false;
                    }
                    return;
                }
                case gui::Key::RIGHT:
                    activate_git_selection(editor);
                    return;
                default:
                    return;
                }
            }
            if (tree_edit_active(editor)) {
                if (ctrl && event.key == gui::Key::Z) {
                    if ((event.mods & gui::KEY_MOD_SHIFT) != 0u) {
                        BASE_UNUSED(restore_editor_redo(editor));
                    } else {
                        BASE_UNUSED(restore_editor_undo(editor));
                    }
                    return;
                }
                if (event.key == gui::Key::ENTER) {
                    BASE_UNUSED(commit_tree_edit(editor));
                    return;
                }
                if (handle_navigation_key(editor, event)) {
                    return;
                }
                switch (event.key) {
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
                    return;
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
                    return;
                default:
                    return;
                }
            }
            if (ctrl && event.key == gui::Key::Z) {
                queue_tree_history(editor, (event.mods & gui::KEY_MOD_SHIFT) != 0u);
                return;
            }
            if (event.key == gui::Key::TAB &&
                (event.mods & (gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT | gui::KEY_MOD_SUPER)) == 0u) {
                cycle_filesystem_tree_cursor(
                    editor, (event.mods & gui::KEY_MOD_SHIFT) != 0u ? -1 : 1
                );
                return;
            }
            if (event.key == gui::Key::ENTER) {
                activate_filesystem_tree_cursor(editor);
                return;
            }
            if (event.key == gui::Key::SPACE) {
                editor.set_flag(EditorFlag::PENDING_LEADER, true);
                return;
            }
            switch (event.key) {
            case gui::Key::UP:
                move_filesystem_tree_cursor(editor, -1);
                return;
            case gui::Key::DOWN:
                move_filesystem_tree_cursor(editor, 1);
                return;
            case gui::Key::LEFT:
                collapse_filesystem_tree_cursor(editor, true);
                return;
            case gui::Key::RIGHT:
                open_filesystem_tree_cursor(editor);
                return;
            default:
                break;
            }
            return;
        }
        int32_t multi_cursor_direction = 0;
        if (multi_cursor_key(event, multi_cursor_direction)) {
            add_cursor_line(editor, multi_cursor_direction);
            return;
        }
        if (line_comment_shortcut_key(event)) {
            toggle_line_comments(editor);
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
        if (shortcut_key(event, gui::Key::N)) {
            editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, true);
            return;
        }
        if (shortcut_key(event, gui::Key::W)) {
            editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, true);
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

        if (editor.view_kind == EditorViewKind::GIT_DIFF) {
            if (handle_navigation_key(editor, event)) {
                return;
            }
            if (event.key == gui::Key::SPACE) {
                editor.set_flag(EditorFlag::PENDING_LEADER, true);
            }
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
        if (focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM) {
            clamp_filesystem_tree_cursor(editor);
        }
        if (input.key_events == nullptr) {
            return;
        }
        if (editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            return;
        }
        bool const git_text_focused =
            editor.sidebar_tab == EditorSidebarTab::GIT &&
            (editor.git_commit_text_focused || editor.git_branch_search_focused ||
             editor.git_commit_search_focused || editor.git_action_ref_focused);
        bool const git_control_focused = editor.sidebar_tab == EditorSidebarTab::GIT &&
                                         focused_pane_kind(editor) == EditorPaneKind::FILESYSTEM &&
                                         editor.git_control_focused;
        bool const git_leader_input = git_control_leader_input(editor, input);
        if ((git_text_focused && (editor.git_text_editing || !git_leader_input)) ||
            editor.git_error_visible || (git_control_focused && !git_leader_input)) {
            return;
        }
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (handle_lsp_popup_event(editor, event)) {
                continue;
            }
            if (editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE) ||
                editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE) ||
                editor.flag(EditorFlag::COMMAND_LINE_ACTIVE)) {
                continue;
            }
            if (editor.flag(EditorFlag::JUMP_LIST_OPEN)) {
                handle_jump_list_event(editor, event);
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
                    if (editor.view_kind == EditorViewKind::GIT_DIFF) {
                        if (!editor.flag(EditorFlag::INSERT_MODE)) {
                            handle_normal_char(editor, ch, event.mods, clipboard);
                        }
                        continue;
                    }
                    if (editor.flag(EditorFlag::INSERT_MODE)) {
                        save_editor_undo(editor);
                        insert_char(editor, ch);
                        if (lsp_completion_text_trigger(ch)) {
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
        return editor_content_rect(rect).min.x +
               editor_scaled_font_size(editor, LINE_NUMBER_WIDTH) - editor.scroll_x;
    }

    [[nodiscard]] auto editor_max_scroll(EditorState const& editor, gui::Rect rect) -> float {
        gui::Rect const content = editor_content_rect(rect);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        float const content_height =
            static_cast<float>(editor_visible_line_count(editor)) * editor_line_height(editor);
        return std::max(0.0f, content_height - visible_height);
    }

    auto clamp_scroll(EditorState& editor, gui::Rect rect) -> void {
        editor.scroll_x = std::max(0.0f, editor.scroll_x);
        editor.scroll_y = std::clamp(editor.scroll_y, 0.0f, editor_max_scroll(editor, rect));
    }

    auto reveal_cursor(EditorState& editor, gui::Rect rect, float char_width) -> void {
        gui::Rect const content = editor_content_rect(rect);
        float const visible_height = std::max(1.0f, content.max.y - content.min.y);
        float const line_height = editor_line_height(editor);
        float const line_top =
            static_cast<float>(editor_visible_line_index(editor, editor.cursor_line)) * line_height;
        float const line_bottom = line_top + line_height;
        if (line_top < editor.scroll_y) {
            editor.scroll_y = line_top;
        } else if (line_bottom > editor.scroll_y + visible_height) {
            editor.scroll_y = line_bottom - visible_height;
        }

        float const text_min_x = content.min.x + editor_scaled_font_size(editor, LINE_NUMBER_WIDTH);
        float const visible_width = std::max(1.0f, content.max.x - text_min_x);
        size_t const line_size_value = line_size(editor, editor.cursor_line);
        size_t const column =
            editor.flag(EditorFlag::INSERT_MODE) || line_size_value == 0u ? editor.cursor_column
            : editor.cursor_column < line_size_value                      ? editor.cursor_column
            : line_has_trailing_newline(editor, editor.cursor_line)       ? line_size_value
                                                                          : line_size_value - 1u;
        float const cursor_left = static_cast<float>(column) * char_width;
        float const cursor_right =
            cursor_left + (editor.flag(EditorFlag::INSERT_MODE) ? 2.0f : char_width);
        if (cursor_left < editor.scroll_x) {
            editor.scroll_x = cursor_left;
        } else if (cursor_right > editor.scroll_x + visible_width) {
            editor.scroll_x = cursor_right - visible_width;
        }
        clamp_scroll(editor, rect);
    }

    [[nodiscard]] auto position_from_mouse(
        EditorState const& editor, gui::Rect rect, gui::Vec2 mouse, float char_width
    ) -> EditorPosition {
        gui::Rect const content = editor_content_rect(rect);
        float const y = std::max(0.0f, mouse.y - content.min.y + editor.scroll_y);
        float const line_height = editor_line_height(editor);
        size_t const visible_line =
            std::min(editor_visible_line_count(editor) - 1u, static_cast<size_t>(y / line_height));
        size_t const line = editor_visible_line_at(editor, visible_line);
        float const text_x = editor_text_x(editor, rect);
        float const column_x = std::max(editor.scroll_x, mouse.x - text_x);
        size_t const column = static_cast<size_t>(column_x / char_width + 0.5f);
        return clamp_position(editor, {line, column});
    }

    auto set_multi_cursor_range(EditorState& editor, EditorPosition focus) -> void {
        focus = clamp_position(editor, focus);
        size_t const focus_line = focus.line;
        size_t const first = std::min(editor.multi_cursor_anchor_line, focus_line);
        size_t const last = std::max(editor.multi_cursor_anchor_line, focus_line);

        ArenaTemp temp = begin_thread_temp_arena();
        EditorCursorEdit* const cursors =
            arena_alloc<EditorCursorEdit>(*temp.arena(), last - first + 1u);
        size_t count = 1u;
        cursors[0u] = {
            .offset = position_offset(editor, focus),
            .anchor_offset = position_offset(
                editor,
                {
                    focus_line,
                    std::min(editor.multi_cursor_anchor_column, line_size(editor, focus_line)),
                }
            ),
            .preferred_column = focus.column,
            .primary = true,
        };
        for (size_t line = first; line <= last; ++line) {
            if (line == focus_line) {
                continue;
            }
            size_t const column = std::min(focus.column, line_size(editor, line));
            cursors[count] = {
                .offset = position_offset(editor, {line, column}),
                .anchor_offset = position_offset(
                    editor,
                    {line, std::min(editor.multi_cursor_anchor_column, line_size(editor, line))}
                ),
                .preferred_column = focus.column,
            };
            count += 1u;
        }
        editor.selection_mode = EditorSelectionMode::NONE;
        store_cursors(editor, cursors, count, false);
    }

    auto begin_multi_cursor_from_mouse(
        EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width
    ) -> void {
        EditorPosition const position = position_from_mouse(editor, rect, mouse, char_width);
        editor.multi_cursor_anchor_line = position.line;
        editor.multi_cursor_anchor_column = position.column;
        set_multi_cursor_range(editor, position);
    }

    auto update_multi_cursor_from_mouse(
        EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width
    ) -> void {
        EditorPosition const position = position_from_mouse(editor, rect, mouse, char_width);
        set_multi_cursor_range(editor, position);
    }

    auto select_range(EditorState& editor, EditorPosition start, EditorPosition end) -> void {
        clear_extra_cursors(editor);
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
        clear_extra_cursors(editor);
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
            clear_extra_cursors(editor);
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
        size_t const extra_cursor_count = pane.extra_cursors.size();
        hash = hash_bytes(hash, &extra_cursor_count, sizeof(extra_cursor_count));
        hash =
            hash_bytes(hash, pane.extra_cursors.data(), extra_cursor_count * sizeof(EditorCursor));
        size_t const folded_count = pane.folded_ranges.size();
        hash = hash_bytes(hash, &folded_count, sizeof(folded_count));
        hash = hash_bytes(hash, pane.folded_ranges.data(), folded_count * sizeof(EditorFoldRange));
        hash = hash_bytes(hash, &pane.folded_revision, sizeof(pane.folded_revision));
        hash = hash_bytes(hash, &pane.selection_anchor_line, sizeof(pane.selection_anchor_line));
        hash =
            hash_bytes(hash, &pane.selection_anchor_column, sizeof(pane.selection_anchor_column));
        hash = hash_bytes(hash, &pane.selection_mode, sizeof(pane.selection_mode));
        hash = hash_bytes(hash, &pane.selection_active, sizeof(pane.selection_active));
        hash = hash_bytes(hash, &pane.scroll_x, sizeof(pane.scroll_x));
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
        hash = hash_bytes(hash, &pane.git_diff_side_by_side, sizeof(pane.git_diff_side_by_side));
        hash = hash_bytes(hash, &pane.view_kind, sizeof(pane.view_kind));
        return hash;
    }

    [[nodiscard]] auto editor_state_hash(EditorState const& editor) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        hash = hash_text(hash, editor.text);
        hash = hash_bytes(hash, &editor.cursor_line, sizeof(editor.cursor_line));
        hash = hash_bytes(hash, &editor.cursor_column, sizeof(editor.cursor_column));
        size_t const extra_cursor_count = editor.extra_cursors.size();
        hash = hash_bytes(hash, &extra_cursor_count, sizeof(extra_cursor_count));
        hash = hash_bytes(
            hash, editor.extra_cursors.data(), extra_cursor_count * sizeof(EditorCursor)
        );
        size_t const folded_count = editor.folded_ranges.size();
        hash = hash_bytes(hash, &folded_count, sizeof(folded_count));
        hash =
            hash_bytes(hash, editor.folded_ranges.data(), folded_count * sizeof(EditorFoldRange));
        hash = hash_bytes(hash, &editor.folded_revision, sizeof(editor.folded_revision));
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
        hash = hash_bytes(hash, &editor.raster_policy, sizeof(editor.raster_policy));
        hash = hash_bytes(hash, &editor.view_kind, sizeof(editor.view_kind));
        hash =
            hash_bytes(hash, &editor.git_diff_side_by_side, sizeof(editor.git_diff_side_by_side));
        hash = hash_bytes(hash, &editor.scroll_x, sizeof(editor.scroll_x));
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
        hash = hash_bytes(hash, &editor.tree_cursor, sizeof(editor.tree_cursor));
        hash = hash_bytes(hash, &editor.sidebar_tab, sizeof(editor.sidebar_tab));
        hash = hash_bytes(hash, &editor.git_selected, sizeof(editor.git_selected));
        hash = hash_bytes(hash, &editor.git_commit_popup, sizeof(editor.git_commit_popup));
        hash = hash_bytes(hash, &editor.git_staged_open, sizeof(editor.git_staged_open));
        hash = hash_bytes(hash, &editor.git_changes_open, sizeof(editor.git_changes_open));
        hash = hash_bytes(hash, &editor.git_graph_open, sizeof(editor.git_graph_open));
        hash = hash_bytes(hash, &editor.git_branches_open, sizeof(editor.git_branches_open));
        hash =
            hash_bytes(hash, &editor.git_pending_pull_count, sizeof(editor.git_pending_pull_count));
        hash =
            hash_bytes(hash, &editor.git_pending_push_count, sizeof(editor.git_pending_push_count));
        hash = hash_bytes(hash, &editor.git_operation_state, sizeof(editor.git_operation_state));
        hash = hash_bytes(hash, &editor.git_error_visible, sizeof(editor.git_error_visible));
        hash =
            hash_bytes(hash, &editor.git_selection_focused, sizeof(editor.git_selection_focused));
        hash = hash_bytes(hash, &editor.git_control_focused, sizeof(editor.git_control_focused));
        hash = hash_bytes(hash, &editor.git_text_editing, sizeof(editor.git_text_editing));
        hash = hash_bytes(hash, &editor.git_cursor_reveal, sizeof(editor.git_cursor_reveal));
        hash = hash_bytes(hash, &editor.git_commits_more, sizeof(editor.git_commits_more));
        hash = hash_bytes(hash, &editor.git_commits_loading, sizeof(editor.git_commits_loading));
        hash =
            hash_bytes(hash, &editor.git_operation_pending, sizeof(editor.git_operation_pending));
        hash = hash_bytes(
            hash,
            &editor.git_commit_load_more_requested,
            sizeof(editor.git_commit_load_more_requested)
        );
        hash = hash_bytes(
            hash, &editor.git_commit_text_focused, sizeof(editor.git_commit_text_focused)
        );
        hash = hash_bytes(
            hash, &editor.git_branch_search_focused, sizeof(editor.git_branch_search_focused)
        );
        hash = hash_bytes(
            hash, &editor.git_commit_search_focused, sizeof(editor.git_commit_search_focused)
        );
        hash =
            hash_bytes(hash, &editor.git_action_ref_focused, sizeof(editor.git_action_ref_focused));
        hash = hash_bytes(
            hash, editor.git_branch_search_text, cstr_len(editor.git_branch_search_text)
        );
        hash = hash_bytes(
            hash, editor.git_commit_search_text, cstr_len(editor.git_commit_search_text)
        );
        hash = hash_bytes(hash, editor.git_action_ref_text, cstr_len(editor.git_action_ref_text));
        size_t const git_status_count = editor.git_status_items.size();
        hash = hash_bytes(hash, &git_status_count, sizeof(git_status_count));
        size_t const git_commit_count = editor.git_commits.size();
        hash = hash_bytes(hash, &git_commit_count, sizeof(git_commit_count));
        size_t const git_commit_file_count = editor.git_commit_files.size();
        hash = hash_bytes(hash, &git_commit_file_count, sizeof(git_commit_file_count));
        size_t const git_branch_count = editor.git_branches.size();
        hash = hash_bytes(hash, &git_branch_count, sizeof(git_branch_count));
        for (GitBranch const& branch : editor.git_branches) {
            size_t const branch_size = branch.name.size();
            hash = hash_bytes(hash, &branch_size, sizeof(branch_size));
            hash = hash_bytes(hash, branch.name.data(), branch.name.size());
        }
        size_t const git_current_branch_size = editor.git_current_branch.size();
        hash = hash_bytes(hash, &git_current_branch_size, sizeof(git_current_branch_size));
        hash = hash_bytes(hash, editor.git_current_branch.data(), editor.git_current_branch.size());
        size_t const git_status_text_size = editor.git_status_text.size();
        hash = hash_bytes(hash, &git_status_text_size, sizeof(git_status_text_size));
        hash = hash_bytes(hash, editor.git_status_text.data(), editor.git_status_text.size());
        size_t const git_error_text_size = editor.git_error_text.size();
        hash = hash_bytes(hash, &git_error_text_size, sizeof(git_error_text_size));
        hash = hash_bytes(hash, editor.git_error_text.data(), editor.git_error_text.size());
        hash = hash_bytes(hash, &editor.tree_edit_mode, sizeof(editor.tree_edit_mode));
        hash =
            hash_bytes(hash, &editor.tree_operation_pending, sizeof(editor.tree_operation_pending));
        hash = hash_bytes(hash, &editor.jump_selected, sizeof(editor.jump_selected));
        hash = hash_bytes(hash, &editor.jump_cursor, sizeof(editor.jump_cursor));
        hash = hash_bytes(hash, &editor.jump_open_index, sizeof(editor.jump_open_index));
        hash = hash_bytes(
            hash, &editor.global_search_open_index, sizeof(editor.global_search_open_index)
        );
        hash = hash_bytes(hash, &editor.jump_list_kind, sizeof(editor.jump_list_kind));
        hash = hash_bytes(
            hash,
            &editor.global_search_refresh_requested,
            sizeof(editor.global_search_refresh_requested)
        );
        hash = hash_bytes(hash, &editor.command_text_size, sizeof(editor.command_text_size));
        hash = hash_bytes(hash, &editor.command_selected, sizeof(editor.command_selected));
        hash = hash_bytes(hash, editor.command_text, editor.command_text_size);
        hash =
            hash_bytes(hash, &editor.text_search_text_size, sizeof(editor.text_search_text_size));
        hash = hash_bytes(
            hash, &editor.text_search_origin_line, sizeof(editor.text_search_origin_line)
        );
        hash = hash_bytes(hash, editor.text_search_text, editor.text_search_text_size);
        hash = hash_bytes(hash, &editor.lsp_popup, sizeof(editor.lsp_popup));
        hash = hash_bytes(hash, &editor.close_intent, sizeof(editor.close_intent));
        hash = hash_bytes(hash, &editor.lsp_selected, sizeof(editor.lsp_selected));
        hash = hash_bytes(hash, &editor.git_branch_selection, sizeof(editor.git_branch_selection));
        hash = hash_bytes(
            hash, &editor.git_commit_popup_selection, sizeof(editor.git_commit_popup_selection)
        );
        hash = hash_bytes(
            hash, &editor.git_commit_popup_keyboard, sizeof(editor.git_commit_popup_keyboard)
        );
        hash = hash_bytes(
            hash, &editor.git_commit_popup_mouse_known, sizeof(editor.git_commit_popup_mouse_known)
        );
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
        hash = hash_bytes(
            hash,
            &editor.lsp_seen_folding_ranges_generation,
            sizeof(editor.lsp_seen_folding_ranges_generation)
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
            size_t const file_extra_cursor_count = file.extra_cursors.size();
            hash = hash_bytes(hash, &file_extra_cursor_count, sizeof(file_extra_cursor_count));
            hash = hash_bytes(
                hash, file.extra_cursors.data(), file_extra_cursor_count * sizeof(EditorCursor)
            );
            size_t const file_folded_count = file.folded_ranges.size();
            hash = hash_bytes(hash, &file_folded_count, sizeof(file_folded_count));
            hash = hash_bytes(
                hash, file.folded_ranges.data(), file_folded_count * sizeof(EditorFoldRange)
            );
            hash = hash_bytes(hash, &file.folded_revision, sizeof(file.folded_revision));
            hash =
                hash_bytes(hash, &file.selection_anchor_line, sizeof(file.selection_anchor_line));
            hash = hash_bytes(
                hash, &file.selection_anchor_column, sizeof(file.selection_anchor_column)
            );
            hash = hash_bytes(hash, &file.selection_mode, sizeof(file.selection_mode));
            hash = hash_bytes(hash, &file.scroll_x, sizeof(file.scroll_x));
            hash = hash_bytes(hash, &file.scroll_y, sizeof(file.scroll_y));
            hash = hash_bytes(hash, &file.text_valid, sizeof(file.text_valid));
            hash = hash_bytes(hash, &file.insert_mode, sizeof(file.insert_mode));
            hash = hash_bytes(hash, &file.selection_active, sizeof(file.selection_active));
            hash = hash_bytes(hash, &file.dirty, sizeof(file.dirty));
            hash = hash_bytes(
                hash, &file.external_change_pending, sizeof(file.external_change_pending)
            );
            hash = hash_bytes(hash, &file.file_deleted_on_disk, sizeof(file.file_deleted_on_disk));
            hash =
                hash_bytes(hash, &file.git_diff_side_by_side, sizeof(file.git_diff_side_by_side));
            hash = hash_bytes(hash, &file.view_kind, sizeof(file.view_kind));
        }
        size_t const jump_count = editor.jumps.size();
        hash = hash_bytes(hash, &jump_count, sizeof(jump_count));
        for (EditorJump const& jump : editor.jumps) {
            size_t const name_size = jump.name.size();
            hash = hash_bytes(hash, &name_size, sizeof(name_size));
            hash = hash_bytes(hash, jump.name.data(), jump.name.size());
            size_t const path_size = jump.path.size();
            hash = hash_bytes(hash, &path_size, sizeof(path_size));
            hash = hash_bytes(hash, jump.path.data(), jump.path.size());
            hash = hash_bytes(hash, &jump.line, sizeof(jump.line));
            hash = hash_bytes(hash, &jump.column, sizeof(jump.column));
        }
        size_t const global_result_count = editor.global_search_results.size();
        hash = hash_bytes(hash, &global_result_count, sizeof(global_result_count));
        for (GlobalSearchResult const& result : editor.global_search_results) {
            hash = hash_bytes(hash, &result.tree_file_index, sizeof(result.tree_file_index));
            hash = hash_bytes(hash, &result.line, sizeof(result.line));
            hash = hash_bytes(hash, &result.column, sizeof(result.column));
            hash = hash_bytes(hash, &result.line_text_size, sizeof(result.line_text_size));
            hash = hash_bytes(hash, result.line_text, result.line_text_size);
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
        hash = hash_bytes(hash, &editor.last_code_split, sizeof(editor.last_code_split));
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
