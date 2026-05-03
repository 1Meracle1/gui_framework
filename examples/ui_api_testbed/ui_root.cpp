#include "ui_root.h"

#include "ui_common.h"
#include "ui_dialogs.h"
#include "ui_main_tab.h"
#include "ui_sample_tab.h"

namespace ui_api_testbed {

    auto draw_ui(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        gui::InputState const& input,
        float delta_time
    ) -> void {
        gui::Id const list_id = gui::id("asset_list");
        gui::Id const log_id = gui::id("log_scroll");

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

            TestbedChrome const chrome = testbed_chrome(spec);
            size_t const selected_tab = tab_view.selected_index();
            if (selected_tab == 1u) {
                draw_sample_tab(ui, state, spec, textures, chrome, delta_time);
            } else {
                draw_main_tab(ui, state, spec, textures, chrome);
            }

            if (selected_tab == 0u && state.modal_open) {
                draw_testbed_modal(ui, state, spec);
            }
            draw_image_preview_modal(ui, state, spec, textures, input);
        }
    }

} // namespace ui_api_testbed
