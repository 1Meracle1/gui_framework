#include "ui_theme.h"

namespace repository_ui_testbed {

    namespace font_cache = gui::font_cache;

    [[nodiscard]] auto repo_theme(font_cache::Font font, RepositorySpec const& spec)
        -> gui::ThemeDesc {
        gui::ThemeDesc theme = gui::default_theme();
        theme.tokens = {
            .canvas = spec.shell,
            .panel = spec.panel,
            .control = spec.control,
            .control_hovered = spec.control_hovered,
            .control_active = spec.selected,
            .accent = spec.blue,
            .danger = spec.red,
            .text = spec.text,
            .text_muted = spec.muted,
            .border = spec.border,
            .disabled_text = spec.faint,
            .radius_sm = 5.0f,
            .radius_md = 7.0f,
            .border_thickness = 1.0f,
        };

        theme.root = {.foreground = spec.text, .font = font, .font_size = 13.0f};
        gui::theme_role(theme, gui::StyleRole::CANVAS).normal = {
            .background = spec.shell,
            .foreground = spec.text,
        };
        gui::theme_role(theme, gui::StyleRole::PANEL).normal = {
            .background = spec.panel,
            .foreground = spec.text,
            .border = spec.border,
            .border_thickness = 1.0f,
            .radius = 8.0f,
        };
        gui::theme_role(theme, gui::StyleRole::TEXT).normal = {.foreground = spec.text};
        gui::theme_role(theme, gui::StyleRole::TEXT_MUTED).normal = {.foreground = spec.muted};
        gui::theme_role(theme, gui::StyleRole::CONTROL) = {
            .normal =
                {
                    .background = spec.control,
                    .foreground = spec.text,
                    .border = spec.border,
                    .border_thickness = 1.0f,
                    .radius = 6.0f,
                },
            .hovered = {.background = spec.control_hovered},
            .active = {.background = spec.selected},
        };
        gui::theme_role(theme, gui::StyleRole::ACCENT).normal = {
            .background = spec.selected,
            .foreground = spec.text,
            .border = spec.strong_border,
            .border_thickness = 1.0f,
            .radius = 6.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::ROOT).role = gui::StyleRole::CANVAS;
        gui::theme_kind(theme, gui::BoxKind::LABEL).role = gui::StyleRole::TEXT;
        gui::theme_kind(theme, gui::BoxKind::ROW).style.hovered.background = spec.control_hovered;
        gui::theme_kind(theme, gui::BoxKind::ROW).style.active.background = spec.selected;
        gui::theme_kind(theme, gui::BoxKind::BUTTON).role = gui::StyleRole::CONTROL;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).role = gui::StyleRole::CONTROL;
        return theme;
    }

} // namespace repository_ui_testbed
