#include "editor_session_state.h"

#include "editor_render.h"
#include "git.h"

#include <algorithm>
#include <base/config.h>
#include <base/memory.h>
#include <base/string_buffer.h>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace code_editor {

    inline constexpr uint32_t SESSION_STATE_MAGIC = 0x53454443u;
    inline constexpr uint32_t SESSION_STATE_VERSION = 2u;
    inline constexpr size_t SESSION_STATE_MAX_COUNT = 4096u;
    inline constexpr size_t SESSION_STATE_MAX_TEXT = 1024u * 1024u;

    [[nodiscard]] auto open_session_read_file(StrRef path) -> std::FILE* {
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

    [[nodiscard]] auto open_session_write_file(StrRef path) -> std::FILE* {
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

    enum class SessionGitDiffKind : uint8_t {
        NONE,
        STATUS,
        COMMIT,
    };

    struct SessionViewRef {
        size_t cursor_line = 0u;
        size_t cursor_column = 0u;
        size_t preferred_column = 0u;
        Slice<EditorCursor const> extra_cursors = {};
        size_t selection_anchor_line = 0u;
        size_t selection_anchor_column = 0u;
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float scroll_x = 0.0f;
        float scroll_y = 0.0f;
        bool selection_active = false;
    };

    struct SessionView {
        size_t cursor_line = 0u;
        size_t cursor_column = 0u;
        size_t preferred_column = 0u;
        Vec<EditorCursor> extra_cursors = {};
        size_t selection_anchor_line = 0u;
        size_t selection_anchor_column = 0u;
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float scroll_x = 0.0f;
        float scroll_y = 0.0f;
        bool selection_active = false;
    };

    struct SessionOpenFile {
        StrRef name = {};
        StrRef path = {};
        StrRef diff_path = {};
        StrRef diff_commit_oid = {};
        SessionView view = {};
        SessionGitDiffKind diff_kind = SessionGitDiffKind::NONE;
        GitStatusScope diff_scope = GitStatusScope::UNSTAGED;
        bool git_diff_side_by_side = false;
    };

    struct SessionPane {
        EditorPaneKind kind = EditorPaneKind::CODE;
        StrRef name = {};
        StrRef path = {};
        StrRef diff_path = {};
        StrRef diff_commit_oid = {};
        SessionView view = {};
        SessionGitDiffKind diff_kind = SessionGitDiffKind::NONE;
        GitStatusScope diff_scope = GitStatusScope::UNSTAGED;
        bool git_diff_side_by_side = false;
    };

    struct SessionSplit {
        EditorSplitKind kind = EditorSplitKind::LEAF;
        size_t parent = static_cast<size_t>(-1);
        size_t first = static_cast<size_t>(-1);
        size_t second = static_cast<size_t>(-1);
        size_t pane = 0u;
        float ratio = 0.5f;
    };

    struct SessionState {
        Vec<SessionOpenFile> open_files = {};
        Vec<SessionPane> panes = {};
        Vec<SessionSplit> splits = {};
        StrRef commit_text = {};
        size_t root_split = 0u;
        size_t focused_split = 0u;
        size_t last_code_split = 0u;
        float sidebar_width_percent = SIDEBAR_DEFAULT_WIDTH_PERCENT;
        EditorSidebarTab sidebar_tab = EditorSidebarTab::FILES;
        bool sidebar_visible = false;
        bool git_staged_open = true;
        bool git_changes_open = true;
        bool git_graph_open = false;
        bool git_branches_open = false;
    };

    [[nodiscard]] auto write_data(std::FILE* file, void const* data, size_t size) -> bool {
        return size == 0u || std::fwrite(data, 1u, size, file) == size;
    }

    [[nodiscard]] auto read_data(std::FILE* file, void* data, size_t size) -> bool {
        return size == 0u || std::fread(data, 1u, size, file) == size;
    }

    [[nodiscard]] auto write_u8(std::FILE* file, uint8_t value) -> bool {
        return write_data(file, &value, sizeof(value));
    }

    [[nodiscard]] auto write_u32(std::FILE* file, uint32_t value) -> bool {
        return write_data(file, &value, sizeof(value));
    }

    [[nodiscard]] auto write_count(std::FILE* file, size_t value) -> bool {
        uint64_t const stored = static_cast<uint64_t>(value);
        return write_data(file, &stored, sizeof(stored));
    }

    [[nodiscard]] auto write_float(std::FILE* file, float value) -> bool {
        return write_data(file, &value, sizeof(value));
    }

    [[nodiscard]] auto write_bool(std::FILE* file, bool value) -> bool {
        return write_u8(file, value ? 1u : 0u);
    }

    [[nodiscard]] auto write_str(std::FILE* file, StrRef value) -> bool {
        return write_count(file, value.size()) && write_data(file, value.data(), value.size());
    }

    [[nodiscard]] auto read_u8(std::FILE* file, uint8_t& out) -> bool {
        return read_data(file, &out, sizeof(out));
    }

    [[nodiscard]] auto read_u32(std::FILE* file, uint32_t& out) -> bool {
        return read_data(file, &out, sizeof(out));
    }

    [[nodiscard]] auto read_count(std::FILE* file, size_t max_value, size_t& out) -> bool {
        uint64_t value = 0u;
        if (!read_data(file, &value, sizeof(value)) || value > static_cast<uint64_t>(max_value)) {
            return false;
        }
        out = static_cast<size_t>(value);
        return true;
    }

    [[nodiscard]] auto read_index(std::FILE* file, size_t max_value, size_t& out) -> bool {
        uint64_t value = 0u;
        if (!read_data(file, &value, sizeof(value))) {
            return false;
        }
        if (value == UINT64_MAX) {
            out = static_cast<size_t>(-1);
            return true;
        }
        if (value > static_cast<uint64_t>(max_value)) {
            return false;
        }
        out = static_cast<size_t>(value);
        return true;
    }

    [[nodiscard]] auto read_float(std::FILE* file, float& out) -> bool {
        return read_data(file, &out, sizeof(out)) && std::isfinite(out);
    }

    [[nodiscard]] auto read_bool(std::FILE* file, bool& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value != 0u;
        return true;
    }

    [[nodiscard]] auto read_git_status_scope(std::FILE* file, GitStatusScope& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(GitStatusScope::UNTRACKED)
                  ? static_cast<GitStatusScope>(value)
                  : GitStatusScope::UNSTAGED;
        return true;
    }

    [[nodiscard]] auto read_session_git_diff_kind(std::FILE* file, SessionGitDiffKind& out)
        -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(SessionGitDiffKind::COMMIT)
                  ? static_cast<SessionGitDiffKind>(value)
                  : SessionGitDiffKind::NONE;
        return true;
    }

    [[nodiscard]] auto read_str(Arena& arena, std::FILE* file, StrRef& out) -> bool {
        size_t size = 0u;
        if (!read_count(file, SESSION_STATE_MAX_TEXT, size)) {
            return false;
        }
        if (size == 0u) {
            out = {};
            return true;
        }
        char* const data = arena_alloc<char>(arena, size + 1u);
        if (!read_data(file, data, size)) {
            return false;
        }
        data[size] = '\0';
        out = StrRef(data, size);
        return true;
    }

    [[nodiscard]] auto read_selection_mode(std::FILE* file, EditorSelectionMode& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(EditorSelectionMode::LINE)
                  ? static_cast<EditorSelectionMode>(value)
                  : EditorSelectionMode::NONE;
        return true;
    }

    [[nodiscard]] auto read_sidebar_tab(std::FILE* file, EditorSidebarTab& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(EditorSidebarTab::GIT)
                  ? static_cast<EditorSidebarTab>(value)
                  : EditorSidebarTab::FILES;
        return true;
    }

    [[nodiscard]] auto read_pane_kind(std::FILE* file, EditorPaneKind& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(EditorPaneKind::FILESYSTEM)
                  ? static_cast<EditorPaneKind>(value)
                  : EditorPaneKind::CODE;
        return true;
    }

    [[nodiscard]] auto read_split_kind(std::FILE* file, EditorSplitKind& out) -> bool {
        uint8_t value = 0u;
        if (!read_u8(file, value)) {
            return false;
        }
        out = value <= static_cast<uint8_t>(EditorSplitKind::HORIZONTAL)
                  ? static_cast<EditorSplitKind>(value)
                  : EditorSplitKind::LEAF;
        return true;
    }

    [[nodiscard]] auto write_cursor(std::FILE* file, EditorCursor const& cursor) -> bool {
        return write_count(file, cursor.line) && write_count(file, cursor.column) &&
               write_count(file, cursor.preferred_column) &&
               write_count(file, cursor.selection_anchor_line) &&
               write_count(file, cursor.selection_anchor_column);
    }

    [[nodiscard]] auto read_cursor(std::FILE* file, EditorCursor& cursor) -> bool {
        return read_count(file, SESSION_STATE_MAX_TEXT, cursor.line) &&
               read_count(file, SESSION_STATE_MAX_TEXT, cursor.column) &&
               read_count(file, SESSION_STATE_MAX_TEXT, cursor.preferred_column) &&
               read_count(file, SESSION_STATE_MAX_TEXT, cursor.selection_anchor_line) &&
               read_count(file, SESSION_STATE_MAX_TEXT, cursor.selection_anchor_column);
    }

    [[nodiscard]] auto write_view(std::FILE* file, SessionViewRef const& view) -> bool {
        if (!write_count(file, view.cursor_line) || !write_count(file, view.cursor_column) ||
            !write_count(file, view.preferred_column) ||
            !write_count(file, view.selection_anchor_line) ||
            !write_count(file, view.selection_anchor_column) ||
            !write_u8(file, static_cast<uint8_t>(view.selection_mode)) ||
            !write_bool(file, view.selection_active) || !write_float(file, view.scroll_x) ||
            !write_float(file, view.scroll_y) || !write_count(file, view.extra_cursors.size())) {
            return false;
        }
        for (EditorCursor const& cursor : view.extra_cursors) {
            if (!write_cursor(file, cursor)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto read_view(Arena& arena, std::FILE* file, SessionView& view) -> bool {
        size_t extra_cursor_count = 0u;
        if (!view.extra_cursors.init(0u, arena.resource()) ||
            !read_count(file, SESSION_STATE_MAX_TEXT, view.cursor_line) ||
            !read_count(file, SESSION_STATE_MAX_TEXT, view.cursor_column) ||
            !read_count(file, SESSION_STATE_MAX_TEXT, view.preferred_column) ||
            !read_count(file, SESSION_STATE_MAX_TEXT, view.selection_anchor_line) ||
            !read_count(file, SESSION_STATE_MAX_TEXT, view.selection_anchor_column) ||
            !read_selection_mode(file, view.selection_mode) ||
            !read_bool(file, view.selection_active) || !read_float(file, view.scroll_x) ||
            !read_float(file, view.scroll_y) ||
            !read_count(file, SESSION_STATE_MAX_COUNT, extra_cursor_count)) {
            return false;
        }
        for (size_t index = 0u; index < extra_cursor_count; ++index) {
            EditorCursor cursor = {};
            if (!read_cursor(file, cursor) || !view.extra_cursors.push_back(cursor)) {
                return false;
            }
        }
        view.scroll_x = std::max(0.0f, view.scroll_x);
        view.scroll_y = std::max(0.0f, view.scroll_y);
        return true;
    }

    [[nodiscard]] auto editor_view_ref(EditorState const& editor) -> SessionViewRef {
        return {
            .cursor_line = editor.cursor_line,
            .cursor_column = editor.cursor_column,
            .preferred_column = editor.preferred_column,
            .extra_cursors = editor.extra_cursors.slice(),
            .selection_anchor_line = editor.selection_anchor_line,
            .selection_anchor_column = editor.selection_anchor_column,
            .selection_mode = editor.selection_mode,
            .scroll_x = editor.scroll_x,
            .scroll_y = editor.scroll_y,
            .selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE),
        };
    }

    [[nodiscard]] auto open_file_view_ref(OpenFile const& file) -> SessionViewRef {
        return {
            .cursor_line = file.cursor_line,
            .cursor_column = file.cursor_column,
            .preferred_column = file.preferred_column,
            .extra_cursors = file.extra_cursors.slice(),
            .selection_anchor_line = file.selection_anchor_line,
            .selection_anchor_column = file.selection_anchor_column,
            .selection_mode = file.selection_mode,
            .scroll_x = file.scroll_x,
            .scroll_y = file.scroll_y,
            .selection_active = file.selection_active,
        };
    }

    [[nodiscard]] auto pane_view_ref(EditorPane const& pane) -> SessionViewRef {
        return {
            .cursor_line = pane.cursor_line,
            .cursor_column = pane.cursor_column,
            .preferred_column = pane.preferred_column,
            .extra_cursors = pane.extra_cursors.slice(),
            .selection_anchor_line = pane.selection_anchor_line,
            .selection_anchor_column = pane.selection_anchor_column,
            .selection_mode = pane.selection_mode,
            .scroll_x = pane.scroll_x,
            .scroll_y = pane.scroll_y,
            .selection_active = pane.selection_active,
        };
    }

    [[nodiscard]] auto
    same_session_file(StrRef lhs_name, StrRef lhs_path, StrRef rhs_name, StrRef rhs_path) -> bool {
        if (!lhs_path.empty() || !rhs_path.empty()) {
            return lhs_path == rhs_path;
        }
        return lhs_name == rhs_name;
    }

    [[nodiscard]] auto session_git_diff_virtual_path(StrRef path) -> bool {
        return path.starts_with("gitdiff:");
    }

    auto fill_session_git_diff(
        StrRef title,
        StrRef path,
        SessionGitDiffKind& kind,
        GitStatusScope& scope,
        StrRef& diff_path,
        StrRef& commit_oid
    ) -> void {
        kind = SessionGitDiffKind::NONE;
        scope = GitStatusScope::UNSTAGED;
        diff_path = {};
        commit_oid = {};
        if (!session_git_diff_virtual_path(path)) {
            return;
        }

        size_t const separator = title.find(' ');
        if (separator == StrRef::NPOS || separator + 1u >= title.size()) {
            return;
        }

        StrRef const head = title.prefix(separator);
        StrRef const rest = title.drop_prefix(separator + 1u);
        if (head == git_status_scope_label(GitStatusScope::STAGED)) {
            kind = SessionGitDiffKind::STATUS;
            scope = GitStatusScope::STAGED;
            diff_path = rest;
        } else if (head == git_status_scope_label(GitStatusScope::UNTRACKED)) {
            kind = SessionGitDiffKind::STATUS;
            scope = GitStatusScope::UNTRACKED;
            diff_path = rest;
        } else if (head == git_status_scope_label(GitStatusScope::UNSTAGED)) {
            kind = SessionGitDiffKind::STATUS;
            scope = GitStatusScope::UNSTAGED;
            diff_path = rest;
        } else {
            kind = SessionGitDiffKind::COMMIT;
            commit_oid = head;
            diff_path = rest;
        }
    }

    [[nodiscard]] auto
    session_git_diff_side_by_side(EditorState const& editor, OpenFile const& file) -> bool {
        return same_session_file(
                   editor.current_file_name, editor.current_file_path, file.name, file.path
               )
                   ? editor.git_diff_side_by_side
                   : file.git_diff_side_by_side;
    }

    [[nodiscard]] auto session_open_file_view(EditorState const& editor, OpenFile const& file)
        -> SessionViewRef {
        if (same_session_file(
                editor.current_file_name, editor.current_file_path, file.name, file.path
            )) {
            return editor_view_ref(editor);
        }
        return open_file_view_ref(file);
    }

    [[nodiscard]] auto write_session_open_files(std::FILE* file, EditorState const& editor)
        -> bool {
        if (!write_count(file, editor.open_files.size())) {
            return false;
        }
        for (OpenFile const& open_file : editor.open_files) {
            SessionGitDiffKind diff_kind = SessionGitDiffKind::NONE;
            GitStatusScope diff_scope = GitStatusScope::UNSTAGED;
            StrRef diff_path = {};
            StrRef diff_commit_oid = {};
            fill_session_git_diff(
                open_file.name, open_file.path, diff_kind, diff_scope, diff_path, diff_commit_oid
            );
            if (!write_str(file, open_file.name) || !write_str(file, open_file.path) ||
                !write_view(file, session_open_file_view(editor, open_file)) ||
                !write_u8(file, static_cast<uint8_t>(diff_kind)) ||
                !write_u8(file, static_cast<uint8_t>(diff_scope)) ||
                !write_bool(file, session_git_diff_side_by_side(editor, open_file)) ||
                !write_str(file, diff_path) || !write_str(file, diff_commit_oid)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto write_session_panes(std::FILE* file, EditorState const& editor) -> bool {
        size_t const focused_pane = editor_focused_pane(editor);
        if (!write_count(file, editor.panes.size())) {
            return false;
        }
        for (size_t index = 0u; index < editor.panes.size(); ++index) {
            EditorPane const* const pane = editor.panes[index];
            bool const active = index == focused_pane && editor.flag(EditorFlag::PANE_LOADED);
            EditorPaneKind const kind = active
                                            ? editor_focused_pane_kind(editor)
                                            : (pane != nullptr ? pane->kind : EditorPaneKind::CODE);
            StrRef const name = active ? editor.current_file_name
                                       : (pane != nullptr ? pane->current_file_name : StrRef());
            StrRef const path = active ? editor.current_file_path
                                       : (pane != nullptr ? pane->current_file_path : StrRef());
            SessionViewRef const view =
                active ? editor_view_ref(editor)
                       : (pane != nullptr ? pane_view_ref(*pane) : SessionViewRef{});
            SessionGitDiffKind diff_kind = SessionGitDiffKind::NONE;
            GitStatusScope diff_scope = GitStatusScope::UNSTAGED;
            StrRef diff_path = {};
            StrRef diff_commit_oid = {};
            fill_session_git_diff(name, path, diff_kind, diff_scope, diff_path, diff_commit_oid);
            bool const side_by_side = active
                                          ? editor.git_diff_side_by_side
                                          : (pane != nullptr ? pane->git_diff_side_by_side : false);
            if (!write_u8(file, static_cast<uint8_t>(kind)) || !write_str(file, name) ||
                !write_str(file, path) || !write_view(file, view) ||
                !write_u8(file, static_cast<uint8_t>(diff_kind)) ||
                !write_u8(file, static_cast<uint8_t>(diff_scope)) ||
                !write_bool(file, side_by_side) || !write_str(file, diff_path) ||
                !write_str(file, diff_commit_oid)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto write_session_splits(std::FILE* file, EditorState const& editor) -> bool {
        if (!write_count(file, editor.split_nodes.size())) {
            return false;
        }
        for (EditorSplitNode const& split : editor.split_nodes) {
            if (!write_u8(file, static_cast<uint8_t>(split.kind)) ||
                !write_count(file, split.parent) || !write_count(file, split.first) ||
                !write_count(file, split.second) || !write_count(file, split.pane) ||
                !write_float(file, split.ratio)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto save_editor_session_state(EditorState const& editor, StrRef state_cache_path)
        -> bool {
        if (state_cache_path.empty()) {
            return false;
        }
        std::FILE* const file = open_session_write_file(state_cache_path);
        if (file == nullptr) {
            return false;
        }
        bool ok =
            write_u32(file, SESSION_STATE_MAGIC) && write_u32(file, SESSION_STATE_VERSION) &&
            write_bool(file, editor.flag(EditorFlag::SIDEBAR_VISIBLE)) &&
            write_u8(file, static_cast<uint8_t>(editor.sidebar_tab)) &&
            write_float(file, editor.sidebar_width_percent) &&
            write_bool(file, editor.git_staged_open) && write_bool(file, editor.git_changes_open) &&
            write_bool(file, editor.git_graph_open) && write_bool(file, editor.git_branches_open) &&
            write_str(file, editor.git_commit_text.str()) && write_count(file, editor.root_split) &&
            write_count(file, editor.focused_split) && write_count(file, editor.last_code_split) &&
            write_session_open_files(file, editor) && write_session_panes(file, editor) &&
            write_session_splits(file, editor);
        std::fclose(file);
        return ok;
    }

    [[nodiscard]] auto read_session_state(Arena& arena, StrRef path, SessionState& state) -> bool {
        if (path.empty() || !editor_path_exists(path)) {
            return false;
        }
        std::FILE* const file = open_session_read_file(path);
        if (file == nullptr) {
            return false;
        }

        uint32_t magic = 0u;
        uint32_t version = 0u;
        bool ok =
            read_u32(file, magic) && read_u32(file, version) && magic == SESSION_STATE_MAGIC &&
            version >= 1u && version <= SESSION_STATE_VERSION &&
            read_bool(file, state.sidebar_visible) && read_sidebar_tab(file, state.sidebar_tab) &&
            read_float(file, state.sidebar_width_percent) &&
            read_bool(file, state.git_staged_open) && read_bool(file, state.git_changes_open) &&
            read_bool(file, state.git_graph_open) && read_bool(file, state.git_branches_open) &&
            read_str(arena, file, state.commit_text) &&
            read_count(file, SESSION_STATE_MAX_COUNT, state.root_split) &&
            read_count(file, SESSION_STATE_MAX_COUNT, state.focused_split) &&
            read_count(file, SESSION_STATE_MAX_COUNT, state.last_code_split);

        size_t open_file_count = 0u;
        ok = ok && state.open_files.init(0u, arena.resource()) &&
             read_count(file, SESSION_STATE_MAX_COUNT, open_file_count);
        for (size_t index = 0u; ok && index < open_file_count; ++index) {
            SessionOpenFile item = {};
            ok = read_str(arena, file, item.name) && read_str(arena, file, item.path) &&
                 read_view(arena, file, item.view);
            if (ok && version >= 2u) {
                ok = read_session_git_diff_kind(file, item.diff_kind) &&
                     read_git_status_scope(file, item.diff_scope) &&
                     read_bool(file, item.git_diff_side_by_side) &&
                     read_str(arena, file, item.diff_path) &&
                     read_str(arena, file, item.diff_commit_oid);
            } else if (ok) {
                fill_session_git_diff(
                    item.name,
                    item.path,
                    item.diff_kind,
                    item.diff_scope,
                    item.diff_path,
                    item.diff_commit_oid
                );
            }
            ok = ok && state.open_files.push_back(item);
        }

        size_t pane_count = 0u;
        ok = ok && state.panes.init(0u, arena.resource()) &&
             read_count(file, SESSION_STATE_MAX_COUNT, pane_count);
        for (size_t index = 0u; ok && index < pane_count; ++index) {
            SessionPane pane = {};
            ok = read_pane_kind(file, pane.kind) && read_str(arena, file, pane.name) &&
                 read_str(arena, file, pane.path) && read_view(arena, file, pane.view);
            if (ok && version >= 2u) {
                ok = read_session_git_diff_kind(file, pane.diff_kind) &&
                     read_git_status_scope(file, pane.diff_scope) &&
                     read_bool(file, pane.git_diff_side_by_side) &&
                     read_str(arena, file, pane.diff_path) &&
                     read_str(arena, file, pane.diff_commit_oid);
            } else if (ok) {
                fill_session_git_diff(
                    pane.name,
                    pane.path,
                    pane.diff_kind,
                    pane.diff_scope,
                    pane.diff_path,
                    pane.diff_commit_oid
                );
            }
            ok = ok && state.panes.push_back(pane);
        }

        size_t split_count = 0u;
        ok = ok && state.splits.init(0u, arena.resource()) &&
             read_count(file, SESSION_STATE_MAX_COUNT, split_count);
        for (size_t index = 0u; ok && index < split_count; ++index) {
            SessionSplit split = {};
            ok = read_split_kind(file, split.kind) &&
                 read_index(file, SESSION_STATE_MAX_COUNT, split.parent) &&
                 read_index(file, SESSION_STATE_MAX_COUNT, split.first) &&
                 read_index(file, SESSION_STATE_MAX_COUNT, split.second) &&
                 read_count(file, SESSION_STATE_MAX_COUNT, split.pane) &&
                 read_float(file, split.ratio) && state.splits.push_back(split);
        }
        std::fclose(file);

        state.sidebar_width_percent = std::clamp(
            state.sidebar_width_percent, SIDEBAR_MIN_WIDTH_PERCENT, SIDEBAR_MAX_WIDTH_PERCENT
        );
        return ok;
    }

    [[nodiscard]] auto find_session_open_file(EditorState& editor, StrRef name, StrRef path)
        -> OpenFile* {
        for (OpenFile& file : editor.open_files) {
            if (same_session_file(file.name, file.path, name, path)) {
                return &file;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto session_git_root(EditorState& editor, Arena& arena, StrRef& root) -> bool {
        if (!editor.git_root_path.empty()) {
            root = editor.git_root_path;
            return true;
        }
        if (editor.arena == nullptr || editor.save_root_path.empty()) {
            editor.git_root_checked = true;
            return false;
        }

        StrRef message = {};
        if (!git_discover_root(arena, editor.save_root_path, root, message)) {
            editor.git_root_checked = true;
            return false;
        }
        editor.git_root_path = arena_copy_cstr(*editor.arena, root);
        editor.git_root_checked = true;
        root = editor.git_root_path;
        return true;
    }

    [[nodiscard]] auto
    session_status_diff_exists(Arena& arena, StrRef root, GitStatusScope scope, StrRef path)
        -> bool {
        Vec<GitStatusItem> items = {};
        StrRef message = {};
        if (!items.init(0u, arena.resource()) ||
            !git_load_status_path(arena, root, path, items, message)) {
            return false;
        }
        for (GitStatusItem const& item : items) {
            if (item.scope == scope && item.path == path) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto session_git_diff_title(
        Arena& arena, SessionGitDiffKind kind, GitStatusScope scope, StrRef path, StrRef commit_oid
    ) -> StrRef {
        StrRef const prefix =
            kind == SessionGitDiffKind::STATUS ? git_status_scope_label(scope) : commit_oid;
        StringBuffer title = {};
        title.init(prefix.size() + path.size() + 2u, arena.resource());
        title.write_string(prefix);
        title.write_byte(' ');
        title.write_string(path);
        return title.str();
    }

    auto sync_session_current_open_file(EditorState& editor) -> void {
        if (editor.text.arena == nullptr) {
            return;
        }
        OpenFile* const file =
            find_session_open_file(editor, editor.current_file_name, editor.current_file_path);
        if (file == nullptr) {
            return;
        }
        file->text = text_buffer_copy(editor.text, *editor.text.arena);
        file->saved_text = editor.saved_text;
        file->git_diff = editor.git_diff;
        file->text_valid = true;
        file->git_diff_side_by_side = editor.git_diff_side_by_side;
        file->view_kind = editor.view_kind;
    }

    [[nodiscard]] auto open_session_git_diff(
        EditorState& editor,
        SessionGitDiffKind kind,
        GitStatusScope scope,
        StrRef path,
        StrRef commit_oid,
        StrRef title,
        bool side_by_side
    ) -> bool {
        if (kind == SessionGitDiffKind::NONE || path.empty()) {
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        StrRef root = {};
        if (!session_git_root(editor, *temp.arena(), root)) {
            return false;
        }

        StrRef patch = {};
        StrRef message = {};
        bool ok = false;
        if (kind == SessionGitDiffKind::STATUS) {
            if (!session_status_diff_exists(*temp.arena(), root, scope, path)) {
                return false;
            }
            ok = git_status_patch(*temp.arena(), root, scope, path, patch, message);
        } else {
            ok = !commit_oid.empty() &&
                 git_commit_patch(*temp.arena(), root, commit_oid, path, patch, message);
        }
        if (!ok || patch.empty()) {
            return false;
        }

        StrRef const open_title =
            title.empty() ? session_git_diff_title(*temp.arena(), kind, scope, path, commit_oid)
                          : title;
        open_git_diff(editor, open_title, patch);
        if (editor.git_diff_side_by_side != side_by_side) {
            editor.git_diff_side_by_side = side_by_side;
            set_git_diff_view_text(editor);
            sync_session_current_open_file(editor);
        }
        return true;
    }

    auto apply_view(OpenFile& file, SessionView const& view, MemoryResource* resource) -> void {
        file.cursor_line = view.cursor_line;
        file.cursor_column = view.cursor_column;
        file.preferred_column = view.preferred_column;
        BASE_UNUSED(file.extra_cursors.copy_from(view.extra_cursors, resource));
        file.selection_anchor_line = view.selection_anchor_line;
        file.selection_anchor_column = view.selection_anchor_column;
        file.selection_mode = view.selection_mode;
        file.scroll_x = view.scroll_x;
        file.scroll_y = view.scroll_y;
        file.selection_active = view.selection_active;
    }

    auto clamp_view_to_text(SessionView& view, EditorText const& text) -> void {
        size_t const line_count = std::max<size_t>(1u, text_buffer_line_count(text));
        view.cursor_line = std::min(view.cursor_line, line_count - 1u);
        view.cursor_column =
            std::min(view.cursor_column, text_buffer_line_size(text, view.cursor_line));
        view.selection_anchor_line = std::min(view.selection_anchor_line, line_count - 1u);
        view.selection_anchor_column = std::min(
            view.selection_anchor_column, text_buffer_line_size(text, view.selection_anchor_line)
        );
        for (EditorCursor& cursor : view.extra_cursors) {
            cursor.line = std::min(cursor.line, line_count - 1u);
            cursor.column = std::min(cursor.column, text_buffer_line_size(text, cursor.line));
            cursor.selection_anchor_line = std::min(cursor.selection_anchor_line, line_count - 1u);
            cursor.selection_anchor_column = std::min(
                cursor.selection_anchor_column,
                text_buffer_line_size(text, cursor.selection_anchor_line)
            );
        }
    }

    auto apply_view(EditorPane& pane, SessionView& view, MemoryResource* resource) -> void {
        clamp_view_to_text(view, pane.text);
        pane.cursor_line = view.cursor_line;
        pane.cursor_column = view.cursor_column;
        pane.preferred_column = view.preferred_column;
        BASE_UNUSED(pane.extra_cursors.copy_from(view.extra_cursors, resource));
        pane.selection_anchor_line = view.selection_anchor_line;
        pane.selection_anchor_column = view.selection_anchor_column;
        pane.selection_mode = view.selection_mode;
        pane.scroll_x = view.scroll_x;
        pane.scroll_y = view.scroll_y;
        pane.selection_active = view.selection_active;
    }

    auto init_session_pane(EditorPane& pane, Arena& arena) -> void {
        text_buffer_init(pane.text, arena);
        BASE_UNUSED(pane.extra_cursors.init(0u, arena.resource()));
        BASE_UNUSED(pane.folded_ranges.init(0u, arena.resource()));
        BASE_UNUSED(pane.open_file_views.init(0u, arena.resource()));
    }

    [[nodiscard]] auto
    clone_editor_to_session_pane(EditorState& editor, SessionPane& state, EditorPane*& out_pane)
        -> bool {
        DEBUG_ASSERT(editor.arena != nullptr);
        EditorPane* const pane = arena_new<EditorPane>(*editor.arena);
        *pane = {};
        init_session_pane(*pane, *editor.arena);
        pane->kind = state.kind;
        bool loaded = state.kind != EditorPaneKind::CODE;
        if (state.kind == EditorPaneKind::CODE) {
            if (state.diff_kind != SessionGitDiffKind::NONE) {
                loaded = open_session_git_diff(
                    editor,
                    state.diff_kind,
                    state.diff_scope,
                    state.diff_path,
                    state.diff_commit_oid,
                    state.name,
                    state.git_diff_side_by_side
                );
            } else if (!state.path.empty() && !session_git_diff_virtual_path(state.path)) {
                loaded = editor_open_path(editor, state.path);
            }
        }
        if (loaded) {
            text_buffer_clone(editor.text, pane->text, *editor.arena);
            pane->git_diff = editor.git_diff;
            pane->current_file_name = editor.current_file_name;
            pane->current_file_path = editor.current_file_path;
            pane->saved_text = editor.saved_text;
            pane->file_write_stamp = editor.file_write_stamp;
            pane->git_diff_side_by_side = editor.git_diff_side_by_side;
            pane->view_kind = editor.view_kind;
            if (!state.name.empty()) {
                pane->current_file_name = arena_copy_cstr(*editor.arena, state.name);
            }
            if (!state.path.empty()) {
                pane->current_file_path = arena_copy_cstr(*editor.arena, state.path);
            }
        }
        apply_view(*pane, state.view, editor.arena->resource());
        out_pane = pane;
        return true;
    }

    auto clone_session_pane_to_editor(EditorState& editor, EditorPane const& pane) -> void {
        text_buffer_clone(pane.text, editor.text, *editor.arena);
        editor.git_diff = pane.git_diff;
        editor.current_file_name = pane.current_file_name;
        editor.current_file_path = pane.current_file_path;
        editor.saved_text = pane.saved_text;
        editor.undo_stack = nullptr;
        editor.redo_stack = nullptr;
        editor.file_write_stamp = pane.file_write_stamp;
        editor.cursor_line = pane.cursor_line;
        editor.cursor_column = pane.cursor_column;
        editor.preferred_column = pane.preferred_column;
        BASE_UNUSED(editor.extra_cursors.copy_from(pane.extra_cursors, editor.arena->resource()));
        editor.selection_anchor_line = pane.selection_anchor_line;
        editor.selection_anchor_column = pane.selection_anchor_column;
        editor.selection_mode = pane.selection_mode;
        editor.scroll_x = pane.scroll_x;
        editor.scroll_y = pane.scroll_y;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, pane.selection_active);
        editor.set_flag(EditorFlag::DIRTY, false);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        editor.git_diff_side_by_side = pane.git_diff_side_by_side;
        editor.view_kind = pane.view_kind;
    }

    [[nodiscard]] auto session_split_tree_valid(SessionState const& state) -> bool {
        if (state.splits.empty() || state.panes.empty() ||
            state.root_split >= state.splits.size() || state.focused_split >= state.splits.size()) {
            return false;
        }
        for (SessionSplit const& split : state.splits) {
            if (split.kind == EditorSplitKind::LEAF) {
                if (split.pane >= state.panes.size()) {
                    return false;
                }
            } else if (split.first >= state.splits.size() || split.second >= state.splits.size()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto editor_has_filesystem_pane(EditorState const& editor) -> bool {
        for (EditorSplitNode const& split : editor.split_nodes) {
            if (split.kind == EditorSplitKind::LEAF && split.pane < editor.panes.size() &&
                editor.panes[split.pane] != nullptr &&
                editor.panes[split.pane]->kind == EditorPaneKind::FILESYSTEM) {
                return true;
            }
        }
        return false;
    }

    auto apply_session_open_files(EditorState& editor, SessionState& state) -> void {
        for (SessionOpenFile& file : state.open_files) {
            bool opened = false;
            if (file.diff_kind != SessionGitDiffKind::NONE) {
                opened = open_session_git_diff(
                    editor,
                    file.diff_kind,
                    file.diff_scope,
                    file.diff_path,
                    file.diff_commit_oid,
                    file.name,
                    file.git_diff_side_by_side
                );
            } else if (!file.path.empty() && !session_git_diff_virtual_path(file.path)) {
                opened = editor_open_path(editor, file.path);
            }
            if (!opened) {
                continue;
            }
            OpenFile* const open = find_session_open_file(editor, file.name, file.path);
            if (open != nullptr) {
                apply_view(*open, file.view, editor.arena->resource());
            }
        }
    }

    auto apply_session_open_file_views(EditorState& editor, SessionState& state) -> void {
        for (SessionOpenFile& file : state.open_files) {
            OpenFile* const open = find_session_open_file(editor, file.name, file.path);
            if (open != nullptr) {
                apply_view(*open, file.view, editor.arena->resource());
            }
        }
    }

    auto restore_session_open_file_order(EditorState& editor, SessionState& state) -> void {
        Vec<OpenFile> ordered = {};
        if (!ordered.init(state.open_files.size(), editor.arena->resource())) {
            return;
        }
        for (SessionOpenFile const& file : state.open_files) {
            OpenFile const* const open = find_session_open_file(editor, file.name, file.path);
            if (open != nullptr) {
                BASE_UNUSED(ordered.push_back(*open));
            }
        }
        editor.open_files = ordered;
    }

    auto apply_session_split_tree(EditorState& editor, SessionState& state) -> void {
        if (!session_split_tree_valid(state)) {
            set_filesystem_panel_visible(editor, state.sidebar_visible);
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        Vec<EditorPane*> panes = {};
        if (!panes.init(0u, temp.arena()->resource())) {
            return;
        }
        for (SessionPane& pane_state : state.panes) {
            EditorPane* pane = nullptr;
            if (!clone_editor_to_session_pane(editor, pane_state, pane) || !panes.push_back(pane)) {
                return;
            }
        }

        editor.panes.clear();
        for (EditorPane* pane : panes) {
            BASE_UNUSED(editor.panes.push_back(pane));
        }
        editor.split_nodes.clear();
        for (SessionSplit const& split : state.splits) {
            BASE_UNUSED(editor.split_nodes.push_back({
                .kind = split.kind,
                .parent = split.parent,
                .first = split.first,
                .second = split.second,
                .pane = split.pane,
                .ratio = std::clamp(split.ratio, 0.08f, 0.92f),
            }));
        }
        editor.root_split = state.root_split;
        editor.focused_split = state.focused_split;
        editor.last_code_split =
            state.last_code_split < editor.split_nodes.size() ? state.last_code_split : 0u;
        editor.set_flag(EditorFlag::PANE_LOADED, false);
        size_t const pane_index = editor.split_nodes[editor.focused_split].pane;
        if (pane_index < editor.panes.size() && editor.panes[pane_index] != nullptr) {
            clone_session_pane_to_editor(editor, *editor.panes[pane_index]);
            editor.set_flag(EditorFlag::PANE_LOADED, true);
        }
        editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, editor_has_filesystem_pane(editor));
    }

    auto apply_editor_session_state(EditorState& editor, SessionState& state) -> void {
        editor.sidebar_width_percent = state.sidebar_width_percent;
        editor.sidebar_tab = state.sidebar_tab;
        editor.git_staged_open = state.git_staged_open;
        editor.git_changes_open = state.git_changes_open;
        editor.git_graph_open = state.git_graph_open;
        editor.git_branches_open = state.git_branches_open;
        editor.git_commit_text.reset();
        BASE_UNUSED(editor.git_commit_text.write_string(state.commit_text));

        apply_session_open_files(editor, state);
        apply_session_split_tree(editor, state);
        apply_session_open_file_views(editor, state);
        restore_session_open_file_order(editor, state);

        if (editor.flag(EditorFlag::SIDEBAR_VISIBLE) &&
            editor.sidebar_tab == EditorSidebarTab::GIT) {
            editor.git_refresh_requested = true;
            editor.git_log_refresh_requested = editor.git_graph_open;
        }
    }

    [[nodiscard]] auto load_editor_session_state(EditorState& editor, StrRef state_cache_path)
        -> bool {
        ArenaTemp temp = begin_thread_temp_arena();
        SessionState state = {};
        if (!read_session_state(*temp.arena(), state_cache_path, state)) {
            return false;
        }
        apply_editor_session_state(editor, state);
        return true;
    }

} // namespace code_editor
