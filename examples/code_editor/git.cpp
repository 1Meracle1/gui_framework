#include "git.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/string_buffer.h>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace code_editor {

    namespace {

        [[nodiscard]] auto normalized_path(Arena& arena, StrRef path) -> StrRef {
            char* const text = arena_alloc<char>(arena, path.size() + 1u);
            for (size_t index = 0u; index < path.size(); ++index) {
                char const ch = path[index];
                text[index] = ch == '\\' ? '/' : ch;
            }
            text[path.size()] = '\0';
            return StrRef(text, path.size());
        }

        auto write_shell_arg(StringBuffer& command, StrRef text) -> void {
            command.write_byte('"');
            for (char ch : text) {
                if (ch == '"') {
                    command.write_string("\\\"");
                } else {
                    command.write_byte(ch);
                }
            }
            command.write_byte('"');
        }

        [[nodiscard]] auto run_capture(Arena& arena, StrRef command_text, bool allow_diff_exit)
            -> GitRunResult {
            StringBuffer output = {};
            output.init(0u, arena.resource());
#if defined(_WIN32)
            std::FILE* pipe = _popen(arena_copy_cstr(arena, command_text).data(), "rb");
#else
            std::FILE* pipe = popen(arena_copy_cstr(arena, command_text).data(), "r");
#endif
            if (pipe == nullptr) {
                return {.output = "Failed to start git.", .exit_code = -1, .ok = false};
            }

            char buffer[4096] = {};
            while (true) {
                size_t const read = std::fread(buffer, 1u, sizeof(buffer), pipe);
                if (read != 0u) {
                    output.write_bytes(buffer, read);
                }
                if (read < sizeof(buffer)) {
                    if (std::feof(pipe) != 0) {
                        break;
                    }
                    if (std::ferror(pipe) != 0) {
                        break;
                    }
                }
            }

#if defined(_WIN32)
            int const exit_code = _pclose(pipe);
#else
            int const status = pclose(pipe);
            int const exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
            bool const ok = exit_code == 0 || (allow_diff_exit && exit_code == 1);
            return {.output = output.str(), .exit_code = exit_code, .ok = ok};
        }

        auto write_git_command(StringBuffer& command, StrRef root) -> void {
            command.write_string("git -C ");
            write_shell_arg(command, root);
            command.write_byte(' ');
        }

        [[nodiscard]] auto run_git(Arena& arena, StrRef root, StrRef args, bool allow_diff_exit)
            -> GitRunResult {
            StringBuffer command = {};
            command.init(256u + root.size() + args.size(), arena.resource());
            write_git_command(command, root);
            command.write_string(args);
            command.write_string(" 2>&1");
            return run_capture(arena, command.str(), allow_diff_exit);
        }

        [[nodiscard]] auto git_has_upstream(Arena& arena, StrRef root) -> bool {
            return run_git(arena, root, "rev-parse --verify --quiet @{upstream}", false).ok;
        }

        [[nodiscard]] auto git_ref_exists(Arena& arena, StrRef root, StrRef ref) -> bool {
            StrRef const args = fmt::tprintf("rev-parse --verify --quiet %s", ref);
            return run_git(arena, root, args, false).ok;
        }

        [[nodiscard]] auto path_is_absolute(StrRef path) -> bool {
            return path.starts_with('/') || path.starts_with('\\') ||
                   (path.size() >= 2u && path[1u] == ':');
        }

        [[nodiscard]] auto joined_git_path(Arena& arena, StrRef root, StrRef path) -> StrRef {
            if (path_is_absolute(path)) {
                return path;
            }
            StringBuffer result = {};
            result.init(root.size() + path.size() + 2u, arena.resource());
            result.write_string(root);
            if (!root.ends_with('/') && !root.ends_with('\\')) {
                result.write_byte('/');
            }
            result.write_string(path);
            return result.str();
        }

        [[nodiscard]] auto filesystem_path_exists(Arena& arena, StrRef path) -> bool {
#if defined(_WIN32)
            struct _stat64 data = {};
            return _stat64(arena_copy_cstr(arena, path).data(), &data) == 0;
#else
            struct stat data = {};
            return stat(arena_copy_cstr(arena, path).data(), &data) == 0;
#endif
        }

        [[nodiscard]] auto first_output_line(StrRef text) -> StrRef;

        [[nodiscard]] auto git_path_exists(Arena& arena, StrRef root, StrRef name) -> bool {
            StrRef const args = fmt::tprintf("rev-parse --git-path %s", name);
            GitRunResult const result = run_git(arena, root, args, false);
            if (!result.ok) {
                return false;
            }
            StrRef const path = first_output_line(result.output);
            return !path.empty() &&
                   filesystem_path_exists(arena, joined_git_path(arena, root, path));
        }

        [[nodiscard]] auto first_output_line(StrRef text) -> StrRef {
            size_t const newline = text.find('\n');
            StrRef line = newline == StrRef::NPOS ? text : text.prefix(newline);
            if (line.ends_with('\r')) {
                line = line.drop_suffix(1u);
            }
            return line.trim();
        }

        auto set_message(StrRef output, StrRef fallback, StrRef& message) -> void {
            StrRef text = output.trim();
            if (text.empty()) {
                message = fallback;
                return;
            }
            size_t const newline = text.find('\n');
            if (newline != StrRef::NPOS) {
                text = text.prefix(newline).trim();
            }
            message = text;
        }

        [[nodiscard]] auto git_output_is_not_repository(StrRef output) -> bool {
            return git_text_contains_ignore_ascii_case(output, "not a git repository");
        }

        [[nodiscard]] auto command_with_path(Arena& arena, StrRef first, StrRef path) -> StrRef {
            StringBuffer command = {};
            command.init(first.size() + path.size() + 16u, arena.resource());
            command.write_string(first);
            command.write_string(" -- ");
            write_shell_arg(command, path);
            return command.str();
        }

        [[nodiscard]] auto absolute_git_path(Arena& arena, StrRef root, StrRef path) -> StrRef {
            StringBuffer result = {};
            result.init(root.size() + path.size() + 2u, arena.resource());
            result.write_string(root);
            if (!root.ends_with('/') && !root.ends_with('\\')) {
                result.write_byte('/');
            }
            for (char ch : path) {
                result.write_byte(ch == '\\' ? '/' : ch);
            }
            return result.str();
        }

        auto write_commit_message_args(StringBuffer& args, StrRef message) -> void {
            args.write_string("commit");
            size_t line_start = 0u;
            while (line_start <= message.size()) {
                size_t line_end = message.find('\n', line_start);
                if (line_end == StrRef::NPOS) {
                    line_end = message.size();
                }
                StrRef line = message.slice(line_start, line_end - line_start);
                if (line.ends_with('\r')) {
                    line.remove_suffix(1u);
                }
                args.write_string(" -m ");
                write_shell_arg(args, line);
                if (line_end == message.size()) {
                    break;
                }
                line_start = line_end + 1u;
            }
        }

        [[nodiscard]] auto command_with_ref(Arena& arena, StrRef first, StrRef ref) -> StrRef {
            StringBuffer command = {};
            command.init(first.size() + ref.size() + 8u, arena.resource());
            command.write_string(first);
            command.write_byte(' ');
            write_shell_arg(command, ref);
            return command.str();
        }

        [[nodiscard]] auto status_from_code(char ch) -> GitFileStatus {
            switch (ch) {
            case 'A':
                return GitFileStatus::ADDED;
            case 'D':
                return GitFileStatus::DELETED;
            case 'R':
                return GitFileStatus::RENAMED;
            case 'T':
                return GitFileStatus::TYPE_CHANGED;
            case 'U':
                return GitFileStatus::UNMERGED;
            default:
                return GitFileStatus::MODIFIED;
            }
        }

        [[nodiscard]] auto unmerged_status(char x, char y) -> bool {
            return x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
        }

        [[nodiscard]] auto push_status(
            Arena& arena,
            Vec<GitStatusItem>& out,
            StrRef path,
            StrRef old_path,
            GitFileStatus status,
            GitStatusScope scope
        ) -> bool {
            return out.push_back({
                .path = normalized_path(arena, path),
                .old_path = old_path.empty() ? StrRef() : normalized_path(arena, old_path),
                .status = status,
                .scope = scope,
            });
        }

        [[nodiscard]] auto next_nul_field(StrRef text, size_t& offset, StrRef& out) -> bool {
            if (offset >= text.size()) {
                return false;
            }
            size_t const end = text.find('\0', offset);
            if (end == StrRef::NPOS) {
                out = text.substr(offset);
                offset = text.size();
                return true;
            }
            out = text.substr(offset, end - offset);
            offset = end + 1u;
            return true;
        }

        struct GitToken {
            size_t offset = 0u;
            size_t size = 0u;
        };

        [[nodiscard]] auto token_equal(StrRef left, GitToken a, StrRef right, GitToken b) -> bool {
            return a.size == b.size &&
                   left.substr(a.offset, a.size) == right.substr(b.offset, b.size);
        }

        [[nodiscard]] auto token_word(char ch) -> bool {
            return is_ascii_alphanumeric(ch) || ch == '_';
        }

        [[nodiscard]] auto collect_tokens(StrRef text, Vec<GitToken>& out) -> bool {
            size_t offset = 0u;
            while (offset < text.size()) {
                size_t end = offset + 1u;
                char const ch = text[offset];
                if (token_word(ch)) {
                    while (end < text.size() && token_word(text[end])) {
                        ++end;
                    }
                } else if (is_ascii_whitespace(ch)) {
                    while (end < text.size() && is_ascii_whitespace(text[end])) {
                        ++end;
                    }
                }
                if (!out.push_back({offset, end - offset})) {
                    return false;
                }
                offset = end;
            }
            return true;
        }

        auto push_span(Vec<GitInlineSpan>& spans, size_t offset, size_t size) -> void {
            if (size == 0u) {
                return;
            }
            if (!spans.empty()) {
                GitInlineSpan& last = spans[spans.size() - 1u];
                if (last.offset + last.size >= offset) {
                    size_t const end = std::max(last.offset + last.size, offset + size);
                    last.size = end - last.offset;
                    return;
                }
            }
            bool const ok = spans.push_back({offset, size});
            DEBUG_ASSERT(ok);
            (void)ok;
        }

        [[nodiscard]] auto read_number(StrRef text, size_t& offset, size_t& out) -> bool {
            if (offset >= text.size() || !is_ascii_digit(text[offset])) {
                return false;
            }
            size_t value = 0u;
            while (offset < text.size() && is_ascii_digit(text[offset])) {
                value = value * 10u + static_cast<size_t>(text[offset] - '0');
                ++offset;
            }
            out = value;
            return true;
        }

        [[nodiscard]] auto parse_count_line(StrRef text, size_t& out) -> bool {
            StrRef const line = first_output_line(text);
            size_t offset = 0u;
            if (!read_number(line, offset, out)) {
                return false;
            }
            return line.substr(offset).trim().empty();
        }

        [[nodiscard]] auto parse_hunk_header(StrRef line, size_t& old_line, size_t& new_line)
            -> bool {
            size_t offset = line.find('-');
            if (offset == StrRef::NPOS) {
                return false;
            }
            ++offset;
            if (!read_number(line, offset, old_line)) {
                return false;
            }
            if (offset < line.size() && line[offset] == ',') {
                ++offset;
                size_t ignored = 0u;
                BASE_UNUSED(read_number(line, offset, ignored));
            }
            offset = line.find('+', offset);
            if (offset == StrRef::NPOS) {
                return false;
            }
            ++offset;
            if (!read_number(line, offset, new_line)) {
                return false;
            }
            old_line = old_line == 0u ? 0u : old_line - 1u;
            new_line = new_line == 0u ? 0u : new_line - 1u;
            return true;
        }

        [[nodiscard]] auto push_diff_row(
            Arena& arena,
            GitDiffDocument& doc,
            GitDiffRowKind kind,
            StrRef left,
            StrRef right,
            size_t old_line,
            size_t new_line
        ) -> GitDiffRow* {
            GitDiffRow* const row = doc.rows.push_back_and_get_ptr({
                .kind = kind,
                .left_text = arena_copy_str(arena, left),
                .right_text = arena_copy_str(arena, right),
                .old_line = old_line,
                .new_line = new_line,
            });
            DEBUG_ASSERT(row != nullptr);
            if (row != nullptr) {
                row->left_spans.init(0u, arena.resource());
                row->right_spans.init(0u, arena.resource());
            }
            return row;
        }

        struct PendingRemove {
            StrRef text = {};
            size_t old_line = 0u;
        };

        auto flush_pending_removes(Arena& arena, GitDiffDocument& doc, Vec<PendingRemove>& pending)
            -> void {
            for (PendingRemove const& remove : pending) {
                BASE_UNUSED(push_diff_row(
                    arena, doc, GitDiffRowKind::REMOVED, remove.text, {}, remove.old_line, 0u
                ));
            }
            pending.clear();
        }

        [[nodiscard]] auto name_status_code(StrRef token) -> GitFileStatus {
            return token.empty() ? GitFileStatus::MODIFIED : status_from_code(token[0u]);
        }

        auto write_line_number(StringBuffer& out, size_t line) -> void {
            if (line == 0u) {
                out.write_string("     ");
            } else {
                BASE_UNUSED(fmt::format(&out, "%5zu", line));
            }
        }

        auto write_side_line(
            StringBuffer& out,
            char marker,
            size_t left_line,
            StrRef left,
            size_t right_line,
            StrRef right
        ) -> void {
            write_line_number(out, left_line);
            out.write_byte(' ');
            out.write_byte(left.empty() && left_line == 0u ? ' ' : marker);
            out.write_byte(' ');
            out.write_string(left);
            out.write_string(" | ");
            write_line_number(out, right_line);
            out.write_byte(' ');
            out.write_byte(right.empty() && right_line == 0u ? ' ' : marker);
            out.write_byte(' ');
            out.write_string(right);
            out.write_byte('\n');
        }

        auto write_unified_line(
            StringBuffer& out, char marker, size_t old_line, size_t new_line, StrRef text
        ) -> void {
            out.write_byte(marker);
            write_line_number(out, old_line);
            out.write_byte(' ');
            write_line_number(out, new_line);
            out.write_string("  ");
            out.write_string(text);
            out.write_byte('\n');
        }

        [[nodiscard]] auto unified_file_header_visible(StrRef line) -> bool {
            return !line.starts_with("diff --git ") && !line.starts_with("index ") &&
                   !line.starts_with("--- ") && !line.starts_with("+++ ");
        }

        [[nodiscard]] auto parse_oid_lines(StrRef text, Vec<StrRef>& out) -> bool {
            out.clear();
            for (StrRef line : text.lines()) {
                StrRef const oid = line.trim();
                if (!oid.empty() && !out.push_back(oid)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto oid_list_contains(Vec<StrRef> const& oids, StrRef oid) -> bool {
            for (StrRef const item : oids) {
                if (item == oid) {
                    return true;
                }
            }
            return false;
        }

        auto mark_incoming_commits(Vec<GitCommit>& commits, Vec<StrRef> const& incoming_oids)
            -> void {
            for (GitCommit& commit : commits) {
                commit.incoming = oid_list_contains(incoming_oids, commit.oid);
            }
        }

        [[nodiscard]] auto git_hex_char(char ch) -> bool {
            return is_ascii_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        }

        [[nodiscard]] auto git_parent_list_text(StrRef text) -> bool {
            if (text.empty()) {
                return true;
            }
            bool in_oid = false;
            for (char ch : text) {
                if (ch == ' ') {
                    if (!in_oid) {
                        return false;
                    }
                    in_oid = false;
                    continue;
                }
                if (!git_hex_char(ch)) {
                    return false;
                }
                in_oid = true;
            }
            return in_oid;
        }

        auto parse_git_parent_list(Arena& arena, StrRef text, GitCommit& commit) -> void {
            commit.parent_count = 0u;
            for (StrRef parent : text.split_ascii_whitespace()) {
                if (commit.parent_count == GIT_COMMIT_MAX_PARENTS) {
                    break;
                }
                commit.parents[commit.parent_count] = arena_copy_cstr(arena, parent);
                commit.parent_count += 1u;
            }
        }

        [[nodiscard]] auto split_git_log_fields(StrRef text, Slice<StrRef> fields) -> size_t {
            size_t count = 0u;
            while (count + 1u < fields.size()) {
                size_t const separator = text.find('\x1f');
                if (separator == StrRef::NPOS) {
                    break;
                }
                fields[count] = text.prefix(separator);
                count += 1u;
                text.remove_prefix(separator + 1u);
            }
            if (count < fields.size()) {
                fields[count] = text;
                count += 1u;
            }
            return count;
        }

        [[nodiscard]] auto parse_git_numstat_count(StrRef text, size_t& value) -> bool {
            text = text.trim();
            if (text.empty() || text == "-") {
                return false;
            }
            size_t parsed = 0u;
            auto const result = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
                return false;
            }
            value = parsed;
            return true;
        }

        auto parse_git_numstat(StrRef text, GitCommit& commit) -> void {
            for (StrRef line : text.lines()) {
                line = line.trim();
                if (line.empty()) {
                    continue;
                }
                StrRef::SplitOnce const first = line.split_once('\t');
                if (!first.found) {
                    continue;
                }
                StrRef::SplitOnce const second = first.after.split_once('\t');
                if (!second.found) {
                    continue;
                }
                size_t count = 0u;
                if (parse_git_numstat_count(first.before, count)) {
                    commit.insertion_count += count;
                }
                if (parse_git_numstat_count(second.before, count)) {
                    commit.deletion_count += count;
                }
                commit.changed_file_count += 1u;
            }
        }

        [[nodiscard]] auto find_git_graph_lane(StrRef const* lanes, size_t count, StrRef oid)
            -> size_t {
            for (size_t index = 0u; index < count; ++index) {
                if (lanes[index] == oid) {
                    return index;
                }
            }
            return GIT_GRAPH_MAX_LANES;
        }

        auto copy_git_graph_lanes(StrRef const* from, size_t count, StrRef* to) -> void {
            for (size_t index = 0u; index < count; ++index) {
                to[index] = from[index];
            }
        }

        auto insert_git_graph_lane(StrRef* lanes, size_t& count, size_t index, StrRef oid) -> void {
            if (count == GIT_GRAPH_MAX_LANES) {
                return;
            }
            index = std::min(index, count);
            for (size_t move = count; move > index; --move) {
                lanes[move] = lanes[move - 1u];
            }
            lanes[index] = oid;
            count += 1u;
        }

        auto remove_git_graph_lane(StrRef* lanes, size_t& count, size_t index) -> void {
            if (index >= count) {
                return;
            }
            for (size_t move = index + 1u; move < count; ++move) {
                lanes[move - 1u] = lanes[move];
            }
            count -= 1u;
        }

        auto remove_duplicate_git_graph_lanes(StrRef* lanes, size_t& count) -> void {
            for (size_t index = 0u; index < count; ++index) {
                size_t other = index + 1u;
                while (other < count) {
                    if (lanes[other] == lanes[index]) {
                        remove_git_graph_lane(lanes, count, other);
                    } else {
                        other += 1u;
                    }
                }
            }
        }

        auto
        push_git_graph_segment(GitCommit& commit, size_t from, size_t to, GitGraphSegmentKind kind)
            -> void {
            if (from >= GIT_GRAPH_MAX_LANES || to >= GIT_GRAPH_MAX_LANES ||
                commit.graph_segment_count == GIT_GRAPH_MAX_SEGMENTS) {
                return;
            }
            commit.graph_segments[commit.graph_segment_count] = {
                .from_lane = static_cast<uint8_t>(from),
                .to_lane = static_cast<uint8_t>(to),
                .kind = kind,
            };
            commit.graph_segment_count += 1u;
        }

        [[nodiscard]] auto
        git_load_incoming_oids(Arena& arena, StrRef root, Vec<StrRef>& out, StrRef& message)
            -> bool {
            out.clear();
            if (!git_has_upstream(arena, root)) {
                message = {};
                return true;
            }
            GitRunResult const result = run_git(arena, root, "rev-list HEAD..@{upstream}", false);
            if (!result.ok) {
                set_message(result.output, "git rev-list failed.", message);
                return false;
            }
            bool const ok = parse_oid_lines(result.output, out);
            message = ok ? StrRef() : StrRef("Failed to parse incoming commits.");
            return ok;
        }

    } // namespace

    auto git_file_status_label(GitFileStatus status) -> StrRef {
        switch (status) {
        case GitFileStatus::ADDED:
            return "A";
        case GitFileStatus::DELETED:
            return "D";
        case GitFileStatus::RENAMED:
            return "R";
        case GitFileStatus::TYPE_CHANGED:
            return "T";
        case GitFileStatus::UNMERGED:
            return "U";
        case GitFileStatus::UNTRACKED:
            return "?";
        case GitFileStatus::MODIFIED:
        default:
            return "M";
        }
    }

    auto git_status_scope_label(GitStatusScope scope) -> StrRef {
        switch (scope) {
        case GitStatusScope::STAGED:
            return "staged";
        case GitStatusScope::UNTRACKED:
            return "untracked";
        case GitStatusScope::UNSTAGED:
        default:
            return "unstaged";
        }
    }

    auto parse_git_status(Arena& arena, StrRef text, Vec<GitStatusItem>& out) -> bool {
        out.clear();
        size_t offset = 0u;
        while (offset < text.size()) {
            StrRef entry = {};
            if (!next_nul_field(text, offset, entry) || entry.empty()) {
                continue;
            }
            if (entry.size() < 4u || entry[2u] != ' ') {
                continue;
            }

            char const x = entry[0u];
            char const y = entry[1u];
            StrRef const path = entry.drop_prefix(3u);
            StrRef old_path = {};
            if (x == 'R' || x == 'C' || y == 'R' || y == 'C') {
                BASE_UNUSED(next_nul_field(text, offset, old_path));
            }

            if (x == '?' && y == '?') {
                if (!push_status(
                        arena, out, path, {}, GitFileStatus::UNTRACKED, GitStatusScope::UNTRACKED
                    )) {
                    return false;
                }
                continue;
            }
            if (unmerged_status(x, y)) {
                if (!push_status(
                        arena,
                        out,
                        path,
                        old_path,
                        GitFileStatus::UNMERGED,
                        GitStatusScope::UNSTAGED
                    )) {
                    return false;
                }
                continue;
            }
            if (x != ' ' && x != '!' && x != '?') {
                if (!push_status(
                        arena, out, path, old_path, status_from_code(x), GitStatusScope::STAGED
                    )) {
                    return false;
                }
            }
            if (y != ' ' && y != '!' && y != '?') {
                if (!push_status(
                        arena, out, path, old_path, status_from_code(y), GitStatusScope::UNSTAGED
                    )) {
                    return false;
                }
            }
        }
        return true;
    }

    auto parse_git_branch_list(Arena& arena, StrRef text, Vec<GitBranch>& out) -> bool {
        out.clear();
        for (StrRef line : text.lines()) {
            StrRef const name = line.trim();
            if (!name.empty() && !out.push_back({arena_copy_cstr(arena, name)})) {
                return false;
            }
        }
        return true;
    }

    auto parse_git_log(Arena& arena, StrRef text, Vec<GitCommit>& out) -> bool {
        out.clear();
        size_t offset = 0u;
        while (offset < text.size()) {
            size_t end = text.find('\x1e', offset);
            if (end == StrRef::NPOS) {
                end = text.size();
            }
            StrRef record = text.substr(offset, end - offset);
            offset = end == text.size() ? text.size() : end + 1u;
            while (!record.empty() && (record.front() == '\n' || record.front() == '\r')) {
                record.remove_prefix(1u);
            }
            while (!record.empty() && (record.back() == '\n' || record.back() == '\r')) {
                record.remove_suffix(1u);
            }
            if (record.empty()) {
                continue;
            }
            StrRef fields[10] = {};
            size_t const field_count = split_git_log_fields(record, fields);
            if (field_count < 3u) {
                continue;
            }
            StrRef parents = {};
            StrRef refs = {};
            StrRef author = {};
            StrRef relative_date = {};
            StrRef author_date = {};
            StrRef summary = fields[2u];
            StrRef body = {};
            StrRef stats = {};
            if (field_count >= 9u) {
                parents = fields[2u];
                refs = fields[3u];
                author = fields[4u];
                relative_date = fields[5u];
                author_date = fields[6u];
                summary = fields[7u];
                body = fields[8u];
                if (field_count >= 10u) {
                    stats = fields[9u];
                }
            } else if (field_count >= 5u && git_parent_list_text(fields[2u])) {
                parents = fields[2u];
                refs = fields[3u];
                summary = fields[4u];
            } else if (field_count >= 4u) {
                refs = fields[2u];
                summary = fields[3u];
            }
            GitCommit commit = {
                .oid = arena_copy_cstr(arena, fields[0u]),
                .short_oid = arena_copy_cstr(arena, fields[1u]),
                .refs = refs.empty() ? StrRef() : arena_copy_cstr(arena, refs),
                .summary = arena_copy_cstr(arena, summary),
                .author = author.empty() ? StrRef() : arena_copy_cstr(arena, author),
                .relative_date =
                    relative_date.empty() ? StrRef() : arena_copy_cstr(arena, relative_date),
                .author_date = author_date.empty() ? StrRef() : arena_copy_cstr(arena, author_date),
                .body = body.empty() ? StrRef() : arena_copy_cstr(arena, body),
            };
            parse_git_parent_list(arena, parents, commit);
            parse_git_numstat(stats, commit);
            if (!out.push_back(commit)) {
                return false;
            }
        }
        return true;
    }

    auto layout_git_commit_graph(Vec<GitCommit>& commits) -> void {
        StrRef lanes[GIT_GRAPH_MAX_LANES] = {};
        size_t lane_count = 0u;

        for (GitCommit& commit : commits) {
            commit.graph_segment_count = 0u;
            commit.graph_lane = 0u;
            commit.graph_lane_count = 1u;

            size_t current_lane = find_git_graph_lane(lanes, lane_count, commit.oid);
            bool const current_active = current_lane != GIT_GRAPH_MAX_LANES;
            if (!current_active) {
                current_lane =
                    lane_count < GIT_GRAPH_MAX_LANES ? lane_count : GIT_GRAPH_MAX_LANES - 1u;
                if (lane_count < GIT_GRAPH_MAX_LANES) {
                    lanes[lane_count] = commit.oid;
                    lane_count += 1u;
                } else {
                    lanes[current_lane] = commit.oid;
                }
            }

            StrRef top_lanes[GIT_GRAPH_MAX_LANES] = {};
            copy_git_graph_lanes(lanes, lane_count, top_lanes);
            size_t const top_count = lane_count;

            StrRef bottom_lanes[GIT_GRAPH_MAX_LANES] = {};
            copy_git_graph_lanes(lanes, lane_count, bottom_lanes);
            size_t bottom_count = lane_count;
            if (commit.parent_count == 0u) {
                remove_git_graph_lane(bottom_lanes, bottom_count, current_lane);
            } else {
                bottom_lanes[current_lane] = commit.parents[0u];
                size_t insert_at = current_lane + 1u;
                for (size_t index = 1u; index < commit.parent_count; ++index) {
                    StrRef const parent = commit.parents[index];
                    if (find_git_graph_lane(bottom_lanes, bottom_count, parent) ==
                        GIT_GRAPH_MAX_LANES) {
                        insert_git_graph_lane(bottom_lanes, bottom_count, insert_at, parent);
                        insert_at += 1u;
                    }
                }
            }
            remove_duplicate_git_graph_lanes(bottom_lanes, bottom_count);

            if (current_active) {
                push_git_graph_segment(
                    commit, current_lane, current_lane, GitGraphSegmentKind::TOP_TO_COMMIT
                );
            }
            for (size_t index = 0u; index < top_count; ++index) {
                if (top_lanes[index] == commit.oid) {
                    continue;
                }
                size_t const to = find_git_graph_lane(bottom_lanes, bottom_count, top_lanes[index]);
                if (to != GIT_GRAPH_MAX_LANES) {
                    push_git_graph_segment(commit, index, to, GitGraphSegmentKind::TOP_TO_BOTTOM);
                }
            }
            for (size_t index = 0u; index < commit.parent_count; ++index) {
                size_t const to =
                    find_git_graph_lane(bottom_lanes, bottom_count, commit.parents[index]);
                if (to != GIT_GRAPH_MAX_LANES) {
                    push_git_graph_segment(
                        commit, current_lane, to, GitGraphSegmentKind::COMMIT_TO_BOTTOM
                    );
                }
            }

            commit.graph_lane =
                static_cast<uint8_t>(std::min(current_lane, GIT_GRAPH_MAX_LANES - 1u));
            commit.graph_lane_count = static_cast<uint8_t>(std::max({
                static_cast<size_t>(1u),
                top_count,
                bottom_count,
                current_lane + 1u,
            }));
            copy_git_graph_lanes(bottom_lanes, bottom_count, lanes);
            lane_count = bottom_count;
        }
    }

    auto
    parse_git_name_status(Arena& arena, StrRef commit_oid, StrRef text, Vec<GitCommitFile>& out)
        -> bool {
        size_t offset = 0u;
        while (offset < text.size()) {
            StrRef token = {};
            if (!next_nul_field(text, offset, token) || token.empty()) {
                continue;
            }
            GitFileStatus const status = name_status_code(token);
            StrRef old_path = {};
            StrRef path = {};
            if (token[0u] == 'R' || token[0u] == 'C') {
                if (!next_nul_field(text, offset, old_path) ||
                    !next_nul_field(text, offset, path)) {
                    return false;
                }
            } else if (!next_nul_field(text, offset, path)) {
                return false;
            }
            if (!out.push_back({
                    .commit_oid = arena_copy_cstr(arena, commit_oid),
                    .path = normalized_path(arena, path),
                    .old_path = old_path.empty() ? StrRef() : normalized_path(arena, old_path),
                    .status = status,
                })) {
                return false;
            }
        }
        return true;
    }

    auto compute_git_inline_diff(
        Arena& arena,
        StrRef left,
        StrRef right,
        Vec<GitInlineSpan>& left_spans,
        Vec<GitInlineSpan>& right_spans
    ) -> void {
        left_spans.clear();
        right_spans.clear();
        if (left == right) {
            return;
        }
        if (left.size() > GIT_INLINE_DIFF_MAX_LINE || right.size() > GIT_INLINE_DIFF_MAX_LINE) {
            push_span(left_spans, 0u, left.size());
            push_span(right_spans, 0u, right.size());
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        Vec<GitToken> left_tokens = {};
        Vec<GitToken> right_tokens = {};
        if (!left_tokens.init(0u, temp.arena()->resource()) ||
            !right_tokens.init(0u, temp.arena()->resource()) ||
            !collect_tokens(left, left_tokens) || !collect_tokens(right, right_tokens)) {
            push_span(left_spans, 0u, left.size());
            push_span(right_spans, 0u, right.size());
            return;
        }
        if (left_tokens.empty() || right_tokens.empty()) {
            push_span(left_spans, 0u, left.size());
            push_span(right_spans, 0u, right.size());
            return;
        }

        size_t const cell_count = (left_tokens.size() + 1u) * (right_tokens.size() + 1u);
        if (cell_count > 64u * 1024u) {
            push_span(left_spans, 0u, left.size());
            push_span(right_spans, 0u, right.size());
            return;
        }

        size_t const width = right_tokens.size() + 1u;
        uint16_t* const lcs = arena_alloc<uint16_t>(*temp.arena(), cell_count);
        std::memset(lcs, 0, sizeof(uint16_t) * cell_count);
        for (size_t i = left_tokens.size(); i > 0u; --i) {
            size_t const left_index = i - 1u;
            for (size_t j = right_tokens.size(); j > 0u; --j) {
                size_t const right_index = j - 1u;
                uint16_t const down = lcs[(left_index + 1u) * width + right_index];
                uint16_t const across = lcs[left_index * width + right_index + 1u];
                lcs[left_index * width + right_index] =
                    token_equal(left, left_tokens[left_index], right, right_tokens[right_index])
                        ? static_cast<uint16_t>(
                              lcs[(left_index + 1u) * width + right_index + 1u] + 1u
                          )
                        : std::max(down, across);
            }
        }

        size_t left_index = 0u;
        size_t right_index = 0u;
        while (left_index < left_tokens.size() || right_index < right_tokens.size()) {
            if (left_index < left_tokens.size() && right_index < right_tokens.size() &&
                token_equal(left, left_tokens[left_index], right, right_tokens[right_index])) {
                ++left_index;
                ++right_index;
                continue;
            }

            uint16_t const skip_left =
                left_index < left_tokens.size() ? lcs[(left_index + 1u) * width + right_index] : 0u;
            uint16_t const skip_right =
                right_index < right_tokens.size() ? lcs[left_index * width + right_index + 1u] : 0u;
            if (right_index == right_tokens.size() ||
                (left_index < left_tokens.size() && skip_left >= skip_right)) {
                GitToken const token = left_tokens[left_index++];
                push_span(left_spans, token.offset, token.size);
            } else {
                GitToken const token = right_tokens[right_index++];
                push_span(right_spans, token.offset, token.size);
            }
        }
        BASE_UNUSED(arena);
    }

    auto parse_git_patch(Arena& arena, StrRef title, StrRef text, GitDiffDocument& out) -> bool {
        out = {};
        out.title = arena_copy_cstr(arena, title);
        if (!out.rows.init(0u, arena.resource())) {
            return false;
        }

        Vec<PendingRemove> pending = {};
        if (!pending.init(0u, arena.resource())) {
            return false;
        }

        bool in_hunk = false;
        size_t old_line = 0u;
        size_t new_line = 0u;
        for (StrRef line : text.lines()) {
            if (line.starts_with("@@ ")) {
                flush_pending_removes(arena, out, pending);
                in_hunk = parse_hunk_header(line, old_line, new_line);
                BASE_UNUSED(
                    push_diff_row(arena, out, GitDiffRowKind::HUNK_HEADER, line, {}, 0u, 0u)
                );
                continue;
            }

            if (line.starts_with("diff --git ") || line.starts_with("index ") ||
                line.starts_with("new file mode ") || line.starts_with("deleted file mode ") ||
                line.starts_with("similarity index ") || line.starts_with("rename from ") ||
                line.starts_with("rename to ") || line.starts_with("--- ") ||
                line.starts_with("+++ ")) {
                flush_pending_removes(arena, out, pending);
                in_hunk = false;
                BASE_UNUSED(
                    push_diff_row(arena, out, GitDiffRowKind::FILE_HEADER, line, {}, 0u, 0u)
                );
                continue;
            }

            if (line.starts_with("Binary files ") || line.starts_with("GIT binary patch")) {
                flush_pending_removes(arena, out, pending);
                in_hunk = false;
                out.binary = true;
                BASE_UNUSED(
                    push_diff_row(arena, out, GitDiffRowKind::FILE_HEADER, line, {}, 0u, 0u)
                );
                continue;
            }

            if (!in_hunk) {
                if (!line.empty()) {
                    BASE_UNUSED(
                        push_diff_row(arena, out, GitDiffRowKind::FILE_HEADER, line, {}, 0u, 0u)
                    );
                }
                continue;
            }

            if (line.starts_with('\\')) {
                flush_pending_removes(arena, out, pending);
                BASE_UNUSED(push_diff_row(arena, out, GitDiffRowKind::CONTEXT, line, line, 0u, 0u));
                continue;
            }
            if (line.starts_with('-')) {
                if (!pending.push_back(
                        {arena_copy_str(arena, line.drop_prefix(1u)), old_line + 1u}
                    )) {
                    return false;
                }
                old_line += 1u;
                continue;
            }
            if (line.starts_with('+')) {
                StrRef const added = line.drop_prefix(1u);
                if (!pending.empty()) {
                    PendingRemove const removed = pending[0u];
                    pending.ordered_remove(0u);
                    GitDiffRow* const row = push_diff_row(
                        arena,
                        out,
                        GitDiffRowKind::MODIFIED,
                        removed.text,
                        added,
                        removed.old_line,
                        new_line + 1u
                    );
                    if (row != nullptr) {
                        compute_git_inline_diff(
                            arena,
                            row->left_text,
                            row->right_text,
                            row->left_spans,
                            row->right_spans
                        );
                    }
                } else {
                    BASE_UNUSED(push_diff_row(
                        arena, out, GitDiffRowKind::ADDED, {}, added, 0u, new_line + 1u
                    ));
                }
                new_line += 1u;
                continue;
            }
            flush_pending_removes(arena, out, pending);
            StrRef const content = line.starts_with(' ') ? line.drop_prefix(1u) : line;
            BASE_UNUSED(push_diff_row(
                arena, out, GitDiffRowKind::CONTEXT, content, content, old_line + 1u, new_line + 1u
            ));
            old_line += 1u;
            new_line += 1u;
        }
        flush_pending_removes(arena, out, pending);
        return true;
    }

    auto git_render_diff_document(Arena& arena, GitDiffDocument const& doc, bool side_by_side)
        -> StrRef {
        StringBuffer out = {};
        out.init(0u, arena.resource());
        if (side_by_side && !doc.title.empty()) {
            out.write_string(doc.title);
            out.write_string("\n\n");
        }
        for (GitDiffRow const& row : doc.rows) {
            if (row.kind == GitDiffRowKind::FILE_HEADER ||
                row.kind == GitDiffRowKind::HUNK_HEADER) {
                if (side_by_side || row.kind == GitDiffRowKind::HUNK_HEADER ||
                    unified_file_header_visible(row.left_text)) {
                    out.write_string(row.left_text);
                    out.write_byte('\n');
                }
                continue;
            }
            if (side_by_side) {
                switch (row.kind) {
                case GitDiffRowKind::ADDED:
                    write_side_line(out, '+', 0u, {}, row.new_line, row.right_text);
                    break;
                case GitDiffRowKind::REMOVED:
                    write_side_line(out, '-', row.old_line, row.left_text, 0u, {});
                    break;
                case GitDiffRowKind::MODIFIED:
                    write_side_line(
                        out, '~', row.old_line, row.left_text, row.new_line, row.right_text
                    );
                    break;
                case GitDiffRowKind::CONTEXT:
                default:
                    write_side_line(
                        out, ' ', row.old_line, row.left_text, row.new_line, row.right_text
                    );
                    break;
                }
            } else {
                switch (row.kind) {
                case GitDiffRowKind::ADDED:
                    write_unified_line(out, '+', 0u, row.new_line, row.right_text);
                    break;
                case GitDiffRowKind::REMOVED:
                    write_unified_line(out, '-', row.old_line, 0u, row.left_text);
                    break;
                case GitDiffRowKind::MODIFIED:
                    write_unified_line(out, '-', row.old_line, 0u, row.left_text);
                    write_unified_line(out, '+', 0u, row.new_line, row.right_text);
                    break;
                case GitDiffRowKind::CONTEXT:
                default:
                    write_unified_line(out, ' ', row.old_line, row.new_line, row.left_text);
                    break;
                }
            }
        }
        return out.str();
    }

    auto git_discover_root(Arena& arena, StrRef path, StrRef& root, StrRef& message) -> bool {
        StringBuffer command = {};
        command.init(path.size() + 80u, arena.resource());
        command.write_string("git -C ");
        write_shell_arg(command, path);
        command.write_string(" rev-parse --show-toplevel 2>&1");
        GitRunResult const result = run_capture(arena, command.str(), false);
        if (!result.ok) {
            if (git_output_is_not_repository(result.output)) {
                message = "Not a Git repository.";
            } else {
                set_message(result.output, "Not a Git repository.", message);
            }
            return false;
        }
        root = normalized_path(arena, first_output_line(result.output));
        message = root.empty() ? StrRef("Not a Git repository.") : StrRef();
        return !root.empty();
    }

    auto git_load_status(Arena& arena, StrRef root, Vec<GitStatusItem>& out, StrRef& message)
        -> bool {
        GitRunResult const result =
            run_git(arena, root, "status --porcelain=v1 -z --untracked-files=all", false);
        if (!result.ok) {
            set_message(result.output, "git status failed.", message);
            return false;
        }
        bool const ok = parse_git_status(arena, result.output, out);
        message = ok ? StrRef() : StrRef("Failed to parse git status.");
        return ok;
    }

    auto git_load_status_path(
        Arena& arena, StrRef root, StrRef path, Vec<GitStatusItem>& out, StrRef& message
    ) -> bool {
        GitRunResult const result = run_git(
            arena,
            root,
            command_with_path(arena, "status --porcelain=v1 -z --untracked-files=all", path),
            false
        );
        if (!result.ok) {
            set_message(result.output, "git status failed.", message);
            return false;
        }
        bool const ok = parse_git_status(arena, result.output, out);
        message = ok ? StrRef() : StrRef("Failed to parse git status.");
        return ok;
    }

    auto git_load_branches(
        Arena& arena, StrRef root, Vec<GitBranch>& out, StrRef& current, StrRef& message
    ) -> bool {
        GitRunResult const current_result = run_git(arena, root, "branch --show-current", false);
        if (!current_result.ok) {
            set_message(current_result.output, "git branch failed.", message);
            return false;
        }

        GitRunResult const list_result =
            run_git(arena, root, "branch --format=\"%(refname:short)\"", false);
        if (!list_result.ok) {
            set_message(list_result.output, "git branch failed.", message);
            return false;
        }

        bool const ok = parse_git_branch_list(arena, list_result.output, out);
        current = first_output_line(current_result.output);
        current = current.empty() ? StrRef("HEAD") : arena_copy_cstr(arena, current);
        message = ok ? StrRef() : StrRef("Failed to parse git branches.");
        return ok;
    }

    auto git_load_commit_log(
        Arena& arena, StrRef root, size_t skip, size_t limit, StrRef& log, StrRef& message
    ) -> bool {
        size_t const count = std::max(limit, GIT_LOG_MIN_LIMIT);
        StrRef const refs =
            git_has_upstream(arena, root) ? StrRef(" HEAD @{upstream}") : StrRef(" HEAD");
#if defined(_WIN32)
        StrRef const date_arg = "--date=\"format-local:%B %#d, %Y at %#I:%M %p\" ";
#else
        StrRef const date_arg = "--date=\"format-local:%B %-d, %Y at %-I:%M %p\" ";
#endif
        StrRef const args = fmt::tprintf(
            "log --skip=%zu --max-count=%zu --topo-order --decorate=short --numstat "
            "%s"
            "--pretty=format:%%x1e%%H%%x1f%%h%%x1f%%P%%x1f%%D%%x1f%%an%%x1f%%ar%%x1f%%ad%%x1f%%s%%"
            "x1f%%b%%x1f%s",
            skip,
            count,
            date_arg,
            refs
        );
        GitRunResult const result = run_git(arena, root, args, false);
        if (!result.ok) {
            set_message(result.output, "git log failed.", message);
            return false;
        }
        log = result.output;
        message = {};
        return true;
    }

    auto git_load_commits(
        Arena& arena, StrRef root, size_t skip, size_t limit, Vec<GitCommit>& out, StrRef& message
    ) -> bool {
        Vec<StrRef> incoming_oids = {};
        if (!incoming_oids.init(0u, arena.resource()) ||
            !git_load_incoming_oids(arena, root, incoming_oids, message)) {
            return false;
        }
        StrRef log = {};
        if (!git_load_commit_log(arena, root, skip, limit, log, message)) {
            return false;
        }
        bool const ok = parse_git_log(arena, log, out);
        if (ok) {
            mark_incoming_commits(out, incoming_oids);
            layout_git_commit_graph(out);
        }
        message = ok ? StrRef() : StrRef("Failed to parse git log.");
        return ok;
    }

    auto git_load_commit_files(
        Arena& arena, StrRef root, StrRef commit_oid, Vec<GitCommitFile>& out, StrRef& message
    ) -> bool {
        StrRef const args = fmt::tprintf(
            "diff-tree --no-commit-id --name-status -z -r --root -m --first-parent %s", commit_oid
        );
        GitRunResult const result = run_git(arena, root, args, false);
        if (!result.ok) {
            set_message(result.output, "git diff-tree failed.", message);
            return false;
        }
        bool const ok = parse_git_name_status(arena, commit_oid, result.output, out);
        message = ok ? StrRef() : StrRef("Failed to parse commit files.");
        return ok;
    }

    auto git_load_pending_pull_count(Arena& arena, StrRef root, size_t& count, StrRef& message)
        -> bool {
        count = 0u;
        if (!git_has_upstream(arena, root)) {
            message = {};
            return true;
        }
        GitRunResult const result =
            run_git(arena, root, "rev-list --count HEAD..@{upstream}", false);
        if (!result.ok || !parse_count_line(result.output, count)) {
            set_message(result.output, "git rev-list failed.", message);
            return false;
        }
        message = {};
        return true;
    }

    auto git_load_pending_push_count(Arena& arena, StrRef root, size_t& count, StrRef& message)
        -> bool {
        count = 0u;
        if (!git_has_upstream(arena, root)) {
            message = {};
            return true;
        }
        GitRunResult const result =
            run_git(arena, root, "rev-list --count @{upstream}..HEAD", false);
        if (!result.ok || !parse_count_line(result.output, count)) {
            set_message(result.output, "git rev-list failed.", message);
            return false;
        }
        message = {};
        return true;
    }

    auto
    git_load_operation_state(Arena& arena, StrRef root, GitOperationState& state, StrRef& message)
        -> bool {
        if (git_path_exists(arena, root, "rebase-merge") ||
            git_path_exists(arena, root, "rebase-apply")) {
            state = GitOperationState::REBASE;
        } else if (git_ref_exists(arena, root, "MERGE_HEAD")) {
            state = GitOperationState::MERGE;
        } else if (git_ref_exists(arena, root, "CHERRY_PICK_HEAD")) {
            state = GitOperationState::CHERRY_PICK;
        } else {
            state = GitOperationState::NONE;
        }
        message = {};
        return true;
    }

    auto git_init(Arena& arena, StrRef path, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, path, "init", false);
        set_message(
            result.output, result.ok ? "Initialized repository." : "git init failed.", message
        );
        return result.ok;
    }

    auto git_stage_path(Arena& arena, StrRef root, StrRef path, StrRef& message) -> bool {
        GitRunResult const result =
            run_git(arena, root, command_with_path(arena, "add", path), false);
        set_message(result.output, result.ok ? "Staged." : "git add failed.", message);
        return result.ok;
    }

    auto git_stage_all(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "add --all", false);
        set_message(result.output, result.ok ? "Staged all." : "git add failed.", message);
        return result.ok;
    }

    auto git_unstage_path(Arena& arena, StrRef root, StrRef path, StrRef& message) -> bool {
        GitRunResult const result =
            run_git(arena, root, command_with_path(arena, "reset", path), false);
        set_message(result.output, result.ok ? "Unstaged." : "git reset failed.", message);
        return result.ok;
    }

    auto git_unstage_all(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "reset", false);
        set_message(result.output, result.ok ? "Unstaged all." : "git reset failed.", message);
        return result.ok;
    }

    auto git_commit(Arena& arena, StrRef root, StrRef message_text, StrRef& message) -> bool {
        StringBuffer args = {};
        args.init(message_text.size() + 16u, arena.resource());
        write_commit_message_args(args, message_text);
        GitRunResult const result = run_git(arena, root, args.str(), false);
        set_message(result.output, result.ok ? "Committed." : "git commit failed.", message);
        return result.ok;
    }

    auto git_push(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "push", false);
        set_message(result.output, result.ok ? "Pushed." : "git push failed.", message);
        return result.ok;
    }

    auto git_branch_publishable(Arena& arena, StrRef root) -> bool {
        GitRunResult const branch = run_git(arena, root, "branch --show-current", false);
        return branch.ok && !first_output_line(branch.output).empty() &&
               git_ref_exists(arena, root, "HEAD") && !git_has_upstream(arena, root);
    }

    auto git_publish_branch(Arena& arena, StrRef root, StrRef remote_url, StrRef& message) -> bool {
        remote_url = remote_url.trim();
        if (remote_url.empty()) {
            message = "Remote URL required.";
            return false;
        }

        GitRunResult const branch_result = run_git(arena, root, "branch --show-current", false);
        StrRef const branch = first_output_line(branch_result.output);
        if (!branch_result.ok || branch.empty()) {
            message = "Current branch required.";
            return false;
        }

        GitRunResult const remote_result = run_git(arena, root, "remote get-url origin", false);
        GitRunResult const remote_set =
            remote_result.ok
                ? run_git(
                      arena,
                      root,
                      command_with_ref(arena, "remote set-url origin", remote_url),
                      false
                  )
                : run_git(
                      arena, root, command_with_ref(arena, "remote add origin", remote_url), false
                  );
        if (!remote_set.ok) {
            set_message(remote_set.output, "git remote failed.", message);
            return false;
        }

        GitRunResult const push =
            run_git(arena, root, command_with_ref(arena, "push -u origin", branch), false);
        set_message(push.output, push.ok ? "Published branch." : "git push failed.", message);
        return push.ok;
    }

    auto git_pull(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "pull --rebase --autostash", false);
        set_message(result.output, result.ok ? "Pulled." : "git pull failed.", message);
        return result.ok;
    }

    auto git_fetch(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "fetch", false);
        set_message(result.output, result.ok ? "Fetched." : "git fetch failed.", message);
        return result.ok;
    }

    auto git_merge_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message) -> bool {
        if (branch.trim().empty()) {
            message = "Branch required.";
            return false;
        }
        GitRunResult const result =
            run_git(arena, root, command_with_ref(arena, "merge --no-edit", branch), false);
        set_message(result.output, result.ok ? "Merged." : "git merge failed.", message);
        return result.ok;
    }

    auto git_rebase_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message) -> bool {
        if (branch.trim().empty()) {
            message = "Branch required.";
            return false;
        }
        GitRunResult const result =
            run_git(arena, root, command_with_ref(arena, "rebase --autostash", branch), false);
        set_message(result.output, result.ok ? "Rebased." : "git rebase failed.", message);
        return result.ok;
    }

    auto git_cherry_pick(Arena& arena, StrRef root, StrRef commit, StrRef& message) -> bool {
        if (commit.trim().empty()) {
            message = "Commit required.";
            return false;
        }
        GitRunResult const result =
            run_git(arena, root, command_with_ref(arena, "cherry-pick", commit), false);
        set_message(
            result.output, result.ok ? "Cherry-picked." : "git cherry-pick failed.", message
        );
        return result.ok;
    }

    auto git_merge_abort(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "merge --abort", false);
        set_message(
            result.output, result.ok ? "Merge aborted." : "git merge --abort failed.", message
        );
        return result.ok;
    }

    auto git_rebase_continue(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result =
            run_git(arena, root, "-c core.editor=true rebase --continue", false);
        set_message(
            result.output,
            result.ok ? "Rebase continued." : "git rebase --continue failed.",
            message
        );
        return result.ok;
    }

    auto git_rebase_abort(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "rebase --abort", false);
        set_message(
            result.output, result.ok ? "Rebase aborted." : "git rebase --abort failed.", message
        );
        return result.ok;
    }

    auto git_cherry_pick_continue(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result =
            run_git(arena, root, "-c core.editor=true cherry-pick --continue", false);
        set_message(
            result.output,
            result.ok ? "Cherry-pick continued." : "git cherry-pick --continue failed.",
            message
        );
        return result.ok;
    }

    auto git_cherry_pick_abort(Arena& arena, StrRef root, StrRef& message) -> bool {
        GitRunResult const result = run_git(arena, root, "cherry-pick --abort", false);
        set_message(
            result.output,
            result.ok ? "Cherry-pick aborted." : "git cherry-pick --abort failed.",
            message
        );
        return result.ok;
    }

    auto git_checkout_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message) -> bool {
        StringBuffer args = {};
        args.init(branch.size() + 16u, arena.resource());
        args.write_string("checkout ");
        write_shell_arg(args, branch);
        GitRunResult const result = run_git(arena, root, args.str(), false);
        set_message(
            result.output, result.ok ? "Checked out branch." : "git checkout failed.", message
        );
        return result.ok;
    }

    auto git_status_patch(
        Arena& arena, StrRef root, GitStatusScope scope, StrRef path, StrRef& patch, StrRef& message
    ) -> bool {
        StringBuffer args = {};
        args.init(path.size() + root.size() + 128u, arena.resource());
        if (scope == GitStatusScope::STAGED) {
            args.write_string("diff --cached --find-renames --src-prefix=a/ --dst-prefix=b/ -- ");
            write_shell_arg(args, path);
        } else if (scope == GitStatusScope::UNTRACKED) {
            StrRef const absolute = absolute_git_path(arena, root, path);
            args.write_string("diff --no-index --src-prefix=a/ --dst-prefix=b/ -- /dev/null ");
            write_shell_arg(args, absolute);
        } else {
            args.write_string("diff --find-renames --src-prefix=a/ --dst-prefix=b/ -- ");
            write_shell_arg(args, path);
        }
        GitRunResult const result = run_git(arena, root, args.str(), true);
        if (!result.ok) {
            set_message(result.output, "git diff failed.", message);
            return false;
        }
        patch = result.output;
        message = {};
        return true;
    }

    auto git_commit_patch(
        Arena& arena, StrRef root, StrRef commit_oid, StrRef path, StrRef& patch, StrRef& message
    ) -> bool {
        StringBuffer args = {};
        args.init(commit_oid.size() + path.size() + 128u, arena.resource());
        args.write_string("diff --find-renames --src-prefix=a/ --dst-prefix=b/ ");
        args.write_string(commit_oid);
        args.write_string("~1 ");
        args.write_string(commit_oid);
        args.write_string(" -- ");
        write_shell_arg(args, path);
        GitRunResult result = run_git(arena, root, args.str(), true);
        if (!result.ok) {
            args.reset();
            args.write_string("show --format= --find-renames --src-prefix=a/ --dst-prefix=b/ ");
            args.write_string(commit_oid);
            args.write_string(" -- ");
            write_shell_arg(args, path);
            result = run_git(arena, root, args.str(), true);
        }
        if (!result.ok) {
            set_message(result.output, "git show failed.", message);
            return false;
        }
        patch = result.output;
        message = {};
        return true;
    }

} // namespace code_editor
