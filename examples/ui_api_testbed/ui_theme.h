#pragma once

#include "ui_types.h"

namespace ui_api_testbed {

    struct LiquidGlassSpec {
        gui::ThemeTokens tokens = {};
        gui::Color panel_border = {};
        gui::Color panel_shadow = {};
        gui::Color control_read_only_background = {};
        gui::Color control_disabled_background = {};
        gui::Color control_disabled_border = {};
        gui::Color accent_border = {};
        gui::Color accent_hovered_background = {};
        gui::Color accent_active_background = {};
        gui::Color danger_foreground = {};
        gui::Color danger_border = {};
        gui::Color danger_hovered_background = {};
        gui::Color danger_hovered_border = {};
        gui::Color danger_active_background = {};
        gui::Color danger_active_border = {};
        gui::Color popup_background = {};
        gui::Color popup_border = {};
        gui::Color popup_shadow = {};
        gui::Color modal_scrim = {};
        gui::Color tab_bar_background = {};
        gui::Color tab_bar_border = {};
        gui::Color table_cell_background = {};
        gui::Color table_cell_foreground = {};
        gui::Color table_header_background = {};
        gui::Color input_background = {};
        gui::Color input_hovered_background = {};
        gui::Color input_active_background = {};
        gui::Color multiline_input_background = {};
        gui::Color input_focus_border = {};
        gui::Color header_background = {};
        gui::Color header_border = {};
        gui::Color header_shadow = {};
        gui::Color controls_background = {};
        gui::Color controls_border = {};
        gui::Color controls_shadow = {};
        gui::Color toolbar_group_background = {};
        gui::Color toolbar_group_border = {};
        gui::Color sidebar_background = {};
        gui::Color sidebar_border = {};
        gui::Color sidebar_shadow = {};
        gui::Color list_background = {};
        gui::Color list_border = {};
        gui::Color selected_row_border = {};
        gui::Color notes_background = {};
        gui::Color notes_border = {};
        gui::Color body_text_background = {};
        gui::Color body_text_border = {};
        gui::Color preview_input_background = {};
        gui::Color preview_input_border = {};
        gui::Color preview_table_background = {};
        gui::Color preview_table_border = {};
        gui::Color log_background = {};
        gui::Color log_foreground = {};
        gui::Color log_border = {};
        gui::Color modal_dialog_background = {};
        gui::Color modal_dialog_border = {};
        gui::Color modal_dialog_shadow = {};
        gui::Color modal_body_foreground = {};
    };

    [[nodiscard]] auto liquid_glass_spec(LiquidGlassTheme theme) -> LiquidGlassSpec;
    auto configure_liquid_glass_theme(
        gui::ThemeDesc& theme, gui::font_cache::Font font, LiquidGlassSpec const& spec
    ) -> void;
    [[nodiscard]] auto liquid_glass_clear_color(LiquidGlassTheme theme) -> gui::render::Color;

} // namespace ui_api_testbed
