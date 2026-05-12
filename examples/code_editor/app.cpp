#include "app.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "editor_config.h"
#include "editor_model.h"
#include "editor_render.h"
#include "editor_theme.h"
#include "git.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/unicode.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#endif

#ifndef CODE_EDITOR_SOURCE_DIR
#define CODE_EDITOR_SOURCE_DIR "."
#endif

namespace code_editor {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    constexpr int SOURCE_CODE_PRO_FONT_ID = 102;
    constexpr int WINDOWS_RCDATA_ID = 10;
    constexpr float FILE_SEARCH_BACKDROP_BLUR_RADIUS = 14.0f;
    constexpr float FILE_SEARCH_BACKDROP_DIM_ALPHA = 0.12f;
    constexpr float CONFIG_ERROR_MARGIN = 16.0f;
    constexpr float CONFIG_ERROR_MIN_WIDTH = 360.0f;
    constexpr float CONFIG_ERROR_MAX_WIDTH = 620.0f;
    constexpr float CONFIG_ERROR_HEIGHT_FRACTION = 0.4f;
    constexpr float CONFIG_ERROR_EXCERPT_FONT_SIZE = 11.0f;
    constexpr float CONFIG_ERROR_EXCERPT_LINE_HEIGHT = 17.0f;
    constexpr float CONFIG_ERROR_SECTION_GAP = 10.0f;
    constexpr float CONFIG_ERROR_PATH_HEIGHT = 18.0f;
    constexpr float CONFIG_ERROR_HEADER_HEIGHT = 28.0f;
    constexpr float CONFIG_ERROR_MESSAGE_LINE_HEIGHT_SCALE = 1.45f;
    constexpr float CONFIG_ERROR_MESSAGE_MIN_HEIGHT = 24.0f;
    constexpr float CONFIG_ERROR_EXCERPT_PADDING = 20.0f;

    struct RuntimeConfigState {
        EditorConfig base = {};
        EditorConfig effective = {};
        EditorConfigPatch session_override = {};
        EditorConfigError error = {};
        char global_path[EDITOR_CONFIG_PATH_CAPACITY] = {};
        char local_path[EDITOR_CONFIG_PATH_CAPACITY] = {};
        uint64_t global_write_stamp = 0u;
        uint64_t local_write_stamp = 0u;
        bool error_visible = false;
    };

    struct Runtime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font ui_font = {};
        font_cache::Font editor_font = {};
        font_cache::Font icon_font = {};
        font_cache::Font branch_icon_font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::Context ui_context = {};
        EditorState editor = {};
        void* native_window = nullptr;
        StrRef const* shared_tree_root_name = nullptr;
        Slice<FileTreeEntry>* shared_tree_files = nullptr;
        bool const* shared_tree_loading = nullptr;
        uint64_t const* shared_file_change_generation = nullptr;
        SpscQueue<GitWorkRequest>* shared_git_requests = nullptr;
        SpscQueue<GitWorkResult>* shared_git_results = nullptr;
        LspBridge const* lsp_bridge = nullptr;
        LspSendEditorRequestFn lsp_send_request = nullptr;
        void* lsp_user_data = nullptr;
        bool* app_close_requested = nullptr;
        bool* app_close_confirmed = nullptr;
        uint64_t file_change_generation = 0u;
        float char_width = 8.0f;
        RuntimeConfigState config = {};
    };

    static auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    static auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto selected_font_backend() -> font_provider::Backend {
        char backend[32] = {};
        DWORD const size = GetEnvironmentVariableA(
            "CODE_EDITOR_FONT_BACKEND", backend, static_cast<DWORD>(sizeof(backend))
        );
        if (size != 0u && size < sizeof(backend) &&
            StrRef(backend, static_cast<size_t>(size)).equals_ignore_ascii_case("freetype")) {
            return font_provider::Backend::FREETYPE;
        }
#if defined(_WIN32)
        return font_provider::Backend::DWRITE;
#else
        return font_provider::Backend::FREETYPE;
#endif
    }

    [[nodiscard]] auto embedded_source_code_pro_font() -> Slice<uint8_t const> {
        HMODULE const module = GetModuleHandleW(nullptr);
        HRSRC const resource = FindResourceW(
            module, MAKEINTRESOURCEW(SOURCE_CODE_PRO_FONT_ID), MAKEINTRESOURCEW(WINDOWS_RCDATA_ID)
        );
        if (resource == nullptr) {
            return {};
        }
        HGLOBAL const loaded = LoadResource(module, resource);
        void const* const data = loaded != nullptr ? LockResource(loaded) : nullptr;
        DWORD const size = SizeofResource(module, resource);
        if (data == nullptr || size == 0u) {
            return {};
        }
        return {static_cast<uint8_t const*>(data), static_cast<size_t>(size)};
    }

    [[nodiscard]] static auto sync_shared_file_tree(Runtime& runtime) -> bool {
        if (runtime.shared_tree_root_name != nullptr) {
            runtime.editor.tree_root_name = *runtime.shared_tree_root_name;
        }
        if (runtime.shared_tree_files != nullptr) {
            runtime.editor.tree_files = *runtime.shared_tree_files;
        }
        if (runtime.shared_tree_loading != nullptr) {
            runtime.editor.tree_loading = *runtime.shared_tree_loading;
        }
        if (runtime.shared_file_change_generation == nullptr) {
            return true;
        }
        uint64_t const generation = *runtime.shared_file_change_generation;
        if (generation == runtime.file_change_generation) {
            return false;
        }
        runtime.file_change_generation = generation;
        return true;
    }

    auto copy_cstr(char* buffer, size_t capacity, StrRef text) -> void {
        if (capacity == 0u) {
            return;
        }
        size_t const size = std::min(text.size(), capacity - 1u);
        if (size != 0u) {
            std::memcpy(buffer, text.data(), size);
        }
        buffer[size] = '\0';
    }

    auto set_git_status_text(EditorState& editor, StrRef text) -> void {
        if (editor.arena == nullptr || text.empty()) {
            editor.git_status_text = {};
            return;
        }
        editor.git_status_text = arena_copy_cstr(*editor.arena, text);
    }

    auto set_git_error_text(EditorState& editor, StrRef text) -> void {
        editor.git_status_text = {};
        if (editor.arena == nullptr || text.empty()) {
            editor.git_error_text = {};
            editor.git_error_visible = false;
            return;
        }
        editor.git_error_text = arena_copy_cstr(*editor.arena, text);
        editor.git_error_visible = true;
    }

    [[nodiscard]] auto copy_git_status_item(Arena& arena, GitStatusItem const& item)
        -> GitStatusItem {
        return {
            .path = arena_copy_cstr(arena, item.path),
            .old_path = item.old_path.empty() ? StrRef() : arena_copy_cstr(arena, item.old_path),
            .status = item.status,
            .scope = item.scope,
        };
    }

    [[nodiscard]] auto copy_git_commit(Arena& arena, GitCommit const& commit) -> GitCommit {
        GitCommit result = {
            .oid = arena_copy_cstr(arena, commit.oid),
            .short_oid = arena_copy_cstr(arena, commit.short_oid),
            .refs = arena_copy_cstr(arena, commit.refs),
            .summary = arena_copy_cstr(arena, commit.summary),
            .author = arena_copy_cstr(arena, commit.author),
            .relative_date = arena_copy_cstr(arena, commit.relative_date),
            .author_date = arena_copy_cstr(arena, commit.author_date),
            .body = arena_copy_cstr(arena, commit.body),
            .changed_file_count = commit.changed_file_count,
            .insertion_count = commit.insertion_count,
            .deletion_count = commit.deletion_count,
            .parent_count = commit.parent_count,
            .incoming = commit.incoming,
            .open = commit.open,
        };
        for (size_t index = 0u; index < commit.parent_count; ++index) {
            result.parents[index] = arena_copy_cstr(arena, commit.parents[index]);
        }
        return result;
    }

    [[nodiscard]] auto copy_git_commit_file(Arena& arena, GitCommitFile const& file)
        -> GitCommitFile {
        return {
            .commit_oid = arena_copy_cstr(arena, file.commit_oid),
            .path = arena_copy_cstr(arena, file.path),
            .old_path = file.old_path.empty() ? StrRef() : arena_copy_cstr(arena, file.old_path),
            .status = file.status,
        };
    }

    [[nodiscard]] auto copy_git_branch(Arena& arena, GitBranch const& branch) -> GitBranch {
        return {.name = arena_copy_cstr(arena, branch.name)};
    }

    auto copy_git_status_items(EditorState& editor, Vec<GitStatusItem> const& items) -> void {
        editor.git_status_items.clear();
        for (GitStatusItem const& item : items) {
            BASE_UNUSED(
                editor.git_status_items.push_back(copy_git_status_item(*editor.arena, item))
            );
        }
    }

    [[nodiscard]] auto git_status_item_matches_path(GitStatusItem const& item, StrRef path)
        -> bool {
        return item.path == path || item.old_path == path;
    }

    auto replace_git_status_path(EditorState& editor, GitWorkResult const& result) -> void {
        for (size_t index = 0u; index < editor.git_status_items.size();) {
            if (git_status_item_matches_path(editor.git_status_items[index], result.path)) {
                editor.git_status_items.ordered_remove(index);
            } else {
                index += 1u;
            }
        }
        for (GitStatusItem const& item : result.status_items) {
            BASE_UNUSED(
                editor.git_status_items.push_back(copy_git_status_item(*editor.arena, item))
            );
        }
    }

    auto copy_git_branches(EditorState& editor, Vec<GitBranch> const& branches) -> void {
        editor.git_branches.clear();
        for (GitBranch const& branch : branches) {
            BASE_UNUSED(editor.git_branches.push_back(copy_git_branch(*editor.arena, branch)));
        }
    }

    auto copy_git_commits(EditorState& editor, Vec<GitCommit> const& commits) -> void {
        editor.git_commits.clear();
        editor.git_commit_popup = GIT_COMMIT_POPUP_NONE;
        editor.git_commit_popup_selection = {};
        editor.git_commit_popup_keyboard = false;
        editor.git_commit_popup_mouse_known = false;
        for (GitCommit const& commit : commits) {
            BASE_UNUSED(editor.git_commits.push_back(copy_git_commit(*editor.arena, commit)));
        }
        layout_git_commit_graph(editor.git_commits);
    }

    [[nodiscard]] auto git_commit_files_loaded(EditorState const& editor, StrRef oid) -> bool {
        for (GitCommitFile const& file : editor.git_commit_files) {
            if (file.commit_oid == oid) {
                return true;
            }
        }
        return false;
    }

    auto trim_git_commit_sentinel(EditorState& editor, size_t git_log_limit) -> void {
        editor.git_commits_more = editor.git_commits.size() > git_log_limit;
        if (editor.git_commits_more) {
            BASE_UNUSED(editor.git_commits.pop_safe());
        }
    }

    auto apply_git_root_result(EditorState& editor, GitWorkResult const& result) -> void {
        editor.git_root_checked = true;
        editor.git_root_path =
            !result.root.empty() ? arena_copy_cstr(*editor.arena, result.root) : StrRef();
    }

    auto apply_git_refresh_result(EditorState& editor, GitWorkResult const& result) -> void {
        editor.git_operation_pending = false;
        editor.git_pending_operation_kind = GitWorkKind::NONE;
        editor.git_refresh_requested = false;
        editor.git_log_refresh_requested = false;
        if (editor.arena == nullptr) {
            return;
        }
        apply_git_root_result(editor, result);
        if (!result.ok) {
            set_git_error_text(
                editor, result.message.empty() ? "Git refresh failed." : result.message
            );
            return;
        }

        copy_git_branches(editor, result.branches);
        copy_git_status_items(editor, result.status_items);
        editor.git_current_branch = arena_copy_cstr(*editor.arena, result.current_branch);
        editor.git_pending_pull_count = result.pending_pull_count;
        editor.git_pending_push_count = result.pending_push_count;
        editor.git_operation_state = result.operation_state;
        if (result.log_loaded) {
            copy_git_commits(editor, result.commits);
            editor.git_commit_files.clear();
            trim_git_commit_sentinel(editor, result.limit);
            editor.git_commit_limit = result.limit;
        }
        set_git_status_text(editor, {});
    }

    auto append_git_commit_page(EditorState& editor, GitWorkResult const& result) -> void {
        if (editor.arena == nullptr || result.generation != editor.git_commit_load_generation) {
            return;
        }
        editor.git_commits_loading = false;
        if (result.offset != editor.git_commits.size()) {
            return;
        }
        if (!result.ok) {
            set_git_error_text(
                editor, result.message.empty() ? "Git commit page failed." : result.message
            );
            return;
        }

        size_t const append_count = std::min(result.commits.size(), result.count);
        for (size_t index = 0u; index < append_count; ++index) {
            BASE_UNUSED(
                editor.git_commits.push_back(copy_git_commit(*editor.arena, result.commits[index]))
            );
        }
        layout_git_commit_graph(editor.git_commits);
        editor.git_commits_more = result.commits.size() > result.count;
        editor.git_commit_limit = std::max(editor.git_commit_limit, editor.git_commits.size());
        set_git_status_text(editor, {});
    }

    auto append_git_commit_files(EditorState& editor, GitWorkResult const& result) -> void {
        editor.git_operation_pending = false;
        editor.git_pending_operation_kind = GitWorkKind::NONE;
        if (editor.arena == nullptr) {
            return;
        }
        apply_git_root_result(editor, result);
        if (!result.ok) {
            set_git_error_text(
                editor, result.message.empty() ? "Git commit files failed." : result.message
            );
            return;
        }
        if (!git_commit_files_loaded(editor, result.commit_oid)) {
            for (GitCommitFile const& file : result.commit_files) {
                BASE_UNUSED(
                    editor.git_commit_files.push_back(copy_git_commit_file(*editor.arena, file))
                );
            }
        }
        set_git_status_text(editor, {});
    }

    auto apply_git_action_result(EditorState& editor, GitWorkResult const& result) -> void {
        editor.git_operation_pending = false;
        editor.git_pending_operation_kind = GitWorkKind::NONE;
        if (editor.arena == nullptr) {
            return;
        }
        apply_git_root_result(editor, result);
        if (!result.ok) {
            set_git_error_text(
                editor, result.message.empty() ? "Git operation failed." : result.message
            );
            editor.git_refresh_requested = true;
            editor.git_log_refresh_requested = true;
            return;
        }

        if (result.kind == GitWorkKind::OPEN_STATUS_DIFF) {
            StrRef const title =
                fmt::tprintf("%s %s", git_status_scope_label(result.scope), result.path);
            open_git_diff(editor, title, result.patch);
            set_git_status_text(editor, {});
            return;
        }
        if (result.kind == GitWorkKind::OPEN_COMMIT_DIFF) {
            StrRef const title = fmt::tprintf("%s %s", result.commit_oid, result.path);
            open_git_diff(editor, title, result.patch);
            set_git_status_text(editor, {});
            return;
        }

        if (result.kind == GitWorkKind::CHECKOUT_BRANCH) {
            editor.git_branches_open = false;
        }
        if (result.kind == GitWorkKind::STAGE || result.kind == GitWorkKind::UNSTAGE) {
            replace_git_status_path(editor, result);
            set_git_status_text(editor, result.message);
            return;
        }
        set_git_status_text(editor, result.message);
        editor.git_refresh_requested = true;
        if (result.kind == GitWorkKind::COMMIT || result.kind == GitWorkKind::PULL ||
            result.kind == GitWorkKind::MERGE_BRANCH || result.kind == GitWorkKind::REBASE_BRANCH ||
            result.kind == GitWorkKind::CHERRY_PICK || result.kind == GitWorkKind::MERGE_ABORT ||
            result.kind == GitWorkKind::REBASE_CONTINUE ||
            result.kind == GitWorkKind::REBASE_ABORT ||
            result.kind == GitWorkKind::CHERRY_PICK_CONTINUE ||
            result.kind == GitWorkKind::CHERRY_PICK_ABORT ||
            result.kind == GitWorkKind::CHECKOUT_BRANCH) {
            editor.git_log_refresh_requested = true;
        }
    }

    auto sync_git_worker_results(Runtime& runtime) -> void {
        if (runtime.shared_git_results == nullptr) {
            return;
        }
        GitWorkResult result = {};
        while (runtime.shared_git_results->pop(result)) {
            switch (result.kind) {
            case GitWorkKind::REFRESH:
                apply_git_refresh_result(runtime.editor, result);
                break;
            case GitWorkKind::COMMIT_PAGE:
                append_git_commit_page(runtime.editor, result);
                break;
            case GitWorkKind::COMMIT_FILES:
                append_git_commit_files(runtime.editor, result);
                break;
            default:
                apply_git_action_result(runtime.editor, result);
                break;
            }
        }
    }

    [[nodiscard]] auto git_work_kind_from_request(GitRequestKind kind) -> GitWorkKind {
        switch (kind) {
        case GitRequestKind::STAGE:
            return GitWorkKind::STAGE;
        case GitRequestKind::STAGE_ALL:
            return GitWorkKind::STAGE_ALL;
        case GitRequestKind::UNSTAGE:
            return GitWorkKind::UNSTAGE;
        case GitRequestKind::UNSTAGE_ALL:
            return GitWorkKind::UNSTAGE_ALL;
        case GitRequestKind::COMMIT:
            return GitWorkKind::COMMIT;
        case GitRequestKind::PUSH:
            return GitWorkKind::PUSH;
        case GitRequestKind::PULL:
            return GitWorkKind::PULL;
        case GitRequestKind::FETCH:
            return GitWorkKind::FETCH;
        case GitRequestKind::MERGE_BRANCH:
            return GitWorkKind::MERGE_BRANCH;
        case GitRequestKind::REBASE_BRANCH:
            return GitWorkKind::REBASE_BRANCH;
        case GitRequestKind::CHERRY_PICK:
            return GitWorkKind::CHERRY_PICK;
        case GitRequestKind::MERGE_ABORT:
            return GitWorkKind::MERGE_ABORT;
        case GitRequestKind::REBASE_CONTINUE:
            return GitWorkKind::REBASE_CONTINUE;
        case GitRequestKind::REBASE_ABORT:
            return GitWorkKind::REBASE_ABORT;
        case GitRequestKind::CHERRY_PICK_CONTINUE:
            return GitWorkKind::CHERRY_PICK_CONTINUE;
        case GitRequestKind::CHERRY_PICK_ABORT:
            return GitWorkKind::CHERRY_PICK_ABORT;
        case GitRequestKind::CHECKOUT_BRANCH:
            return GitWorkKind::CHECKOUT_BRANCH;
        case GitRequestKind::OPEN_STATUS_DIFF:
            return GitWorkKind::OPEN_STATUS_DIFF;
        case GitRequestKind::OPEN_COMMIT_DIFF:
            return GitWorkKind::OPEN_COMMIT_DIFF;
        case GitRequestKind::NONE:
        case GitRequestKind::REFRESH:
        default:
            return GitWorkKind::NONE;
        }
    }

    auto fill_git_root_request(EditorState const& editor, GitWorkRequest& request) -> void {
        request.save_root = editor.save_root_path;
        request.root = editor.git_root_path;
    }

    [[nodiscard]] auto git_root_ready(EditorState const& editor) -> bool {
        return editor.git_root_checked && !editor.git_root_path.empty();
    }

    [[nodiscard]] auto
    submit_git_operation(Runtime& runtime, GitWorkRequest const& request, StrRef status_text)
        -> bool {
        if (runtime.shared_git_requests == nullptr || !runtime.shared_git_requests->push(request)) {
            set_git_error_text(runtime.editor, "Git worker queue is full.");
            return false;
        }
        runtime.editor.git_operation_pending = true;
        runtime.editor.git_pending_operation_kind = request.kind;
        set_git_status_text(runtime.editor, status_text);
        return true;
    }

    [[nodiscard]] auto submit_git_action_request(Runtime& runtime) -> bool {
        EditorState& editor = runtime.editor;
        if (editor.git_operation_pending || editor.git_request.kind == GitRequestKind::NONE) {
            return false;
        }
        GitRequest const request = editor.git_request;
        if (request.kind == GitRequestKind::REFRESH) {
            editor.git_request = {};
            editor.git_refresh_requested = true;
            editor.git_log_refresh_requested = true;
            return false;
        }

        GitWorkRequest work = {
            .kind = git_work_kind_from_request(request.kind),
            .scope = request.scope,
            .path = request.path,
            .commit_oid = request.commit_oid,
            .message_text = request.message,
            .branch = request.branch,
        };
        fill_git_root_request(editor, work);
        StrRef const status =
            work.kind == GitWorkKind::OPEN_STATUS_DIFF || work.kind == GitWorkKind::OPEN_COMMIT_DIFF
                ? StrRef("Loading Git diff...")
                : StrRef("Running Git...");
        if (!submit_git_operation(runtime, work, status)) {
            return false;
        }
        editor.git_request = {};
        return true;
    }

    [[nodiscard]] auto git_log_limit_for_window_height(uint32_t height) -> size_t {
        size_t const rows = static_cast<size_t>(std::ceil(static_cast<float>(height) / 24.0f));
        return std::max(GIT_LOG_MIN_LIMIT, rows);
    }

    [[nodiscard]] auto submit_git_refresh_request(Runtime& runtime, uint32_t window_height)
        -> bool {
        EditorState& editor = runtime.editor;
        if (editor.git_operation_pending) {
            return false;
        }
        bool const git_sidebar_open =
            editor.flag(EditorFlag::SIDEBAR_VISIBLE) && editor.sidebar_tab == EditorSidebarTab::GIT;
        if (!git_sidebar_open) {
            return false;
        }
        bool const load_log = editor.git_graph_open && editor.git_log_refresh_requested;
        if (!editor.git_refresh_requested && !load_log) {
            return false;
        }

        size_t const git_log_limit = git_log_limit_for_window_height(window_height);
        editor.git_refresh_requested = false;
        editor.git_log_refresh_requested = false;
        editor.git_status_items.clear();
        editor.git_branches.clear();
        if (load_log) {
            editor.git_commits.clear();
            editor.git_commit_files.clear();
            editor.git_commit_load_generation += 1u;
            editor.git_commit_load_more_requested = false;
            editor.git_commits_loading = false;
            editor.git_commits_more = false;
        }

        GitWorkRequest request = {
            .kind = GitWorkKind::REFRESH,
            .generation = editor.git_commit_load_generation,
            .count = load_log ? git_log_limit + 1u : 0u,
            .limit = git_log_limit,
        };
        fill_git_root_request(editor, request);
        return submit_git_operation(runtime, request, "Loading Git...");
    }

    [[nodiscard]] auto submit_git_commit_files_request(Runtime& runtime) -> bool {
        EditorState& editor = runtime.editor;
        if (editor.git_operation_pending || !editor.git_graph_open || !git_root_ready(editor)) {
            return false;
        }
        for (GitCommit const& commit : editor.git_commits) {
            if (!commit.open || git_commit_files_loaded(editor, commit.oid)) {
                continue;
            }
            GitWorkRequest request = {
                .kind = GitWorkKind::COMMIT_FILES,
                .commit_oid = commit.oid,
            };
            fill_git_root_request(editor, request);
            return submit_git_operation(runtime, request, "Loading commit files...");
        }
        return false;
    }

    [[nodiscard]] auto submit_git_commit_page_request(Runtime& runtime) -> bool {
        EditorState& editor = runtime.editor;
        if (!editor.git_commit_load_more_requested || editor.git_operation_pending) {
            return false;
        }
        editor.git_commit_load_more_requested = false;
        if (!editor.git_graph_open || !editor.git_commits_more || editor.git_commits_loading ||
            !git_root_ready(editor)) {
            return false;
        }

        GitWorkRequest request = {
            .kind = GitWorkKind::COMMIT_PAGE,
            .generation = editor.git_commit_load_generation + 1u,
            .offset = editor.git_commits.size(),
            .count = GIT_LOG_PAGE_SIZE,
        };
        fill_git_root_request(editor, request);
        if (runtime.shared_git_requests == nullptr || !runtime.shared_git_requests->push(request)) {
            set_git_error_text(editor, "Git worker queue is full.");
            return false;
        }
        editor.git_commit_load_generation = request.generation;
        editor.git_commits_loading = true;
        return true;
    }

    auto submit_git_worker_requests(Runtime& runtime, uint32_t window_height) -> void {
        if (submit_git_action_request(runtime)) {
            return;
        }
        if (submit_git_refresh_request(runtime, window_height)) {
            return;
        }
        if (submit_git_commit_files_request(runtime)) {
            return;
        }
        BASE_UNUSED(submit_git_commit_page_request(runtime));
    }

    [[nodiscard]] auto runtime_config_path(char const* buffer) -> StrRef {
        return StrRef(buffer, cstr_len(buffer));
    }

    [[nodiscard]] auto key_pressed(gui::InputState const& input, gui::Key key) -> bool {
        for (size_t index = 0u; index < input.key_event_count; ++index) {
            gui::KeyEvent const& event = input.key_events[index];
            if (event.kind == gui::KeyEventKind::PRESS && event.key == key) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto line_count(StrRef text) -> size_t {
        if (text.empty()) {
            return 0u;
        }
        size_t count = 1u;
        for (char const ch : text) {
            if (ch == '\n') {
                count += 1u;
            }
        }
        if (text.back() == '\n' && count > 1u) {
            count -= 1u;
        }
        return count;
    }

    [[nodiscard]] auto
    wrapped_line_count(font_cache::Font font, float font_size, StrRef text, float max_width)
        -> size_t {
        if (text.empty() || max_width <= 1.0f) {
            return text.empty() ? 0u : 1u;
        }

        float const space_width = font_cache::text_advance(font, font_size, " ");
        size_t total_lines = 0u;
        for (StrRef const raw_line : text.lines()) {
            if (raw_line.empty()) {
                total_lines += 1u;
                continue;
            }

            size_t line_total = 1u;
            float line_width = 0.0f;
            for (StrRef const word : raw_line.split_ascii_whitespace()) {
                float const word_width = font_cache::text_advance(font, font_size, word);
                if (line_width <= 0.0f) {
                    line_width = word_width;
                    continue;
                }
                if (line_width + space_width + word_width <= max_width) {
                    line_width += space_width + word_width;
                    continue;
                }
                line_total += 1u;
                line_width = word_width;
            }
            total_lines += line_total;
        }
        return total_lines;
    }

    auto append_error_note(EditorConfigError& error, StrRef note) -> void {
        size_t const size = cstr_len(error.message);
        if (size >= sizeof(error.message) - 1u || note.empty()) {
            return;
        }
        size_t const available = sizeof(error.message) - size - 1u;
        if (available <= note.size()) {
            return;
        }
        std::memcpy(error.message + size, note.data(), note.size());
        error.message[size + note.size()] = '\0';
    }

    [[nodiscard]] auto open_read_file(StrRef path) -> std::FILE* {
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

    [[nodiscard]] auto read_config_file_text(Arena& arena, StrRef path, StrRef& out_text) -> bool {
        out_text = {};
        if (path.empty() || !editor_path_exists(path)) {
            return true;
        }

        std::FILE* const file = open_read_file(path);
        if (file == nullptr) {
            return false;
        }

        bool ok = std::fseek(file, 0, SEEK_END) == 0;
        long const size = ok ? std::ftell(file) : -1l;
        ok = ok && size >= 0l && std::fseek(file, 0, SEEK_SET) == 0;
        if (ok) {
            char* const text = arena_alloc<char>(arena, static_cast<size_t>(size) + 1u);
            size_t const read_size = std::fread(text, 1u, static_cast<size_t>(size), file);
            ok = read_size == static_cast<size_t>(size);
            text[read_size] = '\0';
            out_text = ok ? StrRef(text, read_size) : StrRef();
        }
        std::fclose(file);
        if (out_text.starts_with("\xef\xbb\xbf")) {
            out_text.remove_prefix(3u);
        }
        return ok;
    }

    [[nodiscard]] auto ensure_parent_directories(StrRef path) -> bool {
        size_t const separator = path.find_last_of("\\/");
        if (separator == StrRef::NPOS) {
            return true;
        }

        char buffer[EDITOR_CONFIG_PATH_CAPACITY] = {};
        copy_cstr(buffer, sizeof(buffer), path.prefix(separator));
        size_t const size = cstr_len(buffer);
        for (size_t index = 0u; index < size; ++index) {
            if (buffer[index] != '\\' && buffer[index] != '/') {
                continue;
            }
            if (index == 0u || (index == 2u && buffer[1u] == ':')) {
                continue;
            }
            char const saved = buffer[index];
            buffer[index] = '\0';
            if (buffer[0u] != '\0') {
                DWORD const result = CreateDirectoryA(buffer, nullptr);
                if (result == 0u && GetLastError() != ERROR_ALREADY_EXISTS) {
                    buffer[index] = saved;
                    return false;
                }
            }
            buffer[index] = saved;
        }
        if (buffer[0u] == '\0') {
            return true;
        }
        DWORD const result = CreateDirectoryA(buffer, nullptr);
        return result != 0u || GetLastError() == ERROR_ALREADY_EXISTS;
    }

    [[nodiscard]] auto ensure_config_file_exists(StrRef path) -> bool {
        if (path.empty()) {
            return false;
        }
        if (editor_path_exists(path)) {
            return true;
        }
        if (!ensure_parent_directories(path)) {
            return false;
        }
        return editor_write_text_file(path, editor_default_config_template());
    }

    auto apply_runtime_config(Runtime& runtime) -> void {
        runtime.editor.font_size = runtime.config.effective.font_size;
        runtime.editor.raster_policy = runtime.config.effective.raster_policy;
        set_filesystem_panel_visible(runtime.editor, runtime.config.effective.sidebar_visible);
    }

    auto reload_runtime_config(Runtime& runtime, bool force) -> void {
        StrRef const global_path = runtime_config_path(runtime.config.global_path);
        StrRef const local_path = runtime_config_path(runtime.config.local_path);
        uint64_t const global_stamp = editor_file_write_stamp(global_path);
        uint64_t const local_stamp = editor_file_write_stamp(local_path);
        if (!force && global_stamp == runtime.config.global_write_stamp &&
            local_stamp == runtime.config.local_write_stamp) {
            return;
        }
        runtime.config.global_write_stamp = global_stamp;
        runtime.config.local_write_stamp = local_stamp;

        ArenaTemp temp = begin_thread_temp_arena();
        EditorConfig effective = runtime.config.base;
        EditorConfigError global_error = {};
        EditorConfigError local_error = {};

        if (!global_path.empty() && global_stamp != 0u) {
            StrRef text = {};
            if (!read_config_file_text(*temp.arena(), global_path, text)) {
                global_error.valid = true;
                global_error.source = EditorConfigErrorSource::GLOBAL;
                copy_cstr(global_error.path, sizeof(global_error.path), global_path);
                copy_cstr(
                    global_error.message,
                    sizeof(global_error.message),
                    "Failed to read global config file. Using built-in defaults for global "
                    "settings."
                );
            } else {
                EditorConfigPatch patch = {};
                if (parse_editor_config(
                        text, global_path, EditorConfigErrorSource::GLOBAL, patch, global_error
                    )) {
                    apply_editor_config_patch(effective, patch);
                } else {
                    append_error_note(
                        global_error, " Using built-in defaults for global settings."
                    );
                }
            }
        }

        if (!local_path.empty() && local_stamp != 0u) {
            StrRef text = {};
            if (!read_config_file_text(*temp.arena(), local_path, text)) {
                local_error.valid = true;
                local_error.source = EditorConfigErrorSource::LOCAL;
                copy_cstr(local_error.path, sizeof(local_error.path), local_path);
                copy_cstr(
                    local_error.message,
                    sizeof(local_error.message),
                    global_error.valid
                        ? "Failed to read local config file. Using built-in defaults instead."
                        : "Failed to read local config file. Using global config values instead."
                );
            } else {
                EditorConfigPatch patch = {};
                if (parse_editor_config(
                        text, local_path, EditorConfigErrorSource::LOCAL, patch, local_error
                    )) {
                    apply_editor_config_patch(effective, patch);
                } else if (global_error.valid) {
                    append_error_note(local_error, " Using built-in defaults instead.");
                } else {
                    append_error_note(local_error, " Using global config values instead.");
                }
            }
        }

        apply_editor_config_patch(effective, runtime.config.session_override);
        runtime.config.effective = effective;
        apply_runtime_config(runtime);
        if (local_error.valid) {
            runtime.config.error = local_error;
            runtime.config.error_visible = true;
        } else if (global_error.valid) {
            runtime.config.error = global_error;
            runtime.config.error_visible = true;
        } else {
            clear_editor_config_error(runtime.config.error);
            runtime.config.error_visible = false;
        }
    }

    auto handle_runtime_config_request(Runtime& runtime) -> void {
        EditorConfigRequestKind const request = runtime.editor.config_request;
        char request_text[COMMAND_TEXT_CAPACITY] = {};
        size_t const request_text_size = runtime.editor.config_request_text_size;
        std::memcpy(request_text, runtime.editor.config_request_text, request_text_size);
        runtime.editor.config_request = EditorConfigRequestKind::NONE;
        runtime.editor.config_request_text_size = 0u;
        runtime.editor.config_request_text[0u] = '\0';
        if (request == EditorConfigRequestKind::NONE) {
            return;
        }

        switch (request) {
        case EditorConfigRequestKind::OPEN: {
            StrRef const local_path = runtime_config_path(runtime.config.local_path);
            StrRef const global_path = runtime_config_path(runtime.config.global_path);
            StrRef const target =
                !local_path.empty() && editor_path_exists(local_path) ? local_path : global_path;
            if (!target.empty() && ensure_config_file_exists(target)) {
                BASE_UNUSED(editor_open_path(runtime.editor, target));
            } else {
                fmt::eprintf("code_editor: failed to open config file %s\n", target);
            }
        } break;
        case EditorConfigRequestKind::RELOAD:
            reload_runtime_config(runtime, true);
            break;
        case EditorConfigRequestKind::OVERRIDE: {
            EditorConfigPatch patch = {};
            EditorConfigError error = {};
            StrRef const text(request_text, request_text_size);
            if (parse_editor_config_override(text, patch, error)) {
                merge_editor_config_patch(runtime.config.session_override, patch);
                clear_editor_config_error(runtime.config.error);
                runtime.config.error_visible = false;
                reload_runtime_config(runtime, true);
            } else {
                append_error_note(error, " Keeping the previous session override values.");
                runtime.config.error = error;
                runtime.config.error_visible = true;
            }
        } break;
        default:
            break;
        }
    }

    auto open_runtime_config_error_source(Runtime& runtime) -> void {
        EditorConfigError const& error = runtime.config.error;
        StrRef const path = StrRef(error.path);
        if (path.empty()) {
            return;
        }
        if (editor_focused_pane_kind(runtime.editor) != EditorPaneKind::CODE) {
            focus_first_code_split(runtime.editor);
        }
        if (!editor_open_path(runtime.editor, path)) {
            fmt::eprintf("code_editor: failed to open config error source %s\n", path);
            return;
        }
        size_t const line = error.line > 0u ? error.line - 1u : 0u;
        size_t const column = error.column > 0u ? error.column - 1u : 0u;
        set_editor_cursor(runtime.editor, line, column);
    }

    auto draw_config_error_popup(
        gui::Frame& ui,
        Runtime& runtime,
        Palette const& palette,
        float client_width,
        float client_height,
        gui::InputState const& input
    ) -> void {
        if (!runtime.config.error_visible || !runtime.config.error.valid) {
            return;
        }

        EditorConfigError const& error = runtime.config.error;
        StrRef const message_text = StrRef(error.message);
        StrRef const excerpt_text = StrRef(error.excerpt);
        float const width = std::clamp(
            client_width - CONFIG_ERROR_MARGIN * 2.0f,
            CONFIG_ERROR_MIN_WIDTH,
            CONFIG_ERROR_MAX_WIDTH
        );
        float const content_width = std::max(1.0f, width - 28.0f);
        size_t const message_lines = std::max<size_t>(
            1u,
            wrapped_line_count(
                runtime.ui_font, runtime.editor.font_size, message_text, content_width
            )
        );
        float const message_height = std::max(
            CONFIG_ERROR_MESSAGE_MIN_HEIGHT,
            static_cast<float>(message_lines) *
                (runtime.editor.font_size * CONFIG_ERROR_MESSAGE_LINE_HEIGHT_SCALE)
        );
        float const excerpt_height =
            excerpt_text.empty()
                ? 0.0f
                : static_cast<float>(line_count(excerpt_text)) * CONFIG_ERROR_EXCERPT_LINE_HEIGHT +
                      CONFIG_ERROR_EXCERPT_PADDING;
        float const body_content_height =
            CONFIG_ERROR_PATH_HEIGHT + message_height +
            (excerpt_text.empty() ? CONFIG_ERROR_SECTION_GAP
                                  : CONFIG_ERROR_SECTION_GAP * 2.0f + excerpt_height);
        float const body_max_height = std::max(
            1.0f,
            client_height * CONFIG_ERROR_HEIGHT_FRACTION - CONFIG_ERROR_HEADER_HEIGHT -
                CONFIG_ERROR_SECTION_GAP - CONFIG_ERROR_MARGIN * 2.0f
        );
        float const body_height = std::min(body_content_height, body_max_height);
        float const x = std::max(CONFIG_ERROR_MARGIN, client_width - width - CONFIG_ERROR_MARGIN);
        bool close = key_pressed(input, gui::Key::ESCAPE);
        bool open_source = false;
        if (auto popup = ui.popup(
                gui::id("config_error_popup"),
                {
                    .layout =
                        {
                            .width = gui::px(width),
                            .height = gui::children(),
                            .margin = gui::insets(CONFIG_ERROR_MARGIN, 0.0f, 0.0f, x),
                            .padding = gui::insets(14.0f),
                            .gap = 10.0f,
                            .align_x = gui::Align::START,
                            .align_y = gui::Align::START,
                        },
                    .style =
                        {
                            .background = gui::color_alpha(palette.panel, 0.94f),
                            .border = gui::color_alpha(palette.preprocessor, 0.82f),
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 12.0f},
                                    .blur_radius = 28.0f,
                                    .spread = 2.0f,
                                    .color = gui::rgba(0, 0, 0, 110),
                                },
                        },
                    .debug_name = "config_error_popup",
                }
            )) {
            if (auto header = ui.row(
                    gui::id("config_error_header"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(CONFIG_ERROR_HEADER_HEIGHT),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                StrRef const title = error.source == EditorConfigErrorSource::LOCAL
                                         ? StrRef("Local config error")
                                     : error.source == EditorConfigErrorSource::GLOBAL
                                         ? StrRef("Global config error")
                                         : StrRef("Session override error");
                ui.label(
                    title,
                    {
                        .layout = {.width = gui::children(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.text, .font_size = runtime.editor.font_size
                        },
                    }
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                open_source =
                    ui.button(
                          gui::id("config_error_open_source"),
                          "Go To Error",
                          {
                              .layout =
                                  {
                                      .width = gui::px(106.0f),
                                      .height = gui::fill(),
                                      .padding = gui::insets(0.0f, 10.0f),
                                  },
                              .style =
                                  {
                                      .background = gui::color_alpha(palette.panel_raised, 0.88f),
                                      .foreground = palette.cursor,
                                      .border = palette.cursor,
                                      .border_thickness = 1.0f,
                                      .radius = 4.0f,
                                      .font_size = runtime.editor.font_size,
                                  },
                          }
                    )
                        .activated ||
                    open_source;
                close =
                    ui.button(
                          gui::id("config_error_close"),
                          "Close",
                          {
                              .layout =
                                  {
                                      .width = gui::px(72.0f),
                                      .height = gui::fill(),
                                      .padding = gui::insets(0.0f, 10.0f),
                                  },
                              .style =
                                  {
                                      .background = gui::color_alpha(palette.panel_raised, 0.88f),
                                      .foreground = palette.text,
                                      .border = palette.border,
                                      .border_thickness = 1.0f,
                                      .radius = 4.0f,
                                      .font_size = runtime.editor.font_size,
                                  },
                          }
                    )
                        .activated ||
                    close;
            }
            if (auto body = ui.scroll_panel(
                    gui::id("config_error_body"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(body_height),
                                .align_x = gui::Align::STRETCH,
                            },
                        .style = {.radius = 4.0f},
                    }
                )) {
                if (auto content = ui.column(
                        gui::id("config_error_content"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::children(),
                                .gap = 10.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        }
                    )) {
                    ui.label(
                        fmt::tprintf("%s:%zu:%zu", StrRef(error.path), error.line, error.column),
                        {
                            .layout = {.width = gui::fill(), .height = gui::px(18.0f)},
                            .style = {
                                .foreground = palette.cursor,
                                .font_size = runtime.editor.font_size,
                            },
                        }
                    );
                    ui.label(
                        StrRef(error.message),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(message_height),
                                    .word_wrap = true,
                                },
                            .style = {
                                .foreground = palette.muted,
                                .font_size = runtime.editor.font_size,
                            },
                        }
                    );
                    if (!excerpt_text.empty()) {
                        if (auto excerpt = ui.column(
                                gui::id("config_error_excerpt"),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(excerpt_height),
                                            .padding = gui::insets(10.0f),
                                        },
                                    .style = {
                                        .background = gui::color_alpha(palette.panel_raised, 0.82f),
                                        .border = gui::color_alpha(palette.border, 0.65f),
                                        .border_thickness = 1.0f,
                                        .radius = 4.0f,
                                    },
                                }
                            )) {
                            ui.label(
                                excerpt_text,
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = palette.text,
                                        .font = runtime.editor_font,
                                        .font_size = CONFIG_ERROR_EXCERPT_FONT_SIZE,
                                    },
                                }
                            );
                        }
                    }
                }
            }
        }
        if (open_source) {
            open_runtime_config_error_source(runtime);
        }
        if (close) {
            runtime.config.error_visible = false;
        }
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* wide_source = nullptr;
        int wide_count = 0;
        if (!base::utf8_to_wide(text, *temp.arena(), wide_source, wide_count)) {
            CloseClipboard();
            return;
        }

        HGLOBAL const memory =
            GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * (static_cast<size_t>(wide_count) + 1u));
        if (memory == nullptr) {
            CloseClipboard();
            return;
        }

        auto* const wide_text = static_cast<wchar_t*>(GlobalLock(memory));
        if (wide_text == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return;
        }
        std::memcpy(wide_text, wide_source, sizeof(wchar_t) * static_cast<size_t>(wide_count));
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
        CloseClipboard();
    }

    [[nodiscard]] auto get_windows_clipboard_text(void* user_data, Arena& arena) -> StrRef {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return {};
        }

        HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) {
            CloseClipboard();
            return {};
        }

        auto const* const wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
        if (wide_text == nullptr) {
            CloseClipboard();
            return {};
        }

        int const wide_count = lstrlenW(wide_text);
        StrRef text = {};
        if (!base::wide_to_utf8(wide_text, wide_count, arena, text)) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        GlobalUnlock(handle);
        CloseClipboard();
        return text;
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
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->ui_font = {};
        runtime->editor_font = {};
        runtime->icon_font = {};
        runtime->branch_icon_font = {};
    }

    [[nodiscard]] auto
    create_runtime(Arena& arena, ModuleRuntimeContext const& context, Runtime* runtime) -> bool {
        draw::RendererDesc renderer_desc = {};
        renderer_desc.text_atlas_slot_count = 4096u;
        render::Result render_result = draw::create_renderer(
            arena, context.render_context, renderer_desc, runtime->draw_renderer
        );
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::ContextDesc font_desc = {};
        font_desc.backend = selected_font_backend();
        font_provider::Result font_result =
            font_provider::create_context(arena, font_desc, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->ui_font);
        font_cache::open_system_font(runtime->cache, "Segoe MDL2 Assets", runtime->icon_font);
        if (!font_cache::font_valid(runtime->icon_font)) {
            font_cache::open_system_font(runtime->cache, "Segoe Fluent Icons", runtime->icon_font);
        }
        if (!font_cache::font_valid(runtime->icon_font)) {
            runtime->icon_font = runtime->ui_font;
        }
        font_cache::open_font_file(
            runtime->cache,
            CODE_EDITOR_SOURCE_DIR "/third_party/codicons/codicon.ttf",
            runtime->branch_icon_font
        );
        if (!font_cache::font_valid(runtime->branch_icon_font)) {
            runtime->branch_icon_font = runtime->icon_font;
        }
        Slice<uint8_t const> const source_code_pro = embedded_source_code_pro_font();
        if (!source_code_pro.empty()) {
            font_cache::open_font_data(runtime->cache, source_code_pro, runtime->editor_font);
        }
        ASSERT(font_cache::font_valid(runtime->editor_font));

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);
        runtime->native_window = context.native_window;
        runtime->shared_tree_root_name = context.shared_tree_root_name;
        runtime->shared_tree_files = context.shared_tree_files;
        runtime->shared_tree_loading = context.shared_tree_loading;
        runtime->shared_file_change_generation = context.shared_file_change_generation;
        runtime->shared_git_requests = context.shared_git_requests;
        runtime->shared_git_results = context.shared_git_results;
        runtime->lsp_bridge = context.lsp_bridge;
        runtime->lsp_send_request = context.lsp_send_request;
        runtime->lsp_user_data = context.lsp_user_data;
        runtime->app_close_requested = context.app_close_requested;
        runtime->app_close_confirmed = context.app_close_confirmed;
        init_editor(arena, runtime->editor, context.initial_text);
        runtime->editor.lsp_bridge = runtime->lsp_bridge;
        runtime->editor.lsp_send_request = runtime->lsp_send_request;
        runtime->editor.lsp_user_data = runtime->lsp_user_data;
        runtime->editor.shared_tree_operation_request = context.shared_tree_operation_request;
        runtime->editor.shared_tree_operation_result = context.shared_tree_operation_result;
        if (context.shared_tree_operation_result != nullptr) {
            runtime->editor.tree_operation_generation =
                context.shared_tree_operation_result->generation;
            runtime->editor.tree_operation_seen_generation =
                context.shared_tree_operation_result->generation;
        }
        if (!context.initial_file_name.empty()) {
            runtime->editor.current_file_name = context.initial_file_name;
        }
        runtime->editor.current_file_path = context.initial_file_path;
        runtime->editor.tree_root_name = context.tree_root_name;
        runtime->editor.save_root_path = context.save_root_path;
        runtime->editor.tree_files = context.tree_files;
        runtime->editor.tree_loading =
            context.shared_tree_loading != nullptr && *context.shared_tree_loading;
        runtime->editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, context.initial_sidebar_visible);
        BASE_UNUSED(editor_global_config_path(
            runtime->config.global_path, sizeof(runtime->config.global_path)
        ));
        BASE_UNUSED(editor_local_config_path(
            runtime->editor.save_root_path,
            runtime->config.local_path,
            sizeof(runtime->config.local_path)
        ));
        runtime->config.base = {
            .palette = {},
            .raster_policy = runtime->editor.raster_policy,
            .font_size = runtime->editor.font_size,
            .sidebar_visible = runtime->editor.flag(EditorFlag::SIDEBAR_VISIBLE),
        };
        runtime->config.effective = runtime->config.base;
        reload_runtime_config(*runtime, true);
        if (context.initial_sidebar_visible && runtime->editor.flag(EditorFlag::SIDEBAR_VISIBLE)) {
            size_t const filesystem = runtime->editor.split_nodes[runtime->editor.root_split].first;
            if (editor_split_pane_kind(runtime->editor, filesystem) == EditorPaneKind::FILESYSTEM) {
                focus_editor_split(runtime->editor, filesystem);
            }
        }
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        gui::ThemeDesc const theme = code_editor_theme(
            runtime->ui_font, runtime->config.effective.palette, runtime->editor.font_size
        );
        gui::create_context(
            arena,
            {
                .initial_box_capacity = 1024u,
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .clipboard_user_data = context.native_window,
            },
            runtime->ui_context
        );
        touch_open_file(
            runtime->editor, runtime->editor.current_file_name, runtime->editor.current_file_path
        );
        return true;
    }

    [[nodiscard]] auto
    git_control_space_input(EditorState const& editor, gui::InputState const& input) -> bool {
        if (editor.sidebar_tab != EditorSidebarTab::GIT || !editor.git_control_focused ||
            editor_focused_pane_kind(editor) != EditorPaneKind::FILESYSTEM ||
            editor.git_text_editing || input.key_events == nullptr) {
            return false;
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

    [[nodiscard]] auto text_reference_mode_enabled() -> bool {
        char value[8] = {};
        DWORD const size = GetEnvironmentVariableA(
            "CODE_EDITOR_TEXT_REFERENCE_MODE", value, static_cast<DWORD>(sizeof(value))
        );
        return size != 0u && size < sizeof(value) && value[0u] != '0';
    }

    [[nodiscard]] auto text_diagnostics_path(char* buffer, size_t capacity) -> StrRef {
        DWORD const size = GetEnvironmentVariableA(
            "CODE_EDITOR_TEXT_DIAGNOSTICS_PATH", buffer, static_cast<DWORD>(capacity)
        );
        if (size == 0u || size >= capacity) {
            return {};
        }
        return StrRef(buffer, static_cast<size_t>(size));
    }

    [[nodiscard]] auto open_text_diagnostics_file(StrRef path) -> std::FILE* {
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

    auto write_text_diagnostic_json_text(std::FILE* file, StrRef text) -> void {
        std::fputc('"', file);
        for (char const ch : text) {
            uint8_t const byte = static_cast<uint8_t>(ch);
            switch (ch) {
            case '"':
                fmt::fprintf(file, "\\\"");
                break;
            case '\\':
                fmt::fprintf(file, "\\\\");
                break;
            case '\n':
                fmt::fprintf(file, "\\n");
                break;
            case '\r':
                fmt::fprintf(file, "\\r");
                break;
            case '\t':
                fmt::fprintf(file, "\\t");
                break;
            default:
                if (byte < 0x20u) {
                    fmt::fprintf(file, "\\u%04x", static_cast<unsigned>(byte));
                } else {
                    std::fputc(ch, file);
                }
                break;
            }
        }
        std::fputc('"', file);
    }

    [[nodiscard]] auto text_diagnostic_raster_policy_name(font_provider::RasterPolicy policy)
        -> char const* {
        switch (policy) {
        case font_provider::RasterPolicy::SHARP_HINTED:
            return "sharp";
        case font_provider::RasterPolicy::SMOOTH_HINTED:
            return "smooth";
        case font_provider::RasterPolicy::LCD_SHARP_HINTED:
            return "lcd_sharp";
        case font_provider::RasterPolicy::LCD_SMOOTH_HINTED:
            return "lcd_smooth";
        }
        return "unknown";
    }

    [[nodiscard]] auto text_diagnostic_raster_format_name(font_provider::RasterFormat format)
        -> char const* {
        switch (format) {
        case font_provider::RasterFormat::ALPHA:
            return "alpha";
        case font_provider::RasterFormat::LCD_RGB:
            return "lcd_rgb";
        }
        return "unknown";
    }

    [[nodiscard]] auto text_diagnostic_pixel_geometry_name(font_provider::PixelGeometry geometry)
        -> char const* {
        switch (geometry) {
        case font_provider::PixelGeometry::UNKNOWN:
            return "unknown";
        case font_provider::PixelGeometry::RGB_HORIZONTAL:
            return "rgb_horizontal";
        case font_provider::PixelGeometry::BGR_HORIZONTAL:
            return "bgr_horizontal";
        }
        return "unknown";
    }

    [[nodiscard]] auto text_diagnostic_color_format_name(font_provider::TargetColorFormat format)
        -> char const* {
        switch (format) {
        case font_provider::TargetColorFormat::RGBA8_UNORM:
            return "rgba8_unorm";
        case font_provider::TargetColorFormat::R8_UNORM:
            return "r8_unorm";
        }
        return "unknown";
    }

    auto write_text_diagnostic_rect(std::FILE* file, gui::Rect rect) -> void {
        fmt::fprintf(
            file,
            "{\"min_x\":%.3f,\"min_y\":%.3f,\"max_x\":%.3f,\"max_y\":%.3f,"
            "\"width\":%.3f,\"height\":%.3f}",
            static_cast<double>(rect.min.x),
            static_cast<double>(rect.min.y),
            static_cast<double>(rect.max.x),
            static_cast<double>(rect.max.y),
            static_cast<double>(rect.max.x - rect.min.x),
            static_cast<double>(rect.max.y - rect.min.y)
        );
    }

    auto write_text_diagnostic_color(std::FILE* file, gui::Color color) -> void {
        unsigned const r =
            static_cast<unsigned>(std::round(std::clamp(color.r, 0.0f, 1.0f) * 255.0f));
        unsigned const g =
            static_cast<unsigned>(std::round(std::clamp(color.g, 0.0f, 1.0f) * 255.0f));
        unsigned const b =
            static_cast<unsigned>(std::round(std::clamp(color.b, 0.0f, 1.0f) * 255.0f));
        unsigned const a = static_cast<unsigned>(
            std::round(std::clamp(color.a < 0.0f ? 1.0f : color.a, 0.0f, 1.0f) * 255.0f)
        );
        fmt::fprintf(file, "{\"hex\":\"#%02x%02x%02x\"", r, g, b);
        fmt::fprintf(file, ",\"r\":%u", r);
        fmt::fprintf(file, ",\"g\":%u", g);
        fmt::fprintf(file, ",\"b\":%u", b);
        fmt::fprintf(file, ",\"a\":%u}", a);
    }

    struct TextDiagnosticGlyphOrigin {
        float origin = 0.0f;
        uint8_t phase = 0u;
    };

    [[nodiscard]] auto text_diagnostic_quantize_glyph_origin(float value)
        -> TextDiagnosticGlyphOrigin {
        float const scaled =
            std::floor(value * static_cast<float>(font_provider::GLYPH_RASTER_PHASE_COUNT) + 0.5f);
        int32_t const quantized = static_cast<int32_t>(scaled);
        uint8_t const phase = static_cast<uint8_t>(
            ((quantized % font_provider::GLYPH_RASTER_PHASE_COUNT) +
             font_provider::GLYPH_RASTER_PHASE_COUNT) %
            font_provider::GLYPH_RASTER_PHASE_COUNT
        );
        return {scaled / static_cast<float>(font_provider::GLYPH_RASTER_PHASE_COUNT), phase};
    }

    auto write_text_diagnostic_lines(
        std::FILE* file,
        Runtime const& runtime,
        float text_x,
        float text_y,
        float line_height,
        size_t first_visible,
        size_t line_count
    ) -> void {
        constexpr size_t MAX_DUMP_LINES = 4u;
        constexpr size_t MAX_DUMP_GLYPHS = 96u;
        size_t visible = first_visible;
        size_t line = editor_visible_line_at(runtime.editor, visible);
        for (size_t dump_line = 0u; dump_line < MAX_DUMP_LINES && visible < line_count;
             ++dump_line) {
            if (dump_line != 0u) {
                fmt::fprintf(file, ",");
            }
            EditorLine const editor_line_value = editor_line(runtime.editor, line);
            StrRef const text = editor_line_text(editor_line_value);
            font_cache::TextRun run = {};
            font_cache::text_run(
                runtime.cache, runtime.editor_font, runtime.editor.font_size, text, run
            );

            float const y = text_y + static_cast<float>(dump_line) * line_height;
            fmt::fprintf(file, "{\"line\":%zu,\"visible_index\":%zu,\"text\":", line, visible);
            write_text_diagnostic_json_text(file, text);
            fmt::fprintf(
                file,
                ",\"text_origin\":{\"x\":%.3f,\"y\":%.3f},"
                "\"run\":{\"advance\":%.3f,\"origin_x\":%.3f,\"origin_y\":%.3f,"
                "\"baseline_y\":%.3f,\"height\":%.3f,\"size\":{\"width\":%u,\"height\":%u},"
                "\"glyph_count\":%zu,\"run_count\":%zu},\"runs\":[",
                static_cast<double>(text_x),
                static_cast<double>(y),
                static_cast<double>(run.advance),
                static_cast<double>(run.origin_x),
                static_cast<double>(run.origin_y),
                static_cast<double>(run.baseline_y),
                static_cast<double>(run.height),
                run.size.width,
                run.size.height,
                run.glyph_count,
                run.run_count
            );
            for (size_t run_index = 0u; run_index < run.run_count; ++run_index) {
                font_cache::TextBlobRun const& blob_run = run.runs[run_index];
                if (run_index != 0u) {
                    fmt::fprintf(file, ",");
                }
                fmt::fprintf(
                    file,
                    "{\"first_glyph\":%zu,\"glyph_count\":%zu,\"utf8_start\":%u,"
                    "\"utf8_end\":%u,\"script\":%u,\"bidi_level\":%u,"
                    "\"right_to_left\":%s,\"font_backend\":\"%s\"}",
                    blob_run.first_glyph,
                    blob_run.glyph_count,
                    blob_run.utf8_start,
                    blob_run.utf8_end,
                    static_cast<unsigned>(blob_run.script),
                    static_cast<unsigned>(blob_run.bidi_level),
                    blob_run.right_to_left ? "true" : "false",
                    font_provider::backend_name(blob_run.font.backend)
                );
            }
            fmt::fprintf(file, "],\"glyphs\":[");
            size_t const glyph_count = std::min(run.glyph_count, MAX_DUMP_GLYPHS);
            for (size_t glyph_index = 0u; glyph_index < glyph_count; ++glyph_index) {
                font_cache::TextGlyph const& glyph = run.glyphs[glyph_index];
                TextDiagnosticGlyphOrigin const x =
                    text_diagnostic_quantize_glyph_origin(text_x + glyph.x + glyph.offset_x);
                TextDiagnosticGlyphOrigin const glyph_y =
                    text_diagnostic_quantize_glyph_origin(y + run.baseline_y - glyph.offset_y);
                font_provider::GlyphRaster const raster = font_cache::glyph_raster(
                    runtime.editor_font, glyph, runtime.editor.raster_policy, x.phase, glyph_y.phase
                );
                if (glyph_index != 0u) {
                    fmt::fprintf(file, ",");
                }
                fmt::fprintf(
                    file,
                    "{\"glyph_id\":%u,\"run_index\":%u,\"advance\":%.3f,\"x\":%.3f,"
                    "\"offset_x\":%.3f,\"offset_y\":%.3f,\"cluster\":%u,"
                    "\"utf8_start\":%u,\"utf8_end\":%u,\"raster_policy\":\"%s\","
                    "\"phase_x\":%u,\"phase_y\":%u,\"raster\":{\"width\":%u,\"height\":%u,"
                    "\"stride\":%u,\"format\":\"%s\",\"offset_x\":%.3f,\"offset_y\":%.3f}}",
                    static_cast<unsigned>(glyph.glyph_index),
                    static_cast<unsigned>(glyph.run_index),
                    static_cast<double>(glyph.advance),
                    static_cast<double>(glyph.x),
                    static_cast<double>(glyph.offset_x),
                    static_cast<double>(glyph.offset_y),
                    glyph.cluster,
                    glyph.utf8_start,
                    glyph.utf8_end,
                    text_diagnostic_raster_policy_name(runtime.editor.raster_policy),
                    static_cast<unsigned>(x.phase),
                    static_cast<unsigned>(glyph_y.phase),
                    raster.size.width,
                    raster.size.height,
                    raster.stride,
                    text_diagnostic_raster_format_name(raster.format),
                    static_cast<double>(raster.offset_x),
                    static_cast<double>(raster.offset_y)
                );
            }
            fmt::fprintf(file, "]}");
            visible += 1u;
            line = editor_next_visible_line(runtime.editor, line);
        }
    }

    auto write_code_editor_text_diagnostics(
        Runtime const& runtime,
        render::SizeU32 window_size,
        Palette const& palette,
        bool selection_visible
    ) -> void {
        char path_buffer[4096] = {};
        StrRef const path = text_diagnostics_path(path_buffer, sizeof(path_buffer));
        if (path.empty()) {
            return;
        }
        std::FILE* const file = open_text_diagnostics_file(path);
        if (file == nullptr) {
            return;
        }

        font_provider::Metrics metrics = {};
        font_cache::metrics_from_font(runtime.editor_font, runtime.editor.font_size, metrics);
        font_provider::SurfaceProps const surface_props = {};
        size_t const focused = runtime.editor.focused_split;
        gui::Rect const surface = focused < runtime.editor.split_nodes.size()
                                      ? runtime.editor.split_nodes[focused].rect
                                      : gui::Rect{};
        gui::Rect const content = editor_content_rect(surface);
        float const line_height = editor_line_height(runtime.editor);
        size_t const visible_line_count = editor_visible_line_count(runtime.editor);
        size_t const first_visible =
            visible_line_count == 0u
                ? 0u
                : std::min(
                      visible_line_count - 1u,
                      static_cast<size_t>(runtime.editor.scroll_y / std::max(1.0f, line_height))
                  );
        float const first_y = content.min.y - (runtime.editor.scroll_y -
                                               static_cast<float>(first_visible) * line_height);
        float const text_x = editor_text_x(runtime.editor, surface);
        float const text_y = first_y - 2.0f;
        int const crop_x = static_cast<int>(std::round(text_x));
        int const crop_y = static_cast<int>(std::round(text_y));
        int const crop_width = 620;
        int const crop_height = 190;
        size_t const viewport_line_count = static_cast<size_t>(
            std::ceil(std::max(0.0f, content.max.y - first_y) / std::max(1.0f, line_height))
        );
        bool const reference_mode = text_reference_mode_enabled();

        fmt::fprintf(
            file,
            "{\"source\":\"code_editor\",\"font_size\":%.3f,\"line_height\":%.3f,"
            "\"font_backend\":\"%s\",\"requested_raster_policy\":\"%s\","
            "\"resolved_raster_policy\":\"%s\",\"window\":{\"client_width\":%u,"
            "\"client_height\":%u},\"surface_rect\":",
            static_cast<double>(runtime.editor.font_size),
            static_cast<double>(line_height),
            font_provider::backend_name(runtime.provider.backend),
            text_diagnostic_raster_policy_name(runtime.editor.raster_policy),
            text_diagnostic_raster_policy_name(runtime.editor.raster_policy),
            window_size.width,
            window_size.height
        );
        write_text_diagnostic_rect(file, surface);
        fmt::fprintf(file, ",\"content_rect\":");
        write_text_diagnostic_rect(file, content);
        fmt::fprintf(
            file,
            ",\"text_origin\":{\"x\":%.3f,\"y\":%.3f,\"relative_crop_x\":0.0,"
            "\"relative_crop_y\":0.0},\"crop\":{\"x\":%d,\"y\":%d,\"width\":%d,"
            "\"height\":%d,\"scale\":1,\"origin\":\"text_origin\"},"
            "\"font_metrics\":{\"ascent\":%.3f,\"descent\":%.3f,\"line_gap\":%.3f,"
            "\"capital_height\":%.3f,\"space_advance\":%.3f,\"m_advance\":%.3f},"
            "\"scroll\":{\"x\":%.3f,\"y\":%.3f},\"visible_lines\":{\"total\":%zu,"
            "\"first_visible_index\":%zu,\"first_line\":%zu,\"viewport_count\":%zu},"
            "\"surface_props\":{\"pixel_geometry\":\"%s\",\"color_format\":\"%s\","
            "\"text_gamma\":%.3f,\"text_contrast\":%.3f},\"contamination\":{"
            "\"reference_mode\":%s,\"selection_visible\":%s,\"current_line_visible\":%s,"
            "\"caret_visible\":%s,\"selection_active\":%s,\"line_numbers_in_crop\":false,"
            "\"crop_origin\":\"text_origin\"},\"colors\":{\"background\":",
            static_cast<double>(text_x),
            static_cast<double>(text_y),
            crop_x,
            crop_y,
            crop_width,
            crop_height,
            static_cast<double>(metrics.ascent),
            static_cast<double>(metrics.descent),
            static_cast<double>(metrics.line_gap),
            static_cast<double>(metrics.capital_height),
            static_cast<double>(
                font_cache::text_advance(runtime.editor_font, runtime.editor.font_size, " ")
            ),
            static_cast<double>(runtime.char_width),
            static_cast<double>(runtime.editor.scroll_x),
            static_cast<double>(runtime.editor.scroll_y),
            visible_line_count,
            first_visible,
            visible_line_count == 0u ? 0u : editor_visible_line_at(runtime.editor, first_visible),
            viewport_line_count,
            text_diagnostic_pixel_geometry_name(surface_props.pixel_geometry),
            text_diagnostic_color_format_name(surface_props.color_format),
            static_cast<double>(surface_props.text_gamma),
            static_cast<double>(surface_props.text_contrast),
            reference_mode ? "true" : "false",
            selection_visible ? "true" : "false",
            selection_visible ? "true" : "false",
            selection_visible ? "true" : "false",
            runtime.editor.flag(EditorFlag::SELECTION_ACTIVE) ? "true" : "false"
        );
        write_text_diagnostic_color(file, palette.panel);
        fmt::fprintf(file, ",\"text\":");
        write_text_diagnostic_color(file, palette.text);
        fmt::fprintf(file, ",\"keyword\":");
        write_text_diagnostic_color(file, palette.keyword);
        fmt::fprintf(file, ",\"type\":");
        write_text_diagnostic_color(file, palette.type);
        fmt::fprintf(file, ",\"string\":");
        write_text_diagnostic_color(file, palette.string);
        fmt::fprintf(file, ",\"number\":");
        write_text_diagnostic_color(file, palette.number);
        fmt::fprintf(file, ",\"comment\":");
        write_text_diagnostic_color(file, palette.comment);
        fmt::fprintf(file, ",\"preprocessor\":");
        write_text_diagnostic_color(file, palette.preprocessor);
        fmt::fprintf(file, ",\"punctuation\":");
        write_text_diagnostic_color(file, palette.punctuation);
        fmt::fprintf(file, "},\"lines\":[");
        if (visible_line_count != 0u) {
            write_text_diagnostic_lines(
                file, runtime, text_x, text_y, line_height, first_visible, visible_line_count
            );
        }
        fmt::fprintf(file, "]}\n");
        std::fclose(file);
    }

    [[nodiscard]] auto build_ui_commands(
        Runtime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        bool files_changed
    ) -> gui::Frame {
        sync_git_worker_results(*runtime);
        reload_runtime_config(*runtime, false);
        sync_tree_operation_result(runtime->editor);
        if (files_changed) {
            update_open_file_changes(runtime->editor);
        }
        if (runtime->app_close_requested != nullptr && *runtime->app_close_requested) {
            runtime->editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
            *runtime->app_close_requested = false;
        }
        bool const popup_open = editor_focused_pane_kind(runtime->editor) == EditorPaneKind::CODE &&
                                (runtime->editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING) ||
                                 runtime->editor.flag(EditorFlag::FILE_DELETED_ON_DISK) ||
                                 runtime->editor.close_intent != EditorCloseIntent::NONE);
        bool const suppress_git_control_space =
            !popup_open && git_control_space_input(runtime->editor, input);
        if (!popup_open) {
            update_editor_lsp_document(runtime->editor);
            process_editor_input(
                runtime->editor,
                input,
                {
                    .set_clipboard_text = set_windows_clipboard_text,
                    .get_clipboard_text = get_windows_clipboard_text,
                    .user_data = runtime->native_window,
                }
            );
            update_editor_lsp_document(runtime->editor);
        }
        gui::InputState ui_input = input;
        if (suppress_git_control_space) {
            ui_input.key_event_count = 0u;
        }
        submit_git_worker_requests(*runtime, window_size.height);
        if (runtime->editor.git_commits_loading || runtime->editor.git_operation_pending ||
            runtime->editor.tree_loading) {
            runtime->editor.git_loading_phase += delta_time * 1.35f;
            while (runtime->editor.git_loading_phase >= 1.0f) {
                runtime->editor.git_loading_phase -= 1.0f;
            }
        }
        handle_runtime_config_request(*runtime);
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        Palette const& palette = runtime->config.effective.palette;
        gui::ThemeDesc const theme =
            code_editor_theme(runtime->ui_font, palette, runtime->editor.font_size);
        gui::set_theme(runtime->ui_context, theme);

        gui::Frame ui = gui::begin_frame(
            runtime->ui_context,
            {
                .size =
                    {
                        static_cast<float>(window_size.width),
                        static_cast<float>(window_size.height),
                    },
                .delta_time = delta_time,
                .input = ui_input,
            }
        );
        draw_editor_ui(
            ui,
            runtime->editor,
            runtime->editor_font,
            runtime->ui_font,
            runtime->icon_font,
            runtime->branch_icon_font,
            palette,
            static_cast<float>(window_size.width),
            static_cast<float>(window_size.height),
            runtime->char_width,
            ui_input
        );
        draw_config_error_popup(
            ui,
            *runtime,
            palette,
            static_cast<float>(window_size.width),
            static_cast<float>(window_size.height),
            ui_input
        );
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        bool const search_open = runtime->editor.flag(EditorFlag::FILE_SEARCH_OPEN) ||
                                 runtime->editor.flag(EditorFlag::BUFFER_SEARCH_OPEN) ||
                                 runtime->editor.flag(EditorFlag::JUMP_LIST_OPEN);
        if (search_open) {
            draw::LayerDesc backdrop = {};
            backdrop.bounds = {
                {0.0f, 0.0f},
                {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}
            };
            backdrop.filter_kind = draw::FilterKind::BLUR;
            backdrop.filter_radius = FILE_SEARCH_BACKDROP_BLUR_RADIUS;
            draw::push_layer(runtime->draw_context, backdrop);
        }
        gui::render_frame_base(ui, runtime->draw_context);
        bool const editor_selection_visible = !search_open && !text_reference_mode_enabled();
        if (!runtime->editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            draw_editor_surface(
                runtime->draw_context,
                runtime->editor_font,
                runtime->ui_font,
                runtime->editor,
                runtime->char_width,
                ui,
                search_open || runtime->editor.lsp_popup == EditorLspPopupKind::RENAME
                    ? gui::InputState{}
                    : ui_input,
                palette,
                editor_selection_visible
            );
        }
        if (search_open) {
            draw::pop_layer(runtime->draw_context);
            draw::draw_rect_filled(
                runtime->draw_context,
                {{0.0f, 0.0f},
                 {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}},
                {0.0f, 0.0f, 0.0f, FILE_SEARCH_BACKDROP_DIM_ALPHA},
                0.0f
            );
        }
        gui::render_frame_floating(ui, runtime->draw_context);
        draw::end_frame(runtime->draw_context);
        write_code_editor_text_diagnostics(
            *runtime, window_size, palette, editor_selection_visible
        );
        return ui;
    }

    struct ModuleRuntime {
        Arena arena = {};
        Runtime runtime = {};
    };

    auto request_window_close(Runtime& runtime) -> void {
        if (!runtime.editor.flag(EditorFlag::CLOSE_APP_CONFIRMED)) {
            return;
        }
        runtime.editor.set_flag(EditorFlag::CLOSE_APP_CONFIRMED, false);
        if (runtime.app_close_confirmed != nullptr) {
            *runtime.app_close_confirmed = true;
        }
        if (runtime.native_window != nullptr) {
            PostMessageW(static_cast<HWND>(runtime.native_window), WM_CLOSE, 0u, 0l);
        }
    }

    [[nodiscard]] auto draw_command_counts(draw::Context context) -> DrawCommandCounts {
        return {
            .command_count = draw::command_count(context),
            .primitive_count = draw::primitive_command_count(context),
            .batch_count = draw::primitive_batch_count(context),
            .styled_rect_count = draw::styled_rect_command_count(context),
            .text_count = draw::text_command_count(context),
            .layer_count = draw::layer_command_count(context),
        };
    }

    [[nodiscard]] auto module_create(void* storage, void* user_data) -> bool {
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = new (storage) ModuleRuntime{};
        module->arena.init();
        if (!create_runtime(module->arena, *context, &module->runtime)) {
            destroy_runtime(context->render_context, &module->runtime);
            module->~ModuleRuntime();
            return false;
        }
        return true;
    }

    auto module_destroy(void* storage, void* user_data) -> void {
        if (storage == nullptr) {
            return;
        }
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = static_cast<ModuleRuntime*>(storage);
        destroy_runtime(context->render_context, &module->runtime);
        module->~ModuleRuntime();
    }

    [[nodiscard]] auto module_render_frame(
        void* storage,
        render::Context render_context,
        render::Window render_window,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> FrameResult {
        auto* const module = static_cast<ModuleRuntime*>(storage);
        bool const files_changed = sync_shared_file_tree(module->runtime);
        uint64_t const state_hash_before = editor_state_hash(module->runtime.editor);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame =
            build_ui_commands(&module->runtime, window_size, input, delta_time, files_changed);
        request_window_close(module->runtime);
        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.05f, 0.07f, 0.09f, 1.0f};

        frame_result.render_result = draw::render_commands_to_window(
            module->runtime.draw_renderer, render_context, pass_desc, module->runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(module->runtime.draw_context);
        frame_result.redraw_pending =
            frame_result.frame.redraw_requested() || module->runtime.editor.git_commits_loading ||
            module->runtime.editor.git_operation_pending || module->runtime.editor.tree_loading ||
            editor_state_hash(module->runtime.editor) != state_hash_before;
        reset_thread_temp_arenas();
        return frame_result;
    }

    [[nodiscard]] auto code_editor_module_api() -> ModuleApi const* {
        static ModuleApi const API = {
            .hot_reload =
                {
                    .version = gui::HOT_RELOAD_API_VERSION,
                    .runtime_size = sizeof(ModuleRuntime),
                    .runtime_alignment = alignof(ModuleRuntime),
                    .create = module_create,
                    .destroy = module_destroy,
                },
            .render_frame = module_render_frame,
        };
        return &API;
    }
#else
    auto run_console_fallback() -> int {
        fmt::printf("code_editor: windowed editor example is Windows-only\n");
        return 0;
    }
#endif

} // namespace code_editor

#if defined(_WIN32) && defined(CODE_EDITOR_MODULE)
GUI_HOT_RELOAD_EXPORT auto code_editor_get_module_api() -> code_editor::ModuleApi const* {
    return code_editor::code_editor_module_api();
}
#endif
