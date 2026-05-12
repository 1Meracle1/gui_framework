#include "editor_render.h"

#include "git.h"
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
#include <base/string_buffer.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace code_editor {

    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;

    inline constexpr float TREE_INDENT_WIDTH = 16.0f;
    inline constexpr float TREE_ARROW_SLOT_WIDTH = 16.0f;
    inline constexpr float TREE_PANEL_PADDING_X = 10.0f;
    inline constexpr float TREE_FILE_LABEL_PADDING_X = 12.0f;
    inline constexpr float TREE_FOLDER_LABEL_PADDING_X = 2.0f;
    inline constexpr float SIDEBAR_RESIZER_WIDTH = 10.0f;
    inline constexpr float EDITOR_SPLIT_GAP = 6.0f;
    inline constexpr float EDITOR_SPLIT_MIN_RATIO = 0.08f;
    inline constexpr float EDITOR_SPLIT_MAX_RATIO = 0.92f;
    inline constexpr float INLAY_HINT_PADDING_X = 4.0f;
    inline constexpr float INLAY_HINT_PADDING_Y = 2.0f;
    inline constexpr float INLAY_HINT_RADIUS = 3.0f;

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
    inline constexpr size_t TEXT_DRAW_CHUNK_SIZE = 4096u;
    inline constexpr float COMMAND_OVERLAY_HEIGHT = 88.0f;
    inline constexpr float COMMAND_LIST_HEIGHT = 30.0f;
    inline constexpr size_t STICKY_SCOPE_MAX_LINES = 5u;
    inline constexpr char OVERWRITE_FILE_KEY = 'o';
    inline constexpr char RELOAD_FILE_KEY = 'r';
    inline constexpr char CLOSE_WITHOUT_SAVE_KEY = 'c';
    inline constexpr char SAVE_CHANGES_KEY = 's';
    inline constexpr size_t GIT_DIFF_CELL_LIMIT = 1024u * 1024u;
#if defined(_WIN32)
    inline constexpr char GIT_STDERR_REDIRECT[] = "2>nul";
#else
    inline constexpr char GIT_STDERR_REDIRECT[] = "2>/dev/null";
#endif
    inline constexpr char TREE_ARROW_OPEN[] = "\xEE\x9C\x8D";
    inline constexpr char TREE_ARROW_CLOSED[] = "\xEE\x9D\xAC";
    inline constexpr char GIT_BRANCH_ICON[] = "\xEE\xA9\xA8";
    inline constexpr char GIT_REFRESH_ICON[] = "\xEE\x9C\xAC";
    inline constexpr char GIT_FETCH_ICON[] = "\xE2\x86\x93";
    inline constexpr float GIT_PANEL_PADDING_Y = 6.0f;
    inline constexpr float GIT_BRANCH_LIST_TOP = 30.0f;
    inline constexpr float GIT_BRANCH_ROW_HEIGHT = 24.0f;
    inline constexpr float GIT_ROW_HEIGHT = 24.0f;
    inline constexpr float GIT_GRAPH_LANE_WIDTH = 8.0f;
    inline constexpr float GIT_GRAPH_MIN_WIDTH = 22.0f;
    inline constexpr float GIT_GRAPH_MAX_WIDTH = 138.0f;
    inline constexpr float GIT_GRAPH_NODE_RADIUS = 3.2f;
    inline constexpr float GIT_GRAPH_LINE_THICKNESS = 1.6f;
    inline constexpr size_t GIT_GRAPH_OVERSCAN_ROWS = 48u;
    inline constexpr size_t GIT_PANEL_FIXED_SCROLL_ROWS = 5u;
    inline constexpr float GIT_CURSOR_REVEAL_MARGIN = GIT_ROW_HEIGHT;
    inline constexpr float GIT_COMMIT_POPUP_MIN_WIDTH = 420.0f;
    inline constexpr float GIT_COMMIT_POPUP_MAX_WIDTH = 560.0f;
    inline constexpr size_t GIT_COMMIT_POPUP_MAX_BODY_LINES = 8u;
    inline constexpr StrRef GIT_GRAPH_BOX_DEBUG_NAME = "git_commit_graph";
    inline constexpr float GIT_BRANCH_ROW_PADDING_X = 7.0f;
    inline constexpr float GIT_BRANCH_ROW_GAP = 2.0f;
    inline constexpr float GIT_BRANCH_LIST_PADDING = 3.0f;

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

    [[nodiscard]] auto render_path_parent(StrRef path) -> StrRef {
        path = render_path_without_trailing_slash(path);
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

    [[nodiscard]] auto open_process_read(StrRef command) -> std::FILE* {
#if defined(_WIN32)
        return _popen(command.data(), "rb");
#else
        return popen(command.data(), "r");
#endif
    }

    [[nodiscard]] auto close_process_read(std::FILE* pipe) -> int {
#if defined(_WIN32)
        return _pclose(pipe);
#else
        return pclose(pipe);
#endif
    }

    [[nodiscard]] auto read_process_output(Arena& arena, StrRef command, StrRef& out_text) -> bool {
        out_text = {};
        std::FILE* const pipe = open_process_read(command);
        if (pipe == nullptr) {
            return false;
        }

        StringBuffer buffer = {};
        bool ok = buffer.init(4096u, arena.resource());
        char bytes[4096] = {};
        while (ok) {
            size_t const read = std::fread(bytes, 1u, sizeof(bytes), pipe);
            if (read != 0u && buffer.write_bytes(bytes, read) != read) {
                ok = false;
            }
            if (read < sizeof(bytes)) {
                ok = ok && std::feof(pipe) != 0;
                break;
            }
        }

        ok = close_process_read(pipe) == 0 && ok;
        out_text = ok ? buffer.str() : StrRef();
        return ok;
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

    [[nodiscard]] auto trim_render_tree_path(StrRef path) -> StrRef {
        while (path.size() > 1u && (path.back() == '\\' || path.back() == '/') &&
               !(path.size() == 3u && path[1u] == ':')) {
            path.remove_suffix(1u);
        }
        return path;
    }

    [[nodiscard]] auto path_matches_or_contains(StrRef path, StrRef prefix) -> bool {
        prefix = trim_render_tree_path(prefix);
        if (prefix.empty() || path.size() < prefix.size() ||
            !path.starts_with_ignore_ascii_case(prefix)) {
            return false;
        }
        return path.size() == prefix.size() || path[prefix.size()] == '\\' ||
               path[prefix.size()] == '/';
    }

    [[nodiscard]] auto
    replace_path_prefix(Arena& arena, StrRef path, StrRef old_prefix, StrRef new_prefix) -> StrRef {
        if (!path_matches_or_contains(path, old_prefix)) {
            return path;
        }
        old_prefix = trim_render_tree_path(old_prefix);
        new_prefix = trim_render_tree_path(new_prefix);
        StrRef const suffix =
            path.size() == old_prefix.size() ? StrRef{} : path.substr(old_prefix.size());
        char buffer[TREE_OPERATION_PATH_CAPACITY] = {};
        size_t size = 0u;
        if (new_prefix.size() >= sizeof(buffer) ||
            !append_path_buffer(buffer, sizeof(buffer), size, new_prefix) ||
            !append_path_buffer(buffer, sizeof(buffer), size, suffix)) {
            return path;
        }
        return arena_copy_cstr(arena, StrRef(buffer, size));
    }

    [[nodiscard]] auto tree_entry_index_by_path(EditorState const& editor, StrRef path) -> size_t {
        for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
            if (editor.tree_files[index].path.equals_ignore_ascii_case(path)) {
                return index;
            }
        }
        return TREE_CURSOR_ROOT;
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

    [[nodiscard]] auto ensure_git_root(EditorState& editor) -> bool {
        return editor.git_root_checked && !editor.git_root_path.empty();
    }

    [[nodiscard]] auto git_relative_path(Arena& arena, StrRef root, StrRef path) -> StrRef {
        StrRef const relative = render_workspace_relative_path(root, path);
        if (relative.empty() || (relative == path && render_path_is_absolute(path))) {
            return {};
        }

        char* const text = arena_alloc<char>(arena, relative.size() + 1u);
        for (size_t index = 0u; index < relative.size(); ++index) {
            char const ch = relative[index];
            text[index] = ch == '\\' ? '/' : ch;
        }
        text[relative.size()] = '\0';
        return StrRef(text, relative.size());
    }

    auto reset_git_line_change_cache(EditorState& editor, OpenFile& file) -> void {
        file.git_path = arena_copy_cstr(*editor.arena, file.path);
        file.git_relative_path = git_relative_path(*editor.arena, editor.git_root_path, file.path);
        file.git_head_text = {};
        file.git_line_changes.clear();
        file.git_line_change_revision = 0u;
        file.git_head_loaded = false;
    }

    [[nodiscard]] auto ensure_git_line_change_storage(EditorState& editor, OpenFile& file) -> bool {
        if (file.git_line_changes.resource() != nullptr) {
            return true;
        }
        return file.git_line_changes.init(0u, editor.arena->resource());
    }

    auto load_git_head_text(EditorState& editor, OpenFile& file) -> void {
        if (file.git_head_loaded || file.git_relative_path.empty()) {
            return;
        }

        file.git_head_loaded = true;
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" show HEAD:\"%s\" %s",
            editor.git_root_path,
            file.git_relative_path,
            GIT_STDERR_REDIRECT
        );
        StrRef text = {};
        if (read_process_output(*editor.arena, command, text)) {
            file.git_head_text = editor_display_text(*editor.arena, text);
        }
    }

    struct GitTextLine {
        StrRef text = {};
        uint64_t hash = 0u;
    };

    [[nodiscard]] auto git_line_hash(StrRef text) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        for (size_t index = 0u; index < text.size(); ++index) {
            hash ^= static_cast<uint8_t>(text[index]);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] auto git_lines_equal(GitTextLine const& lhs, GitTextLine const& rhs) -> bool {
        return lhs.hash == rhs.hash && lhs.text == rhs.text;
    }

    [[nodiscard]] auto push_git_text_line(Vec<GitTextLine>& lines, StrRef text) -> bool {
        return lines.push_back({text, git_line_hash(text)});
    }

    [[nodiscard]] auto collect_git_text_lines(StrRef text, Vec<GitTextLine>& lines) -> bool {
        for (StrRef const line : text.lines()) {
            if (!push_git_text_line(lines, line)) {
                return false;
            }
        }
        return true;
    }

    auto push_git_line_change(
        OpenFile& file, EditorGitLineChangeKind kind, size_t line, size_t current_line_count
    ) -> void {
        bool const after_line = kind == EditorGitLineChangeKind::REMOVED &&
                                current_line_count != 0u && line >= current_line_count;
        size_t const marker_line =
            current_line_count == 0u ? 0u : std::min(line, current_line_count - 1u);
        bool const ok = file.git_line_changes.push_back({marker_line, kind, after_line});
        DEBUG_ASSERT(ok);
        (void)ok;
    }

    [[nodiscard]] auto git_added_marker_line(Slice<GitTextLine const> lines, size_t line)
        -> size_t {
        return line > 0u && line < lines.size() && lines[line].text.empty() &&
                       lines[line - 1u].text.empty()
                   ? line - 1u
                   : line;
    }

    auto push_git_added_range(
        OpenFile& file, Slice<GitTextLine const> lines, size_t first_line, size_t end_line
    ) -> void {
        for (size_t line = first_line; line < end_line; ++line) {
            push_git_line_change(
                file,
                EditorGitLineChangeKind::ADDED,
                git_added_marker_line(lines, line),
                lines.size()
            );
        }
    }

    auto rebuild_git_line_changes(EditorState& editor, OpenFile& file, StrRef current_text)
        -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        Vec<GitTextLine> old_lines = {};
        Vec<GitTextLine> new_lines = {};
        if (!old_lines.init(0u, temp.arena()->resource()) ||
            !new_lines.init(0u, temp.arena()->resource()) ||
            !collect_git_text_lines(file.git_head_text, old_lines) ||
            !collect_git_text_lines(current_text, new_lines)) {
            return;
        }

        file.git_line_changes.clear();

        size_t prefix = 0u;
        while (prefix < old_lines.size() && prefix < new_lines.size() &&
               git_lines_equal(old_lines[prefix], new_lines[prefix])) {
            prefix += 1u;
        }

        size_t old_end = old_lines.size();
        size_t new_end = new_lines.size();
        while (old_end > prefix && new_end > prefix &&
               git_lines_equal(old_lines[old_end - 1u], new_lines[new_end - 1u])) {
            old_end -= 1u;
            new_end -= 1u;
        }

        size_t const old_count = old_end - prefix;
        size_t const new_count = new_end - prefix;
        if (old_count == 0u) {
            push_git_added_range(file, new_lines.slice(), prefix, new_end);
            file.git_line_change_revision = editor.text.revision;
            return;
        }
        if (new_count == 0u) {
            push_git_line_change(file, EditorGitLineChangeKind::REMOVED, prefix, new_lines.size());
            file.git_line_change_revision = editor.text.revision;
            return;
        }
        if (old_count > GIT_DIFF_CELL_LIMIT / new_count) {
            push_git_added_range(file, new_lines.slice(), prefix, new_end);
            push_git_line_change(file, EditorGitLineChangeKind::REMOVED, prefix, new_lines.size());
            file.git_line_change_revision = editor.text.revision;
            return;
        }

        size_t const width = new_count + 1u;
        uint32_t* const lcs = arena_alloc<uint32_t>(*temp.arena(), (old_count + 1u) * width);
        std::memset(lcs, 0, sizeof(uint32_t) * (old_count + 1u) * width);
        for (size_t old_index = old_count; old_index > 0u; --old_index) {
            size_t const old_line = old_index - 1u;
            for (size_t new_index = new_count; new_index > 0u; --new_index) {
                size_t const new_line = new_index - 1u;
                uint32_t const down = lcs[(old_line + 1u) * width + new_line];
                uint32_t const right = lcs[old_line * width + new_line + 1u];
                lcs[old_line * width + new_line] =
                    git_lines_equal(old_lines[prefix + old_line], new_lines[prefix + new_line])
                        ? lcs[(old_line + 1u) * width + new_line + 1u] + 1u
                        : std::max(down, right);
            }
        }

        size_t old_line = 0u;
        size_t new_line = 0u;
        size_t current_line = prefix;
        while (old_line < old_count || new_line < new_count) {
            if (old_line < old_count && new_line < new_count &&
                git_lines_equal(old_lines[prefix + old_line], new_lines[prefix + new_line])) {
                old_line += 1u;
                new_line += 1u;
                current_line += 1u;
                continue;
            }

            uint32_t const skip_old =
                old_line < old_count ? lcs[(old_line + 1u) * width + new_line] : 0u;
            uint32_t const skip_new =
                new_line < new_count ? lcs[old_line * width + new_line + 1u] : 0u;
            if (old_line < old_count && new_line < new_count && skip_old == skip_new) {
                push_git_line_change(
                    file, EditorGitLineChangeKind::MODIFIED, current_line, new_lines.size()
                );
                old_line += 1u;
                new_line += 1u;
                current_line += 1u;
            } else if (new_line < new_count && (old_line == old_count || skip_new >= skip_old)) {
                push_git_line_change(
                    file,
                    EditorGitLineChangeKind::ADDED,
                    git_added_marker_line(new_lines.slice(), current_line),
                    new_lines.size()
                );
                new_line += 1u;
                current_line += 1u;
            } else {
                push_git_line_change(
                    file, EditorGitLineChangeKind::REMOVED, current_line, new_lines.size()
                );
                old_line += 1u;
            }
        }

        file.git_line_change_revision = editor.text.revision;
    }

    [[nodiscard]] auto current_git_line_changes(EditorState& editor)
        -> Slice<EditorGitLineChange const> {
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.current_file_path.empty() ||
            editor.arena == nullptr || !ensure_git_root(editor)) {
            return {};
        }

        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        OpenFile* const file =
            find_open_file(editor, editor.current_file_name, editor.current_file_path);
        if (file == nullptr || !ensure_git_line_change_storage(editor, *file)) {
            return {};
        }
        if (file->git_path != editor.current_file_path) {
            reset_git_line_change_cache(editor, *file);
        }
        if (file->git_relative_path.empty()) {
            file->git_line_changes.clear();
            file->git_line_change_revision = editor.text.revision;
            return {};
        }

        load_git_head_text(editor, *file);
        if (file->git_line_change_revision != editor.text.revision) {
            ArenaTemp temp = begin_thread_temp_arena();
            StrRef const current_text = text_buffer_copy(editor.text, *temp.arena());
            rebuild_git_line_changes(editor, *file, current_text);
        }
        return file->git_line_changes.slice();
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
        file->git_diff = editor.git_diff;
        file->undo_stack = editor.undo_stack;
        file->redo_stack = editor.redo_stack;
        file->file_write_stamp = editor.file_write_stamp;
        file->cursor_line = editor.cursor_line;
        file->cursor_column = editor.cursor_column;
        file->preferred_column = editor.preferred_column;
        bool const cursors_ok =
            file->extra_cursors.copy_from(editor.extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            file->folded_ranges.copy_from(editor.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        file->selection_anchor_line = editor.selection_anchor_line;
        file->selection_anchor_column = editor.selection_anchor_column;
        file->folded_revision = editor.folded_revision;
        file->selection_mode = editor.selection_mode;
        file->scroll_x = editor.scroll_x;
        file->scroll_y = editor.scroll_y;
        file->text_valid = true;
        file->insert_mode = editor.flag(EditorFlag::INSERT_MODE);
        file->selection_active = editor.flag(EditorFlag::SELECTION_ACTIVE);
        file->dirty = editor.flag(EditorFlag::DIRTY);
        file->external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
        file->file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
        file->git_diff_side_by_side = editor.git_diff_side_by_side;
        file->view_kind = editor.view_kind;
    }

    auto load_open_file_buffer(EditorState& editor, OpenFile const& file) -> void {
        set_editor_text(editor, file.text);
        editor.git_diff = file.git_diff;
        editor.current_file_name = file.name;
        editor.current_file_path = file.path;
        editor.saved_text = file.saved_text;
        editor.undo_stack = file.undo_stack;
        editor.redo_stack = file.redo_stack;
        editor.file_write_stamp = file.file_write_stamp;
        editor.cursor_line = file.cursor_line;
        editor.cursor_column = file.cursor_column;
        editor.preferred_column = file.preferred_column;
        bool const cursors_ok =
            editor.extra_cursors.copy_from(file.extra_cursors, editor.arena->resource());
        DEBUG_ASSERT(cursors_ok);
        (void)cursors_ok;
        bool const folds_ok =
            editor.folded_ranges.copy_from(file.folded_ranges, editor.arena->resource());
        DEBUG_ASSERT(folds_ok);
        (void)folds_ok;
        editor.selection_anchor_line = file.selection_anchor_line;
        editor.selection_anchor_column = file.selection_anchor_column;
        editor.folded_revision = file.folded_revision;
        editor.selection_mode = file.selection_mode;
        editor.scroll_x = file.scroll_x;
        editor.scroll_y = file.scroll_y;
        editor.set_flag(EditorFlag::INSERT_MODE, file.insert_mode);
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, file.selection_active);
        editor.set_flag(EditorFlag::DIRTY, file.dirty);
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, file.external_change_pending);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, file.file_deleted_on_disk);
        editor.git_diff_side_by_side = file.git_diff_side_by_side;
        editor.view_kind = file.view_kind;
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
            select_current_file_in_filesystem_tree(editor);
            return true;
        }
        if (load_shared_editor_buffer(editor, name, path)) {
            store_current_open_file(editor);
            touch_open_file(editor, name, path);
            select_current_file_in_filesystem_tree(editor);
            return true;
        }
        OpenFile const* const file = find_open_file(editor, name, path);
        if (file != nullptr && file->text_valid) {
            load_open_file_buffer(editor, *file);
            touch_open_file(editor, name, path);
            select_current_file_in_filesystem_tree(editor);
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
        editor.git_diff = {};
        editor.git_diff_side_by_side = true;
        editor.view_kind = EditorViewKind::TEXT;
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
        select_current_file_in_filesystem_tree(editor);
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
            size_t const split = preferred_code_split_for_open(editor);
            if (split < editor.split_nodes.size()) {
                focus_editor_split(editor, split);
            } else {
                focus_first_code_split(editor);
            }
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
        editor.git_diff = {};
        editor.git_diff_side_by_side = true;
        editor.view_kind = EditorViewKind::TEXT;
        touch_open_file(editor, editor.current_file_name, {});
        store_current_open_file(editor);
    }

    auto request_more_git_commits_for_scroll(EditorState& editor, gui::ScrollState scroll) -> void {
        if (!scroll.valid || scroll.max_y <= 0.0f || !editor.git_graph_open ||
            !editor.git_commits_more || editor.git_commits_loading || editor.git_commits.empty()) {
            return;
        }
        if (scroll.max_y - scroll.y <= GIT_ROW_HEIGHT * 2.0f) {
            editor.git_commit_load_more_requested = true;
        }
    }

    [[nodiscard]] auto git_clean_status_row_visible(EditorState const& editor) -> bool {
        return editor.git_status_items.empty() && editor.git_changes_open &&
               !editor.git_operation_pending;
    }

    [[nodiscard]] auto git_clean_status_row_before(EditorState const& editor, size_t row) -> bool {
        return git_clean_status_row_visible(editor) && row > 0u;
    }

    [[nodiscard]] auto git_scroll_target_y(EditorState const& editor) -> float {
        size_t row = GIT_PANEL_FIXED_SCROLL_ROWS + editor.git_selected;
        if (git_clean_status_row_before(editor, editor.git_selected)) {
            row += 1u;
        }
        return static_cast<float>(row) * GIT_ROW_HEIGHT;
    }

    auto reveal_git_cursor(
        gui::Frame& ui, EditorState& editor, gui::Id scroll_id, gui::ScrollState scroll
    ) -> void {
        if (!editor.git_cursor_reveal) {
            return;
        }
        float const row_min_y = git_scroll_target_y(editor);
        float const row_max_y = row_min_y + GIT_ROW_HEIGHT;
        float const viewport_height =
            std::max(GIT_ROW_HEIGHT, scroll.viewport_height - GIT_PANEL_PADDING_Y * 2.0f);
        if (!scroll.valid) {
            ui.set_scroll_y(scroll_id, row_min_y - GIT_CURSOR_REVEAL_MARGIN);
        } else if (row_min_y < scroll.y + GIT_CURSOR_REVEAL_MARGIN) {
            ui.set_scroll_y(scroll_id, row_min_y - GIT_CURSOR_REVEAL_MARGIN);
        } else if (row_max_y > scroll.y + viewport_height - GIT_CURSOR_REVEAL_MARGIN) {
            ui.set_scroll_y(scroll_id, row_max_y + GIT_CURSOR_REVEAL_MARGIN - viewport_height);
        }
        editor.git_cursor_reveal = false;
    }

    [[nodiscard]] auto git_diff_virtual_path(Arena& arena, StrRef title) -> StrRef {
        StringBuffer path = {};
        BASE_UNUSED(path.init(title.size() + 16u, arena.resource()));
        BASE_UNUSED(path.write_string("gitdiff:"));
        for (char ch : title) {
            BASE_UNUSED(path.write_byte(ch == '\\' || ch == '/' ? '_' : ch));
        }
        BASE_UNUSED(path.write_string(".diff"));
        return arena_copy_cstr(arena, path.str());
    }

    auto open_git_diff(EditorState& editor, StrRef title, StrRef patch) -> void {
        if (editor.arena == nullptr || editor.text.arena == nullptr) {
            return;
        }
        GitDiffDocument doc = {};
        if (!parse_git_patch(*editor.arena, title, patch, doc)) {
            editor.git_status_text = {};
            editor.git_error_text = "Failed to parse git diff.";
            editor.git_error_visible = true;
            return;
        }

        focus_code_split_for_open(editor);
        store_current_open_file(editor);
        editor.current_file_name = arena_copy_cstr(*editor.arena, title);
        editor.current_file_path = git_diff_virtual_path(*editor.arena, title);
        editor.git_diff = doc;
        editor.git_diff_side_by_side = false;
        set_git_diff_view_text(editor);
        editor.file_write_stamp = 0u;
        editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        store_current_open_file(editor);
        touch_open_file(editor, editor.current_file_name, editor.current_file_path);
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
        pane.extra_cursors.clear();
        pane.folded_ranges.clear();
        pane.folded_revision = 0u;
        pane.selection_anchor_line = 0u;
        pane.selection_anchor_column = 0u;
        pane.selection_mode = EditorSelectionMode::NONE;
        pane.scroll_x = 0.0f;
        pane.scroll_y = 0.0f;
        pane.insert_mode = false;
        pane.selection_active = false;
        pane.mouse_selecting = false;
        pane.mouse_was_down = false;
        pane.multi_cursor_dragging = false;
        pane.middle_mouse_was_down = false;
        pane.dirty = false;
        pane.external_change_pending = false;
        pane.file_deleted_on_disk = false;
        pane.git_diff = {};
        pane.git_diff_side_by_side = true;
        pane.view_kind = EditorViewKind::TEXT;
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
            text_buffer_clone(editor.text, pane->text, *editor.arena);
            pane->git_diff = editor.git_diff;
            pane->scratch_text = editor.scratch_text;
            pane->saved_text = editor.saved_text;
            pane->undo_stack = editor.undo_stack;
            pane->redo_stack = editor.redo_stack;
            pane->file_write_stamp = editor.file_write_stamp;
            pane->dirty = editor.flag(EditorFlag::DIRTY);
            pane->external_change_pending = editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING);
            pane->file_deleted_on_disk = editor.flag(EditorFlag::FILE_DELETED_ON_DISK);
            pane->git_diff_side_by_side = editor.git_diff_side_by_side;
            pane->view_kind = editor.view_kind;
        }
    }

    [[nodiscard]] auto reload_current_file_from_disk(EditorState& editor) -> bool {
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.current_file_path.empty() ||
            editor.text.arena == nullptr) {
            return false;
        }
        StrRef text = {};
        if (!read_tree_file_display_text(*editor.text.arena, editor.current_file_path, text)) {
            return false;
        }
        uint64_t const stamp = file_write_stamp(editor.current_file_path);
        set_editor_text(editor, text);
        editor.git_diff = {};
        editor.git_diff_side_by_side = true;
        editor.view_kind = EditorViewKind::TEXT;
        editor.file_write_stamp = stamp;
        editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
        sync_current_file_to_matching_panes(editor);
        store_current_open_file(editor);
        return true;
    }

    [[nodiscard]] auto overwrite_current_file_to_disk(EditorState& editor) -> bool {
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.current_file_path.empty() ||
            editor.text.arena == nullptr) {
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
        if (editor.view_kind == EditorViewKind::GIT_DIFF || path.empty() ||
            editor.text.arena == nullptr || editor.arena == nullptr) {
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
        editor.git_diff = {};
        editor.git_diff_side_by_side = true;
        editor.view_kind = EditorViewKind::TEXT;
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
            file.extra_cursors.clear();
            file.folded_ranges.clear();
            file.folded_revision = 0u;
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
        size_t chunk_start = start;
        while (chunk_start < end) {
            size_t chunk_end = std::min(chunk_start + TEXT_DRAW_CHUNK_SIZE, end);
            while (chunk_end < end && chunk_end > chunk_start &&
                   (static_cast<uint8_t>(line.text[chunk_end]) & 0xc0u) == 0x80u) {
                chunk_end -= 1u;
            }
            if (chunk_end == chunk_start) {
                chunk_end = std::min(chunk_start + TEXT_DRAW_CHUNK_SIZE, end);
            }
            draw::draw_text(
                context,
                {std::round(x + char_width * static_cast<float>(chunk_start)), std::round(y)},
                style,
                StrRef(line.text + chunk_start, chunk_end - chunk_start),
                nullptr
            );
            chunk_start = chunk_end;
        }
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

    [[nodiscard]] auto diff_line_marker(StrRef line, bool side_by_side) -> char {
        if (side_by_side) {
            return line.size() > 6u ? line[6u] : '\0';
        }
        return line.empty() ? '\0' : line[0u];
    }

    [[nodiscard]] auto diff_marker_color(Palette const& palette, char marker) -> gui::Color {
        switch (marker) {
        case '+':
            return palette.mode_insert;
        case '-':
            return palette.preprocessor;
        case '~':
            return palette.string;
        default:
            return {};
        }
    }

    [[nodiscard]] auto git_diff_unified_code_start(StrRef line) -> size_t {
        if (line.size() < 14u || (line[0u] != ' ' && line[0u] != '+' && line[0u] != '-') ||
            line[6u] != ' ' || line[12u] != ' ' || line[13u] != ' ') {
            return 0u;
        }
        return 14u;
    }

    auto draw_git_diff_line_background(
        draw::Context context,
        EditorState const& editor,
        Palette const& palette,
        EditorLine line,
        gui::Rect content,
        float y,
        float line_height
    ) -> void {
        if (editor.view_kind != EditorViewKind::GIT_DIFF) {
            return;
        }
        StrRef const text = text_buffer_line_text(line);
        gui::Color color =
            diff_marker_color(palette, diff_line_marker(text, editor.git_diff_side_by_side));
        if (color.a < 0.0f && (text.starts_with("@@") || text.starts_with("diff --git"))) {
            color = palette.cursor;
        }
        if (color.a < 0.0f) {
            return;
        }
        draw::draw_rect_filled(
            context,
            {{content.min.x, y}, {content.max.x, y + line_height}},
            to_draw_color(gui::color_alpha(color, 0.12f)),
            0.0f
        );
    }

    auto draw_git_diff_span_rects(
        draw::Context context,
        Slice<GitInlineSpan const> spans,
        size_t prefix,
        float text_x,
        float y,
        float line_height,
        float char_width,
        gui::Color color
    ) -> void {
        for (GitInlineSpan const& span : spans) {
            float const min_x = text_x + char_width * static_cast<float>(prefix + span.offset);
            float const max_x = min_x + char_width * static_cast<float>(span.size);
            draw::draw_rect_filled(
                context,
                {{min_x, y + 2.0f}, {max_x, y + line_height - 2.0f}},
                to_draw_color(gui::color_alpha(color, 0.28f)),
                0.0f
            );
        }
    }

    auto draw_git_diff_inline_spans(
        draw::Context context,
        EditorState const& editor,
        Palette const& palette,
        size_t line,
        float text_x,
        float y,
        float line_height,
        float char_width
    ) -> void {
        if (editor.view_kind != EditorViewKind::GIT_DIFF || !editor.git_diff_side_by_side ||
            line < 2u) {
            return;
        }
        size_t const row_index = line - 2u;
        if (row_index >= editor.git_diff.rows.size()) {
            return;
        }
        GitDiffRow const& row = editor.git_diff.rows[row_index];
        if (row.kind != GitDiffRowKind::MODIFIED) {
            return;
        }
        size_t constexpr side_prefix = 8u;
        size_t const right_prefix = side_prefix + row.left_text.size() + 3u + side_prefix;
        draw_git_diff_span_rects(
            context,
            row.left_spans.slice(),
            side_prefix,
            text_x,
            y,
            line_height,
            char_width,
            palette.preprocessor
        );
        draw_git_diff_span_rects(
            context,
            row.right_spans.slice(),
            right_prefix,
            text_x,
            y,
            line_height,
            char_width,
            palette.mode_insert
        );
    }

    auto draw_git_diff_line(
        draw::Context context,
        font_cache::Font font,
        SyntaxTokenizer tokenizer,
        Palette const& palette,
        EditorLine const& line,
        float x,
        float y,
        float font_size,
        font_provider::RasterPolicy raster_policy,
        float char_width,
        bool side_by_side
    ) -> void {
        StrRef const text = editor_line_text(line);
        size_t const code_start = side_by_side ? 0u : git_diff_unified_code_start(text);
        if (code_start == 0u) {
            draw::TextStyle style = {
                .font = font,
                .size = font_size,
                .raster_policy = raster_policy,
                .color = to_draw_color(text.starts_with("@@") ? palette.muted : palette.text),
            };
            draw::draw_text(context, {std::round(x), std::round(y)}, style, text, nullptr);
            return;
        }

        draw::TextStyle prefix_style = {
            .font = font,
            .size = font_size,
            .raster_policy = raster_policy,
            .color = to_draw_color(palette.faint),
        };
        draw_token(context, prefix_style, line, 0u, code_start, x, y, char_width);

        EditorLine code_line = {line.text + code_start, line.size - code_start};
        draw_syntax_line(
            context,
            font,
            tokenizer,
            palette,
            code_line,
            x + char_width * static_cast<float>(code_start),
            y,
            font_size,
            raster_policy,
            char_width
        );
    }

    [[nodiscard]] auto semantic_tokens_for_editor(EditorState const& editor)
        -> Slice<LspSemanticToken const> {
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.lsp_bridge == nullptr ||
            editor.lsp_bridge->semantic_tokens_path != editor.current_file_path ||
            editor.lsp_bridge->semantic_tokens_revision != editor.text.revision) {
            return {};
        }
        return editor.lsp_bridge->semantic_tokens;
    }

    [[nodiscard]] auto inlay_hints_for_editor(EditorState const& editor)
        -> Slice<LspInlayHint const> {
        if (!editor.inlay_hints_enabled || editor.view_kind == EditorViewKind::GIT_DIFF ||
            editor.lsp_bridge == nullptr ||
            editor.lsp_bridge->inlay_hints_path != editor.current_file_path ||
            editor.lsp_bridge->inlay_hints_revision != editor.text.revision) {
            return {};
        }
        return editor.lsp_bridge->inlay_hints;
    }

    [[nodiscard]] auto inlay_hint_width(LspInlayHint const& hint, float char_width) -> float {
        float width =
            char_width * static_cast<float>(hint.label.size()) + 2.0f * INLAY_HINT_PADDING_X;
        if (hint.padding_left) {
            width += char_width;
        }
        if (hint.padding_right) {
            width += char_width;
        }
        return width;
    }

    [[nodiscard]] auto inlay_shift_for_column(
        Slice<LspInlayHint const> hints, size_t line, size_t column, float char_width
    ) -> float {
        float shift = 0.0f;
        for (LspInlayHint const& hint : hints) {
            if (hint.position.line > line) {
                break;
            }
            if (hint.position.line == line && hint.position.column <= column) {
                shift += inlay_hint_width(hint, char_width);
            }
        }
        return shift;
    }

    [[nodiscard]] auto inlay_column_x(
        Slice<LspInlayHint const> hints, size_t line, size_t column, float text_x, float char_width
    ) -> float {
        return text_x + char_width * static_cast<float>(column) +
               inlay_shift_for_column(hints, line, column, char_width);
    }

    auto draw_token_with_inlay_hints(
        draw::Context context,
        draw::TextStyle style,
        EditorLine const& line,
        size_t line_index,
        size_t start,
        size_t end,
        float x,
        float y,
        float char_width,
        Slice<LspInlayHint const> hints
    ) -> void {
        size_t part_start = start;
        while (part_start < end) {
            size_t part_end = end;
            for (LspInlayHint const& hint : hints) {
                if (hint.position.line > line_index) {
                    break;
                }
                if (hint.position.line == line_index && hint.position.column > part_start &&
                    hint.position.column < part_end) {
                    part_end = hint.position.column;
                }
            }
            draw::draw_text(
                context,
                {std::round(inlay_column_x(hints, line_index, part_start, x, char_width)),
                 std::round(y)},
                style,
                StrRef(line.text + part_start, part_end - part_start),
                nullptr
            );
            part_start = part_end;
        }
    }

    auto draw_inlay_hints_for_line(
        draw::Context context,
        font_cache::Font font,
        Slice<LspInlayHint const> hints,
        Palette const& palette,
        size_t line,
        float x,
        float y,
        float line_height,
        float font_size,
        font_provider::RasterPolicy raster_policy,
        float char_width
    ) -> void {
        float shift = 0.0f;
        for (LspInlayHint const& hint : hints) {
            if (hint.position.line > line) {
                break;
            }
            if (hint.position.line != line) {
                continue;
            }

            float const hint_x = x + char_width * static_cast<float>(hint.position.column) + shift;
            float const bg_x = hint_x + (hint.padding_left ? char_width : 0.0f);
            float const bg_width =
                char_width * static_cast<float>(hint.label.size()) + 2.0f * INLAY_HINT_PADDING_X;
            draw::draw_rect_filled(
                context,
                {{std::round(bg_x), y + INLAY_HINT_PADDING_Y},
                 {std::round(bg_x + bg_width), y + line_height - INLAY_HINT_PADDING_Y}},
                to_draw_color(gui::color_alpha(palette.control_hovered, 0.72f)),
                INLAY_HINT_RADIUS
            );

            draw::TextStyle style = {
                .font = font,
                .size = font_size,
                .raster_policy = raster_policy,
                .color = to_draw_color(palette.faint),
            };
            draw::draw_text(
                context,
                {std::round(bg_x + INLAY_HINT_PADDING_X), std::round(y)},
                style,
                hint.label,
                nullptr
            );
            shift += inlay_hint_width(hint, char_width);
        }
    }

    [[nodiscard]] auto semantic_token_overlaps_range(
        Slice<LspSemanticToken const> tokens,
        size_t line_index,
        size_t line_size,
        size_t start,
        size_t end
    ) -> bool {
        for (LspSemanticToken const& token : tokens) {
            if (token.range.start.line > line_index) {
                break;
            }
            if (token.kind == SyntaxTokenKind::TEXT || token.range.start.line != line_index ||
                token.range.end.line != line_index) {
                continue;
            }
            size_t const token_start = std::min(token.range.start.column, line_size);
            size_t const token_end = std::min(token.range.end.column, line_size);
            if (token_start < end && token_end > start) {
                return true;
            }
        }
        return false;
    }

    auto draw_syntax_line_with_inlay_hints(
        draw::Context context,
        font_cache::Font font,
        SyntaxTokenizer tokenizer,
        Palette const& palette,
        EditorLine const& line,
        size_t line_index,
        float x,
        float y,
        float font_size,
        font_provider::RasterPolicy raster_policy,
        float char_width,
        Slice<LspSemanticToken const> semantic_tokens,
        Slice<LspInlayHint const> hints
    ) -> void {
        if (semantic_tokens.empty() && hints.empty()) {
            draw_syntax_line(
                context, font, tokenizer, palette, line, x, y, font_size, raster_policy, char_width
            );
            return;
        }

        draw::TextStyle style = {.font = font, .size = font_size, .raster_policy = raster_policy};
        StrRef const text = editor_line_text(line);
        while (!semantic_tokens.empty() && semantic_tokens.front().range.start.line < line_index) {
            semantic_tokens.remove_prefix(1u);
        }
        size_t index = 0u;
        while (index < text.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, text, index);
            if (!semantic_token_overlaps_range(
                    semantic_tokens, line_index, line.size, token.start, token.end
                )) {
                style.color = to_draw_color(syntax_token_color(palette, token.kind));
                draw_token_with_inlay_hints(
                    context,
                    style,
                    line,
                    line_index,
                    token.start,
                    token.end,
                    x,
                    y,
                    char_width,
                    hints
                );
            }
            index = token.end;
        }
    }

    [[nodiscard]] auto sticky_scope_folding_ranges(EditorState const& editor)
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

    auto insert_sticky_scope_line(Slice<size_t> lines, size_t& count, size_t line) -> void {
        for (size_t index = 0u; index < count; ++index) {
            if (lines[index] == line) {
                return;
            }
        }
        if (count == lines.size()) {
            if (line <= lines[0u]) {
                return;
            }
            for (size_t index = 1u; index < count; ++index) {
                lines[index - 1u] = lines[index];
            }
            count -= 1u;
        }

        size_t index = count;
        while (index > 0u && lines[index - 1u] > line) {
            lines[index] = lines[index - 1u];
            index -= 1u;
        }
        lines[index] = line;
        count += 1u;
    }

    [[nodiscard]] auto sticky_scope_max_line_count(gui::Rect content, float line_height) -> size_t {
        size_t const visible_slots =
            static_cast<size_t>(std::max(0.0f, content.max.y - content.min.y) / line_height);
        return visible_slots > 1u ? std::min(STICKY_SCOPE_MAX_LINES, visible_slots - 1u) : 0u;
    }

    [[nodiscard]] auto collect_sticky_scope_lines(
        EditorState const& editor, float line_height, gui::Rect content, Slice<size_t> lines
    ) -> size_t {
        size_t const max_count =
            std::min(lines.size(), sticky_scope_max_line_count(content, line_height));
        if (max_count == 0u) {
            return 0u;
        }

        size_t const visible_line_count = editor_visible_line_count(editor);
        size_t const first_visible_line =
            std::min(visible_line_count - 1u, static_cast<size_t>(editor.scroll_y / line_height));
        size_t const first_line = editor_visible_line_at(editor, first_visible_line);
        float const scroll_offset =
            editor.scroll_y - static_cast<float>(first_visible_line) * line_height;
        bool const first_line_clipped = scroll_offset > 0.5f;
        size_t const source_line_count = editor_line_count(editor);

        size_t count = 0u;
        for (LspFoldingRange const range : sticky_scope_folding_ranges(editor)) {
            size_t const start_line = std::min(range.start_line, source_line_count - 1u);
            size_t const end_line = std::min(range.end_line, source_line_count - 1u);
            if (start_line >= end_line || first_line > end_line ||
                (start_line == first_line && !first_line_clipped) || start_line > first_line) {
                continue;
            }
            insert_sticky_scope_line(lines.prefix(max_count), count, start_line);
        }
        return count;
    }

    [[nodiscard]] auto sticky_scope_hit_line(
        gui::Rect content,
        Slice<size_t const> lines,
        gui::Vec2 mouse,
        float line_height,
        size_t& line,
        size_t& row
    ) -> bool {
        float const sticky_height = line_height * static_cast<float>(lines.size());
        if (lines.empty() || mouse.x < content.min.x - 10.0f || mouse.x >= content.max.x ||
            mouse.y < content.min.y || mouse.y >= content.min.y + sticky_height) {
            return false;
        }
        row = std::min(
            lines.size() - 1u, static_cast<size_t>((mouse.y - content.min.y) / line_height)
        );
        line = lines[row];
        return true;
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
        float char_width,
        Slice<LspInlayHint const> inlay_hints
    ) -> void;

    auto draw_sticky_scope_lines(
        draw::Context context,
        font_cache::Font font,
        EditorState const& editor,
        Palette const& palette,
        Slice<size_t const> lines,
        SyntaxTokenizer tokenizer,
        Slice<LspSemanticToken const> semantic_tokens,
        gui::Rect content,
        float text_x,
        float text_min_x,
        float line_height,
        float char_width
    ) -> void {
        if (lines.empty()) {
            return;
        }

        float const height = line_height * static_cast<float>(lines.size());
        draw::Rect const panel = {
            {content.min.x - 10.0f, content.min.y}, {content.max.x, content.min.y + height}
        };
        draw::draw_rect_filled(
            context, panel, to_draw_color(gui::color_alpha(palette.panel, 0.98f)), 0.0f
        );
        draw::draw_line(
            context,
            {panel.min.x, std::round(panel.max.y)},
            {content.max.x, std::round(panel.max.y)},
            to_draw_color(palette.border),
            1.0f
        );

        float const line_number_x = content.min.x;
        float const fold_marker_x =
            content.min.x + editor_scaled_font_size(editor, LINE_NUMBER_WIDTH - 16.0f);
        draw::Rect const text_clip = {{text_min_x, content.min.y}, {content.max.x, panel.max.y}};
        for (size_t index = 0u; index < lines.size(); ++index) {
            size_t const line = lines[index];
            float const y = content.min.y + line_height * static_cast<float>(index);
            EditorLine const text_line = editor_line(editor, line);
            draw::TextStyle number_style = {
                .font = font,
                .size = editor.font_size,
                .raster_policy = editor.raster_policy,
                .color = to_draw_color(palette.faint),
            };
            draw::draw_text(
                context,
                {std::round(line_number_x), std::round(y - 2.0f)},
                number_style,
                fmt::tprintf("%4zu", line + 1u),
                nullptr
            );
            EditorFoldInfo const fold = editor_fold_info(editor, line);
            if (fold.foldable) {
                draw::draw_text(
                    context,
                    {std::round(fold_marker_x), std::round(y - 2.0f)},
                    number_style,
                    fold.folded ? "+" : "-",
                    nullptr
                );
            }

            draw::push_clip_rect(context, text_clip);
            draw_syntax_line_with_inlay_hints(
                context,
                font,
                tokenizer,
                palette,
                text_line,
                line,
                text_x,
                y - 2.0f,
                editor.font_size,
                editor.raster_policy,
                char_width,
                semantic_tokens,
                {}
            );
            draw_semantic_line(
                context,
                font,
                semantic_tokens,
                palette,
                text_line,
                line,
                text_x,
                y - 2.0f,
                editor.font_size,
                editor.raster_policy,
                char_width,
                {}
            );
            draw::pop_clip_rect(context);
        }
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
        float char_width,
        Slice<LspInlayHint const> inlay_hints
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
            draw_token_with_inlay_hints(
                context, style, line, line_index, start, end, x, y, char_width, inlay_hints
            );
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
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.lsp_bridge == nullptr) {
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
        float char_width,
        Slice<LspInlayHint const> inlay_hints
    ) -> void {
        if (editor.view_kind == EditorViewKind::GIT_DIFF || editor.lsp_bridge == nullptr) {
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
                float const x0 = inlay_column_x(inlay_hints, line, start, text_x, char_width);
                float const x1 = inlay_column_x(inlay_hints, line, end, text_x, char_width);
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
        Palette const& palette,
        Slice<LspInlayHint const> inlay_hints
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

        float const x0 = selection.full_line
                             ? text_x
                             : inlay_column_x(inlay_hints, line, start, text_x, char_width);
        float const x1 =
            selection.full_line
                ? max_x
                : inlay_column_x(
                      inlay_hints, line, end + (selects_newline ? 1u : 0u), text_x, char_width
                  );
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

    [[nodiscard]] auto editor_has_cursor_on_line(EditorState const& editor, size_t line) -> bool {
        if (editor.cursor_line == line) {
            return true;
        }
        for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
            if (editor.extra_cursors[index].line == line) {
                return true;
            }
        }
        return false;
    }

    auto draw_editor_block_cursor(
        draw::Context context,
        EditorState const& editor,
        Palette const& palette,
        size_t line,
        size_t line_size,
        size_t column,
        float text_x,
        float y,
        float line_height,
        float char_width,
        Slice<LspInlayHint const> inlay_hints
    ) -> void {
        size_t const cursor_column = draw_cursor_column(editor, line, line_size, column);
        float const cursor_x0 =
            std::round(inlay_column_x(inlay_hints, line, cursor_column, text_x, char_width));
        float const cursor_x1 = std::round(cursor_x0 + char_width);
        draw::draw_rect_filled(
            context,
            {{cursor_x0, y}, {std::max(cursor_x0 + 1.0f, cursor_x1), y + line_height}},
            to_draw_color(gui::color_alpha(palette.cursor, 0.45f)),
            2.0f
        );
    }

    auto draw_editor_insert_cursor(
        draw::Context context,
        Palette const& palette,
        size_t visible_line,
        size_t source_line,
        size_t column,
        float text_x,
        float content_min_y,
        float content_max_y,
        float scroll_y,
        float line_height,
        float char_width,
        Slice<LspInlayHint const> inlay_hints
    ) -> void {
        float const cursor_x =
            std::round(inlay_column_x(inlay_hints, source_line, column, text_x, char_width));
        float const cursor_y =
            content_min_y + static_cast<float>(visible_line) * line_height - scroll_y;
        if (cursor_y + line_height < content_min_y || cursor_y >= content_max_y) {
            return;
        }
        draw::draw_rect_filled(
            context,
            {{cursor_x, std::round(cursor_y + 2.0f)},
             {cursor_x + 2.0f, std::round(cursor_y + line_height - 2.0f)}},
            to_draw_color(palette.mode_insert),
            0.0f
        );
    }

    [[nodiscard]] auto git_line_has_change(
        Slice<EditorGitLineChange const> changes, size_t line, EditorGitLineChangeKind kind
    ) -> bool {
        for (EditorGitLineChange const& change : changes) {
            if (change.line == line && change.kind == kind) {
                return true;
            }
        }
        return false;
    }

    auto draw_git_line_change_kind_marker(
        draw::Context context,
        gui::draw::Color color,
        Slice<EditorGitLineChange const> changes,
        size_t line,
        EditorGitLineChangeKind kind,
        float x,
        float y,
        float line_height
    ) -> void {
        if (!git_line_has_change(changes, line, kind)) {
            return;
        }

        bool const previous = line > 0u && git_line_has_change(changes, line - 1u, kind);
        bool const next = git_line_has_change(changes, line + 1u, kind);
        float const y0 = previous ? y : y + 3.0f;
        float const y1 = next ? y + line_height : y + line_height - 3.0f;
        draw::draw_rect_filled(
            context,
            {{std::round(x), std::round(y0)}, {std::round(x) + 3.0f, std::round(y1)}},
            color,
            0.0f
        );
    }

    [[nodiscard]] auto git_line_has_removed_boundary(
        Slice<EditorGitLineChange const> changes, size_t line, bool after_line
    ) -> bool {
        for (EditorGitLineChange const& change : changes) {
            if (change.line == line && change.kind == EditorGitLineChangeKind::REMOVED &&
                change.after_line == after_line) {
                return true;
            }
        }
        return false;
    }

    auto draw_git_removed_marker(
        draw::Context context,
        Palette const& palette,
        Slice<EditorGitLineChange const> changes,
        size_t line,
        float x,
        float y,
        float line_height
    ) -> void {
        bool const before = git_line_has_removed_boundary(changes, line, false);
        bool const after = git_line_has_removed_boundary(changes, line, true);
        if (!before && !after) {
            return;
        }

        gui::draw::Color const color = to_draw_color(palette.preprocessor);
        float const marker_x = std::round(x + 4.0f);
        if (before) {
            float const center_y = std::round(y + line_height * 0.5f);
            draw::draw_triangle_filled(
                context,
                {marker_x, center_y - 4.0f},
                {marker_x, center_y + 4.0f},
                {marker_x + 5.0f, center_y},
                color
            );
        }
        if (after) {
            float const center_y = std::round(y + line_height - 4.0f);
            draw::draw_triangle_filled(
                context,
                {marker_x, center_y - 4.0f},
                {marker_x, center_y + 4.0f},
                {marker_x + 5.0f, center_y},
                color
            );
        }
    }

    auto draw_git_line_change_marker(
        draw::Context context,
        Palette const& palette,
        Slice<EditorGitLineChange const> changes,
        size_t line,
        float x,
        float y,
        float line_height
    ) -> void {
        float const marker_x = std::round(x);
        draw_git_line_change_kind_marker(
            context,
            to_draw_color(palette.mode_insert),
            changes,
            line,
            EditorGitLineChangeKind::ADDED,
            marker_x,
            y,
            line_height
        );
        draw_git_line_change_kind_marker(
            context,
            to_draw_color(palette.cursor),
            changes,
            line,
            EditorGitLineChangeKind::MODIFIED,
            marker_x,
            y,
            line_height
        );
        draw_git_removed_marker(context, palette, changes, line, marker_x, y, line_height);
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
        bool const middle_mouse_pressed =
            input.mouse_down[1u] && !editor.flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN);
        bool const double_clicked =
            !editor.flag(EditorFlag::SIDEBAR_RESIZING) && hovered && input.mouse_double_clicked[0u];
        bool const triple_clicked =
            !editor.flag(EditorFlag::SIDEBAR_RESIZING) && hovered && input.mouse_triple_clicked[0u];
        bool const clicked = !editor.flag(EditorFlag::SIDEBAR_RESIZING) && mouse_pressed && hovered;
        bool const middle_clicked =
            !editor.flag(EditorFlag::SIDEBAR_RESIZING) && middle_mouse_pressed && hovered;
        bool const dragged = !editor.flag(EditorFlag::SIDEBAR_RESIZING) &&
                             editor.flag(EditorFlag::MOUSE_SELECTING) && input.mouse_down[0u];
        bool const middle_dragged = !editor.flag(EditorFlag::SIDEBAR_RESIZING) &&
                                    editor.flag(EditorFlag::MULTI_CURSOR_DRAGGING) &&
                                    input.mouse_down[1u];
        gui::Rect const full_content = editor_content_rect(rect);
        float const line_height = editor_line_height(editor);
        size_t sticky_scope_lines[STICKY_SCOPE_MAX_LINES] = {};
        size_t sticky_scope_count = 0u;
        gui::Rect input_rect = rect;
        if (scrolled) {
            editor.scroll_y -= input.scroll_delta_y;
        }
        gui::Rect body_rect = rect;
        clamp_scroll(editor, body_rect);
        sticky_scope_count =
            collect_sticky_scope_lines(editor, line_height, full_content, sticky_scope_lines);
        size_t sticky_scope_line = 0u;
        size_t sticky_scope_row = 0u;
        gui::Rect const input_content = editor_content_rect(input_rect);
        bool const sticky_scope_clicked =
            clicked && sticky_scope_hit_line(
                           input_content,
                           Slice<size_t const>(sticky_scope_lines, sticky_scope_count),
                           input.mouse_pos,
                           line_height,
                           sticky_scope_line,
                           sticky_scope_row
                       );
        bool fold_gutter_clicked = false;
        if (sticky_scope_clicked) {
            size_t const visible_line = editor_visible_line_index(editor, sticky_scope_line);
            size_t const first_line =
                sticky_scope_row < visible_line ? visible_line - sticky_scope_row : 0u;
            editor.scroll_y = static_cast<float>(first_line) * line_height;
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (clicked) {
            float const gutter_max_x =
                input_content.min.x + editor_scaled_font_size(editor, LINE_NUMBER_WIDTH);
            float const y =
                std::max(0.0f, input.mouse_pos.y - input_content.min.y + editor.scroll_y);
            size_t const visible_line = std::min(
                editor_visible_line_count(editor) - 1u, static_cast<size_t>(y / line_height)
            );
            size_t const line = editor_visible_line_at(editor, visible_line);
            fold_gutter_clicked =
                input.mouse_pos.x < gutter_max_x && editor_fold_info(editor, line).foldable;
            if (fold_gutter_clicked) {
                toggle_editor_fold_at_line(editor, line);
                editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
            }
        }
        if (middle_clicked && !sticky_scope_clicked) {
            begin_multi_cursor_from_mouse(editor, input_rect, input.mouse_pos, char_width);
            editor.set_flag(EditorFlag::MULTI_CURSOR_DRAGGING, true);
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (middle_dragged) {
            update_multi_cursor_from_mouse(editor, input_rect, input.mouse_pos, char_width);
        } else if (triple_clicked) {
            select_line_from_mouse(editor, input_rect, input.mouse_pos, char_width);
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (double_clicked) {
            select_word_from_mouse(editor, input_rect, input.mouse_pos, char_width);
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        } else if (clicked && !fold_gutter_clicked) {
            update_cursor_from_mouse(
                editor,
                input_rect,
                input.mouse_pos,
                char_width,
                (input.key_mods & gui::KEY_MOD_SHIFT) != 0u
            );
            editor.set_flag(EditorFlag::MOUSE_SELECTING, true);
        } else if (dragged) {
            update_cursor_from_mouse(editor, input_rect, input.mouse_pos, char_width, true);
        }
        if (!input.mouse_down[0u]) {
            editor.set_flag(EditorFlag::MOUSE_SELECTING, false);
        }
        if (!input.mouse_down[1u]) {
            editor.set_flag(EditorFlag::MULTI_CURSOR_DRAGGING, false);
        }
        editor.set_flag(EditorFlag::MOUSE_WAS_DOWN, input.mouse_down[0u]);
        editor.set_flag(EditorFlag::MIDDLE_MOUSE_WAS_DOWN, input.mouse_down[1u]);
        bool const mouse_edited = (clicked && !fold_gutter_clicked) || dragged || middle_clicked ||
                                  middle_dragged || double_clicked || triple_clicked;
        if (!sticky_scope_clicked && (mouse_edited || apply_key_reveal)) {
            reveal_cursor(editor, rect, char_width);
        } else {
            editor.scroll_x = std::max(0.0f, editor.scroll_x);
            editor.scroll_y = std::max(0.0f, editor.scroll_y);
        }

        clamp_scroll(editor, body_rect);
        sticky_scope_count =
            collect_sticky_scope_lines(editor, line_height, full_content, sticky_scope_lines);

        gui::Rect const content = editor_content_rect(body_rect);
        draw::Rect const clip = {
            {rect.min.x, full_content.min.y}, {full_content.max.x, full_content.max.y}
        };
        draw::push_clip_rect(draw_context, clip);

        size_t const line_count = editor_visible_line_count(editor);
        size_t const first_line =
            std::min(line_count - 1u, static_cast<size_t>(editor.scroll_y / line_height));
        float y = content.min.y - (editor.scroll_y - static_cast<float>(first_line) * line_height);
        float const text_x = editor_text_x(editor, rect);
        float const text_min_x = std::min(text_x + editor.scroll_x, content.max.x);
        draw::Rect const text_clip = {{text_min_x, content.min.y}, {content.max.x, content.max.y}};
        float const line_number_x = content.min.x;
        float const git_marker_x = content.min.x - 8.0f;
        float const fold_marker_x =
            content.min.x + editor_scaled_font_size(editor, LINE_NUMBER_WIDTH - 16.0f);
        EditorSelectionRange const selection = editor_selection_range(editor);
        SyntaxTokenizer const tokenizer = syntax_tokenizer_for_file_name(editor.current_file_name);
        Slice<LspSemanticToken const> const semantic_tokens = semantic_tokens_for_editor(editor);
        Slice<LspInlayHint const> const inlay_hints = inlay_hints_for_editor(editor);
        Slice<EditorGitLineChange const> const git_line_changes = current_git_line_changes(editor);
        size_t visible_line = first_line;
        size_t line = editor_visible_line_at(editor, visible_line);
        while (visible_line < line_count && y < content.max.y) {
            EditorLine const& text_line = editor_line(editor, line);
            bool const cursor_line = editor_has_cursor_on_line(editor, line);
            EditorFoldInfo const fold = editor_fold_info(editor, line);
            draw_git_diff_line_background(
                draw_context, editor, palette, text_line, content, y, line_height
            );
            if (selection_visible && cursor_line) {
                draw::draw_rect_filled(
                    draw_context,
                    {{content.min.x, y}, {content.max.x, y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor_line, 0.72f)),
                    0.0f
                );
            }
            draw_git_line_change_marker(
                draw_context, palette, git_line_changes, line, git_marker_x, y, line_height
            );

            if (editor.view_kind != EditorViewKind::GIT_DIFF) {
                draw::TextStyle number_style = {
                    .font = editor_font,
                    .size = editor.font_size,
                    .raster_policy = editor.raster_policy,
                    .color = to_draw_color(
                        selection_visible && cursor_line ? palette.text : palette.faint
                    ),
                };
                draw::draw_text(
                    draw_context,
                    {std::round(line_number_x), std::round(y - 2.0f)},
                    number_style,
                    fmt::tprintf("%4zu", line + 1u),
                    nullptr
                );
                if (fold.foldable) {
                    number_style.color = to_draw_color(fold.folded ? palette.text : palette.faint);
                    draw::draw_text(
                        draw_context,
                        {std::round(fold_marker_x), std::round(y - 2.0f)},
                        number_style,
                        fold.folded ? "+" : "-",
                        nullptr
                    );
                }
            }

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
                    palette,
                    inlay_hints
                );
                for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
                    draw_editor_selection(
                        draw_context,
                        editor_extra_selection_range(editor, index),
                        text_line,
                        line,
                        text_x,
                        y,
                        line_height,
                        char_width,
                        content.max.x,
                        palette,
                        inlay_hints
                    );
                }
            }
            if (selection_visible && !editor.flag(EditorFlag::INSERT_MODE) &&
                line == editor.cursor_line) {
                draw_editor_block_cursor(
                    draw_context,
                    editor,
                    palette,
                    line,
                    text_line.size,
                    editor.cursor_column,
                    text_x,
                    y,
                    line_height,
                    char_width,
                    inlay_hints
                );
            }
            if (selection_visible && !editor.flag(EditorFlag::INSERT_MODE)) {
                for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
                    EditorCursor const cursor = editor.extra_cursors[index];
                    if (cursor.line == line) {
                        draw_editor_block_cursor(
                            draw_context,
                            editor,
                            palette,
                            line,
                            text_line.size,
                            cursor.column,
                            text_x,
                            y,
                            line_height,
                            char_width,
                            inlay_hints
                        );
                    }
                }
            }
            draw_git_diff_inline_spans(
                draw_context, editor, palette, line, text_x, y, line_height, char_width
            );
            if (editor.view_kind == EditorViewKind::GIT_DIFF) {
                draw_git_diff_line(
                    draw_context,
                    editor_font,
                    tokenizer,
                    palette,
                    text_line,
                    text_x,
                    y - 2.0f,
                    editor.font_size,
                    editor.raster_policy,
                    char_width,
                    editor.git_diff_side_by_side
                );
            } else {
                draw_syntax_line_with_inlay_hints(
                    draw_context,
                    editor_font,
                    tokenizer,
                    palette,
                    text_line,
                    line,
                    text_x,
                    y - 2.0f,
                    editor.font_size,
                    editor.raster_policy,
                    char_width,
                    semantic_tokens,
                    inlay_hints
                );
            }
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
                char_width,
                inlay_hints
            );
            draw_inlay_hints_for_line(
                draw_context,
                editor_font,
                inlay_hints,
                palette,
                line,
                text_x,
                y - 2.0f,
                line_height,
                editor.font_size,
                editor.raster_policy,
                char_width
            );
            size_t const hidden_count = fold.hidden_line_count;
            if (hidden_count != 0u) {
                draw::TextStyle fold_style = {
                    .font = editor_font,
                    .size = editor.font_size,
                    .raster_policy = editor.raster_policy,
                    .color = to_draw_color(palette.faint),
                };
                draw::draw_text(
                    draw_context,
                    {std::round(
                         inlay_column_x(inlay_hints, line, text_line.size + 1u, text_x, char_width)
                     ),
                     std::round(y - 2.0f)},
                    fold_style,
                    fmt::tprintf("... %zu", hidden_count),
                    nullptr
                );
            }
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
                char_width,
                inlay_hints
            );
            y += line_height;
            visible_line += 1u;
            line = editor_next_visible_line(editor, line);
        }

        if (selection_visible && editor.flag(EditorFlag::INSERT_MODE)) {
            draw::push_clip_rect(draw_context, text_clip);
            draw_editor_insert_cursor(
                draw_context,
                palette,
                editor_visible_line_index(editor, editor.cursor_line),
                editor.cursor_line,
                editor.cursor_column,
                text_x,
                content.min.y,
                content.max.y,
                editor.scroll_y,
                line_height,
                char_width,
                inlay_hints
            );
            for (size_t index = 0u; index < editor.extra_cursors.size(); ++index) {
                EditorCursor const cursor = editor.extra_cursors[index];
                draw_editor_insert_cursor(
                    draw_context,
                    palette,
                    editor_visible_line_index(editor, cursor.line),
                    cursor.line,
                    cursor.column,
                    text_x,
                    content.min.y,
                    content.max.y,
                    editor.scroll_y,
                    line_height,
                    char_width,
                    inlay_hints
                );
            }
            draw::pop_clip_rect(draw_context);
        }

        draw_sticky_scope_lines(
            draw_context,
            editor_font,
            editor,
            palette,
            Slice<size_t const>(sticky_scope_lines, sticky_scope_count),
            tokenizer,
            semantic_tokens,
            full_content,
            text_x,
            text_min_x,
            line_height,
            char_width
        );

        draw::pop_clip_rect(draw_context);
        return clicked || middle_clicked || double_clicked || triple_clicked;
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

    [[nodiscard]] auto git_commit_graph_id(size_t commit_index) -> gui::Id {
        return gui::id("git_commit_graph", static_cast<uint64_t>(commit_index));
    }

    [[nodiscard]] auto to_draw_rect(gui::Rect rect) -> draw::Rect {
        return {{rect.min.x, rect.min.y}, {rect.max.x, rect.max.y}};
    }

    [[nodiscard]] auto git_graph_lane_count(EditorState const& editor) -> size_t {
        size_t count = 1u;
        for (GitCommit const& commit : editor.git_commits) {
            count = std::max(count, static_cast<size_t>(commit.graph_lane_count));
        }
        return std::min(count, GIT_GRAPH_MAX_LANES);
    }

    [[nodiscard]] auto git_graph_width(EditorState const& editor, float row_width) -> float {
        float const width = GIT_GRAPH_MIN_WIDTH +
                            GIT_GRAPH_LANE_WIDTH * static_cast<float>(git_graph_lane_count(editor));
        float const max_width = row_width > 0.0f ? std::min(GIT_GRAPH_MAX_WIDTH, row_width * 0.42f)
                                                 : GIT_GRAPH_MAX_WIDTH;
        return std::max(GIT_GRAPH_MIN_WIDTH, std::min(width, max_width));
    }

    struct GitGraphVirtualRange {
        size_t first = 0u;
        size_t end = 0u;
    };

    [[nodiscard]] auto git_graph_virtual_range(
        EditorState const& editor,
        gui::ScrollState scroll,
        size_t graph_start_row,
        float sidebar_content_height
    ) -> GitGraphVirtualRange {
        float const y = scroll.valid ? scroll.y : 0.0f;
        float const viewport =
            scroll.valid
                ? std::max(GIT_ROW_HEIGHT, scroll.viewport_height - GIT_PANEL_PADDING_Y * 2.0f)
                : sidebar_content_height;
        float const row = GIT_ROW_HEIGHT;
        size_t const top = y <= row * static_cast<float>(GIT_GRAPH_OVERSCAN_ROWS)
                               ? 0u
                               : static_cast<size_t>(y / row) - GIT_GRAPH_OVERSCAN_ROWS;
        size_t const bottom = static_cast<size_t>(std::ceil((y + std::max(viewport, row)) / row)) +
                              GIT_GRAPH_OVERSCAN_ROWS;
        size_t const clean_row = git_clean_status_row_visible(editor) ? 1u : 0u;
        size_t const graph_start = graph_start_row + GIT_PANEL_FIXED_SCROLL_ROWS + clean_row;
        GitGraphVirtualRange range = {};
        range.first = top > graph_start ? top - graph_start : 0u;
        range.end = bottom > graph_start ? bottom - graph_start : GIT_GRAPH_OVERSCAN_ROWS;
        range.end = std::max(range.first, range.end);
        return range;
    }

    auto draw_git_graph_spacer(gui::Frame& ui, float row_width, size_t rows) -> void {
        if (rows == 0u) {
            return;
        }
        ui.spacer({
            .layout = {
                .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                .height = gui::px(GIT_ROW_HEIGHT * static_cast<float>(rows)),
            },
        });
    }

    [[nodiscard]] auto git_any_commit_open(EditorState const& editor) -> bool {
        for (GitCommit const& commit : editor.git_commits) {
            if (commit.open) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto git_graph_lane_color(Palette const& palette, size_t lane) -> gui::Color {
        switch (lane % 8u) {
        case 0u:
            return palette.cursor;
        case 1u:
            return palette.type;
        case 2u:
            return palette.number;
        case 3u:
            return palette.preprocessor;
        case 4u:
            return palette.string;
        case 5u:
            return palette.keyword;
        case 6u:
            return palette.mode_insert;
        default:
            return palette.function;
        }
    }

    [[nodiscard]] auto git_graph_lane_x(gui::Rect rect, size_t lane, size_t lane_count) -> float {
        float const width = std::max(1.0f, rect.max.x - rect.min.x - 8.0f);
        float const step =
            lane_count > 1u
                ? std::min(GIT_GRAPH_LANE_WIDTH, width / static_cast<float>(lane_count - 1u))
                : GIT_GRAPH_LANE_WIDTH;
        return rect.min.x + 4.0f + step * static_cast<float>(lane);
    }

    [[nodiscard]] auto git_graph_rect_visible(gui::Rect rect, gui::Rect clip) -> bool {
        return rect.min.x < clip.max.x && rect.max.x > clip.min.x && rect.min.y < clip.max.y &&
               rect.max.y > clip.min.y;
    }

    [[nodiscard]] auto git_inset_rect(gui::Rect rect, float inset) -> gui::Rect {
        rect.min.x += inset;
        rect.min.y += inset;
        rect.max.x -= inset;
        rect.max.y -= inset;
        return rect;
    }

    auto draw_git_graph_segment(
        draw::Context context,
        Palette const& palette,
        gui::Rect rect,
        size_t lane_count,
        GitGraphSegment const& segment
    ) -> void {
        float const y0 = rect.min.y;
        float const yc = (rect.min.y + rect.max.y) * 0.5f;
        float const y1 = rect.max.y;
        float const x0 = git_graph_lane_x(rect, segment.from_lane, lane_count);
        float const x1 = git_graph_lane_x(rect, segment.to_lane, lane_count);
        draw::Vec2 from = {x0, y0};
        draw::Vec2 to = {x1, y1};
        if (segment.kind == GitGraphSegmentKind::TOP_TO_COMMIT) {
            to = {x0, yc};
        } else if (segment.kind == GitGraphSegmentKind::COMMIT_TO_BOTTOM) {
            from = {x0, yc};
        }
        gui::Color const color = git_graph_lane_color(palette, segment.to_lane);
        draw::draw_line(
            context,
            from,
            to,
            to_draw_color(gui::color_alpha(color, 0.82f)),
            GIT_GRAPH_LINE_THICKNESS
        );
    }

    auto draw_git_graph_overlay(
        draw::Context context,
        gui::Frame const& ui,
        EditorState const& editor,
        Palette const& palette
    ) -> void {
        if (!editor.flag(EditorFlag::SIDEBAR_VISIBLE) ||
            editor.sidebar_tab != EditorSidebarTab::GIT || !editor.git_graph_open) {
            return;
        }
        size_t const lane_count = git_graph_lane_count(editor);
        gui::Rect clip = {};
        size_t commit_index = 0u;
        for (size_t box_index = 0u; box_index < ui.box_info_count(); ++box_index) {
            gui::BoxInfo const* const box = ui.box_info(box_index);
            if (box == nullptr) {
                continue;
            }
            if (box->kind == gui::BoxKind::SCROLL_PANEL &&
                box->debug_name == "filesystem_surface") {
                clip = box->rect;
                continue;
            }
            if (box->kind != gui::BoxKind::LABEL || box->debug_name != GIT_GRAPH_BOX_DEBUG_NAME) {
                continue;
            }
            while (commit_index < editor.git_commits.size() &&
                   box->authored_id.value != git_commit_graph_id(commit_index).value) {
                commit_index += 1u;
            }
            if (commit_index >= editor.git_commits.size()) {
                break;
            }
            if (!git_graph_rect_visible(box->rect, clip)) {
                commit_index += 1u;
                continue;
            }
            draw::push_clip_rect(context, to_draw_rect(clip));
            GitCommit const& commit = editor.git_commits[commit_index];
            for (size_t segment = 0u; segment < commit.graph_segment_count; ++segment) {
                draw_git_graph_segment(
                    context, palette, box->rect, lane_count, commit.graph_segments[segment]
                );
            }
            gui::Color const node_color =
                commit.incoming ? palette.number : git_graph_lane_color(palette, commit.graph_lane);
            draw::draw_circle_filled(
                context,
                {git_graph_lane_x(box->rect, commit.graph_lane, lane_count),
                 (box->rect.min.y + box->rect.max.y) * 0.5f},
                GIT_GRAPH_NODE_RADIUS,
                to_draw_color(node_color),
                16
            );
            draw::pop_clip_rect(context);
            commit_index += 1u;
        }
    }

    [[nodiscard]] auto point_on_circle(draw::Vec2 center, float radius, float angle) -> draw::Vec2 {
        return {
            center.x + static_cast<float>(std::cos(angle)) * radius,
            center.y + static_cast<float>(std::sin(angle)) * radius,
        };
    }

    auto draw_git_loading_arc(
        draw::Context context,
        draw::Vec2 center,
        float radius,
        float start_angle,
        float end_angle,
        gui::Color color,
        float thickness
    ) -> void {
        draw::path_clear(context);
        draw::path_arc_to(context, center, radius, start_angle, end_angle, 36);
        draw::path_stroke(context, to_draw_color(color), false, thickness);
        draw::path_clear(context);
    }

    [[nodiscard]] auto git_operation_loading_overlay_visible(EditorState const& editor) -> bool {
        if (!editor.git_operation_pending || !editor.flag(EditorFlag::SIDEBAR_VISIBLE) ||
            editor.sidebar_tab != EditorSidebarTab::GIT) {
            return false;
        }
        switch (editor.git_pending_operation_kind) {
        case GitWorkKind::NONE:
        case GitWorkKind::COMMIT_PAGE:
        case GitWorkKind::COMMIT_FILES:
        case GitWorkKind::OPEN_STATUS_DIFF:
        case GitWorkKind::OPEN_COMMIT_DIFF:
            return false;
        default:
            return true;
        }
    }

    auto draw_git_loading_overlay_surface(
        draw::Context context,
        gui::Frame const& ui,
        EditorState const& editor,
        Palette const& palette
    ) -> void {
        if (!git_operation_loading_overlay_visible(editor)) {
            return;
        }

        gui::Rect clip_rect = {};
        for (size_t box_index = 0u; box_index < ui.box_info_count(); ++box_index) {
            gui::BoxInfo const* const box = ui.box_info(box_index);
            if (box != nullptr && box->kind == gui::BoxKind::SCROLL_PANEL &&
                box->debug_name == "filesystem_surface") {
                clip_rect = git_inset_rect(box->rect, 2.0f);
                break;
            }
        }
        if (clip_rect.max.x <= clip_rect.min.x || clip_rect.max.y <= clip_rect.min.y) {
            return;
        }

        draw::Rect const clip = to_draw_rect(clip_rect);
        draw::draw_rect_filled(
            context, clip, to_draw_color(gui::color_alpha(palette.panel, 0.88f)), 4.0f
        );
        draw::push_clip_rect(context, clip);

        draw::Vec2 const center = {
            (clip_rect.min.x + clip_rect.max.x) * 0.5f,
            (clip_rect.min.y + clip_rect.max.y) * 0.5f,
        };
        constexpr float PI = 3.14159265359f;
        float const radius = 24.0f;
        float const start = editor.git_loading_phase * PI * 2.0f - PI * 0.5f;
        float const end = start + PI * 1.45f;
        draw::draw_circle(
            context,
            center,
            radius,
            to_draw_color(gui::color_alpha(palette.border, 0.66f)),
            4.0f,
            64
        );
        draw_git_loading_arc(
            context, center, radius, start, end, gui::color_alpha(palette.cursor, 0.98f), 4.6f
        );
        draw::draw_circle_filled(
            context,
            point_on_circle(center, radius, end),
            4.4f,
            to_draw_color(gui::color_alpha(palette.cursor, 0.98f)),
            16
        );
        draw::pop_clip_rect(context);
    }

    auto draw_tree_edit_cursor(
        draw::Context context,
        gui::Frame const& ui,
        font_cache::Font ui_font,
        EditorState const& editor,
        Palette const& palette
    ) -> void;

    auto draw_editor_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        font_cache::Font ui_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette,
        bool selection_visible
    ) -> void {
        draw_deleted_open_file_tab_marks(draw_context, ui, editor, palette);
        draw_git_graph_overlay(draw_context, ui, editor, palette);
        draw_git_loading_overlay_surface(draw_context, ui, editor, palette);
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
        draw_tree_edit_cursor(draw_context, ui, ui_font, editor, palette);
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

    [[nodiscard]] auto tree_label_id(StrRef scope) -> gui::Id {
        return gui::id(gui::id(scope), "tree_label");
    }

    [[nodiscard]] auto tree_draft_label_id(size_t after_index) -> gui::Id {
        return gui::id("tree_draft_label", after_index == TREE_CURSOR_ROOT ? 0u : after_index + 1u);
    }

    [[nodiscard]] auto tree_edit_text(EditorState const& editor, StrRef fallback) -> StrRef {
        if (editor.tree_edit_mode == TreeEditMode::NONE || editor.text.arena == nullptr) {
            return fallback;
        }
        return text_buffer_copy(editor.text, thread_temp_arena());
    }

    [[nodiscard]] auto tree_label_padding_x(bool directory) -> float {
        return directory ? TREE_FOLDER_LABEL_PADDING_X : TREE_FILE_LABEL_PADDING_X;
    }

    [[nodiscard]] auto tree_row_width(
        font_cache::Font ui_font,
        float font_size,
        StrRef text,
        size_t guide_count,
        bool directory,
        float min_width
    ) -> float {
        float const text_width = font_cache::text_advance(ui_font, font_size, text);
        float const label_padding = tree_label_padding_x(directory);
        float const width = TREE_INDENT_WIDTH * static_cast<float>(guide_count) +
                            (directory ? TREE_ARROW_SLOT_WIDTH : 0.0f) + label_padding * 2.0f +
                            std::ceil(text_width);
        return std::max(min_width, width);
    }

    struct TreeDraftPlacement {
        size_t after_index = TREE_CURSOR_ROOT;
        size_t guide_count = 0u;
        bool active = false;
        bool directory = false;
    };

    [[nodiscard]] auto tree_draft_placement(EditorState const& editor) -> TreeDraftPlacement {
        TreeDraftPlacement placement = {};
        if (editor.tree_edit_mode != TreeEditMode::CREATE_FILE &&
            editor.tree_edit_mode != TreeEditMode::CREATE_DIRECTORY) {
            return placement;
        }
        placement.active = true;
        placement.directory = editor.tree_edit_mode == TreeEditMode::CREATE_DIRECTORY;
        if (editor.tree_cursor == TREE_CURSOR_ROOT) {
            placement.guide_count = 1u;
            return placement;
        }
        if (editor.tree_cursor >= editor.tree_files.size()) {
            placement.active = false;
            return placement;
        }
        FileTreeEntry const& entry = editor.tree_files[editor.tree_cursor];
        placement.after_index = editor.tree_cursor;
        placement.guide_count = entry.depth + (entry.is_directory ? 2u : 1u);
        return placement;
    }

    auto draw_tree_draft(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        size_t guide_count,
        size_t after_index,
        bool directory,
        bool cursor_focused,
        float row_min_width
    ) -> void {
        StrRef const text = tree_edit_text(editor, {});
        if (auto row = ui.row(
                gui::id("tree_draft"),
                {
                    .layout =
                        {.width = gui::px(tree_row_width(
                             ui_font, editor.font_size, text, guide_count, directory, row_min_width
                         )),
                         .height = gui::px(26.0f),
                         .align_y = gui::Align::STRETCH},
                    .style = {
                        .background =
                            gui::color_alpha(palette.cursor, cursor_focused ? 0.28f : 0.16f),
                        .border = cursor_focused ? palette.cursor
                                                 : gui::color_alpha(palette.cursor, 0.6f),
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                    },
                }
            )) {
            BASE_UNUSED(row);
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (directory) {
                if (auto arrow_slot = ui.row({
                        .layout = {
                            .width = gui::px(TREE_ARROW_SLOT_WIDTH),
                            .height = gui::fill(),
                            .align_x = gui::Align::END,
                            .align_y = gui::Align::CENTER,
                        },
                    })) {
                    BASE_UNUSED(arrow_slot);
                    ui.label(
                        TREE_ARROW_CLOSED,
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
            }
            ui.label(
                tree_draft_label_id(after_index),
                text,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, tree_label_padding_x(directory))},
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
        }
    }

    auto draw_tree_file(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font ui_font,
        FileTreeEntry const& file,
        size_t tree_file_index,
        size_t guide_count,
        bool cursor_focused,
        float row_min_width
    ) -> void {
        bool const editing =
            editor.tree_edit_mode == TreeEditMode::RENAME && editor.tree_cursor == tree_file_index;
        StrRef const text = editing ? tree_edit_text(editor, file.name) : file.name;
        bool const selected = editor.current_file_path == file.path;
        bool const cursor = editor.tree_cursor == tree_file_index;
        gui::Color background = selected ? gui::rgb(34, 45, 58) : gui::Color{};
        gui::Color border = {};
        float border_thickness = 0.0f;
        if (cursor) {
            background = gui::color_alpha(
                palette.cursor,
                selected ? (cursor_focused ? 0.42f : 0.28f) : (cursor_focused ? 0.28f : 0.16f)
            );
            border = cursor_focused ? palette.cursor : gui::color_alpha(palette.cursor, 0.6f);
            border_thickness = 1.0f;
        }
        gui::Id const row_id = gui::id(file.path);
        if (cursor && cursor_focused && editor.tree_cursor_reveal &&
            editor.tree_edit_mode == TreeEditMode::NONE) {
            ui.request_focus(row_id);
        }
        if (auto row = ui.row(
                row_id,
                {
                    .layout =
                        {
                            .width = gui::px(tree_row_width(
                                ui_font, editor.font_size, text, guide_count, false, row_min_width
                            )),
                            .height = gui::px(26.0f),
                            .align_y = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = background,
                            .border = border,
                            .border_thickness = border_thickness,
                            .radius = cursor || selected ? 5.0f : -1.0f,
                        },
                    .focusable = cursor_focused,
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            gui::Signal const signal = row.signal();
            if (signal.focused && !editor.tree_cursor_reveal &&
                editor.tree_edit_mode == TreeEditMode::NONE) {
                if (editor.tree_cursor != tree_file_index) {
                    editor.tree_cursor = tree_file_index;
                    editor.tree_cursor_reveal = true;
                }
            }
            if (signal.clicked_left && editor.tree_edit_mode == TreeEditMode::NONE) {
                editor.tree_cursor = tree_file_index;
                editor.tree_cursor_reveal = true;
                focus_code_split_for_open(editor);
                open_tree_file(editor, file);
            }
            ui.label(
                tree_label_id(file.path),
                text,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, TREE_FILE_LABEL_PADDING_X)},
                    .style = {
                        .foreground = selected || cursor || file.file_search_visible
                                          ? palette.text
                                          : palette.muted,
                        .font_size = editor.font_size,
                    },
                }
            );
        }
    }

    auto draw_tree_folder(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        StrRef id_text,
        StrRef text,
        size_t cursor_index,
        size_t guide_count,
        bool tracked,
        bool cursor_focused,
        bool* open,
        float row_min_width
    ) -> void {
        bool const editing =
            editor.tree_edit_mode == TreeEditMode::RENAME && editor.tree_cursor == cursor_index;
        StrRef const label_text = editing ? tree_edit_text(editor, text) : text;
        bool const cursor = editor.tree_cursor == cursor_index;
        gui::Color background =
            cursor ? gui::color_alpha(palette.cursor, cursor_focused ? 0.28f : 0.16f)
                   : gui::Color{};
        gui::Color border =
            cursor ? (cursor_focused ? palette.cursor : gui::color_alpha(palette.cursor, 0.6f))
                   : gui::Color{};
        gui::Id const row_id = gui::id(id_text);
        if (cursor && cursor_focused && editor.tree_cursor_reveal &&
            editor.tree_edit_mode == TreeEditMode::NONE) {
            ui.request_focus(row_id);
        }
        if (auto row = ui.row(
                row_id,
                {
                    .layout =
                        {
                            .width = gui::px(tree_row_width(
                                ui_font,
                                editor.font_size,
                                label_text,
                                guide_count,
                                true,
                                row_min_width
                            )),
                            .height = gui::px(26.0f),
                            .align_y = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = background,
                            .border = border,
                            .border_thickness = cursor ? 1.0f : 0.0f,
                            .radius = cursor ? 5.0f : -1.0f,
                        },
                    .focusable = cursor_focused,
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (auto arrow_slot = ui.row({
                    .layout = {
                        .width = gui::px(TREE_ARROW_SLOT_WIDTH),
                        .height = gui::fill(),
                        .align_x = gui::Align::END,
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
                tree_label_id(id_text),
                label_text,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, TREE_FOLDER_LABEL_PADDING_X)},
                    .style = {
                        .foreground = cursor || tracked ? palette.text : palette.muted,
                        .font_size = editor.font_size,
                    },
                }
            );
            gui::Signal const signal = row.signal();
            if (signal.focused && !editor.tree_cursor_reveal &&
                editor.tree_edit_mode == TreeEditMode::NONE) {
                if (editor.tree_cursor != cursor_index) {
                    editor.tree_cursor = cursor_index;
                    editor.tree_cursor_reveal = true;
                }
            }
            if (open != nullptr && signal.clicked_left &&
                editor.tree_edit_mode == TreeEditMode::NONE) {
                editor.tree_cursor = cursor_index;
                editor.tree_cursor_reveal = true;
                *open = !*open;
            }
        }
    }

    auto draw_tree_edit_cursor(
        draw::Context context,
        gui::Frame const& ui,
        font_cache::Font ui_font,
        EditorState const& editor,
        Palette const& palette
    ) -> void {
        if (editor.tree_edit_mode == TreeEditMode::NONE || editor.text.arena == nullptr) {
            return;
        }

        gui::Id label_id = {};
        switch (editor.tree_edit_mode) {
        case TreeEditMode::RENAME:
            if (editor.tree_cursor >= editor.tree_files.size()) {
                return;
            }
            label_id = tree_label_id(editor.tree_files[editor.tree_cursor].path);
            break;
        case TreeEditMode::CREATE_FILE:
        case TreeEditMode::CREATE_DIRECTORY:
            label_id = tree_draft_label_id(tree_draft_placement(editor).after_index);
            break;
        case TreeEditMode::NONE:
            return;
        }

        gui::BoxInfo const* const box = ui.find_box(label_id, gui::BoxKind::LABEL);
        if (box == nullptr) {
            return;
        }

        Arena& arena = thread_temp_arena();
        StrRef const text = text_buffer_copy(editor.text, arena);
        size_t const cursor = std::min(editor.cursor_column, text.size());
        float const x = std::round(
            box->rect.min.x + box->layout.padding.left +
            font_cache::text_advance(ui_font, editor.font_size, text.prefix(cursor))
        );
        draw::push_clip_rect(
            context, {{box->rect.min.x, box->rect.min.y}, {box->rect.max.x, box->rect.max.y}}
        );
        draw::draw_rect_filled(
            context,
            {{x, std::round(box->rect.min.y + 2.0f)},
             {x + 2.0f, std::round(box->rect.max.y - 2.0f)}},
            to_draw_color(palette.mode_insert),
            0.0f
        );
        draw::pop_clip_rect(context);
    }

    auto draw_tree_entry(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        size_t tree_file_index,
        FileTreeEntry& entry,
        bool cursor_focused,
        float row_min_width
    ) -> void {
        size_t const guide_count = entry.depth + 1u;
        if (entry.is_directory) {
            draw_tree_folder(
                ui,
                editor,
                palette,
                ui_font,
                icon_font,
                entry.path,
                entry.name,
                tree_file_index,
                guide_count,
                entry.file_search_visible,
                cursor_focused,
                &entry.open,
                row_min_width
            );
        } else {
            draw_tree_file(
                ui,
                editor,
                palette,
                ui_font,
                entry,
                tree_file_index,
                guide_count,
                cursor_focused,
                row_min_width
            );
        }
    }

    auto draw_sidebar(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width
    ) -> void {
        float const width = sidebar_width(editor, client_width);
        float const row_min_width = std::max(0.0f, width - TREE_PANEL_PADDING_X * 2.0f);
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
            TreeDraftPlacement const draft = tree_draft_placement(editor);
            draw_tree_folder(
                ui,
                editor,
                palette,
                ui_font,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                TREE_CURSOR_ROOT,
                0u,
                true,
                false,
                &tree_open,
                row_min_width
            );
            editor.set_flag(EditorFlag::TREE_OPEN, tree_open);
            if (tree_open) {
                if (draft.active && draft.after_index == TREE_CURSOR_ROOT) {
                    draw_tree_draft(
                        ui,
                        editor,
                        palette,
                        ui_font,
                        icon_font,
                        draft.guide_count,
                        draft.after_index,
                        draft.directory,
                        false,
                        row_min_width
                    );
                }
                size_t closed_depth = static_cast<size_t>(-1);
                for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
                    FileTreeEntry& entry = editor.tree_files[index];
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    draw_tree_entry(
                        ui, editor, palette, ui_font, icon_font, index, entry, false, row_min_width
                    );
                    if (draft.active && draft.after_index == index) {
                        draw_tree_draft(
                            ui,
                            editor,
                            palette,
                            ui_font,
                            icon_font,
                            draft.guide_count,
                            draft.after_index,
                            draft.directory,
                            false,
                            row_min_width
                        );
                    }
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
        }
    }

    [[nodiscard]] auto git_status_display_label(GitFileStatus status) -> StrRef {
        if (status == GitFileStatus::UNTRACKED) {
            return "U";
        }
        return git_file_status_label(status);
    }

    [[nodiscard]] auto git_status_color(Palette const& palette, GitFileStatus status)
        -> gui::Color {
        switch (status) {
        case GitFileStatus::ADDED:
        case GitFileStatus::UNTRACKED:
            return palette.mode_insert;
        case GitFileStatus::DELETED:
        case GitFileStatus::UNMERGED:
            return palette.preprocessor;
        case GitFileStatus::RENAMED:
        case GitFileStatus::TYPE_CHANGED:
            return palette.number;
        case GitFileStatus::MODIFIED:
        default:
            return palette.string;
        }
    }

    [[nodiscard]] auto git_parent_label(StrRef path) -> StrRef {
        StrRef const parent = render_path_parent(path);
        return parent == "." ? StrRef() : parent;
    }

    [[nodiscard]] auto git_commit_ref_label(GitCommit const& commit) -> StrRef {
        StrRef first = {};
        for (StrRef ref : commit.refs.split(",")) {
            StrRef label = ref.trim();
            if (label.starts_with("HEAD -> ")) {
                label = label.drop_prefix(8u).trim();
            }
            if (label.empty() || label == "HEAD") {
                continue;
            }
            if (first.empty()) {
                first = label;
            }
            if (commit.incoming && label.contains('/')) {
                return label;
            }
        }
        return first;
    }

    [[nodiscard]] auto git_branch_search_query(EditorState const& editor) -> StrRef {
        return StrRef(editor.git_branch_search_text).trim();
    }

    [[nodiscard]] auto git_commit_search_query(EditorState const& editor) -> StrRef {
        return StrRef(editor.git_commit_search_text).trim();
    }

    [[nodiscard]] auto git_action_ref(EditorState const& editor) -> StrRef {
        return StrRef(editor.git_action_ref_text).trim();
    }

    [[nodiscard]] auto git_matching_branch_count(EditorState const& editor) -> size_t {
        size_t count = 0u;
        StrRef const query = git_branch_search_query(editor);
        for (GitBranch const& branch : editor.git_branches) {
            if (git_branch_matches_search(branch, query)) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto git_matching_commit_count(EditorState const& editor) -> size_t {
        size_t count = 0u;
        StrRef const query = git_commit_search_query(editor);
        for (GitCommit const& commit : editor.git_commits) {
            if (git_commit_matches_search(commit, query)) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto draw_git_text_input(
        gui::Frame& ui,
        gui::Id box_id,
        gui::Id input_id,
        StrRef placeholder,
        char* text,
        size_t capacity,
        EditorState& editor,
        Palette const& palette,
        gui::Size width,
        float height
    ) -> gui::Signal {
        gui::Signal signal = {};
        if (auto box = ui.overlay(
                box_id,
                {
                    .layout =
                        {
                            .width = width,
                            .height = gui::px(height),
                        },
                    .style = {
                        .background = palette.panel_raised,
                        .foreground = palette.text,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                        .font_size = editor.font_size,
                    },
                }
            )) {
            BASE_UNUSED(box);
            if (StrRef(text).empty()) {
                ui.label(
                    gui::id(input_id, "placeholder"),
                    placeholder,
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 8.0f),
                                .clip = true,
                            },
                        .style = {.foreground = palette.muted, .font_size = editor.font_size},
                    }
                );
            }
            signal = ui.input_text(
                input_id,
                "",
                text,
                capacity,
                gui::InputTextDesc{
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(0.0f, 8.0f),
                                },
                            .style =
                                {
                                    .background = gui::rgba(0, 0, 0, 0),
                                    .foreground = palette.text,
                                    .border = gui::rgba(0, 0, 0, 0),
                                    .border_thickness = 0.0f,
                                    .radius = 0.0f,
                                    .font_size = editor.font_size,
                                },
                        },
                    .edit_on_enter = true,
                }
            );
            if (signal.focused) {
                editor.git_control_focused = true;
                editor.git_selection_focused = false;
            }
            editor.git_text_editing |= signal.text_edit_active;
        }
        return signal;
    }

    auto sync_git_control_focus(EditorState& editor, gui::Signal signal) -> void;

    auto draw_git_panel_header(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        font_cache::Font branch_icon_font,
        float row_width
    ) -> void {
        if (auto header = ui.column(
                gui::id("git_panel_header"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::children(),
                        .gap = 6.0f,
                    },
                }
            )) {
            BASE_UNUSED(header);
            gui::BoxDesc const control_desc = {
                .layout = {.width = gui::px(24.0f), .height = gui::px(24.0f)},
                .style = {
                    .foreground = palette.text,
                    .border = palette.border,
                    .border_thickness = 1.0f,
                    .radius = 4.0f,
                    .font_size = editor.font_size,
                },
            };
            gui::BoxDesc refresh_desc = control_desc;
            refresh_desc.style.font = icon_font;
            refresh_desc.style.font_size = editor_scaled_font_size(editor, 11.0f);
            StrRef const branch =
                editor.git_current_branch.empty() ? StrRef("HEAD") : editor.git_current_branch;
            if (auto branch_row = ui.row(
                    gui::id("git_branch_row"),
                    {
                        .layout = {
                            .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                            .height = gui::px(GIT_ROW_HEIGHT),
                            .gap = 6.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                BASE_UNUSED(branch_row);
                if (auto branch_label = ui.row(
                        gui::id("git_branch_label_frame"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(0.0f, 7.0f),
                                    .gap = 5.0f,
                                    .align_y = gui::Align::CENTER,
                                    .clip = true,
                                },
                            .style = {
                                .background = palette.panel_raised,
                                .radius = 4.0f,
                            },
                        }
                    )) {
                    BASE_UNUSED(branch_label);
                    ui.label(
                        gui::id("git_branch_icon"),
                        GIT_BRANCH_ICON,
                        {
                            .layout = {.width = gui::px(16.0f), .height = gui::fill()},
                            .style = {
                                .foreground = palette.text,
                                .font = branch_icon_font,
                                .font_size = editor_scaled_font_size(editor, 12.5f),
                            },
                        }
                    );
                    gui::Signal const branch_label_signal = ui.selectable_label(
                        gui::id("git_branch_label"),
                        branch,
                        &editor.git_branch_selection,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .clip = true,
                                },
                            .style = {.foreground = palette.text, .font_size = editor.font_size},
                        }
                    );
                    sync_git_control_focus(editor, branch_label_signal);
                }
                gui::BoxDesc dropdown_desc = control_desc;
                dropdown_desc.style.font = icon_font;
                dropdown_desc.style.font_size = editor_scaled_font_size(editor, 9.5f);
                gui::Signal const dropdown =
                    ui.button(gui::id("git_branch_dropdown"), TREE_ARROW_OPEN, dropdown_desc);
                sync_git_control_focus(editor, dropdown);
                if (dropdown.activated) {
                    editor.git_branches_open = !editor.git_branches_open;
                }
            }
            if (auto actions = ui.row(
                    gui::id("git_panel_actions"),
                    {
                        .layout = {
                            .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                            .height = gui::px(24.0f),
                            .gap = 4.0f,
                        },
                    }
                )) {
                BASE_UNUSED(actions);
                refresh_desc.layout.width = gui::fill();
                refresh_desc.flags =
                    editor.git_operation_pending ? gui::BOX_FLAG_DISABLED : gui::BOX_FLAG_NONE;
                gui::Signal const refresh =
                    ui.button(gui::id("git_refresh"), GIT_REFRESH_ICON, refresh_desc);
                sync_git_control_focus(editor, refresh);
                if (refresh.activated) {
                    editor.git_request = {.kind = GitRequestKind::REFRESH};
                }
                gui::BoxDesc fetch_desc = control_desc;
                fetch_desc.layout.width = gui::fill();
                fetch_desc.flags = refresh_desc.flags;
                gui::Signal const fetch =
                    ui.button(gui::id("git_fetch"), GIT_FETCH_ICON, fetch_desc);
                sync_git_control_focus(editor, fetch);
                if (fetch.activated) {
                    editor.git_request = {.kind = GitRequestKind::FETCH};
                }
            }
        }
    }

    auto draw_git_branch_list(
        gui::Frame& ui, EditorState& editor, Palette const& palette, float sidebar_content_height
    ) -> void {
        if (!editor.git_branches_open) {
            return;
        }
        StrRef const query = git_branch_search_query(editor);
        size_t const match_count = git_matching_branch_count(editor);
        size_t const row_count = std::max(match_count + 1u, static_cast<size_t>(2u));
        float const content_height = GIT_BRANCH_ROW_HEIGHT * static_cast<float>(row_count) +
                                     GIT_BRANCH_ROW_GAP * static_cast<float>(row_count - 1u);
        float const max_height = std::max(
            GIT_BRANCH_ROW_HEIGHT,
            sidebar_content_height - GIT_BRANCH_LIST_TOP - GIT_BRANCH_LIST_PADDING * 2.0f
        );
        float const list_height = std::min(content_height, max_height);
        if (auto list = ui.popup(
                gui::id("git_branch_list"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .margin = gui::insets(GIT_BRANCH_LIST_TOP, 0.0f, 0.0f, 0.0f),
                            .padding = gui::insets(GIT_BRANCH_LIST_PADDING),
                        },
                    .style = {
                        .background = palette.panel_raised,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                    },
                }
            )) {
            if (auto scroller = ui.scroll_panel(
                    gui::id("git_branch_list_scroll"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(list_height),
                            .gap = GIT_BRANCH_ROW_GAP,
                        },
                    }
                )) {
                gui::Signal const search = draw_git_text_input(
                    ui,
                    gui::id("git_branch_search_box"),
                    gui::id("git_branch_search_input"),
                    "Search branches",
                    editor.git_branch_search_text,
                    GIT_SEARCH_TEXT_CAPACITY,
                    editor,
                    palette,
                    gui::fill(),
                    GIT_BRANCH_ROW_HEIGHT
                );
                editor.git_branch_search_focused = search.focused;
                if (search.changed) {
                    editor.git_branch_search_text_size = cstr_len(editor.git_branch_search_text);
                }
                if (editor.git_branches.empty()) {
                    ui.label(
                        "No branches",
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(24.0f),
                                    .padding = gui::insets(0.0f, 7.0f),
                                },
                            .style = {.foreground = palette.muted, .font_size = editor.font_size},
                        }
                    );
                    return;
                }
                if (match_count == 0u) {
                    ui.label(
                        query.empty() ? StrRef("No branches") : StrRef("No matching branches"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(24.0f),
                                    .padding = gui::insets(0.0f, 7.0f),
                                },
                            .style = {.foreground = palette.muted, .font_size = editor.font_size},
                        }
                    );
                    return;
                }
                for (size_t index = 0u; index < editor.git_branches.size(); ++index) {
                    GitBranch const& branch = editor.git_branches[index];
                    if (!git_branch_matches_search(branch, query)) {
                        continue;
                    }
                    bool const current = branch.name == editor.git_current_branch;
                    if (auto row = ui.row(
                            gui::id("git_branch_row", index),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::children(),
                                        .min_height = gui::px(GIT_BRANCH_ROW_HEIGHT),
                                        .padding = gui::insets(0.0f, GIT_BRANCH_ROW_PADDING_X),
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style = {
                                    .background = current ? gui::color_alpha(palette.cursor, 0.22f)
                                                          : gui::Color{},
                                    .border = current ? gui::color_alpha(palette.cursor, 0.6f)
                                                      : gui::Color{},
                                    .border_thickness = current ? 1.0f : 0.0f,
                                    .radius = current ? 4.0f : -1.0f,
                                },
                            }
                        )) {
                        ui.label(
                            branch.name,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::text(),
                                        .clip = true,
                                        .word_wrap = true,
                                    },
                                .style = {
                                    .foreground = palette.text, .font_size = editor.font_size
                                },
                            }
                        );
                        if (row.signal().clicked_left) {
                            editor.git_branches_open = false;
                            if (!current) {
                                editor.git_request = {
                                    .kind = GitRequestKind::CHECKOUT_BRANCH,
                                    .branch = branch.name,
                                };
                            }
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] auto git_commit_ready(EditorState const& editor) -> bool {
        if (editor.git_commit_text.str().trim().empty()) {
            return false;
        }
        for (GitStatusItem const& item : editor.git_status_items) {
            if (item.scope == GitStatusScope::STAGED) {
                return true;
            }
        }
        return false;
    }

    auto draw_git_commit_controls(gui::Frame& ui, EditorState& editor, Palette const& palette)
        -> void {
        gui::Signal input = {};
        if (auto input_box = ui.overlay(
                gui::id("git_inline_commit_box"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .min_height = gui::px(30.0f),
                            .max_height = gui::px(192.0f),
                            .margin = gui::insets(4.0f, 0.0f),
                        },
                    .style = {
                        .background = palette.panel_raised,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                    },
                }
            )) {
            BASE_UNUSED(input_box);
            if (editor.git_commit_text.empty()) {
                ui.label(
                    gui::id("git_inline_commit_placeholder"),
                    "Message",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 8.0f),
                            },
                        .style = {.foreground = palette.muted, .font_size = editor.font_size},
                    }
                );
            }
            input = ui.input_text_multiline(
                gui::id("git_inline_commit_input"),
                "",
                &editor.git_commit_text,
                {
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::text(),
                                    .min_height = gui::px(30.0f),
                                    .max_height = gui::px(192.0f),
                                    .padding = gui::insets(4.0f, 8.0f),
                                },
                            .style =
                                {
                                    .background = gui::rgba(0, 0, 0, 0),
                                    .foreground = palette.text,
                                    .border = gui::rgba(0, 0, 0, 0),
                                    .border_thickness = 0.0f,
                                    .radius = 0.0f,
                                    .font_size = editor.font_size,
                                },
                        },
                    .edit_on_enter = true,
                }
            );
        }
        if (input.changed) {
            editor.git_status_text = {};
        }
        if (input.focused) {
            editor.git_control_focused = true;
            editor.git_selection_focused = false;
        }
        editor.git_text_editing |= input.text_edit_active;
        editor.git_commit_text_focused = input.focused;
        bool const commit_enabled = git_commit_ready(editor);
        if (input.activated && commit_enabled) {
            submit_git_commit(editor);
        }

        gui::BoxDesc const button_desc = {
            .layout =
                {
                    .width = gui::fill(),
                    .height = gui::px(28.0f),
                    .margin = gui::insets(0.0f, 0.0f, 6.0f, 0.0f),
                    .padding = gui::insets(0.0f, 10.0f),
                },
            .style =
                {
                    .background = commit_enabled ? palette.cursor : palette.panel,
                    .foreground = commit_enabled ? palette.text : palette.faint,
                    .border = commit_enabled ? palette.cursor : palette.border,
                    .border_thickness = 1.0f,
                    .radius = 4.0f,
                    .font_size = editor.font_size,
                },
            .flags = commit_enabled ? gui::BOX_FLAG_NONE : gui::BOX_FLAG_DISABLED,
        };
        gui::Signal const commit_button =
            ui.button(gui::id("git_inline_commit"), "Commit", button_desc);
        sync_git_control_focus(editor, commit_button);
        if (commit_button.activated) {
            submit_git_commit(editor);
        }
    }

    [[nodiscard]] auto git_operation_label(GitOperationState state) -> StrRef {
        switch (state) {
        case GitOperationState::MERGE:
            return "Merge paused. Resolve conflicts, stage files, then commit or abort.";
        case GitOperationState::REBASE:
            return "Rebase paused. Resolve conflicts, stage files, then continue or abort.";
        case GitOperationState::CHERRY_PICK:
            return "Cherry-pick paused. Resolve conflicts, stage files, then continue or abort.";
        case GitOperationState::NONE:
        default:
            return {};
        }
    }

    auto draw_git_operation_controls(
        gui::Frame& ui, EditorState& editor, Palette const& palette, float row_width
    ) -> void {
        StrRef const label = git_operation_label(editor.git_operation_state);
        if (label.empty()) {
            return;
        }
        if (auto panel = ui.column(
                gui::id("git_operation_panel"),
                {
                    .layout =
                        {
                            .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(6.0f),
                            .gap = 6.0f,
                        },
                    .style = {
                        .background = gui::color_alpha(palette.preprocessor, 0.14f),
                        .border = gui::color_alpha(palette.preprocessor, 0.62f),
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                    },
                }
            )) {
            BASE_UNUSED(panel);
            ui.label(
                label,
                {
                    .layout = {.width = gui::fill(), .height = gui::text(), .word_wrap = true},
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
            if (auto row = ui.row(
                    gui::id("git_operation_buttons"),
                    {.layout = {.width = gui::fill(), .height = gui::px(24.0f), .gap = 6.0f}}
                )) {
                BASE_UNUSED(row);
                gui::BoxDesc button_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 8.0f),
                        },
                    .style =
                        {
                            .background = palette.panel_raised,
                            .foreground = palette.text,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                            .font_size = editor.font_size,
                        },
                    .flags =
                        editor.git_operation_pending ? gui::BOX_FLAG_DISABLED : gui::BOX_FLAG_NONE,
                };
                if (editor.git_operation_state == GitOperationState::REBASE) {
                    gui::Signal const continue_button =
                        ui.button(gui::id("git_rebase_continue"), "Continue", button_desc);
                    sync_git_control_focus(editor, continue_button);
                    if (continue_button.activated) {
                        editor.git_request = {.kind = GitRequestKind::REBASE_CONTINUE};
                    }
                } else if (editor.git_operation_state == GitOperationState::CHERRY_PICK) {
                    gui::Signal const continue_button =
                        ui.button(gui::id("git_cherry_pick_continue"), "Continue", button_desc);
                    sync_git_control_focus(editor, continue_button);
                    if (continue_button.activated) {
                        editor.git_request = {.kind = GitRequestKind::CHERRY_PICK_CONTINUE};
                    }
                }
                gui::Signal const abort =
                    ui.button(gui::id("git_operation_abort"), "Abort", button_desc);
                sync_git_control_focus(editor, abort);
                if (abort.activated) {
                    if (editor.git_operation_state == GitOperationState::MERGE) {
                        editor.git_request = {.kind = GitRequestKind::MERGE_ABORT};
                    } else if (editor.git_operation_state == GitOperationState::REBASE) {
                        editor.git_request = {.kind = GitRequestKind::REBASE_ABORT};
                    } else {
                        editor.git_request = {.kind = GitRequestKind::CHERRY_PICK_ABORT};
                    }
                }
            }
        }
    }

    auto draw_git_ref_actions(
        gui::Frame& ui, EditorState& editor, Palette const& palette, float row_width
    ) -> void {
        if (auto panel = ui.column(
                gui::id("git_ref_actions"),
                {
                    .layout = {
                        .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                        .height = gui::children(),
                        .gap = 6.0f,
                    },
                }
            )) {
            BASE_UNUSED(panel);
            bool const can_mutate = !editor.git_operation_pending &&
                                    editor.git_operation_state == GitOperationState::NONE;
            bool const pull_visible = editor.git_operation_state == GitOperationState::NONE &&
                                      editor.git_pending_pull_count != 0u;
            bool const push_visible = editor.git_pending_push_count != 0u;
            if (pull_visible || push_visible) {
                gui::BoxDesc const sync_desc = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(28.0f),
                            .padding = gui::insets(0.0f, 10.0f),
                        },
                    .style =
                        {
                            .background = can_mutate ? palette.cursor : palette.panel,
                            .foreground = can_mutate ? palette.text : palette.faint,
                            .border = can_mutate ? palette.cursor : palette.border,
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                            .font_size = editor.font_size,
                        },
                    .flags = can_mutate ? gui::BOX_FLAG_NONE : gui::BOX_FLAG_DISABLED,
                };
                if (pull_visible) {
                    gui::Signal const pull = ui.button(
                        gui::id("git_pull_changes"),
                        fmt::tprintf("Pull (%zu)", editor.git_pending_pull_count),
                        sync_desc
                    );
                    sync_git_control_focus(editor, pull);
                    if (pull.activated) {
                        editor.git_request = {.kind = GitRequestKind::PULL};
                    }
                }
                if (push_visible) {
                    gui::Signal const push = ui.button(
                        gui::id("git_push_changes"),
                        fmt::tprintf("Push (%zu)", editor.git_pending_push_count),
                        sync_desc
                    );
                    sync_git_control_focus(editor, push);
                    if (push.activated) {
                        editor.git_request = {.kind = GitRequestKind::PUSH};
                    }
                }
            }
            gui::Signal const ref_input = draw_git_text_input(
                ui,
                gui::id("git_action_ref_box"),
                gui::id("git_action_ref_input"),
                "Branch or commit",
                editor.git_action_ref_text,
                GIT_SEARCH_TEXT_CAPACITY,
                editor,
                palette,
                gui::fill(),
                26.0f
            );
            editor.git_action_ref_focused = ref_input.focused;
            if (ref_input.focused) {
                editor.git_control_focused = true;
            }
            if (ref_input.changed) {
                editor.git_action_ref_text_size = cstr_len(editor.git_action_ref_text);
                editor.git_status_text = {};
            }
            bool const enabled = !editor.git_operation_pending &&
                                 editor.git_operation_state == GitOperationState::NONE &&
                                 !git_action_ref(editor).empty();
            gui::BoxDesc button_desc = {
                .layout =
                    {
                        .width = gui::fill(),
                        .height = gui::px(24.0f),
                        .padding = gui::insets(0.0f, 8.0f),
                    },
                .style =
                    {
                        .background = enabled ? palette.panel_raised : palette.panel,
                        .foreground = enabled ? palette.text : palette.faint,
                        .border = enabled ? palette.border : gui::color_alpha(palette.border, 0.6f),
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                        .font_size = editor.font_size,
                    },
                .flags = enabled ? gui::BOX_FLAG_NONE : gui::BOX_FLAG_DISABLED,
            };
            if (auto row = ui.row(
                    gui::id("git_ref_action_buttons"),
                    {.layout = {.width = gui::fill(), .height = gui::px(24.0f), .gap = 6.0f}}
                )) {
                BASE_UNUSED(row);
                StrRef const ref = git_action_ref(editor);
                gui::Signal const merge =
                    ui.button(gui::id("git_merge_branch"), "Merge", button_desc);
                sync_git_control_focus(editor, merge);
                if (merge.activated) {
                    editor.git_request = {.kind = GitRequestKind::MERGE_BRANCH, .branch = ref};
                }
                gui::Signal const rebase =
                    ui.button(gui::id("git_rebase_branch"), "Rebase", button_desc);
                sync_git_control_focus(editor, rebase);
                if (rebase.activated) {
                    editor.git_request = {.kind = GitRequestKind::REBASE_BRANCH, .branch = ref};
                }
                gui::Signal const pick = ui.button(gui::id("git_cherry_pick"), "Pick", button_desc);
                sync_git_control_focus(editor, pick);
                if (pick.activated) {
                    editor.git_request = {.kind = GitRequestKind::CHERRY_PICK, .branch = ref};
                }
            }
        }
    }

    [[nodiscard]] auto git_status_stageable(GitStatusItem const& item) -> bool {
        return item.scope == GitStatusScope::UNSTAGED || item.scope == GitStatusScope::UNTRACKED;
    }

    [[nodiscard]] auto git_row_id(size_t row_index) -> gui::Id {
        return gui::id("git_row", row_index);
    }

    auto sync_git_row_focus(EditorState& editor, gui::Signal signal, size_t row_index) -> void {
        if (signal.focused && !editor.git_cursor_reveal) {
            editor.git_selected = row_index;
            editor.git_selection_focused = true;
        } else if (signal.focus_lost && editor.git_selected == row_index) {
            editor.git_selection_focused = false;
        }
    }

    auto sync_git_control_focus(EditorState& editor, gui::Signal signal) -> void {
        if (signal.focused) {
            editor.git_control_focused = true;
            editor.git_selection_focused = false;
        }
    }

    auto draw_git_changes_header(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        StrRef title,
        size_t count,
        GitRequestKind action,
        size_t row_index,
        bool open,
        float row_width,
        bool focused
    ) -> bool {
        bool const selected =
            focused && editor.git_selection_focused && editor.git_selected == row_index;
        gui::Id const row_id = git_row_id(row_index);
        if (focused && editor.git_cursor_reveal && editor.git_selected == row_index) {
            ui.request_focus(row_id);
        }
        if (auto header = ui.row(
                row_id,
                {
                    .layout =
                        {
                            .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                            .height = gui::px(24.0f),
                            .padding = gui::insets(0.0f, 6.0f, 0.0f, 6.0f),
                            .gap = 4.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style =
                        {
                            .background =
                                selected ? gui::color_alpha(palette.cursor, focused ? 0.28f : 0.16f)
                                         : gui::Color{},
                            .border = selected ? (focused ? palette.cursor
                                                          : gui::color_alpha(palette.cursor, 0.6f))
                                               : gui::Color{},
                            .border_thickness = selected ? 1.0f : 0.0f,
                            .radius = selected ? 4.0f : -1.0f,
                        },
                    .focusable = focused,
                }
            )) {
            ui.label(
                open ? TREE_ARROW_OPEN : TREE_ARROW_CLOSED,
                {
                    .layout = {.width = gui::px(TREE_ARROW_SLOT_WIDTH), .height = gui::fill()},
                    .style = {
                        .foreground = palette.muted,
                        .font = icon_font,
                        .font_size = editor_scaled_font_size(editor, 9.5f),
                    },
                }
            );
            ui.label(
                title,
                {
                    .layout = {.width = gui::fill(), .height = gui::fill(), .clip = true},
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
            bool action_activated = false;
            if (count != 0u) {
                gui::BoxDesc const button_desc = {
                    .layout = {.width = gui::px(20.0f), .height = gui::px(20.0f)},
                    .style = {
                        .background = palette.panel,
                        .foreground = palette.text,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                        .font_size = editor.font_size,
                    },
                };
                StrRef const button_text = action == GitRequestKind::STAGE_ALL ? "+" : "-";
                gui::Signal const button = ui.button(
                    gui::id(gui::id("git_status_group_action"), title), button_text, button_desc
                );
                sync_git_control_focus(editor, button);
                if (button.activated) {
                    action_activated = true;
                    editor.git_request = {.kind = action};
                }
            }
            gui::Signal const signal = header.signal();
            sync_git_row_focus(editor, signal, row_index);
            if (signal.clicked_left && !action_activated) {
                editor.git_selected = row_index;
                editor.git_selection_focused = true;
            }
            return signal.clicked_left && !action_activated;
        }
        return false;
    }

    auto draw_git_label(
        gui::Frame& ui,
        EditorState const& editor,
        Palette const& palette,
        StrRef text,
        gui::Color color,
        float indent = 0.0f
    ) -> void {
        ui.label(
            text,
            {
                .layout =
                    {
                        .width = gui::fill(),
                        .height = gui::px(GIT_ROW_HEIGHT),
                        .padding = gui::insets(0.0f, 6.0f + indent),
                    },
                .style = {.foreground = color, .font_size = editor.font_size},
            }
        );
        BASE_UNUSED(palette);
    }

    auto draw_git_row_frame(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        size_t row_index,
        float row_width,
        float indent,
        bool focused,
        gui::Color highlight = {}
    ) -> gui::Scope {
        bool const selected =
            focused && editor.git_selection_focused && editor.git_selected == row_index;
        bool const highlighted = highlight.a >= 0.0f;
        gui::Id const row_id = git_row_id(row_index);
        if (focused && editor.git_cursor_reveal && editor.git_selected == row_index) {
            ui.request_focus(row_id);
        }
        return ui.row(
            row_id,
            {
                .layout =
                    {
                        .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                        .height = gui::px(GIT_ROW_HEIGHT),
                        .padding = gui::insets(0.0f, 6.0f + indent),
                        .gap = 4.0f,
                        .align_y = gui::Align::CENTER,
                    },
                .style =
                    {
                        .background =
                            selected ? gui::color_alpha(palette.cursor, focused ? 0.28f : 0.16f)
                            : highlighted ? gui::color_alpha(highlight, 0.14f)
                                          : gui::Color{},
                        .border = selected      ? (focused ? palette.cursor
                                                           : gui::color_alpha(palette.cursor, 0.6f))
                                  : highlighted ? gui::color_alpha(highlight, 0.55f)
                                                : gui::Color{},
                        .border_thickness = selected || highlighted ? 1.0f : 0.0f,
                        .radius = selected || highlighted ? 4.0f : -1.0f,
                    },
                .focusable = focused,
            }
        );
    }

    [[nodiscard]] auto git_commit_popup_header(GitCommit const& commit) -> StrRef {
        if (commit.author.empty()) {
            return commit.short_oid;
        }
        if (commit.relative_date.empty()) {
            return commit.author;
        }
        if (commit.author_date.empty()) {
            return fmt::tprintf("%s, %s", commit.author, commit.relative_date);
        }
        return fmt::tprintf("%s, %s (%s)", commit.author, commit.relative_date, commit.author_date);
    }

    [[nodiscard]] auto git_commit_popup_width(float row_width) -> float {
        return std::clamp(
            row_width + 120.0f, GIT_COMMIT_POPUP_MIN_WIDTH, GIT_COMMIT_POPUP_MAX_WIDTH
        );
    }

    [[nodiscard]] auto git_commit_popup_stats_tail(GitCommit const& commit) -> bool {
        return commit.insertion_count != 0u || commit.deletion_count != 0u;
    }

    auto append_git_commit_popup_stats(StringBuffer& text, GitCommit const& commit) -> void {
        if (commit.changed_file_count == 0u) {
            return;
        }
        BASE_UNUSED(text.write_string(
            fmt::tprintf(
                "%zu file%s changed",
                commit.changed_file_count,
                commit.changed_file_count == 1u ? "" : "s"
            )
        ));
        if (git_commit_popup_stats_tail(commit)) {
            BASE_UNUSED(text.write_byte(','));
        }
        if (commit.insertion_count != 0u) {
            BASE_UNUSED(text.write_string(
                fmt::tprintf(
                    " %zu insertion%s(+)",
                    commit.insertion_count,
                    commit.insertion_count == 1u ? "" : "s"
                )
            ));
            if (commit.deletion_count != 0u) {
                BASE_UNUSED(text.write_byte(','));
            }
        }
        if (commit.deletion_count != 0u) {
            BASE_UNUSED(text.write_string(
                fmt::tprintf(
                    " %zu deletion%s(-)",
                    commit.deletion_count,
                    commit.deletion_count == 1u ? "" : "s"
                )
            ));
        }
    }

    [[nodiscard]] auto git_commit_conflicts_header(StrRef line) -> bool {
        return line.trim() == "# Conflicts:";
    }

    [[nodiscard]] auto git_commit_conflict_line(StrRef line) -> StrRef {
        line = line.trim_end_matches('\r');
        if (line == "#") {
            return {};
        }
        if (line.starts_with("#\t")) {
            return line.substr(2u).trim();
        }
        if (line.starts_with("# ")) {
            return line.substr(2u).trim();
        }
        return {};
    }

    auto append_git_commit_popup_line(StringBuffer& text, StrRef line) -> void {
        if (!text.empty()) {
            BASE_UNUSED(text.write_byte('\n'));
        }
        BASE_UNUSED(text.write_string(line));
    }

    auto append_git_commit_popup_message(StringBuffer& text, GitCommit const& commit) -> void {
        append_git_commit_popup_line(text, commit.summary);

        size_t offset = 0u;
        size_t line_count = 0u;
        bool conflicts = false;
        while (offset < commit.body.size() && line_count < GIT_COMMIT_POPUP_MAX_BODY_LINES) {
            StrRef const line = next_text_line(commit.body, offset);
            if (git_commit_conflicts_header(line)) {
                append_git_commit_popup_line(text, "Conflicts:");
                conflicts = true;
                line_count += 1u;
                continue;
            }
            if (conflicts) {
                StrRef const conflict = git_commit_conflict_line(line);
                if (conflict.empty()) {
                    continue;
                }
                append_git_commit_popup_line(text, conflict);
                line_count += 1u;
                continue;
            }
            append_git_commit_popup_line(text, line);
            line_count += 1u;
        }
        if (offset < commit.body.size()) {
            BASE_UNUSED(text.write_string("\n..."));
        }
    }

    [[nodiscard]] auto git_commit_popup_text(Arena& arena, GitCommit const& commit) -> StrRef {
        StringBuffer text = {};
        BASE_UNUSED(text.init(512u + commit.body.size() + commit.refs.size(), arena.resource()));
        BASE_UNUSED(text.write_string(git_commit_popup_header(commit)));
        append_git_commit_popup_message(text, commit);
        if (commit.changed_file_count != 0u) {
            BASE_UNUSED(text.write_byte('\n'));
            append_git_commit_popup_stats(text, commit);
        }

        BASE_UNUSED(text.write_byte('\n'));
        BASE_UNUSED(text.write_string(commit.short_oid));
        if (!commit.refs.empty()) {
            BASE_UNUSED(text.write_byte(' '));
            BASE_UNUSED(text.write_string(commit.refs));
        }
        return text.str();
    }

    auto draw_git_commit_popup_label(
        gui::Frame& ui, gui::Id id, StrRef text, gui::Color color, float font_size, bool bold
    ) -> void {
        if (text.empty()) {
            return;
        }
        gui::BoxDesc const label_desc = {
            .layout = {.width = gui::fill(), .height = gui::text(), .word_wrap = true},
            .style = {.foreground = color, .font_size = font_size},
        };
        if (!bold) {
            ui.label(id, text, label_desc);
            return;
        }
        if (auto overlay =
                ui.overlay(id, {.layout = {.width = gui::fill(), .height = gui::children()}})) {
            BASE_UNUSED(overlay);
            ui.label(gui::id("bold_base"), text, label_desc);
            gui::BoxDesc offset_desc = label_desc;
            offset_desc.layout.margin.left = 0.45f;
            ui.label(gui::id("bold_offset"), text, offset_desc);
        }
    }

    auto draw_git_commit_popup_header_line(
        gui::Frame& ui, EditorState const& editor, Palette const& palette, GitCommit const& commit
    ) -> void {
        if (auto overlay = ui.overlay(
                gui::id("git_commit_popup_header"),
                {.layout = {.width = gui::fill(), .height = gui::children()}}
            )) {
            BASE_UNUSED(overlay);
            draw_git_commit_popup_label(
                ui,
                gui::id("git_commit_popup_header_text"),
                git_commit_popup_header(commit),
                commit.author.empty() ? palette.text : palette.muted,
                editor.font_size,
                false
            );
            if (!commit.author.empty()) {
                ui.label(
                    gui::id("git_commit_popup_author"),
                    commit.author,
                    {
                        .layout = {.width = gui::text(), .height = gui::text()},
                        .style = {.foreground = palette.cursor, .font_size = editor.font_size},
                    }
                );
            }
        }
    }

    auto draw_git_commit_popup_stats(
        gui::Frame& ui, EditorState const& editor, Palette const& palette, GitCommit const& commit
    ) -> void {
        if (commit.changed_file_count == 0u) {
            return;
        }
        Arena& arena = thread_temp_arena();
        StringBuffer text = {};
        BASE_UNUSED(text.init(128u, arena.resource()));
        append_git_commit_popup_stats(text, commit);
        if (auto overlay = ui.overlay(
                gui::id("git_commit_popup_stats"),
                {.layout = {.width = gui::fill(), .height = gui::children()}}
            )) {
            BASE_UNUSED(overlay);
            draw_git_commit_popup_label(
                ui,
                gui::id("git_commit_popup_stats_text"),
                text.str(),
                palette.text,
                editor.font_size,
                false
            );
            if (auto row = ui.row(
                    gui::id("git_commit_popup_stats_color"),
                    {.layout = {.width = gui::fill(), .height = gui::children()}}
                )) {
                BASE_UNUSED(row);
                StrRef const files = fmt::tprintf(
                    "%zu file%s changed%s",
                    commit.changed_file_count,
                    commit.changed_file_count == 1u ? "" : "s",
                    git_commit_popup_stats_tail(commit) ? "," : ""
                );
                ui.label(
                    gui::id("git_commit_popup_stats_files"),
                    files,
                    {
                        .layout = {.width = gui::text(), .height = gui::text()},
                        .style = {.foreground = palette.text, .font_size = editor.font_size},
                    }
                );
                if (commit.insertion_count != 0u) {
                    StrRef const insertions = fmt::tprintf(
                        " %zu insertion%s(+)%s",
                        commit.insertion_count,
                        commit.insertion_count == 1u ? "" : "s",
                        commit.deletion_count != 0u ? "," : ""
                    );
                    ui.label(
                        gui::id("git_commit_popup_stats_insertions"),
                        insertions,
                        {
                            .layout = {.width = gui::text(), .height = gui::text()},
                            .style = {
                                .foreground = palette.mode_insert, .font_size = editor.font_size
                            },
                        }
                    );
                }
                if (commit.deletion_count != 0u) {
                    StrRef const deletions = fmt::tprintf(
                        " %zu deletion%s(-)",
                        commit.deletion_count,
                        commit.deletion_count == 1u ? "" : "s"
                    );
                    ui.label(
                        gui::id("git_commit_popup_stats_deletions"),
                        deletions,
                        {
                            .layout = {.width = gui::text(), .height = gui::text()},
                            .style = {
                                .foreground = palette.preprocessor, .font_size = editor.font_size
                            },
                        }
                    );
                }
            }
        }
    }

    [[nodiscard]] auto git_commit_popup_footer_text(GitCommit const& commit) -> StrRef {
        if (commit.refs.empty()) {
            return commit.short_oid;
        }
        return fmt::tprintf("%s %s", commit.short_oid, commit.refs);
    }

    auto draw_git_commit_popup_footer(
        gui::Frame& ui, EditorState const& editor, Palette const& palette, GitCommit const& commit
    ) -> void {
        if (auto overlay = ui.overlay(
                gui::id("git_commit_popup_footer"),
                {.layout = {.width = gui::fill(), .height = gui::children()}}
            )) {
            BASE_UNUSED(overlay);
            draw_git_commit_popup_label(
                ui,
                gui::id("git_commit_popup_footer_text"),
                git_commit_popup_footer_text(commit),
                palette.text,
                editor.font_size,
                false
            );
            ui.label(
                gui::id("git_commit_popup_footer_oid"),
                commit.short_oid,
                {
                    .layout = {.width = gui::text(), .height = gui::text()},
                    .style = {.foreground = palette.cursor, .font_size = editor.font_size},
                }
            );
        }
    }

    auto draw_git_commit_popup_message(
        gui::Frame& ui, EditorState const& editor, Palette const& palette, GitCommit const& commit
    ) -> void {
        Arena& arena = thread_temp_arena();
        StringBuffer normal = {};
        BASE_UNUSED(normal.init(256u + commit.body.size(), arena.resource()));
        append_git_commit_popup_line(normal, commit.summary);
        size_t part_index = 0u;

        auto flush_normal = [&]() -> void {
            if (normal.empty()) {
                return;
            }
            draw_git_commit_popup_label(
                ui,
                gui::id("git_commit_popup_message", part_index),
                normal.str(),
                palette.text,
                editor.font_size,
                false
            );
            part_index += 1u;
            normal.reset();
        };

        size_t offset = 0u;
        size_t line_count = 0u;
        bool conflicts = false;
        while (offset < commit.body.size() && line_count < GIT_COMMIT_POPUP_MAX_BODY_LINES) {
            StrRef const line = next_text_line(commit.body, offset);
            if (git_commit_conflicts_header(line)) {
                flush_normal();
                draw_git_commit_popup_label(
                    ui,
                    gui::id("git_commit_popup_conflicts_header"),
                    "Conflicts:",
                    palette.text,
                    editor.font_size,
                    true
                );
                conflicts = true;
                line_count += 1u;
                continue;
            }
            if (conflicts) {
                StrRef const conflict = git_commit_conflict_line(line);
                if (conflict.empty()) {
                    continue;
                }
                draw_git_commit_popup_label(
                    ui,
                    gui::id("git_commit_popup_conflict_path", line_count),
                    conflict,
                    palette.text,
                    editor.font_size,
                    true
                );
                line_count += 1u;
                continue;
            }
            append_git_commit_popup_line(normal, line);
            line_count += 1u;
        }
        if (offset < commit.body.size()) {
            append_git_commit_popup_line(normal, "...");
        }
        flush_normal();
    }

    [[nodiscard]] auto draw_git_commit_popup_body(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitCommit const& commit,
        float popup_width
    ) -> gui::Signal {
        Arena& arena = thread_temp_arena();
        StrRef const text = git_commit_popup_text(arena, commit);
        float const text_width = std::max(0.0f, popup_width - 16.0f);
        if (auto body = ui.overlay(
                gui::id("git_commit_popup_body"),
                {.layout = {.width = gui::fill(), .height = gui::children()}}
            )) {
            BASE_UNUSED(body);
            gui::Signal const text_signal = ui.selectable_label(
                gui::id("git_commit_popup_text"),
                text,
                &editor.git_commit_popup_selection,
                {
                    .layout =
                        {
                            .width = gui::px(text_width),
                            .height = gui::text(),
                            .show_scrollbars = false,
                            .word_wrap = true,
                        },
                    .style = {
                        .foreground = gui::color_alpha(palette.text, 0.0f),
                        .font_size = editor.font_size,
                    },
                }
            );
            sync_git_control_focus(editor, text_signal);
            if (auto visible = ui.column(
                    gui::id("git_commit_popup_visible"),
                    {.layout = {.width = gui::fill(), .height = gui::children()}}
                )) {
                BASE_UNUSED(visible);
                draw_git_commit_popup_header_line(ui, editor, palette, commit);
                draw_git_commit_popup_message(ui, editor, palette, commit);
                draw_git_commit_popup_stats(ui, editor, palette, commit);
                draw_git_commit_popup_footer(ui, editor, palette, commit);
            }
            return text_signal;
        }
        return {};
    }

    [[nodiscard]] auto draw_git_commit_popup(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitCommit const& commit,
        size_t commit_index,
        gui::Signal row_signal,
        float row_width,
        float graph_width,
        bool open
    ) -> bool {
        float const popup_width = git_commit_popup_width(row_width);
        gui::BoxDesc const desc = {
            .layout =
                {
                    .width = gui::px(popup_width),
                    .height = gui::children(),
                    .padding = gui::insets(8.0f),
                    .gap = 6.0f,
                },
            .style =
                {
                    .background = palette.panel_raised,
                    .border = palette.border,
                    .border_thickness = 1.0f,
                    .radius = 5.0f,
                },
            .debug_name = "git_commit_popup",
        };
        gui::Scope popup = ui.popup_above(
            gui::id("git_commit_popup", commit_index),
            {
                .source = row_signal,
                .box = desc,
                .offset_x = graph_width + 10.0f,
                .gap = 2.0f,
                .open = open,
            }
        );
        if (!popup) {
            return false;
        }
        gui::Signal const popup_signal = popup.signal();
        gui::Signal const text_signal =
            draw_git_commit_popup_body(ui, editor, palette, commit, popup_width);
        return popup_signal.hovered || text_signal.hovered || text_signal.active;
    }

    [[nodiscard]] auto git_commit_popup_mouse_moved(EditorState const& editor, gui::Vec2 mouse)
        -> bool {
        return mouse.x != editor.git_commit_popup_mouse_pos.x ||
               mouse.y != editor.git_commit_popup_mouse_pos.y;
    }

    auto update_git_commit_popup_lifetime(
        EditorState& editor,
        size_t commit_index,
        bool source_hovered,
        bool popup_hovered,
        gui::InputState const& input
    ) -> void {
        if (editor.git_commit_popup != commit_index) {
            return;
        }
        if (source_hovered || popup_hovered) {
            editor.git_commit_popup_keyboard = false;
            editor.git_commit_popup_mouse_known = false;
            return;
        }
        if (!editor.git_commit_popup_keyboard) {
            editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
            editor.git_commit_popup_selection = {};
            editor.git_commit_popup_mouse_known = false;
            return;
        }
        if (!editor.git_commit_popup_mouse_known) {
            editor.git_commit_popup_mouse_pos = input.mouse_pos;
            editor.git_commit_popup_mouse_known = true;
            return;
        }
        if (git_commit_popup_mouse_moved(editor, input.mouse_pos)) {
            editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
            editor.git_commit_popup_selection = {};
            editor.git_commit_popup_keyboard = false;
            editor.git_commit_popup_mouse_known = false;
        }
    }

    struct GitStatusRowSignal {
        gui::Signal row = {};
        bool stage = false;
        bool unstage = false;
    };

    auto draw_git_status_row(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitStatusItem const& item,
        size_t row_index,
        float row_width,
        bool focused
    ) -> GitStatusRowSignal {
        if (auto row =
                draw_git_row_frame(ui, editor, palette, row_index, row_width, 0.0f, focused)) {
            gui::Signal const signal = row.signal();
            sync_git_row_focus(editor, signal, row_index);
            if (signal.clicked_left) {
                editor.git_selected = row_index;
                editor.git_selection_focused = true;
            }
            GitStatusRowSignal result = {.row = signal};
            float constexpr child_indent = 10.0f;
            StrRef const status = git_status_display_label(item.status);
            gui::Color const status_color = git_status_color(palette, item.status);
            StrRef const parent = git_parent_label(item.path);
            bool const has_stage_button =
                git_status_stageable(item) || item.scope == GitStatusScope::STAGED;
            float const button_width = has_stage_button ? 20.0f : 0.0f;
            float const gap_count = has_stage_button ? 4.0f : 3.0f;
            float const name_max_width = std::max(
                0.0f, row_width - child_indent - 12.0f - 18.0f - button_width - 4.0f * gap_count
            );
            ui.spacer({.layout = {.width = gui::px(child_indent), .height = gui::px(1.0f)}});
            ui.label(
                gui::id("git_status_left", row_index),
                status,
                {
                    .layout = {.width = gui::px(18.0f), .height = gui::fill()},
                    .style = {.foreground = status_color, .font_size = editor.font_size},
                }
            );
            ui.label(
                gui::id("git_status_name", row_index),
                render_path_leaf(item.path),
                {
                    .layout =
                        {
                            .width = gui::text(),
                            .height = gui::fill(),
                            .max_width = gui::px(name_max_width),
                            .clip = true,
                        },
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
            if (!parent.empty()) {
                ui.label(
                    gui::id("git_status_parent", row_index),
                    parent,
                    {
                        .layout = {.width = gui::fill(), .height = gui::fill(), .clip = true},
                        .style = {.foreground = palette.muted, .font_size = editor.font_size},
                    }
                );
            } else {
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
            }
            if (has_stage_button) {
                gui::BoxDesc const button_desc = {
                    .layout = {.width = gui::px(20.0f), .height = gui::px(20.0f)},
                    .style = {
                        .background = palette.panel,
                        .foreground = palette.text,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 4.0f,
                        .font_size = editor.font_size,
                    },
                };
                bool const stage = item.scope != GitStatusScope::STAGED;
                gui::Signal const button = ui.button(
                    gui::id("git_status_stage", row_index), stage ? "+" : "-", button_desc
                );
                sync_git_control_focus(editor, button);
                result.stage = button.activated && stage;
                result.unstage = button.activated && !stage;
            }
            return result;
        }
        return {};
    }

    auto draw_git_commit_row(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitCommit& commit,
        size_t commit_index,
        size_t row_index,
        float row_width,
        float graph_width,
        bool focused,
        gui::InputState const& input
    ) -> gui::Signal {
        gui::Color const graph_color = commit.incoming ? palette.number : palette.cursor;
        gui::Signal signal = {};
        {
            auto row = draw_git_row_frame(
                ui,
                editor,
                palette,
                row_index,
                row_width,
                0.0f,
                focused,
                commit.incoming ? graph_color : gui::Color{}
            );
            if (!row) {
                return {};
            }
            signal = row.signal();
            sync_git_row_focus(editor, signal, row_index);
            if (signal.clicked_left) {
                editor.git_selected = row_index;
                editor.git_selection_focused = true;
                editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
                editor.git_commit_popup_selection = {};
                editor.git_commit_popup_keyboard = false;
                editor.git_commit_popup_mouse_known = false;
            }
            if (signal.hovered && !editor.git_commits_loading &&
                editor.git_commit_popup != commit_index) {
                editor.git_commit_popup = commit_index;
                editor.git_commit_popup_selection = {};
                editor.git_commit_popup_keyboard = false;
                editor.git_commit_popup_mouse_known = false;
            }
            ui.label(
                git_commit_graph_id(commit_index),
                "",
                {
                    .layout =
                        {
                            .width = gui::px(graph_width),
                            .height = gui::fill(),
                        },
                    .style = {.foreground = graph_color, .font_size = editor.font_size},
                    .debug_name = GIT_GRAPH_BOX_DEBUG_NAME,
                }
            );
            ui.label(
                gui::id("git_commit_summary", row_index),
                commit.summary,
                {
                    .layout = {.width = gui::fill(), .height = gui::fill(), .clip = true},
                    .style = {.foreground = palette.text, .font_size = editor.font_size},
                }
            );
            StrRef const ref_label = git_commit_ref_label(commit);
            if (!ref_label.empty()) {
                ui.label(
                    gui::id("git_commit_ref", row_index),
                    ref_label,
                    {
                        .layout =
                            {
                                .width = gui::px(92.0f),
                                .height = gui::px(18.0f),
                                .padding = gui::insets(0.0f, 5.0f),
                                .clip = true,
                            },
                        .style = {
                            .background = gui::color_alpha(graph_color, 0.18f),
                            .foreground = graph_color,
                            .border = gui::color_alpha(graph_color, 0.72f),
                            .border_thickness = 1.0f,
                            .radius = 4.0f,
                            .font_size = editor_scaled_font_size(editor, 11.0f),
                        },
                    }
                );
            }
            ui.label(
                gui::id("git_commit_oid", row_index),
                commit.short_oid,
                {
                    .layout = {.width = gui::px(58.0f), .height = gui::fill(), .clip = true},
                    .style = {.foreground = palette.muted, .font_size = editor.font_size},
                }
            );
        }
        bool const open_popup =
            !editor.git_commits_loading && editor.git_commit_popup == commit_index;
        if (!editor.git_commits_loading &&
            (open_popup || editor.git_commit_popup == GIT_COMMIT_POPUP_NONE)) {
            bool const popup_hovered = draw_git_commit_popup(
                ui,
                editor,
                palette,
                commit,
                commit_index,
                signal,
                row_width,
                graph_width,
                open_popup
            );
            if (open_popup) {
                update_git_commit_popup_lifetime(
                    editor, commit_index, signal.hovered, popup_hovered, input
                );
            }
        }
        return signal;
    }

    auto draw_git_commit_file_row(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitCommitFile const& file,
        size_t row_index,
        float row_width,
        bool focused
    ) -> gui::Signal {
        if (auto row =
                draw_git_row_frame(ui, editor, palette, row_index, row_width, 18.0f, focused)) {
            gui::Signal const signal = row.signal();
            sync_git_row_focus(editor, signal, row_index);
            if (signal.clicked_left) {
                editor.git_selected = row_index;
                editor.git_selection_focused = true;
            }
            StrRef const status = git_status_display_label(file.status);
            gui::Color const status_color = git_status_color(palette, file.status);
            ui.label(
                gui::id("git_commit_file_status", row_index),
                status,
                {
                    .layout = {.width = gui::px(18.0f), .height = gui::fill()},
                    .style = {.foreground = status_color, .font_size = editor.font_size},
                }
            );
            ui.label(
                gui::id("git_commit_file_path", row_index),
                file.path,
                {
                    .layout = {.width = gui::fill(), .height = gui::fill(), .clip = true},
                    .style = {.foreground = palette.muted, .font_size = editor.font_size},
                }
            );
            return signal;
        }
        return {};
    }

    auto draw_closed_git_commit_graph(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        size_t& row_index,
        float row_width,
        float graph_width,
        bool focused,
        GitGraphVirtualRange range,
        gui::InputState const& input
    ) -> void {
        size_t const commit_count = editor.git_commits.size();
        size_t const first = std::min(range.first, commit_count);
        size_t const end = std::min(range.end, commit_count);
        draw_git_graph_spacer(ui, row_width, first);
        row_index += first;
        StrRef const query = git_commit_search_query(editor);
        for (size_t commit_index = first; commit_index < end; ++commit_index) {
            GitCommit& commit = editor.git_commits[commit_index];
            if (!git_commit_matches_search(commit, query)) {
                continue;
            }
            gui::Signal const signal = draw_git_commit_row(
                ui,
                editor,
                palette,
                commit,
                commit_index,
                row_index,
                row_width,
                graph_width,
                focused,
                input
            );
            if (signal.clicked_left) {
                commit.open = true;
            }
            row_index += 1u;
        }
        draw_git_graph_spacer(ui, row_width, commit_count - end);
        row_index += commit_count - end;
    }

    auto draw_open_git_commit_graph(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        size_t& row_index,
        float row_width,
        float graph_width,
        bool focused,
        GitGraphVirtualRange range,
        gui::InputState const& input
    ) -> void {
        size_t graph_row = 0u;
        size_t skipped_rows = 0u;
        StrRef const query = git_commit_search_query(editor);
        for (size_t commit_index = 0u; commit_index < editor.git_commits.size(); ++commit_index) {
            GitCommit& commit = editor.git_commits[commit_index];
            if (!git_commit_matches_search(commit, query)) {
                continue;
            }
            bool const commit_visible = graph_row >= range.first && graph_row < range.end;
            if (commit_visible) {
                draw_git_graph_spacer(ui, row_width, skipped_rows);
                skipped_rows = 0u;
                gui::Signal const signal = draw_git_commit_row(
                    ui,
                    editor,
                    palette,
                    commit,
                    commit_index,
                    row_index,
                    row_width,
                    graph_width,
                    focused,
                    input
                );
                if (signal.clicked_left) {
                    commit.open = false;
                }
            } else {
                skipped_rows += 1u;
            }
            row_index += 1u;
            graph_row += 1u;
            if (!commit.open) {
                continue;
            }
            for (GitCommitFile const& file : editor.git_commit_files) {
                if (file.commit_oid != commit.oid) {
                    continue;
                }
                bool const file_visible = graph_row >= range.first && graph_row < range.end;
                if (file_visible) {
                    draw_git_graph_spacer(ui, row_width, skipped_rows);
                    skipped_rows = 0u;
                    gui::Signal const file_signal = draw_git_commit_file_row(
                        ui, editor, palette, file, row_index, row_width, focused
                    );
                    if (file_signal.clicked_left) {
                        editor.git_request = {
                            .kind = GitRequestKind::OPEN_COMMIT_DIFF,
                            .path = file.path,
                            .old_path = file.old_path,
                            .commit_oid = file.commit_oid,
                        };
                    }
                } else {
                    skipped_rows += 1u;
                }
                row_index += 1u;
                graph_row += 1u;
            }
        }
        draw_git_graph_spacer(ui, row_width, skipped_rows);
    }

    [[nodiscard]] auto loading_alpha(float phase, size_t index) -> float {
        float offset = phase - static_cast<float>(index) * 0.18f;
        if (offset < 0.0f) {
            offset += 1.0f;
        }
        float const pulse = offset < 0.5f ? offset * 2.0f : (1.0f - offset) * 2.0f;
        return 0.24f + pulse * 0.62f;
    }

    auto draw_git_graph_rows(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        size_t row_index,
        float row_width,
        float graph_width,
        bool focused,
        GitGraphVirtualRange range,
        gui::InputState const& input
    ) -> void {
        if (auto overlay = ui.overlay(
                gui::id("git_graph_rows"),
                {
                    .layout = {
                        .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                        .height = gui::children(),
                    },
                }
            )) {
            BASE_UNUSED(overlay);
            if (auto rows = ui.column(
                    gui::id("git_graph_row_content"),
                    {.layout = {.width = gui::fill(), .height = gui::children()}}
                )) {
                BASE_UNUSED(rows);
                if (git_any_commit_open(editor)) {
                    draw_open_git_commit_graph(
                        ui,
                        editor,
                        palette,
                        row_index,
                        row_width,
                        graph_width,
                        focused,
                        range,
                        input
                    );
                } else {
                    draw_closed_git_commit_graph(
                        ui,
                        editor,
                        palette,
                        row_index,
                        row_width,
                        graph_width,
                        focused,
                        range,
                        input
                    );
                }
            }
        }
    }

    auto draw_filesystem_loading_row(
        gui::Frame& ui, EditorState const& editor, Palette const& palette, float row_width
    ) -> void {
        if (auto row = ui.row(
                gui::id("filesystem_loading_row"),
                {
                    .layout = {
                        .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                        .height = gui::px(26.0f),
                        .gap = 6.0f,
                        .align_y = gui::Align::CENTER,
                    },
                }
            )) {
            BASE_UNUSED(row);
            draw_tree_guide(ui, palette);
            ui.label(
                "Loading files",
                {
                    .layout = {.width = gui::text(), .height = gui::fill()},
                    .style = {.foreground = palette.muted, .font_size = editor.font_size},
                }
            );
            for (size_t index = 0u; index < 3u; ++index) {
                ui.spacer({
                    .layout = {.width = gui::px(5.0f), .height = gui::px(5.0f)},
                    .style = {
                        .background = gui::color_alpha(
                            palette.cursor, loading_alpha(editor.git_loading_phase, index)
                        ),
                        .radius = 2.5f,
                    },
                });
            }
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
        }
    }

    auto draw_git_commit_search(
        gui::Frame& ui, EditorState& editor, Palette const& palette, float row_width
    ) -> void {
        gui::Signal const search = draw_git_text_input(
            ui,
            gui::id("git_commit_search_box"),
            gui::id("git_commit_search_input"),
            "Search commits",
            editor.git_commit_search_text,
            GIT_SEARCH_TEXT_CAPACITY,
            editor,
            palette,
            row_width > 0.0f ? gui::px(row_width) : gui::fill(),
            26.0f
        );
        editor.git_commit_search_focused = search.focused;
        if (search.changed) {
            editor.git_commit_search_text_size = cstr_len(editor.git_commit_search_text);
            editor.git_selected = 0u;
            editor.git_cursor_reveal = true;
        }
        StrRef const query = git_commit_search_query(editor);
        if (!query.empty()) {
            ui.label(
                fmt::tprintf(
                    "%zu/%zu commits", git_matching_commit_count(editor), editor.git_commits.size()
                ),
                {
                    .layout =
                        {
                            .width = row_width > 0.0f ? gui::px(row_width) : gui::fill(),
                            .height = gui::px(20.0f),
                            .padding = gui::insets(0.0f, 6.0f),
                        },
                    .style = {.foreground = palette.muted, .font_size = editor.font_size},
                }
            );
        }
    }

    [[nodiscard]] auto git_status_scope_count(EditorState const& editor, GitStatusScope scope)
        -> size_t {
        size_t count = 0u;
        for (GitStatusItem const& item : editor.git_status_items) {
            if (item.scope == scope) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto git_status_change_count(EditorState const& editor) -> size_t {
        return git_status_scope_count(editor, GitStatusScope::UNSTAGED) +
               git_status_scope_count(editor, GitStatusScope::UNTRACKED);
    }

    auto draw_git_status_items(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        GitStatusScope scope,
        size_t& row_index,
        float row_width,
        bool focused
    ) -> void {
        for (size_t index = 0u; index < editor.git_status_items.size(); ++index) {
            GitStatusItem const& item = editor.git_status_items[index];
            if (item.scope != scope) {
                continue;
            }
            GitStatusRowSignal const signal =
                draw_git_status_row(ui, editor, palette, item, row_index, row_width, focused);
            if (signal.stage) {
                editor.git_request = {
                    .kind = GitRequestKind::STAGE,
                    .path = item.path,
                };
            } else if (signal.unstage) {
                editor.git_request = {
                    .kind = GitRequestKind::UNSTAGE,
                    .path = item.path,
                };
            } else if (signal.row.clicked_left) {
                editor.git_request = {
                    .kind = GitRequestKind::OPEN_STATUS_DIFF,
                    .scope = item.scope,
                    .path = item.path,
                    .old_path = item.old_path,
                };
            }
            row_index += 1u;
        }
    }

    auto draw_git_panel_content(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        font_cache::Font branch_icon_font,
        float row_width,
        float sidebar_content_height,
        gui::ScrollState scroll,
        bool focused,
        gui::InputState const& input
    ) -> void {
        if (!focused && editor.git_control_focused) {
            ui.clear_focus();
        }
        editor.git_branch_search_focused = false;
        editor.git_commit_search_focused = false;
        editor.git_action_ref_focused = false;
        editor.git_control_focused = false;
        editor.git_text_editing = false;
        size_t row_index = 0u;
        draw_git_panel_header(ui, editor, palette, icon_font, branch_icon_font, row_width);
        draw_git_branch_list(ui, editor, palette, sidebar_content_height);
        ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(8.0f)}});
        draw_git_ref_actions(ui, editor, palette, row_width);
        draw_git_commit_controls(ui, editor, palette);
        draw_git_operation_controls(ui, editor, palette, row_width);
        if (editor.git_status_items.empty()) {
            if (draw_git_changes_header(
                    ui,
                    editor,
                    palette,
                    icon_font,
                    "Changes",
                    0u,
                    GitRequestKind::STAGE_ALL,
                    row_index,
                    editor.git_changes_open,
                    row_width,
                    focused
                )) {
                editor.git_changes_open = !editor.git_changes_open;
            }
            row_index += 1u;
            if (editor.git_changes_open && !editor.git_operation_pending) {
                draw_git_label(ui, editor, palette, "Working tree clean", palette.muted, 8.0f);
            }
        } else {
            size_t const staged_count = git_status_scope_count(editor, GitStatusScope::STAGED);
            if (staged_count != 0u) {
                if (draw_git_changes_header(
                        ui,
                        editor,
                        palette,
                        icon_font,
                        "Staged Changes",
                        staged_count,
                        GitRequestKind::UNSTAGE_ALL,
                        row_index,
                        editor.git_staged_open,
                        row_width,
                        focused
                    )) {
                    editor.git_staged_open = !editor.git_staged_open;
                }
                row_index += 1u;
                if (editor.git_staged_open) {
                    draw_git_status_items(
                        ui, editor, palette, GitStatusScope::STAGED, row_index, row_width, focused
                    );
                }
            }
            size_t const change_count = git_status_change_count(editor);
            if (change_count != 0u) {
                if (draw_git_changes_header(
                        ui,
                        editor,
                        palette,
                        icon_font,
                        "Changes",
                        change_count,
                        GitRequestKind::STAGE_ALL,
                        row_index,
                        editor.git_changes_open,
                        row_width,
                        focused
                    )) {
                    editor.git_changes_open = !editor.git_changes_open;
                }
                row_index += 1u;
                if (editor.git_changes_open) {
                    draw_git_status_items(
                        ui, editor, palette, GitStatusScope::UNSTAGED, row_index, row_width, focused
                    );
                    draw_git_status_items(
                        ui,
                        editor,
                        palette,
                        GitStatusScope::UNTRACKED,
                        row_index,
                        row_width,
                        focused
                    );
                }
            }
        }

        if (draw_git_changes_header(
                ui,
                editor,
                palette,
                icon_font,
                "Graph",
                0u,
                GitRequestKind::NONE,
                row_index,
                editor.git_graph_open,
                row_width,
                focused
            )) {
            editor.git_graph_open = !editor.git_graph_open;
        }
        row_index += 1u;
        if (editor.git_graph_open) {
            draw_git_commit_search(ui, editor, palette, row_width);
            float const graph_width = git_graph_width(editor, row_width);
            GitGraphVirtualRange range =
                git_graph_virtual_range(editor, scroll, row_index, sidebar_content_height);
            if (!git_commit_search_query(editor).empty()) {
                range = {.first = 0u, .end = editor.git_commits.size()};
            }
            draw_git_graph_rows(
                ui, editor, palette, row_index, row_width, graph_width, focused, range, input
            );
        }
    }

    auto draw_filesystem_panel(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        font_cache::Font branch_icon_font,
        Palette const& palette,
        size_t split,
        gui::Size width,
        gui::Size height,
        bool selection_visible,
        gui::InputState const& input
    ) -> void {
        bool focused = selection_visible && split == editor.focused_split;
        bool const git_tab = editor.sidebar_tab == EditorSidebarTab::GIT;
        float const panel_padding_y = git_tab ? GIT_PANEL_PADDING_Y : 14.0f;
        gui::Rect const split_rect = editor.split_nodes[split].rect;
        if (!focused && input.mouse_down[0u] && !editor.flag(EditorFlag::MOUSE_WAS_DOWN) &&
            point_in_rect(split_rect, input.mouse_pos)) {
            focus_editor_split(editor, split);
            focused = selection_visible && split == editor.focused_split;
        }
        float const row_min_width =
            std::max(0.0f, split_rect.max.x - split_rect.min.x - TREE_PANEL_PADDING_X * 2.0f);
        float const sidebar_content_height =
            std::max(0.0f, split_rect.max.y - split_rect.min.y - panel_padding_y * 2.0f);
        if (auto sidebar = ui.scroll_panel(
                editor_surface_id(split),
                {
                    .layout =
                        {
                            .width = width,
                            .height = height,
                            .padding = gui::insets(panel_padding_y, 10.0f),
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
            if (git_tab) {
                gui::Id const surface_id = editor_surface_id(split);
                gui::ScrollState const scroll = ui.scroll_state(surface_id);
                draw_git_panel_content(
                    ui,
                    editor,
                    palette,
                    icon_font,
                    branch_icon_font,
                    row_min_width,
                    sidebar_content_height,
                    scroll,
                    focused,
                    input
                );
                request_more_git_commits_for_scroll(editor, scroll);
                reveal_git_cursor(ui, editor, surface_id, scroll);
                return;
            }
            if (editor.tree_root_name.empty()) {
                return;
            }
            bool tree_open = editor.flag(EditorFlag::TREE_OPEN);
            size_t cursor_row = 0u;
            TreeDraftPlacement const draft = tree_draft_placement(editor);
            draw_tree_folder(
                ui,
                editor,
                palette,
                ui_font,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                TREE_CURSOR_ROOT,
                0u,
                true,
                focused,
                &tree_open,
                row_min_width
            );
            editor.set_flag(EditorFlag::TREE_OPEN, tree_open);
            size_t row_index = 1u;
            if (tree_open) {
                if (editor.tree_loading) {
                    draw_filesystem_loading_row(ui, editor, palette, row_min_width);
                    row_index += 1u;
                }
                if (draft.active && draft.after_index == TREE_CURSOR_ROOT) {
                    if (editor.tree_edit_mode == TreeEditMode::CREATE_FILE ||
                        editor.tree_edit_mode == TreeEditMode::CREATE_DIRECTORY) {
                        cursor_row = row_index;
                    }
                    draw_tree_draft(
                        ui,
                        editor,
                        palette,
                        ui_font,
                        icon_font,
                        draft.guide_count,
                        draft.after_index,
                        draft.directory,
                        focused,
                        row_min_width
                    );
                    row_index += 1u;
                }
                size_t closed_depth = static_cast<size_t>(-1);
                for (size_t index = 0u; index < editor.tree_files.size(); ++index) {
                    FileTreeEntry& entry = editor.tree_files[index];
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    if (editor.tree_cursor == index) {
                        cursor_row = row_index;
                    }
                    draw_tree_entry(
                        ui,
                        editor,
                        palette,
                        ui_font,
                        icon_font,
                        index,
                        entry,
                        focused,
                        row_min_width
                    );
                    row_index += 1u;
                    if (draft.active && draft.after_index == index) {
                        if (editor.tree_edit_mode == TreeEditMode::CREATE_FILE ||
                            editor.tree_edit_mode == TreeEditMode::CREATE_DIRECTORY) {
                            cursor_row = row_index;
                        }
                        draw_tree_draft(
                            ui,
                            editor,
                            palette,
                            ui_font,
                            icon_font,
                            draft.guide_count,
                            draft.after_index,
                            draft.directory,
                            focused,
                            row_min_width
                        );
                        row_index += 1u;
                    }
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
            if (editor.tree_cursor_reveal) {
                ui.scroll_to_index(editor_surface_id(split), cursor_row);
                editor.tree_cursor_reveal = false;
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

    auto rename_editor_path(StrRef& name, StrRef& path, Arena& arena, StrRef source, StrRef target)
        -> void {
        if (path.empty()) {
            return;
        }
        StrRef const updated = replace_path_prefix(arena, path, source, target);
        if (updated == path) {
            return;
        }
        path = updated;
        StrRef leaf = render_path_leaf(path);
        if (leaf.empty()) {
            leaf = path;
        }
        name = arena_copy_cstr(arena, leaf);
    }

    auto sync_tree_operation_result(EditorState& editor) -> void {
        if (editor.shared_tree_operation_result == nullptr ||
            editor.shared_tree_operation_result->generation == 0u ||
            editor.shared_tree_operation_result->generation ==
                editor.tree_operation_seen_generation) {
            return;
        }

        TreeOperationResult const result = *editor.shared_tree_operation_result;
        editor.tree_operation_seen_generation = result.generation;
        editor.tree_operation_pending = false;

        StrRef const source_path(result.source_path, cstr_len(result.source_path));
        StrRef const target_path(result.target_path, cstr_len(result.target_path));
        if (!result.success || editor.arena == nullptr) {
            if (!result.success) {
                fmt::eprintf("code_editor: tree operation failed\n");
            }
            return;
        }

        if (result.update_kind == TreeOperationUpdateKind::RENAME) {
            if (editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
                rename_editor_path(
                    editor.current_file_name,
                    editor.current_file_path,
                    *editor.arena,
                    source_path,
                    target_path
                );
            }
            for (EditorPane* pane : editor.panes) {
                if (pane == nullptr) {
                    continue;
                }
                rename_editor_path(
                    pane->current_file_name,
                    pane->current_file_path,
                    *editor.arena,
                    source_path,
                    target_path
                );
                for (OpenFileViewState& view : pane->open_file_views) {
                    rename_editor_path(
                        view.name, view.path, *editor.arena, source_path, target_path
                    );
                }
            }
            for (OpenFile& file : editor.open_files) {
                rename_editor_path(file.name, file.path, *editor.arena, source_path, target_path);
            }
            for (EditorJump& jump : editor.jumps) {
                rename_editor_path(jump.name, jump.path, *editor.arena, source_path, target_path);
            }

            size_t const index = tree_entry_index_by_path(editor, target_path);
            if (index < editor.tree_files.size() && editor.tree_files[index].is_directory) {
                editor.tree_files[index].open = true;
            }
            editor.tree_cursor = index;
            editor.tree_cursor_reveal = true;
            editor.global_search_refresh_requested = true;
            return;
        }

        if (result.update_kind == TreeOperationUpdateKind::REMOVE) {
            if (path_matches_or_contains(editor.current_file_path, source_path) &&
                editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
                editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, true);
                editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
            }
            for (EditorPane* pane : editor.panes) {
                if (pane != nullptr && pane->kind == EditorPaneKind::CODE &&
                    path_matches_or_contains(pane->current_file_path, source_path)) {
                    pane->file_deleted_on_disk = true;
                    pane->external_change_pending = false;
                }
            }
            for (OpenFile& file : editor.open_files) {
                if (path_matches_or_contains(file.path, source_path)) {
                    file.file_deleted_on_disk = true;
                    file.external_change_pending = false;
                }
            }

            size_t const parent_index =
                tree_entry_index_by_path(editor, render_path_parent(source_path));
            editor.tree_cursor = parent_index;
            editor.tree_cursor_reveal = true;
            editor.global_search_refresh_requested = true;
            return;
        }

        if (result.update_kind == TreeOperationUpdateKind::CREATE ||
            result.update_kind == TreeOperationUpdateKind::RESTORE) {
            if (path_matches_or_contains(editor.current_file_path, source_path) &&
                editor_focused_pane_kind(editor) == EditorPaneKind::CODE) {
                editor.set_flag(EditorFlag::FILE_DELETED_ON_DISK, false);
                editor.set_flag(EditorFlag::EXTERNAL_CHANGE_PENDING, false);
            }
            for (EditorPane* pane : editor.panes) {
                if (pane != nullptr && pane->kind == EditorPaneKind::CODE &&
                    path_matches_or_contains(pane->current_file_path, source_path)) {
                    pane->file_deleted_on_disk = false;
                    pane->external_change_pending = false;
                }
            }
            for (OpenFile& file : editor.open_files) {
                if (path_matches_or_contains(file.path, source_path)) {
                    file.file_deleted_on_disk = false;
                    file.external_change_pending = false;
                }
            }

            size_t const index = tree_entry_index_by_path(editor, source_path);
            if (index < editor.tree_files.size() && editor.tree_files[index].is_directory) {
                editor.tree_files[index].open = true;
            }
            editor.tree_cursor = index;
            editor.tree_cursor_reveal = true;
            editor.global_search_refresh_requested = true;
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
        if (editor.lsp_seen_folding_ranges_generation !=
            editor.lsp_bridge->folding_ranges_generation) {
            editor.lsp_seen_folding_ranges_generation =
                editor.lsp_bridge->folding_ranges_generation;
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

    [[nodiscard]] auto lsp_wrapped_line(
        font_cache::Font font, float font_size, StrRef line, size_t& offset, float wrap_width
    ) -> StrRef {
        size_t end = offset;
        size_t wrap = StrRef::NPOS;
        while (end < line.size()) {
            if (is_ascii_whitespace(line[end])) {
                wrap = end;
            }
            size_t const next = end + 1u;
            if (font_cache::text_advance(font, font_size, line.substr(offset, next - offset)) >
                wrap_width) {
                if (wrap != StrRef::NPOS && wrap > offset) {
                    StrRef const result = line.substr(offset, wrap - offset);
                    offset = wrap + 1u;
                    while (offset < line.size() && is_ascii_whitespace(line[offset])) {
                        offset += 1u;
                    }
                    return result;
                }
                if (end > offset) {
                    StrRef const result = line.substr(offset, end - offset);
                    offset = end;
                    return result;
                }
            }
            end = next;
        }
        StrRef const result = line.substr(offset);
        offset = line.size();
        return result;
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
        float const wrap_width = std::max(1.0f, clip.max.x - clip.min.x);
        size_t offset = 0u;
        size_t line_count = 0u;
        while (offset < text.size() && line_count < max_lines) {
            StrRef const line = next_text_line(text, offset);
            if (line.empty()) {
                y += line_height;
                line_count += 1u;
                continue;
            }
            size_t line_offset = 0u;
            while (line_offset < line.size() && line_count < max_lines) {
                StrRef const wrapped =
                    lsp_wrapped_line(font, editor.font_size, line, line_offset, wrap_width);
                draw::draw_text(context, {clip.min.x, y - 2.0f}, style, wrapped, nullptr);
                y += line_height;
                line_count += 1u;
            }
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
        float const y = content.min.y +
                        static_cast<float>(editor_visible_line_index(editor, editor.cursor_line)) *
                            line_height -
                        editor.scroll_y;
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
            content.min.y +
            static_cast<float>(editor_visible_line_index(editor, line)) * line_height -
            editor.scroll_y;
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
        wrap_width = std::max(1.0f, wrap_width);
        while (offset < text.size()) {
            StrRef const line = next_text_line(text, offset);
            if (line.empty()) {
                count += 1u;
                continue;
            }
            size_t line_offset = 0u;
            while (line_offset < line.size()) {
                BASE_UNUSED(lsp_wrapped_line(font, font_size, line, line_offset, wrap_width));
                count += 1u;
            }
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
        gui::Rect const content = editor_content_rect(rect);
        float const max_width =
            std::max(120.0f, std::min(720.0f, content.max.x - content.min.x - 8.0f));
        float const min_width = std::min(240.0f, max_width);
        float const width = std::clamp(metrics.width + 22.0f, min_width, max_width);
        float const wrap_width = std::max(1.0f, width - 2.0f * LSP_POPUP_PADDING_X);
        size_t const lines =
            lsp_wrapped_line_count(font, editor.font_size, diagnostic->message, wrap_width);
        float const line_height = lsp_text_line_height(font, editor.font_size);
        draw::Rect const panel = lsp_anchor_panel(
            editor,
            rect,
            char_width,
            editor.cursor_line,
            editor.cursor_column,
            width,
            static_cast<float>(lines) * line_height + 18.0f
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
            content.min.y +
            static_cast<float>(editor_visible_line_index(editor, editor.cursor_line)) *
                line_height -
            editor.scroll_y;
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

    auto draw_git_error_modal(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        float client_width,
        gui::InputState const& input
    ) -> void {
        if (!editor.git_error_visible || editor.git_error_text.empty()) {
            return;
        }

        bool close = key_pressed(input, gui::Key::ESCAPE);
        float const width = std::clamp(client_width - 48.0f, 320.0f, 620.0f);
        if (auto modal = ui.modal(
                gui::id("git_error_modal"),
                {
                    .layout =
                        {
                            .padding = gui::insets(58.0f, 24.0f, 24.0f, 24.0f),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::START,
                        },
                    .style = {.background = gui::rgba(0, 0, 0, 82)},
                    .debug_name = "git_error_modal",
                }
            )) {
            BASE_UNUSED(modal);
            if (auto dialog = ui.column(
                    gui::id("git_error_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::px(width),
                                .height = gui::children(),
                                .padding = gui::insets(12.0f),
                                .gap = 10.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.preprocessor,
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                        },
                    }
                )) {
                BASE_UNUSED(dialog);
                ui.label(
                    "Git error",
                    {
                        .layout = {.width = gui::fill(), .height = gui::px(24.0f)},
                        .style = {.foreground = palette.text, .font_size = editor.font_size},
                    }
                );
                ui.label(
                    editor.git_error_text,
                    {
                        .layout = {.width = gui::fill(), .height = gui::text(), .word_wrap = true},
                        .style = {
                            .foreground = palette.preprocessor,
                            .font_size = editor.font_size,
                        },
                    }
                );
                if (auto row = ui.row(
                        gui::id("git_error_buttons"),
                        {.layout = {.width = gui::fill(), .height = gui::px(28.0f)}}
                    )) {
                    BASE_UNUSED(row);
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                    close = ui.button(
                                  gui::id("git_error_close"),
                                  "Close",
                                  {
                                      .layout =
                                          {
                                              .width = gui::px(86.0f),
                                              .height = gui::fill(),
                                              .padding = gui::insets(0.0f, 10.0f),
                                          },
                                      .style =
                                          {
                                              .background = palette.panel_raised,
                                              .foreground = palette.text,
                                              .border = palette.border,
                                              .border_thickness = 1.0f,
                                              .radius = 4.0f,
                                              .font_size = editor.font_size,
                                          },
                                  }
                            )
                                .activated ||
                            close;
                }
            }
        }

        if (close) {
            editor.git_error_visible = false;
            editor.git_error_text = {};
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
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        font_cache::Font branch_icon_font,
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
                    ui,
                    editor,
                    ui_font,
                    icon_font,
                    branch_icon_font,
                    palette,
                    split,
                    width,
                    height,
                    selection_visible,
                    input
                );
                return;
            }
            bool const pressed = !editor.flag(EditorFlag::SIDEBAR_RESIZING) &&
                                 input.mouse_down[0u] && !editor.flag(EditorFlag::MOUSE_WAS_DOWN) &&
                                 point_in_rect(editor.split_nodes[split].rect, input.mouse_pos);
            if (pressed) {
                ui.clear_focus();
                editor.git_selection_focused = false;
                editor.git_control_focused = false;
                editor.git_text_editing = false;
                editor.git_commit_text_focused = false;
                editor.git_branch_search_focused = false;
                editor.git_commit_search_focused = false;
                editor.git_action_ref_focused = false;
                focus_editor_split(editor, split);
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
                    ui_font,
                    icon_font,
                    branch_icon_font,
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
                    ui_font,
                    icon_font,
                    branch_icon_font,
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
                ui_font,
                icon_font,
                branch_icon_font,
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
                ui_font,
                icon_font,
                branch_icon_font,
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
        font_cache::Font ui_font,
        font_cache::Font icon_font,
        font_cache::Font branch_icon_font,
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
                                 editor.flag(EditorFlag::JUMP_LIST_OPEN) ||
                                 editor.git_error_visible;
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
                    ui_font,
                    icon_font,
                    branch_icon_font,
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
                        editor.extra_cursors.empty() ? fmt::tprintf(
                                                           "Ln %zu, Col %zu",
                                                           editor.cursor_line + 1u,
                                                           editor.cursor_column + 1u
                                                       )
                                                     : fmt::tprintf(
                                                           "Ln %zu, Col %zu, %zu cursors",
                                                           editor.cursor_line + 1u,
                                                           editor.cursor_column + 1u,
                                                           editor.extra_cursors.size() + 1u
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
        draw_git_error_modal(ui, editor, palette, client_width, input);
        if (!editor.flag(EditorFlag::FILE_SEARCH_OPEN) &&
            !editor.flag(EditorFlag::BUFFER_SEARCH_OPEN) &&
            !editor.flag(EditorFlag::JUMP_LIST_OPEN) && !editor.flag(EditorFlag::SAVE_PATH_OPEN) &&
            editor.close_intent == EditorCloseIntent::NONE && !editor.git_error_visible) {
            draw_lsp_hover_popup(ui, editor_font, editor, char_width, palette, input);
        }
        draw_lsp_rename_popup(ui, editor, palette, char_width, client_width, client_height);
    }

} // namespace code_editor
