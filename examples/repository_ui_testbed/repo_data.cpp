#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "repo_data.h"

#include <base/fmt.h>
#include <base/memory.h>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

namespace repository_ui_testbed {

    [[nodiscard]] auto repo_tree_open_hash(RepoTree const& tree) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        for (size_t index = 0u; index < tree.node_count; ++index) {
            hash ^= tree.nodes[index].open ? index + 1u : 0u;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] auto trim_command_line(StrRef text) -> StrRef {
        return text.trim_end_matches('\n').trim_end_matches('\r');
    }

    [[nodiscard]] auto compare_repo_nodes(RepoNode const& lhs, RepoNode const& rhs) -> int32_t {
        if (lhs.directory != rhs.directory) {
            return lhs.directory ? -1 : 1;
        }
        int32_t const name_compare = lhs.name.compare_ignore_ascii_case(rhs.name);
        return name_compare != 0 ? name_compare : lhs.path.compare_ignore_ascii_case(rhs.path);
    }

    [[nodiscard]] auto child_head(RepoTree& tree, int32_t parent) -> int32_t* {
        return parent >= 0 ? &tree.nodes[parent].first_child : &tree.root_child;
    }

    [[nodiscard]] auto first_child(RepoTree const& tree, int32_t parent) -> int32_t {
        return parent >= 0 ? tree.nodes[parent].first_child : tree.root_child;
    }

    [[nodiscard]] auto find_child(RepoTree const& tree, int32_t parent, StrRef name) -> int32_t {
        for (int32_t child = first_child(tree, parent); child >= 0;
             child = tree.nodes[child].next_sibling) {
            if (tree.nodes[child].name == name) {
                return child;
            }
        }
        return -1;
    }

    auto insert_child_sorted(RepoTree& tree, int32_t parent, int32_t node_index) -> void {
        int32_t* const head = child_head(tree, parent);
        int32_t previous = -1;
        int32_t current = *head;
        while (current >= 0 &&
               compare_repo_nodes(tree.nodes[current], tree.nodes[node_index]) <= 0) {
            previous = current;
            current = tree.nodes[current].next_sibling;
        }

        if (previous < 0) {
            tree.nodes[node_index].next_sibling = *head;
            *head = node_index;
        } else {
            tree.nodes[node_index].next_sibling = tree.nodes[previous].next_sibling;
            tree.nodes[previous].next_sibling = node_index;
        }
    }

    [[nodiscard]] auto create_repo_node(
        Arena& arena, RepoTree& tree, int32_t parent, StrRef name, StrRef path, bool directory
    ) -> int32_t {
        if (tree.node_count >= MAX_REPO_NODES) {
            return -1;
        }

        int32_t const node_index = static_cast<int32_t>(tree.node_count);
        tree.node_count += 1u;

        RepoNode& node = tree.nodes[node_index];
        node.name = arena_copy_str(arena, name);
        node.path = arena_copy_str(arena, path);
        node.message = "Loading commit data";
        node.age = {};
        node.parent = parent;
        node.indent = parent >= 0 ? static_cast<uint8_t>(tree.nodes[parent].indent + 1u) : 0u;
        node.directory = directory;
        node.open = false;
        insert_child_sorted(tree, parent, node_index);
        return node_index;
    }

    auto repo_tree_add_path(Arena& arena, RepoTree& tree, StrRef path) -> void {
        int32_t parent = -1;
        size_t segment_start = 0u;
        for (size_t cursor = 0u; cursor <= path.size(); ++cursor) {
            char const value = cursor < path.size() ? path[cursor] : '\0';
            if (value != '/' && value != '\\' && value != '\0') {
                continue;
            }
            if (cursor == segment_start) {
                segment_start = cursor + 1u;
                continue;
            }

            StrRef const name = path.slice(segment_start, cursor - segment_start);
            bool const directory = value != '\0';
            int32_t child = find_child(tree, parent, name);
            if (child < 0) {
                child = create_repo_node(arena, tree, parent, name, path.prefix(cursor), directory);
            } else if (directory) {
                tree.nodes[child].directory = true;
            }
            if (child < 0) {
                return;
            }
            parent = child;
            segment_start = cursor + 1u;
        }
    }

    [[nodiscard]] auto read_first_command_line(StrRef command, char* line, size_t capacity)
        -> StrRef {
        line[0] = '\0';
        FILE* pipe = _popen(command.data(), "r");
        if (pipe == nullptr) {
            return {};
        }
        bool const read = std::fgets(line, static_cast<int>(capacity), pipe) != nullptr;
        _pclose(pipe);
        return read ? trim_command_line(StrRef(line)) : StrRef();
    }

    [[nodiscard]] auto read_first_command_line(Arena& arena, StrRef command) -> StrRef {
        ArenaTemp temp = begin_thread_temp_arena();
        char* const line = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
        StrRef const result = read_first_command_line(command, line, REPO_COMMAND_LINE_CAPACITY);
        if (result.empty()) {
            return {};
        }
        return arena_copy_str(arena, result);
    }

    [[nodiscard]] auto repo_full_path(RepoTree const& tree, StrRef relative_path) -> StrRef {
        return fmt::tprintf("%s/%s", tree.root, relative_path);
    }

    [[nodiscard]] auto repo_file_size(RepoTree const& tree, StrRef relative_path) -> uint64_t {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const path = repo_full_path(tree, relative_path);

        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExA(path.data(), GetFileExInfoStandard, &data)) {
            return 0u;
        }

        ULARGE_INTEGER size = {};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        return size.QuadPart;
    }

    [[nodiscard]] auto repo_package_bytes(RepoTree const& tree) -> uint64_t {
        uint64_t bytes = 0u;
        for (size_t index = 0u; index < tree.node_count; ++index) {
            RepoNode const& node = tree.nodes[index];
            if (!node.directory) {
                bytes += repo_file_size(tree, node.path);
            }
        }
        return bytes;
    }

    [[nodiscard]] auto repo_root_name(RepoTree const& tree) -> StrRef {
        StrRef name = tree.root;
        for (size_t index = 0u; index < tree.root.size(); ++index) {
            if (tree.root[index] == '/' || tree.root[index] == '\\') {
                name = tree.root.drop_prefix(index + 1u);
            }
        }
        return name;
    }

    auto load_readme_description(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const path = repo_full_path(tree, "README.md");
        FILE* file = nullptr;
        fopen_s(&file, path.data(), "r");
        if (file == nullptr) {
            details.description = "No README summary found.";
            return;
        }

        char* const line = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
        while (std::fgets(line, static_cast<int>(REPO_COMMAND_LINE_CAPACITY), file) != nullptr) {
            StrRef const text = trim_command_line(StrRef(line));
            if (text.empty() || text.starts_with('#')) {
                continue;
            }
            details.description = arena_copy_str(arena, text);
            break;
        }
        std::fclose(file);

        if (details.description.empty()) {
            details.description = "No README summary found.";
        }
    }

    [[nodiscard]] auto format_byte_size(Arena& arena, uint64_t bytes) -> StrRef {
        char const* suffix = "B";
        double value = static_cast<double>(bytes);
        if (bytes >= 1024ull * 1024ull) {
            value /= 1024.0 * 1024.0;
            suffix = "MB";
        } else if (bytes >= 1024ull) {
            value /= 1024.0;
            suffix = "KB";
        }
        return fmt::aprintf(arena.resource(), "%.1f %s", value, suffix).str();
    }

    auto load_license_name(RepoTree const& tree, RepoDetails& details) -> void {
        constexpr StrRef NAMES[] = {"LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"};
        ArenaTemp temp = begin_thread_temp_arena();
        for (StrRef name : NAMES) {
            StrRef const path = repo_full_path(tree, name);
            if (GetFileAttributesA(path.data()) != INVALID_FILE_ATTRIBUTES) {
                details.license = name;
                return;
            }
        }
        details.license = "Unspecified";
    }

    auto load_latest_commit_details(Arena& arena, RepoTree const& tree, RepoDetails& details)
        -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" log -1 --format=\"%%an%%x09%%ar%%x09%%h%%x09%%s\" 2>nul", tree.root
        );
        char* const line_buffer = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
        StrRef const line =
            read_first_command_line(command, line_buffer, REPO_COMMAND_LINE_CAPACITY);
        StrRef::SplitOnce const author_split = line.split_once('\t');
        if (!author_split) {
            return;
        }
        StrRef::SplitOnce const age_split = author_split.after.split_once('\t');
        if (!age_split) {
            return;
        }
        StrRef::SplitOnce const hash_split = age_split.after.split_once('\t');
        if (!hash_split) {
            return;
        }

        details.author = arena_copy_str(arena, author_split.before);
        details.author_initial = author_split.before.empty()
                                     ? StrRef("?")
                                     : arena_copy_str(arena, author_split.before.prefix(1u));
        details.last_commit_age = arena_copy_str(arena, age_split.before);
        details.short_hash = arena_copy_str(arena, hash_split.before);
        details.last_commit_subject = arena_copy_str(arena, hash_split.after);
    }

    auto load_commit_count(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf("git -C \"%s\" rev-list --count HEAD 2>nul", tree.root);
        StrRef const count = read_first_command_line(arena, command);
        details.commit_count = count.empty() ? StrRef("0") : count;
    }

    auto load_recent_commits(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" log --max-count=%llu --format=\"%%h%%x09%%an%%x09%%ar%%x09%%s\" 2>nul",
            tree.root,
            static_cast<unsigned long long>(MAX_REPO_COMMITS)
        );
        FILE* pipe = _popen(command.data(), "r");
        if (pipe == nullptr) {
            return;
        }

        char* const line_buffer = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
        while (details.shown_commit_count < MAX_REPO_COMMITS &&
               std::fgets(line_buffer, static_cast<int>(REPO_COMMAND_LINE_CAPACITY), pipe) !=
                   nullptr) {
            StrRef const line = trim_command_line(StrRef(line_buffer));
            StrRef::SplitOnce const hash_split = line.split_once('\t');
            if (!hash_split) {
                continue;
            }
            StrRef::SplitOnce const author_split = hash_split.after.split_once('\t');
            if (!author_split) {
                continue;
            }
            StrRef::SplitOnce const age_split = author_split.after.split_once('\t');
            if (!age_split) {
                continue;
            }

            RepoCommit& commit = details.commits[details.shown_commit_count];
            commit.hash = arena_copy_str(arena, hash_split.before);
            commit.author = arena_copy_str(arena, author_split.before);
            commit.age = arena_copy_str(arena, age_split.before);
            commit.subject = arena_copy_str(arena, age_split.after);
            details.shown_commit_count += 1u;
        }
        _pclose(pipe);
    }

    auto load_head_stats(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" show --numstat --format= --no-renames HEAD 2>nul", tree.root
        );

        uint64_t insertions = 0u;
        uint64_t deletions = 0u;
        FILE* pipe = _popen(command.data(), "r");
        if (pipe != nullptr) {
            char* const line = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
            while (std::fgets(line, static_cast<int>(REPO_COMMAND_LINE_CAPACITY), pipe) !=
                   nullptr) {
                char* end = nullptr;
                unsigned long const added = std::strtoul(line, &end, 10);
                if (end == line || *end != '\t') {
                    continue;
                }
                char* const deleted_text = end + 1;
                unsigned long const deleted = std::strtoul(deleted_text, &end, 10);
                if (end == deleted_text) {
                    continue;
                }
                insertions += added;
                deletions += deleted;
            }
            _pclose(pipe);
        }

        details.insertion_count =
            fmt::aprintf(arena.resource(), "+%llu", static_cast<unsigned long long>(insertions))
                .str();
        details.deletion_count =
            fmt::aprintf(arena.resource(), "-%llu", static_cast<unsigned long long>(deletions))
                .str();
    }

    auto load_repo_details(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void {
        details = {};
        details.name = arena_copy_str(arena, repo_root_name(tree));
        load_readme_description(arena, tree, details);

        {
            ArenaTemp temp = begin_thread_temp_arena();
            StrRef const command =
                fmt::tprintf("git -C \"%s\" branch --show-current 2>nul", tree.root);
            StrRef const branch = read_first_command_line(arena, command);
            details.branch = branch.empty() ? StrRef("HEAD") : branch;
        }

        load_latest_commit_details(arena, tree, details);
        load_commit_count(arena, tree, details);
        load_recent_commits(arena, tree, details);

        {
            ArenaTemp temp = begin_thread_temp_arena();
            StrRef const command =
                fmt::tprintf("git -C \"%s\" describe --tags --abbrev=0 2>nul", tree.root);
            StrRef const latest_release = read_first_command_line(arena, command);
            details.latest_release = latest_release.empty() ? StrRef("No tags") : latest_release;
        }

        details.package_size = format_byte_size(arena, repo_package_bytes(tree));
        load_license_name(tree, details);

        {
            ArenaTemp temp = begin_thread_temp_arena();
            StrRef const command = fmt::tprintf(
                "git -C \"%s\" rev-list --count --since=\"30 days ago\" HEAD 2>nul", tree.root
            );
            StrRef const count = read_first_command_line(arena, command);
            details.activity =
                count.empty() ? StrRef("Unknown")
                              : fmt::aprintf(arena.resource(), "%s commits / 30d", count).str();
        }

        load_head_stats(arena, tree, details);
        if (details.last_commit_age.empty()) {
            details.last_commit_age = "No commits";
        }
    }

    [[nodiscard]] auto find_node_by_path(RepoTree const& tree, StrRef path) -> int32_t {
        for (size_t index = 0u; index < tree.node_count; ++index) {
            if (tree.nodes[index].path == path) {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    auto set_commit_if_empty(Arena& arena, RepoNode& node, StrRef age, StrRef message) -> void {
        if (node.has_commit) {
            return;
        }
        node.age = arena_copy_str(arena, age);
        node.message = arena_copy_str(arena, message);
        node.has_commit = true;
    }

    auto set_node_and_parent_commits(
        Arena& arena, RepoTree& tree, int32_t node_index, StrRef age, StrRef message
    ) -> void {
        for (int32_t index = node_index; index >= 0; index = tree.nodes[index].parent) {
            set_commit_if_empty(arena, tree.nodes[index], age, message);
        }
    }

    auto load_repo_commits(Arena& arena, RepoTree& tree) -> void {
        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" log --name-only --format=\"commit%%x09%%ar%%x09%%s\" -- 2>nul", tree.root
        );
        FILE* pipe = _popen(command.data(), "r");
        if (pipe == nullptr) {
            return;
        }

        StrRef age = {};
        StrRef message = {};
        char* const line_buffer = arena_alloc<char>(*temp.arena(), REPO_COMMAND_LINE_CAPACITY);
        while (std::fgets(line_buffer, static_cast<int>(REPO_COMMAND_LINE_CAPACITY), pipe) !=
               nullptr) {
            StrRef const line = trim_command_line(StrRef(line_buffer));
            if (line.empty()) {
                continue;
            }

            StrRef commit = {};
            if (line.strip_prefix("commit\t", &commit)) {
                StrRef::SplitOnce const commit_split = commit.split_once('\t');
                if (commit_split) {
                    age = arena_copy_str(*temp.arena(), commit_split.before);
                    message = arena_copy_str(*temp.arena(), commit_split.after);
                }
                continue;
            }

            int32_t const node_index = find_node_by_path(tree, line);
            if (node_index >= 0 && !age.empty()) {
                set_node_and_parent_commits(arena, tree, node_index, age, message);
            }
        }
        _pclose(pipe);

        for (size_t index = 0u; index < tree.node_count; ++index) {
            RepoNode& node = tree.nodes[index];
            if (!node.has_commit) {
                node.message = node.directory ? StrRef("No committed files") : StrRef("Untracked");
                node.age = "not committed";
            }
        }
    }

    auto load_repo_tree(Arena& arena, RepoTree& tree) -> void {
        tree = {};
        tree.root = read_first_command_line(arena, "git rev-parse --show-toplevel 2>nul");
        if (tree.root.empty()) {
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        StrRef const command = fmt::tprintf(
            "git -C \"%s\" ls-files --cached --others --exclude-standard 2>nul", tree.root
        );
        FILE* pipe = _popen(command.data(), "r");
        if (pipe == nullptr) {
            return;
        }
        char* const path = arena_alloc<char>(*temp.arena(), MAX_REPO_PATH);
        while (std::fgets(path, static_cast<int>(MAX_REPO_PATH), pipe) != nullptr) {
            StrRef const text = trim_command_line(StrRef(path));
            if (!text.empty()) {
                repo_tree_add_path(arena, tree, text);
            }
        }
        _pclose(pipe);

        load_repo_commits(arena, tree);
        tree.loaded = true;
    }

    auto push_visible_node(RepoTree& tree, int32_t node_index) -> void {
        if (node_index < 0 || tree.visible_count >= MAX_REPO_NODES) {
            return;
        }
        tree.visible[tree.visible_count] = node_index;
        tree.visible_count += 1u;

        RepoNode const& node = tree.nodes[node_index];
        if (!node.directory || !node.open) {
            return;
        }
        for (int32_t child = node.first_child; child >= 0; child = tree.nodes[child].next_sibling) {
            push_visible_node(tree, child);
        }
    }

    auto rebuild_visible_tree(RepoTree& tree) -> void {
        tree.visible_count = 0u;
        for (int32_t child = tree.root_child; child >= 0; child = tree.nodes[child].next_sibling) {
            push_visible_node(tree, child);
        }
    }

} // namespace repository_ui_testbed
