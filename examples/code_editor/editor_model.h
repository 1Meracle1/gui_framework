#pragma once

#include "shared.h"
#include "text_buffer.h"

#include <base/memory.h>
#include <base/str_ref.h>
#include <base/vec.h>
#include <cstddef>
#include <cstdint>
#include <gui/gui.h>

namespace code_editor {

    struct EditorUndoEntry;

    inline constexpr float EDITOR_FONT_SIZE = 14.0f;
    inline constexpr float EDITOR_MIN_FONT_SIZE = 8.0f;
    inline constexpr float EDITOR_MAX_FONT_SIZE = 24.0f;
    inline constexpr float EDITOR_FONT_SIZE_STEP = 1.0f;
    inline constexpr float EDITOR_UI_FONT_SIZE = 13.0f;
    inline constexpr float EDITOR_LINE_HEIGHT = 21.0f;
    inline constexpr float EDITOR_PADDING_X = 18.0f;
    inline constexpr float EDITOR_PADDING_Y = 14.0f;
    inline constexpr float LINE_NUMBER_WIDTH = 54.0f;
    inline constexpr float SIDEBAR_DEFAULT_WIDTH_PERCENT = 220.0f / 1320.0f;
    inline constexpr float SIDEBAR_MIN_WIDTH_PERCENT = 0.12f;
    inline constexpr float SIDEBAR_MAX_WIDTH_PERCENT = 0.42f;
    inline constexpr StrRef SCRATCH_FILE_NAME = "scratch";
    inline constexpr size_t FILE_SEARCH_TEXT_CAPACITY = 128u;
    inline constexpr size_t FILE_SEARCH_RESULT_LIMIT = 16u;
    inline constexpr size_t FILE_SEARCH_NO_FILE = static_cast<size_t>(-1);
    inline constexpr size_t SAVE_PATH_TEXT_CAPACITY = 1024u;

    struct EditorSelectionRange {
        size_t start_line = 0u;
        size_t start_column = 0u;
        size_t end_line = 0u;
        size_t end_column = 0u;
        bool active = false;
        bool full_line = false;
    };

    enum class EditorSelectionMode : uint8_t {
        NONE,
        CHARACTER,
        LINE,
    };

    enum class EditorSavePathError : uint8_t {
        NONE,
        EMPTY,
        EXISTS,
        WRITE_FAILED,
    };

    struct OpenFile {
        StrRef name = {};
        StrRef path = {};
        StrRef text = {};
        StrRef saved_text = {};
        uint64_t file_write_stamp = 0u;
        bool text_valid = false;
        bool dirty = false;
        bool external_change_pending = false;
    };

    struct FileSearchMatch {
        size_t tree_file_index = 0u;
        int32_t score = 0;
    };

    enum class EditorPaneKind : uint8_t {
        CODE,
        FILESYSTEM,
    };

    struct EditorPane {
        EditorPaneKind kind = EditorPaneKind::CODE;
        EditorText text = {};
        StrRef current_file_name = SCRATCH_FILE_NAME;
        StrRef current_file_path = {};
        StrRef scratch_text = {};
        StrRef saved_text = {};
        EditorUndoEntry* undo_stack = nullptr;
        EditorUndoEntry* redo_stack = nullptr;
        uint64_t file_write_stamp = 0u;
        size_t cursor_line = 0u;
        size_t cursor_column = 0u;
        size_t preferred_column = 0u;
        size_t selection_anchor_line = 0u;
        size_t selection_anchor_column = 0u;
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float scroll_y = 0.0f;
        bool insert_mode = false;
        bool selection_active = false;
        bool mouse_selecting = false;
        bool mouse_was_down = false;
        bool dirty = false;
        bool external_change_pending = false;
    };

    enum class EditorSplitKind : uint8_t {
        LEAF,
        VERTICAL,
        HORIZONTAL,
    };

    struct EditorSplitNode {
        EditorSplitKind kind = EditorSplitKind::LEAF;
        size_t parent = static_cast<size_t>(-1);
        size_t first = static_cast<size_t>(-1);
        size_t second = static_cast<size_t>(-1);
        size_t pane = 0u;
        float ratio = 0.5f;
        gui::Rect rect = {};
    };

    struct EditorState {
        EditorText text = {};
        StrRef current_file_name = SCRATCH_FILE_NAME;
        StrRef current_file_path = {};
        StrRef scratch_text = {};
        StrRef tree_root_name = {};
        StrRef save_root_path = {};
        StrRef saved_text = {};
        Arena* arena = nullptr;
        Vec<EditorPane*> panes = {};
        Vec<EditorSplitNode> split_nodes = {};
        Vec<OpenFile> open_files = {};
        Slice<FileTreeEntry> tree_files = {};
        EditorUndoEntry* undo_stack = nullptr;
        EditorUndoEntry* redo_stack = nullptr;
        uint64_t file_write_stamp = 0u;
        size_t root_split = 0u;
        size_t focused_split = 0u;
        size_t cursor_line = 0u;
        size_t cursor_column = 0u;
        size_t preferred_column = 0u;
        size_t selection_anchor_line = 0u;
        size_t selection_anchor_column = 0u;
        EditorSelectionMode selection_mode = EditorSelectionMode::NONE;
        float font_size = EDITOR_FONT_SIZE;
        float scroll_y = 0.0f;
        float sidebar_width_percent = SIDEBAR_DEFAULT_WIDTH_PERCENT;
        float sidebar_resize_grab_x = 0.0f;
        char file_search_text[FILE_SEARCH_TEXT_CAPACITY] = {};
        char save_path_text[SAVE_PATH_TEXT_CAPACITY] = {};
        size_t file_search_text_size = 0u;
        size_t file_search_selected = 0u;
        size_t file_search_open_file = FILE_SEARCH_NO_FILE;
        size_t pending_line_number = 0u;
        EditorSavePathError save_path_error = EditorSavePathError::NONE;
        bool insert_mode = false;
        bool sidebar_visible = false;
        bool sidebar_resizing = false;
        bool tree_open = true;
        bool file_search_open = false;
        bool save_requested = false;
        bool save_path_open = false;
        bool pending_line_number_active = false;
        bool pending_leader = false;
        bool pending_buffer = false;
        bool pending_window = false;
        bool pending_g = false;
        bool pending_d = false;
        bool pending_r = false;
        bool close_current_requested = false;
        bool pane_loaded = false;
        bool selection_active = false;
        bool mouse_selecting = false;
        bool mouse_was_down = false;
        bool dirty = false;
        bool external_change_pending = false;
    };

    struct EditorClipboard {
        gui::SetClipboardTextFn set_clipboard_text = nullptr;
        gui::GetClipboardTextFn get_clipboard_text = nullptr;
        void* user_data = nullptr;
    };

    auto init_editor(Arena& arena, EditorState& editor, StrRef text) -> void;
    auto set_editor_text(EditorState& editor, StrRef text) -> void;
    auto remember_open_file(EditorState& editor, StrRef name, StrRef path) -> void;
    [[nodiscard]] auto load_shared_editor_buffer(EditorState& editor, StrRef name, StrRef path)
        -> bool;
    auto save_scratch_file(EditorState& editor) -> void;
    auto open_scratch_file(EditorState& editor) -> void;
    auto mark_editor_saved(EditorState& editor) -> void;
    auto open_save_path_popup(EditorState& editor) -> void;
    auto close_save_path_popup(EditorState& editor) -> void;
    auto ensure_filesystem_panel(EditorState& editor) -> void;
    auto focus_first_code_split(EditorState& editor) -> void;
    auto focus_editor_split(EditorState& editor, size_t split) -> void;
    auto set_editor_split_rect(EditorState& editor, size_t split, gui::Rect rect) -> void;
    [[nodiscard]] auto editor_split_leaf_count(EditorState const& editor) -> size_t;
    [[nodiscard]] auto editor_focused_pane(EditorState const& editor) -> size_t;
    [[nodiscard]] auto editor_focused_pane_kind(EditorState const& editor) -> EditorPaneKind;
    [[nodiscard]] auto editor_split_pane_kind(EditorState const& editor, size_t split)
        -> EditorPaneKind;
    auto process_editor_input(
        EditorState& editor, gui::InputState const& input, EditorClipboard clipboard = {}
    ) -> void;
    [[nodiscard]] auto editor_line_count(EditorState const& editor) -> size_t;
    [[nodiscard]] auto editor_line(EditorState const& editor, size_t index) -> EditorLine;
    [[nodiscard]] auto editor_line_text(EditorLine line) -> StrRef;
    [[nodiscard]] auto editor_selection_range(EditorState const& editor) -> EditorSelectionRange;
    [[nodiscard]] auto editor_file_search_text(EditorState const& editor) -> StrRef;
    [[nodiscard]] auto file_search_entry_text(FileTreeEntry const& entry) -> StrRef;
    [[nodiscard]] auto
    collect_file_search_matches(EditorState const& editor, Slice<FileSearchMatch> matches)
        -> size_t;
    [[nodiscard]] auto point_in_rect(gui::Rect rect, gui::Vec2 point) -> bool;
    [[nodiscard]] auto editor_scaled_font_size(EditorState const& editor, float base_size) -> float;
    [[nodiscard]] auto editor_line_height(EditorState const& editor) -> float;
    [[nodiscard]] auto editor_content_rect(gui::Rect rect) -> gui::Rect;
    [[nodiscard]] auto editor_text_x(EditorState const& editor, gui::Rect rect) -> float;
    auto clamp_scroll(EditorState& editor, gui::Rect rect) -> void;
    auto reveal_cursor(EditorState& editor, gui::Rect rect) -> void;
    auto update_cursor_from_mouse(
        EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width, bool select
    ) -> void;
    auto
    select_word_from_mouse(EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width)
        -> void;
    auto
    select_line_from_mouse(EditorState& editor, gui::Rect rect, gui::Vec2 mouse, float char_width)
        -> void;
    [[nodiscard]] auto editor_state_hash(EditorState const& editor) -> uint64_t;

} // namespace code_editor
