#include "ui_theme.h"

namespace ui_api_testbed {

    auto dark_liquid_glass_spec() -> LiquidGlassSpec {
        return {
            .tokens =
                {
                    .canvas = gui::rgba(5, 9, 15, 128),
                    .panel = gui::rgba(14, 20, 28, 238),
                    .control = gui::rgba(246, 250, 255, 34),
                    .control_hovered = gui::rgba(246, 250, 255, 54),
                    .control_active = gui::rgba(246, 250, 255, 74),
                    .accent = gui::rgba(65, 142, 255, 226),
                    .danger = gui::rgba(255, 91, 86, 78),
                    .text = gui::rgb(246, 249, 252),
                    .text_muted = gui::rgba(222, 232, 242, 176),
                    .border = gui::rgba(255, 255, 255, 68),
                    .disabled_text = gui::rgba(222, 232, 242, 94),
                    .radius_sm = 10.0f,
                    .radius_md = 14.0f,
                },
            .panel_border = gui::rgba(255, 255, 255, 38),
            .panel_shadow = gui::rgba(0, 0, 0, 54),
            .control_read_only_background = gui::rgba(246, 250, 255, 22),
            .control_disabled_background = gui::rgba(246, 250, 255, 16),
            .control_disabled_border = gui::rgba(255, 255, 255, 26),
            .accent_border = gui::rgba(255, 255, 255, 76),
            .accent_hovered_background = gui::rgba(88, 164, 255, 235),
            .accent_active_background = gui::rgba(44, 118, 235, 238),
            .danger_foreground = gui::rgb(255, 238, 238),
            .danger_border = gui::rgba(255, 145, 140, 112),
            .danger_hovered_background = gui::rgba(255, 112, 108, 126),
            .danger_hovered_border = gui::rgba(255, 166, 160, 154),
            .danger_active_background = gui::rgba(220, 70, 66, 178),
            .danger_active_border = gui::rgba(255, 190, 186, 186),
            .popup_background = gui::rgba(24, 33, 45, 246),
            .popup_border = gui::rgba(255, 255, 255, 96),
            .popup_shadow = gui::rgba(0, 0, 0, 134),
            .modal_scrim = gui::rgba(2, 7, 12, 112),
            .tab_bar_background = gui::rgba(246, 250, 255, 18),
            .tab_bar_border = gui::rgba(255, 255, 255, 38),
            .table_cell_background = gui::rgba(15, 21, 29, 232),
            .table_cell_foreground = gui::rgba(234, 240, 246, 222),
            .table_header_background = gui::rgba(27, 35, 45, 238),
            .input_background = gui::rgba(246, 250, 255, 24),
            .input_hovered_background = gui::rgba(246, 250, 255, 32),
            .input_active_background = gui::rgba(246, 250, 255, 38),
            .multiline_input_background = gui::rgba(6, 10, 15, 132),
            .input_focus_border = gui::rgba(142, 210, 255, 126),
            .header_background = gui::rgba(246, 250, 255, 32),
            .header_border = gui::rgba(255, 255, 255, 64),
            .header_shadow = gui::rgba(0, 0, 0, 64),
            .controls_background = gui::rgba(246, 250, 255, 34),
            .controls_border = gui::rgba(255, 255, 255, 76),
            .controls_shadow = gui::rgba(0, 0, 0, 62),
            .toolbar_group_background = gui::rgba(255, 255, 255, 13),
            .toolbar_group_border = gui::rgba(255, 255, 255, 34),
            .sidebar_background = gui::rgba(18, 24, 32, 180),
            .sidebar_border = gui::rgba(255, 255, 255, 54),
            .sidebar_shadow = gui::rgba(0, 0, 0, 54),
            .list_background = gui::rgba(6, 10, 16, 142),
            .list_border = gui::rgba(255, 255, 255, 36),
            .selected_row_border = gui::rgba(255, 255, 255, 96),
            .notes_background = gui::rgba(7, 12, 18, 146),
            .notes_border = gui::rgba(255, 255, 255, 34),
            .body_text_background = gui::rgba(17, 23, 31, 244),
            .body_text_border = gui::rgba(255, 255, 255, 34),
            .preview_input_background = gui::rgba(18, 24, 32, 246),
            .preview_input_border = gui::rgba(255, 255, 255, 42),
            .preview_table_background = gui::rgba(11, 16, 23, 242),
            .preview_table_border = gui::rgba(255, 255, 255, 28),
            .log_background = gui::rgba(12, 17, 24, 246),
            .log_foreground = gui::rgba(232, 238, 245, 220),
            .log_border = gui::rgba(255, 255, 255, 34),
            .modal_dialog_background = gui::rgba(24, 33, 45, 234),
            .modal_dialog_border = gui::rgba(255, 255, 255, 92),
            .modal_dialog_shadow = gui::rgba(0, 0, 0, 126),
            .modal_body_foreground = gui::rgba(232, 238, 245, 206),
        };
    }

    auto light_liquid_glass_spec() -> LiquidGlassSpec {
        return {
            .tokens =
                {
                    .canvas = gui::rgba(248, 250, 253, 168),
                    .panel = gui::rgba(255, 255, 255, 224),
                    .control = gui::rgba(255, 255, 255, 174),
                    .control_hovered = gui::rgba(255, 255, 255, 216),
                    .control_active = gui::rgba(226, 240, 255, 228),
                    .accent = gui::rgba(0, 113, 227, 224),
                    .danger = gui::rgba(255, 96, 92, 92),
                    .text = gui::rgb(26, 32, 42),
                    .text_muted = gui::rgba(62, 73, 88, 190),
                    .border = gui::rgba(75, 90, 110, 54),
                    .disabled_text = gui::rgba(86, 98, 114, 114),
                    .radius_sm = 10.0f,
                    .radius_md = 14.0f,
                },
            .panel_border = gui::rgba(78, 94, 114, 42),
            .panel_shadow = gui::rgba(29, 47, 72, 28),
            .control_read_only_background = gui::rgba(235, 241, 248, 150),
            .control_disabled_background = gui::rgba(229, 236, 244, 105),
            .control_disabled_border = gui::rgba(82, 96, 112, 26),
            .accent_border = gui::rgba(255, 255, 255, 116),
            .accent_hovered_background = gui::rgba(0, 132, 255, 235),
            .accent_active_background = gui::rgba(0, 92, 196, 238),
            .danger_foreground = gui::rgb(118, 24, 22),
            .danger_border = gui::rgba(255, 117, 112, 120),
            .danger_hovered_background = gui::rgba(255, 116, 112, 116),
            .danger_hovered_border = gui::rgba(238, 84, 80, 146),
            .danger_active_background = gui::rgba(255, 126, 122, 150),
            .danger_active_border = gui::rgba(218, 62, 58, 166),
            .popup_background = gui::rgba(255, 255, 255, 244),
            .popup_border = gui::rgba(82, 98, 118, 58),
            .popup_shadow = gui::rgba(22, 36, 58, 70),
            .modal_scrim = gui::rgba(246, 249, 253, 142),
            .tab_bar_background = gui::rgba(255, 255, 255, 150),
            .tab_bar_border = gui::rgba(78, 94, 114, 38),
            .table_cell_background = gui::rgba(255, 255, 255, 214),
            .table_cell_foreground = gui::rgba(37, 45, 56, 226),
            .table_header_background = gui::rgba(236, 242, 249, 232),
            .input_background = gui::rgba(255, 255, 255, 192),
            .input_hovered_background = gui::rgba(255, 255, 255, 220),
            .input_active_background = gui::rgba(255, 255, 255, 238),
            .multiline_input_background = gui::rgba(255, 255, 255, 232),
            .input_focus_border = gui::rgba(0, 122, 255, 140),
            .header_background = gui::rgba(255, 255, 255, 172),
            .header_border = gui::rgba(78, 94, 114, 38),
            .header_shadow = gui::rgba(29, 47, 72, 34),
            .controls_background = gui::rgba(255, 255, 255, 166),
            .controls_border = gui::rgba(78, 94, 114, 42),
            .controls_shadow = gui::rgba(29, 47, 72, 30),
            .toolbar_group_background = gui::rgba(237, 244, 252, 130),
            .toolbar_group_border = gui::rgba(78, 94, 114, 34),
            .sidebar_background = gui::rgba(255, 255, 255, 160),
            .sidebar_border = gui::rgba(78, 94, 114, 42),
            .sidebar_shadow = gui::rgba(29, 47, 72, 30),
            .list_background = gui::rgba(246, 249, 253, 178),
            .list_border = gui::rgba(78, 94, 114, 34),
            .selected_row_border = gui::rgba(255, 255, 255, 180),
            .notes_background = gui::rgba(246, 249, 253, 176),
            .notes_border = gui::rgba(78, 94, 114, 34),
            .body_text_background = gui::rgba(255, 255, 255, 232),
            .body_text_border = gui::rgba(78, 94, 114, 36),
            .preview_input_background = gui::rgba(255, 255, 255, 238),
            .preview_input_border = gui::rgba(78, 94, 114, 40),
            .preview_table_background = gui::rgba(247, 250, 253, 238),
            .preview_table_border = gui::rgba(78, 94, 114, 34),
            .log_background = gui::rgba(247, 250, 253, 226),
            .log_foreground = gui::rgba(45, 56, 70, 216),
            .log_border = gui::rgba(78, 94, 114, 34),
            .modal_dialog_background = gui::rgba(255, 255, 255, 244),
            .modal_dialog_border = gui::rgba(82, 98, 118, 58),
            .modal_dialog_shadow = gui::rgba(22, 36, 58, 82),
            .modal_body_foreground = gui::rgba(55, 67, 82, 210),
        };
    }

    auto liquid_glass_spec(LiquidGlassTheme theme) -> LiquidGlassSpec {
        if (theme == LiquidGlassTheme::LIGHT) {
            return light_liquid_glass_spec();
        }
        return dark_liquid_glass_spec();
    }

    auto configure_liquid_glass_theme(
        gui::ThemeDesc& theme, gui::font_cache::Font font, LiquidGlassSpec const& spec
    ) -> void {
        theme.tokens = spec.tokens;
        gui::ThemeTokens const& tokens = theme.tokens;

        theme.root = {.foreground = tokens.text, .font = font, .font_size = 13.0f};
        gui::theme_role(theme, gui::StyleRole::CANVAS).normal = {
            .background = tokens.canvas,
            .foreground = tokens.text,
        };
        gui::theme_role(theme, gui::StyleRole::PANEL).normal = {
            .background = tokens.panel,
            .foreground = tokens.text,
            .border = spec.panel_border,
            .border_thickness = 1.0f,
            .radius = 18.0f,
            .shadow = {.offset = {0.0f, 16.0f}, .blur_radius = 32.0f, .color = spec.panel_shadow},
        };
        gui::theme_role(theme, gui::StyleRole::CONTROL) = {
            .normal =
                {
                    .background = tokens.control,
                    .foreground = tokens.text,
                    .border = tokens.border,
                    .border_thickness = 1.0f,
                    .radius = tokens.radius_md,
                },
            .hovered = {.background = tokens.control_hovered},
            .active = {.background = tokens.control_active},
            .read_only =
                {.background = spec.control_read_only_background, .foreground = tokens.text_muted},
            .disabled = {
                .background = spec.control_disabled_background,
                .foreground = tokens.disabled_text,
                .border = spec.control_disabled_border
            },
        };
        gui::theme_role(theme, gui::StyleRole::ACCENT) = {
            .normal =
                {
                    .background = tokens.accent,
                    .foreground = gui::rgb(255, 255, 255),
                    .border = spec.accent_border,
                    .border_thickness = 1.0f,
                    .radius = tokens.radius_md,
                },
            .hovered = {.background = spec.accent_hovered_background},
            .active = {.background = spec.accent_active_background},
        };
        gui::theme_role(theme, gui::StyleRole::DANGER) = {
            .normal =
                {
                    .background = tokens.danger,
                    .foreground = spec.danger_foreground,
                    .border = spec.danger_border,
                    .border_thickness = 1.0f,
                    .radius = tokens.radius_md,
                },
            .hovered =
                {
                    .background = spec.danger_hovered_background,
                    .border = spec.danger_hovered_border,
                },
            .active = {
                .background = spec.danger_active_background,
                .border = spec.danger_active_border,
            },
        };

        gui::theme_kind(theme, gui::BoxKind::POPUP).style.normal = {
            .background = spec.popup_background,
            .border = spec.popup_border,
            .border_thickness = 1.0f,
            .radius = 20.0f,
            .shadow = {
                .offset = {0.0f, 24.0f},
                .blur_radius = 54.0f,
                .spread = 2.0f,
                .color = spec.popup_shadow,
            },
        };
        gui::theme_kind(theme, gui::BoxKind::MODAL).style.normal.background = spec.modal_scrim;
        gui::theme_kind(theme, gui::BoxKind::TAB_VIEW).role = gui::StyleRole::NONE;
        gui::theme_kind(theme, gui::BoxKind::TAB_BODY).role = gui::StyleRole::NONE;
        gui::theme_kind(theme, gui::BoxKind::TAB_BAR).style.normal = {
            .background = spec.tab_bar_background,
            .border = spec.tab_bar_border,
            .border_thickness = 1.0f,
            .radius = 17.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::TABLE).style.normal = {
            .background = spec.preview_table_background,
            .border = spec.preview_table_border,
            .border_thickness = 1.0f,
            .radius = 14.0f,
            .shadow =
                {
                    .offset = {0.0f, 6.0f},
                    .blur_radius = 18.0f,
                    .color = spec.panel_shadow,
                },
            .font_size = 12.5f,
        };
        gui::theme_kind(theme, gui::BoxKind::TABLE_CELL).style.normal = {
            .background = spec.table_cell_background,
            .foreground = spec.table_cell_foreground,
            .border = gui::rgba(0, 0, 0, 0),
            .border_thickness = 1.0f,
            .radius = 8.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::TABLE_HEADER_CELL).style.normal = {
            .background = spec.table_header_background,
            .foreground = tokens.text,
            .border = gui::rgba(0, 0, 0, 0),
            .border_thickness = 1.0f,
            .radius = 9.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).style.normal.background =
            spec.input_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).style.hovered.background =
            spec.input_hovered_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).style.active.background =
            spec.input_active_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT_MULTILINE).style.normal.background =
            spec.multiline_input_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT_MULTILINE).style.hovered.background =
            spec.multiline_input_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT_MULTILINE).style.active.background =
            spec.multiline_input_background;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).style.focused.border =
            spec.input_focus_border;
        gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT_MULTILINE).style.focused.border =
            spec.input_focus_border;
    }

    auto liquid_glass_clear_color(LiquidGlassTheme theme) -> gui::render::Color {
        if (theme == LiquidGlassTheme::LIGHT) {
            return {0.94f, 0.965f, 0.985f, 1.0f};
        }
        return {0.010f, 0.014f, 0.020f, 1.0f};
    }

} // namespace ui_api_testbed
