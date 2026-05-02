#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <objbase.h>
#include <render/render.h>
#include <ui_api_testbed_embedded_texture.h>
#include <wincodec.h>
#include <windows.h>
#endif
#include <gui/gui.h>

namespace {

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
        float sample_value = 0.5f;
        float sample_loading_phase = 0.0f;
        size_t selected_tab = 0u;
        size_t selected_index = 12u;
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

    auto row_id(size_t index) -> gui::Id {
        return gui::id(0xA1100000ull + static_cast<uint64_t>(index));
    }

    constexpr char BODY_TEXT[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.m dolor sit amet, consectetur "
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
        TextureSample const& sample,
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
            ui.image(
                sample.texture,
                {
                    .box =
                        {
                            .layout = {.width = gui::fill(), .height = gui::px(108.0f)},
                            .style =
                                {
                                    .background = gui::rgba(0, 0, 0, 38),
                                    .radius = 10.0f,
                                },
                        },
                    .size = sample.size,
                    .fit = gui::ImageFit::CONTAIN,
                }
            );
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
                            textures.disk,
                            spec
                        );
                        draw_texture_sample(
                            ui,
                            gui::id("sample_embedded_texture"),
                            "Embedded in exe",
                            textures.embedded,
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
                            if (auto row = table.row(gui::id(0x5A500000ull + index))) {
                                draw_sample_table_cell(
                                    ui, row, gui::id(0x5A510000ull + index), item.item, 132.0f
                                );
                                draw_sample_table_cell(
                                    ui, row, gui::id(0x5A520000ull + index), item.layer, 116.0f
                                );
                                draw_sample_table_cell(
                                    ui, row, gui::id(0x5A530000ull + index), item.state, 116.0f
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
                                            .height = gui::px(504.0f),
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
                            auto row = rows.row(
                                row_id(index),
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
                                        .word_wrap = true,
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
                                            .word_wrap = true,
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
                                if (auto row = table.row(gui::id(0x77000000ull + source))) {
                                    if (auto cell = row.cell(
                                            gui::id(0x77010000ull + source),
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
                                            gui::id(0x77020000ull + source),
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
                                            gui::id(0x77030000ull + source),
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
        }
    }

#if defined(_WIN32)
    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_ui_api_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 800u;
    constexpr size_t MAX_KEY_EVENTS_PER_FRAME = 32u;

    struct TraceOptions {
        char const* path = nullptr;
        uint64_t warmup_ms = 0u;
        uint64_t duration_ms = 0u;
        bool enable_debug_layer = true;
    };

    struct ManualTrace {
#if BASE_DEBUG
        std::FILE* file = nullptr;
        LARGE_INTEGER frequency = {};
        int64_t start_counter = 0;
        uint64_t cpu_start_100ns = 0u;
        uint32_t pid = 0u;
        uint32_t tid = 0u;
        uint32_t logical_processor_count = 1u;
        size_t frame_count = 0u;
        size_t command_sum = 0u;
        size_t primitive_sum = 0u;
        size_t batch_sum = 0u;
        size_t styled_rect_sum = 0u;
        size_t text_sum = 0u;
        size_t layer_sum = 0u;
        size_t command_max = 0u;
        bool first_event = true;
#endif
    };

#if BASE_DEBUG
    [[nodiscard]] auto trace_active(ManualTrace const* trace) -> bool {
        return trace != nullptr && trace->file != nullptr;
    }

    [[nodiscard]] auto trace_counter() -> int64_t {
        LARGE_INTEGER counter = {};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    [[nodiscard]] auto trace_us(ManualTrace const& trace, int64_t counter) -> uint64_t {
        return static_cast<uint64_t>(
            (counter - trace.start_counter) * 1000000ll / trace.frequency.QuadPart
        );
    }

    [[nodiscard]] auto trace_elapsed_ms(ManualTrace const& trace) -> double {
        int64_t const counter = trace_counter();
        return static_cast<double>(counter - trace.start_counter) * 1000.0 /
               static_cast<double>(trace.frequency.QuadPart);
    }

    [[nodiscard]] auto file_time_100ns(FILETIME time) -> uint64_t {
        return (static_cast<uint64_t>(time.dwHighDateTime) << 32u) |
               static_cast<uint64_t>(time.dwLowDateTime);
    }

    [[nodiscard]] auto process_cpu_100ns() -> uint64_t {
        FILETIME creation = {};
        FILETIME exit = {};
        FILETIME kernel = {};
        FILETIME user = {};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
            return 0u;
        }
        return file_time_100ns(kernel) + file_time_100ns(user);
    }

    auto trace_separator(ManualTrace* trace) -> void {
        if (!trace->first_event) {
            fmt::fprintf(trace->file, ",\n");
        }
        trace->first_event = false;
    }

    auto trace_zone_event(ManualTrace* trace, char const* name, char phase) -> void {
        if (!trace_active(trace)) {
            return;
        }
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"%s\",\"cat\":\"zone\",\"ph\":\"%c\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u}",
            name,
            phase,
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid
        );
    }

    class TraceScope final {
      public:
        TraceScope(ManualTrace* trace, char const* name)
            : m_trace(trace_active(trace) ? trace : nullptr), m_name(name) {
            if (m_trace != nullptr) {
                trace_zone_event(m_trace, m_name, 'B');
            }
        }

        ~TraceScope() {
            if (m_trace != nullptr) {
                trace_zone_event(m_trace, m_name, 'E');
            }
        }

      private:
        ManualTrace* m_trace = nullptr;
        char const* m_name = nullptr;
    };

#define UI_TRACE_JOIN_INNER(a, b) a##b
#define UI_TRACE_JOIN(a, b) UI_TRACE_JOIN_INNER(a, b)
#define TRACE_SCOPE(trace, name) TraceScope UI_TRACE_JOIN(trace_scope_, __LINE__)((trace), (name))

    auto trace_instant_u32(
        ManualTrace* trace, char const* name, render::SizeU32 window_size, uint32_t debug_layer
    ) -> void {
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"%s\",\"cat\":\"meta\",\"ph\":\"i\",\"s\":\"p\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u,\"args\":{\"build\":\"windows-msvc-debug\","
            "\"debug_layer\":%u,\"window_width\":%u,\"window_height\":%u,"
            "\"logical_processors\":%u,\"timestamp_unit\":\"microseconds\"}}",
            name,
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid,
            debug_layer,
            window_size.width,
            window_size.height,
            trace->logical_processor_count
        );
    }

    [[nodiscard]] auto
    trace_start(ManualTrace* trace, char const* path, render::SizeU32 size, bool enable_debug_layer)
        -> bool {
        if (trace == nullptr || path == nullptr || path[0] == '\0') {
            return false;
        }

        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
            fmt::eprintf("ui_api_testbed trace: failed to open %s\n", path);
            return false;
        }

        *trace = {};
        trace->file = file;
        QueryPerformanceFrequency(&trace->frequency);
        trace->start_counter = trace_counter();
        trace->cpu_start_100ns = process_cpu_100ns();
        trace->pid = GetCurrentProcessId();
        trace->tid = GetCurrentThreadId();
        SYSTEM_INFO system_info = {};
        GetSystemInfo(&system_info);
        trace->logical_processor_count =
            std::max(static_cast<uint32_t>(system_info.dwNumberOfProcessors), 1u);

        fmt::fprintf(trace->file, "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n");
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":%u,\"tid\":0,"
            "\"args\":{\"name\":\"ui_api_testbed\"}}",
            trace->pid
        );
        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":%u,\"tid\":%u,"
            "\"args\":{\"name\":\"main\"}}",
            trace->pid,
            trace->tid
        );
        trace_instant_u32(trace, "trace_start", size, enable_debug_layer ? 1u : 0u);
        fmt::printf("ui_api_testbed trace: recording %s\n", path);
        return true;
    }

    auto trace_draw_command_counts(ManualTrace* trace, draw::Context context) -> void {
        if (!trace_active(trace)) {
            return;
        }

        size_t const command_count = draw::command_count(context);
        size_t const primitive_count = draw::primitive_command_count(context);
        size_t const batch_count = draw::primitive_batch_count(context);
        size_t const styled_rect_count = draw::styled_rect_command_count(context);
        size_t const text_count = draw::text_command_count(context);
        size_t const layer_count = draw::layer_command_count(context);
        size_t const frame_index = trace->frame_count;
        trace->frame_count += 1u;
        trace->command_sum += command_count;
        trace->primitive_sum += primitive_count;
        trace->batch_sum += batch_count;
        trace->styled_rect_sum += styled_rect_count;
        trace->text_sum += text_count;
        trace->layer_sum += layer_count;
        trace->command_max = std::max(trace->command_max, command_count);

        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"draw_commands\",\"cat\":\"counters\",\"ph\":\"C\",\"ts\":%llu,"
            "\"pid\":%u,\"tid\":%u,\"args\":{\"frame\":%zu,\"total\":%zu,"
            "\"primitive\":%zu,\"primitive_batches\":%zu,\"styled_rects\":%zu,"
            "\"text\":%zu,\"layers\":%zu}}",
            static_cast<unsigned long long>(trace_us(*trace, trace_counter())),
            trace->pid,
            trace->tid,
            frame_index,
            command_count,
            primitive_count,
            batch_count,
            styled_rect_count,
            text_count,
            layer_count
        );
    }

    [[nodiscard]] auto trace_average(size_t sum, size_t count) -> double {
        return count != 0u ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    }

    auto trace_finish(
        ManualTrace* trace, char const* path, render::SizeU32 size, bool enable_debug_layer
    ) -> void {
        if (!trace_active(trace)) {
            return;
        }

        int64_t const end_counter = trace_counter();
        uint64_t const cpu_end_100ns = process_cpu_100ns();
        double const duration_ms = static_cast<double>(end_counter - trace->start_counter) *
                                   1000.0 / static_cast<double>(trace->frequency.QuadPart);
        double const duration_100ns = duration_ms * 10000.0;
        double const cpu_percent =
            duration_100ns > 0.0
                ? static_cast<double>(cpu_end_100ns - trace->cpu_start_100ns) * 100.0 /
                      duration_100ns / static_cast<double>(trace->logical_processor_count)
                : 0.0;
        double const fps = duration_ms > 0.0
                               ? static_cast<double>(trace->frame_count) * 1000.0 / duration_ms
                               : 0.0;
        double const avg_commands = trace_average(trace->command_sum, trace->frame_count);
        double const avg_primitives = trace_average(trace->primitive_sum, trace->frame_count);
        double const avg_batches = trace_average(trace->batch_sum, trace->frame_count);
        double const avg_styled_rects = trace_average(trace->styled_rect_sum, trace->frame_count);
        double const avg_text = trace_average(trace->text_sum, trace->frame_count);
        double const avg_layers = trace_average(trace->layer_sum, trace->frame_count);

        trace_separator(trace);
        fmt::fprintf(
            trace->file,
            "{\"name\":\"trace_summary\",\"cat\":\"summary\",\"ph\":\"i\",\"s\":\"p\","
            "\"ts\":%llu,\"pid\":%u,\"tid\":%u,\"args\":{\"duration_ms\":%.2f,"
            "\"process_cpu_percent\":%.2f,\"frames\":%zu,\"fps\":%.2f,"
            "\"window_width\":%u,\"window_height\":%u,\"debug_layer\":%u,"
            "\"avg_commands\":%.2f,"
            "\"avg_primitives\":%.2f,\"avg_primitive_batches\":%.2f,"
            "\"avg_styled_rects\":%.2f,\"avg_text\":%.2f,\"avg_layers\":%.2f,"
            "\"max_commands\":%zu}}",
            static_cast<unsigned long long>(trace_us(*trace, end_counter)),
            trace->pid,
            trace->tid,
            duration_ms,
            cpu_percent,
            trace->frame_count,
            fps,
            size.width,
            size.height,
            enable_debug_layer ? 1u : 0u,
            avg_commands,
            avg_primitives,
            avg_batches,
            avg_styled_rects,
            avg_text,
            avg_layers,
            trace->command_max
        );
        fmt::fprintf(trace->file, "\n]}\n");
        std::fclose(trace->file);
        trace->file = nullptr;

        fmt::printf(
            "ui_api_testbed trace: wrote %s frames=%zu duration_ms=%.1f fps=%.1f "
            "process_cpu=%.2f%% commands_avg=%.1f max=%zu primitive_avg=%.1f "
            "styled_rect_avg=%.1f text_avg=%.1f layers_avg=%.1f\n",
            path,
            trace->frame_count,
            duration_ms,
            fps,
            cpu_percent,
            avg_commands,
            trace->command_max,
            avg_primitives,
            avg_styled_rects,
            avg_text,
            avg_layers
        );
    }

    [[nodiscard]] auto trace_input_message(UINT message) -> bool {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
            return true;
        default:
            return false;
        }
    }
#else
#define TRACE_SCOPE(trace, name) BASE_UNUSED(trace)
#endif

    [[nodiscard]] auto parse_u64(char const* text, uint64_t* out_value) -> bool {
        if (text == nullptr || text[0] == '\0' || out_value == nullptr) {
            return false;
        }
        uint64_t value = 0u;
        for (char const* at = text; *at != '\0'; ++at) {
            if (*at < '0' || *at > '9') {
                return false;
            }
            value = value * 10u + static_cast<uint64_t>(*at - '0');
        }
        *out_value = value;
        return true;
    }

    [[nodiscard]] auto consume_trace_value(
        int argc, char** argv, int* index, char const* option, char const** out_value
    ) -> bool {
        if (*index + 1 >= argc) {
            fmt::eprintf("%s requires a value\n", option);
            return false;
        }
        *index += 1;
        *out_value = argv[*index];
        return true;
    }

    [[nodiscard]] auto parse_trace_options(int argc, char** argv, TraceOptions* out_options)
        -> bool {
        for (int index = 1; index < argc; ++index) {
            char const* const arg = argv[index];
            if (std::strncmp(arg, "--trace=", 8u) == 0) {
                out_options->path = arg + 8u;
            } else if (std::strcmp(arg, "--trace") == 0) {
                if (!consume_trace_value(argc, argv, &index, "--trace", &out_options->path)) {
                    return false;
                }
            } else if (std::strncmp(arg, "--trace-warmup-ms=", 18u) == 0) {
                if (!parse_u64(arg + 18u, &out_options->warmup_ms)) {
                    fmt::eprintf("invalid --trace-warmup-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--trace-warmup-ms") == 0) {
                char const* value = nullptr;
                if (!consume_trace_value(argc, argv, &index, arg, &value) ||
                    !parse_u64(value, &out_options->warmup_ms)) {
                    fmt::eprintf("invalid --trace-warmup-ms value\n");
                    return false;
                }
            } else if (std::strncmp(arg, "--trace-duration-ms=", 20u) == 0) {
                if (!parse_u64(arg + 20u, &out_options->duration_ms)) {
                    fmt::eprintf("invalid --trace-duration-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--trace-duration-ms") == 0) {
                char const* value = nullptr;
                if (!consume_trace_value(argc, argv, &index, arg, &value) ||
                    !parse_u64(value, &out_options->duration_ms)) {
                    fmt::eprintf("invalid --trace-duration-ms value\n");
                    return false;
                }
            } else if (std::strcmp(arg, "--no-d3d-debug-layer") == 0) {
                out_options->enable_debug_layer = false;
            } else {
                fmt::eprintf(
                    "usage: ui_api_testbed [--trace <path>] [--trace-warmup-ms N] "
                    "[--trace-duration-ms N] [--no-d3d-debug-layer]\n"
                );
                return false;
            }
        }
        if (out_options->path == nullptr &&
            (out_options->warmup_ms != 0u || out_options->duration_ms != 0u)) {
            fmt::eprintf("--trace-warmup-ms and --trace-duration-ms require --trace\n");
            return false;
        }
        return true;
    }

    [[nodiscard]] auto trace_requested(TraceOptions const& options) -> bool {
        return options.path != nullptr || options.warmup_ms != 0u || options.duration_ms != 0u;
    }

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

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool redraw_pending = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::Frame last_frame = {};
        gui::Id mouse_hit_id = {};
        gui::InputState input = {};
        gui::KeyEvent key_events[MAX_KEY_EVENTS_PER_FRAME] = {};
        gui::Vec2 left_double_click_pos = {};
        uint64_t left_double_click_ticks = 0u;
        bool left_double_click_pending = false;
    };

    AppState* global_app_state = nullptr;

    auto request_redraw(AppState* state) -> void {
        if (state != nullptr) {
            state->redraw_pending = true;
        }
    }

    [[nodiscard]] auto frame_ready(gui::Frame const& frame) -> bool {
        return frame.box_info_count() != 0u;
    }

    [[nodiscard]] auto frame_hit_id(gui::Frame const& frame, gui::Vec2 pos) -> gui::Id {
        gui::BoxInfo const* const box = frame.hit_test(pos);
        return box != nullptr ? box->id : gui::Id{};
    }

    [[nodiscard]] auto focused_box_exists(gui::Frame const& frame) -> bool {
        return frame.focused_box() != nullptr;
    }

    [[nodiscard]] auto focused_text_box_exists(gui::Frame const& frame) -> bool {
        gui::BoxInfo const* const box = frame.focused_box();
        return box != nullptr && (box->kind == gui::BoxKind::INPUT_TEXT ||
                                  box->kind == gui::BoxKind::INPUT_TEXT_MULTILINE);
    }

    [[nodiscard]] auto shortcut_key(gui::Key key, gui::KeyMods mods) -> bool {
        if ((mods & gui::KEY_MOD_CTRL) == 0u) {
            return false;
        }
        return key == gui::Key::A || key == gui::Key::C || key == gui::Key::V ||
               key == gui::Key::X || key == gui::Key::Z;
    }

    [[nodiscard]] auto key_event_needs_frame(AppState const& state, gui::Key key, gui::KeyMods mods)
        -> bool {
        return state.redraw_pending || !frame_ready(state.last_frame) || key == gui::Key::TAB ||
               shortcut_key(key, mods) || focused_box_exists(state.last_frame);
    }

    [[nodiscard]] auto text_event_needs_frame(AppState const& state) -> bool {
        return state.redraw_pending || !frame_ready(state.last_frame) ||
               focused_text_box_exists(state.last_frame);
    }

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

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto lparam_x(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>(value & 0xffff));
    }

    [[nodiscard]] auto lparam_y(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto left_triple_click(AppState const& state, gui::Vec2 pos) -> bool {
        float const x_radius = static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)) * 0.5f;
        float const y_radius = static_cast<float>(GetSystemMetrics(SM_CYDOUBLECLK)) * 0.5f;
        return state.left_double_click_pending &&
               GetTickCount64() - state.left_double_click_ticks <=
                   static_cast<uint64_t>(GetDoubleClickTime()) &&
               pos.x >= state.left_double_click_pos.x - x_radius &&
               pos.x <= state.left_double_click_pos.x + x_radius &&
               pos.y >= state.left_double_click_pos.y - y_radius &&
               pos.y <= state.left_double_click_pos.y + y_radius;
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    auto log_hresult(char const* operation, HRESULT result) -> void {
        fmt::eprintf("%s failed: 0x%x\n", operation, static_cast<uint32_t>(result));
    }

    template <typename T> auto release_com(T*& value) -> void {
        if (value != nullptr) {
            value->Release();
            value = nullptr;
        }
    }

    class ComApartment final {
      public:
        auto init() -> bool {
            HRESULT const result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (result == RPC_E_CHANGED_MODE) {
                return true;
            }
            if (FAILED(result)) {
                log_hresult("CoInitializeEx", result);
                return false;
            }
            m_initialized = true;
            return true;
        }

        ~ComApartment() {
            if (m_initialized) {
                CoUninitialize();
            }
        }

        ComApartment() = default;
        ComApartment(ComApartment&&) = delete;
        ComApartment(ComApartment const&) = delete;
        auto operator=(ComApartment&&) -> ComApartment& = delete;
        auto operator=(ComApartment const&) -> ComApartment& = delete;

      private:
        bool m_initialized = false;
    };

    [[nodiscard]] auto texture_sample_size(render::SizeU32 size) -> gui::Vec2 {
        return {static_cast<float>(size.width), static_cast<float>(size.height)};
    }

    [[nodiscard]] auto testbed_asset_path(wchar_t* path, size_t capacity, wchar_t const* file_name)
        -> bool {
        DWORD const length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(capacity));
        if (length == 0u || static_cast<size_t>(length) >= capacity) {
            return false;
        }

        size_t dir_length = static_cast<size_t>(length);
        while (dir_length != 0u && path[dir_length - 1u] != L'\\' &&
               path[dir_length - 1u] != L'/') {
            --dir_length;
        }

        size_t const file_length = static_cast<size_t>(lstrlenW(file_name));
        if (dir_length + file_length >= capacity) {
            return false;
        }

        for (size_t index = 0u; index <= file_length; ++index) {
            path[dir_length + index] = file_name[index];
        }
        return true;
    }

    [[nodiscard]] auto create_texture_from_wic_frame(
        render::Context context,
        IWICImagingFactory* factory,
        IWICBitmapFrameDecode* frame,
        TextureSample& out_sample
    ) -> bool {
        UINT width = 0u;
        UINT height = 0u;
        HRESULT result = frame->GetSize(&width, &height);
        if (FAILED(result)) {
            log_hresult("IWICBitmapFrameDecode::GetSize", result);
            return false;
        }
        if (width == 0u || height == 0u) {
            fmt::eprintf("image decode failed: empty image\n");
            return false;
        }

        uint64_t const bytes_per_row64 = static_cast<uint64_t>(width) * 4u;
        uint64_t const byte_size64 = bytes_per_row64 * static_cast<uint64_t>(height);
        if (bytes_per_row64 > 0xffffffffu || byte_size64 > 0xffffffffu) {
            fmt::eprintf("image decode failed: image is too large\n");
            return false;
        }

        IWICFormatConverter* converter = nullptr;
        result = factory->CreateFormatConverter(&converter);
        if (FAILED(result) || converter == nullptr) {
            log_hresult("IWICImagingFactory::CreateFormatConverter", result);
            return false;
        }

        result = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom
        );
        if (FAILED(result)) {
            log_hresult("IWICFormatConverter::Initialize", result);
            release_com(converter);
            return false;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        uint32_t const bytes_per_row = static_cast<uint32_t>(bytes_per_row64);
        uint32_t const byte_size = static_cast<uint32_t>(byte_size64);
        uint8_t* const pixels = arena_alloc<uint8_t>(*temp.arena(), byte_size);
        result = converter->CopyPixels(nullptr, bytes_per_row, byte_size, pixels);
        release_com(converter);
        if (FAILED(result)) {
            log_hresult("IWICFormatConverter::CopyPixels", result);
            return false;
        }

        render::TextureDesc desc = {};
        desc.size = {width, height};
        desc.bytes_per_row = bytes_per_row;
        desc.rgba_pixels = pixels;
        render::Result const texture_result =
            render::create_texture(context, desc, out_sample.texture);
        if (render::result_failed(texture_result)) {
            log_render_result("render::create_texture", texture_result);
            return false;
        }

        out_sample.size = texture_sample_size(desc.size);
        return true;
    }

    [[nodiscard]] auto
    load_texture_from_file(render::Context context, wchar_t const* path, TextureSample& out_sample)
        -> bool {
        IWICImagingFactory* factory = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)
        );
        if (FAILED(result) || factory == nullptr) {
            log_hresult("CoCreateInstance(CLSID_WICImagingFactory)", result);
            return false;
        }

        IWICBitmapDecoder* decoder = nullptr;
        result = factory->CreateDecoderFromFilename(
            path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
        );
        if (FAILED(result) || decoder == nullptr) {
            log_hresult("IWICImagingFactory::CreateDecoderFromFilename", result);
            release_com(factory);
            return false;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        result = decoder->GetFrame(0u, &frame);
        if (FAILED(result) || frame == nullptr) {
            log_hresult("IWICBitmapDecoder::GetFrame", result);
            release_com(decoder);
            release_com(factory);
            return false;
        }

        bool const loaded = create_texture_from_wic_frame(context, factory, frame, out_sample);
        release_com(frame);
        release_com(decoder);
        release_com(factory);
        return loaded;
    }

    [[nodiscard]] auto load_texture_from_memory(
        render::Context context, uint8_t const* bytes, size_t byte_count, TextureSample& out_sample
    ) -> bool {
        if (bytes == nullptr || byte_count == 0u || byte_count > 0xffffffffu) {
            return false;
        }

        IWICImagingFactory* factory = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)
        );
        if (FAILED(result) || factory == nullptr) {
            log_hresult("CoCreateInstance(CLSID_WICImagingFactory)", result);
            return false;
        }

        IWICStream* stream = nullptr;
        result = factory->CreateStream(&stream);
        if (FAILED(result) || stream == nullptr) {
            log_hresult("IWICImagingFactory::CreateStream", result);
            release_com(factory);
            return false;
        }

        result = stream->InitializeFromMemory(
            const_cast<uint8_t*>(bytes), static_cast<DWORD>(byte_count)
        );
        if (FAILED(result)) {
            log_hresult("IWICStream::InitializeFromMemory", result);
            release_com(stream);
            release_com(factory);
            return false;
        }

        IWICBitmapDecoder* decoder = nullptr;
        result = factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder
        );
        if (FAILED(result) || decoder == nullptr) {
            log_hresult("IWICImagingFactory::CreateDecoderFromStream", result);
            release_com(stream);
            release_com(factory);
            return false;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        result = decoder->GetFrame(0u, &frame);
        if (FAILED(result) || frame == nullptr) {
            log_hresult("IWICBitmapDecoder::GetFrame", result);
            release_com(decoder);
            release_com(stream);
            release_com(factory);
            return false;
        }

        bool const loaded = create_texture_from_wic_frame(context, factory, frame, out_sample);
        release_com(frame);
        release_com(decoder);
        release_com(stream);
        release_com(factory);
        return loaded;
    }

    auto destroy_texture_sample(render::Context context, TextureSample& sample) -> void {
        if (render::texture_valid(sample.texture)) {
            render::destroy_texture(context, sample.texture);
        }
        sample.size = {};
    }

    [[nodiscard]] auto key_from_virtual_key(WPARAM value) -> gui::Key {
        switch (value) {
        case VK_TAB:
            return gui::Key::TAB;
        case VK_RETURN:
            return gui::Key::ENTER;
        case VK_ESCAPE:
            return gui::Key::ESCAPE;
        case VK_SPACE:
            return gui::Key::SPACE;
        case VK_LEFT:
            return gui::Key::LEFT;
        case VK_RIGHT:
            return gui::Key::RIGHT;
        case VK_UP:
            return gui::Key::UP;
        case VK_DOWN:
            return gui::Key::DOWN;
        case VK_HOME:
            return gui::Key::HOME;
        case VK_END:
            return gui::Key::END;
        case VK_BACK:
            return gui::Key::BACKSPACE;
        case VK_DELETE:
            return gui::Key::DELETE_KEY;
        case 'A':
            return gui::Key::A;
        case 'C':
            return gui::Key::C;
        case 'V':
            return gui::Key::V;
        case 'X':
            return gui::Key::X;
        case 'Z':
            return gui::Key::Z;
        default:
            return gui::Key::UNKNOWN;
        }
    }

    [[nodiscard]] auto current_key_mods() -> gui::KeyMods {
        gui::KeyMods mods = gui::KEY_MOD_NONE;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SHIFT;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_CTRL;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_ALT;
        }
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SUPER;
        }
        return mods;
    }

    auto push_key_event(AppState* state, gui::Key key, gui::KeyEventKind kind, gui::KeyMods mods)
        -> void {
        if (state == nullptr || key == gui::Key::UNKNOWN) {
            return;
        }
        if (state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
#if BASE_DEBUG
            fmt::eprintf("dropped key event\n");
#endif
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {.key = key, .kind = kind, .mods = mods};
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;

#if BASE_DEBUG
        if (kind == gui::KeyEventKind::PRESS || kind == gui::KeyEventKind::REPEAT) {
            fmt::printf(
                "key %s: %u mods=0x%02x\n",
                kind == gui::KeyEventKind::REPEAT ? "repeat" : "press",
                static_cast<unsigned>(key),
                static_cast<unsigned>(state->key_events[index].mods)
            );
        }
#endif
    }

    auto push_text_event(AppState* state, uint32_t codepoint, gui::KeyMods mods) -> void {
        if (state == nullptr || state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {
            .kind = gui::KeyEventKind::TEXT, .mods = mods, .codepoint = codepoint
        };
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    [[nodiscard]] auto key_down_kind(LPARAM lparam) -> gui::KeyEventKind {
        return (lparam & (1ll << 30)) != 0 ? gui::KeyEventKind::REPEAT : gui::KeyEventKind::PRESS;
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
        wchar_t disk_texture_path[MAX_PATH] = {};
        if (!testbed_asset_path(disk_texture_path, MAX_PATH, L"ui_api_testbed_texture.png")) {
            fmt::eprintf("ui_api_testbed_texture.png path is too long\n");
            return false;
        }
        if (!load_texture_from_file(render_context, disk_texture_path, runtime->textures.disk)) {
            return false;
        }
        if (!load_texture_from_memory(
                render_context,
                ui_api_testbed_assets::texture_png,
                ui_api_testbed_assets::texture_png_size,
                runtime->textures.embedded
            )) {
            return false;
        }

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
            "Editable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        UiRuntime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        ManualTrace* trace
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
            draw_ui(ui, runtime->state, style, runtime->textures, delta_time);
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

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                    request_redraw(global_app_state);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                gui::Id const hit_id = frame_hit_id(global_app_state->last_frame, pos);
                bool const needs_frame = global_app_state->redraw_pending ||
                                         !frame_ready(global_app_state->last_frame) ||
                                         global_app_state->input.mouse_down[0u] ||
                                         hit_id.value != global_app_state->mouse_hit_id.value;
                global_app_state->input.mouse_pos = pos;
                global_app_state->mouse_hit_id = hit_id;
                if (needs_frame) {
                    request_redraw(global_app_state);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                global_app_state->input.mouse_down[0u] = true;
                global_app_state->input.mouse_pos = pos;
                global_app_state->input.mouse_triple_clicked[0u] =
                    left_triple_click(*global_app_state, pos);
                global_app_state->left_double_click_pending = false;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[0u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
                request_redraw(global_app_state);
            }
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                global_app_state->input.mouse_down[0u] = true;
                global_app_state->input.mouse_double_clicked[0u] = true;
                global_app_state->input.mouse_pos = pos;
                global_app_state->left_double_click_pos = pos;
                global_app_state->left_double_click_ticks = GetTickCount64();
                global_app_state->left_double_click_pending = true;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 36.0f;
                request_redraw(global_app_state);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            gui::Key const key = key_from_virtual_key(wparam);
            if (key != gui::Key::UNKNOWN) {
                gui::KeyMods const mods = current_key_mods();
                if (global_app_state != nullptr &&
                    key_event_needs_frame(*global_app_state, key, mods)) {
                    push_key_event(global_app_state, key, key_down_kind(lparam), mods);
                    request_redraw(global_app_state);
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        case WM_CHAR:
            if (global_app_state != nullptr) {
                if (text_event_needs_frame(*global_app_state)) {
                    push_text_event(
                        global_app_state, static_cast<uint32_t>(wparam), current_key_mods()
                    );
                    request_redraw(global_app_state);
                }
            }
            return 0;

        case WM_CLOSE:
            if (global_app_state != nullptr) {
                global_app_state->running = false;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            return false;
        }

        HWND const hwnd = CreateWindowExW(
            0u,
            WINDOW_CLASS_NAME,
            L"gui_framework UI API testbed",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    auto destroy_testbed_window(AppState* app_state) -> void {
        if (app_state->hwnd != nullptr && IsWindow(app_state->hwnd)) {
            DestroyWindow(app_state->hwnd);
        }
        app_state->hwnd = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    }

#if BASE_DEBUG
    [[nodiscard]] auto trace_idle_wait_ms(
        TraceOptions const& options,
        ManualTrace const& trace,
        bool trace_start_done,
        uint64_t trace_warmup_start
    ) -> DWORD {
        uint64_t wait_ms = INFINITE;
        if (!trace_start_done && options.path != nullptr) {
            uint64_t const elapsed_ms = GetTickCount64() - trace_warmup_start;
            wait_ms = elapsed_ms < options.warmup_ms ? options.warmup_ms - elapsed_ms : 0u;
        } else if (options.duration_ms != 0u && trace_active(&trace)) {
            double const elapsed_ms = trace_elapsed_ms(trace);
            wait_ms = elapsed_ms < static_cast<double>(options.duration_ms)
                          ? options.duration_ms - static_cast<uint64_t>(elapsed_ms)
                          : 0u;
        }
        return wait_ms >= INFINITE ? INFINITE - 1u : static_cast<DWORD>(wait_ms);
    }
#endif

    auto run_windowed(TraceOptions trace_options) -> int {
        AppState app_state = {};
        global_app_state = &app_state;

        ComApartment com;
        if (!com.init()) {
            global_app_state = nullptr;
            return 1;
        }

        if (!create_testbed_window(&app_state)) {
            global_app_state = nullptr;
            return 1;
        }

        Arena app_arena = {};
        app_arena.init();

        render::Context render_context = {};
        render::ContextDesc context_desc = {};
        context_desc.backend = render::Backend::D3D11;
#if BASE_DEBUG
        context_desc.enable_debug_layer = trace_options.enable_debug_layer;
#endif

        render::Result result = render::create_context(app_arena, context_desc, render_context);
        if (render::result_failed(result)) {
            log_render_result("render::create_context", result);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        render::Window render_window = {};
        render::WindowDesc window_desc = {};
        window_desc.native_window = app_state.hwnd;
        window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
        window_desc.buffer_count = 2u;
        window_desc.present_mode = render::PresentMode::VSYNC;

        result = render::create_window(app_arena, render_context, window_desc, render_window);
        if (render::result_failed(result)) {
            log_render_result("render::create_window", result);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        UiRuntime runtime = {};
        if (!create_ui_runtime(app_arena, render_context, app_state.hwnd, &runtime)) {
            destroy_ui_runtime(render_context, &runtime);
            render::destroy_window(render_window);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        uint64_t previous_ticks = GetTickCount64();
        uint64_t const trace_warmup_start = previous_ticks;
        bool trace_start_done = false;
        ManualTrace trace = {};
        while (app_state.running) {
            if (!trace_start_done && trace_options.path != nullptr &&
                GetTickCount64() - trace_warmup_start >= trace_options.warmup_ms) {
                trace_start_done = true;
#if BASE_DEBUG
                BASE_UNUSED(trace_start(
                    &trace,
                    trace_options.path,
                    render::window_size(render_window),
                    trace_options.enable_debug_layer
                ));
#endif
            }
#if BASE_DEBUG
            if (trace_options.duration_ms != 0u && trace_active(&trace) &&
                trace_elapsed_ms(trace) >= static_cast<double>(trace_options.duration_ms)) {
                app_state.running = false;
                break;
            }
#endif

            {
                TRACE_SCOPE(&trace, "frame");
                {
                    TRACE_SCOPE(&trace, "pump_messages");
                    MSG message = {};
                    while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
                        if (message.message == WM_QUIT) {
                            app_state.running = false;
                            break;
                        }
                        TranslateMessage(&message);
#if BASE_DEBUG
                        if (trace_input_message(message.message)) {
                            TRACE_SCOPE(&trace, "input_handling");
                            DispatchMessageW(&message);
                        } else
#endif
                        {
                            DispatchMessageW(&message);
                        }
                    }
                }

                if (!app_state.running) {
                    break;
                }

                if (app_state.resize_pending) {
                    TRACE_SCOPE(&trace, "resize");
                    result = render::resize_window(
                        render_context, render_window, app_state.pending_size
                    );
                    if (render::result_failed(result)) {
                        log_render_result("render::resize_window", result);
                        break;
                    }
                    app_state.resize_pending = false;
                    app_state.redraw_pending = true;
                }

                if (!app_state.redraw_pending) {
                    TRACE_SCOPE(&trace, "idle_wait");
#if BASE_DEBUG
                    DWORD const wait_ms = trace_idle_wait_ms(
                        trace_options, trace, trace_start_done, trace_warmup_start
                    );
#else
                    DWORD const wait_ms = INFINITE;
#endif
                    BASE_UNUSED(MsgWaitForMultipleObjectsEx(
                        0u, nullptr, wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE
                    ));
                    continue;
                }

                uint64_t const ticks = GetTickCount64();
                float const delta_time = static_cast<float>(ticks - previous_ticks) * 0.001f;
                previous_ticks = ticks;
                app_state.redraw_pending = false;
                uint64_t const state_hash_before = testbed_state_hash(runtime.state);

                {
                    TRACE_SCOPE(&trace, "render_begin_frame");
                    render::begin_frame(render_context);
                }
                app_state.last_frame = build_ui_commands(
                    &runtime,
                    render::window_size(render_window),
                    app_state.input,
                    delta_time,
                    &trace
                );
                app_state.mouse_hit_id =
                    frame_hit_id(app_state.last_frame, app_state.input.mouse_pos);

                render::WindowRenderPassDesc pass_desc = {};
                pass_desc.window = render_window;
                pass_desc.clear_color = liquid_glass_clear_color(runtime.state.theme);

                {
                    TRACE_SCOPE(&trace, "draw_render_commands_to_window");
                    result = draw::render_commands_to_window(
                        runtime.draw_renderer, render_context, pass_desc, runtime.draw_context
                    );
                }
                reset_thread_temp_arenas();
                if (render::result_failed(result)) {
                    log_render_result("draw::render_commands_to_window", result);
                    break;
                }

                {
                    TRACE_SCOPE(&trace, "present");
                    result = render::present_window(render_context, render_window);
                }
                app_state.redraw_pending = runtime.state.selected_tab == 1u ||
                                           testbed_state_hash(runtime.state) != state_hash_before;
                app_state.input.scroll_delta_y = 0.0f;
                app_state.input.mouse_double_clicked[0u] = false;
                app_state.input.mouse_triple_clicked[0u] = false;
                app_state.input.key_events = app_state.key_events;
                app_state.input.key_event_count = 0u;
                if (result == render::Result::OCCLUDED) {
                    TRACE_SCOPE(&trace, "idle_wait");
                    Sleep(16u);
                } else if (render::result_failed(result)) {
                    log_render_result("render::present_window", result);
                    break;
                }
            }
        }

#if BASE_DEBUG
        trace_finish(
            &trace,
            trace_options.path,
            render::window_size(render_window),
            trace_options.enable_debug_layer
        );
#else
        BASE_UNUSED(trace_options);
#endif
        destroy_ui_runtime(render_context, &runtime);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        destroy_testbed_window(&app_state);
        global_app_state = nullptr;
        return 0;
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
        draw_ui(ui, state, style, textures, 1.0f / 60.0f);
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

} // namespace

auto main(int argc, char** argv) -> int {
    base::install_crash_handlers();

#if defined(_WIN32)
    TraceOptions trace_options = {};
    if (!parse_trace_options(argc, argv, &trace_options)) {
        return 2;
    }
#if !BASE_DEBUG
    if (trace_requested(trace_options)) {
        fmt::printf("ui_api_testbed trace: debug-only tracing is disabled in this build\n");
        trace_options = {};
    }
#endif
    int const result = run_windowed(trace_options);
#else
    BASE_UNUSED(argc);
    BASE_UNUSED(argv);
    int const result = run_console_fallback();
#endif
    shutdown_thread_temp_arenas();
    return result;
}
