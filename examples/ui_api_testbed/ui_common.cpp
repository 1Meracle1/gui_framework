#include "ui_common.h"

#include <base/fmt.h>

namespace ui_api_testbed {

    auto testbed_chrome(LiquidGlassSpec const& spec) -> TestbedChrome {
        gui::ThemeTokens const& tokens = spec.tokens;
        return {
            .controls_bar =
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(5.0f, 6.0f),
                            .gap = 5.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style =
                        {
                            .background = spec.controls_background,
                            .border = spec.controls_border,
                            .border_thickness = 1.0f,
                            .radius = 21.0f,
                            .shadow =
                                {
                                    .offset = {0.0f, 8.0f},
                                    .blur_radius = 28.0f,
                                    .color = spec.controls_shadow,
                                },
                        },
                },
            .toolbar_group =
                {
                    .layout =
                        {
                            .width = gui::children(),
                            .height = gui::px(34.0f),
                            .padding = gui::insets(3.0f, 4.0f),
                            .gap = 4.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style =
                        {
                            .background = spec.toolbar_group_background,
                            .border = spec.toolbar_group_border,
                            .border_thickness = 1.0f,
                            .radius = 17.0f,
                        },
                },
            .toolbar_label = {.foreground = tokens.text_muted},
            .enabled_checkbox = {.layout = {.width = gui::px(98.0f), .height = gui::fill()}},
            .read_only_checkbox =
                {
                    .layout = {.width = gui::px(110.0f), .height = gui::fill()},
                    .flags = gui::BOX_FLAG_READ_ONLY,
                },
            .size_radio = {.layout = {.width = gui::px(74.0f), .height = gui::fill()}},
        };
    }

    auto draw_scroll_lines(gui::Frame& ui, StrRef prefix, size_t count) -> void {
        for (size_t index = 0u; index < count; ++index) {
            ui.label(
                fmt::tprintf("%s %02zu", prefix, index),
                {.layout = {.width = gui::fill(), .height = gui::px(22.0f)}}
            );
        }
    }

} // namespace ui_api_testbed
