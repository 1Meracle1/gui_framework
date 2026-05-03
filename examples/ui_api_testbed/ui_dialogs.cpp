#include "ui_dialogs.h"

#include <algorithm>
#include <base/fmt.h>

namespace ui_api_testbed {

    auto draw_testbed_modal(gui::Frame& ui, TestbedState& state, LiquidGlassSpec const& spec)
        -> void {
        gui::ThemeTokens const& tokens = spec.tokens;
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

    auto draw_image_preview_modal(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        gui::InputState const& input
    ) -> void {
        gui::ThemeTokens const& tokens = spec.tokens;
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

} // namespace ui_api_testbed
