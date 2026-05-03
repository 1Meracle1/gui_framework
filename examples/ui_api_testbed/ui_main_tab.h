#pragma once

#include "ui_common.h"

namespace ui_api_testbed {

    auto draw_main_tab(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        TestbedChrome const& chrome
    ) -> void;

} // namespace ui_api_testbed
