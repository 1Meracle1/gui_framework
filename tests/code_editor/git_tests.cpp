#include "git.h"

#include <test/test.h>

namespace {

    [[nodiscard]] auto status_count(
        Vec<code_editor::GitStatusItem> const& items,
        code_editor::GitStatusScope scope,
        code_editor::GitFileStatus status
    ) -> size_t {
        size_t count = 0u;
        for (code_editor::GitStatusItem const& item : items) {
            if (item.scope == scope && item.status == status) {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto
    row_count(code_editor::GitDiffDocument const& doc, code_editor::GitDiffRowKind kind) -> size_t {
        size_t count = 0u;
        for (code_editor::GitDiffRow const& row : doc.rows) {
            if (row.kind == kind) {
                count += 1u;
            }
        }
        return count;
    }

    TEST_CASE(git_status_parser_handles_porcelain_v1_z_entries) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitStatusItem> items = {};
        TEST_EXPECT(context, items.init(0u, arena.resource()));

        char const text[] = "A  added.txt\0"
                            " M modified.txt\0"
                            " D deleted.txt\0"
                            "R  renamed.txt\0old.txt\0"
                            "UU conflict.txt\0"
                            "?? untracked.txt\0";

        TEST_EXPECT(
            context, code_editor::parse_git_status(arena, StrRef(text, sizeof(text) - 1u), items)
        );
        TEST_EXPECT(context, items.size() == 6u);
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::STAGED, code_editor::GitFileStatus::ADDED
            ) == 1u
        );
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::UNSTAGED, code_editor::GitFileStatus::MODIFIED
            ) == 1u
        );
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::UNSTAGED, code_editor::GitFileStatus::DELETED
            ) == 1u
        );
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::STAGED, code_editor::GitFileStatus::RENAMED
            ) == 1u
        );
        TEST_EXPECT(context, items[3u].path == "renamed.txt");
        TEST_EXPECT(context, items[3u].old_path == "old.txt");
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::UNSTAGED, code_editor::GitFileStatus::UNMERGED
            ) == 1u
        );
        TEST_EXPECT(
            context,
            status_count(
                items, code_editor::GitStatusScope::UNTRACKED, code_editor::GitFileStatus::UNTRACKED
            ) == 1u
        );
    }

    TEST_CASE(git_branch_parser_skips_blank_lines) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitBranch> branches = {};
        TEST_EXPECT(context, branches.init(0u, arena.resource()));

        TEST_EXPECT(
            context,
            code_editor::parse_git_branch_list(arena, "main\r\nfeature/git-panel\n\n", branches)
        );
        TEST_EXPECT(context, branches.size() == 2u);
        TEST_EXPECT(context, branches[0u].name == "main");
        TEST_EXPECT(context, branches[1u].name == "feature/git-panel");
    }

    TEST_CASE(git_log_parser_trims_record_newlines) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitCommit> commits = {};
        TEST_EXPECT(context, commits.init(0u, arena.resource()));

        char const text[] = "aaaaaaaa"
                            "\x1f"
                            "aaaaaaa"
                            "\x1f"
                            "first"
                            "\x1e"
                            "\n"
                            "bbbbbbbb"
                            "\x1f"
                            "bbbbbbb"
                            "\x1f"
                            "second"
                            "\x1e";

        TEST_EXPECT(context, code_editor::parse_git_log(arena, text, commits));
        TEST_EXPECT(context, commits.size() == 2u);
        TEST_EXPECT(context, commits[1u].oid == "bbbbbbbb");
        TEST_EXPECT(context, commits[1u].short_oid == "bbbbbbb");
        TEST_EXPECT(context, commits[1u].summary == "second");
    }

    TEST_CASE(git_log_parser_reads_refs_field) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitCommit> commits = {};
        TEST_EXPECT(context, commits.init(0u, arena.resource()));

        char const text[] = "aaaaaaaa"
                            "\x1f"
                            "aaaaaaa"
                            "\x1f"
                            "HEAD -> main, origin/main"
                            "\x1f"
                            "first"
                            "\x1e";

        TEST_EXPECT(context, code_editor::parse_git_log(arena, text, commits));
        TEST_EXPECT(context, commits.size() == 1u);
        TEST_EXPECT(context, commits[0u].refs == "HEAD -> main, origin/main");
        TEST_EXPECT(context, commits[0u].summary == "first");
    }

    TEST_CASE(git_log_parser_reads_parent_field) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitCommit> commits = {};
        TEST_EXPECT(context, commits.init(0u, arena.resource()));

        char const text[] = "aaaaaaaa"
                            "\x1f"
                            "aaaaaaa"
                            "\x1f"
                            "bbbbbbbb cccccccc"
                            "\x1f"
                            "HEAD -> main"
                            "\x1f"
                            "merge"
                            "\x1e";

        TEST_EXPECT(context, code_editor::parse_git_log(arena, text, commits));
        TEST_EXPECT(context, commits.size() == 1u);
        TEST_EXPECT(context, commits[0u].parent_count == 2u);
        TEST_EXPECT(context, commits[0u].parents[0u] == "bbbbbbbb");
        TEST_EXPECT(context, commits[0u].parents[1u] == "cccccccc");
        TEST_EXPECT(context, commits[0u].refs == "HEAD -> main");
        TEST_EXPECT(context, commits[0u].summary == "merge");
    }

    TEST_CASE(git_log_parser_reads_commit_popup_fields) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitCommit> commits = {};
        TEST_EXPECT(context, commits.init(0u, arena.resource()));

        char const text[] = "\x1e"
                            "aaaaaaaa"
                            "\x1f"
                            "aaaaaaa"
                            "\x1f"
                            "\x1f"
                            "HEAD -> main"
                            "\x1f"
                            "Hector Landa"
                            "\x1f"
                            "3 months ago"
                            "\x1f"
                            "February 06, 2026 at 01:01 PM"
                            "\x1f"
                            "Issue with amend pick"
                            "\x1f"
                            "(cherry picked from commit 29705)\n"
                            "(cherry picked from commit f1f914)"
                            "\x1f"
                            "\n"
                            "1\t1\tfile.cpp\n"
                            "-\t-\tasset.bin\n";

        TEST_EXPECT(context, code_editor::parse_git_log(arena, text, commits));
        TEST_EXPECT(context, commits.size() == 1u);
        TEST_EXPECT(context, commits[0u].author == "Hector Landa");
        TEST_EXPECT(context, commits[0u].relative_date == "3 months ago");
        TEST_EXPECT(context, commits[0u].summary == "Issue with amend pick");
        TEST_EXPECT(context, commits[0u].body.starts_with("(cherry picked"));
        TEST_EXPECT(context, commits[0u].changed_file_count == 2u);
        TEST_EXPECT(context, commits[0u].insertion_count == 1u);
        TEST_EXPECT(context, commits[0u].deletion_count == 1u);
    }

    TEST_CASE(git_graph_layout_assigns_merge_lanes) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitCommit> commits = {};
        TEST_EXPECT(context, commits.init(0u, arena.resource()));

        char const text[] = "aaaaaaaa"
                            "\x1f"
                            "aaaaaaa"
                            "\x1f"
                            "bbbbbbbb cccccccc"
                            "\x1f"
                            "\x1f"
                            "merge commit"
                            "\x1e"
                            "bbbbbbbb"
                            "\x1f"
                            "bbbbbbb"
                            "\x1f"
                            ""
                            "\x1f"
                            "\x1f"
                            "left commit"
                            "\x1e"
                            "cccccccc"
                            "\x1f"
                            "ccccccc"
                            "\x1f"
                            ""
                            "\x1f"
                            "\x1f"
                            "right commit"
                            "\x1e";

        TEST_EXPECT(context, code_editor::parse_git_log(arena, text, commits));
        code_editor::layout_git_commit_graph(commits);
        TEST_EXPECT(context, commits[0u].graph_lane == 0u);
        TEST_EXPECT(context, commits[0u].graph_lane_count == 2u);
        TEST_EXPECT(context, commits[0u].graph_segment_count == 2u);
        TEST_EXPECT(
            context,
            commits[0u].graph_segments[0u].kind ==
                code_editor::GitGraphSegmentKind::COMMIT_TO_BOTTOM
        );
        TEST_EXPECT(context, commits[0u].graph_segments[1u].to_lane == 1u);
        TEST_EXPECT(context, commits[1u].graph_lane == 0u);
        TEST_EXPECT(context, commits[2u].graph_lane == 0u);
    }

    TEST_CASE(git_patch_parser_pairs_modified_rows_and_headers) {
        Arena arena = {};
        arena.init();

        code_editor::GitDiffDocument doc = {};
        TEST_EXPECT(
            context,
            code_editor::parse_git_patch(
                arena,
                "sample",
                "diff --git a/file.txt b/file.txt\n"
                "index 111..222 100644\n"
                "--- a/file.txt\n"
                "+++ b/file.txt\n"
                "@@ -1,3 +1,3 @@\n"
                " keep\n"
                "-old word\n"
                "+new word\n"
                " tail\n",
                doc
            )
        );
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::HUNK_HEADER) == 1u);
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::MODIFIED) == 1u);
        code_editor::GitDiffRow const* modified = nullptr;
        for (code_editor::GitDiffRow const& row : doc.rows) {
            if (row.kind == code_editor::GitDiffRowKind::MODIFIED) {
                modified = &row;
            }
        }
        TEST_EXPECT(context, modified != nullptr);
        TEST_EXPECT(context, modified != nullptr && modified->left_text == "old word");
        TEST_EXPECT(context, modified != nullptr && modified->right_text == "new word");
        TEST_EXPECT(context, modified != nullptr && !modified->left_spans.empty());
        TEST_EXPECT(context, modified != nullptr && !modified->right_spans.empty());
    }

    TEST_CASE(git_diff_renderer_formats_inline_view_without_git_metadata) {
        Arena arena = {};
        arena.init();

        code_editor::GitDiffDocument doc = {};
        TEST_EXPECT(
            context,
            code_editor::parse_git_patch(
                arena,
                "sample",
                "diff --git a/file.txt b/file.txt\n"
                "index 111..222 100644\n"
                "--- a/file.txt\n"
                "+++ b/file.txt\n"
                "@@ -1,3 +1,3 @@\n"
                " keep\n"
                "-old word\n"
                "+new word\n",
                doc
            )
        );

        StrRef const rendered = code_editor::git_render_diff_document(arena, doc, false);
        TEST_EXPECT(context, !rendered.contains("diff --git"));
        TEST_EXPECT(context, !rendered.contains("index 111"));
        TEST_EXPECT(context, rendered.contains("@@ -1,3 +1,3 @@"));
        TEST_EXPECT(context, rendered.contains("     1     1  keep"));
        TEST_EXPECT(context, rendered.contains("-    2        old word"));
        TEST_EXPECT(context, rendered.contains("+          2  new word"));
    }

    TEST_CASE(git_patch_parser_handles_added_deleted_rename_binary_and_markers) {
        Arena arena = {};
        arena.init();

        code_editor::GitDiffDocument doc = {};
        TEST_EXPECT(
            context,
            code_editor::parse_git_patch(
                arena,
                "mixed",
                "diff --git a/old.txt b/new.txt\r\n"
                "similarity index 88%\r\n"
                "rename from old.txt\r\n"
                "rename to new.txt\r\n"
                "--- a/old.txt\r\n"
                "+++ b/new.txt\r\n"
                "@@ -1 +1 @@\r\n"
                "-old\r\n"
                "+new\r\n"
                "\\ No newline at end of file\r\n"
                "diff --git a/add.txt b/add.txt\r\n"
                "new file mode 100644\r\n"
                "--- /dev/null\r\n"
                "+++ b/add.txt\r\n"
                "@@ -0,0 +1 @@\r\n"
                "+added\r\n"
                "diff --git a/delete.txt b/delete.txt\r\n"
                "deleted file mode 100644\r\n"
                "--- a/delete.txt\r\n"
                "+++ /dev/null\r\n"
                "@@ -1 +0,0 @@\r\n"
                "-deleted\r\n"
                "Binary files a/bin.dat and b/bin.dat differ\r\n",
                doc
            )
        );
        TEST_EXPECT(context, doc.binary);
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::ADDED) == 1u);
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::REMOVED) == 1u);
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::MODIFIED) == 1u);
        TEST_EXPECT(context, row_count(doc, code_editor::GitDiffRowKind::HUNK_HEADER) == 3u);
    }

    TEST_CASE(git_inline_diff_marks_words_punctuation_and_long_lines) {
        Arena arena = {};
        arena.init();
        Vec<code_editor::GitInlineSpan> left = {};
        Vec<code_editor::GitInlineSpan> right = {};
        TEST_EXPECT(context, left.init(0u, arena.resource()));
        TEST_EXPECT(context, right.init(0u, arena.resource()));

        code_editor::compute_git_inline_diff(
            arena, "hello world", "hello brave world", left, right
        );
        TEST_EXPECT(context, left.empty());
        TEST_EXPECT(context, !right.empty());

        code_editor::compute_git_inline_diff(arena, "call(a)", "call(b)", left, right);
        TEST_EXPECT(context, left.size() == 1u);
        TEST_EXPECT(context, right.size() == 1u);
        TEST_EXPECT(context, left[0u].offset == 5u);
        TEST_EXPECT(context, right[0u].offset == 5u);

        char long_left[1001] = {};
        char long_right[1001] = {};
        for (size_t index = 0u; index < sizeof(long_left); ++index) {
            long_left[index] = 'a';
            long_right[index] = 'a';
        }
        long_right[0u] = 'b';
        code_editor::compute_git_inline_diff(
            arena,
            StrRef(long_left, sizeof(long_left)),
            StrRef(long_right, sizeof(long_right)),
            left,
            right
        );
        TEST_EXPECT(context, left.size() == 1u);
        TEST_EXPECT(context, right.size() == 1u);
        TEST_EXPECT(context, left[0u].size == sizeof(long_left));
        TEST_EXPECT(context, right[0u].size == sizeof(long_right));
    }

} // namespace

TEST_MAIN()
