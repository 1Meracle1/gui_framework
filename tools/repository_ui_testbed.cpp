#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <draw/draw.h>
#include <draw/draw_renderer.h>
#include <dwmapi.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <gui/gui.h>
#include <render/render.h>
#include <repository_ui_testbed_embedded_codicons.h>
#include <windows.h>

namespace {

    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_repository_ui_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1600u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 900u;
    constexpr float SIDEBAR_WIDTH = 220.0f;
    constexpr DWORD DWM_ATTR_USE_IMMERSIVE_DARK_MODE = 20u;
    constexpr DWORD DWM_ATTR_BORDER_COLOR = 34u;
    constexpr DWORD DWM_ATTR_CAPTION_COLOR = 35u;
    constexpr DWORD DWM_ATTR_TEXT_COLOR = 36u;
    constexpr COLORREF WINDOW_HEADER_BACKGROUND = RGB(0, 0, 0);
    constexpr COLORREF WINDOW_HEADER_TEXT = RGB(233, 233, 233);
    constexpr size_t MAX_REPO_NODES = 1024u;
    constexpr size_t MAX_REPO_PATH = 384u;
    constexpr size_t MAX_REPO_NAME = 96u;
    constexpr size_t MAX_REPO_MESSAGE = 160u;
    constexpr size_t MAX_REPO_AGE = 48u;
    constexpr size_t MAX_REPO_DETAIL = 192u;
    constexpr size_t MAX_REPO_COMMITS = 64u;
    constexpr char ICON_CODE[] = "\xee\xab\x84";
    constexpr char ICON_ISSUES[] = "\xee\xac\x8c";
    constexpr char ICON_PULL_REQUESTS[] = "\xee\xa9\xa4";
    constexpr char ICON_ACTIONS[] = "\xee\xac\xac";
    constexpr char ICON_PROJECTS[] = "\xee\xac\xb0";
    constexpr char ICON_SECURITY[] = "\xee\xad\x93";
    constexpr char ICON_INSIGHTS[] = "\xee\xaf\xa2";
    constexpr char ICON_WIKI[] = "\xee\xaa\xa4";
    constexpr char ICON_SETTINGS[] = "\xee\xab\xb8";
    constexpr char ICON_CHEVRON_RIGHT[] = "\xee\xaa\xb6";

    struct RepositorySpec {
        gui::Color shell = gui::rgb(0, 0, 0);
        gui::Color panel = gui::rgb(3, 3, 3);
        gui::Color raised = gui::rgb(12, 12, 12);
        gui::Color control = gui::rgb(8, 8, 8);
        gui::Color control_hovered = gui::rgb(20, 20, 20);
        gui::Color selected = gui::rgb(31, 31, 31);
        gui::Color text = gui::rgb(233, 233, 233);
        gui::Color muted = gui::rgb(136, 136, 142);
        gui::Color faint = gui::rgb(86, 86, 92);
        gui::Color border = gui::rgb(32, 32, 32);
        gui::Color strong_border = gui::rgb(45, 45, 45);
        gui::Color green = gui::rgb(61, 185, 126);
        gui::Color red = gui::rgb(220, 88, 92);
        gui::Color blue = gui::rgb(11, 80, 129);
    };

    struct Runtime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        font_cache::Font icon_font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::Context ui_context = {};
        char icon_font_path[MAX_PATH] = {};
    };

    enum class RepositorySection : uint8_t {
        CODE,
        ISSUES,
        PULL_REQUESTS,
        ACTIONS,
        PROJECTS,
        SECURITY,
        INSIGHTS,
        WIKI,
        SETTINGS,
        COUNT,
    };

    struct NavItem {
        gui::Id id = {};
        RepositorySection section = RepositorySection::CODE;
        StrRef icon = {};
        StrRef label = {};
        StrRef count = {};
        bool chevron = false;
    };

    struct RepoNode {
        char name[MAX_REPO_NAME] = {};
        char path[MAX_REPO_PATH] = {};
        char message[MAX_REPO_MESSAGE] = {};
        char age[MAX_REPO_AGE] = {};
        int32_t parent = -1;
        int32_t first_child = -1;
        int32_t next_sibling = -1;
        uint8_t indent = 0u;
        bool directory = false;
        bool open = false;
        bool has_commit = false;
    };

    struct RepoTree {
        char root[MAX_REPO_PATH] = {};
        RepoNode nodes[MAX_REPO_NODES] = {};
        int32_t root_child = -1;
        int32_t visible[MAX_REPO_NODES] = {};
        size_t node_count = 0u;
        size_t visible_count = 0u;
        bool loaded = false;
    };

    struct RepoCommit {
        char hash[24] = {};
        char author[MAX_REPO_NAME] = {};
        char age[MAX_REPO_AGE] = {};
        char subject[MAX_REPO_MESSAGE] = {};
    };

    struct RepoDetails {
        char name[MAX_REPO_NAME] = {};
        char description[MAX_REPO_DETAIL] = {};
        char branch[MAX_REPO_NAME] = {};
        char short_hash[24] = {};
        char author[MAX_REPO_NAME] = {};
        char author_initial[2] = {};
        char last_commit_age[MAX_REPO_AGE] = {};
        char last_commit_subject[MAX_REPO_MESSAGE] = {};
        char latest_release[MAX_REPO_NAME] = {};
        char package_size[MAX_REPO_NAME] = {};
        char license[MAX_REPO_NAME] = {};
        char activity[MAX_REPO_NAME] = {};
        char insertion_count[24] = {};
        char deletion_count[24] = {};
        char commit_count[24] = {};
        RepoCommit commits[MAX_REPO_COMMITS] = {};
        size_t shown_commit_count = 0u;
    };

    struct AppState {
        HWND hwnd = nullptr;
        RepoTree* tree = nullptr;
        RepoDetails* details = nullptr;
        bool running = true;
        bool redraw_pending = true;
        bool resize_pending = false;
        render::SizeU32 window_size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
        render::SizeU32 pending_size = {};
        gui::Frame last_frame = {};
        gui::Id mouse_hit_id = {};
        gui::InputState input = {};
        RepositorySection selected_section = RepositorySection::CODE;
        size_t selected_tab = 0u;
    };

    struct RepositoryTab {
        StrRef title = {};
        StrRef count = {};
        float width = 0.0f;
        float badge_width = 0.0f;
    };

    RepoTree global_repo_tree = {};
    RepoDetails global_repo_details = {};
    AppState* global_app_state = nullptr;
    uint64_t decorative_label_index = 0u;

    auto request_redraw(AppState* state) -> void {
        if (state != nullptr) {
            state->redraw_pending = true;
        }
    }

    [[nodiscard]] auto frame_ready(gui::Frame const& frame) -> bool {
        return frame.box_info_count() != 0u;
    }

    [[nodiscard]] auto frame_hit_id(gui::Frame const& frame, gui::Vec2 pos) -> gui::Id {
        gui::BoxInfo const* const box = frame.hit_test(pos);
        return box != nullptr ? box->id : gui::Id{};
    }

    [[nodiscard]] auto repo_tree_open_hash(RepoTree const& tree) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        for (size_t index = 0u; index < tree.node_count; ++index) {
            hash ^= tree.nodes[index].open ? index + 1u : 0u;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    constexpr RepositoryTab REPOSITORY_TABS[] = {
        {"Files", "", 70.0f, 0.0f},
        {"Commits", "", 146.0f, 52.0f},
        {"Branches", "23", 122.0f, 30.0f},
        {"Tags", "1,247", 116.0f, 46.0f},
        {"Releases", "412", 126.0f, 38.0f},
        {"Contributors", "3.2k", 158.0f, 42.0f},
    };

    constexpr StrRef SECTION_TITLES[] = {
        "Code",
        "Issues",
        "Pull requests",
        "Actions",
        "Projects",
        "Security",
        "Insights",
        "Wiki",
        "Settings",
    };
    static_assert(
        sizeof(SECTION_TITLES) / sizeof(SECTION_TITLES[0]) ==
        static_cast<size_t>(RepositorySection::COUNT)
    );

    constexpr StrRef SECTION_ROWS[] = {"Overview", "Open items", "Recent activity", "Timeline"};

    [[nodiscard]] auto indexed_id(StrRef scope, uint64_t index) -> gui::Id {
        return gui::id(gui::id(scope), index);
    }

    [[nodiscard]] auto indexed_local_id(StrRef scope, uint64_t index, StrRef local) -> gui::Id {
        return gui::id(indexed_id(scope, index), local);
    }

    [[nodiscard]] auto file_row_id(size_t index) -> gui::Id {
        gui::Id const tab_scope = indexed_id("tab_scroll", 0u);
        gui::Id const row_scope =
            gui::id(tab_scope, indexed_id("file_row", static_cast<uint64_t>(index)));
        return gui::id(row_scope, "row");
    }

    [[nodiscard]] auto tab_scroll_id(size_t index) -> gui::Id {
        return indexed_local_id("tab_scroll", static_cast<uint64_t>(index), "content");
    }

    [[nodiscard]] auto section_title(RepositorySection section) -> StrRef {
        return SECTION_TITLES[static_cast<size_t>(section)];
    }

    auto copy_cstr(char* dst, size_t capacity, char const* src) -> void {
        if (dst == nullptr || capacity == 0u) {
            return;
        }
        size_t index = 0u;
        if (src != nullptr) {
            while (index + 1u < capacity && src[index] != '\0') {
                dst[index] = src[index];
                index += 1u;
            }
        }
        dst[index] = '\0';
    }

    auto copy_slice(char* dst, size_t capacity, char const* src, size_t size) -> void {
        if (dst == nullptr || capacity == 0u) {
            return;
        }
        size_t const count = std::min(size, capacity - 1u);
        for (size_t index = 0u; index < count; ++index) {
            dst[index] = src[index];
        }
        dst[count] = '\0';
    }

    auto trim_line(char* text) -> void {
        if (text == nullptr) {
            return;
        }
        size_t size = std::strlen(text);
        while (size > 0u && (text[size - 1u] == '\n' || text[size - 1u] == '\r')) {
            size -= 1u;
            text[size] = '\0';
        }
    }

    [[nodiscard]] auto ascii_lower(char value) -> char {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    }

    [[nodiscard]] auto compare_ascii_ci(char const* lhs, char const* rhs) -> int32_t {
        size_t index = 0u;
        for (;;) {
            char const left = ascii_lower(lhs[index]);
            char const right = ascii_lower(rhs[index]);
            if (left != right || left == '\0' || right == '\0') {
                return static_cast<int32_t>(static_cast<unsigned char>(left)) -
                       static_cast<int32_t>(static_cast<unsigned char>(right));
            }
            index += 1u;
        }
    }

    [[nodiscard]] auto compare_repo_nodes(RepoNode const& lhs, RepoNode const& rhs) -> int32_t {
        if (lhs.directory != rhs.directory) {
            return lhs.directory ? -1 : 1;
        }
        int32_t const name_compare = compare_ascii_ci(lhs.name, rhs.name);
        return name_compare != 0 ? name_compare : compare_ascii_ci(lhs.path, rhs.path);
    }

    [[nodiscard]] auto child_head(RepoTree& tree, int32_t parent) -> int32_t* {
        return parent >= 0 ? &tree.nodes[parent].first_child : &tree.root_child;
    }

    [[nodiscard]] auto first_child(RepoTree const& tree, int32_t parent) -> int32_t {
        return parent >= 0 ? tree.nodes[parent].first_child : tree.root_child;
    }

    [[nodiscard]] auto find_child(RepoTree const& tree, int32_t parent, char const* name)
        -> int32_t {
        for (int32_t child = first_child(tree, parent); child >= 0;
             child = tree.nodes[child].next_sibling) {
            if (std::strcmp(tree.nodes[child].name, name) == 0) {
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
        RepoTree& tree, int32_t parent, char const* name, char const* path, bool directory
    ) -> int32_t {
        if (tree.node_count >= MAX_REPO_NODES) {
            return -1;
        }

        int32_t const node_index = static_cast<int32_t>(tree.node_count);
        tree.node_count += 1u;

        RepoNode& node = tree.nodes[node_index];
        copy_cstr(node.name, sizeof(node.name), name);
        copy_cstr(node.path, sizeof(node.path), path);
        copy_cstr(node.message, sizeof(node.message), "Loading commit data");
        copy_cstr(node.age, sizeof(node.age), "");
        node.parent = parent;
        node.indent = parent >= 0 ? static_cast<uint8_t>(tree.nodes[parent].indent + 1u) : 0u;
        node.directory = directory;
        node.open = false;
        insert_child_sorted(tree, parent, node_index);
        return node_index;
    }

    auto repo_tree_add_path(RepoTree& tree, char const* path) -> void {
        size_t const path_size = std::strlen(path);
        int32_t parent = -1;
        size_t segment_start = 0u;
        for (size_t cursor = 0u; cursor <= path_size; ++cursor) {
            if (path[cursor] != '/' && path[cursor] != '\\' && path[cursor] != '\0') {
                continue;
            }
            if (cursor == segment_start) {
                segment_start = cursor + 1u;
                continue;
            }

            char name[MAX_REPO_NAME] = {};
            copy_slice(name, sizeof(name), path + segment_start, cursor - segment_start);
            bool const directory = path[cursor] != '\0';
            int32_t child = find_child(tree, parent, name);
            if (child < 0) {
                char node_path[MAX_REPO_PATH] = {};
                copy_slice(node_path, sizeof(node_path), path, cursor);
                child = create_repo_node(tree, parent, name, node_path, directory);
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

    [[nodiscard]] auto read_first_command_line(char const* command, char* out, size_t capacity)
        -> bool {
        if (out == nullptr || capacity == 0u) {
            return false;
        }
        out[0] = '\0';
        FILE* pipe = _popen(command, "r");
        if (pipe == nullptr) {
            return false;
        }
        bool const read = std::fgets(out, static_cast<int>(capacity), pipe) != nullptr;
        _pclose(pipe);
        if (read) {
            trim_line(out);
        }
        return read && out[0] != '\0';
    }

    auto repo_full_path(RepoTree const& tree, char const* relative_path, char* out, size_t capacity)
        -> void {
        fmt::snprintf(out, capacity, "%s/%s", tree.root, relative_path);
    }

    [[nodiscard]] auto repo_file_size(RepoTree const& tree, char const* relative_path) -> uint64_t {
        char path[MAX_REPO_PATH * 2u] = {};
        repo_full_path(tree, relative_path, path, sizeof(path));

        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
            return 0u;
        }

        ULARGE_INTEGER size = {};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        return size.QuadPart;
    }

    auto format_byte_size(uint64_t bytes, char* out, size_t capacity) -> void {
        char const* suffix = "B";
        double value = static_cast<double>(bytes);
        if (bytes >= 1024ull * 1024ull) {
            value /= 1024.0 * 1024.0;
            suffix = "MB";
        } else if (bytes >= 1024ull) {
            value /= 1024.0;
            suffix = "KB";
        }
        fmt::snprintf(out, capacity, "%.1f %s", value, suffix);
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

    auto repo_root_name(RepoTree const& tree, char* out, size_t capacity) -> void {
        char const* name = tree.root;
        for (char const* cursor = tree.root; *cursor != '\0'; ++cursor) {
            if (*cursor == '/' || *cursor == '\\') {
                name = cursor + 1;
            }
        }
        copy_cstr(out, capacity, name);
    }

    auto load_readme_description(RepoTree const& tree, RepoDetails& details) -> void {
        char path[MAX_REPO_PATH * 2u] = {};
        repo_full_path(tree, "README.md", path, sizeof(path));
        FILE* file = nullptr;
        fopen_s(&file, path, "r");
        if (file == nullptr) {
            copy_cstr(details.description, sizeof(details.description), "No README summary found.");
            return;
        }

        char line[512] = {};
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            trim_line(line);
            if (line[0] == '\0' || line[0] == '#') {
                continue;
            }
            copy_cstr(details.description, sizeof(details.description), line);
            break;
        }
        std::fclose(file);

        if (details.description[0] == '\0') {
            copy_cstr(details.description, sizeof(details.description), "No README summary found.");
        }
    }

    auto load_license_name(RepoTree const& tree, RepoDetails& details) -> void {
        char const* names[] = {"LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING"};
        for (char const* name : names) {
            char path[MAX_REPO_PATH * 2u] = {};
            repo_full_path(tree, name, path, sizeof(path));
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                copy_cstr(details.license, sizeof(details.license), name);
                return;
            }
        }
        copy_cstr(details.license, sizeof(details.license), "Unspecified");
    }

    auto load_latest_commit_details(RepoTree const& tree, RepoDetails& details) -> void {
        char command[1024] = {};
        fmt::snprintf(
            command,
            "git -C \"%s\" log -1 --format=\"%%an%%x09%%ar%%x09%%h%%x09%%s\" 2>nul",
            tree.root
        );

        char line[512] = {};
        if (!read_first_command_line(command, line, sizeof(line))) {
            return;
        }

        char* age = std::strchr(line, '\t');
        if (age == nullptr) {
            return;
        }
        *age = '\0';
        age += 1;

        char* hash = std::strchr(age, '\t');
        if (hash == nullptr) {
            return;
        }
        *hash = '\0';
        hash += 1;

        char* subject = std::strchr(hash, '\t');
        if (subject == nullptr) {
            return;
        }
        *subject = '\0';
        subject += 1;

        copy_cstr(details.author, sizeof(details.author), line);
        details.author_initial[0] = line[0] != '\0' ? line[0] : '?';
        details.author_initial[1] = '\0';
        copy_cstr(details.last_commit_age, sizeof(details.last_commit_age), age);
        copy_cstr(details.short_hash, sizeof(details.short_hash), hash);
        copy_cstr(details.last_commit_subject, sizeof(details.last_commit_subject), subject);
    }

    auto load_commit_count(RepoTree const& tree, RepoDetails& details) -> void {
        char command[1024] = {};
        fmt::snprintf(command, "git -C \"%s\" rev-list --count HEAD 2>nul", tree.root);
        if (!read_first_command_line(command, details.commit_count, sizeof(details.commit_count))) {
            copy_cstr(details.commit_count, sizeof(details.commit_count), "0");
        }
    }

    auto load_recent_commits(RepoTree const& tree, RepoDetails& details) -> void {
        char command[1024] = {};
        fmt::snprintf(
            command,
            "git -C \"%s\" log --max-count=%llu --format=\"%%h%%x09%%an%%x09%%ar%%x09%%s\" 2>nul",
            tree.root,
            static_cast<unsigned long long>(MAX_REPO_COMMITS)
        );

        FILE* pipe = _popen(command, "r");
        if (pipe == nullptr) {
            return;
        }

        char line[512] = {};
        while (details.shown_commit_count < MAX_REPO_COMMITS &&
               std::fgets(line, sizeof(line), pipe) != nullptr) {
            trim_line(line);
            char* author = std::strchr(line, '\t');
            if (author == nullptr) {
                continue;
            }
            *author = '\0';
            author += 1;

            char* age = std::strchr(author, '\t');
            if (age == nullptr) {
                continue;
            }
            *age = '\0';
            age += 1;

            char* subject = std::strchr(age, '\t');
            if (subject == nullptr) {
                continue;
            }
            *subject = '\0';
            subject += 1;

            RepoCommit& commit = details.commits[details.shown_commit_count];
            copy_cstr(commit.hash, sizeof(commit.hash), line);
            copy_cstr(commit.author, sizeof(commit.author), author);
            copy_cstr(commit.age, sizeof(commit.age), age);
            copy_cstr(commit.subject, sizeof(commit.subject), subject);
            details.shown_commit_count += 1u;
        }
        _pclose(pipe);
    }

    auto load_head_stats(RepoTree const& tree, RepoDetails& details) -> void {
        char command[1024] = {};
        fmt::snprintf(
            command, "git -C \"%s\" show --numstat --format= --no-renames HEAD 2>nul", tree.root
        );

        uint64_t insertions = 0u;
        uint64_t deletions = 0u;
        FILE* pipe = _popen(command, "r");
        if (pipe != nullptr) {
            char line[512] = {};
            while (std::fgets(line, sizeof(line), pipe) != nullptr) {
                char* end = nullptr;
                unsigned long const added = std::strtoul(line, &end, 10);
                if (end == line || *end != '\t') {
                    continue;
                }
                char* deleted_text = end + 1;
                unsigned long const deleted = std::strtoul(deleted_text, &end, 10);
                if (end == deleted_text) {
                    continue;
                }
                insertions += added;
                deletions += deleted;
            }
            _pclose(pipe);
        }

        fmt::snprintf(
            details.insertion_count, "+%llu", static_cast<unsigned long long>(insertions)
        );
        fmt::snprintf(details.deletion_count, "-%llu", static_cast<unsigned long long>(deletions));
    }

    auto load_repo_details(RepoTree const& tree, RepoDetails& details) -> void {
        details = {};
        repo_root_name(tree, details.name, sizeof(details.name));
        load_readme_description(tree, details);

        char command[1024] = {};
        fmt::snprintf(command, "git -C \"%s\" branch --show-current 2>nul", tree.root);
        if (!read_first_command_line(command, details.branch, sizeof(details.branch))) {
            copy_cstr(details.branch, sizeof(details.branch), "HEAD");
        }

        load_latest_commit_details(tree, details);
        load_commit_count(tree, details);
        load_recent_commits(tree, details);

        fmt::snprintf(command, "git -C \"%s\" describe --tags --abbrev=0 2>nul", tree.root);
        if (!read_first_command_line(
                command, details.latest_release, sizeof(details.latest_release)
            )) {
            copy_cstr(details.latest_release, sizeof(details.latest_release), "No tags");
        }

        format_byte_size(
            repo_package_bytes(tree), details.package_size, sizeof(details.package_size)
        );
        load_license_name(tree, details);

        char count_line[32] = {};
        fmt::snprintf(
            command, "git -C \"%s\" rev-list --count --since=\"30 days ago\" HEAD 2>nul", tree.root
        );
        if (read_first_command_line(command, count_line, sizeof(count_line))) {
            fmt::snprintf(details.activity, "%s commits / 30d", count_line);
        } else {
            copy_cstr(details.activity, sizeof(details.activity), "Unknown");
        }

        load_head_stats(tree, details);
        if (details.last_commit_age[0] == '\0') {
            copy_cstr(details.last_commit_age, sizeof(details.last_commit_age), "No commits");
        }
    }

    [[nodiscard]] auto find_node_by_path(RepoTree const& tree, char const* path) -> int32_t {
        for (size_t index = 0u; index < tree.node_count; ++index) {
            if (std::strcmp(tree.nodes[index].path, path) == 0) {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    auto set_commit_if_empty(RepoNode& node, char const* age, char const* message) -> void {
        if (node.has_commit) {
            return;
        }
        copy_cstr(node.age, sizeof(node.age), age);
        copy_cstr(node.message, sizeof(node.message), message);
        node.has_commit = true;
    }

    auto set_node_and_parent_commits(
        RepoTree& tree, int32_t node_index, char const* age, char const* message
    ) -> void {
        for (int32_t index = node_index; index >= 0; index = tree.nodes[index].parent) {
            set_commit_if_empty(tree.nodes[index], age, message);
        }
    }

    auto load_repo_commits(RepoTree& tree) -> void {
        char command[1024] = {};
        fmt::snprintf(
            command,
            "git -C \"%s\" log --name-only --format=\"commit%%x09%%ar%%x09%%s\" -- 2>nul",
            tree.root
        );

        FILE* pipe = _popen(command, "r");
        if (pipe == nullptr) {
            return;
        }

        char age[MAX_REPO_AGE] = {};
        char message[MAX_REPO_MESSAGE] = {};
        char line[512] = {};
        while (std::fgets(line, sizeof(line), pipe) != nullptr) {
            trim_line(line);
            if (line[0] == '\0') {
                continue;
            }

            if (std::strncmp(line, "commit\t", 7u) == 0) {
                char* const tab = std::strchr(line + 7, '\t');
                if (tab != nullptr) {
                    *tab = '\0';
                    copy_cstr(age, sizeof(age), line + 7);
                    copy_cstr(message, sizeof(message), tab + 1);
                }
                continue;
            }

            int32_t const node_index = find_node_by_path(tree, line);
            if (node_index >= 0 && age[0] != '\0') {
                set_node_and_parent_commits(tree, node_index, age, message);
            }
        }
        _pclose(pipe);

        for (size_t index = 0u; index < tree.node_count; ++index) {
            RepoNode& node = tree.nodes[index];
            if (!node.has_commit) {
                copy_cstr(
                    node.message,
                    sizeof(node.message),
                    node.directory ? "No committed files" : "Untracked"
                );
                copy_cstr(node.age, sizeof(node.age), "not committed");
            }
        }
    }

    auto load_repo_tree(RepoTree& tree) -> void {
        tree = {};

        char root[MAX_REPO_PATH] = {};
        if (!read_first_command_line("git rev-parse --show-toplevel 2>nul", root, sizeof(root))) {
            return;
        }
        copy_cstr(tree.root, sizeof(tree.root), root);

        char command[1024] = {};
        fmt::snprintf(
            command, "git -C \"%s\" ls-files --cached --others --exclude-standard 2>nul", tree.root
        );

        FILE* pipe = _popen(command, "r");
        if (pipe == nullptr) {
            return;
        }
        char path[MAX_REPO_PATH] = {};
        while (std::fgets(path, sizeof(path), pipe) != nullptr) {
            trim_line(path);
            if (path[0] != '\0') {
                repo_tree_add_path(tree, path);
            }
        }
        _pclose(pipe);

        load_repo_commits(tree);
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

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto loword_i32(LPARAM value) -> int32_t {
        return static_cast<int32_t>(static_cast<int16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_i32(LPARAM value) -> int32_t {
        return static_cast<int32_t>(static_cast<int16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto to_draw(gui::Color color) -> draw::Color {
        return {color.r, color.g, color.b, color.a};
    }

    [[nodiscard]] auto to_draw(gui::Rect rect) -> draw::Rect {
        return {{rect.min.x, rect.min.y}, {rect.max.x, rect.max.y}};
    }

    [[nodiscard]] auto rect_intersects(gui::Rect lhs, gui::Rect rhs) -> bool {
        return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x && lhs.min.y < rhs.max.y &&
               lhs.max.y > rhs.min.y;
    }

    [[nodiscard]] auto decorative_label_id() -> gui::Id {
        gui::Id const result = indexed_id("decorative_label", decorative_label_index);
        decorative_label_index += 1u;
        return result;
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto repo_theme(font_cache::Font font, RepositorySpec const& spec)
        -> gui::ThemeDesc {
        gui::ThemeDesc theme = gui::default_theme();
        theme.tokens = {
            .canvas = spec.shell,
            .panel = spec.panel,
            .control = spec.control,
            .control_hovered = spec.control_hovered,
            .control_active = spec.selected,
            .accent = spec.blue,
            .danger = spec.red,
            .text = spec.text,
            .text_muted = spec.muted,
            .border = spec.border,
            .disabled_text = spec.faint,
            .radius_sm = 5.0f,
            .radius_md = 7.0f,
            .border_thickness = 1.0f,
        };

        theme.root = {.foreground = spec.text, .font = font, .font_size = 13.0f};
        gui::theme_role(theme, gui::StyleRole::CANVAS).normal = {
            .background = spec.shell,
            .foreground = spec.text,
        };
        gui::theme_role(theme, gui::StyleRole::PANEL).normal = {
            .background = spec.panel,
            .foreground = spec.text,
            .border = spec.border,
            .border_thickness = 1.0f,
            .radius = 8.0f,
        };
        gui::theme_role(theme, gui::StyleRole::TEXT).normal = {.foreground = spec.text};
        gui::theme_role(theme, gui::StyleRole::TEXT_MUTED).normal = {.foreground = spec.muted};
        gui::theme_role(theme, gui::StyleRole::CONTROL) = {
            .normal =
                {
                    .background = spec.control,
                    .foreground = spec.text,
                    .border = spec.border,
                    .border_thickness = 1.0f,
                    .radius = 6.0f,
                },
            .hovered = {.background = spec.control_hovered},
            .active = {.background = spec.selected},
        };
        gui::theme_role(theme, gui::StyleRole::ACCENT).normal = {
            .background = spec.selected,
            .foreground = spec.text,
            .border = spec.strong_border,
            .border_thickness = 1.0f,
            .radius = 6.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::ROOT).role = gui::StyleRole::CANVAS;
        gui::theme_kind(theme, gui::BoxKind::LABEL).role = gui::StyleRole::TEXT;
        gui::theme_kind(theme, gui::BoxKind::ROW).style.hovered.background = spec.control_hovered;
        gui::theme_kind(theme, gui::BoxKind::ROW).style.active.background = spec.selected;
        gui::theme_kind(theme, gui::BoxKind::BUTTON).role = gui::StyleRole::CONTROL;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).role = gui::StyleRole::CONTROL;
        return theme;
    }

    [[nodiscard]] auto separator_y(RepositorySpec const& spec) -> gui::BoxDesc {
        return {
            .layout = {.width = gui::fill(), .height = gui::px(1.0f)},
            .style = {.background = spec.border},
        };
    }

    [[nodiscard]] auto separator_x(RepositorySpec const& spec) -> gui::BoxDesc {
        return {
            .layout = {.width = gui::px(1.0f), .height = gui::fill()},
            .style = {.background = spec.border},
        };
    }

    auto label(gui::Frame& ui, StrRef text, float size, gui::StyleRole role) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::text(), .height = gui::fill()},
                .style = {.role = role, .font_size = size},
            }
        );
    }

    auto label_fill(gui::Frame& ui, StrRef text, float size, gui::StyleRole role) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::fill(), .height = gui::fill()},
                .style = {.role = role, .font_size = size},
            }
        );
    }

    auto label_color(gui::Frame& ui, StrRef text, float size, gui::Color color) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::text(), .height = gui::fill()},
                .style = {.role = gui::StyleRole::NONE, .foreground = color, .font_size = size},
            }
        );
    }

    auto icon_label(
        gui::Frame& ui,
        font_cache::Font icon_font,
        StrRef icon,
        gui::Color color,
        float size,
        float width
    ) -> void {
        ui.label(
            decorative_label_id(),
            icon,
            {
                .layout = {.width = gui::px(width), .height = gui::px(20.0f)},
                .style = {
                    .role = gui::StyleRole::NONE,
                    .foreground = color,
                    .font = icon_font,
                    .font_size = size,
                },
            }
        );
    }

    auto draw_nav_item(
        gui::Frame& ui,
        RepositorySpec const& spec,
        font_cache::Font icon_font,
        NavItem item,
        RepositorySection& selected_section
    ) -> void {
        bool const selected = selected_section == item.section;
        gui::Color const color = selected ? spec.text : spec.muted;
        gui::BoxDesc desc = {
            .layout =
                {
                    .width = gui::fill(),
                    .height = gui::px(36.0f),
                    .margin = gui::insets(0.0f, 8.0f),
                    .padding = gui::insets(0.0f, 12.0f),
                    .gap = 12.0f,
                    .align_y = gui::Align::CENTER,
                },
            .style = {
                .background = selected ? spec.selected : gui::unset_color(),
                .foreground = color,
                .radius = 6.0f,
            },
        };
        if (auto row = ui.row(item.id, desc)) {
            if (row.signal().activated) {
                selected_section = item.section;
            }
            icon_label(ui, icon_font, item.icon, color, 16.0f, 20.0f);
            label_fill(
                ui, item.label, 13.5f, selected ? gui::StyleRole::TEXT : gui::StyleRole::TEXT_MUTED
            );
            if (!item.count.empty()) {
                label(ui, item.count, 12.5f, gui::StyleRole::TEXT_MUTED);
            } else if (item.chevron) {
                icon_label(ui, icon_font, ICON_CHEVRON_RIGHT, spec.muted, 16.0f, 14.0f);
            }
        }
    }

    auto draw_sidebar(
        gui::Frame& ui,
        RepositorySpec const& spec,
        font_cache::Font icon_font,
        RepositorySection& selected_section
    ) -> void {
        if (auto sidebar = ui.column(
                gui::id("sidebar"),
                {
                    .layout = {.width = gui::px(SIDEBAR_WIDTH), .height = gui::fill()},
                    .style = {.background = spec.shell},
                }
            )) {
            if (auto header = ui.row(
                    gui::id("workspace_header"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 16.0f),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                ui.spacer({.layout = {.width = gui::px(26.0f), .height = gui::px(1.0f)}});
                label(ui, "Acme", 14.0f, gui::StyleRole::TEXT);
                ui.label(
                    "Pro",
                    {
                        .layout =
                            {
                                .width = gui::px(42.0f),
                                .height = gui::px(20.0f),
                                .padding = gui::insets(0.0f, 8.0f),
                            },
                        .style = {
                            .background = spec.blue,
                            .foreground = gui::rgb(82, 175, 255),
                            .radius = 10.0f,
                            .font_size = 12.0f,
                        },
                    }
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, "v", 16.0f, gui::StyleRole::TEXT_MUTED);
            }

            ui.spacer(separator_y(spec));
            if (auto search = ui.row(
                    gui::id("search_box"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(32.0f),
                                .margin = gui::insets(12.0f, 10.0f),
                                .padding = gui::insets(0.0f, 12.0f, 0.0f, 42.0f),
                                .align_y = gui::Align::CENTER,
                            },
                        .style = {
                            .background = spec.control,
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                        },
                    }
                )) {
                label_fill(ui, "Find...", 13.0f, gui::StyleRole::TEXT_MUTED);
                ui.label(
                    "F",
                    {
                        .layout = {.width = gui::px(22.0f), .height = gui::px(22.0f)},
                        .style = {
                            .background = gui::rgb(13, 13, 13),
                            .foreground = spec.faint,
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 5.0f,
                            .font_size = 11.0f,
                        },
                    }
                );
            }

            NavItem const items[] = {
                {
                    gui::id("nav_code"),
                    RepositorySection::CODE,
                    ICON_CODE,
                    "Code",
                    "",
                },
                {
                    gui::id("nav_issues"),
                    RepositorySection::ISSUES,
                    ICON_ISSUES,
                    "Issues",
                    "1.2k",
                },
                {
                    gui::id("nav_prs"),
                    RepositorySection::PULL_REQUESTS,
                    ICON_PULL_REQUESTS,
                    "Pull requests",
                    "247",
                },
                {
                    gui::id("nav_actions"),
                    RepositorySection::ACTIONS,
                    ICON_ACTIONS,
                    "Actions",
                    "",
                    true,
                },
                {
                    gui::id("nav_projects"),
                    RepositorySection::PROJECTS,
                    ICON_PROJECTS,
                    "Projects",
                    "8",
                },
                {
                    gui::id("nav_security"),
                    RepositorySection::SECURITY,
                    ICON_SECURITY,
                    "Security",
                    "",
                    true,
                },
                {
                    gui::id("nav_insights"),
                    RepositorySection::INSIGHTS,
                    ICON_INSIGHTS,
                    "Insights",
                    "",
                    true,
                },
                {gui::id("nav_wiki"), RepositorySection::WIKI, ICON_WIKI, "Wiki", ""},
                {
                    gui::id("nav_settings"),
                    RepositorySection::SETTINGS,
                    ICON_SETTINGS,
                    "Settings",
                    "",
                },
            };
            for (size_t index = 0u; index < sizeof(items) / sizeof(items[0]); ++index) {
                draw_nav_item(ui, spec, icon_font, items[index], selected_section);
            }

            ui.spacer({.layout = {.width = gui::fill(), .height = gui::fill()}});
            ui.spacer(separator_y(spec));
            if (auto user = ui.row(
                    gui::id("user_switcher"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(62.0f),
                            .padding = gui::insets(0.0f, 16.0f),
                            .gap = 10.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                ui.spacer({.layout = {.width = gui::px(26.0f), .height = gui::px(1.0f)}});
                if (auto names = ui.column(
                        gui::id("user_name_block"),
                        {
                            .layout = {
                                .width = gui::children(),
                                .height = gui::children(),
                                .gap = 1.0f,
                            },
                        }
                    )) {
                    ui.label(
                        "Evil Rabbit", {.layout = {.width = gui::text(), .height = gui::px(18.0f)}}
                    );
                    ui.label(
                        "Hobby",
                        {
                            .layout = {.width = gui::text(), .height = gui::px(16.0f)},
                            .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                        }
                    );
                }
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, "v", 16.0f, gui::StyleRole::TEXT_MUTED);
            }
        }
    }

    auto draw_tab(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepositoryTab tab_data,
        size_t tab_index,
        size_t& selected_tab
    ) -> void {
        bool const selected = selected_tab == tab_index;
        auto tab_scope =
            ui.id_scope(indexed_id("repository_tab", static_cast<uint64_t>(tab_index)));
        BASE_UNUSED(tab_scope);
        if (auto tab = ui.column(
                gui::id("tab"),
                {
                    .layout =
                        {
                            .width = gui::px(tab_data.width),
                            .height = gui::fill(),
                            .gap = 0.0f,
                        },
                    .style = {.foreground = selected ? spec.text : spec.muted},
                }
            )) {
            if (tab.signal().activated) {
                selected_tab = tab_index;
            }
            if (auto row = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::fill(),
                        .gap = 8.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label(
                    ui,
                    tab_data.title,
                    14.0f,
                    selected ? gui::StyleRole::TEXT : gui::StyleRole::TEXT_MUTED
                );
                if (!tab_data.count.empty()) {
                    if (auto badge = ui.row(
                            decorative_label_id(),
                            {
                                .layout =
                                    {
                                        .width = gui::px(tab_data.badge_width),
                                        .height = gui::px(18.0f),
                                        .align_x = gui::Align::CENTER,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style = {
                                    .background = spec.control,
                                    .border = spec.border,
                                    .border_thickness = 1.0f,
                                    .radius = 5.0f,
                                },
                            }
                        )) {
                        ui.label(
                            decorative_label_id(),
                            tab_data.count,
                            {
                                .layout =
                                    {
                                        .width = gui::text(),
                                        .height = gui::fill(),
                                        .margin = gui::insets(0.0f, 2.0f, 0.0f, 0.0f),
                                    },
                                .style = {
                                    .role = gui::StyleRole::NONE,
                                    .foreground = selected ? spec.text : spec.muted,
                                    .font_size = 11.0f,
                                },
                            }
                        );
                    }
                }
            }
            ui.spacer({
                .layout = {.width = gui::fill(), .height = gui::px(1.0f)},
                .style = {.background = selected ? spec.text : gui::rgba(0, 0, 0, 0)},
            });
        }
    }

    auto draw_top_bar(
        gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details, size_t& selected_tab
    ) -> void {
        if (auto top = ui.row(
                gui::id("top_bar"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 20.0f),
                            .gap = 16.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            if (auto env = ui.row(
                    gui::id("environment"),
                    {
                        .layout = {
                            .width = gui::children(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 0.0f, 0.0f, 18.0f),
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                label(ui, "Production", 13.0f, gui::StyleRole::TEXT_MUTED);
            }
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            ui.label(
                details.branch,
                {
                    .layout = {.width = gui::text(), .height = gui::fill()},
                    .style = {.font_size = 13.5f},
                }
            );
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.short_hash, 13.0f, gui::StyleRole::TEXT_MUTED);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
            label(ui, "Local", 12.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.activity, 12.5f, gui::StyleRole::TEXT_MUTED);
        }
        ui.spacer(separator_y(spec));

        if (auto tabs = ui.row(
                gui::id("tabs"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(46.0f),
                        .padding = gui::insets(0.0f, 24.0f),
                        .gap = 28.0f,
                    },
                }
            )) {
            for (size_t index = 0u; index < sizeof(REPOSITORY_TABS) / sizeof(REPOSITORY_TABS[0]);
                 ++index) {
                RepositoryTab tab = REPOSITORY_TABS[index];
                if (index == 1u) {
                    tab.count = StrRef(details.commit_count);
                    tab.badge_width = std::max(
                        30.0f, 18.0f + 7.0f * static_cast<float>(std::strlen(details.commit_count))
                    );
                    tab.width = 94.0f + tab.badge_width;
                }
                draw_tab(ui, spec, tab, index, selected_tab);
            }
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_stat_cell(
        gui::Frame& ui, RepositorySpec const& spec, StrRef label_text, StrRef value, bool last
    ) -> void {
        if (auto cell = ui.column({
                .layout = {
                    .width = gui::fill(),
                    .height = gui::fill(),
                    .padding = gui::insets(15.0f, 20.0f),
                    .gap = 8.0f,
                },
            })) {
            label(ui, label_text, 12.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, value, 14.0f, gui::StyleRole::TEXT);
        }
        if (!last) {
            ui.spacer(separator_x(spec));
        }
    }

    auto draw_repo_summary(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto summary = ui.column(
                gui::id("summary"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children(), .gap = 6.0f},
                }
            )) {
            if (auto crumb = ui.row(
                    {.layout = {.width = gui::fill(), .height = gui::px(20.0f), .gap = 8.0f}}
                )) {
                label(ui, "Repository", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "/", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "Files", 12.5f, gui::StyleRole::TEXT);
            }
            ui.label(
                details.name,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(28.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.label(
                details.description,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(24.0f)},
                    .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.5f},
                }
            );
        }

        if (auto stats = ui.row(
                gui::id("stats"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(76.0f),
                            .margin = gui::insets(16.0f, 0.0f, 0.0f, 0.0f),
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            draw_stat_cell(ui, spec, "Current Branch", details.branch, false);
            draw_stat_cell(ui, spec, "Last Commit", details.last_commit_age, false);
            draw_stat_cell(ui, spec, "Latest Tag", details.latest_release, false);
            draw_stat_cell(ui, spec, "Repository Size", details.package_size, false);
            draw_stat_cell(ui, spec, "License", details.license, false);
            draw_stat_cell(ui, spec, "Activity", details.activity, true);
        }
    }

    auto draw_latest_commit(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto panel = ui.column(
                gui::id("latest_commit"),
                {
                    .layout = {.width = gui::fill(), .height = gui::px(142.0f)},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(40.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label_fill(ui, "Latest commit", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "View history ->", 12.5f, gui::StyleRole::TEXT_MUTED);
            }
            ui.spacer(separator_y(spec));
            if (auto body = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::fill(),
                        .padding = gui::insets(18.0f, 20.0f),
                        .gap = 16.0f,
                    },
                })) {
                ui.label(
                    details.author_initial,
                    {
                        .layout = {.width = gui::px(32.0f), .height = gui::px(32.0f)},
                        .style = {
                            .background = gui::rgb(12, 12, 12),
                            .foreground = spec.text,
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 16.0f,
                            .font_size = 12.0f,
                        },
                    }
                );
                if (auto commit_text = ui.column({
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .gap = 8.0f,
                        },
                    })) {
                    if (auto byline = ui.row({
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(20.0f),
                                .gap = 8.0f,
                            },
                        })) {
                        label(ui, details.author, 13.0f, gui::StyleRole::TEXT);
                        label(ui, "authored", 12.5f, gui::StyleRole::TEXT_MUTED);
                        label(ui, details.last_commit_age, 12.5f, gui::StyleRole::TEXT_MUTED);
                    }
                    ui.label(
                        details.last_commit_subject,
                        {.layout = {.width = gui::fill(), .height = gui::px(22.0f)}}
                    );
                    if (auto meta = ui.row({
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(20.0f),
                                .gap = 12.0f,
                            },
                        })) {
                        label(ui, details.short_hash, 12.0f, gui::StyleRole::TEXT_MUTED);
                        label(ui, details.branch, 12.0f, gui::StyleRole::TEXT_MUTED);
                        label_color(ui, details.insertion_count, 12.0f, spec.green);
                        label_color(ui, details.deletion_count, 12.0f, spec.red);
                    }
                }
            }
        }
    }

    auto draw_file_row(gui::Frame& ui, RepositorySpec const& spec, RepoTree& tree, size_t index)
        -> void {
        int32_t const node_index = tree.visible[index];
        RepoNode& node = tree.nodes[node_index];
        auto row_scope = ui.id_scope(indexed_id("file_row", static_cast<uint64_t>(node_index)));
        BASE_UNUSED(row_scope);
        if (auto item = ui.row(
                gui::id("row"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(44.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .gap = 0.0f,
                        .align_y = gui::Align::CENTER,
                    },
                }
            )) {
            if (item.signal().activated && node.directory) {
                node.open = !node.open;
            }
            ui.label(
                node.name,
                {
                    .layout =
                        {
                            .width = gui::px(340.0f),
                            .height = gui::fill(),
                            .padding = gui::insets(
                                0.0f, 0.0f, 0.0f, 56.0f + 18.0f * static_cast<float>(node.indent)
                            ),
                        },
                    .style = {.font_size = 13.5f},
                }
            );
            ui.label(
                node.message,
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                }
            );
            if (auto age = ui.row({
                    .layout = {
                        .width = gui::px(132.0f),
                        .height = gui::fill(),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, node.age, 12.5f, gui::StyleRole::TEXT_MUTED);
            }
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_file_table(gui::Frame& ui, RepositorySpec const& spec, RepoTree& tree) -> void {
        rebuild_visible_tree(tree);
        if (auto table = ui.column(
                gui::id("file_table"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto toolbar = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(50.0f),
                        .padding = gui::insets(10.0f, 14.0f),
                        .gap = 8.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                if (auto search = ui.row(
                        gui::id("go_to_file"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(0.0f, 10.0f, 0.0f, 32.0f),
                                    .align_y = gui::Align::CENTER,
                                },
                            .style = {
                                .background = spec.control,
                                .border = spec.border,
                                .border_thickness = 1.0f,
                                .radius = 6.0f,
                            },
                        }
                    )) {
                    label_fill(ui, "Go to file", 13.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, "T", 11.0f, gui::StyleRole::TEXT_MUTED);
                }
                ui.button(
                    "+ Add file",
                    {
                        .layout =
                            {
                                .width = gui::px(108.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {.font_size = 13.0f},
                    }
                );
                ui.button(
                    "Clone",
                    {
                        .layout =
                            {
                                .width = gui::px(100.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {
                            .role = gui::StyleRole::NONE,
                            .background = gui::rgb(239, 239, 239),
                            .foreground = gui::rgb(24, 24, 24),
                            .border = gui::rgb(255, 255, 255),
                            .border_thickness = 1.0f,
                            .radius = 7.0f,
                            .font_size = 13.0f,
                        },
                    }
                );
            }
            ui.spacer(separator_y(spec));
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(36.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                ui.label(
                    "NAME",
                    {
                        .layout = {.width = gui::px(340.0f), .height = gui::fill()},
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                    }
                );
                ui.label(
                    "LAST COMMIT MESSAGE",
                    {
                        .layout = {.width = gui::fill(), .height = gui::fill()},
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                    }
                );
                if (auto age =
                        ui.row({.layout = {.width = gui::px(132.0f), .height = gui::fill()}})) {
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                    label(ui, "LAST COMMIT", 11.0f, gui::StyleRole::TEXT_MUTED);
                }
            }
            ui.spacer(separator_y(spec));
            if (!tree.loaded) {
                ui.label(
                    "No git repository tree found",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                            },
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                    }
                );
            }
            for (size_t index = 0u; index < tree.visible_count; ++index) {
                draw_file_row(ui, spec, tree, index);
            }
        }
    }

    auto draw_commits_tab(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto panel = ui.column(
                gui::id("commits_tab_content"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(58.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .gap = 10.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label_fill(ui, "Commits", 23.0f, gui::StyleRole::TEXT);
                label(ui, details.commit_count, 13.0f, gui::StyleRole::TEXT_MUTED);
            }
            ui.spacer(separator_y(spec));
            if (details.shown_commit_count == 0u) {
                ui.label(
                    "No commits found",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                            },
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                    }
                );
                return;
            }

            for (size_t index = 0u; index < details.shown_commit_count; ++index) {
                RepoCommit const& commit = details.commits[index];
                auto row_scope =
                    ui.id_scope(indexed_id("commit_row", static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(46.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, commit.subject, 13.5f, gui::StyleRole::TEXT);
                    label(ui, commit.hash, 12.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, commit.author, 12.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, commit.age, 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_secondary_tab_content(gui::Frame& ui, RepositorySpec const& spec, size_t selected_tab)
        -> void {
        RepositoryTab const tab = REPOSITORY_TABS[selected_tab];
        if (auto panel = ui.column(
                gui::id("secondary_tab_content"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(20.0f),
                            .gap = 16.0f,
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            ui.label(
                tab.title,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(30.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.spacer(separator_y(spec));
            for (size_t index = 0u; index < 18u; ++index) {
                auto row_scope =
                    ui.id_scope(indexed_id("secondary_row", static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(42.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, tab.title, 13.5f, gui::StyleRole::TEXT);
                    label(ui, "Updated 11 minutes ago", 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_section_top_bar(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepoDetails const& details,
        RepositorySection section
    ) -> void {
        StrRef const title = section_title(section);
        if (auto top = ui.row(
                gui::id("section_top_bar"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 20.0f),
                            .gap = 12.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            label(ui, details.name, 13.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, title, 13.5f, gui::StyleRole::TEXT);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
            label(ui, "Local", 12.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.activity, 12.5f, gui::StyleRole::TEXT_MUTED);
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_section_page(gui::Frame& ui, RepositorySpec const& spec, RepositorySection section)
        -> void {
        StrRef const title = section_title(section);
        if (auto panel = ui.column(
                gui::id("section_panel"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(20.0f),
                            .gap = 16.0f,
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto crumb = ui.row(
                    {.layout = {.width = gui::fill(), .height = gui::px(20.0f), .gap = 8.0f}}
                )) {
                label(ui, "Repository", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "/", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, title, 12.5f, gui::StyleRole::TEXT);
            }
            ui.label(
                title,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(30.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.spacer(separator_y(spec));
            for (size_t index = 0u; index < sizeof(SECTION_ROWS) / sizeof(SECTION_ROWS[0]);
                 ++index) {
                gui::Id const scope = indexed_id("section_row", static_cast<uint64_t>(section));
                auto row_scope = ui.id_scope(gui::id(scope, static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, SECTION_ROWS[index], 13.5f, gui::StyleRole::TEXT);
                    label(ui, "Updated now", 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_main_content(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepoDetails const& details,
        RepositorySection selected_section,
        size_t& selected_tab,
        RepoTree& tree
    ) -> void {
        if (auto main = ui.column(
                gui::id("main_content"),
                {
                    .layout = {.width = gui::fill(), .height = gui::fill()},
                    .style = {.background = spec.shell},
                }
            )) {
            if (selected_section == RepositorySection::CODE) {
                draw_top_bar(ui, spec, details, selected_tab);
                auto scroll_scope =
                    ui.id_scope(indexed_id("tab_scroll", static_cast<uint64_t>(selected_tab)));
                BASE_UNUSED(scroll_scope);
                if (auto content = ui.scroll_panel(
                        gui::id("content"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(24.0f),
                                    .gap = 20.0f,
                                },
                            .debug_name = "repository_tab_content",
                        }
                    )) {
                    if (selected_tab == 0u) {
                        draw_repo_summary(ui, spec, details);
                        draw_latest_commit(ui, spec, details);
                        draw_file_table(ui, spec, tree);
                    } else if (selected_tab == 1u) {
                        draw_commits_tab(ui, spec, details);
                    } else {
                        draw_secondary_tab_content(ui, spec, selected_tab);
                    }
                }
            } else {
                draw_section_top_bar(ui, spec, details, selected_section);
                auto scroll_scope = ui.id_scope(
                    indexed_id("section_scroll", static_cast<uint64_t>(selected_section))
                );
                BASE_UNUSED(scroll_scope);
                if (auto content = ui.scroll_panel(
                        gui::id("content"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(24.0f),
                                    .gap = 20.0f,
                                },
                            .debug_name = "repository_section_content",
                        }
                    )) {
                    draw_section_page(ui, spec, selected_section);
                }
            }
        }
    }

    auto draw_repository_ui(
        gui::Frame& ui,
        font_cache::Font icon_font,
        RepositorySection& selected_section,
        size_t& selected_tab,
        RepoTree& tree,
        RepoDetails const& details
    ) -> void {
        decorative_label_index = 0u;
        RepositorySpec const spec = {};
        if (auto shell = ui.row(
                gui::id("app_shell"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .clip = true,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            draw_sidebar(ui, spec, icon_font, selected_section);
            ui.spacer(separator_x(spec));
            draw_main_content(ui, spec, details, selected_section, selected_tab, tree);
        }
    }

    auto draw_magnifier(draw::Context context, draw::Vec2 center, draw::Color color) -> void {
        draw::draw_circle(context, center, 5.0f, color, 1.4f, 18);
        draw::draw_line(
            context,
            {center.x + 4.0f, center.y + 4.0f},
            {center.x + 8.0f, center.y + 8.0f},
            color,
            1.4f
        );
    }

    auto draw_folder(draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent)
        -> void {
        float const x = rect.min.x + 38.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f - 7.0f;
        draw::draw_line(context, {x, y + 4.0f}, {x + 5.0f, y + 4.0f}, color, 1.2f);
        draw::draw_line(context, {x + 5.0f, y + 4.0f}, {x + 7.5f, y + 6.0f}, color, 1.2f);
        draw::draw_rect(context, {{x, y + 6.0f}, {x + 16.0f, y + 16.0f}}, color, 1.2f, 2.0f);
    }

    auto draw_file_leaf(draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent)
        -> void {
        float const x = rect.min.x + 38.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f - 8.0f;
        draw::draw_rect(context, {{x + 2.0f, y}, {x + 14.0f, y + 16.0f}}, color, 1.1f, 2.0f);
        draw::draw_line(context, {x + 5.0f, y + 5.0f}, {x + 11.0f, y + 5.0f}, color, 1.0f);
        draw::draw_line(context, {x + 5.0f, y + 9.0f}, {x + 11.0f, y + 9.0f}, color, 1.0f);
    }

    auto draw_tree_caret(
        draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent, bool open
    ) -> void {
        float const x = rect.min.x + 22.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f;
        if (open) {
            draw::draw_triangle_filled(
                context, {x - 4.0f, y - 2.0f}, {x + 4.0f, y - 2.0f}, {x, y + 4.0f}, color
            );
        } else {
            draw::draw_triangle_filled(
                context, {x - 2.0f, y - 5.0f}, {x - 2.0f, y + 5.0f}, {x + 4.0f, y}, color
            );
        }
    }

    auto draw_logo(draw::Context context, gui::Rect rect, RepositorySpec const& spec) -> void {
        draw::Vec2 const center = {rect.min.x + 16.0f, (rect.min.y + rect.max.y) * 0.5f};
        draw::Color const color = to_draw(spec.text);
        draw::draw_circle(context, center, 13.0f, to_draw(spec.border), 1.0f, 24);
        draw::draw_triangle_filled(
            context,
            {center.x, center.y - 6.0f},
            {center.x - 6.0f, center.y + 5.0f},
            {center.x + 6.0f, center.y + 5.0f},
            color
        );
    }

    auto draw_user_mark(draw::Context context, gui::Rect rect, RepositorySpec const& spec) -> void {
        float const x = rect.min.x + 16.0f;
        float const y = (rect.min.y + rect.max.y) * 0.5f;
        draw::Color const color = to_draw(spec.text);
        draw::draw_triangle_filled(
            context, {x - 5.0f, y - 5.0f}, {x, y + 4.0f}, {x - 1.0f, y + 9.0f}, color
        );
        draw::draw_triangle_filled(
            context, {x + 5.0f, y - 5.0f}, {x, y + 4.0f}, {x + 1.0f, y + 9.0f}, color
        );
        draw::draw_circle_filled(context, {x, y + 7.0f}, 4.0f, color, 16);
    }

    auto draw_repository_icons(
        gui::Frame const& ui,
        draw::Context context,
        RepositorySection selected_section,
        size_t selected_tab,
        RepoTree const& tree
    ) -> void {
        RepositorySpec const spec = {};
        draw::Color const muted = to_draw(spec.muted);

        if (gui::BoxInfo const* box = ui.find_box(gui::id("workspace_header"))) {
            draw_logo(context, box->rect, spec);
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("user_switcher"))) {
            draw_user_mark(context, box->rect, spec);
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("search_box"))) {
            draw_magnifier(
                context,
                {box->rect.min.x + 22.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                muted
            );
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("go_to_file"))) {
            draw_magnifier(
                context,
                {box->rect.min.x + 18.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                muted
            );
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("environment"))) {
            draw::draw_circle_filled(
                context,
                {box->rect.min.x + 6.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                3.0f,
                to_draw(spec.green),
                12
            );
        }

        if (selected_section == RepositorySection::CODE && selected_tab == 0u) {
            gui::Rect clip_rect = {};
            bool clipped = false;
            if (gui::BoxInfo const* scroll = ui.find_box(tab_scroll_id(0u))) {
                clip_rect = scroll->rect;
                clipped = true;
                draw::push_clip_rect(context, to_draw(clip_rect));
            }
            for (size_t index = 0u; index < tree.visible_count; ++index) {
                int32_t const node_index = tree.visible[index];
                RepoNode const& node = tree.nodes[node_index];
                if (gui::BoxInfo const* box =
                        ui.find_box(file_row_id(static_cast<size_t>(node_index)))) {
                    if (clipped && !rect_intersects(box->rect, clip_rect)) {
                        continue;
                    }
                    if (node.directory) {
                        draw_tree_caret(context, box->rect, muted, node.indent, node.open);
                        draw_folder(context, box->rect, muted, node.indent);
                    } else {
                        draw_file_leaf(context, box->rect, muted, node.indent);
                    }
                }
            }
            if (ui.find_box(tab_scroll_id(0u)) != nullptr) {
                draw::pop_clip_rect(context);
            }
        }
    }

    [[nodiscard]] auto write_embedded_icon_font(Runtime* runtime) -> bool {
        char temp_dir[MAX_PATH] = {};
        DWORD const temp_dir_capacity = static_cast<DWORD>(sizeof(temp_dir));
        DWORD const temp_dir_size = GetTempPathA(temp_dir_capacity, temp_dir);
        if (temp_dir_size == 0u || temp_dir_size >= temp_dir_capacity) {
            return false;
        }

        char font_path[MAX_PATH] = {};
        if (GetTempFileNameA(temp_dir, "gfi", 0u, font_path) == 0u) {
            return false;
        }

        HANDLE const file = CreateFileA(
            font_path, GENERIC_WRITE, 0u, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr
        );
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileA(font_path);
            return false;
        }

        DWORD const font_size = static_cast<DWORD>(repository_ui_testbed_assets::codicon_ttf_size);
        DWORD bytes_written = 0u;
        BOOL const write_ok = WriteFile(
            file, repository_ui_testbed_assets::codicon_ttf, font_size, &bytes_written, nullptr
        );
        CloseHandle(file);

        if (write_ok == FALSE || bytes_written != font_size) {
            DeleteFileA(font_path);
            return false;
        }

        copy_cstr(runtime->icon_font_path, sizeof(runtime->icon_font_path), font_path);
        return true;
    }

    auto destroy_runtime(render::Context render_context, Runtime* runtime) -> void {
        if (runtime == nullptr) {
            return;
        }
        if (gui::context_valid(runtime->ui_context)) {
            gui::destroy_context(runtime->ui_context);
        }
        if (draw::context_valid(runtime->draw_context)) {
            draw::destroy_context(runtime->draw_context);
        }
        if (draw::renderer_valid(runtime->draw_renderer)) {
            draw::destroy_renderer(render_context, runtime->draw_renderer);
        }
        if (font_cache::cache_valid(runtime->cache)) {
            font_cache::destroy_cache(runtime->cache);
        }
        if (runtime->icon_font_path[0] != '\0') {
            DeleteFileA(runtime->icon_font_path);
            runtime->icon_font_path[0] = '\0';
        }
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
        runtime->icon_font = {};
    }

    [[nodiscard]] auto
    create_runtime(Arena& arena, render::Context render_context, Runtime* runtime) -> bool {
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, runtime->draw_renderer);
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->font);
        if (!write_embedded_icon_font(runtime)) {
            fmt::eprintf("failed to write embedded Codicons font\n");
            return false;
        }
        font_cache::open_font_file(runtime->cache, runtime->icon_font_path, runtime->icon_font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        RepositorySpec const spec = {};
        gui::ThemeDesc const theme = repo_theme(runtime->font, spec);
        gui::create_context(
            arena, {.initial_box_capacity = 2048u, .theme = &theme}, runtime->ui_context
        );
        return true;
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                    request_redraw(global_app_state);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
                gui::Id const hit_id = frame_hit_id(global_app_state->last_frame, pos);
                bool const needs_frame = global_app_state->redraw_pending ||
                                         !frame_ready(global_app_state->last_frame) ||
                                         global_app_state->input.mouse_down[0u] ||
                                         hit_id.value != global_app_state->mouse_hit_id.value;
                global_app_state->input.mouse_pos = pos;
                global_app_state->mouse_hit_id = hit_id;
                if (needs_frame) {
                    request_redraw(global_app_state);
                }
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                POINT point = {
                    static_cast<LONG>(loword_i32(lparam)),
                    static_cast<LONG>(hiword_i32(lparam)),
                };
                ScreenToClient(hwnd, &point);
                global_app_state->input.mouse_pos = {
                    static_cast<float>(point.x),
                    static_cast<float>(point.y),
                };
                global_app_state->input.scroll_delta_y +=
                    (static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                     static_cast<float>(WHEEL_DELTA)) *
                    72.0f;
                request_redraw(global_app_state);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
                global_app_state->input.mouse_down[0u] = true;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
                global_app_state->input.mouse_down[0u] = false;
                request_redraw(global_app_state);
            }
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        case WM_CLOSE:
            if (global_app_state != nullptr) {
                global_app_state->running = false;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    auto apply_window_header_theme(HWND hwnd) -> void {
        BOOL const dark_mode = TRUE;
        COLORREF const border_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const caption_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const text_color = WINDOW_HEADER_TEXT;

        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd,
            DWM_ATTR_USE_IMMERSIVE_DARK_MODE,
            &dark_mode,
            static_cast<DWORD>(sizeof(dark_mode))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_BORDER_COLOR, &border_color, static_cast<DWORD>(sizeof(border_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_CAPTION_COLOR, &caption_color, static_cast<DWORD>(sizeof(caption_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_TEXT_COLOR, &text_color, static_cast<DWORD>(sizeof(text_color))
        ));
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        DWORD const ex_style = WS_EX_DLGMODALFRAME;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRectEx(&rect, style, FALSE, ex_style)) {
            fmt::eprintf("AdjustWindowRectEx failed: %lu\n", GetLastError());
            return false;
        }

        HWND const hwnd = CreateWindowExW(
            ex_style,
            WINDOW_CLASS_NAME,
            L"gui_framework repository UI testbed",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        apply_window_header_theme(hwnd);
        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

} // namespace

auto main() -> int {
    base::install_crash_handlers();

    AppState app_state = {};
    app_state.tree = &global_repo_tree;
    app_state.details = &global_repo_details;
    global_app_state = &app_state;
    if (!create_testbed_window(&app_state)) {
        global_app_state = nullptr;
        return 1;
    }

    Arena app_arena = {};
    app_arena.init();

    render::Context render_context = {};
    render::ContextDesc context_desc = {};
    context_desc.backend = render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    render::Result render_result = render::create_context(app_arena, context_desc, render_context);
    if (render::result_failed(render_result)) {
        log_render_result("render::create_context", render_result);
        DestroyWindow(app_state.hwnd);
        global_app_state = nullptr;
        return 1;
    }

    render::Window render_window = {};
    render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = app_state.window_size;
    window_desc.buffer_count = 2u;
    window_desc.present_mode = render::PresentMode::VSYNC;

    render_result = render::create_window(app_arena, render_context, window_desc, render_window);
    if (render::result_failed(render_result)) {
        log_render_result("render::create_window", render_result);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        global_app_state = nullptr;
        return 1;
    }

    Runtime runtime = {};
    if (!create_runtime(app_arena, render_context, &runtime)) {
        destroy_runtime(render_context, &runtime);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        global_app_state = nullptr;
        return 1;
    }
    load_repo_tree(*app_state.tree);
    load_repo_details(*app_state.tree, *app_state.details);

    while (app_state.running) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                app_state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!app_state.running) {
            break;
        }

        if (app_state.resize_pending) {
            render_result =
                render::resize_window(render_context, render_window, app_state.pending_size);
            if (render::result_failed(render_result)) {
                log_render_result("render::resize_window", render_result);
                break;
            }
            app_state.window_size = app_state.pending_size;
            app_state.resize_pending = false;
            app_state.redraw_pending = true;
        }

        if (!app_state.redraw_pending) {
            BASE_UNUSED(
                MsgWaitForMultipleObjectsEx(0u, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE)
            );
            continue;
        }

        RepositorySection const selected_section_before = app_state.selected_section;
        size_t const selected_tab_before = app_state.selected_tab;
        uint64_t const open_hash_before = repo_tree_open_hash(*app_state.tree);
        app_state.redraw_pending = false;

        gui::Frame ui = gui::begin_frame(
            runtime.ui_context,
            {
                .size =
                    {
                        static_cast<float>(app_state.window_size.width),
                        static_cast<float>(app_state.window_size.height),
                    },
                .delta_time = 1.0f / 60.0f,
                .input = app_state.input,
            }
        );
        draw_repository_ui(
            ui,
            runtime.icon_font,
            app_state.selected_section,
            app_state.selected_tab,
            *app_state.tree,
            *app_state.details
        );
        gui::end_frame(ui);
        app_state.last_frame = ui;
        app_state.mouse_hit_id = frame_hit_id(app_state.last_frame, app_state.input.mouse_pos);

        render::begin_frame(render_context);
        draw::begin_frame(runtime.draw_context);
        gui::render_frame(ui, runtime.draw_context);
        draw_repository_icons(
            ui,
            runtime.draw_context,
            app_state.selected_section,
            app_state.selected_tab,
            *app_state.tree
        );
        draw::end_frame(runtime.draw_context);

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

        render_result = draw::render_commands_to_window(
            runtime.draw_renderer, render_context, pass_desc, runtime.draw_context
        );
        if (render::result_failed(render_result)) {
            log_render_result("draw::render_commands_to_window", render_result);
            break;
        }

        render_result = render::present_window(render_context, render_window);
        app_state.redraw_pending = app_state.selected_section != selected_section_before ||
                                   app_state.selected_tab != selected_tab_before ||
                                   repo_tree_open_hash(*app_state.tree) != open_hash_before;
        app_state.input.scroll_delta_y = 0.0f;
        if (render_result == render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (render::result_failed(render_result)) {
            log_render_result("render::present_window", render_result);
            break;
        }
    }

    destroy_runtime(render_context, &runtime);
    render::destroy_window(render_window);
    render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }
    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
