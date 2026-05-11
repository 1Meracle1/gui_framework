#pragma once

#include <base/memory.h>
#include <base/slice.h>
#include <base/vec.h>
#include <cstddef>
#include <cstdint>
#include <draw/draw.h>

namespace gui::draw::path_model {

    enum class PathVerb : uint8_t {
        MOVE,
        LINE,
        QUAD,
        CUBIC,
        CLOSE,
    };

    enum class ShapeKind : uint8_t {
        EMPTY,
        LINE,
        RECT,
        RRECT,
        OVAL,
        POLYGON,
        GENERAL_PATH,
    };

    enum class ShapeOp : uint8_t {
        FILL,
        STROKE,
    };

    enum class Convexity : uint8_t {
        UNKNOWN,
        CONVEX,
        CONCAVE,
    };

    enum PathSegmentMask : uint8_t {
        PATH_SEGMENT_NONE = 0u,
        PATH_SEGMENT_LINE = 1u << 0u,
        PATH_SEGMENT_QUAD = 1u << 1u,
        PATH_SEGMENT_CUBIC = 1u << 2u,
    };

    struct VerbRecord {
        PathVerb verb = PathVerb::MOVE;
        size_t point_index = 0u;
        size_t point_count = 0u;
    };

    struct Path {
        Vec<VerbRecord> verbs = {};
        Vec<Vec2> points = {};
        Vec<Vec2> flat_points = {};
        Rect bounds = {};
        Vec2 contour_start = {};
        Vec2 current = {};
        ShapeKind hint_kind = ShapeKind::EMPTY;
        Convexity convexity = Convexity::UNKNOWN;
        uint8_t segment_mask = PATH_SEGMENT_NONE;
        float hint_radius = 0.0f;
        size_t hint_verb_count = 0u;
        size_t hint_point_count = 0u;
        size_t hint_flat_point_count = 0u;
        bool has_bounds = false;
        bool has_current = false;
        bool closed = false;
    };

    struct Shape {
        ShapeKind kind = ShapeKind::EMPTY;
        ShapeOp op = ShapeOp::FILL;
        Path path = {};
        Rect bounds = {};
        Convexity convexity = Convexity::UNKNOWN;
        uint8_t segment_mask = PATH_SEGMENT_NONE;
        float radius = 0.0f;
        float stroke_thickness = 0.0f;
        bool closed = false;
    };

    struct ShapeCommand {
        Shape shape = {};
        Color color = {};
        Rect clip_rect = {};
        Transform2D transform = {};
        float opacity = 1.0f;
    };

    auto init(Path& path, MemoryResource* resource) -> void;
    auto clear(Path& path) -> void;
    auto copy(Path const& path, MemoryResource* resource) -> Path;

    auto move_to(Path& path, Vec2 point) -> void;
    auto line_to(Path& path, Vec2 point) -> void;
    auto arc_to(
        Path& path,
        Vec2 center,
        float radius,
        float angle_min,
        float angle_max,
        int32_t segment_count
    ) -> void;
    auto quad_to(Path& path, Vec2 control, Vec2 end, int32_t segment_count) -> void;
    auto cubic_to(Path& path, Vec2 control0, Vec2 control1, Vec2 end, int32_t segment_count)
        -> void;
    auto close(Path& path) -> void;
    auto rect(Path& path, Rect rect, float rounding) -> void;

    [[nodiscard]] auto flattened_points(Path const& path) -> Slice<Vec2 const>;

    [[nodiscard]] auto line_shape(Vec2 p0, Vec2 p1, float thickness, MemoryResource* resource)
        -> Shape;
    [[nodiscard]] auto rect_shape(Rect rect, MemoryResource* resource) -> Shape;
    [[nodiscard]] auto rrect_shape(Rect rect, float radius, MemoryResource* resource) -> Shape;
    [[nodiscard]] auto oval_shape(Vec2 center, Vec2 radius, MemoryResource* resource) -> Shape;
    [[nodiscard]] auto polygon_shape(Slice<Vec2 const> points, MemoryResource* resource) -> Shape;
    [[nodiscard]] auto fill_shape(Path const& path, MemoryResource* resource) -> Shape;
    [[nodiscard]] auto
    stroke_shape(Path const& path, bool closed, float thickness, MemoryResource* resource) -> Shape;

    [[nodiscard]] auto shape_command_count(Context context) -> size_t;
    [[nodiscard]] auto shape_command(Context context, size_t index) -> ShapeCommand const*;

} // namespace gui::draw::path_model
