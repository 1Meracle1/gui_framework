#include "editor_theme.h"

namespace code_editor {

    [[nodiscard]] auto to_draw_color(gui::Color color) -> gui::draw::Color {
        return {color.r, color.g, color.b, color.a < 0.0f ? 1.0f : color.a};
    }

    [[nodiscard]] auto
    code_editor_theme(gui::font_cache::Font font, Palette const& palette, float font_size)
        -> gui::ThemeDesc {
        gui::ThemeDesc theme = gui::default_theme();
        theme.tokens = {
            .canvas = palette.shell,
            .panel = palette.panel,
            .control = palette.panel_raised,
            .control_hovered = palette.control_hovered,
            .control_active = palette.control_active,
            .accent = palette.cursor,
            .danger = palette.preprocessor,
            .text = palette.text,
            .text_muted = palette.muted,
            .border = palette.border,
            .disabled_text = palette.faint,
            .radius_sm = 5.0f,
            .radius_md = 7.0f,
            .border_thickness = 1.0f,
        };
        theme.root = {.foreground = palette.text, .font = font, .font_size = font_size};
        gui::theme_role(theme, gui::StyleRole::CANVAS).normal = {
            .background = palette.shell,
            .foreground = palette.text,
        };
        gui::theme_role(theme, gui::StyleRole::PANEL).normal = {
            .background = palette.panel,
            .foreground = palette.text,
            .border = palette.border,
            .border_thickness = 1.0f,
            .radius = 8.0f,
        };
        gui::theme_role(theme, gui::StyleRole::CONTROL).normal = {
            .background = palette.panel_raised,
            .foreground = palette.text,
            .border = palette.border,
            .border_thickness = 1.0f,
            .radius = 6.0f,
        };
        gui::theme_role(theme, gui::StyleRole::CONTROL).hovered = {
            .background = palette.control_hovered,
        };
        gui::theme_role(theme, gui::StyleRole::CONTROL).active = {
            .background = palette.control_active,
        };
        gui::theme_role(theme, gui::StyleRole::TEXT).normal = {.foreground = palette.text};
        gui::theme_role(theme, gui::StyleRole::TEXT_MUTED).normal = {.foreground = palette.muted};
        gui::theme_kind(theme, gui::BoxKind::ROOT).role = gui::StyleRole::CANVAS;
        gui::theme_kind(theme, gui::BoxKind::LABEL).role = gui::StyleRole::TEXT;
        gui::theme_kind(theme, gui::BoxKind::BUTTON).role = gui::StyleRole::CONTROL;
        return theme;
    }

} // namespace code_editor
