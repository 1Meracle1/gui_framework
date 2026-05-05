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
            (void)buffer.write_string("line\n");
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
        code_editor::text_buffer_clone(text, clone);
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
        editor.scroll_y = code_editor::editor_line_height(editor) * 15.0f;
        editor.set_flag(EditorFlag::INSERT_MODE, true);

        send_text(editor, "x");
        editor.set_flag(EditorFlag::INSERT_MODE, false);
        send_text(editor, "u");

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

    TEST_CASE(editor_colon_opens_completes_and_closes_command_line) {
        Arena arena = {};
        arena.init();

        code_editor::EditorState editor = {};
        code_editor::init_editor(arena, editor, "");

        send_text(editor, ":");
        TEST_EXPECT(context, editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));
        TEST_EXPECT(context, code_editor::editor_command_text(editor).empty());

        press_key(editor, gui::Key::TAB);
        TEST_EXPECT(context, code_editor::editor_command_text(editor) == "write");

        press_key(editor, gui::Key::ESCAPE);
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        send_text(editor, ":");
        press_key(editor, gui::Key::BACKSPACE);
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
        send_text(editor, ":w");
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.flag(EditorFlag::SAVE_PATH_OPEN));
        TEST_EXPECT(context, !editor.flag(EditorFlag::SAVE_REQUESTED));
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        code_editor::close_save_path_popup(editor);
        editor.current_file_name = "file.cpp";
        editor.current_file_path = "C:\\src\\file.cpp";
        send_text(editor, ":w");
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.flag(EditorFlag::SAVE_REQUESTED));
        TEST_EXPECT(context, !editor.flag(EditorFlag::COMMAND_LINE_ACTIVE));

        send_text(editor, ":buffer-close");
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.flag(EditorFlag::CLOSE_CURRENT_REQUESTED));

        send_text(editor, ":open");
        press_key(editor, gui::Key::ENTER);
        TEST_EXPECT(context, editor.flag(EditorFlag::FILE_SEARCH_OPEN));
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
        code_editor::init_editor(arena, editor, "one two_three + four");

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
        send_text(editor, "B");
        TEST_EXPECT(context, editor.cursor_column == 14u);
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
        TEST_EXPECT(context, editor.split_nodes[editor.root_split].ratio > 0.15f);
        TEST_EXPECT(context, editor.split_nodes[editor.root_split].ratio < 0.20f);

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

} // namespace

TEST_MAIN()
