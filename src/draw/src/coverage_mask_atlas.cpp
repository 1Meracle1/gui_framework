#include "coverage_mask_atlas.h"

#include <algorithm>
#include <base/assert.h>
#include <base/memory.h>
#include <cmath>
#include <cstring>

namespace gui::draw::coverage_mask {
    namespace {

        constexpr gui::render::SizeU32 ATLAS_SIZE = {512u, 512u};
        constexpr uint32_t ATLAS_PADDING = 1u;
        constexpr uint32_t MASK_SUBSAMPLES = 4u;
        constexpr uint32_t MAX_MASK_SIZE = 64u;
        constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
        constexpr uint64_t FNV_PRIME = 1099511628211ull;

        struct Page {
            Page* next = nullptr;
            gui::render::Texture texture = {};
            uint32_t x = 0u;
            uint32_t y = 0u;
            uint32_t row_height = 0u;
        };

        struct Entry {
            Entry* next = nullptr;
            ShapeKey key = {};
            Page* page = nullptr;
            float uv_rect[4] = {};
            float uv_clamp[4] = {};
            uint32_t width = 0u;
            uint32_t height = 0u;
        };

        struct AtlasImpl {
            Arena* arena = nullptr;
            Page* first_page = nullptr;
            Page* last_page = nullptr;
            Entry** slots = nullptr;
            size_t slot_count = 0u;
            size_t entry_count = 0u;
        };

        [[nodiscard]] auto atlas_from_handle(Atlas atlas) -> AtlasImpl* {
            return static_cast<AtlasImpl*>(atlas.handle);
        }

        [[nodiscard]] auto rect_width(Rect rect) -> float {
            return rect.max.x - rect.min.x;
        }

        [[nodiscard]] auto rect_height(Rect rect) -> float {
            return rect.max.y - rect.min.y;
        }

        [[nodiscard]] auto hash_u64(uint64_t hash, uint64_t value) -> uint64_t {
            hash ^= value;
            hash *= FNV_PRIME;
            return hash;
        }

        [[nodiscard]] auto quantize(float value) -> uint32_t {
            int32_t const scaled = static_cast<int32_t>(std::round(value * 256.0f));
            return static_cast<uint32_t>(scaled);
        }

        [[nodiscard]] auto
        mask_size(path_model::Shape const& shape, uint32_t& width, uint32_t& height) -> bool {
            float const shape_width = rect_width(shape.bounds);
            float const shape_height = rect_height(shape.bounds);
            if (shape_width <= 0.0f || shape_height <= 0.0f) {
                return false;
            }

            width = static_cast<uint32_t>(std::ceil(shape_width));
            height = static_cast<uint32_t>(std::ceil(shape_height));
            return width != 0u && height != 0u && width <= MAX_MASK_SIZE && height <= MAX_MASK_SIZE;
        }

        [[nodiscard]] auto cacheable_shape(path_model::Shape const& shape) -> bool {
            if (shape.op != path_model::ShapeOp::FILL) {
                return false;
            }
            if (shape.kind == path_model::ShapeKind::OVAL) {
                return true;
            }
            return shape.kind == path_model::ShapeKind::GENERAL_PATH &&
                   (shape.segment_mask &
                    (path_model::PATH_SEGMENT_QUAD | path_model::PATH_SEGMENT_CUBIC)) != 0u &&
                   shape.path.flat_points.size() >= 3u;
        }

        [[nodiscard]] auto hash_shape_key(ShapeKey key) -> size_t {
            uint64_t hash = FNV_OFFSET;
            hash = hash_u64(hash, key.point_hash);
            hash = hash_u64(hash, key.width);
            hash = hash_u64(hash, key.height);
            hash = hash_u64(hash, key.point_count);
            hash = hash_u64(hash, static_cast<uint8_t>(key.kind));
            hash = hash_u64(hash, static_cast<uint8_t>(key.convexity));
            hash = hash_u64(hash, key.segment_mask);
            return static_cast<size_t>(hash);
        }

        [[nodiscard]] auto find_entry(AtlasImpl& atlas, ShapeKey key) -> Entry* {
            if (atlas.slots == nullptr || atlas.slot_count == 0u) {
                return nullptr;
            }

            size_t const slot_index = hash_shape_key(key) % atlas.slot_count;
            for (Entry* entry = atlas.slots[slot_index]; entry != nullptr; entry = entry->next) {
                if (shape_key_equal(entry->key, key)) {
                    return entry;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto point_in_oval(path_model::Shape const& shape, Vec2 point) -> bool {
            float const width = rect_width(shape.bounds);
            float const height = rect_height(shape.bounds);
            float const rx = width * 0.5f;
            float const ry = height * 0.5f;
            if (rx <= 0.0f || ry <= 0.0f) {
                return false;
            }

            Vec2 const center = {
                (shape.bounds.min.x + shape.bounds.max.x) * 0.5f,
                (shape.bounds.min.y + shape.bounds.max.y) * 0.5f
            };
            float const x = (point.x - center.x) / rx;
            float const y = (point.y - center.y) / ry;
            return (x * x) + (y * y) <= 1.0f;
        }

        [[nodiscard]] auto point_in_path(path_model::Shape const& shape, Vec2 point) -> bool {
            Slice<Vec2 const> const points = shape.path.flat_points.slice();
            bool inside = false;
            size_t previous = points.size() - 1u;
            for (size_t index = 0u; index < points.size(); ++index) {
                Vec2 const a = points[index];
                Vec2 const b = points[previous];
                if ((a.y > point.y) != (b.y > point.y)) {
                    float const t = (point.y - a.y) / (b.y - a.y);
                    if (point.x < a.x + ((b.x - a.x) * t)) {
                        inside = !inside;
                    }
                }
                previous = index;
            }
            return inside;
        }

        [[nodiscard]] auto point_in_shape(path_model::Shape const& shape, Vec2 point) -> bool {
            if (shape.kind == path_model::ShapeKind::OVAL) {
                return point_in_oval(shape, point);
            }
            return point_in_path(shape, point);
        }

        [[nodiscard]] auto coverage_at(
            path_model::Shape const& shape, uint32_t x, uint32_t y, uint32_t width, uint32_t height
        ) -> uint8_t {
            float const shape_width = rect_width(shape.bounds);
            float const shape_height = rect_height(shape.bounds);
            uint32_t covered = 0u;
            for (uint32_t sample_y = 0u; sample_y < MASK_SUBSAMPLES; ++sample_y) {
                for (uint32_t sample_x = 0u; sample_x < MASK_SUBSAMPLES; ++sample_x) {
                    Vec2 const point = {
                        shape.bounds.min.x +
                            ((static_cast<float>(x) + (static_cast<float>(sample_x) + 0.5f) /
                                                          static_cast<float>(MASK_SUBSAMPLES)) *
                             shape_width / static_cast<float>(width)),
                        shape.bounds.min.y +
                            ((static_cast<float>(y) + (static_cast<float>(sample_y) + 0.5f) /
                                                          static_cast<float>(MASK_SUBSAMPLES)) *
                             shape_height / static_cast<float>(height))
                    };
                    if (point_in_shape(shape, point)) {
                        covered += 1u;
                    }
                }
            }

            uint32_t const total = MASK_SUBSAMPLES * MASK_SUBSAMPLES;
            return static_cast<uint8_t>((covered * 255u + (total / 2u)) / total);
        }

        [[nodiscard]] auto
        create_page(AtlasImpl& atlas, gui::render::Context render_context, Page*& out_page)
            -> gui::render::Result {
            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = ATLAS_SIZE;
            texture_desc.format = gui::render::TextureFormat::R8_UNORM;
            texture_desc.updatable = true;

            gui::render::Texture texture = {};
            gui::render::Result const result =
                gui::render::create_texture(render_context, texture_desc, texture);
            if (gui::render::result_failed(result)) {
                return result;
            }

            Page* const page = arena_new<Page>(*atlas.arena);
            page->texture = texture;
            if (atlas.last_page != nullptr) {
                atlas.last_page->next = page;
            } else {
                atlas.first_page = page;
            }
            atlas.last_page = page;
            out_page = page;
            return gui::render::Result::OK;
        }

        [[nodiscard]] auto alloc_page_rect(
            Page& page, uint32_t width, uint32_t height, uint32_t& out_x, uint32_t& out_y
        ) -> bool {
            uint32_t const alloc_width = width + (ATLAS_PADDING * 2u);
            uint32_t const alloc_height = height + (ATLAS_PADDING * 2u);
            if (alloc_width > ATLAS_SIZE.width || alloc_height > ATLAS_SIZE.height) {
                return false;
            }

            if (page.x + alloc_width > ATLAS_SIZE.width) {
                page.x = 0u;
                page.y += page.row_height;
                page.row_height = 0u;
            }
            if (page.y + alloc_height > ATLAS_SIZE.height) {
                return false;
            }

            out_x = page.x + ATLAS_PADDING;
            out_y = page.y + ATLAS_PADDING;
            page.x += alloc_width;
            page.row_height = std::max(page.row_height, alloc_height);
            return true;
        }

        [[nodiscard]] auto alloc_atlas_rect(
            AtlasImpl& atlas,
            gui::render::Context render_context,
            uint32_t width,
            uint32_t height,
            Page*& out_page,
            uint32_t& out_x,
            uint32_t& out_y
        ) -> bool {
            for (Page* page = atlas.first_page; page != nullptr; page = page->next) {
                if (alloc_page_rect(*page, width, height, out_x, out_y)) {
                    out_page = page;
                    return true;
                }
            }

            Page* page = nullptr;
            gui::render::Result const result = create_page(atlas, render_context, page);
            ASSERT(gui::render::result_succeeded(result));
            return gui::render::result_succeeded(result) &&
                   alloc_page_rect(*page, width, height, out_x, out_y);
        }

        auto destroy_pages(gui::render::Context render_context, AtlasImpl& atlas) -> void {
            for (Page* page = atlas.first_page; page != nullptr; page = page->next) {
                if (gui::render::texture_valid(page->texture)) {
                    gui::render::destroy_texture(render_context, page->texture);
                }
            }
            atlas.first_page = nullptr;
            atlas.last_page = nullptr;
        }

    } // namespace

    auto atlas_valid(Atlas atlas) -> bool {
        return atlas.handle != nullptr;
    }

    auto shape_key(path_model::Shape const& shape, ShapeKey& out_key) -> bool {
        out_key = {};
        uint32_t width = 0u;
        uint32_t height = 0u;
        if (!cacheable_shape(shape) || !mask_size(shape, width, height)) {
            return false;
        }

        uint64_t hash = FNV_OFFSET;
        if (shape.kind != path_model::ShapeKind::OVAL) {
            Slice<Vec2 const> const points = shape.path.flat_points.slice();
            for (Vec2 const point : points) {
                hash = hash_u64(hash, quantize(point.x - shape.bounds.min.x));
                hash = hash_u64(hash, quantize(point.y - shape.bounds.min.y));
            }
            out_key.point_count = static_cast<uint32_t>(points.size());
        }

        out_key.point_hash = hash;
        out_key.width = width;
        out_key.height = height;
        out_key.kind = shape.kind;
        out_key.convexity = shape.convexity;
        out_key.segment_mask = shape.segment_mask;
        return true;
    }

    auto shape_key_equal(ShapeKey lhs, ShapeKey rhs) -> bool {
        return lhs.point_hash == rhs.point_hash && lhs.width == rhs.width &&
               lhs.height == rhs.height && lhs.point_count == rhs.point_count &&
               lhs.kind == rhs.kind && lhs.convexity == rhs.convexity &&
               lhs.segment_mask == rhs.segment_mask;
    }

    auto rasterize(Arena& arena, path_model::Shape const& shape, Raster& out_raster) -> bool {
        out_raster = {};
        ShapeKey key = {};
        if (!shape_key(shape, key)) {
            return false;
        }

        uint32_t const width = key.width;
        uint32_t const height = key.height;
        uint32_t const upload_width = width + (ATLAS_PADDING * 2u);
        uint32_t const upload_height = height + (ATLAS_PADDING * 2u);
        size_t const upload_size =
            static_cast<size_t>(upload_width) * static_cast<size_t>(upload_height);
        uint8_t* const pixels = arena_alloc<uint8_t>(arena, upload_size);
        std::memset(pixels, 0, upload_size);

        for (uint32_t y = 0u; y < height; ++y) {
            uint8_t* const dst =
                pixels + (static_cast<size_t>(y + ATLAS_PADDING) * upload_width) + ATLAS_PADDING;
            for (uint32_t x = 0u; x < width; ++x) {
                dst[x] = coverage_at(shape, x, y, width, height);
            }
        }

        out_raster.pixels = pixels;
        out_raster.size = {upload_width, upload_height};
        out_raster.bytes_per_row = upload_width;
        out_raster.content_x = ATLAS_PADDING;
        out_raster.content_y = ATLAS_PADDING;
        out_raster.content_width = width;
        out_raster.content_height = height;
        return true;
    }

    auto create_atlas(
        Arena& arena, gui::render::Context render_context, size_t slot_count, Atlas& out_atlas
    ) -> gui::render::Result {
        ASSERT(out_atlas.handle == nullptr);

        AtlasImpl* const atlas = arena_new<AtlasImpl>(arena);
        atlas->arena = &arena;
        atlas->slot_count = slot_count;
        if (slot_count != 0u) {
            atlas->slots = arena_alloc<Entry*>(arena, slot_count);
            std::memset(atlas->slots, 0, sizeof(Entry*) * slot_count);
            Page* page = nullptr;
            gui::render::Result const result = create_page(*atlas, render_context, page);
            if (gui::render::result_failed(result)) {
                return result;
            }
        }

        out_atlas.handle = atlas;
        return gui::render::Result::OK;
    }

    auto clear_atlas(gui::render::Context render_context, Atlas atlas) -> void {
        BASE_UNUSED(render_context);

        AtlasImpl* const impl = atlas_from_handle(atlas);
        if (impl == nullptr) {
            return;
        }

        for (Page* page = impl->first_page; page != nullptr; page = page->next) {
            page->x = 0u;
            page->y = 0u;
            page->row_height = 0u;
        }
        if (impl->slots != nullptr && impl->slot_count != 0u) {
            std::memset(impl->slots, 0, sizeof(Entry*) * impl->slot_count);
        }
        impl->entry_count = 0u;
    }

    auto destroy_atlas(gui::render::Context render_context, Atlas& atlas) -> void {
        AtlasImpl* const impl = atlas_from_handle(atlas);
        if (impl == nullptr) {
            return;
        }

        clear_atlas(render_context, atlas);
        destroy_pages(render_context, *impl);
        impl->slots = nullptr;
        impl->slot_count = 0u;
        impl->arena = nullptr;
        atlas.handle = nullptr;
    }

    auto atlas_entry_count(Atlas atlas) -> size_t {
        AtlasImpl const* const impl = atlas_from_handle(atlas);
        return impl != nullptr ? impl->entry_count : 0u;
    }

    auto ensure_entry(
        Arena& upload_arena,
        Atlas atlas,
        gui::render::Context render_context,
        path_model::Shape const& shape,
        AtlasEntry& out_entry
    ) -> bool {
        out_entry = {};
        AtlasImpl* const impl = atlas_from_handle(atlas);
        ShapeKey key = {};
        if (impl == nullptr || impl->slot_count == 0u || !shape_key(shape, key)) {
            return false;
        }

        Entry* entry = find_entry(*impl, key);
        if (entry == nullptr) {
            Raster raster = {};
            if (!rasterize(upload_arena, shape, raster)) {
                return false;
            }

            Page* page = nullptr;
            uint32_t atlas_x = 0u;
            uint32_t atlas_y = 0u;
            if (!alloc_atlas_rect(
                    *impl,
                    render_context,
                    raster.content_width,
                    raster.content_height,
                    page,
                    atlas_x,
                    atlas_y
                )) {
                return false;
            }

            gui::render::TextureUpdateDesc update_desc = {};
            update_desc.x = atlas_x - ATLAS_PADDING;
            update_desc.y = atlas_y - ATLAS_PADDING;
            update_desc.size = raster.size;
            update_desc.bytes_per_row = raster.bytes_per_row;
            update_desc.pixels = raster.pixels;
            gui::render::Result const result =
                gui::render::update_texture(render_context, page->texture, update_desc);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return false;
            }

            size_t const slot_index = hash_shape_key(key) % impl->slot_count;
            entry = arena_new<Entry>(*impl->arena);
            entry->key = key;
            entry->page = page;
            entry->width = raster.content_width;
            entry->height = raster.content_height;
            entry->uv_rect[0u] = static_cast<float>(atlas_x) / static_cast<float>(ATLAS_SIZE.width);
            entry->uv_rect[1u] =
                static_cast<float>(atlas_y) / static_cast<float>(ATLAS_SIZE.height);
            entry->uv_rect[2u] = static_cast<float>(atlas_x + raster.content_width) /
                                 static_cast<float>(ATLAS_SIZE.width);
            entry->uv_rect[3u] = static_cast<float>(atlas_y + raster.content_height) /
                                 static_cast<float>(ATLAS_SIZE.height);
            entry->uv_clamp[0u] =
                (static_cast<float>(atlas_x) + 0.5f) / static_cast<float>(ATLAS_SIZE.width);
            entry->uv_clamp[1u] =
                (static_cast<float>(atlas_y) + 0.5f) / static_cast<float>(ATLAS_SIZE.height);
            entry->uv_clamp[2u] = (static_cast<float>(atlas_x + raster.content_width) - 0.5f) /
                                  static_cast<float>(ATLAS_SIZE.width);
            entry->uv_clamp[3u] = (static_cast<float>(atlas_y + raster.content_height) - 0.5f) /
                                  static_cast<float>(ATLAS_SIZE.height);
            entry->next = impl->slots[slot_index];
            impl->slots[slot_index] = entry;
            impl->entry_count += 1u;
        }

        out_entry.texture = entry->page->texture;
        std::memcpy(out_entry.uv_rect, entry->uv_rect, sizeof(out_entry.uv_rect));
        std::memcpy(out_entry.uv_clamp, entry->uv_clamp, sizeof(out_entry.uv_clamp));
        out_entry.width = entry->width;
        out_entry.height = entry->height;
        return true;
    }

} // namespace gui::draw::coverage_mask
