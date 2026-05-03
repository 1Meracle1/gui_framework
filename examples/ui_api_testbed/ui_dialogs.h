#pragma once

#include "ui_theme.h"

namespace ui_api_testbed {

    auto draw_testbed_modal(gui::Frame& ui, TestbedState& state, LiquidGlassSpec const& spec)
        -> void;
    auto draw_image_preview_modal(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        gui::InputState const& input
    ) -> void;

} // namespace ui_api_testbed
