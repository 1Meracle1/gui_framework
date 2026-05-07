#pragma once

#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <gui/gui.h>

namespace code_editor {

    struct Palette {
        gui::Color shell = gui::rgb(13, 17, 22);
        gui::Color panel = gui::rgb(18, 23, 30);
        gui::Color panel_raised = gui::rgb(24, 30, 38);
        gui::Color control_hovered = gui::rgb(31, 39, 49);
        gui::Color control_active = gui::rgb(37, 49, 62);
        gui::Color border = gui::rgb(49, 58, 70);
        gui::Color text = gui::rgb(224, 230, 236);
        gui::Color muted = gui::rgb(126, 139, 153);
        gui::Color faint = gui::rgb(82, 94, 108);
        gui::Color cursor_line = gui::rgb(27, 36, 46);
        gui::Color cursor = gui::rgb(82, 172, 255);
        gui::Color keyword = gui::rgb(132, 178, 255);
        gui::Color type = gui::rgb(86, 211, 178);
        gui::Color string = gui::rgb(238, 172, 105);
        gui::Color number = gui::rgb(202, 156, 255);
        gui::Color comment = gui::rgb(103, 132, 112);
        gui::Color preprocessor = gui::rgb(255, 116, 132);
        gui::Color punctuation = gui::rgb(153, 166, 181);
        gui::Color function = gui::rgb(220, 220, 170);
        gui::Color mode_insert = gui::rgb(80, 200, 146);
        gui::Color mode_normal = gui::rgb(82, 172, 255);
    };

    [[nodiscard]] auto to_draw_color(gui::Color color) -> gui::draw::Color;
    [[nodiscard]] auto
    code_editor_theme(gui::font_cache::Font font, Palette const& palette, float font_size)
        -> gui::ThemeDesc;

} // namespace code_editor
