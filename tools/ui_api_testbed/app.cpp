#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <ui_api_testbed_embedded_texture.h>
#include <windows.h>
#endif
#include <gui/gui.h>

namespace ui_api_testbed {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;
#endif

    enum class LiquidGlassTheme : uint8_t {
        DARK,
        LIGHT,
    };

    inline constexpr size_t PREVIEW_TABLE_COLUMN_COUNT = 3u;
    inline constexpr size_t PREVIEW_TABLE_ROW_COUNT = 4u;
    inline constexpr size_t SAMPLE_TABLE_COLUMN_COUNT = 3u;
    inline constexpr size_t SAMPLE_TABLE_ROW_COUNT = 4u;

    struct TestbedState {
        bool enabled = true;
        bool preview = true;
        bool read_only_value = false;
        bool reveal_asset_list = true;
        bool reveal_log_scroll = true;
        bool popup_open = false;
        bool modal_open = false;
        bool sample_enabled = true;
        bool sample_preview = false;
        LiquidGlassTheme theme = LiquidGlassTheme::DARK;
        float scale = 1.25f;
        float image_preview_zoom = 1.0f;
        float sample_value = 0.5f;
        float sample_loading_phase = 0.0f;
        size_t selected_tab = 0u;
        size_t selected_index = 12u;
        size_t size_mode = 1u;
        size_t image_preview_sample = 0u;
        size_t preview_table_sort_count = 0u;
        size_t sample_table_sort_count = 0u;
        char name[64] = "Editable text";
        char sample_name[64] = "Second tab text";
        gui::TableSortColumn preview_table_sort_columns[PREVIEW_TABLE_COLUMN_COUNT] = {};
        gui::TableSortColumn sample_table_sort_columns[SAMPLE_TABLE_COLUMN_COUNT] = {};
        gui::TableFilterColumn sample_table_filter_columns[SAMPLE_TABLE_COLUMN_COUNT] = {};
        gui::TableFilterValue sample_table_item_filter_values[SAMPLE_TABLE_ROW_COUNT] = {
            {"Checkbox"}, {"Popup"}, {"Slider"}, {"Table"}
        };
        gui::TableFilterValue sample_table_layer_filter_values[3] = {
            {"Input"}, {"Overlay"}, {"Layout"}
        };
        gui::TableFilterValue sample_table_state_filter_values[SAMPLE_TABLE_ROW_COUNT] = {
            {"Enabled"}, {"Closed"}, {"Ready"}, {"Sortable"}
        };
        bool preview_table_selected_columns[PREVIEW_TABLE_COLUMN_COUNT] = {true, true, false};
        bool sample_table_selected_columns[SAMPLE_TABLE_COLUMN_COUNT] = {true, true, false};
        bool sample_table_filter_open[SAMPLE_TABLE_COLUMN_COUNT] = {};
        char sample_table_item_filter[32] = {};
        char sample_table_layer_filter[32] = {};
        char sample_table_state_filter[32] = {};
        gui::TextSelection title_selection = {};
        gui::TextSelection body_selection = {};
        gui::Signal header_signal = {};
        gui::Signal selected_row_signal = {};
        StringBuffer multiline_text_buffer;
    };

    struct TextureSample {
        gui::render::Texture texture = {};
        gui::Vec2 size = {};
    };

    struct TestbedTextures {
        TextureSample disk = {};
        TextureSample embedded = {};
    };

    auto indexed_id(StrRef scope, uint64_t index) -> gui::Id {
        return gui::id(gui::id(scope), index);
    }

    constexpr char BODY_TEXT[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.m dolor sit amet, "
        "consecteturdolor sit amet, consectetur "
        "adipiscing elit.m dolor sit amet, consectetur adipiscing elit.\n"
        "Integer posuere erat a ante venenatis dapibus posuere velit aliquet.\n\n"
        "Donec ullamcorper nulla non metus auctor fringilla.\n"
        "Cras mattis consectetur purus sit amet fermentum.\n\n"
        "Maecenas sed diam eget risus varius blandit sit amet non magna.\n"
        "Vestibulum id ligula porta felis euismod semper.";
    constexpr char CLOSE_GLYPH[] = "\xC3\x97";

    struct PreviewTableRow {
        StrRef group = {};
        StrRef task = {};
        StrRef status = {};
    };

    constexpr PreviewTableRow PREVIEW_TABLE_ROWS[PREVIEW_TABLE_ROW_COUNT] = {
        {"Layout", "Build table element", "Active"},
        {"Input", "Edit multiline text", "Ready"},
        {"Render", "Clip table cells", "Done"},
        {"Overlay", "Stack floating layers", "Open"},
    };

    struct SampleTableRow {
        StrRef item = {};
        StrRef layer = {};
        StrRef state = {};
    };

    constexpr SampleTableRow SAMPLE_TABLE_ROWS[SAMPLE_TABLE_ROW_COUNT] = {
        {"Checkbox", "Input", "Enabled"},
        {"Popup", "Overlay", "Closed"},
        {"Slider", "Input", "Ready"},
        {"Table", "Layout", "Sortable"},
    };

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

    auto draw_scroll_lines(gui::Frame& ui, StrRef prefix, size_t count) -> void {
        for (size_t index = 0u; index < count; ++index) {
            ui.label(
                fmt::tprintf("%s %02zu", prefix, index),
                {.layout = {.width = gui::fill(), .height = gui::px(22.0f)}}
            );
        }
    }

    auto draw_preview_table_sort_control(
        gui::Frame& ui,
        gui::TableScope& table,
        TestbedState& state,
        gui::TableSortDesc const& desc,
        gui::Id checkbox_id,
        StrRef label,
        size_t column,
        float checkbox_width
    ) -> void {
        BASE_UNUSED(table.sort_button(column, desc));
        ui.checkbox(
            checkbox_id,
            label,
            state.preview_table_selected_columns + column,
            {.layout = {.width = gui::px(checkbox_width), .height = gui::fill()}}
        );
    }

    auto draw_sample_table_cell(
        gui::Frame& ui, gui::TableRowScope& row, gui::Id id, StrRef text, float width
    ) -> void {
        if (auto cell = row.cell(
                id, {.box = {.layout = {.width = gui::px(width), .height = gui::px(26.0f)}}}
            )) {
            ui.label(text, {.layout = {.width = gui::fill(), .height = gui::fill()}});
        }
    }

    auto draw_texture_sample(
        gui::Frame& ui,
        gui::Id id,
        StrRef title,
        TestbedState& state,
        TextureSample const& sample,
        size_t sample_index,
        LiquidGlassSpec const& spec
    ) -> void {
        if (auto card = ui.column(
                id,
                {
                    .layout =
                        {
                            .width = gui::px(220.0f),
                            .height = gui::children(),
                            .padding = gui::insets(8.0f),
                            .gap = 6.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {.role = gui::StyleRole::CONTROL},
                }
            )) {
            ui.label(
                title,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(20.0f)},
                    .style = {.foreground = spec.tokens.text_muted},
                }
            );
            if (auto preview = ui.overlay(
                    gui::id("texture_preview"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(108.0f),
                                .clip = true,
                            },
                        .style = {
                            .background = gui::rgba(0, 0, 0, 38),
                            .radius = 10.0f,
                        },
                    }
                )) {
                if (preview.signal().activated) {
                    state.image_preview_sample = sample_index;
                    state.image_preview_zoom = 1.0f;
                }
                ui.image(
                    sample.texture,
                    {
                        .box =
                            {
                                .layout = {.width = gui::fill(), .height = gui::fill()},
                            },
                        .size = sample.size,
                        .fit = gui::ImageFit::CONTAIN,
                    }
                );
            }
            ui.label(
                fmt::tprintf(
                    "%ux%u",
                    static_cast<uint32_t>(sample.size.x),
                    static_cast<uint32_t>(sample.size.y)
                ),
                {
                    .layout = {.width = gui::fill(), .height = gui::px(18.0f)},
                    .style = {.foreground = spec.tokens.text_muted, .font_size = 11.0f},
                }
            );
        }
    }

    auto color_alpha(gui::Color color, float alpha) -> gui::Color {
        color.a = alpha;
        return color;
    }

    auto loading_pulse(float phase) -> float {
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
        if (phase < 0.5f) {
            return phase * 2.0f;
        }
        return (1.0f - phase) * 2.0f;
    }

    auto loading_ring_alpha(float phase, size_t index, size_t count) -> float {
        float offset = phase - static_cast<float>(index) / static_cast<float>(count);
        if (offset < 0.0f) {
            offset += 1.0f;
        }
        float const pulse = std::max(0.0f, 1.0f - offset * 4.0f);
        return 0.18f + pulse * 0.70f;
    }

    auto draw_sample_loading_animation(
        gui::Frame& ui, TestbedState const& state, LiquidGlassSpec const& spec
    ) -> void {
        gui::ThemeTokens const& tokens = spec.tokens;
        if (auto strip = ui.row(
                gui::id("sample_loading_animation"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(54.0f),
                            .padding = gui::insets(8.0f, 14.0f),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {
                        .background = spec.controls_background,
                        .border = spec.controls_border,
                        .border_thickness = 1.0f,
                        .radius = 22.0f,
                        .shadow = {
                            .offset = {0.0f, 10.0f},
                            .blur_radius = 28.0f,
                            .color = spec.controls_shadow
                        },
                    },
                }
            )) {
            BASE_UNUSED(strip);
            ui.label(
                "Loading",
                {
                    .layout = {.width = gui::px(72.0f), .height = gui::fill()},
                    .style = {.foreground = tokens.text, .font_size = 14.0f},
                }
            );
            for (size_t index = 0u; index < 9u; ++index) {
                float const pulse =
                    loading_pulse(state.sample_loading_phase + static_cast<float>(index) * 0.09f);
                ui.spacer({
                    .layout =
                        {
                            .width = gui::px(8.0f),
                            .height = gui::px(12.0f + pulse * 24.0f),
                        },
                    .style = {
                        .background = color_alpha(tokens.accent, 0.22f + pulse * 0.62f),
                        .radius = 4.0f,
                    },
                });
            }
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
        }
    }

    auto draw_sample_circular_loading_animation(
        gui::Frame& ui, TestbedState const& state, LiquidGlassSpec const& spec
    ) -> void {
        constexpr size_t DOT_COUNT = 12u;
        constexpr gui::Vec2 DOT_POSITIONS[DOT_COUNT] = {
            {25.5f, 4.0f},
            {37.0f, 7.0f},
            {45.5f, 15.5f},
            {49.0f, 27.0f},
            {45.5f, 38.5f},
            {37.0f, 47.0f},
            {25.5f, 50.0f},
            {14.0f, 47.0f},
            {5.5f, 38.5f},
            {2.0f, 27.0f},
            {5.5f, 15.5f},
            {14.0f, 7.0f},
        };

        gui::ThemeTokens const& tokens = spec.tokens;
        float ring_phase = state.sample_loading_phase * 2.0f;
        while (ring_phase >= 1.0f) {
            ring_phase -= 1.0f;
        }

        if (auto card = ui.overlay(
                gui::id("sample_circular_loading_animation"),
                {
                    .layout =
                        {
                            .width = gui::px(74.0f),
                            .height = gui::px(74.0f),
                            .padding = gui::insets(9.0f),
                        },
                    .style = {
                        .background = spec.controls_background,
                        .border = spec.controls_border,
                        .border_thickness = 1.0f,
                        .radius = 22.0f,
                        .shadow = {
                            .offset = {0.0f, 10.0f},
                            .blur_radius = 28.0f,
                            .color = spec.controls_shadow,
                        },
                    },
                }
            )) {
            BASE_UNUSED(card);
            for (size_t index = 0u; index < DOT_COUNT; ++index) {
                gui::Vec2 const pos = DOT_POSITIONS[index];
                ui.spacer({
                    .layout =
                        {
                            .width = gui::px(5.0f),
                            .height = gui::px(5.0f),
                            .margin = gui::insets(pos.y, 0.0f, 0.0f, pos.x),
                        },
                    .style = {
                        .background = color_alpha(
                            tokens.accent, loading_ring_alpha(ring_phase, index, DOT_COUNT)
                        ),
                        .radius = 2.5f,
                    },
                });
            }
        }
    }

    auto draw_ui(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        gui::InputState const& input,
        float delta_time
    ) -> void {
        gui::Id const list_id = gui::id("asset_list");
        gui::Id const notes_id = gui::id("notes_scroll");
        gui::Id const body_text_id = gui::id("body_text_scroll");
        gui::Id const log_id = gui::id("log_scroll");
        gui::ThemeTokens const& tokens = spec.tokens;

        if (state.reveal_asset_list) {
            ui.scroll_to_index(list_id, state.selected_index, gui::ScrollReveal::KEEP_VISIBLE);
            state.reveal_asset_list = false;
        }
        if (state.reveal_log_scroll) {
            ui.scroll_to_end(log_id);
            state.reveal_log_scroll = false;
        }
        state.selected_row_signal = {};

        if (auto root = ui.column(
                gui::id("ui_api_testbed_root"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(10.0f),
                            .gap = 8.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style = {.role = gui::StyleRole::CANVAS},
                    .debug_name = "ui_api_testbed_root",
                }
            )) {

            if (auto header = ui.row(
                    gui::id("header_bar"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(40.0f),
                                .padding = gui::insets(6.0f, 14.0f),
                                .gap = 8.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        .style =
                            {
                                .role = gui::StyleRole::PANEL,
                                .background = spec.header_background,
                                .border = spec.header_border,
                                .border_thickness = 1.0f,
                                .radius = 18.0f,
                                .shadow =
                                    {.offset = {0.0f, 8.0f},
                                     .blur_radius = 26.0f,
                                     .color = spec.header_shadow},
                            },
                        .debug_name = "clickable_header_bar",
                    }
                )) {
                state.header_signal = header.signal();

                ui.label(
                    "UI API Testbed", {.layout = {.width = gui::text(), .height = gui::fill()}}
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                LiquidGlassTheme const next_theme = state.theme == LiquidGlassTheme::DARK
                                                        ? LiquidGlassTheme::LIGHT
                                                        : LiquidGlassTheme::DARK;
                char const* const theme_text =
                    next_theme == LiquidGlassTheme::LIGHT ? "Light" : "Dark";
                if (ui.button(
                          gui::id("theme_toggle"),
                          theme_text,
                          {
                              .layout =
                                  {
                                      .width = gui::px(62.0f),
                                      .height = gui::px(28.0f),
                                      .padding = gui::insets(2.0f, 6.0f),
                                  },
                              .style =
                                  {
                                      .role = gui::StyleRole::CONTROL,
                                      .radius = 14.0f,
                                  },
                              .debug_name = "theme_toggle_button",
                          }
                    )
                        .activated) {
                    state.theme = next_theme;
                }
                if (ui.button(
                          gui::id("reset_scale"),
                          "Reset",
                          {
                              .layout =
                                  {
                                      .width = gui::px(74.0f),
                                      .height = gui::px(28.0f),
                                      .padding = gui::insets(2.0f, 6.0f),
                                  },
                              .style = {.role = gui::StyleRole::DANGER},
                              .debug_name = "reset_scale_button",
                          }
                    )
                        .activated) {
                    state.scale = 1.0f;
                }
            }

            auto tab_view = ui.tab_view(
                gui::id("testbed_tabs"),
                {
                    .read_only_tabs =
                        {
                            {gui::id("testbed_main_tab"), "Testbed"},
                            {gui::id("testbed_sample_tab"), "Samples"},
                        },
                    .selected_index = &state.selected_tab,
                    .flags = 0u,
                    .box =
                        {
                            .layout = {.width = gui::fill(), .height = gui::fill(), .gap = 8.0f},
                        },
                    .tab_bar_box =
                        {
                            .layout =
                                {
                                    .width = gui::children(),
                                    .height = gui::px(34.0f),
                                    .padding = gui::insets(3.0f),
                                    .gap = 3.0f,
                                },
                        },
                    .tab_box =
                        {
                            .layout = {.padding = gui::insets(0.0f, 13.0f)},
                        },
                    .tab_bar_height = 34.0f,
                    .tab_min_width = 102.0f,
                }
            );

            gui::BoxDesc const controls_bar = {
                .layout =
                    {
                        .width = gui::fill(),
                        .height = gui::children(),
                        .padding = gui::insets(5.0f, 6.0f),
                        .gap = 5.0f,
                        .align_y = gui::Align::CENTER,
                    },
                .style = {
                    .background = spec.controls_background,
                    .border = spec.controls_border,
                    .border_thickness = 1.0f,
                    .radius = 21.0f,
                    .shadow = {
                        .offset = {0.0f, 8.0f}, .blur_radius = 28.0f, .color = spec.controls_shadow
                    },
                },
            };
            gui::BoxDesc const toolbar_group = {
                .layout =
                    {
                        .width = gui::children(),
                        .height = gui::px(34.0f),
                        .padding = gui::insets(3.0f, 4.0f),
                        .gap = 4.0f,
                        .align_y = gui::Align::CENTER,
                    },
                .style = {
                    .background = spec.toolbar_group_background,
                    .border = spec.toolbar_group_border,
                    .border_thickness = 1.0f,
                    .radius = 17.0f,
                },
            };
            gui::StyleDesc const toolbar_label = {
                .foreground = tokens.text_muted,
            };
            gui::BoxDesc const enabled_checkbox = {
                .layout = {.width = gui::px(98.0f), .height = gui::fill()},
            };
            gui::BoxDesc const read_only_checkbox = {
                .layout = {.width = gui::px(110.0f), .height = gui::fill()},
                .flags = gui::BOX_FLAG_READ_ONLY,
            };
            gui::BoxDesc const size_radio = {
                .layout = {.width = gui::px(74.0f), .height = gui::fill()},
            };

            if (tab_view.selected_index() == 1u) {
                state.sample_loading_phase += delta_time * 0.42f;
                while (state.sample_loading_phase >= 1.0f) {
                    state.sample_loading_phase -= 1.0f;
                }

                if (auto samples = ui.column(
                        gui::id("sample_tab_body"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(14.0f),
                                    .gap = 12.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .style = {.role = gui::StyleRole::PANEL},
                            .debug_name = "sample_tab_body",
                        }
                    )) {
                    ui.label(
                        "Samples",
                        {
                            .layout = {.width = gui::fill(), .height = gui::px(24.0f)},
                            .style = {.font_size = 14.0f},
                        }
                    );
                    if (auto loading_row = ui.row(
                            gui::id("sample_loading_row"),
                            {
                                .layout = {
                                    .width = gui::fill(),
                                    .height = gui::children(),
                                    .gap = 10.0f,
                                    .align_y = gui::Align::CENTER,
                                },
                            }
                        )) {
                        BASE_UNUSED(loading_row);
                        draw_sample_loading_animation(ui, state, spec);
                        draw_sample_circular_loading_animation(ui, state, spec);
                    }
                    if (auto row = ui.row(gui::id("sample_tab_controls"), controls_bar)) {
                        if (auto switches =
                                ui.row(gui::id("sample_control_switches"), toolbar_group)) {
                            ui.checkbox(
                                gui::id("sample_enabled_checkbox"),
                                "Enabled",
                                &state.sample_enabled,
                                enabled_checkbox
                            );
                            ui.toggle(
                                gui::id("sample_preview_toggle"),
                                "Preview",
                                &state.sample_preview,
                                {
                                    .layout = {.width = gui::px(112.0f), .height = gui::fill()},
                                }
                            );
                        }
                        if (auto edit = ui.row(gui::id("sample_control_edit"), toolbar_group)) {
                            ui.input_text(
                                gui::id("sample_name_input"),
                                "Name",
                                state.sample_name,
                                sizeof(state.sample_name),
                                {
                                    .layout = {
                                        .width = gui::px(184.0f),
                                        .height = gui::fill(),
                                        .padding = gui::insets(4.0f, 8.0f),
                                    },
                                }
                            );
                            ui.label(
                                "Value",
                                {
                                    .layout = {.width = gui::px(38.0f), .height = gui::fill()},
                                    .style = toolbar_label,
                                }
                            );
                            ui.slider_float(
                                gui::id("sample_value_slider"),
                                " ",
                                &state.sample_value,
                                {
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::px(150.0f),
                                                    .height = gui::fill(),
                                                },
                                        },
                                    .min = 0.0f,
                                    .max = 1.0f,
                                    .step = 0.05f,
                                }
                            );
                        }
                        ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                        if (auto actions =
                                ui.row(gui::id("sample_control_actions"), toolbar_group)) {
                            gui::Signal const above_signal = ui.button(
                                gui::id("sample_hover_above_button"),
                                "Above",
                                {
                                    .layout =
                                        {
                                            .width = gui::px(72.0f),
                                            .height = gui::fill(),
                                            .padding = gui::insets(3.0f, 6.0f),
                                        },
                                    .debug_name = "sample_hover_above_button",
                                }
                            );
                            if (auto above = ui.hover_popup(
                                    gui::id("sample_hover_above_popup"),
                                    above_signal,
                                    {
                                        .layout =
                                            {
                                                .width = gui::px(212.0f),
                                                .height = gui::children(),
                                                .margin = gui::insets(-104.0f, 0.0f, 0.0f, 0.0f),
                                                .padding = gui::insets(10.0f, 12.0f),
                                                .gap = 4.0f,
                                                .align_x = gui::Align::STRETCH,
                                            },
                                        .debug_name = "sample_hover_above_popup",
                                    }
                                )) {
                                ui.label(
                                    "Samples popup",
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::px(22.0f)},
                                        .style = {.foreground = tokens.text, .font_size = 14.0f},
                                    }
                                );
                                ui.label(
                                    "Positioned above the hovered button.",
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::text()},
                                        .style = {.foreground = tokens.text_muted},
                                    }
                                );
                            }
                            if (ui.button(
                                      gui::id("sample_reset_button"),
                                      "Reset",
                                      {
                                          .layout =
                                              {
                                                  .width = gui::px(72.0f),
                                                  .height = gui::fill(),
                                                  .padding = gui::insets(3.0f, 6.0f),
                                              },
                                          .style = {.role = gui::StyleRole::ACCENT},
                                      }
                                )
                                    .activated) {
                                state.sample_value = 0.5f;
                            }
                        }
                    }
                    if (auto texture_row = ui.row(
                            gui::id("sample_texture_row"),
                            {
                                .layout =
                                    {
                                        .width = gui::children(),
                                        .height = gui::children(),
                                        .gap = 10.0f,
                                    },
                                .debug_name = "sample_texture_row",
                            }
                        )) {
                        draw_texture_sample(
                            ui,
                            gui::id("sample_disk_texture"),
                            "Loaded from disk",
                            state,
                            textures.disk,
                            1u,
                            spec
                        );
                        draw_texture_sample(
                            ui,
                            gui::id("sample_embedded_texture"),
                            "Embedded in exe",
                            state,
                            textures.embedded,
                            2u,
                            spec
                        );
                    }
                    gui::TableSortDesc const sample_table_sort_desc = {
                        .columns = slice(state.sample_table_sort_columns),
                        .column_count = &state.sample_table_sort_count,
                        .selected_columns = slice(state.sample_table_selected_columns),
                    };
                    state.sample_table_filter_columns[0u] = {
                        .column = 0u,
                        .search_text = state.sample_table_item_filter,
                        .search_text_buffer_size = sizeof(state.sample_table_item_filter),
                        .values = slice(state.sample_table_item_filter_values),
                        .popup_open = state.sample_table_filter_open + 0u,
                    };
                    state.sample_table_filter_columns[1u] = {
                        .column = 1u,
                        .search_text = state.sample_table_layer_filter,
                        .search_text_buffer_size = sizeof(state.sample_table_layer_filter),
                        .values = slice(state.sample_table_layer_filter_values),
                        .popup_open = state.sample_table_filter_open + 1u,
                    };
                    state.sample_table_filter_columns[2u] = {
                        .column = 2u,
                        .search_text = state.sample_table_state_filter,
                        .search_text_buffer_size = sizeof(state.sample_table_state_filter),
                        .values = slice(state.sample_table_state_filter_values),
                        .popup_open = state.sample_table_filter_open + 2u,
                    };
                    gui::TableFilterDesc const sample_table_filter_desc = {
                        .columns = slice(state.sample_table_filter_columns),
                    };
                    if (auto table = ui.table(
                            gui::id("sample_defaults_table"),
                            {
                                .box =
                                    {
                                        .layout =
                                            {
                                                .width = gui::children(),
                                                .height = gui::children(),
                                                .padding = gui::insets(4.0f),
                                                .gap = 3.0f,
                                            },
                                        .debug_name = "sample_defaults_table",
                                    },
                                .sort = sample_table_sort_desc,
                                .filter = sample_table_filter_desc,
                            }
                        )) {
                        if (auto header = table.header_row()) {
                            BASE_UNUSED(header);
                            BASE_UNUSED(table.sortable_header_cell(
                                gui::id("sample_table_item_header"),
                                0u,
                                "Item",
                                {.box = {
                                     .layout = {
                                         .width = gui::px(132.0f),
                                         .height = gui::px(28.0f),
                                     }
                                 }}
                            ));
                            BASE_UNUSED(table.sortable_header_cell(
                                gui::id("sample_table_layer_header"),
                                1u,
                                "Layer",
                                {.box = {
                                     .layout = {
                                         .width = gui::px(116.0f),
                                         .height = gui::px(28.0f),
                                     }
                                 }}
                            ));
                            BASE_UNUSED(table.sortable_header_cell(
                                gui::id("sample_table_state_header"),
                                2u,
                                "State",
                                {.box = {
                                     .layout = {
                                         .width = gui::px(116.0f),
                                         .height = gui::px(28.0f),
                                     }
                                 }}
                            ));
                        }
                        for (size_t index = 0u; index < SAMPLE_TABLE_ROW_COUNT; ++index) {
                            SampleTableRow const& item = SAMPLE_TABLE_ROWS[index];
                            auto row_scope = ui.id_scope(
                                indexed_id("sample_table_row", static_cast<uint64_t>(index))
                            );
                            BASE_UNUSED(row_scope);
                            if (auto row = table.row(gui::id("row"))) {
                                draw_sample_table_cell(ui, row, gui::id("item"), item.item, 132.0f);
                                draw_sample_table_cell(
                                    ui, row, gui::id("layer"), item.layer, 116.0f
                                );
                                draw_sample_table_cell(
                                    ui, row, gui::id("state"), item.state, 116.0f
                                );
                            }
                        }
                    }
                }
            } else if (auto body = ui.row(
                           gui::id("body"),
                           {
                               .layout =
                                   {
                                       .width = gui::fill(),
                                       .height = gui::fill(),
                                       .gap = 10.0f,
                                       .align_y = gui::Align::STRETCH,
                                   },
                               .debug_name = "body_row",
                           }
                       )) {

                if (auto sidebar = ui.column(
                        gui::id("sidebar"),
                        {
                            .layout =
                                {
                                    .width = gui::px(216.0f),
                                    .height = gui::fill(),
                                    .padding = gui::insets(10.0f),
                                    .gap = 7.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .style =
                                {
                                    .background = spec.sidebar_background,
                                    .border = spec.sidebar_border,
                                    .border_thickness = 1.0f,
                                    .radius = 20.0f,
                                    .shadow =
                                        {.offset = {0.0f, 10.0f},
                                         .blur_radius = 28.0f,
                                         .color = spec.sidebar_shadow},
                                },
                            .debug_name = "sidebar",
                        }
                    )) {
                    ui.selectable_label(
                        "Virtualized Assets",
                        &state.title_selection,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(22.0f),
                                    .padding = gui::insets(0.0f, 6.0f),
                                },
                            .style = {
                                .foreground = tokens.text_muted,
                                .font_size = 12.0f,
                            },
                        }
                    );

                    gui::TreeNodeDesc tree_desc = {
                        .box =
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(24.0f),
                                        .padding = gui::insets(0.0f, 6.0f),
                                    },
                                .style = {.role = gui::StyleRole::CONTROL, .radius = 9.0f},
                            },
                        .default_open = true,
                    };
                    gui::BoxDesc const tree_leaf = {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::text(),
                                .min_height = gui::px(20.0f),
                                .padding = gui::insets(0.0f, 6.0f),
                                .word_wrap = true,
                            },
                        .style = {.foreground = tokens.text_muted},
                    };
                    if (auto root_tree =
                            ui.tree_node(gui::id("asset_tree_root"), "Asset Browser", tree_desc)) {
                        if (auto textures_tree = ui.tree_node(
                                gui::id("asset_tree_textures"), "Textures", tree_desc
                            )) {
                            ui.label(
                                gui::id("asset_tree_disk"), "ui_api_testbed_texture.png", tree_leaf
                            );
                            ui.label(gui::id("asset_tree_embedded"), "embedded_texture", tree_leaf);
                        }
                        tree_desc.default_open = false;
                        if (auto runtime_tree =
                                ui.tree_node(gui::id("asset_tree_runtime"), "Runtime", tree_desc)) {
                            ui.label(gui::id("asset_tree_draw"), "draw_context", tree_leaf);
                            ui.label(gui::id("asset_tree_font"), "font_cache", tree_leaf);
                        }
                    }

                    {
                        auto rows = ui.list_fixed(
                            list_id,
                            {
                                .item_count = 48u,
                                .item_height = 28.0f,
                                .box = {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(342.0f),
                                            .clip = true,
                                        },
                                    .style =
                                        {
                                            .background = spec.list_background,
                                            .border = spec.list_border,
                                            .border_thickness = 1.0f,
                                            .radius = 14.0f,
                                        },
                                    .debug_name = "asset_list",
                                },
                            }
                        );
                        for (size_t index = rows.first; index < rows.end; ++index) {
                            bool const selected = index == state.selected_index;
                            auto row_scope =
                                ui.id_scope(indexed_id("asset_row", static_cast<uint64_t>(index)));
                            BASE_UNUSED(row_scope);
                            auto row = rows.row(
                                gui::id("row"),
                                {
                                    .layout =
                                        {
                                            .padding = gui::insets(0.0f, 18.0f, 0.0f, 8.0f),
                                            .gap = 6.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .style =
                                        {
                                            .role = selected ? gui::StyleRole::ACCENT
                                                             : gui::StyleRole::CONTROL,
                                            .border = selected ? spec.selected_row_border
                                                               : gui::rgba(255, 255, 255, 0),
                                            .radius = 9.0f,
                                        },
                                    .debug_name = "asset_row",
                                }
                            );
                            gui::Signal const signal = row.signal();
                            if (signal.activated) {
                                state.selected_index = index;
                            }
                            if (index == state.selected_index) {
                                state.selected_row_signal = signal;
                            }
                            ui.label(
                                fmt::tprintf("Asset %02zu", index),
                                {.layout = {.width = gui::fill(), .height = gui::fill()}}
                            );
                        }
                    }

                    if (auto notes = ui.scroll_panel(
                            notes_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(116.0f),
                                        .padding = gui::insets(8.0f),
                                        .gap = 4.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .background = spec.notes_background,
                                        .foreground = tokens.text_muted,
                                        .border = spec.notes_border,
                                        .border_thickness = 1.0f,
                                        .radius = 14.0f,
                                    },
                                .debug_name = "notes_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Note line", 8u);
                    }
                }

                if (auto main_panel = ui.column(
                        gui::id("main_panel"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .gap = 8.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .debug_name = "main_panel",
                        }
                    )) {

                    if (auto controls = ui.row(gui::id("controls"), controls_bar)) {
                        if (auto switches = ui.row(gui::id("control_switches"), toolbar_group)) {
                            ui.checkbox(
                                gui::id("enabled_checkbox"),
                                "Enabled",
                                &state.enabled,
                                enabled_checkbox
                            );
                            ui.toggle(
                                gui::id("preview_toggle"),
                                "Preview",
                                &state.preview,
                                {
                                    .layout = {.width = gui::px(112.0f), .height = gui::fill()},
                                }
                            );
                        }
                        if (auto scale = ui.row(gui::id("scale_control"), toolbar_group)) {
                            ui.label(
                                "Scale",
                                {
                                    .layout = {.width = gui::px(38.0f), .height = gui::fill()},
                                    .style = toolbar_label,
                                }
                            );
                            ui.slider_float(
                                gui::id("scale_slider"),
                                " ",
                                &state.scale,
                                {
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::px(126.0f),
                                                    .height = gui::fill(),
                                                },
                                        },
                                    .min = 0.5f,
                                    .max = 2.0f,
                                    .step = 0.05f,
                                }
                            );
                        }
                        if (auto size = ui.row(gui::id("size_radio_group"), toolbar_group)) {
                            ui.label(
                                "Size",
                                {
                                    .layout = {.width = gui::px(32.0f), .height = gui::fill()},
                                    .style = toolbar_label,
                                }
                            );
                            ui.radio_button(
                                gui::id("size_small_radio"),
                                "Small",
                                &state.size_mode,
                                0u,
                                size_radio
                            );
                            ui.radio_button(
                                gui::id("size_medium_radio"),
                                "Med",
                                &state.size_mode,
                                1u,
                                size_radio
                            );
                            ui.radio_button(
                                gui::id("size_large_radio"),
                                "Large",
                                &state.size_mode,
                                2u,
                                size_radio
                            );
                        }
                        if (auto edit = ui.row(gui::id("control_edit"), toolbar_group)) {
                            ui.checkbox(
                                gui::id("read_only_checkbox"),
                                "Read-only",
                                &state.read_only_value,
                                read_only_checkbox
                            );
                            ui.input_text(
                                gui::id("name_input"),
                                "Name",
                                state.name,
                                sizeof(state.name),
                                {
                                    .layout =
                                        {
                                            .width = gui::px(138.0f),
                                            .height = gui::fill(),
                                            .padding = gui::insets(4.0f, 8.0f),
                                        },
                                    .debug_name = "name_input",
                                }
                            );
                            ui.button(
                                gui::id("disabled_button"),
                                "Disabled",
                                {
                                    .layout =
                                        {
                                            .width = gui::px(82.0f),
                                            .height = gui::fill(),
                                            .padding = gui::insets(3.0f, 6.0f),
                                        },
                                    .flags = gui::BOX_FLAG_DISABLED,
                                }
                            );
                        }
                        ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                        if (auto actions = ui.row(gui::id("control_actions"), toolbar_group)) {
                            if (ui.button(
                                      gui::id("popup_button"),
                                      "Popup",
                                      {
                                          .layout =
                                              {
                                                  .width = gui::px(84.0f),
                                                  .height = gui::fill(),
                                                  .padding = gui::insets(3.0f, 6.0f),
                                              },
                                          .icon =
                                              {.texture = textures.embedded.texture, .size = 14.0f},
                                          .debug_name = "popup_button",
                                      }
                                )
                                    .activated) {
                                state.popup_open = !state.popup_open;
                            }
                            gui::Signal const info_signal = ui.button(
                                gui::id("hover_info_button"),
                                "Info",
                                {
                                    .layout =
                                        {
                                            .width = gui::px(58.0f),
                                            .height = gui::fill(),
                                            .padding = gui::insets(3.0f, 6.0f),
                                        },
                                    .debug_name = "hover_info_button",
                                }
                            );
                            if (auto info = ui.hover_popup(
                                    gui::id("hover_info_popup"),
                                    info_signal,
                                    {
                                        .layout =
                                            {
                                                .width = gui::px(270.0f),
                                                .height = gui::children(),
                                                .margin = gui::insets(28.0f, 0.0f, 0.0f, 0.0f),
                                                .padding = gui::insets(10.0f, 12.0f),
                                                .gap = 6.0f,
                                                .align_x = gui::Align::STRETCH,
                                            },
                                        .debug_name = "hover_info_popup",
                                    }
                                )) {
                                ui.label(
                                    "Hover popup",
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::px(22.0f)},
                                        .style = {.foreground = tokens.text, .font_size = 14.0f},
                                    }
                                );
                                ui.label(
                                    "Stays open while the Info button or this popup is hovered.",
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::text()},
                                        .style = {.foreground = tokens.text_muted},
                                    }
                                );
                            }
                            if (ui.button(
                                      gui::id("modal_button"),
                                      "Modal",
                                      {
                                          .layout =
                                              {
                                                  .width = gui::px(68.0f),
                                                  .height = gui::fill(),
                                                  .padding = gui::insets(3.0f, 6.0f),
                                              },
                                          .style = {.role = gui::StyleRole::ACCENT},
                                          .debug_name = "modal_button",
                                      }
                                )
                                    .activated) {
                                state.modal_open = true;
                            }
                        }
                    }

                    if (auto body_text = ui.scroll_panel(
                            body_text_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(148.0f),
                                        .padding = gui::insets(12.0f, 18.0f, 12.0f, 12.0f),
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .background = spec.body_text_background,
                                        .foreground = tokens.text,
                                        .border = spec.body_text_border,
                                        .border_thickness = 1.0f,
                                        .radius = 14.0f,
                                    },
                                .debug_name = "body_text_scroll",
                            }
                        )) {
                        ui.selectable_label(
                            gui::id("body_text"),
                            BODY_TEXT,
                            &state.body_selection,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::text(),
                                        // .word_wrap = true,
                                    },
                                .style = {.foreground = tokens.text},
                                .debug_name = "body_text",
                            }
                        );
                    }

                    if (auto preview = ui.overlay(
                            gui::id("preview_overlay"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .padding = gui::insets(10.0f),
                                        .align_x = gui::Align::START,
                                        .align_y = gui::Align::START,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .shadow = {.color = gui::rgba(0, 0, 0, 0)},
                                    },
                                .debug_name = "preview_overlay",
                            }
                        )) {
                        if (auto top = ui.row(
                                gui::id("preview_top"),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(28.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .debug_name = "preview_top",
                                }
                            )) {
                            ui.icon(
                                gui::id("sample_icon"),
                                textures.embedded.texture,
                                {
                                    .style = {.foreground = tokens.text_muted},
                                    .icon = {.size = 18.0f},
                                }
                            );
                            ui.label(
                                "Preview fills the overlay",
                                {
                                    .layout = {.width = gui::text(), .height = gui::fill()},
                                    .style = {.foreground = tokens.text_muted},
                                }
                            );
                            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                            ui.image(
                                gui::id("sample_image"),
                                textures.disk.texture,
                                {
                                    .box = {
                                        .layout = {
                                            .width = gui::px(48.0f),
                                            .height = gui::px(24.0f),
                                        },
                                    },
                                }
                            );
                            if (auto badge = ui.row(
                                    gui::id("overlay_badge"),
                                    {
                                        .layout =
                                            {
                                                .width = gui::children(),
                                                .height = gui::px(28.0f),
                                                .padding = gui::insets(3.0f, 8.0f),
                                                .align_y = gui::Align::CENTER,
                                            },
                                        .style = {.role = gui::StyleRole::CONTROL},
                                        .debug_name = "overlay_badge",
                                    }
                                )) {
                                ui.label(
                                    "Overlay",
                                    {.layout = {.width = gui::text(), .height = gui::fill()}}
                                );
                            }
                        }
                        ui.input_text_multiline(
                            gui::id("preview_multiline_input"),
                            "Preview Notes",
                            &state.multiline_text_buffer,
                            {
                                .box = {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(108.0f),
                                            .margin = gui::insets(36.0f, 0.0f, 0.0f, 0.0f),
                                            .padding = gui::insets(9.0f, 12.0f),
                                            // .word_wrap = true,
                                        },
                                    .style =
                                        {
                                            .background = spec.preview_input_background,
                                            .foreground = tokens.text,
                                            .border = spec.preview_input_border,
                                            .border_thickness = 1.0f,
                                            .radius = 12.0f,
                                        },
                                    .debug_name = "preview_multiline_input",
                                },
                            }
                        );
                        gui::Color status_background = tokens.accent;
                        status_background.a = 0.20f;
                        gui::Color status_border = tokens.accent;
                        status_border.a = 0.36f;
                        gui::StyleDesc const table_key_cell_style = {
                            .background = spec.table_header_background,
                            .foreground = tokens.text_muted,
                        };
                        gui::StyleDesc const table_status_cell_style = {
                            .background = status_background,
                            .foreground = tokens.text,
                            .border = status_border,
                        };
                        gui::TableSortDesc const table_sort_desc = {
                            .columns = slice(state.preview_table_sort_columns),
                            .column_count = &state.preview_table_sort_count,
                            .selected_columns = slice(state.preview_table_selected_columns),
                            .box = {
                                .layout = {
                                    .width = gui::px(22.0f),
                                    .height = gui::fill(),
                                    .padding = gui::insets(0.0f),
                                },
                            },
                        };
                        if (auto table = ui.table(
                                gui::id("preview_table"),
                                {
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::children(),
                                                    .height = gui::children(),
                                                    .margin = gui::insets(154.0f, 0.0f, 0.0f, 0.0f),
                                                    .padding = gui::insets(4.0f),
                                                    .gap = 3.0f,
                                                },
                                            .debug_name = "preview_table",
                                        },
                                    .sort = table_sort_desc,
                                }
                            )) {
                            if (auto header = table.header_row()) {
                                if (auto cell = header.cell(
                                        gui::id("preview_table_header_plan"),
                                        {
                                            .column_span = 2u,
                                        }
                                    )) {
                                    if (auto sort_row = ui.row(
                                            gui::id("preview_table_plan_sort"),
                                            {
                                                .layout = {
                                                    .width = gui::children(),
                                                    .height = gui::fill(),
                                                    .gap = 4.0f,
                                                    .align_y = gui::Align::CENTER,
                                                },
                                            }
                                        )) {
                                        draw_preview_table_sort_control(
                                            ui,
                                            table,
                                            state,
                                            table_sort_desc,
                                            gui::id("preview_table_select_group"),
                                            "Group",
                                            0u,
                                            78.0f
                                        );
                                        draw_preview_table_sort_control(
                                            ui,
                                            table,
                                            state,
                                            table_sort_desc,
                                            gui::id("preview_table_select_task"),
                                            "Task",
                                            1u,
                                            68.0f
                                        );
                                    }
                                }
                                if (auto cell = header.cell(
                                        gui::id("preview_table_header_status"),
                                        {
                                            .box = {
                                                .layout = {
                                                    .width = gui::px(128.0f),
                                                },
                                            },
                                        }
                                    )) {
                                    if (auto sort_row = ui.row(
                                            gui::id("preview_table_status_sort"),
                                            {
                                                .layout = {
                                                    .width = gui::children(),
                                                    .height = gui::fill(),
                                                    .gap = 4.0f,
                                                    .align_y = gui::Align::CENTER,
                                                },
                                            }
                                        )) {
                                        draw_preview_table_sort_control(
                                            ui,
                                            table,
                                            state,
                                            table_sort_desc,
                                            gui::id("preview_table_select_status"),
                                            "Status",
                                            2u,
                                            82.0f
                                        );
                                    }
                                }
                            }
                            for (size_t source = 0u; source < PREVIEW_TABLE_ROW_COUNT; ++source) {
                                PreviewTableRow const& item = PREVIEW_TABLE_ROWS[source];
                                auto row_scope = ui.id_scope(
                                    indexed_id("preview_table_row", static_cast<uint64_t>(source))
                                );
                                BASE_UNUSED(row_scope);
                                if (auto row = table.row(gui::id("row"))) {
                                    if (auto cell = row.cell(
                                            gui::id("group"),
                                            {
                                                .box = {
                                                    .layout =
                                                        {
                                                            .width = gui::px(112.0f),
                                                        },
                                                    .style = table_key_cell_style,
                                                },
                                            }
                                        )) {
                                        ui.label(
                                            item.group,
                                            {.layout = {
                                                 .width = gui::fill(), .height = gui::fill()
                                             }}
                                        );
                                    }
                                    if (auto cell = row.cell(
                                            gui::id("task"),
                                            {
                                                .box = {
                                                    .layout = {
                                                        .width = gui::px(204.0f),
                                                    },
                                                },
                                            }
                                        )) {
                                        ui.label(
                                            item.task,
                                            {.layout = {
                                                 .width = gui::fill(), .height = gui::fill()
                                             }}
                                        );
                                    }
                                    if (auto cell = row.cell(
                                            gui::id("status"),
                                            {
                                                .box = {
                                                    .layout =
                                                        {
                                                            .width = gui::px(112.0f),
                                                        },
                                                    .style = table_status_cell_style,
                                                },
                                            }
                                        )) {
                                        ui.label(
                                            item.status,
                                            {.layout = {
                                                 .width = gui::fill(), .height = gui::fill()
                                             }}
                                        );
                                    }
                                }
                            }
                        }
                        if (state.popup_open) {
                            if (auto popup = ui.popup(
                                    gui::id("sample_popup"),
                                    {
                                        .layout =
                                            {
                                                .width = gui::px(248.0f),
                                                .height = gui::children(),
                                                .margin = gui::insets(58.0f, 0.0f, 0.0f, 690.0f),
                                                .padding = gui::insets(10.0f, 12.0f),
                                                .gap = 8.0f,
                                                .align_x = gui::Align::STRETCH,
                                            },
                                        .debug_name = "sample_popup",
                                    }
                                )) {
                                if (auto header = ui.row(
                                        gui::id("popup_header"),
                                        {
                                            .layout = {
                                                .width = gui::fill(),
                                                .height = gui::px(30.0f),
                                                .gap = 8.0f,
                                                .align_y = gui::Align::CENTER,
                                            },
                                        }
                                    )) {
                                    ui.label(
                                        "Floating popup",
                                        {
                                            .layout = {.width = gui::fill(), .height = gui::fill()},
                                            .style = {
                                                .foreground = tokens.text,
                                                .font_size = 14.0f,
                                            },
                                        }
                                    );
                                    if (ui.button(
                                              gui::id("popup_close"),
                                              CLOSE_GLYPH,
                                              {
                                                  .layout =
                                                      {
                                                          .width = gui::px(28.0f),
                                                          .height = gui::px(28.0f),
                                                          .padding = gui::insets(0.0f),
                                                      },
                                                  .style =
                                                      {
                                                          .role = gui::StyleRole::DANGER,
                                                          .font_size = 15.0f,
                                                      },
                                              }
                                        )
                                            .activated) {
                                        state.popup_open = false;
                                    }
                                }
                            }
                        }
                    }

                    if (auto log = ui.scroll_panel(
                            log_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(104.0f),
                                        .padding = gui::insets(10.0f, 18.0f, 10.0f, 10.0f),
                                        .gap = 4.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .background = spec.log_background,
                                        .foreground = spec.log_foreground,
                                        .border = spec.log_border,
                                        .border_thickness = 1.0f,
                                        .radius = 14.0f,
                                    },
                                .debug_name = "log_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Log entry", 8u);
                    }
                }
            }

            if (tab_view.selected_index() == 0u && state.modal_open) {
                if (auto modal = ui.modal(
                        gui::id("sample_modal"),
                        {
                            .layout =
                                {
                                    .padding = gui::insets(12.0f),
                                    .align_x = gui::Align::CENTER,
                                    .align_y = gui::Align::CENTER,
                                },
                            .debug_name = "sample_modal",
                        }
                    )) {
                    if (auto dialog = ui.column(
                            gui::id("sample_modal_dialog"),
                            {
                                .layout =
                                    {
                                        .width = gui::px(380.0f),
                                        .height = gui::children(),
                                        .padding = gui::insets(16.0f),
                                        .gap = 10.0f,
                                        .align_x = gui::Align::STRETCH,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = spec.modal_dialog_background,
                                        .border = spec.modal_dialog_border,
                                        .border_thickness = 1.0f,
                                        .radius = 24.0f,
                                        .shadow =
                                            {
                                                .offset = {0.0f, 26.0f},
                                                .blur_radius = 64.0f,
                                                .spread = 4.0f,
                                                .color = spec.modal_dialog_shadow,
                                            },
                                    },
                                .debug_name = "sample_modal_dialog",
                            }
                        )) {
                        if (auto header = ui.row(
                                gui::id("sample_modal_header"),
                                {
                                    .layout = {
                                        .width = gui::fill(),
                                        .height = gui::px(32.0f),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                }
                            )) {
                            ui.label(
                                "Modal dialog",
                                {
                                    .layout = {.width = gui::fill(), .height = gui::fill()},
                                    .style = {
                                        .foreground = tokens.text,
                                        .font_size = 15.0f,
                                    },
                                }
                            );
                            if (ui.button(
                                      gui::id("sample_modal_close"),
                                      CLOSE_GLYPH,
                                      {
                                          .layout =
                                              {
                                                  .width = gui::px(30.0f),
                                                  .height = gui::px(30.0f),
                                                  .padding = gui::insets(0.0f),
                                              },
                                          .style =
                                              {
                                                  .role = gui::StyleRole::DANGER,
                                                  .font_size = 15.0f,
                                              },
                                      }
                                )
                                    .activated) {
                                state.modal_open = false;
                            }
                        }
                        ui.label(
                            "Blocks the canvas behind it.",
                            {
                                .layout = {.width = gui::fill(), .height = gui::px(22.0f)},
                                .style = {.foreground = spec.modal_body_foreground},
                            }
                        );
                    }
                }
            }

            if (state.image_preview_sample != 0u) {
                TextureSample const& sample =
                    state.image_preview_sample == 2u ? textures.embedded : textures.disk;
                StrRef const title =
                    state.image_preview_sample == 2u ? "Embedded in exe" : "Loaded from disk";
                float const image_width = std::max(1.0f, sample.size.x * state.image_preview_zoom);
                float const image_height = std::max(1.0f, sample.size.y * state.image_preview_zoom);
                if (auto modal = ui.modal(
                        gui::id("image_preview_modal"),
                        {
                            .layout =
                                {
                                    .padding = gui::insets(12.0f),
                                    .align_x = gui::Align::CENTER,
                                    .align_y = gui::Align::CENTER,
                                },
                            .debug_name = "image_preview_modal",
                        }
                    )) {
                    if (auto dialog = ui.column(
                            gui::id("image_preview_dialog"),
                            {
                                .layout =
                                    {
                                        .width = gui::children(),
                                        .height = gui::children(),
                                        .padding = gui::insets(12.0f),
                                        .gap = 10.0f,
                                        .align_x = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = spec.modal_dialog_background,
                                        .border = spec.modal_dialog_border,
                                        .border_thickness = 1.0f,
                                        .radius = 20.0f,
                                    },
                                .debug_name = "image_preview_dialog",
                            }
                        )) {
                        if (auto header = ui.row(
                                gui::id("image_preview_header"),
                                {
                                    .layout =
                                        {
                                            .width = gui::children(),
                                            .height = gui::px(30.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .debug_name = "image_preview_header",
                                }
                            )) {
                            ui.label(
                                title,
                                {
                                    .layout = {.width = gui::text(), .height = gui::fill()},
                                    .style = {.foreground = tokens.text, .font_size = 14.0f},
                                }
                            );
                            ui.label(
                                fmt::tprintf("%.0f%%", state.image_preview_zoom * 100.0f),
                                {
                                    .layout = {.width = gui::text(), .height = gui::fill()},
                                    .style = {.foreground = tokens.text_muted},
                                }
                            );
                            if (ui.button(
                                      gui::id("image_preview_close"),
                                      CLOSE_GLYPH,
                                      {
                                          .layout =
                                              {
                                                  .width = gui::px(30.0f),
                                                  .height = gui::px(30.0f),
                                                  .padding = gui::insets(0.0f),
                                              },
                                          .style =
                                              {
                                                  .role = gui::StyleRole::DANGER,
                                                  .font_size = 15.0f,
                                              },
                                      }
                                )
                                    .activated) {
                                state.image_preview_sample = 0u;
                            }
                        }
                        if (auto image = ui.overlay(
                                gui::id("image_preview_image_frame"),
                                {
                                    .layout =
                                        {
                                            .width = gui::px(image_width),
                                            .height = gui::px(image_height),
                                            .clip = true,
                                        },
                                    .style =
                                        {
                                            .background = gui::rgba(0, 0, 0, 80),
                                            .radius = 10.0f,
                                        },
                                    .debug_name = "image_preview_image_frame",
                                }
                            )) {
                            gui::Signal const image_signal = image.signal();
                            if (image_signal.hovered && input.scroll_delta_y != 0.0f) {
                                state.image_preview_zoom = std::clamp(
                                    state.image_preview_zoom + input.scroll_delta_y / 288.0f,
                                    0.25f,
                                    8.0f
                                );
                            }
                            if (image_signal.activated) {
                                state.image_preview_zoom = 1.0f;
                            }
                            ui.image(
                                gui::id("image_preview_image"),
                                sample.texture,
                                {
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::fill(),
                                                    .height = gui::fill(),
                                                },
                                        },
                                    .size = sample.size,
                                }
                            );
                        }
                    }
                }
            }
        }
    }

#if defined(_WIN32)
#define TRACE_SCOPE(trace, name) BASE_UNUSED(trace)
#define trace_draw_command_counts(trace, context) BASE_UNUSED(trace)
    struct UiRuntime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        TestbedTextures textures = {};
        gui::Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        TestbedState state = {};
        HWND hwnd = nullptr;
        StringBuffer clipboard_text = {};
        DWORD clipboard_sequence = 0u;
        bool clipboard_valid = false;
    };

    [[nodiscard]] auto hash_bytes(uint64_t hash, void const* data, size_t size) -> uint64_t {
        uint8_t const* bytes = static_cast<uint8_t const*>(data);
        for (size_t index = 0u; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] auto testbed_state_hash(TestbedState const& state) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        hash = hash_bytes(hash, &state, sizeof(state));
        StrRef const multiline_text = state.multiline_text_buffer.str();
        return hash_bytes(hash, multiline_text.data(), multiline_text.size());
    }

    auto
    draw_liquid_glass_backdrop(draw::Context context, render::SizeU32 size, LiquidGlassTheme theme)
        -> void {
        float const width = static_cast<float>(size.width);
        float const height = static_cast<float>(size.height);
        float const long_side = std::max(width, height);
        draw::Rect const canvas = {{0.0f, 0.0f}, {width, height}};
        bool const is_light = theme == LiquidGlassTheme::LIGHT;

        draw::Color const canvas_0 = is_light ? draw::Color{0.94f, 0.975f, 1.0f, 1.0f}
                                              : draw::Color{0.014f, 0.024f, 0.038f, 0.98f};
        draw::Color const canvas_1 = is_light ? draw::Color{0.90f, 0.965f, 0.94f, 1.0f}
                                              : draw::Color{0.020f, 0.052f, 0.058f, 0.98f};
        draw::Color const canvas_2 = is_light ? draw::Color{0.985f, 0.95f, 1.0f, 1.0f}
                                              : draw::Color{0.030f, 0.026f, 0.046f, 0.98f};
        draw::Color const canvas_3 = is_light ? draw::Color{1.0f, 0.945f, 0.91f, 1.0f}
                                              : draw::Color{0.048f, 0.030f, 0.044f, 0.98f};
        draw::Color const accent_a = is_light ? draw::Color{0.10f, 0.72f, 0.86f, 0.120f}
                                              : draw::Color{0.08f, 0.56f, 0.68f, 0.105f};
        draw::Color const accent_b = is_light ? draw::Color{0.24f, 0.44f, 0.95f, 0.095f}
                                              : draw::Color{0.34f, 0.34f, 0.90f, 0.085f};
        draw::Color const accent_c = is_light ? draw::Color{1.0f, 0.42f, 0.25f, 0.085f}
                                              : draw::Color{0.80f, 0.24f, 0.18f, 0.060f};
        draw::Color const sheen_a = is_light ? draw::Color{1.0f, 1.0f, 1.0f, 0.125f}
                                             : draw::Color{0.78f, 0.92f, 1.0f, 0.014f};
        draw::Color const sheen_b = is_light ? draw::Color{0.30f, 0.56f, 1.0f, 0.055f}
                                             : draw::Color{0.20f, 0.30f, 0.70f, 0.016f};
        draw::Color const highlight_a = is_light ? draw::Color{1.0f, 1.0f, 1.0f, 0.190f}
                                                 : draw::Color{1.0f, 1.0f, 1.0f, 0.018f};
        draw::Color const highlight_b = is_light ? draw::Color{0.36f, 0.72f, 0.92f, 0.075f}
                                                 : draw::Color{0.52f, 0.82f, 0.90f, 0.022f};

        draw::draw_rect_filled_multicolor(context, canvas, canvas_0, canvas_1, canvas_2, canvas_3);
        draw::draw_circle_filled(
            context, {width * 0.18f, height * 0.18f}, long_side * 0.30f, accent_a, 72
        );
        draw::draw_circle_filled(
            context, {width * 0.78f, height * 0.28f}, long_side * 0.34f, accent_b, 72
        );
        draw::draw_circle_filled(
            context, {width * 0.55f, height * 0.92f}, long_side * 0.36f, accent_c, 72
        );
        draw::draw_quad_filled(
            context,
            {width * 0.46f, 0.0f},
            {width * 0.66f, 0.0f},
            {width * 0.50f, height},
            {width * 0.31f, height},
            sheen_a
        );
        draw::draw_quad_filled(
            context,
            {width * 0.80f, 0.0f},
            {width, 0.0f},
            {width, height * 0.70f},
            {width * 0.66f, height},
            sheen_b
        );
        draw::draw_rect_filled(
            context,
            {{width * 0.08f, height * 0.08f}, {width * 0.92f, height * 0.15f}},
            highlight_a,
            38.0f
        );
        draw::draw_rect_filled(
            context,
            {{width * 0.14f, height * 0.84f}, {width * 0.72f, height * 0.91f}},
            highlight_b,
            34.0f
        );
    }

    auto liquid_glass_clear_color(LiquidGlassTheme theme) -> render::Color {
        if (theme == LiquidGlassTheme::LIGHT) {
            return {0.94f, 0.965f, 0.985f, 1.0f};
        }
        return {0.010f, 0.014f, 0.020f, 1.0f};
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto texture_sample_size(render::SizeU32 size) -> gui::Vec2 {
        return {static_cast<float>(size.width), static_cast<float>(size.height)};
    }

    [[nodiscard]] auto testbed_asset_path(char* path, size_t capacity, wchar_t const* file_name)
        -> bool {
        wchar_t wide_path[MAX_PATH] = {};
        DWORD const length = GetModuleFileNameW(nullptr, wide_path, MAX_PATH);
        if (length == 0u || length >= MAX_PATH || capacity == 0u ||
            capacity > static_cast<size_t>(0x7fffffffu)) {
            return false;
        }

        size_t dir_length = static_cast<size_t>(length);
        while (dir_length != 0u && wide_path[dir_length - 1u] != L'\\' &&
               wide_path[dir_length - 1u] != L'/') {
            --dir_length;
        }

        size_t const file_length = static_cast<size_t>(lstrlenW(file_name));
        if (dir_length + file_length >= MAX_PATH) {
            return false;
        }

        for (size_t index = 0u; index <= file_length; ++index) {
            wide_path[dir_length + index] = file_name[index];
        }

        int const written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_path,
            -1,
            path,
            static_cast<int>(capacity),
            nullptr,
            nullptr
        );
        return written > 0;
    }

    auto destroy_texture_sample(render::Context context, TextureSample& sample) -> void {
        if (render::texture_valid(sample.texture)) {
            render::destroy_texture(context, sample.texture);
        }
        sample.size = {};
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        UiRuntime* const runtime = static_cast<UiRuntime*>(user_data);
        HWND const hwnd = runtime->hwnd;
        if (!OpenClipboard(hwnd)) {
            fmt::eprintf("OpenClipboard failed: %lu\n", GetLastError());
            return;
        }

        int const wide_count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0
        );
        if (wide_count <= 0) {
            CloseClipboard();
            fmt::eprintf("MultiByteToWideChar failed: %lu\n", GetLastError());
            return;
        }

        HGLOBAL const memory =
            GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * (static_cast<size_t>(wide_count) + 1u));
        if (memory == nullptr) {
            CloseClipboard();
            fmt::eprintf("GlobalAlloc failed: %lu\n", GetLastError());
            return;
        }

        wchar_t* const wide_text = static_cast<wchar_t*>(GlobalLock(memory));
        if (wide_text == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            fmt::eprintf("GlobalLock failed: %lu\n", GetLastError());
            return;
        }
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide_text,
            wide_count
        );
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
            fmt::eprintf("SetClipboardData failed: %lu\n", GetLastError());
        }
#if BASE_DEBUG
        else {
            fmt::printf("copied %zu byte(s) to clipboard\n", text.size());
        }
#endif
        CloseClipboard();
    }

    auto get_windows_clipboard_text(void* user_data, Arena& arena) -> StrRef {
        UiRuntime* const runtime = static_cast<UiRuntime*>(user_data);
        DWORD const sequence = GetClipboardSequenceNumber();
        if (runtime->clipboard_valid && runtime->clipboard_sequence == sequence) {
            StrRef const text = runtime->clipboard_text.str();
            if (text.empty()) {
                return {};
            }
            char* const copy = arena_alloc<char>(arena, text.size());
            return {copy, text.copy_to(copy, text.size())};
        }

        HWND const hwnd = runtime->hwnd;
        if (!OpenClipboard(hwnd)) {
            fmt::eprintf("OpenClipboard failed: %lu\n", GetLastError());
            return {};
        }

        HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) {
            CloseClipboard();
            return {};
        }

        wchar_t const* const wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
        if (wide_text == nullptr) {
            CloseClipboard();
            fmt::eprintf("GlobalLock failed: %lu\n", GetLastError());
            return {};
        }

        int const wide_count = lstrlenW(wide_text);
        if (wide_count == 0) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        int const byte_count = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_text, wide_count, nullptr, 0, nullptr, nullptr
        );
        if (byte_count <= 0) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        char* const text = arena_alloc<char>(arena, static_cast<size_t>(byte_count));
        WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_text, wide_count, text, byte_count, nullptr, nullptr
        );
        GlobalUnlock(handle);
        CloseClipboard();
        runtime->clipboard_text.reset();
        BASE_UNUSED(runtime->clipboard_text.write_bytes(text, static_cast<size_t>(byte_count)));
        runtime->clipboard_sequence = sequence;
        runtime->clipboard_valid = true;
        return {text, static_cast<size_t>(byte_count)};
    }

    auto destroy_ui_runtime(render::Context render_context, UiRuntime* runtime) -> void {
        if (runtime == nullptr) {
            return;
        }

        if (draw::renderer_valid(runtime->draw_renderer)) {
            draw::destroy_renderer(render_context, runtime->draw_renderer);
        }
        if (draw::context_valid(runtime->draw_context)) {
            draw::destroy_context(runtime->draw_context);
        }
        if (gui::context_valid(runtime->ui_context)) {
            gui::destroy_context(runtime->ui_context);
        }
        if (font_cache::cache_valid(runtime->cache)) {
            font_cache::destroy_cache(runtime->cache);
        }
        destroy_texture_sample(render_context, runtime->textures.disk);
        destroy_texture_sample(render_context, runtime->textures.embedded);
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
    }

    [[nodiscard]] auto
    create_ui_runtime(Arena& arena, render::Context render_context, HWND hwnd, UiRuntime* runtime)
        -> bool {
        runtime->hwnd = hwnd;
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, runtime->draw_renderer);
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->font);
        char disk_texture_path[MAX_PATH * 4] = {};
        if (!testbed_asset_path(
                disk_texture_path, sizeof(disk_texture_path), L"ui_api_testbed_texture.png"
            )) {
            fmt::eprintf("ui_api_testbed_texture.png path is too long\n");
            return false;
        }
        render_result = render::load_image_texture_from_file(
            render_context, disk_texture_path, runtime->textures.disk.texture
        );
        if (render::result_failed(render_result)) {
            log_render_result("render::load_image_texture_from_file", render_result);
            return false;
        }
        runtime->textures.disk.size =
            texture_sample_size(render::texture_size(runtime->textures.disk.texture));

        render_result = render::load_image_texture_from_memory(
            render_context,
            ui_api_testbed_assets::texture_png,
            ui_api_testbed_assets::texture_png_size,
            runtime->textures.embedded.texture
        );
        if (render::result_failed(render_result)) {
            log_render_result("render::load_image_texture_from_memory", render_result);
            return false;
        }
        runtime->textures.embedded.size =
            texture_sample_size(render::texture_size(runtime->textures.embedded.texture));

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        LiquidGlassSpec const style = liquid_glass_spec(runtime->state.theme);
        gui::ThemeDesc theme = gui::default_theme();
        configure_liquid_glass_theme(theme, runtime->font, style);
        gui::create_context(
            arena,
            {
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .clipboard_user_data = runtime,
            },
            runtime->ui_context
        );
        BASE_UNUSED(runtime->state.multiline_text_buffer.write_string(
            "Editable multiline textEditable multiline textEditable multiline textEditable "
            "multiline textEditable multiline textEditable multiline textEditable multiline "
            "textEditable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        UiRuntime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        void* trace
    ) -> gui::Frame {
        TRACE_SCOPE(trace, "ui_build");
        LiquidGlassSpec style = {};
        {
            TRACE_SCOPE(trace, "theme_setup");
            style = liquid_glass_spec(runtime->state.theme);
            gui::ThemeDesc theme = gui::default_theme();
            configure_liquid_glass_theme(theme, runtime->font, style);
            gui::set_theme(runtime->ui_context, theme);
        }

        gui::Frame ui = {};
        {
            TRACE_SCOPE(trace, "begin_ui_frame");
            ui = gui::begin_frame(
                runtime->ui_context,
                {
                    .size =
                        {
                            static_cast<float>(window_size.width),
                            static_cast<float>(window_size.height),
                        },
                    .delta_time = delta_time,
                    .input = input,
                }
            );
        }
        {
            TRACE_SCOPE(trace, "draw_ui");
            draw_ui(ui, runtime->state, style, runtime->textures, input, delta_time);
        }
        {
            TRACE_SCOPE(trace, "end_ui_frame");
            gui::end_frame(ui);
        }

        {
            TRACE_SCOPE(trace, "draw_command_recording");
            {
                TRACE_SCOPE(trace, "draw_begin_frame");
                draw::begin_frame(runtime->draw_context);
            }
            {
                TRACE_SCOPE(trace, "draw_backdrop");
                draw_liquid_glass_backdrop(
                    runtime->draw_context, window_size, runtime->state.theme
                );
            }
            {
                TRACE_SCOPE(trace, "gui_render_frame");
                gui::render_frame(ui, runtime->draw_context);
            }
            {
                TRACE_SCOPE(trace, "draw_end_frame");
                draw::end_frame(runtime->draw_context);
            }
        }
#if BASE_DEBUG
        trace_draw_command_counts(trace, runtime->draw_context);
#endif
        return ui;
    }

#undef trace_draw_command_counts
#undef TRACE_SCOPE

    struct ModuleRuntime {
        Arena arena = {};
        UiRuntime runtime = {};
    };

    [[nodiscard]] auto draw_command_counts(draw::Context context) -> DrawCommandCounts {
        return {
            .command_count = draw::command_count(context),
            .primitive_count = draw::primitive_command_count(context),
            .batch_count = draw::primitive_batch_count(context),
            .styled_rect_count = draw::styled_rect_command_count(context),
            .text_count = draw::text_command_count(context),
            .layer_count = draw::layer_command_count(context),
        };
    }

    [[nodiscard]] auto module_create(void* storage, void* user_data) -> bool {
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = new (storage) ModuleRuntime{};
        module->arena.init();
        if (!create_ui_runtime(
                module->arena,
                context->render_context,
                static_cast<HWND>(context->native_window),
                &module->runtime
            )) {
            destroy_ui_runtime(context->render_context, &module->runtime);
            module->~ModuleRuntime();
            return false;
        }
        return true;
    }

    auto module_destroy(void* storage, void* user_data) -> void {
        if (storage == nullptr) {
            return;
        }
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = static_cast<ModuleRuntime*>(storage);
        destroy_ui_runtime(context->render_context, &module->runtime);
        module->~ModuleRuntime();
    }

    [[nodiscard]] auto module_render_frame(
        void* storage,
        render::Context render_context,
        render::Window render_window,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> FrameResult {
        auto* const module = static_cast<ModuleRuntime*>(storage);
        uint64_t const state_hash_before = testbed_state_hash(module->runtime.state);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame =
            build_ui_commands(&module->runtime, window_size, input, delta_time, nullptr);
        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = liquid_glass_clear_color(module->runtime.state.theme);

        frame_result.render_result = draw::render_commands_to_window(
            module->runtime.draw_renderer, render_context, pass_desc, module->runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(module->runtime.draw_context);
        reset_thread_temp_arenas();
        frame_result.redraw_pending =
            frame_result.frame.redraw_requested() || module->runtime.state.selected_tab == 1u ||
            testbed_state_hash(module->runtime.state) != state_hash_before;
        return frame_result;
    }

    [[nodiscard]] auto ui_api_testbed_module_api() -> ModuleApi const* {
        static ModuleApi const api = {
            .hot_reload =
                {
                    .version = gui::HOT_RELOAD_API_VERSION,
                    .runtime_size = sizeof(ModuleRuntime),
                    .runtime_alignment = alignof(ModuleRuntime),
                    .create = module_create,
                    .destroy = module_destroy,
                },
            .render_frame = module_render_frame,
        };
        return &api;
    }
#else
    auto run_console_fallback() -> int {
        Arena arena = {};
        arena.init();

        TestbedState state = {};
        LiquidGlassSpec const style = liquid_glass_spec(state.theme);
        gui::ThemeDesc theme = gui::default_theme();
        configure_liquid_glass_theme(theme, {}, style);

        gui::Context ui_context = {};
        gui::create_context(arena, {.theme = &theme}, ui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        BASE_UNUSED(state.multiline_text_buffer.write_string(
            "Editable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        int texture_storage = 0;
        TestbedTextures textures = {};
        textures.disk = {{&texture_storage}, {2.0f, 2.0f}};
        textures.embedded = textures.disk;
        gui::Frame ui =
            gui::begin_frame(ui_context, {.size = {640.0f, 400.0f}, .delta_time = 1.0f / 60.0f});
        draw_ui(ui, state, style, textures, gui::InputState{}, 1.0f / 60.0f);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        gui::draw::end_frame(draw_context);

        fmt::printf(
            "ui_api_testbed: windowed D3D11 path is Windows-only; commands=%zu styled_rects=%zu "
            "text=%zu\n",
            gui::draw::command_count(draw_context),
            gui::draw::styled_rect_command_count(draw_context),
            gui::draw::text_command_count(draw_context)
        );

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(ui_context);
        return 0;
    }

#endif

} // namespace ui_api_testbed

#if defined(_WIN32) && defined(UI_API_TESTBED_MODULE)
GUI_HOT_RELOAD_EXPORT auto ui_api_testbed_get_module_api() -> ui_api_testbed::ModuleApi const* {
    return ui_api_testbed::ui_api_testbed_module_api();
}
#endif
