#include "ui_backdrop.h"

#include <algorithm>

namespace ui_api_testbed {

    namespace draw = gui::draw;
    namespace render = gui::render;

    auto
    draw_liquid_glass_backdrop(draw::Context context, render::SizeU32 size, LiquidGlassTheme theme)
        -> void {
        float const width = static_cast<float>(size.width);
        float const height = static_cast<float>(size.height);
        float const long_side = std::max(width, height);
        draw::Rect const canvas = {{0.0f, 0.0f}, {width, height}};
        bool const is_light = theme == LiquidGlassTheme::LIGHT;

        draw::Color const canvas_0 = is_light ? draw::Color{0.94f, 0.975f, 1.0f, 1.0f}
                                              : draw::Color{0.014f, 0.024f, 0.038f, 0.98f};
        draw::Color const canvas_1 = is_light ? draw::Color{0.90f, 0.965f, 0.94f, 1.0f}
                                              : draw::Color{0.020f, 0.052f, 0.058f, 0.98f};
        draw::Color const canvas_2 = is_light ? draw::Color{0.985f, 0.95f, 1.0f, 1.0f}
                                              : draw::Color{0.030f, 0.026f, 0.046f, 0.98f};
        draw::Color const canvas_3 = is_light ? draw::Color{1.0f, 0.945f, 0.91f, 1.0f}
                                              : draw::Color{0.048f, 0.030f, 0.044f, 0.98f};
        draw::Color const accent_a = is_light ? draw::Color{0.10f, 0.72f, 0.86f, 0.120f}
                                              : draw::Color{0.08f, 0.56f, 0.68f, 0.105f};
        draw::Color const accent_b = is_light ? draw::Color{0.24f, 0.44f, 0.95f, 0.095f}
                                              : draw::Color{0.34f, 0.34f, 0.90f, 0.085f};
        draw::Color const accent_c = is_light ? draw::Color{1.0f, 0.42f, 0.25f, 0.085f}
                                              : draw::Color{0.80f, 0.24f, 0.18f, 0.060f};
        draw::Color const sheen_a = is_light ? draw::Color{1.0f, 1.0f, 1.0f, 0.125f}
                                             : draw::Color{0.78f, 0.92f, 1.0f, 0.014f};
        draw::Color const sheen_b = is_light ? draw::Color{0.30f, 0.56f, 1.0f, 0.055f}
                                             : draw::Color{0.20f, 0.30f, 0.70f, 0.016f};
        draw::Color const highlight_a = is_light ? draw::Color{1.0f, 1.0f, 1.0f, 0.190f}
                                                 : draw::Color{1.0f, 1.0f, 1.0f, 0.018f};
        draw::Color const highlight_b = is_light ? draw::Color{0.36f, 0.72f, 0.92f, 0.075f}
                                                 : draw::Color{0.52f, 0.82f, 0.90f, 0.022f};

        draw::draw_rect_filled_multicolor(context, canvas, canvas_0, canvas_1, canvas_2, canvas_3);
        draw::draw_circle_filled(
            context, {width * 0.18f, height * 0.18f}, long_side * 0.30f, accent_a, 72
        );
        draw::draw_circle_filled(
            context, {width * 0.78f, height * 0.28f}, long_side * 0.34f, accent_b, 72
        );
        draw::draw_circle_filled(
            context, {width * 0.55f, height * 0.92f}, long_side * 0.36f, accent_c, 72
        );
        draw::draw_quad_filled(
            context,
            {width * 0.46f, 0.0f},
            {width * 0.66f, 0.0f},
            {width * 0.50f, height},
            {width * 0.31f, height},
            sheen_a
        );
        draw::draw_quad_filled(
            context,
            {width * 0.80f, 0.0f},
            {width, 0.0f},
            {width, height * 0.70f},
            {width * 0.66f, height},
            sheen_b
        );
        draw::draw_rect_filled(
            context,
            {{width * 0.08f, height * 0.08f}, {width * 0.92f, height * 0.15f}},
            highlight_a,
            38.0f
        );
        draw::draw_rect_filled(
            context,
            {{width * 0.14f, height * 0.84f}, {width * 0.72f, height * 0.91f}},
            highlight_b,
            34.0f
        );
    }

} // namespace ui_api_testbed
