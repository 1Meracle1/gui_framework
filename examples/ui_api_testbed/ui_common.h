#pragma once

#include "ui_theme.h"

namespace ui_api_testbed {

    struct TestbedChrome {
        gui::BoxDesc controls_bar = {};
        gui::BoxDesc toolbar_group = {};
        gui::StyleDesc toolbar_label = {};
        gui::BoxDesc enabled_checkbox = {};
        gui::BoxDesc read_only_checkbox = {};
        gui::BoxDesc size_radio = {};
    };

    [[nodiscard]] auto testbed_chrome(LiquidGlassSpec const& spec) -> TestbedChrome;
    auto draw_scroll_lines(gui::Frame& ui, StrRef prefix, size_t count) -> void;

} // namespace ui_api_testbed
