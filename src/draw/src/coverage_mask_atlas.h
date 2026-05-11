#pragma once

#include "draw_path.h"

#include <cstddef>
#include <cstdint>
#include <render/render.h>

namespace gui::draw::coverage_mask {

    struct ShapeKey {
        uint64_t point_hash = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t point_count = 0u;
        path_model::ShapeKind kind = path_model::ShapeKind::EMPTY;
        path_model::Convexity convexity = path_model::Convexity::UNKNOWN;
        uint8_t segment_mask = path_model::PATH_SEGMENT_NONE;
    };

    struct Raster {
        uint8_t* pixels = nullptr;
        gui::render::SizeU32 size = {};
        uint32_t bytes_per_row = 0u;
        uint32_t content_x = 0u;
        uint32_t content_y = 0u;
        uint32_t content_width = 0u;
        uint32_t content_height = 0u;
    };

    struct Atlas {
        void* handle = nullptr;
    };

    struct AtlasEntry {
        gui::render::Texture texture = {};
        float uv_rect[4] = {};
        float uv_clamp[4] = {};
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    [[nodiscard]] auto atlas_valid(Atlas atlas) -> bool;
    [[nodiscard]] auto shape_key(path_model::Shape const& shape, ShapeKey& out_key) -> bool;
    [[nodiscard]] auto shape_key_equal(ShapeKey lhs, ShapeKey rhs) -> bool;
    [[nodiscard]] auto rasterize(Arena& arena, path_model::Shape const& shape, Raster& out_raster)
        -> bool;

    [[nodiscard]] auto create_atlas(
        Arena& arena, gui::render::Context render_context, size_t slot_count, Atlas& out_atlas
    ) -> gui::render::Result;
    auto clear_atlas(gui::render::Context render_context, Atlas atlas) -> void;
    auto destroy_atlas(gui::render::Context render_context, Atlas& atlas) -> void;
    [[nodiscard]] auto atlas_entry_count(Atlas atlas) -> size_t;
    [[nodiscard]] auto ensure_entry(
        Arena& upload_arena,
        Atlas atlas,
        gui::render::Context render_context,
        path_model::Shape const& shape,
        AtlasEntry& out_entry
    ) -> bool;

} // namespace gui::draw::coverage_mask
