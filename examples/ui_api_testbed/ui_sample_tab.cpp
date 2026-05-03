#include "ui_sample_tab.h"

#include "ui_common.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>

namespace ui_api_testbed {

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
                        .background = gui::color_alpha(tokens.accent, 0.22f + pulse * 0.62f),
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
                        .background = gui::color_alpha(
                            tokens.accent, loading_ring_alpha(ring_phase, index, DOT_COUNT)
                        ),
                        .radius = 2.5f,
                    },
                });
            }
        }
    }

    auto draw_sample_tab(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        TestbedChrome const& chrome,
        float delta_time
    ) -> void {
        gui::ThemeTokens const& tokens = spec.tokens;
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
            if (auto row = ui.row(gui::id("sample_tab_controls"), chrome.controls_bar)) {
                if (auto switches =
                        ui.row(gui::id("sample_control_switches"), chrome.toolbar_group)) {
                    ui.checkbox(
                        gui::id("sample_enabled_checkbox"),
                        "Enabled",
                        &state.sample_enabled,
                        chrome.enabled_checkbox
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
                if (auto edit = ui.row(gui::id("sample_control_edit"), chrome.toolbar_group)) {
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
                            .style = chrome.toolbar_label,
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
                        ui.row(gui::id("sample_control_actions"), chrome.toolbar_group)) {
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
                    auto row_scope =
                        ui.id_scope(gui::id("sample_table_row", static_cast<uint64_t>(index)));
                    BASE_UNUSED(row_scope);
                    if (auto row = table.row(gui::id("row"))) {
                        draw_sample_table_cell(ui, row, gui::id("item"), item.item, 132.0f);
                        draw_sample_table_cell(ui, row, gui::id("layer"), item.layer, 116.0f);
                        draw_sample_table_cell(ui, row, gui::id("state"), item.state, 116.0f);
                    }
                }
            }
        }
    }

} // namespace ui_api_testbed
