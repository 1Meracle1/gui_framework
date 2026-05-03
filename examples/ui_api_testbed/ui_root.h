#pragma once

#include "ui_theme.h"

namespace ui_api_testbed {

    auto draw_ui(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        gui::InputState const& input,
        float delta_time
    ) -> void;

} // namespace ui_api_testbed
