#pragma once

#include "git.h"
#include "lsp.h"

#include <algorithm>
#include <base/config.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/spsc_queue.h>
#include <base/str_ref.h>
#include <base/unicode.h>
#include <cstddef>
#include <cstdint>
#include <gui/hot_reload_app.h>

namespace code_editor {

#if defined(_WIN32) && BASE_DEBUG
    inline constexpr bool HOT_RELOAD_ENABLED = true;
#else
    inline constexpr bool HOT_RELOAD_ENABLED = false;
#endif

    inline constexpr uint32_t INITIAL_WINDOW_WIDTH = 1320u;
    inline constexpr uint32_t INITIAL_WINDOW_HEIGHT = 820u;
    inline constexpr size_t MAX_KEY_EVENTS_PER_FRAME = 64u;
    inline constexpr size_t MODULE_STORAGE_SIZE = 128u * 1024u;
    inline constexpr size_t MODULE_STORAGE_ALIGNMENT = 64u;
    inline constexpr StrRef MODULE_FILE_NAME = "code_editor_module.dll";
    inline constexpr uint32_t HOT_RELOAD_POLL_MS = 250u;
    inline constexpr size_t FILE_DROP_PATH_CAPACITY = 4096u;

    [[nodiscard]] inline auto hex_digit(uint8_t value) -> char {
        return static_cast<char>(value < 10u ? '0' + value : 'a' + value - 10u);
    }

    inline auto append_hex_byte(char*& out, uint8_t value) -> void {
        *out++ = hex_digit(value >> 4u);
        *out++ = hex_digit(value & 0x0fu);
    }

    inline auto append_hex_u32(char*& out, uint32_t value) -> void {
        for (int32_t shift = 28; shift >= 0; shift -= 4) {
            *out++ = hex_digit(static_cast<uint8_t>((value >> shift) & 0x0fu));
        }
    }

    [[nodiscard]] inline auto editor_text_supported(StrRef text) -> bool {
        size_t offset = 0u;
        while (offset < text.size()) {
            uint8_t const byte = static_cast<uint8_t>(text[offset]);
            if (byte < 0x80u) {
                if ((byte >= 0x20u && byte < 0x7fu) || byte == '\n' || byte == '\r' ||
                    byte == '\t') {
                    offset += 1u;
                    continue;
                }
                return false;
            }

            if (!base::utf8_codepoint_valid(text, offset)) {
                return false;
            }
            offset += base::utf8_codepoint_size(text, offset);
        }
        return true;
    }

    [[nodiscard]] inline auto editor_display_text(Arena& arena, StrRef text) -> StrRef {
        if (text.starts_with("\xef\xbb\xbf")) {
            text.remove_prefix(3u);
        }
        if (editor_text_supported(text)) {
            return text;
        }

        size_t const line_count = (text.size() + 15u) / 16u;
        char* const display = arena_alloc<char>(arena, line_count * 58u + 1u);
        char* out = display;
        for (size_t offset = 0u; offset < text.size(); offset += 16u) {
            append_hex_u32(out, static_cast<uint32_t>(offset));
            *out++ = ' ';
            *out++ = ' ';
            size_t const end = std::min(offset + 16u, text.size());
            for (size_t index = offset; index < end; ++index) {
                append_hex_byte(out, static_cast<uint8_t>(text[index]));
                if (index + 1u < end) {
                    *out++ = ' ';
                }
            }
            *out++ = '\n';
        }
        *out = '\0';
        return StrRef(display, static_cast<size_t>(out - display));
    }

    struct FileTreeEntry {
        StrRef name = {};
        StrRef path = {};
        StrRef relative_path = {};
        size_t depth = 0u;
        bool is_directory = false;
        bool open = false;
        bool file_search_visible = true;
    };

    struct FileDropRequest {
        char path[FILE_DROP_PATH_CAPACITY] = {};
        gui::Vec2 pos = {};
        uint64_t generation = 0u;
    };

    inline constexpr size_t TREE_OPERATION_PATH_CAPACITY = 2048u;

    enum class TreeOperationKind : uint8_t {
        NONE,
        RENAME,
        CREATE_FILE,
        CREATE_DIRECTORY,
        REMOVE,
        UNDO,
        REDO,
    };

    enum class TreeOperationUpdateKind : uint8_t {
        NONE,
        RENAME,
        CREATE,
        REMOVE,
        RESTORE,
    };

    struct TreeOperationRequest {
        uint64_t generation = 0u;
        TreeOperationKind kind = TreeOperationKind::NONE;
        char source_path[TREE_OPERATION_PATH_CAPACITY] = {};
        char target_path[TREE_OPERATION_PATH_CAPACITY] = {};
    };

    struct TreeOperationResult {
        uint64_t generation = 0u;
        TreeOperationKind request_kind = TreeOperationKind::NONE;
        TreeOperationUpdateKind update_kind = TreeOperationUpdateKind::NONE;
        bool success = false;
        char source_path[TREE_OPERATION_PATH_CAPACITY] = {};
        char target_path[TREE_OPERATION_PATH_CAPACITY] = {};
    };

    enum class GitWorkKind : uint8_t {
        NONE,
        REFRESH,
        COMMIT_PAGE,
        COMMIT_FILES,
        STAGE,
        STAGE_ALL,
        UNSTAGE,
        UNSTAGE_ALL,
        COMMIT,
        PUSH,
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

    struct GitWorkRequest {
        GitWorkKind kind = GitWorkKind::NONE;
        uint64_t generation = 0u;
        size_t offset = 0u;
        size_t count = 0u;
        size_t limit = 0u;
        GitStatusScope scope = GitStatusScope::UNSTAGED;
        StrRef save_root = {};
        StrRef root = {};
        StrRef path = {};
        StrRef commit_oid = {};
        StrRef message_text = {};
        StrRef branch = {};
    };

    struct GitWorkResult {
        GitWorkKind kind = GitWorkKind::NONE;
        uint64_t generation = 0u;
        size_t offset = 0u;
        size_t count = 0u;
        size_t limit = 0u;
        size_t pending_pull_count = 0u;
        size_t pending_push_count = 0u;
        GitStatusScope scope = GitStatusScope::UNSTAGED;
        GitOperationState operation_state = GitOperationState::NONE;
        StrRef root = {};
        StrRef current_branch = {};
        StrRef path = {};
        StrRef commit_oid = {};
        StrRef patch = {};
        StrRef message = {};
        Vec<GitStatusItem> status_items = {};
        Vec<GitCommit> commits = {};
        Vec<GitCommitFile> commit_files = {};
        Vec<GitBranch> branches = {};
        bool ok = false;
        bool log_loaded = false;
    };

    struct ModuleRuntimeContext {
        gui::render::Context render_context = {};
        void* native_window = nullptr;
        StrRef initial_text = {};
        StrRef initial_file_name = {};
        StrRef initial_file_path = {};
        StrRef tree_root_name = {};
        StrRef save_root_path = {};
        StrRef state_cache_path = {};
        Slice<FileTreeEntry> tree_files = {};
        StrRef const* shared_tree_root_name = nullptr;
        Slice<FileTreeEntry>* shared_tree_files = nullptr;
        bool const* shared_tree_loading = nullptr;
        uint64_t const* shared_file_change_generation = nullptr;
        FileDropRequest const* shared_file_drop_request = nullptr;
        SpscQueue<GitWorkRequest>* shared_git_requests = nullptr;
        SpscQueue<GitWorkResult>* shared_git_results = nullptr;
        TreeOperationRequest* shared_tree_operation_request = nullptr;
        TreeOperationResult* shared_tree_operation_result = nullptr;
        LspBridge const* lsp_bridge = nullptr;
        LspSendEditorRequestFn lsp_send_request = nullptr;
        LspControlFn lsp_control = nullptr;
        void* lsp_user_data = nullptr;
        void* lsp_control_user_data = nullptr;
        bool* app_close_requested = nullptr;
        bool* app_close_confirmed = nullptr;
        bool initial_sidebar_visible = false;
    };

    using DrawCommandCounts = gui::HotReloadDrawCommandCounts;
    using FrameResult = gui::HotReloadFrameResult;
    using ModuleApi = gui::HotReloadAppApi;

} // namespace code_editor
