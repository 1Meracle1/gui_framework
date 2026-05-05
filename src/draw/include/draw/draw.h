#pragma once

#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <font_cache/font_cache.h>
#include <render/render.h>

namespace gui::draw {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Color {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct Rect {
        Vec2 min = {};
        Vec2 max = {};
    };

    struct Transform2D {
        Vec2 x_axis = {1.0f, 0.0f};
        Vec2 y_axis = {0.0f, 1.0f};
        Vec2 translation = {};
    };

    struct Vertex {
        Vec2 position = {};
        Vec2 uv = {};
        Color color = {};
    };

    struct PrimitiveCommand {
        Vertex const* vertices = nullptr;
        size_t vertex_count = 0u;
        gui::render::Texture texture = {};
        Rect clip_rect = {};
        Transform2D transform = {};
        float opacity = 1.0f;
    };

    struct PrimitiveBatch {
        size_t command_index = 0u;
        size_t command_count = 0u;
        size_t vertex_count = 0u;
        gui::render::Texture texture = {};
        Rect clip_rect = {};
        Transform2D transform = {};
        float opacity = 1.0f;
    };

    enum class LayerBlendMode : uint8_t {
        NORMAL,
        PREMULTIPLIED_NORMAL,
        ADDITIVE,
        MULTIPLY,
        SCREEN,
    };

    enum class FilterKind : uint8_t {
        NONE,
        BLUR,
    };

    struct DropShadow {
        Vec2 offset = {};
        float blur_radius = 0.0f;
        Color color = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct LayerDesc {
        Rect bounds = {};
        float clip_radius = 0.0f;
        float opacity = 1.0f;
        LayerBlendMode blend_mode = LayerBlendMode::NORMAL;
        bool isolated = true;
        FilterKind filter_kind = FilterKind::NONE;
        float filter_radius = 0.0f;
        DropShadow drop_shadow = {};
    };

    struct LayerCommand {
        LayerDesc desc = {};
        Rect clip_rect = {};
        size_t begin_command_index = 0u;
        size_t end_command_index = 0u;
    };

    enum class CommandKind : uint8_t {
        PRIMITIVE_BATCH,
        STYLED_RECT,
        TEXT,
        LAYER_BEGIN,
        LAYER_END,
    };

    struct Command {
        CommandKind kind = CommandKind::PRIMITIVE_BATCH;
        size_t index = 0u;
    };

    struct ContextDesc {
        font_cache::Cache font_cache = {};
        size_t initial_command_capacity = 256u;
        size_t frame_arena_reserve_size = 4u * 1024u * 1024u;
        size_t frame_arena_commit_size = DEFAULT_ARENA_COMMIT_SIZE;
    };

    struct Context {
        void* handle = nullptr;
    };

    struct TextStyle {
        font_cache::Font font = {};
        float size = 16.0f;
        font_provider::RasterPolicy raster_policy = font_provider::RasterPolicy::SHARP_HINTED;
        Color color = {};
    };

    struct BoxShadow {
        Vec2 offset = {};
        float blur_radius = 0.0f;
        float spread = 0.0f;
        Color color = {0.0f, 0.0f, 0.0f, 0.0f};
        bool inset = false;
    };

    struct BoxStyle {
        Color fill_color = {};
        gui::render::Texture texture = {};
        Rect uv_rect = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        Color border_color = {};
        float border_thickness = 0.0f;
        float radius = 0.0f;
        float softness = 1.0f;
        BoxShadow shadow = {};
    };

    struct StyledRectCommand {
        Rect rect = {};
        BoxStyle style = {};
        Rect clip_rect = {};
        Transform2D transform = {};
        float opacity = 1.0f;
    };

    struct TextCommand {
        Vec2 position = {};
        TextStyle style = {};
        StrRef text = {};
        font_cache::TextRun run = {};
        Rect clip_rect = {};
        Transform2D transform = {};
        float opacity = 1.0f;
    };

    [[nodiscard]] auto context_valid(Context context) -> bool;

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void;
    auto destroy_context(Context& context) -> void;

    auto begin_frame(Context context) -> void;
    auto end_frame(Context context) -> void;

    // Clip rects are axis-aligned screen-space scissor metadata. Transforms do not affect them.
    auto push_clip_rect(Context context, Rect rect) -> Rect;
    auto pop_clip_rect(Context context) -> Rect;
    [[nodiscard]] auto top_clip_rect(Context context) -> Rect;

    auto push_transform(Context context, Transform2D transform) -> Transform2D;
    auto pop_transform(Context context) -> Transform2D;
    [[nodiscard]] auto top_transform(Context context) -> Transform2D;

    auto push_opacity(Context context, float opacity) -> float;
    auto pop_opacity(Context context) -> float;
    [[nodiscard]] auto top_opacity(Context context) -> float;

    auto push_layer(Context context, LayerDesc desc) -> void;
    auto pop_layer(Context context) -> void;

    auto draw_line(Context context, Vec2 p0, Vec2 p1, Color color, float thickness) -> void;
    auto draw_polyline(
        Context context, Slice<Vec2 const> points, Color color, float thickness, bool closed
    ) -> void;
    auto draw_triangle(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Color color, float thickness)
        -> void;
    auto draw_triangle_filled(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Color color) -> void;
    auto
    draw_quad(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color color, float thickness)
        -> void;
    auto draw_quad_filled(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color color) -> void;
    auto draw_rect(Context context, Rect rect, Color color, float thickness, float rounding)
        -> void;
    auto draw_rect_filled(Context context, Rect rect, Color color, float rounding) -> void;
    auto draw_rect_styled(Context context, Rect rect, BoxStyle style) -> void;
    auto
    draw_image(Context context, gui::render::Texture texture, Rect rect, Rect uv_rect, Color color)
        -> void;
    auto draw_rect_filled_multicolor(
        Context context,
        Rect rect,
        Color top_left,
        Color top_right,
        Color bottom_right,
        Color bottom_left
    ) -> void;
    auto draw_circle(
        Context context,
        Vec2 center,
        float radius,
        Color color,
        float thickness,
        int32_t segment_count
    ) -> void;
    auto draw_circle_filled(
        Context context, Vec2 center, float radius, Color color, int32_t segment_count
    ) -> void;
    auto draw_ellipse(
        Context context,
        Vec2 center,
        Vec2 radius,
        Color color,
        float thickness,
        int32_t segment_count
    ) -> void;
    auto draw_ellipse_filled(
        Context context, Vec2 center, Vec2 radius, Color color, int32_t segment_count
    ) -> void;

    auto path_clear(Context context) -> void;
    auto path_line_to(Context context, Vec2 p) -> void;
    auto path_arc_to(
        Context context,
        Vec2 center,
        float radius,
        float angle_min,
        float angle_max,
        int32_t segment_count
    ) -> void;
    auto path_bezier_quadratic_to(Context context, Vec2 control, Vec2 end, int32_t segment_count)
        -> void;
    auto path_bezier_cubic_to(
        Context context, Vec2 control0, Vec2 control1, Vec2 end, int32_t segment_count
    ) -> void;
    auto path_rect(Context context, Rect rect, float rounding) -> void;
    auto path_stroke(Context context, Color color, bool closed, float thickness) -> void;
    auto path_fill_convex(Context context, Color color) -> void;

    auto draw_text(
        Context context, Vec2 position, TextStyle const& style, StrRef text, float* out_advance
    ) -> void;
    auto
    measure_text(Context context, TextStyle const& style, StrRef text, font_cache::TextRun& out_run)
        -> void;

    [[nodiscard]] auto primitive_command_count(Context context) -> size_t;
    [[nodiscard]] auto primitive_command(Context context, size_t index) -> PrimitiveCommand const*;
    [[nodiscard]] auto primitive_batch_count(Context context) -> size_t;
    [[nodiscard]] auto primitive_batch(Context context, size_t index) -> PrimitiveBatch const*;
    [[nodiscard]] auto command_count(Context context) -> size_t;
    [[nodiscard]] auto command(Context context, size_t index) -> Command const*;
    [[nodiscard]] auto layer_command_count(Context context) -> size_t;
    [[nodiscard]] auto layer_command(Context context, size_t index) -> LayerCommand const*;
    [[nodiscard]] auto styled_rect_command_count(Context context) -> size_t;
    [[nodiscard]] auto styled_rect_command(Context context, size_t index)
        -> StyledRectCommand const*;
    [[nodiscard]] auto text_command_count(Context context) -> size_t;
    [[nodiscard]] auto text_command(Context context, size_t index) -> TextCommand const*;

} // namespace gui::draw
