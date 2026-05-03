#pragma once

#include "ui_common.h"

namespace ui_api_testbed {

    auto draw_sample_tab(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        TestbedChrome const& chrome,
        float delta_time
    ) -> void;

} // namespace ui_api_testbed
