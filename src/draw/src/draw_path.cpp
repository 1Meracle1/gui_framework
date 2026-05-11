#include "draw_path.h"

#include <algorithm>
#include <base/assert.h>
#include <cmath>

namespace gui::draw::path_model {
    namespace {

        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 2.0f * PI;
        constexpr float HALF_PI = 0.5f * PI;
        constexpr float CUBIC_OVAL_KAPPA = 0.5522847498307936f;
        constexpr int32_t MAX_SEGMENTS = 128;
        constexpr int32_t DEFAULT_CURVE_SEGMENTS = 12;

        [[nodiscard]] auto vec2_equal(Vec2 lhs, Vec2 rhs) -> bool {
            return lhs.x == rhs.x && lhs.y == rhs.y;
        }

        [[nodiscard]] auto vec2_add(Vec2 lhs, Vec2 rhs) -> Vec2 {
            return {lhs.x + rhs.x, lhs.y + rhs.y};
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

        [[nodiscard]] auto rect_width(Rect rect) -> float {
            return rect.max.x - rect.min.x;
        }

        [[nodiscard]] auto rect_height(Rect rect) -> float {
            return rect.max.y - rect.min.y;
        }

        [[nodiscard]] auto rect_visible(Rect rect) -> bool {
            return rect_width(rect) > 0.0f && rect_height(rect) > 0.0f;
        }

        [[nodiscard]] auto rect_inset(Rect rect, float amount) -> Rect {
            return {
                {rect.min.x - amount, rect.min.y - amount},
                {rect.max.x + amount, rect.max.y + amount}
            };
        }

        auto include_point(Path& path, Vec2 point) -> void {
            if (!path.has_bounds) {
                path.bounds = {point, point};
                path.has_bounds = true;
                return;
            }

            path.bounds.min.x = std::min(path.bounds.min.x, point.x);
            path.bounds.min.y = std::min(path.bounds.min.y, point.y);
            path.bounds.max.x = std::max(path.bounds.max.x, point.x);
            path.bounds.max.y = std::max(path.bounds.max.y, point.y);
        }

        auto push_verb(Path& path, PathVerb verb, Slice<Vec2 const> points) -> void {
            VerbRecord record = {};
            record.verb = verb;
            record.point_index = path.points.size();
            record.point_count = points.size();
            ASSERT(path.verbs.push_back(record));
            ASSERT(path.points.append(points) == points.size());
        }

        auto push_flat_point(Path& path, Vec2 point) -> void {
            ASSERT(path.flat_points.push_back(point));
        }

        auto set_hint(Path& path, ShapeKind kind, float radius) -> void {
            path.hint_kind = kind;
            path.hint_radius = radius;
            path.hint_verb_count = path.verbs.size();
            path.hint_point_count = path.points.size();
            path.hint_flat_point_count = path.flat_points.size();
        }

        [[nodiscard]] auto valid_hint(Path const& path) -> bool {
            return path.hint_kind != ShapeKind::EMPTY &&
                   path.hint_verb_count == path.verbs.size() &&
                   path.hint_point_count == path.points.size() &&
                   path.hint_flat_point_count == path.flat_points.size();
        }

        [[nodiscard]] auto segment_count_from_radius(float radius) -> int32_t {
            float const estimate = std::clamp(radius * 0.35f, 12.0f, 64.0f);
            return static_cast<int32_t>(estimate);
        }

        [[nodiscard]] auto
        clamped_arc_segment_count(float radius, float angle_delta, int32_t segment_count)
            -> int32_t {
            if (segment_count > 0) {
                return std::clamp(segment_count, 1, MAX_SEGMENTS);
            }

            float const fraction = std::abs(angle_delta) / TWO_PI;
            int32_t const full_count = segment_count_from_radius(radius);
            int32_t const result = static_cast<int32_t>(std::max(1.0f, full_count * fraction));
            return std::clamp(result, 1, MAX_SEGMENTS);
        }

        [[nodiscard]] auto clamped_curve_segment_count(int32_t segment_count) -> int32_t {
            return segment_count > 0 ? std::clamp(segment_count, 1, MAX_SEGMENTS)
                                     : DEFAULT_CURVE_SEGMENTS;
        }

        [[nodiscard]] auto compact_count(Slice<Vec2 const> points, bool closed) -> size_t {
            size_t count = 0u;
            Vec2 previous = {};
            for (size_t index = 0u; index < points.size(); ++index) {
                if (count == 0u || !vec2_equal(previous, points[index])) {
                    previous = points[index];
                    count += 1u;
                }
            }

            if (closed && count > 1u && vec2_equal(points.front(), previous)) {
                count -= 1u;
            }
            return count;
        }

        [[nodiscard]] auto polygon_convexity(Slice<Vec2 const> points) -> Convexity {
            if (points.size() < 4u) {
                return points.size() >= 3u ? Convexity::CONVEX : Convexity::UNKNOWN;
            }

            float sign = 0.0f;
            for (size_t index = 0u; index < points.size(); ++index) {
                Vec2 const p0 = points[index];
                Vec2 const p1 = points[(index + 1u) % points.size()];
                Vec2 const p2 = points[(index + 2u) % points.size()];
                float const cross =
                    ((p1.x - p0.x) * (p2.y - p1.y)) - ((p1.y - p0.y) * (p2.x - p1.x));
                if (cross == 0.0f) {
                    continue;
                }
                if (sign == 0.0f) {
                    sign = cross;
                } else if ((sign < 0.0f) != (cross < 0.0f)) {
                    return Convexity::CONCAVE;
                }
            }
            return sign == 0.0f ? Convexity::UNKNOWN : Convexity::CONVEX;
        }

        [[nodiscard]] auto path_is_line(Path const& path, bool closed) -> bool {
            return !closed && path.verbs.size() == 2u && path.verbs[0u].verb == PathVerb::MOVE &&
                   path.verbs[1u].verb == PathVerb::LINE;
        }

        [[nodiscard]] auto path_uses_only_lines(Path const& path) -> bool {
            return (path.segment_mask & (PATH_SEGMENT_QUAD | PATH_SEGMENT_CUBIC)) == 0u;
        }

        [[nodiscard]] auto path_is_axis_rect(Path const& path, bool closed) -> bool {
            if (!closed || path.flat_points.size() != 4u || !path_uses_only_lines(path)) {
                return false;
            }

            Rect const rect = path.bounds;
            Vec2 const expected[] = {
                rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}
            };
            Vec2 const reverse[] = {
                rect.min, {rect.min.x, rect.max.y}, rect.max, {rect.max.x, rect.min.y}
            };
            bool forward = true;
            bool backward = true;
            for (size_t index = 0u; index < 4u; ++index) {
                forward = forward && vec2_equal(path.flat_points[index], expected[index]);
                backward = backward && vec2_equal(path.flat_points[index], reverse[index]);
            }
            return forward || backward;
        }

        [[nodiscard]] auto classify_kind(Path const& path, ShapeOp op, bool closed) -> ShapeKind {
            if (path.flat_points.empty()) {
                return ShapeKind::EMPTY;
            }
            if (valid_hint(path)) {
                return path.hint_kind;
            }
            if (op == ShapeOp::STROKE && path_is_line(path, closed)) {
                return ShapeKind::LINE;
            }
            if (path_is_axis_rect(path, closed)) {
                return ShapeKind::RECT;
            }
            if (path_uses_only_lines(path) && closed) {
                return ShapeKind::POLYGON;
            }
            return ShapeKind::GENERAL_PATH;
        }

        [[nodiscard]] auto shape_from_path(
            Path const& path, ShapeOp op, bool closed, float thickness, MemoryResource* resource
        ) -> Shape {
            size_t const needed_points = op == ShapeOp::FILL ? 3u : 2u;
            if (!path.has_bounds ||
                compact_count(path.flat_points.slice(), closed) < needed_points) {
                return {};
            }

            Shape shape = {};
            shape.path = copy(path, resource);
            shape.op = op;
            shape.kind = classify_kind(path, op, closed);
            shape.bounds = path.bounds;
            shape.convexity =
                valid_hint(path) && path.convexity != Convexity::UNKNOWN
                    ? path.convexity
                    : (path.segment_mask == PATH_SEGMENT_LINE || path_uses_only_lines(path)
                           ? polygon_convexity(path.flat_points.slice())
                           : Convexity::UNKNOWN);
            shape.segment_mask = path.segment_mask;
            shape.radius = valid_hint(path) ? path.hint_radius : 0.0f;
            shape.stroke_thickness = thickness;
            shape.closed = closed;

            if (op == ShapeOp::STROKE) {
                shape.bounds = rect_inset(shape.bounds, thickness * 0.5f);
                shape.convexity = Convexity::UNKNOWN;
            }
            return shape;
        }

    } // namespace

    auto init(Path& path, MemoryResource* resource) -> void {
        ASSERT(resource != nullptr);
        path = {};
        ASSERT(path.verbs.init(0u, resource));
        ASSERT(path.points.init(0u, resource));
        ASSERT(path.flat_points.init(0u, resource));
    }

    auto clear(Path& path) -> void {
        path.verbs.clear();
        path.points.clear();
        path.flat_points.clear();
        path.bounds = {};
        path.contour_start = {};
        path.current = {};
        path.hint_kind = ShapeKind::EMPTY;
        path.convexity = Convexity::UNKNOWN;
        path.segment_mask = PATH_SEGMENT_NONE;
        path.hint_radius = 0.0f;
        path.hint_verb_count = 0u;
        path.hint_point_count = 0u;
        path.hint_flat_point_count = 0u;
        path.has_bounds = false;
        path.has_current = false;
        path.closed = false;
    }

    auto copy(Path const& path, MemoryResource* resource) -> Path {
        Path result = {};
        init(result, resource);
        ASSERT(result.verbs.append(path.verbs.slice()) == path.verbs.size());
        ASSERT(result.points.append(path.points.slice()) == path.points.size());
        ASSERT(result.flat_points.append(path.flat_points.slice()) == path.flat_points.size());
        result.bounds = path.bounds;
        result.contour_start = path.contour_start;
        result.current = path.current;
        result.hint_kind = path.hint_kind;
        result.convexity = path.convexity;
        result.segment_mask = path.segment_mask;
        result.hint_radius = path.hint_radius;
        result.hint_verb_count = path.hint_verb_count;
        result.hint_point_count = path.hint_point_count;
        result.hint_flat_point_count = path.hint_flat_point_count;
        result.has_bounds = path.has_bounds;
        result.has_current = path.has_current;
        result.closed = path.closed;
        return result;
    }

    auto move_to(Path& path, Vec2 point) -> void {
        push_verb(path, PathVerb::MOVE, {&point, 1u});
        push_flat_point(path, point);
        include_point(path, point);
        path.contour_start = point;
        path.current = point;
        path.has_current = true;
        path.closed = false;
    }

    auto line_to(Path& path, Vec2 point) -> void {
        if (!path.has_current) {
            move_to(path, point);
            return;
        }

        push_verb(path, PathVerb::LINE, {&point, 1u});
        push_flat_point(path, point);
        include_point(path, point);
        path.current = point;
        path.segment_mask |= PATH_SEGMENT_LINE;
    }

    auto arc_to(
        Path& path,
        Vec2 center,
        float radius,
        float angle_min,
        float angle_max,
        int32_t segment_count
    ) -> void {
        if (radius < 0.5f) {
            line_to(path, center);
            return;
        }

        int32_t const segments =
            clamped_arc_segment_count(radius, angle_max - angle_min, segment_count);
        float const step = (angle_max - angle_min) / static_cast<float>(segments);
        for (int32_t index = 0; index <= segments; ++index) {
            float const angle = angle_min + (step * static_cast<float>(index));
            line_to(
                path, {center.x + (std::cos(angle) * radius), center.y + (std::sin(angle) * radius)}
            );
        }
    }

    auto quad_to(Path& path, Vec2 control, Vec2 end, int32_t segment_count) -> void {
        ASSERT(path.has_current);

        Vec2 const points[] = {control, end};
        push_verb(path, PathVerb::QUAD, points);
        include_point(path, control);
        include_point(path, end);

        Vec2 const start = path.current;
        int32_t const segments = clamped_curve_segment_count(segment_count);
        for (int32_t index = 1; index <= segments; ++index) {
            float const t = static_cast<float>(index) / static_cast<float>(segments);
            float const u = 1.0f - t;
            Vec2 const point = vec2_add(
                vec2_add(vec2_mul(start, u * u), vec2_mul(control, 2.0f * u * t)),
                vec2_mul(end, t * t)
            );
            push_flat_point(path, point);
        }

        path.current = end;
        path.segment_mask |= PATH_SEGMENT_QUAD;
    }

    auto cubic_to(Path& path, Vec2 control0, Vec2 control1, Vec2 end, int32_t segment_count)
        -> void {
        ASSERT(path.has_current);

        Vec2 const points[] = {control0, control1, end};
        push_verb(path, PathVerb::CUBIC, points);
        include_point(path, control0);
        include_point(path, control1);
        include_point(path, end);

        Vec2 const start = path.current;
        int32_t const segments = clamped_curve_segment_count(segment_count);
        for (int32_t index = 1; index <= segments; ++index) {
            float const t = static_cast<float>(index) / static_cast<float>(segments);
            float const u = 1.0f - t;
            Vec2 const point = vec2_add(
                vec2_add(vec2_mul(start, u * u * u), vec2_mul(control0, 3.0f * u * u * t)),
                vec2_add(vec2_mul(control1, 3.0f * u * t * t), vec2_mul(end, t * t * t))
            );
            push_flat_point(path, point);
        }

        path.current = end;
        path.segment_mask |= PATH_SEGMENT_CUBIC;
    }

    auto close(Path& path) -> void {
        if (!path.has_current || path.closed) {
            return;
        }

        push_verb(path, PathVerb::CLOSE, {});
        path.current = path.contour_start;
        path.closed = true;
    }

    auto rect(Path& path, Rect rect_value, float rounding) -> void {
        bool const empty_before = path.verbs.empty();
        rect_value = rect_normalized(rect_value);
        if (!rect_visible(rect_value)) {
            return;
        }

        float const max_rounding = std::min(rect_width(rect_value), rect_height(rect_value)) * 0.5f;
        rounding = std::clamp(rounding, 0.0f, max_rounding);
        if (rounding < 0.5f) {
            move_to(path, rect_value.min);
            line_to(path, {rect_value.max.x, rect_value.min.y});
            line_to(path, rect_value.max);
            line_to(path, {rect_value.min.x, rect_value.max.y});
            close(path);
            if (empty_before) {
                set_hint(path, ShapeKind::RECT, 0.0f);
                path.convexity = Convexity::CONVEX;
            }
            return;
        }

        arc_to(
            path,
            {rect_value.max.x - rounding, rect_value.min.y + rounding},
            rounding,
            -HALF_PI,
            0.0f,
            0
        );
        arc_to(
            path,
            {rect_value.max.x - rounding, rect_value.max.y - rounding},
            rounding,
            0.0f,
            HALF_PI,
            0
        );
        arc_to(
            path,
            {rect_value.min.x + rounding, rect_value.max.y - rounding},
            rounding,
            HALF_PI,
            PI,
            0
        );
        arc_to(
            path,
            {rect_value.min.x + rounding, rect_value.min.y + rounding},
            rounding,
            PI,
            PI + HALF_PI,
            0
        );
        close(path);
        if (empty_before) {
            set_hint(path, ShapeKind::RRECT, rounding);
            path.convexity = Convexity::CONVEX;
        }
    }

    auto flattened_points(Path const& path) -> Slice<Vec2 const> {
        return path.flat_points.slice();
    }

    auto line_shape(Vec2 p0, Vec2 p1, float thickness, MemoryResource* resource) -> Shape {
        Path path = {};
        init(path, resource);
        move_to(path, p0);
        line_to(path, p1);
        return stroke_shape(path, false, thickness, resource);
    }

    auto rect_shape(Rect rect_value, MemoryResource* resource) -> Shape {
        Path path = {};
        init(path, resource);
        rect(path, rect_value, 0.0f);
        return fill_shape(path, resource);
    }

    auto rrect_shape(Rect rect_value, float radius, MemoryResource* resource) -> Shape {
        Path path = {};
        init(path, resource);
        rect(path, rect_value, radius);
        return fill_shape(path, resource);
    }

    auto oval_shape(Vec2 center, Vec2 radius, MemoryResource* resource) -> Shape {
        if (radius.x <= 0.0f || radius.y <= 0.0f) {
            return {};
        }

        Path path = {};
        init(path, resource);
        move_to(path, {center.x + radius.x, center.y});
        cubic_to(
            path,
            {center.x + radius.x, center.y + (radius.y * CUBIC_OVAL_KAPPA)},
            {center.x + (radius.x * CUBIC_OVAL_KAPPA), center.y + radius.y},
            {center.x, center.y + radius.y},
            DEFAULT_CURVE_SEGMENTS
        );
        cubic_to(
            path,
            {center.x - (radius.x * CUBIC_OVAL_KAPPA), center.y + radius.y},
            {center.x - radius.x, center.y + (radius.y * CUBIC_OVAL_KAPPA)},
            {center.x - radius.x, center.y},
            DEFAULT_CURVE_SEGMENTS
        );
        cubic_to(
            path,
            {center.x - radius.x, center.y - (radius.y * CUBIC_OVAL_KAPPA)},
            {center.x - (radius.x * CUBIC_OVAL_KAPPA), center.y - radius.y},
            {center.x, center.y - radius.y},
            DEFAULT_CURVE_SEGMENTS
        );
        cubic_to(
            path,
            {center.x + (radius.x * CUBIC_OVAL_KAPPA), center.y - radius.y},
            {center.x + radius.x, center.y - (radius.y * CUBIC_OVAL_KAPPA)},
            {center.x + radius.x, center.y},
            DEFAULT_CURVE_SEGMENTS
        );
        close(path);
        set_hint(path, ShapeKind::OVAL, 0.0f);
        path.convexity = Convexity::CONVEX;
        return fill_shape(path, resource);
    }

    auto polygon_shape(Slice<Vec2 const> points, MemoryResource* resource) -> Shape {
        if (points.size() < 3u) {
            return {};
        }

        Path path = {};
        init(path, resource);
        move_to(path, points[0u]);
        for (size_t index = 1u; index < points.size(); ++index) {
            line_to(path, points[index]);
        }
        close(path);
        set_hint(path, ShapeKind::POLYGON, 0.0f);
        path.convexity = polygon_convexity(points);
        return fill_shape(path, resource);
    }

    auto fill_shape(Path const& path, MemoryResource* resource) -> Shape {
        Shape shape = shape_from_path(path, ShapeOp::FILL, true, 0.0f, resource);
        if (shape.kind == ShapeKind::LINE) {
            shape.kind = ShapeKind::POLYGON;
        }
        return shape;
    }

    auto stroke_shape(Path const& path, bool closed, float thickness, MemoryResource* resource)
        -> Shape {
        if (thickness <= 0.0f) {
            return {};
        }
        return shape_from_path(path, ShapeOp::STROKE, closed, thickness, resource);
    }

} // namespace gui::draw::path_model
