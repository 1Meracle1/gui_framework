#include "editor_model.h"

#include <base/string_buffer.h>
#include <test/test.h>

namespace {

    using code_editor::EditorFlag;

    struct ClipboardCapture {
        char text[128] = {};
        size_t text_size = 0u;
        size_t call_count = 0u;
        size_t read_count = 0u;
    };

    auto capture_clipboard_text(void* user_data, StrRef text) -> void {
        auto* const clipboard = static_cast<ClipboardCapture*>(user_data);
        clipboard->text_size = text.copy_to(clipboard->text, sizeof(clipboard->text));
        clipboard->call_count += 1u;
    }

    auto read_clipboard_text(void* user_data, Arena&) -> StrRef {
        auto* const clipboard = static_cast<ClipboardCapture*>(user_data);
        clipboard->read_count += 1u;
        return StrRef(clipboard->text, clipboard->text_size);
    }

    auto append_lines(StringBuffer& buffer, size_t count) -> void {
        for (size_t index = 0u; index < count; ++index) {
            buffer.write_string("line\n");
        }
    }

    auto insert_expected_byte(char* text, size_t& size, size_t offset, char ch) -> void {
        for (size_t index = size; index > offset; --index) {
            text[index] = text[index - 1u];
        }
        text[offset] = ch;
        size += 1u;
    }

    auto send_text(
        code_editor::EditorState& editor,
        StrRef text,
        gui::KeyMods mods = gui::KEY_MOD_NONE,
        code_editor::EditorClipboard clipboard = {}
    ) -> void {
        for (char ch : text) {
            gui::KeyEvent const event = {
                .kind = gui::KeyEventKind::TEXT,
                .mods = mods,
                .codepoint = static_cast<uint32_t>(static_cast<unsigned char>(ch)),
            };
            gui::InputState const input = {.key_events = &event, .key_event_count = 1u};
            code_editor::process_editor_input(editor, input, clipboard);
        }
    }

    auto press_key(
        code_editor::EditorState& editor,
        gui::Key key,
        gui::KeyMods mods = gui::KEY_MOD_NONE,
        code_editor::EditorClipboard clipboard = {}
    ) -> void {
        gui::KeyEvent const event = {.key = key, .kind = gui::KeyEventKind::PRESS, .mods = mods};
        gui::InputState const input = {.key_events = &event, .key_event_count = 1u};
        code_editor::process_editor_input(editor, input, clipboard);
    }

    auto set_command_line_text(code_editor::EditorState& editor, StrRef text) -> void {
        editor.command_text_size =
            text.copy_to(editor.command_text, code_editor::COMMAND_TEXT_CAPACITY - 1u);
        editor.command_text[editor.command_text_size] = '\0';
        code_editor::select_command_match(editor);
    }

    auto run_command_line_text(code_editor::EditorState& editor, StrRef text) -> void {
        send_text(editor, ":");
        set_command_line_text(editor, text);
        code_editor::run_command_line(editor);
    }

    auto set_text_search_text(code_editor::EditorState& editor, StrRef text) -> void {
        editor.text_search_text_size =
            text.copy_to(editor.text_search_text, code_editor::TEXT_SEARCH_TEXT_CAPACITY - 1u);
        editor.text_search_text[editor.text_search_text_size] = '\0';
        BASE_UNUSED(code_editor::update_text_search_selection(editor));
    }

    auto open_text_search_text(code_editor::EditorState& editor, StrRef text) -> void {
        send_text(editor, "/");
        set_text_search_text(editor, text);
    }

    struct LspRequestCapture {
        code_editor::LspEditorRequest requests[16] = {};
        size_t count = 0u;
    };

    auto capture_lsp_request(void* user_data, code_editor::LspEditorRequest const& request)
        -> void {
        auto* const capture = static_cast<LspRequestCapture*>(user_data);
        if (capture->count < sizeof(capture->requests) / sizeof(capture->requests[0u])) {
            capture->requests[capture->count] = request;
        }
        capture->count += 1u;
    }

    auto set_cpp_lsp_server(Arena& arena, code_editor::EditorState& editor) -> void {
        StrRef* const extensions = arena_alloc<StrRef>(arena, 1u);
        extensions[0u] = ".cpp";
        code_editor::LspServerConfig* const servers =
            arena_alloc<code_editor::LspServerConfig>(arena, 1u);
        servers[0u] = {};
        servers[0u].id = "clangd";
        servers[0u].extensions = Slice<StrRef>(extensions, 1u);
        servers[0u].enabled = true;
        editor.lsp_servers = Slice<code_editor::LspServerConfig const>(servers, 1u);
    }

    auto select_editor_range(
        code_editor::EditorState& editor,
        size_t start_line,
        size_t start_column,
        size_t end_line,
        size_t end_column,
        code_editor::EditorSelectionMode mode = code_editor::EditorSelectionMode::NONE
    ) -> void {
        editor.selection_anchor_line = start_line;
        editor.selection_anchor_column = start_column;
        editor.cursor_line = end_line;
        editor.cursor_column = end_column;
        editor.preferred_column = end_column;
        editor.selection_mode = mode;
        editor.set_flag(EditorFlag::SELECTION_ACTIVE, true);
    }

    TEST_CASE(text_buffer_set_normalizes_loaded_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);

        code_editor::text_buffer_set(text, "");
        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 1u);
        TEST_EXPECT(context, code_editor::text_buffer_line_size(text, 0u) == 0u);

        code_editor::text_buffer_set(text, "a\n");
        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 1u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "a");

        code_editor::text_buffer_set(text, "a\r\nb");
        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 2u);
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 0u)) == "a"
        );
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 1u)) == "b"
        );
    }

    TEST_CASE(text_buffer_insert_can_create_trailing_empty_line) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "a");

        code_editor::text_buffer_insert(text, code_editor::text_buffer_size(text), "\n");

        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 2u);
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 0u)) == "a"
        );
        TEST_EXPECT(context, code_editor::text_buffer_line_size(text, 1u) == 0u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "a\n");
    }

    TEST_CASE(text_buffer_line_materializes_across_pieces) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "abcd");

        code_editor::text_buffer_insert(text, 2u, "XY");

        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 1u);
        code_editor::EditorLine const line = code_editor::text_buffer_line(text, 0u);
        TEST_EXPECT(context, code_editor::text_buffer_line_text(line) == "abXYcd");
        TEST_EXPECT(context, line.text == text.line_cache.data());
    }

    TEST_CASE(text_buffer_erase_splits_and_joins_lines) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "ab\ncd");

        size_t const newline = code_editor::text_buffer_position_to_offset(text, {0u, 2u});
        code_editor::text_buffer_erase(text, newline, newline + 1u);

        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 1u);
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 0u)) == "abcd"
        );
    }

    TEST_CASE(text_buffer_many_inserts_keep_tree_order) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "");

        char expected[64] = {};
        size_t expected_size = 0u;
        for (size_t index = 0u; index < 24u; ++index) {
            char const ch = static_cast<char>('a' + index);
            size_t const offset = expected_size == 0u ? 0u : (index * 7u) % (expected_size + 1u);
            code_editor::text_buffer_insert(text, offset, StrRef(&ch, 1u));
            insert_expected_byte(expected, expected_size, offset, ch);
        }

        TEST_EXPECT(context, code_editor::text_buffer_size(text) == expected_size);
        TEST_EXPECT(
            context, code_editor::text_buffer_copy(text, arena) == StrRef(expected, expected_size)
        );
    }

    TEST_CASE(text_buffer_erase_across_multiple_pieces) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "0123456789");

        code_editor::text_buffer_insert(text, 2u, "AA");
        code_editor::text_buffer_insert(text, 7u, "BB");
        code_editor::text_buffer_insert(text, code_editor::text_buffer_size(text), "CC");
        code_editor::text_buffer_erase(text, 3u, 13u);

        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "01A9CC");
    }

    TEST_CASE(text_buffer_line_lookup_after_many_edits) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "0\n1\n2\n3");

        for (size_t line = 0u; line < 4u; ++line) {
            char const ch = static_cast<char>('a' + line);
            size_t const offset = code_editor::text_buffer_position_to_offset(text, {line, 1u});
            code_editor::text_buffer_insert(text, offset, StrRef(&ch, 1u));
        }
        code_editor::text_buffer_insert(text, code_editor::text_buffer_size(text), "\n");

        TEST_EXPECT(context, code_editor::text_buffer_line_count(text) == 5u);
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 0u)) == "0a"
        );
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 1u)) == "1b"
        );
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 2u)) == "2c"
        );
        TEST_EXPECT(
            context,
            code_editor::text_buffer_line_text(code_editor::text_buffer_line(text, 3u)) == "3d"
        );
        TEST_EXPECT(context, code_editor::text_buffer_line_size(text, 4u) == 0u);
    }

    TEST_CASE(text_buffer_positions_cross_piece_boundaries) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "ab\ncd");

        code_editor::text_buffer_insert(text, 1u, "XY");
        code_editor::text_buffer_insert(text, 6u, "Z");

        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "aXYb\ncZd");
        TEST_EXPECT(context, code_editor::text_buffer_position_to_offset(text, {0u, 3u}) == 3u);
        TEST_EXPECT(context, code_editor::text_buffer_position_to_offset(text, {1u, 2u}) == 7u);

        code_editor::EditorTextPosition position =
            code_editor::text_buffer_offset_to_position(text, 7u);
        TEST_EXPECT(context, position.line == 1u);
        TEST_EXPECT(context, position.column == 2u);
    }

    TEST_CASE(text_buffer_clone_after_edits_is_independent) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "abc\ndef");
        code_editor::text_buffer_insert(text, 3u, "X");
        code_editor::text_buffer_erase(text, 1u, 2u);

        code_editor::EditorText clone = {};
        code_editor::text_buffer_clone(text, clone, arena);
        code_editor::text_buffer_insert(text, 0u, "S");
        code_editor::text_buffer_insert(clone, code_editor::text_buffer_size(clone), "T");

        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "SacX\ndef");
        TEST_EXPECT(context, code_editor::text_buffer_copy(clone, arena) == "acX\ndefT");
    }

    TEST_CASE(text_buffer_full_copy_after_split_merge_operations) {
        Arena arena = {};
        arena.init();

        code_editor::EditorText text = {};
        code_editor::text_buffer_init(text, arena);
        code_editor::text_buffer_set(text, "abcdef");

        code_editor::text_buffer_insert(text, 3u, "XY");
        code_editor::text_buffer_erase(text, 3u, 5u);
        code_editor::text_buffer_insert(text, 6u, "Z");
        code_editor::text_buffer_erase(text, 0u, 1u);

        TEST_EXPECT(context, code_editor::text_buffer_copy(text, arena) == "bcdefZ");
        TEST_EXPECT(context, code_editor::text_buffer_copy_range(text, arena, 1u, 4u) == "cde");
    }

    TEST_CASE(editor_state_hash_tracks_text_buffer_edits) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");

        uint64_t const before = code_editor::editor_state_hash(editor);
        code_editor::text_buffer_insert(editor.text, 1u, "X");

        TEST_EXPECT(context, code_editor::editor_state_hash(editor) != before);
    }

    TEST_CASE(editor_loads_more_than_old_line_cap) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(2048u, arena.resource()));
        append_lines(text, 160u);

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 160u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 159u)) == "line"
        );
    }

    TEST_CASE(editor_default_file_is_scratch) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        TEST_EXPECT(context, editor.current_file_name == code_editor::SCRATCH_FILE_NAME);
        TEST_EXPECT(context, editor.current_file_path.empty());
    }

    TEST_CASE(editor_undo_to_saved_text_clears_dirty) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");
        editor.cursor_column = 3u;
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, "x");
        TEST_EXPECT(context, editor.flag(EditorFlag::DIRTY));

        press_key(editor, gui::Key::Z, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, !editor.flag(EditorFlag::DIRTY));
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abc"
        );
    }

    TEST_CASE(editor_normal_undo_restores_scroll) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(512u, arena.resource()));
        append_lines(text, 40u);

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());
        editor.cursor_line = 20u;
        editor.scroll_x = 12.0f;
        editor.scroll_y = code_editor::editor_line_height(editor) * 15.0f;
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, "x");
        editor.set_flag(EditorFlag::INSERT_MODE, false);
        send_text(editor, "u");

        TEST_EXPECT(context, editor.scroll_x == 12.0f);
        TEST_EXPECT(context, editor.scroll_y == code_editor::editor_line_height(editor) * 15.0f);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 20u)) == "line"
        );
    }

    TEST_CASE(editor_loads_and_edits_line_past_old_column_cap) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(320u, arena.resource()));
        TEST_EXPECT(context, text.write_fill('a', 256u) == 256u);

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());
        editor.cursor_column = code_editor::editor_line(editor, 0u).size;
        editor.preferred_column = editor.cursor_column;
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "z");

        StrRef const line = code_editor::editor_line_text(code_editor::editor_line(editor, 0u));
        TEST_EXPECT(context, line.size() == 257u);
        TEST_EXPECT(context, line[256u] == 'z');
    }

    TEST_CASE(editor_inserts_lines_past_old_line_cap) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        for (size_t index = 0u; index < 130u; ++index) {
            press_key(editor, gui::Key::ENTER);
        }

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 131u);
        TEST_EXPECT(context, editor.cursor_line == 130u);
    }

    TEST_CASE(editor_insert_enter_copies_line_indent) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, " \talpha");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = 4u;
        editor.preferred_column = 4u;

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == " \tal"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == " \tpha"
        );
        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 2u);
    }

    TEST_CASE(editor_insert_enter_inside_indent_does_not_duplicate_indent) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "    value");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "  "
        );
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    value"
        );
        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 4u);
    }

    TEST_CASE(editor_ctrl_alt_down_adds_cursor_for_insert_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aa\nbb\ncc");
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "X");

        TEST_EXPECT(context, editor.extra_cursors.size() == 1u);
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 3u);
        TEST_EXPECT(context, editor.extra_cursors[0u].line == 1u);
        TEST_EXPECT(context, editor.extra_cursors[0u].column == 3u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "aaX\nbbX\ncc");
    }

    TEST_CASE(editor_multi_cursor_backspace_updates_all_cursors) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aaX\nbbX\ncc");
        editor.cursor_line = 0u;
        editor.cursor_column = 3u;
        editor.preferred_column = 3u;
        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        press_key(editor, gui::Key::BACKSPACE);

        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "aa\nbb\ncc");
        TEST_EXPECT(context, editor.cursor_column == 2u);
        TEST_EXPECT(context, editor.extra_cursors.size() == 1u);
        TEST_EXPECT(context, editor.extra_cursors[0u].line == 1u);
        TEST_EXPECT(context, editor.extra_cursors[0u].column == 2u);
    }

    TEST_CASE(editor_multi_cursor_count_is_not_capped_at_old_fixed_limit) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(512u, arena.resource()));
        StringBuffer expected = {};
        TEST_EXPECT(context, expected.init(512u, arena.resource()));
        for (size_t line = 0u; line < 81u; ++line) {
            TEST_EXPECT(context, text.write_byte('x') == 1u);
            TEST_EXPECT(context, expected.write_string("x!") == 2u);
            if (line + 1u < 81u) {
                TEST_EXPECT(context, text.write_byte('\n') == 1u);
                TEST_EXPECT(context, expected.write_byte('\n') == 1u);
            }
        }

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;
        for (size_t index = 0u; index < 80u; ++index) {
            press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        }
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "!");

        TEST_EXPECT(context, editor.extra_cursors.size() == 80u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == expected.str());
    }

    TEST_CASE(editor_middle_drag_builds_line_cursors) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aa\nbb\ncc");
        gui::Rect const rect = {{0.0f, 0.0f}, {260.0f, 120.0f}};
        float constexpr CHAR_WIDTH = 10.0f;
        float const text_x = code_editor::editor_text_x(editor, rect);
        float const content_y = code_editor::editor_content_rect(rect).min.y;
        float const line_height = code_editor::editor_line_height(editor);

        code_editor::begin_multi_cursor_from_mouse(
            editor, rect, {text_x + CHAR_WIDTH * 2.0f, content_y + 1.0f}, CHAR_WIDTH
        );
        code_editor::update_multi_cursor_from_mouse(
            editor, rect, {text_x + CHAR_WIDTH * 2.0f, content_y + line_height * 2.0f}, CHAR_WIDTH
        );
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "!");

        TEST_EXPECT(context, editor.extra_cursors.size() == 2u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "aa!\nbb!\ncc!");
    }

    TEST_CASE(editor_middle_drag_selects_columns_on_each_line) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd\nefgh\nijkl");
        gui::Rect const rect = {{0.0f, 0.0f}, {260.0f, 120.0f}};
        float constexpr CHAR_WIDTH = 10.0f;
        float const text_x = code_editor::editor_text_x(editor, rect);
        float const content_y = code_editor::editor_content_rect(rect).min.y;
        float const line_height = code_editor::editor_line_height(editor);

        code_editor::begin_multi_cursor_from_mouse(
            editor, rect, {text_x + CHAR_WIDTH, content_y + 1.0f}, CHAR_WIDTH
        );
        code_editor::update_multi_cursor_from_mouse(
            editor, rect, {text_x + CHAR_WIDTH * 3.0f, content_y + line_height * 2.0f}, CHAR_WIDTH
        );
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "X");

        TEST_EXPECT(context, editor.extra_cursors.size() == 2u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "aXd\neXh\niXl");
    }

    TEST_CASE(editor_multi_cursor_visual_selects_and_deletes) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc\ndef");

        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        send_text(editor, "v");
        send_text(editor, "l");
        send_text(editor, "x");

        TEST_EXPECT(context, editor.extra_cursors.size() == 1u);
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        TEST_EXPECT(context, editor.extra_cursors[0u].line == 1u);
        TEST_EXPECT(context, editor.extra_cursors[0u].column == 0u);
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "c\nf");
    }

    TEST_CASE(editor_multi_cursor_visual_zero_and_dollar_move_all_cursors) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd\nefgh");
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        send_text(editor, "v");
        send_text(editor, "0");

        TEST_EXPECT(context, editor.cursor_column == 0u);
        TEST_EXPECT(context, editor.extra_cursors.size() == 1u);
        TEST_EXPECT(context, editor.extra_cursors[0u].column == 0u);

        send_text(editor, "$");

        code_editor::EditorSelectionRange const extra_selection =
            code_editor::editor_extra_selection_range(editor, 0u);
        TEST_EXPECT(context, editor.cursor_column == 4u);
        TEST_EXPECT(context, editor.extra_cursors[0u].column == 4u);
        TEST_EXPECT(context, extra_selection.active);
        TEST_EXPECT(context, extra_selection.start_line == 1u);
        TEST_EXPECT(context, extra_selection.start_column == 2u);
        TEST_EXPECT(context, extra_selection.end_line == 1u);
        TEST_EXPECT(context, extra_selection.end_column == 4u);
    }

    TEST_CASE(editor_comma_clears_extra_cursors) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aa\nbb");

        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        send_text(editor, ",");

        TEST_EXPECT(context, editor.extra_cursors.empty());
        TEST_EXPECT(context, !editor.flag(EditorFlag::SELECTION_ACTIVE));
        TEST_EXPECT(context, code_editor::text_buffer_copy(editor.text, arena) == "aa\nbb");
    }

    TEST_CASE(editor_escape_collapses_extra_cursors_from_normal_mode) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aa\nbb");

        press_key(editor, gui::Key::DOWN, gui::KEY_MOD_CTRL | gui::KEY_MOD_ALT);
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        press_key(editor, gui::Key::ESCAPE);

        TEST_EXPECT(context, editor.extra_cursors.size() == 1u);
        TEST_EXPECT(context, !editor.flag(EditorFlag::INSERT_MODE));

        press_key(editor, gui::Key::ESCAPE);

        TEST_EXPECT(context, editor.extra_cursors.empty());
    }

    TEST_CASE(editor_backspace_joins_long_lines) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(384u, arena.resource()));
        TEST_EXPECT(context, text.write_fill('a', 160u) == 160u);
        TEST_EXPECT(context, text.write_byte('\n') == 1u);
        TEST_EXPECT(context, text.write_fill('b', 160u) == 160u);

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());
        editor.cursor_line = 1u;
        editor.cursor_column = 0u;
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        press_key(editor, gui::Key::BACKSPACE);

        StrRef const line = code_editor::editor_line_text(code_editor::editor_line(editor, 0u));
        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 1u);
        TEST_EXPECT(context, line.size() == 320u);
        TEST_EXPECT(context, line[159u] == 'a');
        TEST_EXPECT(context, line[160u] == 'b');
        TEST_EXPECT(context, editor.cursor_column == 160u);
    }

    TEST_CASE(editor_clamps_cursor_after_delete_and_long_edit) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "short\nlonger");
        editor.cursor_line = 1u;
        editor.cursor_column = 200u;
        send_text(editor, "dd");

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 1u);
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 5u);

        editor.set_flag(EditorFlag::INSERT_MODE, true);
        press_key(editor, gui::Key::BACKSPACE);

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "shor"
        );
        TEST_EXPECT(context, editor.cursor_column == 4u);
    }

    TEST_CASE(editor_toggles_sidebar_with_leader_e) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        TEST_EXPECT(context, !editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        press_key(editor, gui::Key::SPACE);
        send_text(editor, " e");
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);
        send_text(editor, " e");
        TEST_EXPECT(context, !editor.flag(EditorFlag::SIDEBAR_VISIBLE));
    }

    TEST_CASE(editor_insert_mode_space_e_does_not_toggle_sidebar) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, " e");

        TEST_EXPECT(context, !editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == " e"
        );
    }

    TEST_CASE(editor_space_f_opens_file_search_over_indexed_files) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "src",
                .path = "C:\\repo\\src",
                .relative_path = "src",
                .is_directory = true,
            },
            {
                .name = "font_cache.cpp",
                .path = "C:\\repo\\src\\font_cache.cpp",
                .relative_path = "src\\font_cache.cpp",
            },
            {
                .name = "app.cpp",
                .path = "C:\\repo\\src\\app.cpp",
                .relative_path = "src\\app.cpp",
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);

        send_text(editor, " f");
        TEST_EXPECT(context, editor.flag(EditorFlag::FILE_SEARCH_OPEN));
        editor.file_search_text_size =
            StrRef("fc").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);
        TEST_EXPECT(context, code_editor::editor_file_search_text(editor) == "fc");
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)).empty()
        );

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !editor.flag(EditorFlag::FILE_SEARCH_OPEN));
        TEST_EXPECT(context, editor.file_search_open_file == 1u);
    }

    TEST_CASE(file_search_skips_entries_hidden_from_file_search) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "tracked.cpp",
                .path = "C:\\repo\\tracked.cpp",
                .relative_path = "tracked.cpp",
            },
            {
                .name = "untracked.cpp",
                .path = "C:\\repo\\untracked.cpp",
                .relative_path = "untracked.cpp",
                .file_search_visible = false,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size =
            StrRef("cpp").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 0u);
    }

    TEST_CASE(file_search_selection_expands_filesystem_tree_to_selected_file) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "src",
                .path = "C:\\repo\\src",
                .relative_path = "src",
                .depth = 0u,
                .is_directory = true,
            },
            {
                .name = "app",
                .path = "C:\\repo\\src\\app",
                .relative_path = "src\\app",
                .depth = 1u,
                .is_directory = true,
            },
            {
                .name = "main.cpp",
                .path = "C:\\repo\\src\\app\\main.cpp",
                .relative_path = "src\\app\\main.cpp",
                .depth = 2u,
            },
            {
                .name = "include",
                .path = "C:\\repo\\include",
                .relative_path = "include",
                .depth = 0u,
                .is_directory = true,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);

        send_text(editor, " f");
        editor.file_search_text_size =
            StrRef("main").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.file_search_open_file == 2u);
        TEST_EXPECT(context, editor.flag(EditorFlag::TREE_OPEN));
        TEST_EXPECT(context, tree[0u].open);
        TEST_EXPECT(context, tree[1u].open);
        TEST_EXPECT(context, !tree[3u].open);
        TEST_EXPECT(context, editor.tree_cursor == 2u);
    }

    TEST_CASE(select_current_file_in_filesystem_tree_expands_and_moves_cursor) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "src",
                .path = "C:\\repo\\src",
                .relative_path = "src",
                .depth = 0u,
                .is_directory = true,
            },
            {
                .name = "app",
                .path = "C:\\repo\\src\\app",
                .relative_path = "src\\app",
                .depth = 1u,
                .is_directory = true,
            },
            {
                .name = "main.cpp",
                .path = "C:\\repo\\src\\app\\main.cpp",
                .relative_path = "src\\app\\main.cpp",
                .depth = 2u,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\src\\app\\main.cpp";

        code_editor::select_current_file_in_filesystem_tree(editor);

        TEST_EXPECT(context, editor.flag(EditorFlag::TREE_OPEN));
        TEST_EXPECT(context, tree[0u].open);
        TEST_EXPECT(context, tree[1u].open);
        TEST_EXPECT(context, editor.tree_cursor == 2u);
    }

    TEST_CASE(file_search_prioritizes_file_names_before_folders) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {.name = "src", .path = "C:\\repo\\docs\\src", .relative_path = "docs\\src"},
            {
                .name = "source.cpp",
                .path = "C:\\repo\\include\\source.cpp",
                .relative_path = "include\\source.cpp",
            },
            {.name = "main.cpp",
             .path = "C:\\repo\\src\\main.cpp",
             .relative_path = "src\\main.cpp"},
            {
                .name = "main.cpp",
                .path = "C:\\repo\\source\\main.cpp",
                .relative_path = "source\\main.cpp",
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size =
            StrRef("src").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 4u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 0u);
        TEST_EXPECT(context, matches[1u].tree_file_index == 1u);
        TEST_EXPECT(context, matches[2u].tree_file_index == 2u);
        TEST_EXPECT(context, matches[3u].tree_file_index == 3u);
    }

    TEST_CASE(file_search_prefers_top_level_files_before_deeper_matches) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "README",
                .path = "C:\\repo\\third_party\\README",
                .relative_path = "third_party\\README",
                .depth = 1u,
            },
            {
                .name = "README.md",
                .path = "C:\\repo\\README.md",
                .relative_path = "README.md",
                .depth = 0u,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size = StrRef("readme").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 2u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 1u);
        TEST_EXPECT(context, matches[1u].tree_file_index == 0u);
    }

    TEST_CASE(file_search_prefers_closer_file_name_before_shallow_loose_match) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "ui_api_examples.md",
                .path = "C:\\repo\\docs\\ui_api_examples.md",
                .relative_path = "docs\\ui_api_examples.md",
                .depth = 1u,
            },
            {
                .name = "app.cpp",
                .path = "C:\\repo\\examples\\code_editor\\app.cpp",
                .relative_path = "examples\\code_editor\\app.cpp",
                .depth = 2u,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size =
            StrRef("app").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 2u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 1u);
        TEST_EXPECT(context, matches[1u].tree_file_index == 0u);
    }

    TEST_CASE(file_search_matches_relative_path_prefix_with_slash_insensitive_query) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "str_ref.h",
                .path = "C:\\repo\\src\\base\\str_ref.h",
                .relative_path = "src\\base\\str_ref.h",
            },
            {
                .name = "src_helpers.h",
                .path = "C:\\repo\\include\\src_helpers.h",
                .relative_path = "include\\src_helpers.h",
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size =
            StrRef("src/base/")
                .copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 0u);
    }

    TEST_CASE(file_search_matches_space_separated_path_parts) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "str_ref.h",
                .path = "C:\\repo\\src\\base\\str_ref.h",
                .relative_path = "src\\base\\str_ref.h",
            },
            {
                .name = "string_ref.h",
                .path = "C:\\repo\\src\\buffer\\string_ref.h",
                .relative_path = "src\\buffer\\string_ref.h",
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.file_search_text_size =
            StrRef("src base str ref")
                .copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::FileSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_file_search_matches(editor, matches);

        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].tree_file_index == 0u);
    }

    TEST_CASE(editor_colon_opens_completes_and_closes_command_line) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, ":");
        TEST_EXPECT(context, editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_command_text(editor).empty());

        code_editor::complete_command_line(editor);
        TEST_EXPECT(context, code_editor::editor_command_text(editor) == "write");

        code_editor::clear_command_line(editor);
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        send_text(editor, ":");
        code_editor::clear_command_line(editor);
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));
    }

    TEST_CASE(editor_colon_commands_run_editor_actions) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        press_key(editor, gui::Key::S, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.flag(EditorFlag::SAVE_PATH_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::SAVE_REQUESTED));

        code_editor::close_save_path_popup(editor);
        run_command_line_text(editor, "w");
        TEST_EXPECT(context, editor.flag(EditorFlag::SAVE_PATH_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::SAVE_REQUESTED));
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        code_editor::close_save_path_popup(editor);
        editor.current_file_name = "file.cpp";
        editor.current_file_path = "C:\\src\\file.cpp";
        run_command_line_text(editor, "w");
        TEST_EXPECT(context, editor.flag(EditorFlag::SAVE_REQUESTED));
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        run_command_line_text(editor, "buffer-close");
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_CURRENT_REQUESTED));
        editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, false);

        run_command_line_text(editor, "new");
        TEST_EXPECT(context, editor.flag(EditorFlag::NEW_SCRATCH_REQUESTED));
        editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, false);

        run_command_line_text(editor, "q!");
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_CURRENT_REQUESTED));
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED));
        editor.set_flag(EditorFlag::CLOSE_CURRENT_REQUESTED, false);
        editor.set_flag(EditorFlag::CLOSE_CURRENT_FORCE_REQUESTED, false);

        run_command_line_text(editor, "wq");
        TEST_EXPECT(context, editor.flag(EditorFlag::WRITE_QUIT_REQUESTED));
        editor.set_flag(EditorFlag::WRITE_QUIT_REQUESTED, false);

        TEST_EXPECT(context, editor.raster_policy == gui::font_provider::DEFAULT_RASTER_POLICY);
        run_command_line_text(editor, "rp");
        TEST_EXPECT(
            context, editor.raster_policy == gui::font_provider::RasterPolicy::SHARP_HINTED
        );
        run_command_line_text(editor, "toggle-raster-policy");
        TEST_EXPECT(
            context, editor.raster_policy == gui::font_provider::RasterPolicy::SMOOTH_HINTED
        );

        run_command_line_text(editor, "open");
        TEST_EXPECT(context, editor.flag(EditorFlag::FILE_SEARCH_OPEN));
    }

    TEST_CASE(editor_slash_searches_current_buffer_and_repeats_matches) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "alpha\nbeta alpha\ngamma alpha");

        open_text_search_text(editor, "alp");

        TEST_EXPECT(context, editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_text_search_text(editor) == "alp");
        code_editor::EditorSelectionRange selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 3u);

        code_editor::finish_text_search(editor);
        TEST_EXPECT(context, !editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));

        send_text(editor, "n");
        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.start_column == 5u);
        TEST_EXPECT(context, selection.end_line == 1u);
        TEST_EXPECT(context, selection.end_column == 8u);

        send_text(editor, "N");
        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 3u);
    }

    TEST_CASE(editor_text_search_uses_direct_case_insensitive_matches) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "aXlpha\nAlpha");

        open_text_search_text(editor, "alp");

        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 1u);
        TEST_EXPECT(context, selection.end_column == 3u);
    }

    TEST_CASE(editor_text_search_starts_at_cursor_line_before_wrapping) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "target\nother\nTARGET\ntarget");
        editor.cursor_line = 2u;
        editor.cursor_column = 0u;

        open_text_search_text(editor, "target");

        code_editor::EditorSelectionRange selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 2u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 2u);
        TEST_EXPECT(context, selection.end_column == 6u);

        code_editor::finish_text_search(editor);
        send_text(editor, "n");
        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_line == 3u);
        send_text(editor, "n");
        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_line == 0u);
    }

    TEST_CASE(editor_text_search_shift_n_can_wrap_above_origin_line) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "target\nother");
        editor.cursor_line = 1u;
        editor.cursor_column = 0u;

        open_text_search_text(editor, "target");
        TEST_EXPECT(context, !code_editor::editor_selection_range(editor).active);

        code_editor::finish_text_search(editor);
        TEST_EXPECT(context, !editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));

        send_text(editor, "N");
        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 6u);
    }

    TEST_CASE(editor_search_command_opens_buffer_search_input) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "alpha\nbeta\ngamma");

        run_command_line_text(editor, "search");
        TEST_EXPECT(context, editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
        editor.set_flag(EditorFlag::TEXT_SEARCH_ACTIVE, false);

        run_command_line_text(editor, "s");

        TEST_EXPECT(context, editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_text_search_text(editor).empty());

        set_text_search_text(editor, "gam");
        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 2u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 2u);
        TEST_EXPECT(context, selection.end_column == 3u);

        code_editor::finish_text_search(editor);
        run_command_line_text(editor, "s");

        TEST_EXPECT(context, editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_text_search_text(editor).empty());
        TEST_EXPECT(context, !code_editor::editor_selection_range(editor).active);
    }

    TEST_CASE(editor_global_search_commands_open_bottom_search_input) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        run_command_line_text(editor, "gs");
        TEST_EXPECT(context, editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE));
        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));

        editor.set_flag(EditorFlag::GLOBAL_SEARCH_ACTIVE, false);
        run_command_line_text(editor, "global-search");
        TEST_EXPECT(context, editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE));
        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
    }

    TEST_CASE(editor_space_slash_opens_global_search_input) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " /");

        TEST_EXPECT(context, editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE));
        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LEADER));
        TEST_EXPECT(context, !editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
    }

    TEST_CASE(editor_global_search_submit_opens_jump_picker_with_query) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_editor_global_search(editor);
        set_text_search_text(editor, "target");

        code_editor::finish_global_search(editor);

        TEST_EXPECT(context, !editor.flag(EditorFlag::GLOBAL_SEARCH_ACTIVE));
        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(
            context, editor.jump_list_kind == code_editor::EditorJumpListKind::GLOBAL_SEARCH
        );
        TEST_EXPECT(context, code_editor::editor_file_search_text(editor) == "target");
        TEST_EXPECT(context, editor.global_search_refresh_requested);
    }

    TEST_CASE(editor_global_search_selects_cached_workspace_result) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "app.cpp",
                .path = "C:\\repo\\app.cpp",
                .relative_path = "app.cpp",
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        code_editor::open_editor_global_search(editor);
        set_text_search_text(editor, "target");
        code_editor::finish_global_search(editor);

        bool const ok = editor.global_search_results.push_back({
            .tree_file_index = 0u,
            .line = 4u,
            .column = 9u,
        });
        TEST_EXPECT(context, ok);

        code_editor::JumpListMatch matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 0u);

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.global_search_open_index == 0u);
        TEST_EXPECT(context, editor.jump_open_index == code_editor::JUMP_LIST_NO_SELECTION);
    }

    TEST_CASE(editor_star_search_submits_selected_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "beta one\nbeta two\nbeta three");
        select_editor_range(editor, 0u, 0u, 0u, 4u);

        send_text(editor, "*");

        TEST_EXPECT(context, !editor.flag(EditorFlag::TEXT_SEARCH_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_text_search_text(editor) == "beta");
        code_editor::EditorSelectionRange selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 4u);

        send_text(editor, "n");
        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 1u);
        TEST_EXPECT(context, selection.end_column == 4u);
    }

    TEST_CASE(editor_new_scratch_shortcuts_request_scratch_buffer) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        press_key(editor, gui::Key::N, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.flag(EditorFlag::NEW_SCRATCH_REQUESTED));
        editor.set_flag(EditorFlag::NEW_SCRATCH_REQUESTED, false);

        send_text(editor, " n");
        TEST_EXPECT(context, editor.flag(EditorFlag::NEW_SCRATCH_REQUESTED));
    }

    TEST_CASE(editor_ctrl_w_requests_current_buffer_close) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        press_key(editor, gui::Key::W, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_CURRENT_REQUESTED));
    }

    TEST_CASE(editor_ctrl_plus_minus_changes_font_size) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        TEST_EXPECT(context, editor.font_size == code_editor::EDITOR_FONT_SIZE);
        press_key(editor, gui::Key::PLUS, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.font_size == code_editor::EDITOR_FONT_SIZE + 1.0f);
        TEST_EXPECT(
            context,
            code_editor::editor_scaled_font_size(editor, 12.0f) >
                code_editor::editor_scaled_font_size(code_editor::EditorState{}, 12.0f)
        );
        press_key(editor, gui::Key::MINUS, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.font_size == code_editor::EDITOR_FONT_SIZE);
    }

    TEST_CASE(editor_ctrl_plus_minus_text_events_do_not_insert_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, "+-=", gui::KEY_MOD_CTRL);

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)).empty()
        );
    }

    TEST_CASE(editor_shift_right_selects_text_and_insert_replaces_it) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;

        press_key(editor, gui::Key::RIGHT, gui::KEY_MOD_SHIFT);
        press_key(editor, gui::Key::RIGHT, gui::KEY_MOD_SHIFT);

        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 1u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 3u);

        send_text(editor, "Z");

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aZd"
        );
        TEST_EXPECT(context, editor.cursor_column == 2u);
        TEST_EXPECT(context, !code_editor::editor_selection_range(editor).active);
    }

    TEST_CASE(editor_left_right_cross_line_boundaries) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc\ndef");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_line = 1u;
        editor.cursor_column = 0u;

        press_key(editor, gui::Key::LEFT);

        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 3u);

        press_key(editor, gui::Key::RIGHT);

        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
    }

    TEST_CASE(editor_ctrl_c_copies_selection) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "alpha beta");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = code_editor::editor_line(editor, 0u).size;
        editor.preferred_column = editor.cursor_column;

        for (size_t index = 0u; index < 4u; ++index) {
            press_key(editor, gui::Key::LEFT, gui::KEY_MOD_SHIFT);
        }
        press_key(
            editor,
            gui::Key::C,
            gui::KEY_MOD_CTRL,
            {.set_clipboard_text = capture_clipboard_text, .user_data = &clipboard}
        );

        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == "beta");
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "alpha beta"
        );
    }

    TEST_CASE(editor_ctrl_v_pastes_multiline_clipboard_text) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        clipboard.text_size = StrRef("X\r\nY").copy_to(clipboard.text, sizeof(clipboard.text));

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "ab");
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;

        press_key(
            editor,
            gui::Key::V,
            gui::KEY_MOD_CTRL,
            {.get_clipboard_text = read_clipboard_text, .user_data = &clipboard}
        );

        TEST_EXPECT(context, clipboard.read_count == 1u);
        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aX"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "Yb"
        );
        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 1u);
    }

    TEST_CASE(editor_ctrl_z_reverts_changes_one_at_a_time) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "Hi");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        send_text(editor, "!?*");

        StrRef const expected[] = {"Hi!?", "Hi!", "Hi"};
        for (StrRef text : expected) {
            press_key(editor, gui::Key::Z, gui::KEY_MOD_CTRL);
            TEST_EXPECT(
                context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == text
            );
        }

        press_key(editor, gui::Key::Z, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "Hi"
        );
    }

    TEST_CASE(editor_mouse_word_selection_selects_current_word) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one two_three + four");
        gui::Rect const rect = {{0.0f, 0.0f}, {500.0f, 300.0f}};
        float const char_width = 10.0f;
        gui::Vec2 const mouse = {
            code_editor::editor_text_x(editor, rect) + char_width * 6.0f,
            code_editor::editor_content_rect(rect).min.y + 2.0f,
        };

        code_editor::select_word_from_mouse(editor, rect, mouse, char_width);

        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 4u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 13u);
    }

    TEST_CASE(editor_mouse_line_selection_selects_current_line_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "first\nsecond line");
        gui::Rect const rect = {{0.0f, 0.0f}, {500.0f, 300.0f}};
        float const char_width = 10.0f;
        gui::Vec2 const mouse = {
            code_editor::editor_text_x(editor, rect) + char_width * 4.0f,
            code_editor::editor_content_rect(rect).min.y + code_editor::editor_line_height(editor) +
                2.0f,
        };

        code_editor::select_line_from_mouse(editor, rect, mouse, char_width);

        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 1u);
        TEST_EXPECT(context, selection.end_column == 11u);
    }

    TEST_CASE(editor_ctrl_left_right_moves_by_words) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one two_three + four");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = code_editor::editor_line(editor, 0u).size;
        editor.preferred_column = editor.cursor_column;

        press_key(editor, gui::Key::LEFT, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.cursor_column == 16u);
        press_key(editor, gui::Key::LEFT, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.cursor_column == 4u);
        press_key(editor, gui::Key::RIGHT, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.cursor_column == 16u);
    }

    TEST_CASE(editor_normal_word_keys_move_by_words) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one.two + four");

        send_text(editor, "w");
        TEST_EXPECT(context, editor.cursor_column == 3u);
        send_text(editor, "w");
        TEST_EXPECT(context, editor.cursor_column == 4u);
        send_text(editor, "e");
        TEST_EXPECT(context, editor.cursor_column == 6u);
        send_text(editor, "e");
        TEST_EXPECT(context, editor.cursor_column == 8u);
        send_text(editor, "b");
        TEST_EXPECT(context, editor.cursor_column == 4u);
        send_text(editor, "b");
        TEST_EXPECT(context, editor.cursor_column == 3u);
        send_text(editor, "W");
        TEST_EXPECT(context, editor.cursor_column == 8u);
        send_text(editor, "E");
        TEST_EXPECT(context, editor.cursor_column == 13u);
        send_text(editor, "B");
        TEST_EXPECT(context, editor.cursor_column == 10u);
        send_text(editor, "B");
        TEST_EXPECT(context, editor.cursor_column == 8u);
    }

    TEST_CASE(editor_normal_word_keys_stop_on_empty_lines) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one\n\ntwo");

        send_text(editor, "w");
        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        send_text(editor, "w");
        TEST_EXPECT(context, editor.cursor_line == 2u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        send_text(editor, "b");
        TEST_EXPECT(context, editor.cursor_line == 1u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        send_text(editor, "b");
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        send_text(editor, "w");
        send_text(editor, "e");
        TEST_EXPECT(context, editor.cursor_line == 2u);
        TEST_EXPECT(context, editor.cursor_column == 2u);
    }

    TEST_CASE(editor_normal_count_prefix_repeats_hjkl_motions) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd\nefgh\nijkl\nmnop");

        send_text(editor, "2j");
        TEST_EXPECT(context, editor.cursor_line == 2u);
        TEST_EXPECT(context, editor.cursor_column == 0u);

        send_text(editor, "3l");
        TEST_EXPECT(context, editor.cursor_column == 3u);

        send_text(editor, "2h");
        TEST_EXPECT(context, editor.cursor_column == 1u);

        send_text(editor, "2k");
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 1u);
    }

    TEST_CASE(editor_normal_count_prefix_repeats_word_motions) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one.two + four");

        send_text(editor, "2w");
        TEST_EXPECT(context, editor.cursor_column == 4u);
        send_text(editor, "2e");
        TEST_EXPECT(context, editor.cursor_column == 8u);
        send_text(editor, "2b");
        TEST_EXPECT(context, editor.cursor_column == 3u);
        send_text(editor, "2W");
        TEST_EXPECT(context, editor.cursor_column == 10u);
        send_text(editor, "2B");
        TEST_EXPECT(context, editor.cursor_column == 0u);
    }

    TEST_CASE(editor_normal_v_selects_characters) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd");
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;

        send_text(editor, "v");

        code_editor::EditorSelectionRange selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, !selection.full_line);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 1u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 2u);

        send_text(editor, "l");

        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_column == 1u);
        TEST_EXPECT(context, selection.end_column == 3u);

        send_text(editor, "v");
        TEST_EXPECT(context, !code_editor::editor_selection_range(editor).active);
    }

    TEST_CASE(editor_normal_v_can_select_newline_without_next_line_text) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        code_editor::EditorClipboard clip = {
            .set_clipboard_text = capture_clipboard_text,
            .user_data = &clipboard,
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc\ndef");
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        send_text(editor, "v");
        send_text(editor, "l", gui::KEY_MOD_NONE, clip);

        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, !selection.full_line);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 2u);
        TEST_EXPECT(context, selection.end_line == 1u);
        TEST_EXPECT(context, selection.end_column == 0u);

        send_text(editor, "y", gui::KEY_MOD_NONE, clip);

        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == "c\n");
    }

    TEST_CASE(editor_normal_shift_v_selects_full_lines) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one\ntwo\nthree\nfour");
        editor.cursor_line = 1u;
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;

        send_text(editor, "V");

        code_editor::EditorSelectionRange selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.full_line);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.start_column == 0u);
        TEST_EXPECT(context, selection.end_line == 2u);
        TEST_EXPECT(context, selection.end_column == 0u);

        send_text(editor, "j");

        selection = code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.start_line == 1u);
        TEST_EXPECT(context, selection.end_line == 3u);
        TEST_EXPECT(context, selection.full_line);
    }

    TEST_CASE(editor_normal_selection_insert_keys_use_selection_edges) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one\ntwo\nthree");

        select_editor_range(editor, 0u, 1u, 0u, 2u);
        send_text(editor, "i");
        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 1u);

        editor.set_flag(EditorFlag::INSERT_MODE, false);
        select_editor_range(editor, 0u, 1u, 0u, 2u);
        send_text(editor, "a");
        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, editor.cursor_column == 2u);

        editor.set_flag(EditorFlag::INSERT_MODE, false);
        editor.cursor_line = 1u;
        editor.cursor_column = 1u;
        send_text(editor, "I");
        TEST_EXPECT(context, editor.cursor_column == 0u);

        editor.set_flag(EditorFlag::INSERT_MODE, false);
        editor.cursor_line = 1u;
        editor.cursor_column = 1u;
        send_text(editor, "A");
        TEST_EXPECT(context, editor.cursor_column == 3u);
    }

    TEST_CASE(editor_normal_selection_open_line_keys_use_selected_lines) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState below = {};
        code_editor::init_editor(arena, below, "one\ntwo\nthree");
        select_editor_range(below, 1u, 0u, 2u, 0u, code_editor::EditorSelectionMode::LINE);
        send_text(below, "o");

        TEST_EXPECT(context, code_editor::editor_line_count(below) == 4u);
        TEST_EXPECT(context, below.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, below.cursor_line == 3u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(below, 3u)).empty()
        );

        code_editor::EditorState above = {};
        code_editor::init_editor(arena, above, "one\ntwo\nthree");
        select_editor_range(above, 1u, 0u, 2u, 0u, code_editor::EditorSelectionMode::LINE);
        send_text(above, "O");

        TEST_EXPECT(context, code_editor::editor_line_count(above) == 4u);
        TEST_EXPECT(context, above.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, above.cursor_line == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(above, 1u)).empty()
        );
    }

    TEST_CASE(editor_normal_open_line_keys_copy_line_indent) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState below = {};
        code_editor::init_editor(arena, below, "root\n \tchild\nend");
        below.cursor_line = 1u;
        send_text(below, "o");

        TEST_EXPECT(context, below.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, below.cursor_line == 2u);
        TEST_EXPECT(context, below.cursor_column == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(below, 2u)) == " \t"
        );

        code_editor::EditorState above = {};
        code_editor::init_editor(arena, above, "root\n\tchild\nend");
        above.cursor_line = 1u;
        send_text(above, "O");

        TEST_EXPECT(context, above.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, above.cursor_line == 1u);
        TEST_EXPECT(context, above.cursor_column == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(above, 1u)) == "\t"
        );
    }

    TEST_CASE(editor_normal_selection_yank_delete_paste_replace_and_change) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        code_editor::EditorClipboard clip = {
            .set_clipboard_text = capture_clipboard_text,
            .get_clipboard_text = read_clipboard_text,
            .user_data = &clipboard,
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "y", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == "bc");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abcd"
        );

        send_text(editor, "d", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == "bc");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "ad"
        );

        send_text(editor, "u");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abcd"
        );
        send_text(editor, "U");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "ad"
        );

        clipboard.text_size = StrRef("X").copy_to(clipboard.text, sizeof(clipboard.text));
        code_editor::set_editor_text(editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "P", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aXbcd"
        );

        code_editor::set_editor_text(editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "p", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abcXd"
        );

        clipboard.text_size = StrRef("YZ").copy_to(clipboard.text, sizeof(clipboard.text));
        code_editor::set_editor_text(editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "R", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aYZd"
        );

        code_editor::set_editor_text(editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "c", gui::KEY_MOD_NONE, clip);
        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == "bc");
        send_text(editor, "Q");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aQd"
        );
    }

    TEST_CASE(editor_normal_c_enters_insert_mode_and_deletes_next_character) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd");
        editor.cursor_column = 1u;
        editor.preferred_column = 1u;

        send_text(editor, "c");

        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, editor.cursor_column == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "acd"
        );

        send_text(editor, "Q");

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aQcd"
        );

        code_editor::EditorState end = {};
        code_editor::init_editor(arena, end, "abcd");
        end.cursor_column = 4u;
        end.preferred_column = 4u;

        send_text(end, "c");

        TEST_EXPECT(context, end.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, end.cursor_column == 4u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(end, 0u)) == "abcd"
        );
    }

    TEST_CASE(editor_normal_x_deletes_newline_character) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc\ndef");
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        send_text(editor, "l");
        send_text(editor, "x");

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abcdef"
        );
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 3u);
    }

    TEST_CASE(editor_normal_alt_delete_and_change_do_not_yank) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        code_editor::EditorClipboard clip = {
            .set_clipboard_text = capture_clipboard_text,
            .user_data = &clipboard,
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "d", gui::KEY_MOD_ALT, clip);

        TEST_EXPECT(context, clipboard.call_count == 0u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "ad"
        );

        code_editor::set_editor_text(editor, "abcd");
        select_editor_range(editor, 0u, 1u, 0u, 3u);
        send_text(editor, "c", gui::KEY_MOD_ALT, clip);

        TEST_EXPECT(context, clipboard.call_count == 0u);
        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "ad"
        );
    }

    TEST_CASE(editor_normal_selection_replace_case_and_indent) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcdef");
        select_editor_range(editor, 0u, 1u, 0u, 4u);
        send_text(editor, "rX");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aXef"
        );

        code_editor::set_editor_text(editor, "Ab\nCd");
        select_editor_range(editor, 0u, 0u, 1u, 2u);
        send_text(editor, "~");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "aB"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "cD"
        );

        select_editor_range(editor, 0u, 0u, 1u, 2u);
        send_text(editor, "`");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "ab"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "cd"
        );

        select_editor_range(editor, 0u, 0u, 1u, 2u);
        send_text(editor, "`", gui::KEY_MOD_ALT);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "AB"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "CD"
        );

        code_editor::set_editor_text(editor, "a\n b\n\tc");
        select_editor_range(editor, 0u, 0u, 2u, 0u, code_editor::EditorSelectionMode::LINE);
        send_text(editor, ">");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "    a"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "     b"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 2u)) == "    \tc"
        );

        select_editor_range(editor, 0u, 0u, 2u, 0u, code_editor::EditorSelectionMode::LINE);
        send_text(editor, "<");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "a"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == " b"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 2u)) == "\tc"
        );
    }

    TEST_CASE(editor_ctrl_slash_toggles_line_comments) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int a;\n    int b;");
        editor.cursor_line = 1u;
        editor.cursor_column = 8u;
        editor.preferred_column = editor.cursor_column;

        press_key(editor, gui::Key::SLASH, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    // int b;"
        );
        TEST_EXPECT(context, editor.cursor_column == 11u);

        press_key(editor, gui::Key::SLASH, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    int b;"
        );
        TEST_EXPECT(context, editor.cursor_column == 8u);
    }

    TEST_CASE(editor_ctrl_slash_toggles_selected_line_comments) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int a;\n    int b;");

        select_editor_range(editor, 0u, 0u, 1u, 0u, code_editor::EditorSelectionMode::LINE);
        press_key(editor, gui::Key::SLASH, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "// int a;"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    // int b;"
        );

        select_editor_range(editor, 0u, 0u, 1u, 0u, code_editor::EditorSelectionMode::LINE);
        press_key(editor, gui::Key::SLASH, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "int a;"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    int b;"
        );
    }

    TEST_CASE(editor_normal_goto_line_home_end) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one\n two\nthree\nfour");

        send_text(editor, "3G");
        TEST_EXPECT(context, editor.cursor_line == 2u);
        TEST_EXPECT(context, editor.cursor_column == 0u);

        editor.cursor_column = 3u;
        press_key(editor, gui::Key::HOME);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        press_key(editor, gui::Key::END);
        TEST_EXPECT(context, editor.cursor_column == 5u);

        send_text(editor, "G");
        TEST_EXPECT(context, editor.cursor_line == 3u);
    }

    TEST_CASE(editor_ctrl_d_u_moves_by_half_focused_split) {
        Arena arena = {};
        arena.init();

        StringBuffer text = {};
        TEST_EXPECT(context, text.init(512u, arena.resource()));
        append_lines(text, 40u);

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, text.str());
        float const line_height = code_editor::editor_line_height(editor);
        code_editor::set_editor_split_rect(
            editor,
            editor.focused_split,
            {{0.0f, 0.0f}, {500.0f, line_height * 10.0f + code_editor::EDITOR_PADDING_Y * 2.0f}}
        );

        press_key(editor, gui::Key::D, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.cursor_line == 5u);
        TEST_EXPECT(context, editor.scroll_y == line_height * 5.0f);

        press_key(editor, gui::Key::U, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.scroll_y == 0.0f);
    }

    TEST_CASE(editor_filesystem_panel_ctrl_d_u_do_not_move_tree_cursor) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {.name = "0.cpp", .path = "C:\\repo\\0.cpp", .relative_path = "0.cpp"},
            {.name = "1.cpp", .path = "C:\\repo\\1.cpp", .relative_path = "1.cpp"},
            {.name = "2.cpp", .path = "C:\\repo\\2.cpp", .relative_path = "2.cpp"},
            {.name = "3.cpp", .path = "C:\\repo\\3.cpp", .relative_path = "3.cpp"},
            {.name = "4.cpp", .path = "C:\\repo\\4.cpp", .relative_path = "4.cpp"},
            {.name = "5.cpp", .path = "C:\\repo\\5.cpp", .relative_path = "5.cpp"},
            {.name = "6.cpp", .path = "C:\\repo\\6.cpp", .relative_path = "6.cpp"},
            {.name = "7.cpp", .path = "C:\\repo\\7.cpp", .relative_path = "7.cpp"},
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        code_editor::focus_editor_split(editor, filesystem);

        press_key(editor, gui::Key::D, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::D, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.tree_cursor == 0u);
        press_key(editor, gui::Key::U, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.tree_cursor == 0u);
    }

    TEST_CASE(editor_reveal_cursor_scrolls_horizontally) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abcdefghijklmnop");

        float constexpr CHAR_WIDTH = 10.0f;
        gui::Rect const rect = {{0.0f, 0.0f}, {140.0f, 80.0f}};
        gui::Rect const content = code_editor::editor_content_rect(rect);
        float const text_min_x =
            content.min.x +
            code_editor::editor_scaled_font_size(editor, code_editor::LINE_NUMBER_WIDTH);
        float const visible_width = content.max.x - text_min_x;

        editor.cursor_column = 10u;
        code_editor::reveal_cursor(editor, rect, CHAR_WIDTH);
        TEST_EXPECT(context, editor.scroll_x == CHAR_WIDTH * 11.0f - visible_width);

        editor.cursor_column = 2u;
        code_editor::reveal_cursor(editor, rect, CHAR_WIDTH);
        TEST_EXPECT(context, editor.scroll_x == CHAR_WIDTH * 2.0f);
    }

    TEST_CASE(editor_ctrl_backspace_deletes_previous_word) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one two three");
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.cursor_column = code_editor::editor_line(editor, 0u).size;
        editor.preferred_column = editor.cursor_column;

        press_key(editor, gui::Key::BACKSPACE, gui::KEY_MOD_CTRL);

        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "one two "
        );
        TEST_EXPECT(context, editor.cursor_column == 8u);
    }

    TEST_CASE(editor_space_b_opens_buffer_search_over_open_files) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        code_editor::remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        code_editor::remember_open_file(editor, "app.cpp", "C:\\repo\\app.cpp");

        send_text(editor, " b");
        TEST_EXPECT(context, editor.flag(EditorFlag::BUFFER_SEARCH_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LEADER));
        TEST_EXPECT(context, editor.file_search_selected == 0u);

        editor.file_search_text_size =
            StrRef("appc").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::BufferSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_buffer_search_matches(editor, matches);
        TEST_EXPECT(context, code_editor::editor_file_search_text(editor) == "appc");
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].open_file_index == 1u);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !editor.flag(EditorFlag::BUFFER_SEARCH_OPEN));
        TEST_EXPECT(context, editor.buffer_search_open_file == 1u);
    }

    TEST_CASE(buffer_search_matches_path_like_queries_against_open_file_paths) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::remember_open_file(editor, "str_ref.h", "C:\\repo\\src\\base\\str_ref.h");
        code_editor::remember_open_file(editor, "main.cpp", "C:\\repo\\src\\main.cpp");

        editor.file_search_text_size =
            StrRef("src/base/")
                .copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::BufferSearchMatch matches[code_editor::FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const count = code_editor::collect_buffer_search_matches(editor, matches);

        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].open_file_index == 0u);
    }

    TEST_CASE(editor_jump_list_records_filters_and_navigates) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        code_editor::record_editor_jump(editor, "main.cpp", "C:\\repo\\main.cpp", 2u, 4u);
        code_editor::record_editor_jump(editor, "render.cpp", "C:\\repo\\render.cpp", 8u, 1u);
        code_editor::record_editor_jump(editor, "app.cpp", "C:\\repo\\app.cpp", 12u, 6u);
        code_editor::record_editor_jump(editor, "app.cpp", "C:\\repo\\app.cpp", 12u, 6u);

        TEST_EXPECT(context, editor.jumps.size() == 3u);
        TEST_EXPECT(context, editor.jump_cursor == 2u);

        editor.file_search_text_size = StrRef("render").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';
        code_editor::JumpListMatch matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 1u);

        editor.file_search_text_size =
            StrRef("2").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';
        size_t const number_count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, number_count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 1u);
        TEST_EXPECT(context, matches[0u].priority == 0u);

        code_editor::open_editor_jump_list(editor);
        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.jump_selected == 2u);

        editor.file_search_text_size =
            StrRef("app").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';
        editor.jump_selected = 0u;
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.jump_open_index == 2u);

        editor.jump_open_index = code_editor::JUMP_LIST_NO_SELECTION;
        send_text(editor, " o");
        TEST_EXPECT(context, editor.jump_cursor == 1u);
        TEST_EXPECT(context, editor.jump_open_index == 1u);

        editor.jump_open_index = code_editor::JUMP_LIST_NO_SELECTION;
        send_text(editor, " i");
        TEST_EXPECT(context, editor.jump_cursor == 2u);
        TEST_EXPECT(context, editor.jump_open_index == 2u);
    }

    TEST_CASE(editor_lsp_references_use_jump_list_picker) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        code_editor::LspLocation locations[] = {
            {
                .path = "C:\\repo\\main.cpp",
                .range = {.start = {2u, 4u}, .end = {2u, 8u}},
            },
            {
                .path = "C:\\repo\\render.cpp",
                .range = {.start = {8u, 1u}, .end = {8u, 7u}},
            },
        };
        code_editor::LspBridge bridge = {
            .locations = Slice<code_editor::LspLocation>(locations),
            .locations_kind = code_editor::LspRequestKind::REFERENCES,
        };
        editor.lsp_bridge = &bridge;

        code_editor::open_editor_lsp_locations(editor);

        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(
            context, editor.jump_list_kind == code_editor::EditorJumpListKind::LSP_LOCATIONS
        );
        TEST_EXPECT(context, editor.lsp_popup == code_editor::EditorLspPopupKind::NONE);
        TEST_EXPECT(context, code_editor::jump_list_total_count(editor) == 2u);

        editor.file_search_text_size = StrRef("render").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::JumpListMatch matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 1u);

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.lsp_open_location_index == 1u);
        TEST_EXPECT(context, editor.jump_open_index == code_editor::JUMP_LIST_NO_SELECTION);
    }

    TEST_CASE(editor_lsp_symbols_use_jump_list_picker) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        code_editor::LspDocumentSymbol symbols[] = {
            {
                .path = "C:\\repo\\main.cpp",
                .name = "draw_panel",
                .detail = "void()",
                .selection_range = {.start = {2u, 4u}, .end = {2u, 14u}},
            },
            {
                .path = "C:\\repo\\render.cpp",
                .name = "layout_window",
                .detail = "void()",
                .selection_range = {.start = {8u, 1u}, .end = {8u, 14u}},
            },
        };
        code_editor::LspBridge bridge = {
            .symbols = Slice<code_editor::LspDocumentSymbol>(symbols),
            .symbols_kind = code_editor::LspRequestKind::WORKSPACE_SYMBOL,
        };
        editor.lsp_bridge = &bridge;

        code_editor::open_editor_lsp_symbols(
            editor, code_editor::EditorJumpListKind::LSP_WORKSPACE_SYMBOLS
        );

        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(
            context, editor.jump_list_kind == code_editor::EditorJumpListKind::LSP_WORKSPACE_SYMBOLS
        );
        TEST_EXPECT(context, code_editor::jump_list_total_count(editor) == 2u);

        editor.file_search_text_size = StrRef("layout").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::JumpListMatch matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 1u);

        editor.file_search_text_size =
            StrRef("2").copy_to(editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY);
        editor.file_search_text[editor.file_search_text_size] = '\0';

        TEST_EXPECT(context, code_editor::collect_jump_list_matches(editor, matches) == 0u);

        editor.file_search_text_size = StrRef("render").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.lsp_open_symbol_index == 1u);
        TEST_EXPECT(context, editor.jump_open_index == code_editor::JUMP_LIST_NO_SELECTION);
    }

    TEST_CASE(editor_space_s_requests_document_and_workspace_symbols) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int main() {}");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        set_cpp_lsp_server(arena, editor);

        code_editor::LspBridge bridge = {.status = code_editor::LspStatusKind::READY};
        LspRequestCapture capture = {};
        editor.lsp_bridge = &bridge;
        editor.lsp_send_request = capture_lsp_request;
        editor.lsp_user_data = &capture;

        send_text(editor, " s");

        TEST_EXPECT(context, capture.count == 5u);
        TEST_EXPECT(
            context, capture.requests[4u].kind == code_editor::LspRequestKind::DOCUMENT_SYMBOL
        );

        send_text(editor, " ");
        send_text(editor, "S", gui::KEY_MOD_SHIFT);

        TEST_EXPECT(context, capture.count == 6u);
        TEST_EXPECT(
            context, capture.requests[5u].kind == code_editor::LspRequestKind::WORKSPACE_SYMBOL
        );
    }

    TEST_CASE(editor_space_d_and_shift_d_open_diagnostics) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int main() {}");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";

        code_editor::LspDiagnostic diagnostics[] = {
            {
                .path = "C:\\repo\\main.cpp",
                .range = {.start = {2u, 4u}, .end = {2u, 8u}},
                .severity = code_editor::LspDiagnosticSeverity::ERROR_DIAGNOSTIC,
                .message = "expected ';'",
            },
            {
                .path = "C:\\repo\\render.cpp",
                .range = {.start = {8u, 1u}, .end = {8u, 7u}},
                .severity = code_editor::LspDiagnosticSeverity::WARNING,
                .message = "unused variable",
            },
        };
        code_editor::LspBridge bridge = {
            .diagnostics = Slice<code_editor::LspDiagnostic>(diagnostics)
        };
        editor.lsp_bridge = &bridge;

        send_text(editor, " d");

        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(
            context, editor.jump_list_kind == code_editor::EditorJumpListKind::LSP_FILE_DIAGNOSTICS
        );
        TEST_EXPECT(context, code_editor::jump_list_total_count(editor) == 1u);

        code_editor::JumpListMatch matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const count = code_editor::collect_jump_list_matches(editor, matches);
        TEST_EXPECT(context, count == 1u);
        TEST_EXPECT(context, matches[0u].jump_index == 0u);

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.lsp_open_diagnostic_index == 0u);

        send_text(editor, " ");
        send_text(editor, "D", gui::KEY_MOD_SHIFT);

        TEST_EXPECT(context, editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(
            context,
            editor.jump_list_kind == code_editor::EditorJumpListKind::LSP_WORKSPACE_DIAGNOSTICS
        );
        TEST_EXPECT(context, code_editor::jump_list_total_count(editor) == 2u);

        editor.file_search_text_size = StrRef("render").copy_to(
            editor.file_search_text, code_editor::FILE_SEARCH_TEXT_CAPACITY
        );
        editor.file_search_text[editor.file_search_text_size] = '\0';

        code_editor::JumpListMatch workspace_matches[code_editor::JUMP_LIST_LIMIT] = {};
        size_t const workspace_count =
            code_editor::collect_jump_list_matches(editor, workspace_matches);
        TEST_EXPECT(context, workspace_count == 1u);
        TEST_EXPECT(context, workspace_matches[0u].jump_index == 1u);

        press_key(editor, gui::Key::ENTER);

        TEST_EXPECT(context, !editor.flag(EditorFlag::JUMP_LIST_OPEN));
        TEST_EXPECT(context, editor.lsp_open_diagnostic_index == 1u);
    }

    TEST_CASE(editor_z_bindings_fold_lsp_scopes) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "void f() {\nif (x) {\ncall();\n}\nafter();\n}");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        editor.cursor_line = 1u;
        editor.preferred_column = 0u;

        code_editor::LspFoldingRange ranges[] = {
            {.start_line = 0u, .end_line = 5u},
            {.start_line = 1u, .end_line = 3u},
        };
        code_editor::LspBridge bridge = {
            .folding_ranges = Slice<code_editor::LspFoldingRange>(ranges),
            .folding_ranges_path = "C:\\repo\\main.cpp",
            .folding_ranges_revision = editor.text.revision,
        };
        editor.lsp_bridge = &bridge;

        send_text(editor, "zc");

        TEST_EXPECT(context, code_editor::editor_line_folded(editor, 1u));
        TEST_EXPECT(context, code_editor::editor_line_hidden(editor, 2u));
        TEST_EXPECT(context, code_editor::editor_visible_line_count(editor) == 4u);
        TEST_EXPECT(context, code_editor::editor_visible_line_at(editor, 2u) == 4u);

        press_key(editor, gui::Key::DOWN);

        TEST_EXPECT(context, editor.cursor_line == 4u);

        editor.cursor_line = 1u;
        send_text(editor, "zo");

        TEST_EXPECT(context, !code_editor::editor_line_folded(editor, 1u));
        TEST_EXPECT(context, code_editor::editor_visible_line_count(editor) == 6u);

        send_text(editor, "zM");

        TEST_EXPECT(context, code_editor::editor_line_folded(editor, 0u));
        TEST_EXPECT(context, code_editor::editor_line_hidden(editor, 4u));
        TEST_EXPECT(context, code_editor::editor_visible_line_count(editor) == 1u);

        send_text(editor, "zR");

        TEST_EXPECT(context, !code_editor::editor_line_folded(editor, 0u));
        TEST_EXPECT(context, code_editor::editor_visible_line_count(editor) == 6u);
    }

    TEST_CASE(editor_json_files_publish_validation_diagnostics) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "{\n  \"ok\": true,\n}\n");
        editor.current_file_name = "settings.json";
        editor.current_file_path = "C:\\repo\\settings.json";

        code_editor::update_editor_lsp_document(editor);

        TEST_EXPECT(context, editor.lsp_bridge == &editor.json_bridge);
        TEST_EXPECT(context, editor.lsp_bridge->diagnostics.size() == 1u);
        code_editor::LspDiagnostic const& diagnostic = editor.lsp_bridge->diagnostics[0u];
        TEST_EXPECT(
            context, diagnostic.severity == code_editor::LspDiagnosticSeverity::ERROR_DIAGNOSTIC
        );
        TEST_EXPECT(context, diagnostic.source == "json");
        TEST_EXPECT(context, diagnostic.range.start.line == 2u);
    }

    TEST_CASE(editor_json_files_publish_folding_ranges) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "{\n  \"items\": [\n    1\n  ]\n}\n");
        editor.current_file_name = "settings.json";
        editor.current_file_path = "C:\\repo\\settings.json";
        editor.cursor_line = 1u;

        code_editor::update_editor_lsp_document(editor);

        TEST_EXPECT(context, editor.lsp_bridge == &editor.json_bridge);
        TEST_EXPECT(context, editor.lsp_bridge->diagnostics.empty());
        TEST_EXPECT(context, editor.lsp_bridge->folding_ranges.size() == 2u);
        TEST_EXPECT(context, code_editor::editor_line_foldable(editor, 0u));
        TEST_EXPECT(context, code_editor::editor_line_foldable(editor, 1u));

        send_text(editor, "zc");

        TEST_EXPECT(context, code_editor::editor_line_folded(editor, 1u));
        TEST_EXPECT(context, code_editor::editor_line_hidden(editor, 2u));
        TEST_EXPECT(context, code_editor::editor_visible_line_count(editor) == 3u);
    }

    TEST_CASE(editor_lsp_rename_prefills_and_selects_current_word) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int original_name = 0;");
        editor.cursor_column = 6u;
        editor.preferred_column = editor.cursor_column;

        send_text(editor, " rn");

        TEST_EXPECT(context, editor.lsp_popup == code_editor::EditorLspPopupKind::RENAME);
        TEST_EXPECT(
            context, StrRef(editor.lsp_rename_text, editor.lsp_rename_text_size) == "original_name"
        );
        TEST_EXPECT(context, editor.lsp_rename_text_selected);
    }

    TEST_CASE(editor_lsp_completion_expands_snippet_and_selects_first_placeholder) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "fo");

        code_editor::LspCompletionItem completions[] = {
            {
                .label = "for",
                .insert_text = "for (${1:int i = 0}; ${2:i < count}; ${3:++i}) {\n    $0\n}",
                .edit_range = {.start = {0u, 0u}, .end = {0u, 2u}},
                .has_edit = true,
                .is_snippet = true,
            },
        };
        code_editor::LspBridge bridge = {
            .completions = Slice<code_editor::LspCompletionItem>(completions)
        };
        editor.lsp_bridge = &bridge;
        editor.lsp_popup = code_editor::EditorLspPopupKind::COMPLETION;

        code_editor::accept_lsp_popup(editor);

        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) ==
                "for (int i = 0; i < count; ++i) {"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "    "
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 2u)) == "}"
        );
        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 0u);
        TEST_EXPECT(context, selection.start_column == 5u);
        TEST_EXPECT(context, selection.end_line == 0u);
        TEST_EXPECT(context, selection.end_column == 14u);
    }

    TEST_CASE(editor_lsp_completion_applies_additional_edits_before_snippet_selection) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int main() {\n    Vec\n}");
        editor.current_file_path = "C:\\repo\\main.cpp";
        editor.cursor_line = 1u;
        editor.cursor_column = 7u;
        editor.preferred_column = 7u;

        code_editor::LspTextEdit additional_edits[] = {
            {
                .path = "C:\\repo\\main.cpp",
                .range = {.start = {0u, 0u}, .end = {0u, 0u}},
                .new_text = "#include <vector>\n",
            },
        };
        code_editor::LspCompletionItem completions[] = {
            {
                .label = "Vector",
                .insert_text = "Vector${1:T}",
                .additional_edits = Slice<code_editor::LspTextEdit>(additional_edits),
                .edit_range = {.start = {1u, 4u}, .end = {1u, 7u}},
                .has_edit = true,
                .is_snippet = true,
            },
        };
        code_editor::LspBridge bridge = {
            .completions = Slice<code_editor::LspCompletionItem>(completions)
        };
        editor.lsp_bridge = &bridge;
        editor.lsp_popup = code_editor::EditorLspPopupKind::COMPLETION;

        code_editor::accept_lsp_popup(editor);

        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) ==
                "#include <vector>"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_line_text(code_editor::editor_line(editor, 2u)) == "    VectorT"
        );
        code_editor::EditorSelectionRange const selection =
            code_editor::editor_selection_range(editor);
        TEST_EXPECT(context, selection.active);
        TEST_EXPECT(context, selection.start_line == 2u);
        TEST_EXPECT(context, selection.start_column == 10u);
        TEST_EXPECT(context, selection.end_line == 2u);
        TEST_EXPECT(context, selection.end_column == 11u);
    }

    TEST_CASE(editor_insert_mode_typing_identifier_requests_lsp_completion) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        set_cpp_lsp_server(arena, editor);

        code_editor::LspBridge bridge = {.status = code_editor::LspStatusKind::READY};
        LspRequestCapture capture = {};
        editor.lsp_bridge = &bridge;
        editor.lsp_send_request = capture_lsp_request;
        editor.lsp_user_data = &capture;

        send_text(editor, "a");

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "a"
        );
        TEST_EXPECT(context, capture.count == 5u);
        TEST_EXPECT(context, capture.requests[4u].kind == code_editor::LspRequestKind::COMPLETION);
        TEST_EXPECT(context, capture.requests[4u].position.line == 0u);
        TEST_EXPECT(context, capture.requests[4u].position.column == 1u);
    }

    TEST_CASE(editor_lsp_document_sync_requests_background_lsp_features) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int main() {}");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        set_cpp_lsp_server(arena, editor);

        code_editor::LspBridge bridge = {.status = code_editor::LspStatusKind::READY};
        LspRequestCapture capture = {};
        editor.lsp_bridge = &bridge;
        editor.lsp_send_request = capture_lsp_request;
        editor.lsp_user_data = &capture;

        code_editor::update_editor_lsp_document(editor);

        TEST_EXPECT(context, capture.count == 4u);
        TEST_EXPECT(context, capture.requests[0u].kind == code_editor::LspRequestKind::DID_OPEN);
        TEST_EXPECT(
            context, capture.requests[1u].kind == code_editor::LspRequestKind::SEMANTIC_TOKENS
        );
        TEST_EXPECT(context, capture.requests[1u].revision == editor.text.revision);
        TEST_EXPECT(
            context, capture.requests[2u].kind == code_editor::LspRequestKind::FOLDING_RANGE
        );
        TEST_EXPECT(context, capture.requests[2u].revision == editor.text.revision);
        TEST_EXPECT(context, capture.requests[3u].kind == code_editor::LspRequestKind::INLAY_HINTS);
        TEST_EXPECT(context, capture.requests[3u].range.start.line == 0u);
        TEST_EXPECT(context, capture.requests[3u].range.end.line == 0u);
        TEST_EXPECT(context, capture.requests[3u].range.end.column == 13u);
        TEST_EXPECT(context, capture.requests[3u].revision == editor.text.revision);

        code_editor::text_buffer_insert(
            editor.text, code_editor::text_buffer_size(editor.text), "\n"
        );
        code_editor::update_editor_lsp_document(editor);

        TEST_EXPECT(context, capture.count == 8u);
        TEST_EXPECT(context, capture.requests[4u].kind == code_editor::LspRequestKind::DID_CHANGE);
        TEST_EXPECT(
            context, capture.requests[5u].kind == code_editor::LspRequestKind::SEMANTIC_TOKENS
        );
        TEST_EXPECT(
            context, capture.requests[6u].kind == code_editor::LspRequestKind::FOLDING_RANGE
        );
        TEST_EXPECT(context, capture.requests[7u].kind == code_editor::LspRequestKind::INLAY_HINTS);
    }

    TEST_CASE(editor_lsp_document_sync_skips_disabled_inlay_hints) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "int main() {}");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";
        editor.inlay_hints_enabled = false;
        set_cpp_lsp_server(arena, editor);

        code_editor::LspBridge bridge = {.status = code_editor::LspStatusKind::READY};
        LspRequestCapture capture = {};
        editor.lsp_bridge = &bridge;
        editor.lsp_send_request = capture_lsp_request;
        editor.lsp_user_data = &capture;

        code_editor::update_editor_lsp_document(editor);

        TEST_EXPECT(context, capture.count == 3u);
        TEST_EXPECT(context, capture.requests[0u].kind == code_editor::LspRequestKind::DID_OPEN);
        TEST_EXPECT(
            context, capture.requests[1u].kind == code_editor::LspRequestKind::SEMANTIC_TOKENS
        );
        TEST_EXPECT(
            context, capture.requests[2u].kind == code_editor::LspRequestKind::FOLDING_RANGE
        );
    }

    TEST_CASE(editor_space_w_v_and_s_create_splits) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);

        send_text(editor, " wv");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);
        TEST_EXPECT(context, code_editor::editor_focused_pane(editor) == 1u);
        TEST_EXPECT(
            context,
            editor.split_nodes[editor.root_split].kind == code_editor::EditorSplitKind::VERTICAL
        );

        send_text(editor, " ws");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 3u);
        TEST_EXPECT(context, code_editor::editor_focused_pane(editor) == 2u);
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LEADER));
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_WINDOW));
    }

    TEST_CASE(editor_space_w_hjkl_moves_between_split_rects) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " wv");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, left, {{0.0f, 0.0f}, {100.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, right, {{100.0f, 0.0f}, {200.0f, 100.0f}});

        TEST_EXPECT(context, editor.focused_split == right);
        send_text(editor, " wh");
        TEST_EXPECT(context, editor.focused_split == left);
        send_text(editor, " wl");
        TEST_EXPECT(context, editor.focused_split == right);

        send_text(editor, " ws");
        size_t const top = editor.split_nodes[right].first;
        size_t const bottom = editor.split_nodes[right].second;
        code_editor::set_editor_split_rect(editor, left, {{0.0f, 0.0f}, {100.0f, 200.0f}});
        code_editor::set_editor_split_rect(editor, top, {{100.0f, 0.0f}, {200.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, bottom, {{100.0f, 100.0f}, {200.0f, 200.0f}});

        TEST_EXPECT(context, editor.focused_split == bottom);
        send_text(editor, " wk");
        TEST_EXPECT(context, editor.focused_split == top);
        send_text(editor, " wj");
        TEST_EXPECT(context, editor.focused_split == bottom);
    }

    TEST_CASE(editor_space_w_shift_hjkl_swaps_with_directional_split) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " wv");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;
        size_t const left_pane = editor.split_nodes[left].pane;
        size_t const right_pane = editor.split_nodes[right].pane;
        code_editor::set_editor_split_rect(editor, left, {{0.0f, 0.0f}, {100.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, right, {{100.0f, 0.0f}, {200.0f, 100.0f}});

        send_text(editor, " wH");

        TEST_EXPECT(context, editor.focused_split == left);
        TEST_EXPECT(context, editor.split_nodes[left].pane == right_pane);
        TEST_EXPECT(context, editor.split_nodes[right].pane == left_pane);
    }

    TEST_CASE(editor_space_w_shift_h_swaps_code_with_filesystem_panel) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " e");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, left, {{0.0f, 0.0f}, {80.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, right, {{80.0f, 0.0f}, {200.0f, 100.0f}});

        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, left) ==
                code_editor::EditorPaneKind::FILESYSTEM
        );
        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, right) == code_editor::EditorPaneKind::CODE
        );

        send_text(editor, " wH");

        TEST_EXPECT(context, editor.focused_split == left);
        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, left) == code_editor::EditorPaneKind::CODE
        );
        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, right) ==
                code_editor::EditorPaneKind::FILESYSTEM
        );
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));
    }

    TEST_CASE(editor_filesystem_panel_participates_in_window_navigation) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " e");

        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);
        TEST_EXPECT(context, editor.split_nodes[editor.root_split].ratio > 0.30f);
        TEST_EXPECT(context, editor.split_nodes[editor.root_split].ratio < 0.35f);

        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        size_t const code = editor.split_nodes[editor.root_split].second;
        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, filesystem) ==
                code_editor::EditorPaneKind::FILESYSTEM
        );
        TEST_EXPECT(
            context,
            code_editor::editor_split_pane_kind(editor, code) == code_editor::EditorPaneKind::CODE
        );
        code_editor::set_editor_split_rect(editor, filesystem, {{0.0f, 0.0f}, {80.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, code, {{80.0f, 0.0f}, {200.0f, 100.0f}});

        send_text(editor, " wh");
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::FILESYSTEM
        );

        send_text(editor, " wl");
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );
    }

    TEST_CASE(editor_filesystem_panel_normal_mode_navigates_tree_entries) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "src",
                .path = "C:\\repo\\src",
                .relative_path = "src",
                .depth = 0u,
                .is_directory = true,
            },
            {
                .name = "main.cpp",
                .path = "C:\\repo\\src\\main.cpp",
                .relative_path = "src\\main.cpp",
                .depth = 1u,
            },
            {
                .name = "include",
                .path = "C:\\repo\\include",
                .relative_path = "include",
                .depth = 0u,
                .is_directory = true,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_root_name = "repo";
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        code_editor::focus_editor_split(editor, filesystem);

        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);
        TEST_EXPECT(context, code_editor::preferred_code_split_for_open(editor) != filesystem);

        press_key(editor, gui::Key::TAB);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::TAB, gui::KEY_MOD_SHIFT);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        press_key(editor, gui::Key::TAB, gui::KEY_MOD_SHIFT);
        TEST_EXPECT(context, editor.tree_cursor == 2u);

        press_key(editor, gui::Key::TAB);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        send_text(editor, "jkhl<>");
        send_text(editor, "G", gui::KEY_MOD_SHIFT);
        send_text(editor, "gg");
        press_key(editor, gui::Key::D, gui::KEY_MOD_CTRL);
        press_key(editor, gui::Key::U, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, tree[0u].open);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 1u);

        press_key(editor, gui::Key::UP);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, tree[0u].open);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 1u);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.file_search_open_file == 1u);
        editor.file_search_open_file = code_editor::FILE_SEARCH_NO_FILE;

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, editor.file_search_open_file == 1u);
        editor.file_search_open_file = code_editor::FILE_SEARCH_NO_FILE;

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.file_search_open_file == 1u);
        editor.file_search_open_file = code_editor::FILE_SEARCH_NO_FILE;

        press_key(editor, gui::Key::LEFT);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::LEFT);
        TEST_EXPECT(context, !tree[0u].open);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::LEFT);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, tree[0u].open);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !tree[0u].open);

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, tree[0u].open);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !tree[0u].open);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, tree[0u].open);

        press_key(editor, gui::Key::TAB);
        press_key(editor, gui::Key::TAB);
        TEST_EXPECT(context, editor.tree_cursor == 2u);

        press_key(editor, gui::Key::TAB);
        TEST_EXPECT(context, editor.tree_cursor == code_editor::TREE_CURSOR_ROOT);

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, !editor.flag(EditorFlag::TREE_OPEN));

        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.flag(EditorFlag::TREE_OPEN));
    }

    TEST_CASE(editor_filesystem_tree_insert_mode_survives_focus_round_trip) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "src",
                .path = "C:\\repo\\src",
                .relative_path = "src",
                .depth = 0u,
                .is_directory = true,
            },
            {
                .name = "main.cpp",
                .path = "C:\\repo\\src\\main.cpp",
                .relative_path = "src\\main.cpp",
                .depth = 1u,
            },
        };

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_root_name = "repo";
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        size_t const code = editor.split_nodes[editor.root_split].second;
        code_editor::focus_editor_split(editor, filesystem);

        press_key(editor, gui::Key::DOWN);
        send_text(editor, "i");

        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, editor.tree_edit_mode == code_editor::TreeEditMode::RENAME);

        code_editor::focus_editor_split(editor, code);
        code_editor::focus_editor_split(editor, filesystem);

        TEST_EXPECT(context, editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(context, editor.tree_edit_mode == code_editor::TreeEditMode::RENAME);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "src"
        );
    }

    TEST_CASE(editor_filesystem_tree_dd_queues_delete_request_for_root_file) {
        Arena arena = {};
        arena.init();

        code_editor::FileTreeEntry tree[] = {
            {
                .name = "temp.txt",
                .path = "C:\\repo\\temp.txt",
                .relative_path = "temp.txt",
                .depth = 0u,
            },
        };

        code_editor::TreeOperationRequest request = {};
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.tree_root_name = "repo";
        editor.tree_files = Slice<code_editor::FileTreeEntry>(tree);
        editor.shared_tree_operation_request = &request;

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        code_editor::focus_editor_split(editor, filesystem);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.tree_cursor == 0u);

        send_text(editor, "dd");

        TEST_EXPECT(context, editor.tree_operation_pending);
        TEST_EXPECT(context, request.kind == code_editor::TreeOperationKind::REMOVE);
        TEST_EXPECT(context, StrRef(request.source_path) == "C:\\repo\\temp.txt");
    }

    TEST_CASE(editor_remembers_last_code_split_for_filesystem_open) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " wv");
        size_t const left_code = editor.split_nodes[editor.root_split].first;
        size_t const right_code = editor.split_nodes[editor.root_split].second;

        code_editor::focus_editor_split(editor, left_code);
        TEST_EXPECT(context, code_editor::preferred_code_split_for_open(editor) == left_code);

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        code_editor::focus_editor_split(editor, filesystem);

        TEST_EXPECT(context, editor.last_code_split == left_code);
        TEST_EXPECT(context, code_editor::preferred_code_split_for_open(editor) == left_code);

        code_editor::focus_editor_split(editor, right_code);
        code_editor::focus_editor_split(editor, filesystem);

        TEST_EXPECT(context, editor.last_code_split == right_code);
        TEST_EXPECT(context, code_editor::preferred_code_split_for_open(editor) == right_code);
    }

    TEST_CASE(editor_focused_filesystem_panel_can_split_and_close) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " e");
        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        size_t const code = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, filesystem, {{0.0f, 0.0f}, {80.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, code, {{80.0f, 0.0f}, {200.0f, 100.0f}});
        send_text(editor, " wh");

        send_text(editor, " wv");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 3u);
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::FILESYSTEM
        );

        send_text(editor, " wq");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));

        send_text(editor, " wq");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);
        TEST_EXPECT(context, !editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );
    }

    TEST_CASE(editor_space_w_q_closes_focused_split) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " wq");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_APP_REQUESTED));
        editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, false);

        send_text(editor, " wv");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);

        send_text(editor, " wq");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);

        send_text(editor, " wq");
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_APP_REQUESTED));
    }

    TEST_CASE(editor_space_w_q_closes_non_empty_split) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc\ndef");

        send_text(editor, " wv");
        send_text(editor, " wq");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abc"
        );
    }

    TEST_CASE(editor_space_w_q_closes_app_when_last_code_split_has_filesystem_sibling) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " e");
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );

        send_text(editor, " wq");

        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_APP_REQUESTED));
        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 2u);
    }

    TEST_CASE(editor_split_clones_view_and_shares_text_for_same_file) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");
        editor.cursor_column = 2u;
        editor.preferred_column = 2u;

        send_text(editor, " wv");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, left, {{0.0f, 0.0f}, {100.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, right, {{100.0f, 0.0f}, {200.0f, 100.0f}});

        TEST_EXPECT(context, editor.cursor_column == 2u);
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "Z");
        editor.set_flag(EditorFlag::INSERT_MODE, false);

        send_text(editor, " wh");

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abZc"
        );
    }

    TEST_CASE(editor_shared_buffer_load_keeps_pane_view_state_separate) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");
        editor.current_file_name = "main.cpp";
        editor.current_file_path = "C:\\repo\\main.cpp";

        send_text(editor, " wv");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;

        editor.cursor_column = 1u;
        editor.preferred_column = 1u;
        editor.scroll_y = 5.0f;
        code_editor::store_focused_open_file_view(editor);

        code_editor::focus_editor_split(editor, left);

        editor.cursor_column = 3u;
        editor.preferred_column = 3u;
        editor.scroll_y = 12.0f;
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        send_text(editor, "Z");

        code_editor::focus_editor_split(editor, right);
        code_editor::set_editor_text(editor, "other");
        editor.current_file_name = "other.cpp";
        editor.current_file_path = "C:\\repo\\other.cpp";

        TEST_EXPECT(
            context,
            code_editor::load_shared_editor_buffer(editor, "main.cpp", "C:\\repo\\main.cpp")
        );
        TEST_EXPECT(context, editor.cursor_column == 1u);
        TEST_EXPECT(context, editor.preferred_column == 1u);
        TEST_EXPECT(context, editor.scroll_y == 5.0f);
        TEST_EXPECT(context, !editor.flag(EditorFlag::INSERT_MODE));
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abcZ"
        );

        press_key(editor, gui::Key::Z, gui::KEY_MOD_CTRL);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abc"
        );
        TEST_EXPECT(context, editor.cursor_column == 3u);
    }

    TEST_CASE(editor_split_focus_does_not_reclone_unchanged_text) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");

        send_text(editor, " wv");
        size_t const left = editor.split_nodes[editor.root_split].first;
        size_t const right = editor.split_nodes[editor.root_split].second;

        code_editor::focus_editor_split(editor, left);
        code_editor::focus_editor_split(editor, right);
        size_t const used = arena.used_size();

        for (size_t index = 0u; index < 8u; ++index) {
            code_editor::focus_editor_split(editor, left);
            code_editor::focus_editor_split(editor, right);
        }

        TEST_EXPECT(context, arena.used_size() == used);
    }

    TEST_CASE(editor_insert_mode_space_w_v_does_not_split) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, " wv");

        TEST_EXPECT(context, code_editor::editor_split_leaf_count(editor) == 1u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == " wv"
        );
    }

    TEST_CASE(editor_replaces_text_when_opening_file) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "old\ntext");
        editor.cursor_line = 1u;
        editor.cursor_column = 4u;
        editor.scroll_y = 20.0f;

        code_editor::set_editor_text(editor, "new\nfile");

        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "new"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "file"
        );
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 0u);
        TEST_EXPECT(context, editor.scroll_y == 0.0f);
    }

    TEST_CASE(editor_remembers_unique_open_files) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::remember_open_file(editor, "a.cpp", "C:\\src\\a.cpp");
        code_editor::remember_open_file(editor, "a.cpp", "C:\\src\\a.cpp");
        code_editor::remember_open_file(editor, "b.cpp", "C:\\src\\b.cpp");

        TEST_EXPECT(context, editor.open_files.size() == 2u);
        TEST_EXPECT(context, editor.open_files[0u].name == "a.cpp");
        TEST_EXPECT(context, editor.open_files[1u].path == "C:\\src\\b.cpp");
    }

    TEST_CASE(editor_opens_saved_scratch_file) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::set_editor_text(editor, "draft\ntext");
        code_editor::save_scratch_file(editor);

        code_editor::set_editor_text(editor, "file");
        editor.current_file_name = "file.cpp";
        editor.current_file_path = "C:\\src\\file.cpp";
        code_editor::open_scratch_file(editor);

        TEST_EXPECT(context, editor.current_file_name == code_editor::SCRATCH_FILE_NAME);
        TEST_EXPECT(context, editor.current_file_path.empty());
        TEST_EXPECT(context, code_editor::editor_line_count(editor) == 2u);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "draft"
        );
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 1u)) == "text"
        );
    }

    TEST_CASE(editor_preserves_text_for_display) {
        Arena arena = {};
        arena.init();

        TEST_EXPECT(
            context, code_editor::editor_display_text(arena, "ok\r\n\ttext") == "ok\r\n\ttext"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_display_text(
                arena,
                "\xef\xbb\xbf"
                "\xe2\x80\x9c"
                "ok"
                "\xe2\x80\x9d"
            ) == "\xe2\x80\x9c"
                 "ok"
                 "\xe2\x80\x9d"
        );
    }

    TEST_CASE(editor_dumps_non_text_bytes_for_display) {
        Arena arena = {};
        arena.init();

        char const nul_text[] = {'o', 'k', '\0'};
        char const high_text[] = {'o', 'k', static_cast<char>(0xff)};
        char const invalid_utf8[] = {'o', 'k', static_cast<char>(0xc0), static_cast<char>(0x80)};

        TEST_EXPECT(
            context,
            code_editor::editor_display_text(arena, StrRef(nul_text, sizeof(nul_text))) ==
                "00000000  6f 6b 00\n"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_display_text(arena, StrRef(high_text, sizeof(high_text))) ==
                "00000000  6f 6b ff\n"
        );
        TEST_EXPECT(
            context,
            code_editor::editor_display_text(arena, StrRef(invalid_utf8, sizeof(invalid_utf8))) ==
                "00000000  6f 6b c0 80\n"
        );
    }

    TEST_CASE(git_sidebar_leader_shortcuts_switch_tabs) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, " g");
        TEST_EXPECT(context, editor.sidebar_tab == code_editor::EditorSidebarTab::GIT);
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::FILESYSTEM
        );

        send_text(editor, " g");
        TEST_EXPECT(context, !editor.flag(EditorFlag::SIDEBAR_VISIBLE));
        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );

        send_text(editor, " g");
        TEST_EXPECT(context, editor.sidebar_tab == code_editor::EditorSidebarTab::GIT);
        TEST_EXPECT(context, editor.flag(EditorFlag::SIDEBAR_VISIBLE));

        send_text(editor, " e");
        TEST_EXPECT(context, editor.sidebar_tab == code_editor::EditorSidebarTab::FILES);
    }

    TEST_CASE(git_sidebar_stage_and_unstage_create_requests) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;

        TEST_EXPECT(
            context,
            editor.git_status_items.push_back({
                .path = arena_copy_cstr(arena, "unstaged.cpp"),
                .status = code_editor::GitFileStatus::MODIFIED,
                .scope = code_editor::GitStatusScope::UNSTAGED,
            })
        );
        TEST_EXPECT(
            context,
            editor.git_status_items.push_back({
                .path = arena_copy_cstr(arena, "staged.cpp"),
                .status = code_editor::GitFileStatus::ADDED,
                .scope = code_editor::GitStatusScope::STAGED,
            })
        );

        editor.git_selected = 3u;
        send_text(editor, "s");
        TEST_EXPECT(context, editor.git_request.kind == code_editor::GitRequestKind::STAGE);
        TEST_EXPECT(context, editor.git_request.path == "unstaged.cpp");

        editor.git_request = {};
        editor.git_selected = 1u;
        send_text(editor, "u");
        TEST_EXPECT(context, editor.git_request.kind == code_editor::GitRequestKind::UNSTAGE);
        TEST_EXPECT(context, editor.git_request.path == "staged.cpp");
    }

    TEST_CASE(git_sidebar_changes_header_collapses_with_keyboard) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;

        TEST_EXPECT(
            context,
            editor.git_status_items.push_back({
                .path = arena_copy_cstr(arena, "changed.cpp"),
                .status = code_editor::GitFileStatus::MODIFIED,
                .scope = code_editor::GitStatusScope::UNSTAGED,
            })
        );

        editor.git_selected = 0u;
        press_key(editor, gui::Key::LEFT);
        TEST_EXPECT(context, !editor.git_changes_open);

        send_text(editor, "j");
        TEST_EXPECT(context, editor.git_selected == 0u);
        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.git_selected == 1u);
        press_key(editor, gui::Key::UP);
        TEST_EXPECT(context, editor.git_selected == 0u);

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, editor.git_changes_open);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.git_selected == 1u);
        send_text(editor, "s");
        TEST_EXPECT(context, editor.git_request.kind == code_editor::GitRequestKind::STAGE);
        TEST_EXPECT(context, editor.git_request.path == "changed.cpp");
    }

    TEST_CASE(git_sidebar_focused_control_does_not_activate_selected_row) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;

        TEST_EXPECT(
            context,
            editor.git_status_items.push_back({
                .path = arena_copy_cstr(arena, "changed.cpp"),
                .status = code_editor::GitFileStatus::MODIFIED,
                .scope = code_editor::GitStatusScope::UNSTAGED,
            })
        );

        editor.git_selected = 1u;
        editor.git_control_focused = true;
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.git_request.kind == code_editor::GitRequestKind::NONE);

        press_key(editor, gui::Key::SPACE);
        TEST_EXPECT(context, editor.flag(EditorFlag::PENDING_LEADER));
        editor.set_flag(EditorFlag::PENDING_LEADER, false);

        send_text(editor, "j");
        TEST_EXPECT(context, editor.git_selected == 1u);

        editor.git_control_focused = false;
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(
            context, editor.git_request.kind == code_editor::GitRequestKind::OPEN_STATUS_DIFF
        );
        TEST_EXPECT(context, editor.git_request.path == "changed.cpp");
    }

    TEST_CASE(git_sidebar_focused_control_allows_window_leader_navigation) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);

        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        size_t const code = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, filesystem, {{0.0f, 0.0f}, {80.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, code, {{80.0f, 0.0f}, {200.0f, 100.0f}});

        editor.git_control_focused = true;
        press_key(editor, gui::Key::SPACE);
        send_text(editor, "wl");

        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LEADER));
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_WINDOW));
    }

    TEST_CASE(git_sidebar_tab_focused_text_input_allows_window_leader_navigation) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);

        size_t const filesystem = editor.split_nodes[editor.root_split].first;
        size_t const code = editor.split_nodes[editor.root_split].second;
        code_editor::set_editor_split_rect(editor, filesystem, {{0.0f, 0.0f}, {80.0f, 100.0f}});
        code_editor::set_editor_split_rect(editor, code, {{80.0f, 0.0f}, {200.0f, 100.0f}});

        editor.git_control_focused = true;
        editor.git_action_ref_focused = true;
        press_key(editor, gui::Key::SPACE);
        send_text(editor, "wl");

        TEST_EXPECT(
            context,
            code_editor::editor_focused_pane_kind(editor) == code_editor::EditorPaneKind::CODE
        );
    }

    TEST_CASE(git_sidebar_editing_text_input_keeps_space_for_text) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);

        editor.git_control_focused = true;
        editor.git_action_ref_focused = true;
        editor.git_text_editing = true;
        press_key(editor, gui::Key::SPACE);

        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LEADER));
    }

    TEST_CASE(git_sidebar_stale_text_focus_does_not_block_focused_code_pane) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);

        size_t const code = editor.split_nodes[editor.root_split].second;
        code_editor::focus_editor_split(editor, code);
        editor.set_flag(EditorFlag::INSERT_MODE, true);
        editor.git_commit_text_focused = true;
        editor.git_text_editing = true;

        send_text(editor, "x");

        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "x"
        );
    }

    TEST_CASE(git_sidebar_graph_header_is_collapsed_by_default) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;

        TEST_EXPECT(
            context,
            editor.git_commits.push_back({
                .oid = arena_copy_cstr(arena, "abcdef"),
                .short_oid = arena_copy_cstr(arena, "abcdef"),
                .summary = arena_copy_cstr(arena, "commit"),
            })
        );

        TEST_EXPECT(context, !editor.git_graph_open);
        editor.git_selected = 1u;
        send_text(editor, "j");
        TEST_EXPECT(context, editor.git_selected == 1u);

        press_key(editor, gui::Key::RIGHT);
        TEST_EXPECT(context, editor.git_graph_open);

        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.git_selected == 2u);
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.git_commits[0u].open);
    }

    TEST_CASE(git_sidebar_shift_k_opens_commit_popup) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;
        editor.git_graph_open = true;

        TEST_EXPECT(
            context,
            editor.git_commits.push_back({
                .oid = arena_copy_cstr(arena, "abcdef"),
                .short_oid = arena_copy_cstr(arena, "abcdef"),
                .summary = arena_copy_cstr(arena, "commit"),
            })
        );

        editor.git_selected = 2u;
        send_text(editor, "K", gui::KEY_MOD_SHIFT);
        TEST_EXPECT(context, editor.git_commit_popup == 0u);

        send_text(editor, "k");
        TEST_EXPECT(context, editor.git_commit_popup == code_editor::GIT_COMMIT_POPUP_NONE);
    }

    TEST_CASE(git_sidebar_requests_more_commits_at_loaded_tail) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;
        editor.git_graph_open = true;
        editor.git_commits_more = true;

        TEST_EXPECT(
            context,
            editor.git_commits.push_back({
                .oid = arena_copy_cstr(arena, "aaaa"),
                .short_oid = arena_copy_cstr(arena, "aaaa"),
                .summary = arena_copy_cstr(arena, "first"),
            })
        );
        TEST_EXPECT(
            context,
            editor.git_commits.push_back({
                .oid = arena_copy_cstr(arena, "bbbb"),
                .short_oid = arena_copy_cstr(arena, "bbbb"),
                .summary = arena_copy_cstr(arena, "second"),
            })
        );

        editor.git_selected = 2u;
        press_key(editor, gui::Key::DOWN);
        TEST_EXPECT(context, editor.git_selected == 3u);
        TEST_EXPECT(context, editor.git_commit_load_more_requested);
    }

    TEST_CASE(git_sidebar_normal_navigation_keys_do_not_move_selection) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;
        editor.git_graph_open = true;
        editor.git_selection_focused = true;

        for (size_t index = 0u; index < 3u; ++index) {
            TEST_EXPECT(
                context,
                editor.git_commits.push_back({
                    .oid = arena_copy_cstr(arena, "aaaa"),
                    .short_oid = arena_copy_cstr(arena, "aaaa"),
                    .summary = arena_copy_cstr(arena, "commit"),
                })
            );
        }

        editor.git_selected = 2u;
        send_text(editor, "jkhlgg");
        send_text(editor, "G", gui::KEY_MOD_SHIFT);
        press_key(editor, gui::Key::D, gui::KEY_MOD_CTRL);
        press_key(editor, gui::Key::U, gui::KEY_MOD_CTRL);
        TEST_EXPECT(context, editor.git_selected == 2u);
    }

    TEST_CASE(git_commit_validates_staged_changes_and_message) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");
        code_editor::open_git_sidebar(editor);
        editor.git_refresh_requested = false;

        code_editor::submit_git_commit(editor);
        TEST_EXPECT(context, editor.git_status_text == "Stage changes before committing.");

        TEST_EXPECT(
            context,
            editor.git_status_items.push_back({
                .path = arena_copy_cstr(arena, "file.cpp"),
                .status = code_editor::GitFileStatus::ADDED,
                .scope = code_editor::GitStatusScope::STAGED,
            })
        );

        editor.git_status_text = {};
        code_editor::submit_git_commit(editor);
        TEST_EXPECT(context, editor.git_status_text == "Commit message required.");

        editor.git_status_text = {};
        TEST_EXPECT(context, editor.git_commit_text.write_string(" \t\r\n") == 4u);
        code_editor::submit_git_commit(editor);
        TEST_EXPECT(context, editor.git_status_text == "Commit message required.");

        editor.git_commit_text.reset();
        TEST_EXPECT(context, editor.git_commit_text.write_string("add file\nbody") == 13u);
        code_editor::submit_git_commit(editor);
        TEST_EXPECT(context, editor.git_request.kind == code_editor::GitRequestKind::COMMIT);
        TEST_EXPECT(context, editor.git_request.message == "add file\nbody");
    }

    TEST_CASE(git_diff_tabs_ignore_editing_commands) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "abc");
        editor.view_kind = code_editor::EditorViewKind::GIT_DIFF;
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, "x");
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abc"
        );

        editor.set_flag(EditorFlag::INSERT_MODE, false);
        press_key(editor, gui::Key::DELETE_KEY);
        TEST_EXPECT(
            context, code_editor::editor_line_text(code_editor::editor_line(editor, 0u)) == "abc"
        );
    }

    TEST_CASE(git_diff_tabs_support_row_boundary_navigation) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "first\nsecond row");
        editor.view_kind = code_editor::EditorViewKind::GIT_DIFF;
        editor.cursor_line = 1u;
        editor.cursor_column = 4u;

        send_text(editor, "0");
        TEST_EXPECT(context, editor.cursor_column == 0u);

        send_text(editor, "$", gui::KEY_MOD_SHIFT);
        TEST_EXPECT(context, editor.cursor_column == code_editor::editor_line(editor, 1u).size);

        editor.cursor_column = 4u;
        press_key(editor, gui::Key::HOME);
        TEST_EXPECT(context, editor.cursor_column == 0u);

        press_key(editor, gui::Key::END);
        TEST_EXPECT(context, editor.cursor_column == code_editor::editor_line(editor, 1u).size);
    }

    TEST_CASE(git_diff_tabs_support_read_only_normal_motions) {
        Arena arena = {};
        arena.init();
        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "one two_three + four\nlast row\nthird");
        editor.view_kind = code_editor::EditorViewKind::GIT_DIFF;

        send_text(editor, "w");
        TEST_EXPECT(context, editor.cursor_column == 4u);
        send_text(editor, "e");
        TEST_EXPECT(context, editor.cursor_column == 12u);
        send_text(editor, "b");
        TEST_EXPECT(context, editor.cursor_column == 4u);
        send_text(editor, "W");
        TEST_EXPECT(context, editor.cursor_column == 14u);
        send_text(editor, "E");
        TEST_EXPECT(context, editor.cursor_column == 19u);
        send_text(editor, "B");
        TEST_EXPECT(context, editor.cursor_column == 16u);

        send_text(editor, "G");
        TEST_EXPECT(context, editor.cursor_line == 2u);
        TEST_EXPECT(context, editor.cursor_column == 0u);

        send_text(editor, "gg");
        TEST_EXPECT(context, editor.cursor_line == 0u);
        TEST_EXPECT(context, editor.cursor_column == 0u);

        send_text(editor, "3");
        TEST_EXPECT(context, !editor.flag(EditorFlag::PENDING_LINE_NUMBER_ACTIVE));
        send_text(editor, "G");
        TEST_EXPECT(context, editor.cursor_line == 2u);
    }

} // namespace

TEST_MAIN()
