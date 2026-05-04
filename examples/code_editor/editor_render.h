#pragma once

#include "editor_model.h"
#include "editor_theme.h"

#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <gui/gui.h>

namespace code_editor {

    auto draw_editor_surface(
        gui::draw::Context draw_context,
        gui::font_cache::Font editor_font,
        EditorState& editor,
        float char_width,
        gui::Frame const& ui,
        gui::InputState const& input,
        Palette const& palette
    ) -> void;
    auto draw_editor_ui(
        gui::Frame& ui,
        EditorState& editor,
        gui::font_cache::Font ui_font,
        gui::font_cache::Font icon_font,
        Palette const& palette,
        float client_width,
        gui::InputState const& input
    ) -> void;

} // namespace code_editor
