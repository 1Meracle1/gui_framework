#pragma once

#include "ui_theme.h"

namespace ui_api_testbed {

    auto draw_liquid_glass_backdrop(
        gui::draw::Context context, gui::render::SizeU32 size, LiquidGlassTheme theme
    ) -> void;

} // namespace ui_api_testbed
