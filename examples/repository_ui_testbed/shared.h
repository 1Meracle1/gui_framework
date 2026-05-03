#pragma once

#include <base/config.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <gui/gui.h>
#include <gui/hot_reload_app.h>

namespace repository_ui_testbed {

#if defined(_WIN32) && BASE_DEBUG
    inline constexpr bool HOT_RELOAD_ENABLED = true;
#else
    inline constexpr bool HOT_RELOAD_ENABLED = false;
#endif

    inline constexpr uint32_t INITIAL_WINDOW_WIDTH = 1600u;
    inline constexpr uint32_t INITIAL_WINDOW_HEIGHT = 900u;
    inline constexpr float SIDEBAR_WIDTH = 220.0f;
    inline constexpr size_t MAX_REPO_NODES = 1024u;
    inline constexpr size_t MAX_REPO_PATH = 384u;
    inline constexpr size_t MAX_REPO_COMMITS = 64u;
    inline constexpr size_t REPO_COMMAND_LINE_CAPACITY = 512u;
    inline constexpr size_t MODULE_STORAGE_SIZE = 256u * 1024u;
    inline constexpr size_t MODULE_STORAGE_ALIGNMENT = 64u;
    inline constexpr StrRef MODULE_FILE_NAME = "repository_ui_testbed_module.dll";
    inline constexpr uint32_t HOT_RELOAD_POLL_MS = 250u;

    inline constexpr StrRef ICON_CODE = "\xee\xab\x84";
    inline constexpr StrRef ICON_ISSUES = "\xee\xac\x8c";
    inline constexpr StrRef ICON_PULL_REQUESTS = "\xee\xa9\xa4";
    inline constexpr StrRef ICON_ACTIONS = "\xee\xac\xac";
    inline constexpr StrRef ICON_PROJECTS = "\xee\xac\xb0";
    inline constexpr StrRef ICON_SECURITY = "\xee\xad\x93";
    inline constexpr StrRef ICON_INSIGHTS = "\xee\xaf\xa2";
    inline constexpr StrRef ICON_WIKI = "\xee\xaa\xa4";
    inline constexpr StrRef ICON_SETTINGS = "\xee\xab\xb8";
    inline constexpr StrRef ICON_CHEVRON_RIGHT = "\xee\xaa\xb6";

    using ModuleRuntimeContext = gui::HotReloadRuntimeContext;
    using DrawCommandCounts = gui::HotReloadDrawCommandCounts;
    using FrameResult = gui::HotReloadFrameResult;
    using ModuleRenderFrameFn = gui::HotReloadRenderFrameFn;
    using ModuleApi = gui::HotReloadAppApi;

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
        StrRef name = {};
        StrRef path = {};
        StrRef message = {};
        StrRef age = {};
        int32_t parent = -1;
        int32_t first_child = -1;
        int32_t next_sibling = -1;
        uint8_t indent = 0u;
        bool directory = false;
        bool open = false;
        bool has_commit = false;
    };

    struct RepoTree {
        StrRef root = {};
        RepoNode nodes[MAX_REPO_NODES] = {};
        int32_t root_child = -1;
        int32_t visible[MAX_REPO_NODES] = {};
        size_t node_count = 0u;
        size_t visible_count = 0u;
        bool loaded = false;
    };

    struct RepoCommit {
        StrRef hash = {};
        StrRef author = {};
        StrRef age = {};
        StrRef subject = {};
    };

    struct RepoDetails {
        StrRef name = {};
        StrRef description = {};
        StrRef branch = {};
        StrRef short_hash = {};
        StrRef author = {};
        StrRef author_initial = {};
        StrRef last_commit_age = {};
        StrRef last_commit_subject = {};
        StrRef latest_release = {};
        StrRef package_size = {};
        StrRef license = {};
        StrRef activity = {};
        StrRef insertion_count = {};
        StrRef deletion_count = {};
        StrRef commit_count = {};
        RepoCommit commits[MAX_REPO_COMMITS] = {};
        size_t shown_commit_count = 0u;
    };

    struct RepositoryTab {
        StrRef title = {};
        StrRef count = {};
        float width = 0.0f;
        float badge_width = 0.0f;
    };

} // namespace repository_ui_testbed
