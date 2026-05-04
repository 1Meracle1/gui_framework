#include "editor_render.h"

#include "syntax.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <cmath>
#include <cstdio>

namespace code_editor {

    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;

    inline constexpr float TREE_INDENT_WIDTH = 16.0f;
    inline constexpr float TREE_ARROW_SLOT_WIDTH = 16.0f;
    inline constexpr float SIDEBAR_RESIZER_WIDTH = 10.0f;
    inline constexpr float EDITOR_SPLIT_GAP = 6.0f;
    inline constexpr float EDITOR_SPLIT_MIN_RATIO = 0.08f;
    inline constexpr float EDITOR_SPLIT_MAX_RATIO = 0.92f;
    inline constexpr float OPEN_TAB_HEIGHT = 28.0f;
    inline constexpr float OPEN_TAB_GAP = 6.0f;
    inline constexpr float OPEN_TAB_HEADER_PADDING = 6.0f;
    inline constexpr float OPEN_TAB_PADDING = 12.0f;
    inline constexpr float OPEN_TAB_CLOSE_SIZE = 18.0f;
    inline constexpr float OPEN_TAB_FONT_SIZE = 13.0f;
    inline constexpr float FILE_SEARCH_ROW_HEIGHT = 27.0f;
    inline constexpr char TREE_ARROW_OPEN[] = "\xEE\x9C\x8D";
    inline constexpr char TREE_ARROW_CLOSED[] = "\xEE\x9D\xAC";

    [[nodiscard]] auto sidebar_width(EditorState const& editor, float client_width) -> float {
        float const width = std::clamp(
            editor.sidebar_width_percent, SIDEBAR_MIN_WIDTH_PERCENT, SIDEBAR_MAX_WIDTH_PERCENT
        );
        return width * std::max(1.0f, client_width);
    }

    auto
    update_sidebar_resize(EditorState& editor, float client_width, gui::InputState const& input)
        -> void {
        if (!editor.sidebar_resizing) {
            return;
        }

        float const width = std::clamp(
            input.mouse_pos.x - editor.sidebar_resize_grab_x,
            client_width * SIDEBAR_MIN_WIDTH_PERCENT,
            client_width * SIDEBAR_MAX_WIDTH_PERCENT
        );
        editor.sidebar_width_percent = width / std::max(1.0f, client_width);
        editor.sidebar_resizing = input.mouse_down[0u];
    }

    [[nodiscard]] auto open_tree_read_file(StrRef path) -> std::FILE* {
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

    [[nodiscard]] auto read_tree_file_text(Arena& arena, StrRef path, StrRef& out_text) -> bool {
        std::FILE* const file = open_tree_read_file(path);
        if (file == nullptr) {
            return false;
        }
        bool ok = std::fseek(file, 0, SEEK_END) == 0;
        long const size = ok ? std::ftell(file) : -1l;
        ok = ok && size >= 0l && std::fseek(file, 0, SEEK_SET) == 0;
        if (ok && size != 0l) {
            char* const text = arena_alloc<char>(arena, static_cast<size_t>(size));
            size_t const read_size = std::fread(text, 1u, static_cast<size_t>(size), file);
            ok = read_size == static_cast<size_t>(size);
            out_text = ok ? StrRef(text, read_size) : StrRef();
        }
        std::fclose(file);
        return ok;
    }

    [[nodiscard]] auto open_file(EditorState& editor, StrRef name, StrRef path) -> bool {
        DEBUG_ASSERT(editor.text.arena != nullptr);
        if (load_shared_editor_buffer(editor, name, path)) {
            return true;
        }
        if (path.empty()) {
            open_scratch_file(editor);
            return true;
        }
        save_scratch_file(editor);
        StrRef text = {};
        if (!read_tree_file_text(*editor.text.arena, path, text)) {
            fmt::eprintf("code_editor: failed to read %s\n", path);
            return false;
        }
        set_editor_text(editor, editor_display_text(*editor.text.arena, text));
        editor.current_file_name = name;
        editor.current_file_path = path;
        remember_open_file(editor, name, path);
        return true;
    }

    auto open_tree_file(EditorState& editor, FileTreeEntry const& file) -> void {
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    auto draw_token(
        draw::Context context,
        draw::TextStyle style,
        EditorLine const& line,
        size_t start,
        size_t end,
        float x,
        float y,
        float char_width
    ) -> void {
        if (end <= start) {
            return;
        }
        draw::draw_text(
            context,
            {std::round(x + char_width * static_cast<float>(start)), std::round(y)},
            style,
            StrRef(line.text + start, end - start),
            nullptr
        );
    }

    [[nodiscard]] auto syntax_token_color(Palette const& palette, SyntaxTokenKind kind)
        -> gui::Color {
        switch (kind) {
        case SyntaxTokenKind::TEXT:
            return palette.text;
        case SyntaxTokenKind::KEYWORD:
            return palette.keyword;
        case SyntaxTokenKind::TYPE:
            return palette.type;
        case SyntaxTokenKind::STRING:
            return palette.string;
        case SyntaxTokenKind::NUMBER:
            return palette.number;
        case SyntaxTokenKind::COMMENT:
            return palette.comment;
        case SyntaxTokenKind::PREPROCESSOR:
            return palette.preprocessor;
        case SyntaxTokenKind::PUNCTUATION:
            return palette.punctuation;
        }
        return palette.text;
    }

    auto draw_syntax_line(
        draw::Context context,
        font_cache::Font font,
        SyntaxTokenizer tokenizer,
        Palette const& palette,
        EditorLine const& line,
        float x,
        float y,
        float font_size,
        float char_width
    ) -> void {
        draw::TextStyle style = {.font = font, .size = font_size};
        StrRef const text = editor_line_text(line);
        size_t index = 0u;
        while (index < text.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, text, index);
            style.color = to_draw_color(syntax_token_color(palette, token.kind));
            draw_token(context, style, line, token.start, token.end, x, y, char_width);
            index = token.end;
        }
    }

    auto draw_editor_selection(
        draw::Context context,
        EditorSelectionRange selection,
        EditorLine const& editor_line_value,
        size_t line,
        float text_x,
        float y,
        float line_height,
        float char_width,
        float max_x,
        Palette const& palette
    ) -> void {
        if (!selection.active || line < selection.start_line || line > selection.end_line) {
            return;
        }
        if (selection.full_line && selection.end_column == 0u &&
            selection.end_line > selection.start_line && line == selection.end_line) {
            return;
        }

        size_t const start = line == selection.start_line
                                 ? std::min(selection.start_column, editor_line_value.size)
                                 : 0u;
        size_t const end = line == selection.end_line
                               ? std::min(selection.end_column, editor_line_value.size)
                               : editor_line_value.size;
        bool const selects_newline = line < selection.end_line;
        if (start == end && !selects_newline && !selection.full_line) {
            return;
        }

        float const x0 =
            selection.full_line ? text_x : text_x + char_width * static_cast<float>(start);
        float const x1 =
            selection.full_line
                ? max_x
                : text_x + char_width * static_cast<float>(end + (selects_newline ? 1u : 0u));
        if (x0 >= max_x || x1 <= x0) {
            return;
        }
        draw::draw_rect_filled(
            context,
            {{std::round(x0), y}, {std::round(std::min(x1, max_x)), y + line_height}},
            to_draw_color(gui::color_alpha(palette.cursor, 0.28f)),
            0.0f
        );
    }

    [[nodiscard]] auto editor_surface_id(size_t split) -> gui::Id {
        return gui::id("editor_surface", split);
    }

    [[nodiscard]] auto editor_split_id(size_t split) -> gui::Id {
        return gui::id("editor_split", split);
    }

    [[nodiscard]] auto editor_split_resizer_id(size_t split) -> gui::Id {
        return gui::id("editor_split_resizer", split);
    }

    [[nodiscard]] auto draw_editor_surface_rect(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Rect rect,
        gui::InputState const& input,
        Palette const& palette,
        bool apply_key_reveal
    ) -> bool {
        bool const scrolled = !editor.sidebar_resizing && input.scroll_delta_y != 0.0f &&
                              point_in_rect(rect, input.mouse_pos);
        bool const hovered = point_in_rect(rect, input.mouse_pos);
        bool const mouse_pressed = input.mouse_down[0u] && !editor.mouse_was_down;
        bool const double_clicked =
            !editor.sidebar_resizing && hovered && input.mouse_double_clicked[0u];
        bool const triple_clicked =
            !editor.sidebar_resizing && hovered && input.mouse_triple_clicked[0u];
        bool const clicked = !editor.sidebar_resizing && mouse_pressed && hovered;
        bool const dragged =
            !editor.sidebar_resizing && editor.mouse_selecting && input.mouse_down[0u];
        if (scrolled) {
            editor.scroll_y -= input.scroll_delta_y;
        }
        if (triple_clicked) {
            select_line_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.mouse_selecting = false;
        } else if (double_clicked) {
            select_word_from_mouse(editor, rect, input.mouse_pos, char_width);
            editor.mouse_selecting = false;
        } else if (clicked) {
            update_cursor_from_mouse(
                editor,
                rect,
                input.mouse_pos,
                char_width,
                (input.key_mods & gui::KEY_MOD_SHIFT) != 0u
            );
            editor.mouse_selecting = true;
        } else if (dragged) {
            update_cursor_from_mouse(editor, rect, input.mouse_pos, char_width, true);
        }
        if (!input.mouse_down[0u]) {
            editor.mouse_selecting = false;
        }
        editor.mouse_was_down = input.mouse_down[0u];
        if (clicked || dragged || double_clicked || triple_clicked || apply_key_reveal) {
            reveal_cursor(editor, rect);
        } else {
            clamp_scroll(editor, rect);
        }

        gui::Rect const content = editor_content_rect(rect);
        draw::Rect const clip = {
            {content.min.x, content.min.y},
            {content.max.x, content.max.y},
        };
        draw::push_clip_rect(draw_context, clip);

        size_t const line_count = editor_line_count(editor);
        float const line_height = editor_line_height(editor);
        size_t const first_line =
            std::min(line_count - 1u, static_cast<size_t>(editor.scroll_y / line_height));
        float y = content.min.y - (editor.scroll_y - static_cast<float>(first_line) * line_height);
        float const text_x = editor_text_x(editor, rect);
        float const line_number_x = content.min.x;
        EditorSelectionRange const selection = editor_selection_range(editor);
        SyntaxTokenizer const tokenizer = syntax_tokenizer_for_file_name(editor.current_file_name);
        size_t line = first_line;
        while (line < line_count && y < content.max.y) {
            if (line == editor.cursor_line) {
                draw::draw_rect_filled(
                    draw_context,
                    {{content.min.x, y}, {content.max.x, y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor_line, 0.72f)),
                    0.0f
                );
            }

            EditorLine const& text_line = editor_line(editor, line);
            draw_editor_selection(
                draw_context,
                selection,
                text_line,
                line,
                text_x,
                y,
                line_height,
                char_width,
                content.max.x,
                palette
            );
            if (!editor.insert_mode && line == editor.cursor_line) {
                size_t const cursor_column =
                    text_line.size == 0u ? 0u : std::min(editor.cursor_column, text_line.size - 1u);
                float const cursor_x0 =
                    std::round(text_x + char_width * static_cast<float>(cursor_column));
                float const cursor_x1 =
                    std::round(text_x + char_width * static_cast<float>(cursor_column + 1u));
                draw::draw_rect_filled(
                    draw_context,
                    {{cursor_x0, y}, {std::max(cursor_x0 + 1.0f, cursor_x1), y + line_height}},
                    to_draw_color(gui::color_alpha(palette.cursor, 0.45f)),
                    2.0f
                );
            }
            draw::TextStyle number_style = {
                .font = editor_font,
                .size = editor.font_size,
                .color = to_draw_color(line == editor.cursor_line ? palette.text : palette.faint),
            };
            draw::draw_text(
                draw_context,
                {std::round(line_number_x), std::round(y - 2.0f)},
                number_style,
                fmt::tprintf("%4zu", line + 1u),
                nullptr
            );
            draw_syntax_line(
                draw_context,
                editor_font,
                tokenizer,
                palette,
                text_line,
                text_x,
                y - 2.0f,
                editor.font_size,
                char_width
            );
            y += line_height;
            line += 1u;
        }

        float const cursor_x =
            std::round(text_x + char_width * static_cast<float>(editor.cursor_column));
        float const cursor_y =
            content.min.y + static_cast<float>(editor.cursor_line) * line_height - editor.scroll_y;
        if (editor.insert_mode && cursor_y + line_height >= content.min.y &&
            cursor_y < content.max.y) {
            draw::draw_rect_filled(
                draw_context,
                {{cursor_x, std::round(cursor_y + 2.0f)},
                 {cursor_x + 2.0f, std::round(cursor_y + line_height - 2.0f)}},
                to_draw_color(palette.mode_insert),
                0.0f
            );
        }

        draw::pop_clip_rect(draw_context);
        return clicked || double_clicked || triple_clicked;
    }

    auto draw_editor_split_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette,
        size_t split,
        size_t initial_focus,
        size_t& target_focus
    ) -> void {
        if (split >= editor.split_nodes.size()) {
            return;
        }

        EditorSplitNode const node = editor.split_nodes[split];
        if (node.kind != EditorSplitKind::LEAF) {
            gui::BoxKind const box_kind =
                node.kind == EditorSplitKind::VERTICAL ? gui::BoxKind::ROW : gui::BoxKind::COLUMN;
            gui::BoxInfo const* const box = ui.find_box(editor_split_id(split), box_kind);
            if (box != nullptr) {
                set_editor_split_rect(editor, split, box->rect);
            }
            draw_editor_split_surface(
                draw_context,
                editor_font,
                editor,
                char_width,
                ui,
                input,
                palette,
                node.first,
                initial_focus,
                target_focus
            );
            draw_editor_split_surface(
                draw_context,
                editor_font,
                editor,
                char_width,
                ui,
                input,
                palette,
                node.second,
                initial_focus,
                target_focus
            );
            return;
        }
        if (editor_split_pane_kind(editor, split) == EditorPaneKind::FILESYSTEM) {
            gui::BoxInfo const* const box =
                ui.find_box(editor_surface_id(split), gui::BoxKind::SCROLL_PANEL);
            if (box != nullptr) {
                set_editor_split_rect(editor, split, box->rect);
            }
            return;
        }

        gui::BoxInfo const* const box = ui.find_box(editor_surface_id(split), gui::BoxKind::ROW);
        if (box == nullptr) {
            return;
        }

        set_editor_split_rect(editor, split, box->rect);
        focus_editor_split(editor, split);
        bool const clicked = draw_editor_surface_rect(
            draw_context,
            editor_font,
            editor,
            char_width,
            box->rect,
            input,
            palette,
            split == initial_focus && input.key_event_count != 0u
        );
        if (clicked) {
            target_focus = split;
        }
    }

    auto draw_editor_surface(
        draw::Context draw_context,
        font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette
    ) -> void {
        size_t const initial_focus = editor.focused_split;
        size_t target_focus = initial_focus;
        draw_editor_split_surface(
            draw_context,
            editor_font,
            editor,
            char_width,
            ui,
            input,
            palette,
            editor.root_split,
            initial_focus,
            target_focus
        );
        focus_editor_split(editor, target_focus);
    }

    auto draw_tree_guide(gui::Frame& ui, Palette const& palette) -> void {
        if (auto slot = ui.column({
                .layout = {
                    .width = gui::px(TREE_INDENT_WIDTH),
                    .height = gui::fill(),
                    .align_x = gui::Align::CENTER,
                    .align_y = gui::Align::STRETCH,
                },
            })) {
            ui.spacer({
                .layout = {.width = gui::px(1.0f), .height = gui::fill()},
                .style = {.background = gui::color_alpha(palette.muted, 0.36f)},
            });
        }
    }

    auto draw_tree_file(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        FileTreeEntry const& file,
        size_t guide_count
    ) -> void {
        if (auto row = ui.row(
                gui::id(file.path),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(26.0f),
                        .align_y = gui::Align::STRETCH,
                    },
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (row.signal().clicked_left) {
                focus_first_code_split(editor);
                open_tree_file(editor, file);
            }
            bool const selected = editor.current_file_path == file.path;
            ui.label(
                file.name,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, 12.0f, 0.0f, 4.0f)},
                    .style = {
                        .role = selected ? gui::StyleRole::AUTO : gui::StyleRole::TEXT_MUTED,
                        .background = selected ? gui::rgb(34, 45, 58) : gui::Color{},
                        .foreground = selected ? palette.text : gui::Color{},
                        .radius = selected ? 5.0f : -1.0f,
                        .font_size = editor_scaled_font_size(editor, 12.5f),
                    },
                }
            );
        }
    }

    auto draw_tree_folder(
        gui::Frame& ui,
        EditorState const& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        StrRef id_text,
        StrRef text,
        size_t guide_count,
        bool* open
    ) -> void {
        if (auto row = ui.row(
                gui::id(id_text),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(26.0f),
                        .align_y = gui::Align::STRETCH,
                    },
                }
            )) {
            for (size_t index = 0u; index < guide_count; ++index) {
                draw_tree_guide(ui, palette);
            }
            if (auto arrow_slot = ui.row({
                    .layout = {
                        .width = gui::px(TREE_ARROW_SLOT_WIDTH),
                        .height = gui::fill(),
                        .align_x = gui::Align::CENTER,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                bool const is_open = open != nullptr && *open;
                ui.label(
                    is_open ? TREE_ARROW_OPEN : TREE_ARROW_CLOSED,
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font = icon_font,
                            .font_size = editor_scaled_font_size(editor, 9.5f),
                        },
                    }
                );
            }
            ui.label(
                text,
                {
                    .layout =
                        {.width = gui::fill(),
                         .height = gui::fill(),
                         .padding = gui::insets(0.0f, 8.0f, 0.0f, 4.0f)},
                    .style = {
                        .role = gui::StyleRole::TEXT_MUTED,
                        .font_size = editor_scaled_font_size(editor, 12.5f),
                    },
                }
            );
            if (open != nullptr && row.signal().clicked_left) {
                *open = !*open;
            }
        }
    }

    auto draw_tree_entry(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        font_cache::Font icon_font,
        FileTreeEntry& entry
    ) -> void {
        size_t const guide_count = entry.depth + 1u;
        if (entry.is_directory) {
            draw_tree_folder(
                ui, editor, palette, icon_font, entry.path, entry.name, guide_count, &entry.open
            );
        } else {
            draw_tree_file(ui, editor, palette, entry, guide_count);
        }
    }

    auto draw_sidebar(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width
    ) -> void {
        float const width = sidebar_width(editor, client_width);
        if (auto sidebar = ui.scroll_panel(
                gui::id("sidebar"),
                {
                    .layout =
                        {
                            .width = gui::px(width),
                            .height = gui::fill(),
                            .padding = gui::insets(14.0f, 10.0f),
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {
                        .background = palette.panel,
                        .border = palette.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &editor.tree_open
            );
            if (editor.tree_open) {
                size_t closed_depth = static_cast<size_t>(-1);
                for (FileTreeEntry& entry : editor.tree_files) {
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    draw_tree_entry(ui, editor, palette, icon_font, entry);
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
        }
    }

    auto draw_filesystem_panel(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        size_t split,
        gui::Size width,
        gui::Size height
    ) -> void {
        bool const focused = split == editor.focused_split;
        if (auto sidebar = ui.scroll_panel(
                editor_surface_id(split),
                {
                    .layout =
                        {
                            .width = width,
                            .height = height,
                            .padding = gui::insets(14.0f, 10.0f),
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .background = palette.panel,
                            .border = focused ? palette.cursor : palette.border,
                            .border_thickness = focused ? 2.0f : 1.0f,
                            .radius = 8.0f,
                        },
                    .debug_name = "filesystem_surface",
                }
            )) {
            if (sidebar.signal().pressed_left) {
                focus_editor_split(editor, split);
            }
            if (editor.tree_root_name.empty()) {
                return;
            }
            draw_tree_folder(
                ui,
                editor,
                palette,
                icon_font,
                editor.tree_root_name,
                editor.tree_root_name,
                0u,
                &editor.tree_open
            );
            if (editor.tree_open) {
                size_t closed_depth = static_cast<size_t>(-1);
                for (FileTreeEntry& entry : editor.tree_files) {
                    if (closed_depth != static_cast<size_t>(-1)) {
                        if (entry.depth > closed_depth) {
                            continue;
                        }
                        closed_depth = static_cast<size_t>(-1);
                    }
                    draw_tree_entry(ui, editor, palette, icon_font, entry);
                    if (entry.is_directory && !entry.open) {
                        closed_depth = entry.depth;
                    }
                }
            }
        }
    }

    auto draw_sidebar_resizer(
        gui::Frame& ui, EditorState& editor, float client_width, gui::InputState const& input
    ) -> void {
        if (auto resizer = ui.row(
                gui::id("sidebar_resizer"),
                {.layout = {.width = gui::px(SIDEBAR_RESIZER_WIDTH), .height = gui::fill()}}
            )) {
            gui::Signal const signal = resizer.signal();
            if (signal.pressed_left) {
                editor.sidebar_resizing = true;
                editor.sidebar_resize_grab_x =
                    input.mouse_pos.x - sidebar_width(editor, client_width);
            }
        }
    }

    [[nodiscard]] auto open_file_selected(EditorState const& editor, OpenFile const& file) -> bool {
        if (!file.path.empty() || !editor.current_file_path.empty()) {
            return file.path == editor.current_file_path;
        }
        return file.name == editor.current_file_name;
    }

    [[nodiscard]] auto selected_open_file_index(EditorState const& editor, size_t& out_index)
        -> bool {
        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
            if (open_file_selected(editor, editor.open_files[index])) {
                out_index = index;
                return true;
            }
        }
        return false;
    }

    auto close_open_file(EditorState& editor, size_t index) -> void {
        if (index >= editor.open_files.size()) {
            return;
        }

        bool const selected = open_file_selected(editor, editor.open_files[index]);
        if (selected) {
            save_scratch_file(editor);
        }
        editor.open_files.ordered_remove(index);
        if (!selected) {
            return;
        }
        if (editor.open_files.empty()) {
            open_scratch_file(editor);
            return;
        }

        size_t const next_index = std::min(index, editor.open_files.size() - 1u);
        OpenFile const next = editor.open_files[next_index];
        if (!open_file(editor, next.name, next.path)) {
            open_scratch_file(editor);
        }
    }

    auto close_current_file(EditorState& editor) -> void {
        size_t index = 0u;
        if (selected_open_file_index(editor, index)) {
            close_open_file(editor, index);
        }
    }

    auto open_file_search_match(EditorState& editor, size_t tree_file_index) -> void {
        if (tree_file_index >= editor.tree_files.size()) {
            return;
        }
        FileTreeEntry const& file = editor.tree_files[tree_file_index];
        if (file.is_directory) {
            return;
        }
        focus_first_code_split(editor);
        BASE_UNUSED(open_file(editor, file.name, file.path));
    }

    auto draw_file_search_picker(gui::Frame& ui, EditorState& editor, Palette const& palette)
        -> void {
        FileSearchMatch matches[FILE_SEARCH_RESULT_LIMIT] = {};
        size_t const match_count = collect_file_search_matches(editor, matches);
        editor.file_search_selected =
            match_count == 0u ? 0u : std::min(editor.file_search_selected, match_count - 1u);

        if (auto modal = ui.modal(
                gui::id("file_search_modal"),
                {
                    .layout =
                        {
                            .padding = gui::insets(42.0f, 24.0f, 24.0f, 24.0f),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::START,
                        },
                    .debug_name = "file_search_modal",
                }
            )) {
            if (auto dialog = ui.column(
                    gui::id("file_search_dialog"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::children(),
                                .max_width = gui::px(860.0f),
                                .padding = gui::insets(8.0f),
                                .gap = 8.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                        },
                    }
                )) {
                if (auto query = ui.row(
                        gui::id("file_search_query"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(36.0f),
                                    .padding = gui::insets(0.0f, 10.0f),
                                    .gap = 6.0f,
                                    .align_y = gui::Align::CENTER,
                                },
                            .style = {
                                .background = palette.panel_raised,
                                .border = palette.cursor,
                                .border_thickness = 1.0f,
                                .radius = 4.0f,
                            },
                        }
                    )) {
                    ui.label(
                        ">",
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {.foreground = palette.cursor},
                        }
                    );
                    ui.label(
                        editor_file_search_text(editor),
                        {
                            .layout = {.width = gui::fill(), .height = gui::fill()},
                            .style = {.foreground = palette.text},
                        }
                    );
                    ui.label(
                        fmt::tprintf(
                            "%zu/%zu",
                            match_count == 0u ? 0u : editor.file_search_selected + 1u,
                            match_count
                        ),
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {.foreground = palette.muted},
                        }
                    );
                }

                if (auto results = ui.column(
                        gui::id("file_search_results"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height =
                                    gui::px(FILE_SEARCH_ROW_HEIGHT * FILE_SEARCH_RESULT_LIMIT),
                                .align_x = gui::Align::STRETCH,
                            },
                        }
                    )) {
                    if (match_count == 0u) {
                        ui.label(
                            editor.tree_files.empty() ? "No indexed files" : "No matching files",
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                        .padding = gui::insets(0.0f, 10.0f),
                                    },
                                .style = {.foreground = palette.muted},
                            }
                        );
                    }
                    for (size_t index = 0u; index < match_count; ++index) {
                        FileTreeEntry const& file =
                            editor.tree_files[matches[index].tree_file_index];
                        bool const selected = index == editor.file_search_selected;
                        if (auto row = ui.row(
                                gui::id("file_search_result", index),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(FILE_SEARCH_ROW_HEIGHT),
                                            .padding = gui::insets(0.0f, 10.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .style = {
                                        .background = selected ? palette.cursor_line : gui::Color{},
                                        .radius = selected ? 4.0f : -1.0f,
                                    },
                                }
                            )) {
                            if (row.signal().clicked_left) {
                                editor.file_search_selected = index;
                                open_file_search_match(editor, matches[index].tree_file_index);
                                editor.file_search_open = false;
                            }
                            ui.label(
                                selected ? ">" : "",
                                {
                                    .layout = {.width = gui::px(14.0f), .height = gui::fill()},
                                    .style = {.foreground = palette.cursor},
                                }
                            );
                            ui.label(
                                file_search_entry_text(file),
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = selected ? palette.text : palette.muted,
                                        .font_size = editor_scaled_font_size(editor, 12.5f),
                                    },
                                }
                            );
                        }
                    }
                }
            }
        }
    }

    struct OpenFileTabSignal {
        bool selected = false;
        bool closed = false;
    };

    [[nodiscard]] auto draw_open_file_tab(
        gui::Frame& ui,
        EditorState& editor,
        Palette const& palette,
        OpenFile const& file,
        size_t index
    ) -> OpenFileTabSignal {
        OpenFileTabSignal result = {};
        bool const selected = open_file_selected(editor, file);
        if (auto tab = ui.row(
                gui::id("open_file_tab", index),
                {
                    .layout =
                        {
                            .width = gui::children(),
                            .height = gui::px(OPEN_TAB_HEIGHT),
                            .padding = gui::insets(0.0f, OPEN_TAB_PADDING),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {
                        .background = selected ? palette.panel_raised : palette.panel,
                        .border = gui::color_alpha(palette.text, selected ? 0.38f : 0.22f),
                        .border_thickness = 1.0f,
                        .radius = 5.0f,
                    },
                }
            )) {
            ui.label(
                file.name,
                {
                    .layout = {.width = gui::text(), .height = gui::fill()},
                    .style = {
                        .foreground = selected ? palette.text : palette.muted,
                        .font_size = editor_scaled_font_size(editor, OPEN_TAB_FONT_SIZE),
                    },
                }
            );
            if (auto close = ui.row(
                    gui::id("open_file_tab_close", index),
                    {
                        .layout = {
                            .width = gui::px(OPEN_TAB_CLOSE_SIZE),
                            .height = gui::px(OPEN_TAB_CLOSE_SIZE),
                            .align_x = gui::Align::CENTER,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                result.closed = close.signal().clicked_left;
                ui.label(
                    "x",
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font_size = editor_scaled_font_size(editor, 12.0f),
                        },
                    }
                );
            }
            result.selected = !result.closed && !selected && tab.signal().clicked_left;
        }
        return result;
    }

    auto draw_editor_split_resizer(
        gui::Frame& ui,
        EditorState& editor,
        gui::InputState const& input,
        size_t split,
        EditorSplitKind kind
    ) -> void {
        gui::BoxDesc const desc = {
            .layout = {
                .width =
                    kind == EditorSplitKind::VERTICAL ? gui::px(EDITOR_SPLIT_GAP) : gui::fill(),
                .height =
                    kind == EditorSplitKind::VERTICAL ? gui::fill() : gui::px(EDITOR_SPLIT_GAP),
            },
        };
        if (auto resizer = ui.row(editor_split_resizer_id(split), desc)) {
            gui::Signal const signal = resizer.signal();
            if (!signal.active || !input.mouse_down[0u] || split >= editor.split_nodes.size()) {
                return;
            }
            gui::Rect const rect = editor.split_nodes[split].rect;
            float const span = kind == EditorSplitKind::VERTICAL ? rect.max.x - rect.min.x
                                                                 : rect.max.y - rect.min.y;
            float const available = std::max(1.0f, span - EDITOR_SPLIT_GAP);
            float const mouse = kind == EditorSplitKind::VERTICAL ? input.mouse_pos.x - rect.min.x
                                                                  : input.mouse_pos.y - rect.min.y;
            editor.split_nodes[split].ratio = std::clamp(
                (mouse - EDITOR_SPLIT_GAP * 0.5f) / available,
                EDITOR_SPLIT_MIN_RATIO,
                EDITOR_SPLIT_MAX_RATIO
            );
            if (kind == EditorSplitKind::VERTICAL &&
                editor_split_pane_kind(editor, editor.split_nodes[split].first) ==
                    EditorPaneKind::FILESYSTEM) {
                editor.sidebar_width_percent = editor.split_nodes[split].ratio;
            }
        }
    }

    auto draw_editor_split_ui(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font icon_font,
        Palette const& palette,
        gui::InputState const& input,
        size_t split,
        gui::Size width,
        gui::Size height
    ) -> void {
        if (split >= editor.split_nodes.size()) {
            return;
        }

        EditorSplitNode const node = editor.split_nodes[split];
        if (node.kind == EditorSplitKind::LEAF) {
            if (editor_split_pane_kind(editor, split) == EditorPaneKind::FILESYSTEM) {
                draw_filesystem_panel(ui, editor, icon_font, palette, split, width, height);
                return;
            }
            bool const focused = split == editor.focused_split;
            if (auto editor_panel = ui.row(
                    editor_surface_id(split),
                    {
                        .layout =
                            {
                                .width = width,
                                .height = height,
                                .clip = true,
                            },
                        .style =
                            {
                                .background = palette.panel,
                                .border = focused ? palette.cursor : palette.border,
                                .border_thickness = focused ? 2.0f : 1.0f,
                                .radius = 8.0f,
                            },
                        .debug_name = "editor_surface",
                    }
                )) {
                if (editor_panel.signal().pressed_left) {
                    focus_editor_split(editor, split);
                }
            }
            return;
        }

        gui::BoxDesc const desc = {
            .layout = {
                .width = width,
                .height = height,
                .gap = 0.0f,
                .align_x = gui::Align::STRETCH,
                .align_y = gui::Align::STRETCH,
            },
        };
        float const ratio = std::clamp(
            editor.split_nodes[split].ratio, EDITOR_SPLIT_MIN_RATIO, EDITOR_SPLIT_MAX_RATIO
        );
        if (node.kind == EditorSplitKind::VERTICAL) {
            if (auto row = ui.row(editor_split_id(split), desc)) {
                draw_editor_split_ui(
                    ui, editor, icon_font, palette, input, node.first, gui::fill(ratio), gui::fill()
                );
                draw_editor_split_resizer(ui, editor, input, split, node.kind);
                draw_editor_split_ui(
                    ui,
                    editor,
                    icon_font,
                    palette,
                    input,
                    node.second,
                    gui::fill(1.0f - ratio),
                    gui::fill()
                );
            }
        } else if (auto column = ui.column(editor_split_id(split), desc)) {
            draw_editor_split_ui(
                ui, editor, icon_font, palette, input, node.first, gui::fill(), gui::fill(ratio)
            );
            draw_editor_split_resizer(ui, editor, input, split, node.kind);
            draw_editor_split_ui(
                ui,
                editor,
                icon_font,
                palette,
                input,
                node.second,
                gui::fill(),
                gui::fill(1.0f - ratio)
            );
        }
    }

    auto draw_editor_ui(
        gui::Frame& ui,
        EditorState& editor,
        font_cache::Font,
        font_cache::Font icon_font,
        Palette const& palette,
        float client_width,
        gui::InputState const& input
    ) -> void {
        if (editor.sidebar_visible) {
            ensure_filesystem_panel(editor);
        }
        update_sidebar_resize(editor, client_width, input);
        if (editor.close_current_requested) {
            close_current_file(editor);
            editor.close_current_requested = false;
        }
        if (editor.file_search_open_file != FILE_SEARCH_NO_FILE) {
            size_t const tree_file_index = editor.file_search_open_file;
            editor.file_search_open_file = FILE_SEARCH_NO_FILE;
            open_file_search_match(editor, tree_file_index);
        }
        remember_open_file(editor, editor.current_file_name, editor.current_file_path);
        char const* mode = editor.insert_mode ? "INSERT" : "NORMAL";
        if (!editor.insert_mode && editor.selection_mode == EditorSelectionMode::CHARACTER) {
            mode = "VISUAL";
        } else if (!editor.insert_mode && editor.selection_mode == EditorSelectionMode::LINE) {
            mode = "V-LINE";
        }
        gui::Color const mode_color =
            editor.insert_mode ? palette.mode_insert : palette.mode_normal;

        if (auto shell = ui.column(
                gui::id("app_shell"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(12.0f),
                            .gap = 10.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {.background = palette.shell},
                }
            )) {
            if (auto header = ui.row(
                    gui::id("header"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(40.0f),
                                .padding = gui::insets(0.0f, OPEN_TAB_HEADER_PADDING),
                                .gap = 6.0f,
                                .align_y = gui::Align::CENTER,
                                .clip = true,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 8.0f,
                        },
                    }
                )) {
                if (auto tabs = ui.scroll_panel(
                        gui::id("open_file_tabs"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(36.0f),
                                .align_y = gui::Align::CENTER,
                                .clip = true,
                                .scroll_x = true,
                                .show_scrollbars = false,
                            },
                        }
                    )) {
                    if (auto tab_row = ui.row(
                            gui::id("open_file_tab_row"),
                            {
                                .layout = {
                                    .width = gui::children(),
                                    .height = gui::px(OPEN_TAB_HEIGHT),
                                    .gap = OPEN_TAB_GAP,
                                    .align_y = gui::Align::CENTER,
                                },
                            }
                        )) {
                        size_t selected_index = static_cast<size_t>(-1);
                        size_t closed_index = static_cast<size_t>(-1);
                        for (size_t index = 0u; index < editor.open_files.size(); ++index) {
                            OpenFileTabSignal const signal = draw_open_file_tab(
                                ui, editor, palette, editor.open_files[index], index
                            );
                            if (signal.closed) {
                                closed_index = index;
                            } else if (signal.selected) {
                                selected_index = index;
                            }
                        }
                        if (closed_index != static_cast<size_t>(-1)) {
                            close_open_file(editor, closed_index);
                        } else if (selected_index != static_cast<size_t>(-1)) {
                            focus_first_code_split(editor);
                            OpenFile const file = editor.open_files[selected_index];
                            BASE_UNUSED(open_file(editor, file.name, file.path));
                        }
                    }
                }
            }

            if (auto body = ui.row(
                    gui::id("body"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .align_y = gui::Align::STRETCH,
                        },
                    }
                )) {
                draw_editor_split_ui(
                    ui,
                    editor,
                    icon_font,
                    palette,
                    input,
                    editor.root_split,
                    gui::fill(),
                    gui::fill()
                );
            }

            if (auto status = ui.row(
                    gui::id("status"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(30.0f),
                                .padding = gui::insets(0.0f, 12.0f),
                                .gap = 10.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        .style = {
                            .background = palette.panel,
                            .border = palette.border,
                            .border_thickness = 1.0f,
                            .radius = 8.0f,
                        },
                    }
                )) {
                ui.label(
                    mode,
                    {
                        .layout = {.width = gui::px(74.0f), .height = gui::fill()},
                        .style = {
                            .foreground = mode_color,
                            .font_size = editor_scaled_font_size(editor, 12.0f),
                        },
                    }
                );
                ui.label(
                    fmt::tprintf(
                        "Ln %zu, Col %zu", editor.cursor_line + 1u, editor.cursor_column + 1u
                    ),
                    {
                        .layout = {.width = gui::px(128.0f), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font_size = editor_scaled_font_size(editor, 12.0f),
                        },
                    }
                );
                ui.label(
                    "UTF-8",
                    {
                        .layout = {.width = gui::px(58.0f), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font_size = editor_scaled_font_size(editor, 12.0f),
                        },
                    }
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                ui.label(
                    "code_editor",
                    {
                        .layout = {.width = gui::text(), .height = gui::fill()},
                        .style = {
                            .foreground = palette.muted,
                            .font_size = editor_scaled_font_size(editor, 12.0f),
                        },
                    }
                );
            }

            if (editor.file_search_open) {
                draw_file_search_picker(ui, editor, palette);
            }
        }
    }

} // namespace code_editor
