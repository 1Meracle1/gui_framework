#include "coverage_mask_atlas.h"
#include "draw_path.h"
#include "draw_primitive.h"

#include <algorithm>
#include <base/memory.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <draw/draw.h>

namespace gui::draw {
    namespace {

        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 2.0f * PI;
        constexpr int32_t MIN_SEGMENTS = 3;
        constexpr int32_t MAX_SEGMENTS = 128;
        constexpr float AA_FRINGE_SIZE = 1.0f;
        constexpr float DEFAULT_STROKE_MITER_LIMIT = 10.0f;
        constexpr float TRIANGULATION_EPSILON = 0.000001f;
        constexpr size_t INVALID_SHAPE_COMMAND_INDEX = static_cast<size_t>(-1);
        constexpr Color TRANSPARENT = {0.0f, 0.0f, 0.0f, 0.0f};

        struct ClipStackNode {
            ClipStackNode* next = nullptr;
            Rect value = {};
        };

        struct TransformStackNode {
            TransformStackNode* next = nullptr;
            Transform2D value = {};
        };

        struct OpacityStackNode {
            OpacityStackNode* next = nullptr;
            float value = 1.0f;
        };

        struct LayerStackNode {
            LayerStackNode* next = nullptr;
            size_t index = 0u;
            Rect previous_clip_rect = {};
        };

        struct StrokeStyle {
            float thickness = 0.0f;
            float half_width = 0.0f;
            float outer_half_width = 0.0f;
            float miter_limit = DEFAULT_STROKE_MITER_LIMIT;
            bool closed = false;
        };

        struct ContextImpl {
            Arena frame_arena = {};
            font_cache::Cache font_cache = {};
            PrimitiveCommand* primitive_commands = nullptr;
            primitive_model::PrimitiveInfo* primitive_infos = nullptr;
            PrimitiveBatch* primitive_batches = nullptr;
            Command* commands = nullptr;
            LayerCommand* layer_commands = nullptr;
            StyledRectCommand* styled_rect_commands = nullptr;
            TextCommand* text_commands = nullptr;
            path_model::ShapeCommand* shape_commands = nullptr;
            path_model::Path path = {};
            ClipStackNode* clip_stack_top = nullptr;
            TransformStackNode* transform_stack_top = nullptr;
            OpacityStackNode* opacity_stack_top = nullptr;
            LayerStackNode* layer_stack_top = nullptr;
            Rect current_clip_rect = {};
            Transform2D current_transform = {};
            float current_opacity = 1.0f;
            size_t primitive_command_count = 0u;
            size_t primitive_batch_count = 0u;
            size_t command_count = 0u;
            size_t layer_command_count = 0u;
            size_t styled_rect_command_count = 0u;
            size_t text_command_count = 0u;
            size_t shape_command_count = 0u;
            size_t command_capacity = 0u;
            size_t order_capacity = 0u;
        };

        [[nodiscard]] auto context_from_handle(Context context) -> ContextImpl* {
            return static_cast<ContextImpl*>(context.handle);
        }

        [[nodiscard]] auto default_clip_rect() -> Rect {
            return {{-FLT_MAX, -FLT_MAX}, {FLT_MAX, FLT_MAX}};
        }

        auto reset_state(ContextImpl* impl) -> void {
            ASSERT(impl != nullptr);
            impl->clip_stack_top = nullptr;
            impl->transform_stack_top = nullptr;
            impl->opacity_stack_top = nullptr;
            impl->layer_stack_top = nullptr;
            impl->current_clip_rect = default_clip_rect();
            impl->current_transform = {};
            impl->current_opacity = 1.0f;
        }

        [[nodiscard]] auto color_visible(Color color) -> bool {
            return color.a > 0.0f;
        }

        [[nodiscard]] auto box_shadow_visible(BoxShadow const& shadow) -> bool {
            return !shadow.inset && color_visible(shadow.color);
        }

        [[nodiscard]] auto box_style_visible(BoxStyle const& style) -> bool {
            return color_visible(style.fill_color) ||
                   (style.border_thickness > 0.0f && color_visible(style.border_color)) ||
                   box_shadow_visible(style.shadow);
        }

        [[nodiscard]] auto transform_point(Transform2D const& transform, Vec2 point) -> Vec2 {
            return {
                (point.x * transform.x_axis.x) + (point.y * transform.y_axis.x) +
                    transform.translation.x,
                (point.x * transform.x_axis.y) + (point.y * transform.y_axis.y) +
                    transform.translation.y
            };
        }

        [[nodiscard]] auto transform_vector(Transform2D const& transform, Vec2 vector) -> Vec2 {
            return {
                (vector.x * transform.x_axis.x) + (vector.y * transform.y_axis.x),
                (vector.x * transform.x_axis.y) + (vector.y * transform.y_axis.y)
            };
        }

        [[nodiscard]] auto compose_transform(Transform2D outer, Transform2D inner) -> Transform2D {
            return {
                transform_vector(outer, inner.x_axis),
                transform_vector(outer, inner.y_axis),
                transform_point(outer, inner.translation)
            };
        }

        [[nodiscard]] auto vec2_add(Vec2 lhs, Vec2 rhs) -> Vec2 {
            return {lhs.x + rhs.x, lhs.y + rhs.y};
        }

        [[nodiscard]] auto vec2_sub(Vec2 lhs, Vec2 rhs) -> Vec2 {
            return {lhs.x - rhs.x, lhs.y - rhs.y};
        }

        [[nodiscard]] auto vec2_mul(Vec2 value, float scalar) -> Vec2 {
            return {value.x * scalar, value.y * scalar};
        }

        [[nodiscard]] auto rect_normalized(Rect rect) -> Rect {
            return {
                {std::min(rect.min.x, rect.max.x), std::min(rect.min.y, rect.max.y)},
                {std::max(rect.min.x, rect.max.x), std::max(rect.min.y, rect.max.y)}
            };
        }

        [[nodiscard]] auto rect_intersect(Rect lhs, Rect rhs) -> Rect {
            return {
                {std::max(lhs.min.x, rhs.min.x), std::max(lhs.min.y, rhs.min.y)},
                {std::min(lhs.max.x, rhs.max.x), std::min(lhs.max.y, rhs.max.y)}
            };
        }

        [[nodiscard]] auto rect_width(Rect rect) -> float {
            return rect.max.x - rect.min.x;
        }

        [[nodiscard]] auto rect_height(Rect rect) -> float {
            return rect.max.y - rect.min.y;
        }

        [[nodiscard]] auto rect_visible(Rect rect) -> bool {
            return rect_width(rect) > 0.0f && rect_height(rect) > 0.0f;
        }

        [[nodiscard]] auto rect_outset(Rect rect, float amount) -> Rect {
            return {
                {rect.min.x - amount, rect.min.y - amount},
                {rect.max.x + amount, rect.max.y + amount}
            };
        }

        [[nodiscard]] auto vec2_equal(Vec2 lhs, Vec2 rhs) -> bool {
            return lhs.x == rhs.x && lhs.y == rhs.y;
        }

        [[nodiscard]] auto rect_equal(Rect lhs, Rect rhs) -> bool {
            return vec2_equal(lhs.min, rhs.min) && vec2_equal(lhs.max, rhs.max);
        }

        [[nodiscard]] auto transform_equal(Transform2D lhs, Transform2D rhs) -> bool {
            return vec2_equal(lhs.x_axis, rhs.x_axis) && vec2_equal(lhs.y_axis, rhs.y_axis) &&
                   vec2_equal(lhs.translation, rhs.translation);
        }

        [[nodiscard]] auto
        primitive_commands_compatible(PrimitiveCommand const& lhs, PrimitiveCommand const& rhs)
            -> bool;

        [[nodiscard]] auto segment_count_from_radius(float radius) -> int32_t {
            float const estimate = std::clamp(radius * 0.35f, 12.0f, 64.0f);
            return static_cast<int32_t>(estimate);
        }

        [[nodiscard]] auto clamped_segment_count(float radius, int32_t segment_count) -> int32_t {
            if (segment_count <= 0) {
                segment_count = segment_count_from_radius(radius);
            }

            return std::clamp(segment_count, MIN_SEGMENTS, MAX_SEGMENTS);
        }

        [[nodiscard]] auto
        primitive_commands_compatible(PrimitiveCommand const& lhs, PrimitiveCommand const& rhs)
            -> bool {
            return lhs.texture.handle == rhs.texture.handle &&
                   rect_equal(lhs.clip_rect, rhs.clip_rect) &&
                   transform_equal(lhs.transform, rhs.transform) && lhs.opacity == rhs.opacity;
        }

        auto append_command(ContextImpl* impl, CommandKind kind, size_t index) -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->command_count < impl->order_capacity);

            Command* const command = impl->commands + impl->command_count;
            command->kind = kind;
            command->index = index;
            impl->command_count += 1u;
        }

        auto append_primitive_batch(ContextImpl* impl, size_t command_index) -> void {
            ASSERT(impl != nullptr);
            ASSERT(command_index < impl->primitive_command_count);

            PrimitiveCommand const& command = impl->primitive_commands[command_index];
            if (impl->primitive_batch_count != 0u && impl->command_count != 0u) {
                Command const& previous_command = impl->commands[impl->command_count - 1u];
                PrimitiveBatch* const batch =
                    impl->primitive_batches + (impl->primitive_batch_count - 1u);
                PrimitiveCommand const& first_command =
                    impl->primitive_commands[batch->command_index];
                if (previous_command.kind == CommandKind::PRIMITIVE_BATCH &&
                    previous_command.index == impl->primitive_batch_count - 1u &&
                    primitive_commands_compatible(first_command, command)) {
                    batch->command_count += 1u;
                    batch->vertex_count += command.vertex_count;
                    return;
                }
            }

            ASSERT(impl->primitive_batch_count < impl->command_capacity);

            size_t const batch_index = impl->primitive_batch_count;
            PrimitiveBatch* const batch = impl->primitive_batches + batch_index;
            *batch = {};
            batch->command_index = command_index;
            batch->command_count = 1u;
            batch->vertex_count = command.vertex_count;
            batch->texture = command.texture;
            batch->clip_rect = command.clip_rect;
            batch->transform = command.transform;
            batch->opacity = command.opacity;
            impl->primitive_batch_count += 1u;
            append_command(impl, CommandKind::PRIMITIVE_BATCH, batch_index);
        }

        [[nodiscard]] auto push_primitive_vertices(
            ContextImpl* impl, size_t vertex_count, gui::render::Texture texture = {}
        ) -> Vertex* {
            ASSERT(impl != nullptr);
            ASSERT(vertex_count != 0u);
            ASSERT(impl->primitive_command_count < impl->command_capacity);

            Vertex* const vertices = arena_alloc<Vertex>(impl->frame_arena, vertex_count);
            size_t const command_index = impl->primitive_command_count;
            PrimitiveCommand* const command = impl->primitive_commands + command_index;
            *command = {};
            impl->primitive_infos[command_index] = {};
            command->vertices = vertices;
            command->vertex_count = vertex_count;
            command->texture = texture;
            command->clip_rect = impl->current_clip_rect;
            command->transform = impl->current_transform;
            command->opacity = impl->current_opacity;
            impl->primitive_command_count += 1u;
            append_primitive_batch(impl, command_index);
            return vertices;
        }

        auto mark_analytic_rect(
            ContextImpl* impl,
            size_t command_index,
            Rect rect,
            Transform2D analytic_transform,
            Color fill_color,
            Color border_color,
            float border_thickness,
            float radius
        ) -> void {
            ASSERT(impl != nullptr);
            ASSERT(command_index < impl->primitive_command_count);

            rect = rect_normalized(rect);
            if (!rect_visible(rect) ||
                (!color_visible(fill_color) &&
                 !(border_thickness > 0.0f && color_visible(border_color)))) {
                return;
            }

            float const max_size = std::min(rect_width(rect), rect_height(rect)) * 0.5f;
            primitive_model::PrimitiveInfo* const info = impl->primitive_infos + command_index;
            info->render_kind = primitive_model::RenderKind::ANALYTIC_RECT;
            info->analytic_rect.rect = rect;
            info->analytic_rect.transform = analytic_transform;
            info->analytic_rect.fill_color = fill_color;
            info->analytic_rect.border_color = border_color;
            info->analytic_rect.border_thickness = std::clamp(border_thickness, 0.0f, max_size);
            info->analytic_rect.radius = std::clamp(radius, 0.0f, max_size);
            info->analytic_rect.softness = 1.0f;
        }

        auto mark_analytic_rect(
            ContextImpl* impl,
            size_t command_index,
            Rect rect,
            Color fill_color,
            Color border_color,
            float border_thickness,
            float radius
        ) -> void {
            mark_analytic_rect(
                impl,
                command_index,
                rect,
                impl->current_transform,
                fill_color,
                border_color,
                border_thickness,
                radius
            );
        }

        auto mark_analytic_line(
            ContextImpl* impl, size_t command_index, Vec2 p0, Vec2 p1, Color color, float thickness
        ) -> void {
            ASSERT(impl != nullptr);
            if (thickness <= 0.0f || !color_visible(color) || vec2_equal(p0, p1)) {
                return;
            }

            float const dx = p1.x - p0.x;
            float const dy = p1.y - p0.y;
            float const length = std::sqrt((dx * dx) + (dy * dy));
            if (length <= 0.0f) {
                return;
            }

            float const inverse_length = 1.0f / length;
            Transform2D const line_transform = {
                {dx * inverse_length, dy * inverse_length},
                {-dy * inverse_length, dx * inverse_length},
                p0
            };
            float const half_thickness = std::max(thickness, 1.0f) * 0.5f;
            Color line_color = color;
            line_color.a *= std::min(thickness, 1.0f);
            mark_analytic_rect(
                impl,
                command_index,
                {{0.0f, -half_thickness}, {length, half_thickness}},
                compose_transform(impl->current_transform, line_transform),
                line_color,
                TRANSPARENT,
                0.0f,
                0.0f
            );
        }

        auto mark_coverage_mask(ContextImpl* impl, size_t command_index, size_t shape_command_index)
            -> void {
            ASSERT(impl != nullptr);
            ASSERT(command_index < impl->primitive_command_count);
            if (shape_command_index == INVALID_SHAPE_COMMAND_INDEX ||
                shape_command_index >= impl->shape_command_count) {
                return;
            }

            path_model::ShapeCommand const& shape_command =
                impl->shape_commands[shape_command_index];
            coverage_mask::ShapeKey key = {};
            if (!coverage_mask::shape_key(shape_command.shape, key)) {
                return;
            }

            primitive_model::PrimitiveInfo* const info = impl->primitive_infos + command_index;
            info->render_kind = primitive_model::RenderKind::COVERAGE_MASK;
            info->coverage_mask_shape_index = shape_command_index;
        }

        auto push_styled_rect(ContextImpl* impl, Rect rect, BoxStyle style) -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->styled_rect_command_count < impl->command_capacity);

            float const max_size = std::min(rect_width(rect), rect_height(rect)) * 0.5f;
            style.border_thickness = std::clamp(style.border_thickness, 0.0f, max_size);
            style.radius = std::clamp(style.radius, 0.0f, max_size);
            style.softness = std::max(style.softness, 0.0f);
            style.shadow.blur_radius = std::max(style.shadow.blur_radius, 0.0f);

            size_t const command_index = impl->styled_rect_command_count;
            StyledRectCommand* const command = impl->styled_rect_commands + command_index;
            *command = {};
            command->rect = rect;
            command->style = style;
            command->clip_rect = impl->current_clip_rect;
            command->transform = impl->current_transform;
            command->opacity = impl->current_opacity;
            impl->styled_rect_command_count += 1u;
            append_command(impl, CommandKind::STYLED_RECT, command_index);
        }

        auto
        write_vertex(ContextImpl const* impl, Vertex& vertex, Vec2 position, Vec2 uv, Color color)
            -> void {
            vertex = {};
            vertex.position = transform_point(impl->current_transform, position);
            vertex.uv = uv;
            color.a *= impl->current_opacity;
            vertex.color = color;
        }

        auto write_vertex(ContextImpl const* impl, Vertex& vertex, Vec2 position, Color color)
            -> void {
            write_vertex(impl, vertex, position, {}, color);
        }

        auto write_triangle(
            ContextImpl const* impl, Vertex* vertices, Vec2 p0, Vec2 p1, Vec2 p2, Color color
        ) -> void {
            write_vertex(impl, vertices[0u], p0, color);
            write_vertex(impl, vertices[1u], p1, color);
            write_vertex(impl, vertices[2u], p2, color);
        }

        auto write_rect_vertices(
            ContextImpl const* impl, Vertex* vertices, Rect rect, Rect uv_rect, Color color
        ) -> void {
            Vec2 const p0 = rect.min;
            Vec2 const p1 = {rect.max.x, rect.min.y};
            Vec2 const p2 = rect.max;
            Vec2 const p3 = {rect.min.x, rect.max.y};
            Vec2 const uv0 = uv_rect.min;
            Vec2 const uv1 = {uv_rect.max.x, uv_rect.min.y};
            Vec2 const uv2 = uv_rect.max;
            Vec2 const uv3 = {uv_rect.min.x, uv_rect.max.y};
            write_vertex(impl, vertices[0u], p0, uv0, color);
            write_vertex(impl, vertices[1u], p1, uv1, color);
            write_vertex(impl, vertices[2u], p2, uv2, color);
            write_vertex(impl, vertices[3u], p0, uv0, color);
            write_vertex(impl, vertices[4u], p2, uv2, color);
            write_vertex(impl, vertices[5u], p3, uv3, color);
        }

        [[nodiscard]] auto points_equal(Vec2 lhs, Vec2 rhs) -> bool {
            return vec2_equal(lhs, rhs);
        }

        [[nodiscard]] auto segment_normal(Vec2 p0, Vec2 p1) -> Vec2 {
            float const dx = p1.x - p0.x;
            float const dy = p1.y - p0.y;
            float const length = std::sqrt((dx * dx) + (dy * dy));
            ASSERT(length > 0.0f);
            return {-dy / length, dx / length};
        }

        [[nodiscard]] auto tangent_from_normal(Vec2 normal) -> Vec2 {
            return {normal.y, -normal.x};
        }

        [[nodiscard]] auto miter_join_offset(
            Vec2 prev_normal, Vec2 next_normal, float half_thickness, float miter_limit
        ) -> Vec2 {
            Vec2 const average = vec2_mul(vec2_add(prev_normal, next_normal), 0.5f);
            float const length_sqr = (average.x * average.x) + (average.y * average.y);
            if (length_sqr <= 0.000001f) {
                return vec2_mul(next_normal, half_thickness);
            }

            float const inverse_length_sqr = std::min(1.0f / length_sqr, miter_limit * miter_limit);
            return vec2_mul(average, inverse_length_sqr * half_thickness);
        }

        [[nodiscard]] auto
        stroke_join_offset(Vec2 prev_normal, Vec2 next_normal, float half_thickness) -> Vec2 {
            return miter_join_offset(
                prev_normal, next_normal, half_thickness, DEFAULT_STROKE_MITER_LIMIT
            );
        }

        auto write_stroke_segment(
            ContextImpl const* impl,
            Vertex* vertices,
            Vec2 p0,
            Vec2 p1,
            Vec2 offset0,
            Vec2 offset1,
            Color color
        ) -> void {
            Vec2 const a = vec2_add(p0, offset0);
            Vec2 const b = vec2_add(p1, offset1);
            Vec2 const c = vec2_sub(p1, offset1);
            Vec2 const d = vec2_sub(p0, offset0);

            write_triangle(impl, vertices, a, b, c, color);
            write_triangle(impl, vertices + 3u, a, c, d, color);
        }

        auto write_fringe_segment(
            ContextImpl const* impl,
            Vertex* vertices,
            Vec2 p0,
            Vec2 p1,
            Vec2 offset0,
            Vec2 offset1,
            Color inner_color,
            Color outer_color
        ) -> void {
            Vec2 const a = p0;
            Vec2 const b = p1;
            Vec2 const c = vec2_add(p1, offset1);
            Vec2 const d = vec2_add(p0, offset0);

            write_vertex(impl, vertices[0u], a, inner_color);
            write_vertex(impl, vertices[1u], b, inner_color);
            write_vertex(impl, vertices[2u], c, outer_color);
            write_vertex(impl, vertices[3u], a, inner_color);
            write_vertex(impl, vertices[4u], c, outer_color);
            write_vertex(impl, vertices[5u], d, outer_color);
        }

        auto write_stroke_cap(
            ContextImpl const* impl,
            Vertex* vertices,
            Vec2 point,
            Vec2 inner_offset,
            Vec2 outer_offset,
            Vec2 cap_offset,
            Color inner_color,
            Color outer_color
        ) -> void {
            Vec2 const p0 = vec2_sub(point, inner_offset);
            Vec2 const p1 = vec2_add(point, inner_offset);
            Vec2 const offset0 = vec2_add(vec2_sub(inner_offset, outer_offset), cap_offset);
            Vec2 const offset1 = vec2_add(vec2_sub(outer_offset, inner_offset), cap_offset);
            write_fringe_segment(
                impl, vertices, p0, p1, offset0, offset1, inner_color, outer_color
            );
        }

        [[nodiscard]] auto default_stroke_style(float thickness, bool closed) -> StrokeStyle {
            StrokeStyle style = {};
            style.thickness = thickness;
            style.half_width = std::max(thickness, 1.0f) * 0.5f;
            style.outer_half_width = style.half_width + AA_FRINGE_SIZE;
            style.closed = closed;
            return style;
        }

        auto
        compact_points(ContextImpl* impl, Slice<Vec2 const> points, bool closed, size_t& out_count)
            -> Vec2 const* {
            bool needs_compaction = closed && points.size() > 1u &&
                                    points_equal(points[0u], points[points.size() - 1u]);
            for (size_t index = 1u; index < points.size(); ++index) {
                if (points_equal(points[index - 1u], points[index])) {
                    needs_compaction = true;
                    break;
                }
            }

            if (!needs_compaction) {
                out_count = points.size();
                return points.data();
            }

            Vec2* const stroke_points = arena_alloc<Vec2>(impl->frame_arena, points.size());
            size_t count = 0u;
            for (size_t index = 0u; index < points.size(); ++index) {
                if (count == 0u || !points_equal(stroke_points[count - 1u], points[index])) {
                    stroke_points[count] = points[index];
                    count += 1u;
                }
            }

            if (closed && count > 1u &&
                points_equal(stroke_points[0u], stroke_points[count - 1u])) {
                count -= 1u;
            }

            out_count = count;
            return stroke_points;
        }

        [[nodiscard]] auto polygon_area_twice(Slice<Vec2 const> points) -> float {
            float result = 0.0f;
            for (size_t index = 0u; index < points.size(); ++index) {
                Vec2 const p0 = points[index];
                Vec2 const p1 = points[(index + 1u) % points.size()];
                result += (p0.x * p1.y) - (p0.y * p1.x);
            }
            return result;
        }

        [[nodiscard]] auto triangle_cross(Vec2 p0, Vec2 p1, Vec2 p2) -> float {
            return ((p1.x - p0.x) * (p2.y - p0.y)) - ((p1.y - p0.y) * (p2.x - p0.x));
        }

        [[nodiscard]] auto convex_turn(float turn, float area_twice) -> bool {
            return area_twice > 0.0f ? turn > TRIANGULATION_EPSILON : turn < -TRIANGULATION_EPSILON;
        }

        [[nodiscard]] auto
        point_in_triangle(Vec2 point, Vec2 p0, Vec2 p1, Vec2 p2, float area_twice) -> bool {
            float const c0 = triangle_cross(p0, p1, point);
            float const c1 = triangle_cross(p1, p2, point);
            float const c2 = triangle_cross(p2, p0, point);
            return area_twice > 0.0f
                       ? c0 >= -TRIANGULATION_EPSILON && c1 >= -TRIANGULATION_EPSILON &&
                             c2 >= -TRIANGULATION_EPSILON
                       : c0 <= TRIANGULATION_EPSILON && c1 <= TRIANGULATION_EPSILON &&
                             c2 <= TRIANGULATION_EPSILON;
        }

        [[nodiscard]] auto polygon_convex(Slice<Vec2 const> points, float area_twice) -> bool {
            for (size_t index = 0u; index < points.size(); ++index) {
                float const turn = triangle_cross(
                    points[index],
                    points[(index + 1u) % points.size()],
                    points[(index + 2u) % points.size()]
                );
                if (std::abs(turn) > TRIANGULATION_EPSILON && !convex_turn(turn, area_twice)) {
                    return false;
                }
            }
            return true;
        }

        auto write_fill_fringe(
            ContextImpl* impl,
            Vertex* fringe_vertices,
            Slice<Vec2 const> points,
            Color color,
            float area_twice
        ) -> void {
            float const normal_scale = area_twice > 0.0f ? -1.0f : 1.0f;
            Vec2* const fringe_offsets = arena_alloc<Vec2>(impl->frame_arena, points.size());
            for (size_t index = 0u; index < points.size(); ++index) {
                size_t const prev_index = (index + points.size() - 1u) % points.size();
                size_t const next_index = (index + 1u) % points.size();
                Vec2 const prev_normal =
                    vec2_mul(segment_normal(points[prev_index], points[index]), normal_scale);
                Vec2 const next_normal =
                    vec2_mul(segment_normal(points[index], points[next_index]), normal_scale);
                fringe_offsets[index] =
                    stroke_join_offset(prev_normal, next_normal, AA_FRINGE_SIZE);
            }

            Color outer_color = color;
            outer_color.a = 0.0f;
            for (size_t index = 0u; index < points.size(); ++index) {
                size_t const next_index = (index + 1u) % points.size();
                write_fringe_segment(
                    impl,
                    fringe_vertices + (index * 6u),
                    points[index],
                    points[next_index],
                    fringe_offsets[index],
                    fringe_offsets[next_index],
                    color,
                    outer_color
                );
            }
        }

        [[nodiscard]] auto triangulate_simple_polygon(
            ContextImpl* impl, Slice<Vec2 const> points, float area_twice, size_t* out_indices
        ) -> bool {
            size_t* const remaining = arena_alloc<size_t>(impl->frame_arena, points.size());
            for (size_t index = 0u; index < points.size(); ++index) {
                remaining[index] = index;
            }

            size_t remaining_count = points.size();
            size_t index_count = 0u;
            while (remaining_count > 3u) {
                bool clipped = false;
                for (size_t index = 0u; index < remaining_count; ++index) {
                    size_t const prev_index =
                        remaining[(index + remaining_count - 1u) % remaining_count];
                    size_t const curr_index = remaining[index];
                    size_t const next_index = remaining[(index + 1u) % remaining_count];
                    Vec2 const p0 = points[prev_index];
                    Vec2 const p1 = points[curr_index];
                    Vec2 const p2 = points[next_index];
                    if (!convex_turn(triangle_cross(p0, p1, p2), area_twice)) {
                        continue;
                    }

                    bool contains_point = false;
                    for (size_t test = 0u; test < remaining_count; ++test) {
                        size_t const point_index = remaining[test];
                        if (point_index == prev_index || point_index == curr_index ||
                            point_index == next_index) {
                            continue;
                        }
                        if (point_in_triangle(points[point_index], p0, p1, p2, area_twice)) {
                            contains_point = true;
                            break;
                        }
                    }
                    if (contains_point) {
                        continue;
                    }

                    out_indices[index_count++] = prev_index;
                    out_indices[index_count++] = curr_index;
                    out_indices[index_count++] = next_index;
                    for (size_t move = index; move + 1u < remaining_count; ++move) {
                        remaining[move] = remaining[move + 1u];
                    }
                    remaining_count -= 1u;
                    clipped = true;
                    break;
                }

                if (!clipped) {
                    return false;
                }
            }

            out_indices[index_count++] = remaining[0u];
            out_indices[index_count++] = remaining[1u];
            out_indices[index_count++] = remaining[2u];
            return index_count == (points.size() - 2u) * 3u;
        }

        auto fill_convex_points(ContextImpl* impl, Slice<Vec2 const> points, Color color) -> void {
            if (points.size() < 3u || !color_visible(color)) {
                return;
            }

            size_t point_count = 0u;
            points = {compact_points(impl, points, true, point_count), point_count};
            if (points.size() < 3u) {
                return;
            }

            float const area_twice = polygon_area_twice(points);
            if (area_twice == 0.0f) {
                return;
            }

            size_t const fill_vertex_count = (points.size() - 2u) * 3u;
            size_t const vertex_count = fill_vertex_count + (points.size() * 6u);
            Vertex* const vertices = push_primitive_vertices(impl, vertex_count);
            for (size_t index = 1u; index + 1u < points.size(); ++index) {
                write_triangle(
                    impl,
                    vertices + ((index - 1u) * 3u),
                    points[0u],
                    points[index],
                    points[index + 1u],
                    color
                );
            }
            write_fill_fringe(impl, vertices + fill_vertex_count, points, color, area_twice);
        }

        auto fill_path_points(ContextImpl* impl, Slice<Vec2 const> points, Color color) -> void {
            if (points.size() < 3u || !color_visible(color)) {
                return;
            }

            size_t point_count = 0u;
            points = {compact_points(impl, points, true, point_count), point_count};
            if (points.size() < 3u) {
                return;
            }

            float const area_twice = polygon_area_twice(points);
            if (area_twice == 0.0f) {
                return;
            }

            if (polygon_convex(points, area_twice)) {
                fill_convex_points(impl, points, color);
                return;
            }

            size_t const fill_index_count = (points.size() - 2u) * 3u;
            size_t* const indices = arena_alloc<size_t>(impl->frame_arena, fill_index_count);
            if (!triangulate_simple_polygon(impl, points, area_twice, indices)) {
                fill_convex_points(impl, points, color);
                return;
            }

            size_t const vertex_count = fill_index_count + (points.size() * 6u);
            Vertex* const vertices = push_primitive_vertices(impl, vertex_count);
            for (size_t index = 0u; index < fill_index_count; index += 3u) {
                write_triangle(
                    impl,
                    vertices + index,
                    points[indices[index]],
                    points[indices[index + 1u]],
                    points[indices[index + 2u]],
                    color
                );
            }
            write_fill_fringe(impl, vertices + fill_index_count, points, color, area_twice);
        }

        auto copy_path_points(ContextImpl* impl) -> Slice<Vec2 const> {
            ASSERT(impl != nullptr);
            return path_model::flattened_points(impl->path);
        }

        auto clear_path(ContextImpl* impl) -> void {
            ASSERT(impl != nullptr);
            path_model::clear(impl->path);
        }

        auto append_path_point(ContextImpl* impl, Vec2 point) -> void {
            ASSERT(impl != nullptr);
            path_model::line_to(impl->path, point);
        }

        auto push_shape_command(ContextImpl* impl, path_model::Shape shape, Color color) -> size_t {
            ASSERT(impl != nullptr);
            if (shape.kind == path_model::ShapeKind::EMPTY) {
                return INVALID_SHAPE_COMMAND_INDEX;
            }

            ASSERT(impl->shape_command_count < impl->command_capacity);
            size_t const command_index = impl->shape_command_count;
            path_model::ShapeCommand* const command = impl->shape_commands + command_index;
            *command = {};
            command->shape = shape;
            command->color = color;
            command->clip_rect = impl->current_clip_rect;
            command->transform = impl->current_transform;
            command->opacity = impl->current_opacity;
            impl->shape_command_count += 1u;
            return command_index;
        }

        auto emit_stroke_contour(
            ContextImpl* impl, Slice<Vec2 const> points, Color color, StrokeStyle style
        ) -> void {
            ASSERT(impl != nullptr);

            size_t point_count = 0u;
            Vec2 const* const stroke_points =
                compact_points(impl, points, style.closed, point_count);
            if (point_count < 2u || (style.closed && point_count < 3u)) {
                return;
            }

            size_t const segment_count = style.closed ? point_count : point_count - 1u;
            Vec2* const segment_normals = arena_alloc<Vec2>(impl->frame_arena, segment_count);
            for (size_t index = 0u; index < segment_count; ++index) {
                segment_normals[index] =
                    segment_normal(stroke_points[index], stroke_points[(index + 1u) % point_count]);
            }

            Color inner_color = color;
            inner_color.a *= std::min(style.thickness, 1.0f);
            Color outer_color = inner_color;
            outer_color.a = 0.0f;

            Vec2* const point_offsets = arena_alloc<Vec2>(impl->frame_arena, point_count);
            Vec2* const outer_point_offsets = arena_alloc<Vec2>(impl->frame_arena, point_count);
            if (style.closed) {
                for (size_t index = 0u; index < point_count; ++index) {
                    size_t const prev_index = (index + segment_count - 1u) % segment_count;
                    point_offsets[index] = miter_join_offset(
                        segment_normals[prev_index],
                        segment_normals[index],
                        style.half_width,
                        style.miter_limit
                    );
                    outer_point_offsets[index] = miter_join_offset(
                        segment_normals[prev_index],
                        segment_normals[index],
                        style.outer_half_width,
                        style.miter_limit
                    );
                }
            } else {
                point_offsets[0u] = vec2_mul(segment_normals[0u], style.half_width);
                outer_point_offsets[0u] = vec2_mul(segment_normals[0u], style.outer_half_width);
                point_offsets[point_count - 1u] =
                    vec2_mul(segment_normals[segment_count - 1u], style.half_width);
                outer_point_offsets[point_count - 1u] =
                    vec2_mul(segment_normals[segment_count - 1u], style.outer_half_width);
                for (size_t index = 1u; index + 1u < point_count; ++index) {
                    point_offsets[index] = miter_join_offset(
                        segment_normals[index - 1u],
                        segment_normals[index],
                        style.half_width,
                        style.miter_limit
                    );
                    outer_point_offsets[index] = miter_join_offset(
                        segment_normals[index - 1u],
                        segment_normals[index],
                        style.outer_half_width,
                        style.miter_limit
                    );
                }
            }

            size_t const core_vertex_count = segment_count * 6u;
            size_t const side_fringe_vertex_count = segment_count * 12u;
            size_t const cap_vertex_count = style.closed ? 0u : 12u;
            Vertex* const vertices = push_primitive_vertices(
                impl, core_vertex_count + side_fringe_vertex_count + cap_vertex_count
            );
            for (size_t index = 0u; index < segment_count; ++index) {
                size_t const next_index = (index + 1u) % point_count;
                write_stroke_segment(
                    impl,
                    vertices + (index * 6u),
                    stroke_points[index],
                    stroke_points[next_index],
                    point_offsets[index],
                    point_offsets[next_index],
                    inner_color
                );
            }

            Vertex* const fringe_vertices = vertices + core_vertex_count;
            for (size_t index = 0u; index < segment_count; ++index) {
                size_t const next_index = (index + 1u) % point_count;
                Vec2 const positive_delta0 =
                    vec2_sub(outer_point_offsets[index], point_offsets[index]);
                Vec2 const positive_delta1 =
                    vec2_sub(outer_point_offsets[next_index], point_offsets[next_index]);
                Vec2 const negative_delta0 =
                    vec2_sub(point_offsets[index], outer_point_offsets[index]);
                Vec2 const negative_delta1 =
                    vec2_sub(point_offsets[next_index], outer_point_offsets[next_index]);
                write_fringe_segment(
                    impl,
                    fringe_vertices + (index * 12u),
                    vec2_add(stroke_points[index], point_offsets[index]),
                    vec2_add(stroke_points[next_index], point_offsets[next_index]),
                    positive_delta0,
                    positive_delta1,
                    inner_color,
                    outer_color
                );
                write_fringe_segment(
                    impl,
                    fringe_vertices + (index * 12u) + 6u,
                    vec2_sub(stroke_points[index], point_offsets[index]),
                    vec2_sub(stroke_points[next_index], point_offsets[next_index]),
                    negative_delta0,
                    negative_delta1,
                    inner_color,
                    outer_color
                );
            }

            if (!style.closed) {
                Vec2 const start_tangent = tangent_from_normal(segment_normals[0u]);
                Vec2 const end_tangent = tangent_from_normal(segment_normals[segment_count - 1u]);
                Vertex* const cap_vertices = fringe_vertices + side_fringe_vertex_count;
                write_stroke_cap(
                    impl,
                    cap_vertices,
                    stroke_points[0u],
                    point_offsets[0u],
                    outer_point_offsets[0u],
                    vec2_mul(start_tangent, -AA_FRINGE_SIZE),
                    inner_color,
                    outer_color
                );
                write_stroke_cap(
                    impl,
                    cap_vertices + 6u,
                    stroke_points[point_count - 1u],
                    point_offsets[point_count - 1u],
                    outer_point_offsets[point_count - 1u],
                    vec2_mul(end_tangent, AA_FRINGE_SIZE),
                    inner_color,
                    outer_color
                );
            }
        }

        auto
        append_ellipse_points(ContextImpl* impl, Vec2 center, Vec2 radius, int32_t segment_count)
            -> Slice<Vec2 const> {
            float const max_radius = std::max(radius.x, radius.y);
            int32_t const segments = clamped_segment_count(max_radius, segment_count);
            Vec2* const points =
                arena_alloc<Vec2>(impl->frame_arena, static_cast<size_t>(segments));

            for (int32_t index = 0; index < segments; ++index) {
                float const angle =
                    TWO_PI * static_cast<float>(index) / static_cast<float>(segments);
                points[index] = {
                    center.x + (std::cos(angle) * radius.x), center.y + (std::sin(angle) * radius.y)
                };
            }

            return {points, static_cast<size_t>(segments)};
        }

        auto copy_frame_text(Arena& arena, StrRef text, StrRef& out_text) -> void {
            if (text.empty()) {
                out_text = {};
                return;
            }

            char* const data = arena_alloc<char>(arena, text.size());
            std::memcpy(data, text.data(), text.size());
            out_text = StrRef(data, text.size());
        }

    } // namespace

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void {
        ASSERT(out_context.handle == nullptr);
        ASSERT(desc.initial_command_capacity != 0u);
        ASSERT(desc.frame_arena_reserve_size != 0u);
        ASSERT(desc.frame_arena_commit_size != 0u);

        ContextImpl* const impl = arena_new<ContextImpl>(arena);

        ArenaOptions const arena_options = {
            desc.frame_arena_reserve_size, desc.frame_arena_commit_size
        };
        impl->frame_arena.init(arena_options);

        impl->primitive_commands =
            arena_alloc<PrimitiveCommand>(arena, desc.initial_command_capacity);
        impl->primitive_infos =
            arena_alloc<primitive_model::PrimitiveInfo>(arena, desc.initial_command_capacity);
        impl->primitive_batches = arena_alloc<PrimitiveBatch>(arena, desc.initial_command_capacity);
        impl->commands = arena_alloc<Command>(arena, desc.initial_command_capacity * 5u);
        impl->layer_commands = arena_alloc<LayerCommand>(arena, desc.initial_command_capacity);
        impl->styled_rect_commands =
            arena_alloc<StyledRectCommand>(arena, desc.initial_command_capacity);
        impl->text_commands = arena_alloc<TextCommand>(arena, desc.initial_command_capacity);
        impl->shape_commands =
            arena_alloc<path_model::ShapeCommand>(arena, desc.initial_command_capacity);
        impl->command_capacity = desc.initial_command_capacity;
        impl->order_capacity = desc.initial_command_capacity * 5u;
        impl->font_cache = desc.font_cache;
        path_model::init(impl->path, impl->frame_arena.resource());
        reset_state(impl);
        out_context.handle = impl;
    }

    auto destroy_context(Context& context) -> void {
        ASSERT(context.handle != nullptr);

        ContextImpl* const impl = context_from_handle(context);
        impl->primitive_commands = nullptr;
        impl->primitive_infos = nullptr;
        impl->primitive_batches = nullptr;
        impl->commands = nullptr;
        impl->layer_commands = nullptr;
        impl->styled_rect_commands = nullptr;
        impl->text_commands = nullptr;
        impl->shape_commands = nullptr;
        impl->primitive_command_count = 0u;
        impl->primitive_batch_count = 0u;
        impl->command_count = 0u;
        impl->layer_command_count = 0u;
        impl->styled_rect_command_count = 0u;
        impl->text_command_count = 0u;
        impl->shape_command_count = 0u;
        clear_path(impl);
        reset_state(impl);
        impl->command_capacity = 0u;
        impl->order_capacity = 0u;
        impl->frame_arena.destroy();
        context.handle = nullptr;
    }

    auto begin_frame(Context context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        impl->frame_arena.reset();
        impl->primitive_command_count = 0u;
        impl->primitive_batch_count = 0u;
        impl->command_count = 0u;
        impl->layer_command_count = 0u;
        impl->styled_rect_command_count = 0u;
        impl->text_command_count = 0u;
        impl->shape_command_count = 0u;
        path_model::init(impl->path, impl->frame_arena.resource());
        clear_path(impl);
        reset_state(impl);
    }

    auto end_frame(Context) -> void {}

    auto push_clip_rect(Context context, Rect rect) -> Rect {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Rect const previous = impl->current_clip_rect;
        ClipStackNode* const node = arena_new<ClipStackNode>(impl->frame_arena);
        node->next = impl->clip_stack_top;
        node->value = previous;
        impl->clip_stack_top = node;
        impl->current_clip_rect = rect_intersect(previous, rect_normalized(rect));
        return previous;
    }

    auto pop_clip_rect(Context context) -> Rect {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Rect const popped = impl->current_clip_rect;
        if (impl->clip_stack_top != nullptr) {
            impl->current_clip_rect = impl->clip_stack_top->value;
            impl->clip_stack_top = impl->clip_stack_top->next;
        }
        return popped;
    }

    auto top_clip_rect(Context context) -> Rect {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->current_clip_rect : default_clip_rect();
    }

    auto push_transform(Context context, Transform2D transform) -> Transform2D {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Transform2D const previous = impl->current_transform;
        TransformStackNode* const node = arena_new<TransformStackNode>(impl->frame_arena);
        node->next = impl->transform_stack_top;
        node->value = previous;
        impl->transform_stack_top = node;
        impl->current_transform = transform;
        return previous;
    }

    auto pop_transform(Context context) -> Transform2D {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Transform2D const popped = impl->current_transform;
        if (impl->transform_stack_top != nullptr) {
            impl->current_transform = impl->transform_stack_top->value;
            impl->transform_stack_top = impl->transform_stack_top->next;
        }
        return popped;
    }

    auto top_transform(Context context) -> Transform2D {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->current_transform : Transform2D{};
    }

    auto push_opacity(Context context, float opacity) -> float {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        float const previous = impl->current_opacity;
        OpacityStackNode* const node = arena_new<OpacityStackNode>(impl->frame_arena);
        node->next = impl->opacity_stack_top;
        node->value = previous;
        impl->opacity_stack_top = node;
        impl->current_opacity = opacity;
        return previous;
    }

    auto pop_opacity(Context context) -> float {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        float const popped = impl->current_opacity;
        if (impl->opacity_stack_top != nullptr) {
            impl->current_opacity = impl->opacity_stack_top->value;
            impl->opacity_stack_top = impl->opacity_stack_top->next;
        }
        return popped;
    }

    auto top_opacity(Context context) -> float {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->current_opacity : 1.0f;
    }

    auto push_layer(Context context, LayerDesc desc) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(impl->layer_command_count < impl->command_capacity);

        desc.bounds = rect_normalized(desc.bounds);
        float const max_radius =
            std::max(std::min(rect_width(desc.bounds), rect_height(desc.bounds)) * 0.5f, 0.0f);
        desc.clip_radius = std::clamp(desc.clip_radius, 0.0f, max_radius);
        desc.opacity = std::clamp(desc.opacity, 0.0f, 1.0f);
        desc.isolated = true;
        desc.filter_radius = std::max(desc.filter_radius, 0.0f);
        desc.drop_shadow.blur_radius = std::max(desc.drop_shadow.blur_radius, 0.0f);

        Rect const previous_clip_rect = impl->current_clip_rect;
        Rect const layer_clip_rect = rect_intersect(previous_clip_rect, desc.bounds);
        size_t const layer_index = impl->layer_command_count;
        LayerCommand* const command = impl->layer_commands + layer_index;
        *command = {};
        command->desc = desc;
        command->clip_rect = layer_clip_rect;
        command->begin_command_index = impl->command_count;
        command->end_command_index = impl->command_count;
        impl->layer_command_count += 1u;
        append_command(impl, CommandKind::LAYER_BEGIN, layer_index);

        LayerStackNode* const node = arena_new<LayerStackNode>(impl->frame_arena);
        node->next = impl->layer_stack_top;
        node->index = layer_index;
        node->previous_clip_rect = previous_clip_rect;
        impl->layer_stack_top = node;
        impl->current_clip_rect = layer_clip_rect;
    }

    auto pop_layer(Context context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(impl->layer_stack_top != nullptr);
        if (impl->layer_stack_top == nullptr) {
            return;
        }

        LayerStackNode* const node = impl->layer_stack_top;
        LayerCommand* const command = impl->layer_commands + node->index;
        command->end_command_index = impl->command_count;
        append_command(impl, CommandKind::LAYER_END, node->index);
        impl->current_clip_rect = node->previous_clip_rect;
        impl->layer_stack_top = node->next;
    }

    auto draw_line(Context context, Vec2 p0, Vec2 p1, Color color, float thickness) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (thickness > 0.0f && color_visible(color) && !vec2_equal(p0, p1)) {
            size_t const command_index = impl->primitive_command_count;
            push_shape_command(
                impl, path_model::line_shape(p0, p1, thickness, impl->frame_arena.resource()), color
            );
            draw_polyline(context, {p0, p1}, color, thickness, false);
            if (impl->primitive_command_count > command_index) {
                mark_analytic_line(impl, command_index, p0, p1, color, thickness);
            }
        }
    }

    auto draw_polyline(
        Context context, Slice<Vec2 const> points, Color color, float thickness, bool closed
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (points.size() < 2u || thickness <= 0.0f || !color_visible(color)) {
            return;
        }

        emit_stroke_contour(impl, points, color, default_stroke_style(thickness, closed));
    }

    auto draw_triangle(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Color color, float thickness)
        -> void {
        draw_polyline(context, {p0, p1, p2}, color, thickness, true);
    }

    auto draw_triangle_filled(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Color color) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        fill_convex_points(impl, {p0, p1, p2}, color);
    }

    auto
    draw_quad(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color color, float thickness)
        -> void {
        draw_polyline(context, {p0, p1, p2, p3}, color, thickness, true);
    }

    auto draw_quad_filled(Context context, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color color)
        -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (p0.y == p1.y && p1.x == p2.x && p2.y == p3.y && p3.x == p0.x) {
            Rect const rect = rect_normalized({p0, p2});
            if (!rect_visible(rect) || !color_visible(color)) {
                return;
            }
            push_shape_command(
                impl, path_model::rect_shape(rect, impl->frame_arena.resource()), color
            );
            size_t const command_index = impl->primitive_command_count;
            fill_convex_points(impl, {p0, p1, p2, p3}, color);
            if (impl->primitive_command_count > command_index) {
                mark_analytic_rect(impl, command_index, rect, color, TRANSPARENT, 0.0f, 0.0f);
            }
            return;
        }

        fill_path_points(impl, {p0, p1, p2, p3}, color);
    }

    auto draw_rect(Context context, Rect rect, Color color, float thickness, float rounding)
        -> void {
        path_clear(context);
        path_rect(context, rect, rounding);
        path_stroke(context, color, true, thickness);
    }

    auto draw_rect_filled(Context context, Rect rect, Color color, float rounding) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        rect = rect_normalized(rect);
        if (!rect_visible(rect) || !color_visible(color)) {
            return;
        }

        if (rounding < 0.5f) {
            push_shape_command(
                impl, path_model::rect_shape(rect, impl->frame_arena.resource()), color
            );
            size_t const command_index = impl->primitive_command_count;
            fill_convex_points(
                impl,
                {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}},
                color
            );
            if (impl->primitive_command_count > command_index) {
                mark_analytic_rect(impl, command_index, rect, color, TRANSPARENT, 0.0f, 0.0f);
            }
            return;
        }

        path_clear(context);
        path_rect(context, rect, rounding);
        path_fill_convex(context, color);
    }

    auto draw_rect_styled(Context context, Rect rect, BoxStyle style) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        rect = rect_normalized(rect);
        if (!rect_visible(rect) || !box_style_visible(style)) {
            return;
        }

        push_styled_rect(impl, rect, style);
    }

    auto
    draw_image(Context context, gui::render::Texture texture, Rect rect, Rect uv_rect, Color color)
        -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        rect = rect_normalized(rect);
        if (!gui::render::texture_valid(texture) || !rect_visible(rect) || !color_visible(color)) {
            return;
        }

        Vertex* const vertices = push_primitive_vertices(impl, 6u, texture);
        write_rect_vertices(impl, vertices, rect, uv_rect, color);
    }

    auto draw_rect_filled_multicolor(
        Context context,
        Rect rect,
        Color top_left,
        Color top_right,
        Color bottom_right,
        Color bottom_left
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        rect = rect_normalized(rect);
        if (!rect_visible(rect) || (!color_visible(top_left) && !color_visible(top_right) &&
                                    !color_visible(bottom_right) && !color_visible(bottom_left))) {
            return;
        }

        Vertex* const vertices = push_primitive_vertices(impl, 6u);
        Vec2 const p0 = rect.min;
        Vec2 const p1 = {rect.max.x, rect.min.y};
        Vec2 const p2 = rect.max;
        Vec2 const p3 = {rect.min.x, rect.max.y};
        write_vertex(impl, vertices[0u], p0, top_left);
        write_vertex(impl, vertices[1u], p1, top_right);
        write_vertex(impl, vertices[2u], p2, bottom_right);
        write_vertex(impl, vertices[3u], p0, top_left);
        write_vertex(impl, vertices[4u], p2, bottom_right);
        write_vertex(impl, vertices[5u], p3, bottom_left);
    }

    auto draw_circle(
        Context context,
        Vec2 center,
        float radius,
        Color color,
        float thickness,
        int32_t segment_count
    ) -> void {
        draw_ellipse(context, center, {radius, radius}, color, thickness, segment_count);
    }

    auto draw_circle_filled(
        Context context, Vec2 center, float radius, Color color, int32_t segment_count
    ) -> void {
        draw_ellipse_filled(context, center, {radius, radius}, color, segment_count);
    }

    auto draw_ellipse(
        Context context,
        Vec2 center,
        Vec2 radius,
        Color color,
        float thickness,
        int32_t segment_count
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (radius.x < 0.5f || radius.y < 0.5f || thickness <= 0.0f || !color_visible(color)) {
            return;
        }

        Slice<Vec2 const> points = append_ellipse_points(impl, center, radius, segment_count);
        draw_polyline(context, points, color, thickness, true);
    }

    auto draw_ellipse_filled(
        Context context, Vec2 center, Vec2 radius, Color color, int32_t segment_count
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (radius.x < 0.5f || radius.y < 0.5f || !color_visible(color)) {
            return;
        }

        size_t const shape_command_index = push_shape_command(
            impl, path_model::oval_shape(center, radius, impl->frame_arena.resource()), color
        );
        size_t const command_index = impl->primitive_command_count;
        Slice<Vec2 const> points = append_ellipse_points(impl, center, radius, segment_count);
        fill_path_points(impl, points, color);
        if (impl->primitive_command_count > command_index) {
            mark_coverage_mask(impl, command_index, shape_command_index);
        }
    }

    auto path_clear(Context context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        clear_path(impl);
    }

    auto path_line_to(Context context, Vec2 p) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        append_path_point(impl, p);
    }

    auto path_arc_to(
        Context context,
        Vec2 center,
        float radius,
        float angle_min,
        float angle_max,
        int32_t segment_count
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (radius < 0.5f) {
            append_path_point(impl, center);
            return;
        }

        path_model::arc_to(impl->path, center, radius, angle_min, angle_max, segment_count);
    }

    auto path_bezier_quadratic_to(Context context, Vec2 control, Vec2 end, int32_t segment_count)
        -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(impl->path.has_current);
        path_model::quad_to(impl->path, control, end, segment_count);
    }

    auto path_bezier_cubic_to(
        Context context, Vec2 control0, Vec2 control1, Vec2 end, int32_t segment_count
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(impl->path.has_current);
        path_model::cubic_to(impl->path, control0, control1, end, segment_count);
    }

    auto path_rect(Context context, Rect rect, float rounding) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        path_model::rect(impl->path, rect, rounding);
    }

    auto path_stroke(Context context, Color color, bool closed, float thickness) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Slice<Vec2 const> const points = copy_path_points(impl);
        path_model::Shape shape = {};
        if (color_visible(color) && thickness > 0.0f) {
            shape = path_model::stroke_shape(
                impl->path, closed, thickness, impl->frame_arena.resource()
            );
            push_shape_command(impl, shape, color);
        }
        size_t const command_index = impl->primitive_command_count;
        draw_polyline(context, points, color, thickness, closed);
        if (impl->primitive_command_count > command_index) {
            if (shape.kind == path_model::ShapeKind::RECT ||
                shape.kind == path_model::ShapeKind::RRECT) {
                float const half_thickness = thickness * 0.5f;
                mark_analytic_rect(
                    impl,
                    command_index,
                    shape.bounds,
                    TRANSPARENT,
                    color,
                    thickness,
                    shape.radius + half_thickness
                );
            } else if (shape.kind == path_model::ShapeKind::LINE &&
                       shape.path.flat_points.size() >= 2u) {
                mark_analytic_line(
                    impl,
                    command_index,
                    shape.path.flat_points[0u],
                    shape.path.flat_points[1u],
                    color,
                    thickness
                );
            }
        }
        clear_path(impl);
    }

    auto path_fill_convex(Context context, Color color) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        Slice<Vec2 const> const points = copy_path_points(impl);
        path_model::Shape shape = {};
        size_t shape_command_index = INVALID_SHAPE_COMMAND_INDEX;
        if (color_visible(color)) {
            shape = path_model::fill_shape(impl->path, impl->frame_arena.resource());
            shape_command_index = push_shape_command(impl, shape, color);
        }
        size_t const command_index = impl->primitive_command_count;
        fill_path_points(impl, points, color);
        if (impl->primitive_command_count > command_index &&
            (shape.kind == path_model::ShapeKind::RECT ||
             shape.kind == path_model::ShapeKind::RRECT)) {
            mark_analytic_rect(
                impl, command_index, shape.bounds, color, TRANSPARENT, 0.0f, shape.radius
            );
        } else if (impl->primitive_command_count > command_index) {
            mark_coverage_mask(impl, command_index, shape_command_index);
        }
        clear_path(impl);
    }

    auto
    measure_text(Context context, TextStyle const& style, StrRef text, font_cache::TextRun& out_run)
        -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(font_cache::cache_valid(impl->font_cache));

        font_cache::text_run(impl->font_cache, style.font, style.size, text, out_run);
    }

    auto draw_text(
        Context context, Vec2 position, TextStyle const& style, StrRef text, float* out_advance
    ) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        font_cache::TextRun run = {};
        measure_text(context, style, text, run);

        ASSERT(impl->text_command_count < impl->command_capacity);

        StrRef frame_text = {};
        copy_frame_text(impl->frame_arena, text, frame_text);

        size_t const command_index = impl->text_command_count;
        TextCommand* const command = impl->text_commands + command_index;
        *command = {};
        command->position = position;
        command->style = style;
        command->text = frame_text;
        command->run = run;
        command->clip_rect = impl->current_clip_rect;
        command->transform = impl->current_transform;
        command->opacity = impl->current_opacity;
        impl->text_command_count += 1u;
        append_command(impl, CommandKind::TEXT, command_index);

        if (out_advance != nullptr) {
            *out_advance = run.advance;
        }
    }

    auto primitive_command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->primitive_command_count : 0u;
    }

    auto primitive_command(Context context, size_t index) -> PrimitiveCommand const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->primitive_command_count) {
            return nullptr;
        }

        return impl->primitive_commands + index;
    }

    auto primitive_batch_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->primitive_batch_count : 0u;
    }

    auto primitive_batch(Context context, size_t index) -> PrimitiveBatch const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->primitive_batch_count) {
            return nullptr;
        }

        return impl->primitive_batches + index;
    }

    auto command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->command_count : 0u;
    }

    auto command(Context context, size_t index) -> Command const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->command_count) {
            return nullptr;
        }

        return impl->commands + index;
    }

    auto layer_command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->layer_command_count : 0u;
    }

    auto layer_command(Context context, size_t index) -> LayerCommand const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->layer_command_count) {
            return nullptr;
        }

        return impl->layer_commands + index;
    }

    auto styled_rect_command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->styled_rect_command_count : 0u;
    }

    auto styled_rect_command(Context context, size_t index) -> StyledRectCommand const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->styled_rect_command_count) {
            return nullptr;
        }

        return impl->styled_rect_commands + index;
    }

    auto text_command_count(Context context) -> size_t {
        ContextImpl const* const impl = context_from_handle(context);
        return impl != nullptr ? impl->text_command_count : 0u;
    }

    auto text_command(Context context, size_t index) -> TextCommand const* {
        ContextImpl const* const impl = context_from_handle(context);
        if (impl == nullptr || index >= impl->text_command_count) {
            return nullptr;
        }

        return impl->text_commands + index;
    }

    namespace path_model {

        auto shape_command_count(Context context) -> size_t {
            ContextImpl const* const impl = context_from_handle(context);
            return impl != nullptr ? impl->shape_command_count : 0u;
        }

        auto shape_command(Context context, size_t index) -> ShapeCommand const* {
            ContextImpl const* const impl = context_from_handle(context);
            if (impl == nullptr || index >= impl->shape_command_count) {
                return nullptr;
            }

            return impl->shape_commands + index;
        }

    } // namespace path_model

    namespace primitive_model {

        auto primitive_info(Context context, size_t index) -> PrimitiveInfo const* {
            ContextImpl const* const impl = context_from_handle(context);
            if (impl == nullptr || index >= impl->primitive_command_count) {
                return nullptr;
            }

            return impl->primitive_infos + index;
        }

    } // namespace primitive_model

} // namespace gui::draw
