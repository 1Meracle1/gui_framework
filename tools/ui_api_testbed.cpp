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
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
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
        size_t selected_tab = 0u;
        size_t selected_index = 12u;
        char name[64] = "Editable text";
        char sample_name[64] = "Second tab text";
        gui::TextSelection title_selection = {};
        gui::TextSelection body_selection = {};
        gui::Signal header_signal = {};
        gui::Signal selected_row_signal = {};
        StringBuffer multiline_text_buffer;
    };

    auto row_id(size_t index) -> gui::Id {
        return gui::id(0xA1100000ull + static_cast<uint64_t>(index));
    }

    constexpr char BODY_TEXT[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n"
        "Integer posuere erat a ante venenatis dapibus posuere velit aliquet.\n\n"
        "Donec ullamcorper nulla non metus auctor fringilla.\n"
        "Cras mattis consectetur purus sit amet fermentum.\n\n"
        "Maecenas sed diam eget risus varius blandit sit amet non magna.\n"
        "Vestibulum id ligula porta felis euismod semper.";
    constexpr char CLOSE_GLYPH[] = "\xC3\x97";

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

    auto draw_ui(gui::Frame& ui, TestbedState& state, LiquidGlassSpec const& spec) -> void {
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

            gui::TabItem testbed_tabs[] = {
                {gui::id("testbed_main_tab"), "Testbed"},
                {gui::id("testbed_sample_tab"), "Samples"},
            };
            auto tab_view = ui.tab_view(
                gui::id("testbed_tabs"),
                {
                    .tabs = slice(testbed_tabs),
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
                                                  .width = gui::px(66.0f),
                                                  .height = gui::fill(),
                                                  .padding = gui::insets(3.0f, 6.0f),
                                              },
                                          .debug_name = "popup_button",
                                      }
                                )
                                    .activated) {
                                state.popup_open = !state.popup_open;
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
                                .layout = {.width = gui::fill(), .height = gui::text()},
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
                            ui.label(
                                "Preview fills the overlay",
                                {
                                    .layout = {.width = gui::text(), .height = gui::fill()},
                                    .style = {.foreground = tokens.text_muted},
                                }
                            );
                            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
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
                        if (auto table = ui.table(
                                gui::id("preview_table"),
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
                                }
                            )) {
                            if (auto header = table.header_row()) {
                                if (auto cell = header.cell(
                                        gui::id("preview_table_header_plan"),
                                        {
                                            .column_span = 2u,
                                        }
                                    )) {
                                    ui.label(
                                        "Plan",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                                if (auto cell =
                                        header.cell(gui::id("preview_table_header_status"))) {
                                    ui.label(
                                        "Status",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                            }
                            if (auto row = table.row()) {
                                if (auto cell = row.cell(
                                        gui::id("preview_table_row_span"),
                                        {
                                            .row_span = 2u,
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
                                        "Joined rows",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                                if (auto cell = row.cell(
                                        gui::id("preview_table_build_cell"),
                                        {
                                            .box = {
                                                .layout = {
                                                    .width = gui::px(204.0f),
                                                },
                                            },
                                        }
                                    )) {
                                    ui.label(
                                        "Build table element",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                                if (auto cell = row.cell(
                                        gui::id("preview_table_done_cell"),
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
                                        "Done",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                            }
                            if (auto row = table.row()) {
                                if (auto cell = row.cell(
                                        gui::id("preview_table_span_cell"),
                                        {
                                            .column_span = 2u,
                                        }
                                    )) {
                                    ui.label(
                                        "Joined columns",
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
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

    struct UiRuntime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        gui::Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        TestbedState state = {};
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::InputState input = {};
        gui::KeyEvent key_events[MAX_KEY_EVENTS_PER_FRAME] = {};
        gui::Vec2 left_double_click_pos = {};
        uint64_t left_double_click_ticks = 0u;
        bool left_double_click_pending = false;
    };

    AppState* global_app_state = nullptr;

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

    auto push_key_event(AppState* state, gui::Key key, gui::KeyEventKind kind) -> void {
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
        state->key_events[index] = {.key = key, .kind = kind, .mods = current_key_mods()};
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

    auto push_text_event(AppState* state, uint32_t codepoint) -> void {
        if (state == nullptr || state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {
            .kind = gui::KeyEventKind::TEXT, .mods = current_key_mods(), .codepoint = codepoint
        };
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    [[nodiscard]] auto key_down_kind(LPARAM lparam) -> gui::KeyEventKind {
        return (lparam & (1ll << 30)) != 0 ? gui::KeyEventKind::REPEAT : gui::KeyEventKind::PRESS;
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        HWND const hwnd = static_cast<HWND>(user_data);
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
        HWND const hwnd = static_cast<HWND>(user_data);
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
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
    }

    [[nodiscard]] auto
    create_ui_runtime(Arena& arena, render::Context render_context, HWND hwnd, UiRuntime* runtime)
        -> bool {
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
                .clipboard_user_data = hwnd,
            },
            runtime->ui_context
        );
        BASE_UNUSED(runtime->state.multiline_text_buffer.write_string(
            "Editable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        return true;
    }

    auto build_ui_commands(
        UiRuntime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> void {
        LiquidGlassSpec const style = liquid_glass_spec(runtime->state.theme);
        gui::ThemeDesc theme = gui::default_theme();
        configure_liquid_glass_theme(theme, runtime->font, style);
        gui::set_theme(runtime->ui_context, theme);

        gui::Frame ui = gui::begin_frame(
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
        draw_ui(ui, runtime->state, style);
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        draw_liquid_glass_backdrop(runtime->draw_context, window_size, runtime->state.theme);
        gui::render_frame(ui, runtime->draw_context);
        draw::end_frame(runtime->draw_context);
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
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
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[0u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
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
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 36.0f;
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            gui::Key const key = key_from_virtual_key(wparam);
            if (key != gui::Key::UNKNOWN) {
                push_key_event(global_app_state, key, key_down_kind(lparam));
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        case WM_CHAR:
            if (global_app_state != nullptr) {
                push_text_event(global_app_state, static_cast<uint32_t>(wparam));
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

    auto run_windowed() -> int {
        AppState app_state = {};
        global_app_state = &app_state;

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
        context_desc.enable_debug_layer = true;
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
        while (app_state.running) {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    app_state.running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (!app_state.running) {
                break;
            }

            if (app_state.resize_pending) {
                result =
                    render::resize_window(render_context, render_window, app_state.pending_size);
                if (render::result_failed(result)) {
                    log_render_result("render::resize_window", result);
                    break;
                }
                app_state.resize_pending = false;
            }

            uint64_t const ticks = GetTickCount64();
            float const delta_time = static_cast<float>(ticks - previous_ticks) * 0.001f;
            previous_ticks = ticks;

            render::begin_frame(render_context);
            build_ui_commands(
                &runtime, render::window_size(render_window), app_state.input, delta_time
            );

            render::WindowRenderPassDesc pass_desc = {};
            pass_desc.window = render_window;
            pass_desc.clear_color = liquid_glass_clear_color(runtime.state.theme);

            result = draw::render_commands_to_window(
                runtime.draw_renderer, render_context, pass_desc, runtime.draw_context
            );
            reset_thread_temp_arenas();
            if (render::result_failed(result)) {
                log_render_result("draw::render_commands_to_window", result);
                break;
            }

            result = render::present_window(render_context, render_window);
            app_state.input.scroll_delta_y = 0.0f;
            app_state.input.mouse_double_clicked[0u] = false;
            app_state.input.mouse_triple_clicked[0u] = false;
            app_state.input.key_events = app_state.key_events;
            app_state.input.key_event_count = 0u;
            if (result == render::Result::OCCLUDED) {
                Sleep(16u);
                continue;
            }
            if (render::result_failed(result)) {
                log_render_result("render::present_window", result);
                break;
            }
        }

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
        gui::Frame ui =
            gui::begin_frame(ui_context, {.size = {640.0f, 400.0f}, .delta_time = 1.0f / 60.0f});
        draw_ui(ui, state, style);
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

auto main() -> int {
    base::install_crash_handlers();

#if defined(_WIN32)
    int const result = run_windowed();
#else
    int const result = run_console_fallback();
#endif
    shutdown_thread_temp_arenas();
    return result;
}
