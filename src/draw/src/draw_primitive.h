#pragma once

#include <cstdint>
#include <draw/draw.h>

namespace gui::draw::primitive_model {

    enum class RenderKind : uint8_t {
        TRIANGLES,
        ANALYTIC_RECT,
        COVERAGE_MASK,
    };

    struct AnalyticRect {
        Rect rect = {};
        Transform2D transform = {};
        Color fill_color = {};
        Color border_color = {};
        float border_thickness = 0.0f;
        float radius = 0.0f;
        float softness = 1.0f;
    };

    struct PrimitiveInfo {
        RenderKind render_kind = RenderKind::TRIANGLES;
        AnalyticRect analytic_rect = {};
        size_t coverage_mask_shape_index = static_cast<size_t>(-1);
    };

    [[nodiscard]] auto primitive_info(Context context, size_t index) -> PrimitiveInfo const*;

} // namespace gui::draw::primitive_model
