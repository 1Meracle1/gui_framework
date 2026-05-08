#pragma once

#include "editor_model.h"
#include "editor_theme.h"

#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <gui/gui.h>

namespace code_editor {

    [[nodiscard]] auto editor_file_write_stamp(StrRef path) -> uint64_t;
    [[nodiscard]] auto editor_path_exists(StrRef path) -> bool;
    [[nodiscard]] auto editor_write_text_file(StrRef path, StrRef text) -> bool;
    [[nodiscard]] auto editor_open_path(EditorState& editor, StrRef path) -> bool;
    auto sync_tree_operation_result(EditorState& editor) -> void;
    auto update_open_file_changes(EditorState& editor) -> void;
    auto draw_editor_surface(
        gui::draw::Context draw_context,
        gui::font_cache::Font editor_font,
        gui::font_cache::Font ui_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette,
        bool selection_visible
    ) -> void;
    auto draw_editor_ui(
        gui::Frame& ui,
        EditorState& editor,
        gui::font_cache::Font editor_font,
        gui::font_cache::Font ui_font,
        gui::font_cache::Font icon_font,
        Palette const& palette,
        float client_width,
        float client_height,
        float char_width,
        gui::InputState const& input
    ) -> void;

} // namespace code_editor
