#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <base/vec.h>
#include <cstddef>
#include <cstdint>

namespace code_editor {

    inline constexpr size_t GIT_LOG_MIN_LIMIT = 32u;
    inline constexpr size_t GIT_LOG_PAGE_SIZE = 32u;
    inline constexpr size_t GIT_INLINE_DIFF_MAX_LINE = 1000u;
    inline constexpr size_t GIT_COMMIT_MAX_PARENTS = 8u;
    inline constexpr size_t GIT_GRAPH_MAX_LANES = 16u;
    inline constexpr size_t GIT_GRAPH_MAX_SEGMENTS = 32u;

    enum class GitFileStatus : uint8_t {
        ADDED,
        MODIFIED,
        DELETED,
        RENAMED,
        TYPE_CHANGED,
        UNMERGED,
        UNTRACKED,
    };

    enum class GitStatusScope : uint8_t {
        STAGED,
        UNSTAGED,
        UNTRACKED,
    };

    struct GitStatusItem {
        StrRef path = {};
        StrRef old_path = {};
        GitFileStatus status = GitFileStatus::MODIFIED;
        GitStatusScope scope = GitStatusScope::UNSTAGED;
    };

    enum class GitGraphSegmentKind : uint8_t {
        TOP_TO_COMMIT,
        TOP_TO_BOTTOM,
        COMMIT_TO_BOTTOM,
    };

    struct GitGraphSegment {
        uint8_t from_lane = 0u;
        uint8_t to_lane = 0u;
        GitGraphSegmentKind kind = GitGraphSegmentKind::TOP_TO_BOTTOM;
    };

    struct GitCommit {
        StrRef oid = {};
        StrRef short_oid = {};
        StrRef parents[GIT_COMMIT_MAX_PARENTS] = {};
        StrRef refs = {};
        StrRef summary = {};
        StrRef author = {};
        StrRef relative_date = {};
        StrRef author_date = {};
        StrRef body = {};
        GitGraphSegment graph_segments[GIT_GRAPH_MAX_SEGMENTS] = {};
        size_t changed_file_count = 0u;
        size_t insertion_count = 0u;
        size_t deletion_count = 0u;
        size_t parent_count = 0u;
        size_t graph_segment_count = 0u;
        uint8_t graph_lane = 0u;
        uint8_t graph_lane_count = 1u;
        bool incoming = false;
        bool open = false;
    };

    struct GitCommitFile {
        StrRef commit_oid = {};
        StrRef path = {};
        StrRef old_path = {};
        GitFileStatus status = GitFileStatus::MODIFIED;
    };

    struct GitBranch {
        StrRef name = {};
    };

    enum class GitOperationState : uint8_t {
        NONE,
        MERGE,
        REBASE,
        CHERRY_PICK,
    };

    enum class GitDiffRowKind : uint8_t {
        FILE_HEADER,
        HUNK_HEADER,
        CONTEXT,
        ADDED,
        REMOVED,
        MODIFIED,
    };

    struct GitInlineSpan {
        size_t offset = 0u;
        size_t size = 0u;
    };

    struct GitDiffRow {
        GitDiffRowKind kind = GitDiffRowKind::CONTEXT;
        StrRef left_text = {};
        StrRef right_text = {};
        size_t old_line = 0u;
        size_t new_line = 0u;
        Vec<GitInlineSpan> left_spans = {};
        Vec<GitInlineSpan> right_spans = {};
    };

    struct GitDiffDocument {
        StrRef title = {};
        Vec<GitDiffRow> rows = {};
        bool binary = false;
    };

    enum class GitRequestKind : uint8_t {
        NONE,
        REFRESH,
        INIT_REPOSITORY,
        STAGE,
        STAGE_ALL,
        UNSTAGE,
        UNSTAGE_ALL,
        COMMIT,
        PUSH,
        PUBLISH_BRANCH,
        PULL,
        FETCH,
        MERGE_BRANCH,
        REBASE_BRANCH,
        CHERRY_PICK,
        MERGE_ABORT,
        REBASE_CONTINUE,
        REBASE_ABORT,
        CHERRY_PICK_CONTINUE,
        CHERRY_PICK_ABORT,
        CHECKOUT_BRANCH,
        OPEN_STATUS_DIFF,
        OPEN_COMMIT_DIFF,
    };

    struct GitRequest {
        GitRequestKind kind = GitRequestKind::NONE;
        GitStatusScope scope = GitStatusScope::UNSTAGED;
        StrRef path = {};
        StrRef old_path = {};
        StrRef commit_oid = {};
        StrRef message = {};
        StrRef branch = {};
        StrRef remote_url = {};
    };

    struct GitRunResult {
        StrRef output = {};
        int exit_code = 0;
        bool ok = false;
    };

    [[nodiscard]] auto git_file_status_label(GitFileStatus status) -> StrRef;
    [[nodiscard]] auto git_status_scope_label(GitStatusScope scope) -> StrRef;
    [[nodiscard]] inline auto git_text_contains_ignore_ascii_case(StrRef text, StrRef query)
        -> bool {
        query = query.trim();
        if (query.empty()) {
            return true;
        }
        if (query.size() > text.size()) {
            return false;
        }
        for (size_t start = 0u; start <= text.size() - query.size(); ++start) {
            size_t index = 0u;
            while (index < query.size() &&
                   to_ascii_lower(text[start + index]) == to_ascii_lower(query[index])) {
                index += 1u;
            }
            if (index == query.size()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline auto git_branch_matches_search(GitBranch const& branch, StrRef query)
        -> bool {
        return git_text_contains_ignore_ascii_case(branch.name, query);
    }

    [[nodiscard]] inline auto git_commit_matches_search(GitCommit const& commit, StrRef query)
        -> bool {
        query = query.trim();
        return query.empty() || git_text_contains_ignore_ascii_case(commit.summary, query) ||
               git_text_contains_ignore_ascii_case(commit.author, query) ||
               git_text_contains_ignore_ascii_case(commit.short_oid, query) ||
               git_text_contains_ignore_ascii_case(commit.oid, query) ||
               git_text_contains_ignore_ascii_case(commit.refs, query);
    }

    [[nodiscard]] auto parse_git_status(Arena& arena, StrRef text, Vec<GitStatusItem>& out) -> bool;
    [[nodiscard]] auto parse_git_branch_list(Arena& arena, StrRef text, Vec<GitBranch>& out)
        -> bool;
    [[nodiscard]] auto parse_git_log(Arena& arena, StrRef text, Vec<GitCommit>& out) -> bool;
    auto layout_git_commit_graph(Vec<GitCommit>& commits) -> void;
    [[nodiscard]] auto
    parse_git_name_status(Arena& arena, StrRef commit_oid, StrRef text, Vec<GitCommitFile>& out)
        -> bool;
    [[nodiscard]] auto
    parse_git_patch(Arena& arena, StrRef title, StrRef text, GitDiffDocument& out) -> bool;
    [[nodiscard]] auto
    git_render_diff_document(Arena& arena, GitDiffDocument const& doc, bool side_by_side) -> StrRef;
    auto compute_git_inline_diff(
        Arena& arena,
        StrRef left,
        StrRef right,
        Vec<GitInlineSpan>& left_spans,
        Vec<GitInlineSpan>& right_spans
    ) -> void;

    [[nodiscard]] auto git_discover_root(Arena& arena, StrRef path, StrRef& root, StrRef& message)
        -> bool;
    [[nodiscard]] auto
    git_load_status(Arena& arena, StrRef root, Vec<GitStatusItem>& out, StrRef& message) -> bool;
    [[nodiscard]] auto git_load_status_path(
        Arena& arena, StrRef root, StrRef path, Vec<GitStatusItem>& out, StrRef& message
    ) -> bool;
    [[nodiscard]] auto git_load_branches(
        Arena& arena, StrRef root, Vec<GitBranch>& out, StrRef& current, StrRef& message
    ) -> bool;
    [[nodiscard]] auto git_load_commit_log(
        Arena& arena, StrRef root, size_t skip, size_t limit, StrRef& log, StrRef& message
    ) -> bool;
    [[nodiscard]] auto git_load_commits(
        Arena& arena, StrRef root, size_t skip, size_t limit, Vec<GitCommit>& out, StrRef& message
    ) -> bool;
    [[nodiscard]] auto git_load_commit_files(
        Arena& arena, StrRef root, StrRef commit_oid, Vec<GitCommitFile>& out, StrRef& message
    ) -> bool;
    [[nodiscard]] auto
    git_load_pending_pull_count(Arena& arena, StrRef root, size_t& count, StrRef& message) -> bool;
    [[nodiscard]] auto
    git_load_pending_push_count(Arena& arena, StrRef root, size_t& count, StrRef& message) -> bool;
    [[nodiscard]] auto
    git_load_operation_state(Arena& arena, StrRef root, GitOperationState& state, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_init(Arena& arena, StrRef path, StrRef& message) -> bool;
    [[nodiscard]] auto git_stage_path(Arena& arena, StrRef root, StrRef path, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_stage_all(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_unstage_path(Arena& arena, StrRef root, StrRef path, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_unstage_all(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_commit(Arena& arena, StrRef root, StrRef message_text, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_push(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_branch_publishable(Arena& arena, StrRef root) -> bool;
    [[nodiscard]] auto
    git_publish_branch(Arena& arena, StrRef root, StrRef remote_url, StrRef& message) -> bool;
    [[nodiscard]] auto git_pull(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_fetch(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_merge_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_rebase_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_cherry_pick(Arena& arena, StrRef root, StrRef commit, StrRef& message)
        -> bool;
    [[nodiscard]] auto git_merge_abort(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_rebase_continue(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_rebase_abort(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_cherry_pick_continue(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto git_cherry_pick_abort(Arena& arena, StrRef root, StrRef& message) -> bool;
    [[nodiscard]] auto
    git_checkout_branch(Arena& arena, StrRef root, StrRef branch, StrRef& message) -> bool;
    [[nodiscard]] auto git_status_patch(
        Arena& arena, StrRef root, GitStatusScope scope, StrRef path, StrRef& patch, StrRef& message
    ) -> bool;
    [[nodiscard]] auto git_commit_patch(
        Arena& arena, StrRef root, StrRef commit_oid, StrRef path, StrRef& patch, StrRef& message
    ) -> bool;

} // namespace code_editor
