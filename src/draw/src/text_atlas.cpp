#include "text_atlas.h"

#include <algorithm>
#include <base/memory.h>
#include <cmath>
#include <cstring>

namespace gui::draw {
    namespace {

        constexpr gui::render::SizeU32 TEXT_ATLAS_SIZE = {1024u, 1024u};
        constexpr uint32_t TEXT_ATLAS_PADDING = 1u;

        struct QuantizedGlyphOrigin {
            float origin = 0.0f;
            uint8_t phase = 0u;
        };

        struct TextAtlasKey {
            size_t font = 0u;
            font_provider::Backend backend = font_provider::Backend::DEFAULT;
            uint32_t size_bits = 0u;
            uint16_t glyph_index = 0u;
            font_provider::RasterPolicy raster_policy = font_provider::DEFAULT_RASTER_POLICY;
            uint8_t phase_x = 0u;
            uint8_t phase_y = 0u;
            font_provider::PixelGeometry pixel_geometry =
                font_provider::PixelGeometry::RGB_HORIZONTAL;
            font_provider::TargetColorFormat color_format =
                font_provider::TargetColorFormat::RGBA8_UNORM;
            uint32_t text_gamma_bits = 0u;
            uint32_t text_contrast_bits = 0u;
        };

        struct TextAtlasPage {
            TextAtlasPage* next = nullptr;
            gui::render::Texture texture = {};
            gui::render::BindGroup bind_group = {};
            uint32_t x = 0u;
            uint32_t y = 0u;
            uint32_t row_height = 0u;
        };

        struct TextAtlasEntry {
            TextAtlasEntry* next = nullptr;
            TextAtlasKey key = {};
            TextAtlasPage* page = nullptr;
            float uv_rect[4] = {};
            float offset_x = 0.0f;
            float offset_y = 0.0f;
            uint32_t width = 0u;
            uint32_t height = 0u;
            font_provider::RasterFormat format = font_provider::RasterFormat::ALPHA;
        };

        struct TextAtlasImpl {
            Arena* arena = nullptr;
            TextAtlasPage* first_page = nullptr;
            TextAtlasPage* last_page = nullptr;
            TextAtlasEntry** slots = nullptr;
            size_t slot_count = 0u;
        };

        [[nodiscard]] auto atlas_from_handle(TextAtlas atlas) -> TextAtlasImpl* {
            return static_cast<TextAtlasImpl*>(atlas.handle);
        }

        [[nodiscard]] auto float_bits(float value) -> uint32_t {
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        [[nodiscard]] auto glyph_phase_value(uint8_t phase) -> float {
            return static_cast<float>(phase % font_provider::GLYPH_RASTER_PHASE_COUNT) /
                   static_cast<float>(font_provider::GLYPH_RASTER_PHASE_COUNT);
        }

        [[nodiscard]] auto quantize_glyph_origin(float value) -> QuantizedGlyphOrigin {
            float const scaled = std::floor(
                value * static_cast<float>(font_provider::GLYPH_RASTER_PHASE_COUNT) + 0.5f
            );
            int32_t const quantized = static_cast<int32_t>(scaled);
            uint8_t const phase = static_cast<uint8_t>(
                ((quantized % font_provider::GLYPH_RASTER_PHASE_COUNT) +
                 font_provider::GLYPH_RASTER_PHASE_COUNT) %
                font_provider::GLYPH_RASTER_PHASE_COUNT
            );
            return {scaled / static_cast<float>(font_provider::GLYPH_RASTER_PHASE_COUNT), phase};
        }

        [[nodiscard]] auto glyph_origin_base(QuantizedGlyphOrigin origin) -> float {
            return origin.origin - glyph_phase_value(origin.phase);
        }

        [[nodiscard]] auto transform_identity(Transform2D transform) -> bool {
            return transform.x_axis.x == 1.0f && transform.x_axis.y == 0.0f &&
                   transform.y_axis.x == 0.0f && transform.y_axis.y == 1.0f &&
                   transform.translation.x == 0.0f && transform.translation.y == 0.0f;
        }

        [[nodiscard]] auto lcd_raster_policy(font_provider::RasterPolicy raster_policy) -> bool {
            return raster_policy == font_provider::RasterPolicy::LCD_SHARP_HINTED ||
                   raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED;
        }

        [[nodiscard]] auto grayscale_raster_policy(font_provider::RasterPolicy raster_policy)
            -> font_provider::RasterPolicy {
            return raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED
                       ? font_provider::RasterPolicy::SMOOTH_HINTED
                       : font_provider::RasterPolicy::SHARP_HINTED;
        }

        [[nodiscard]] auto surface_allows_lcd_text(font_provider::SurfaceProps const& props)
            -> bool {
            return props.pixel_geometry != font_provider::PixelGeometry::UNKNOWN &&
                   props.color_format == font_provider::TargetColorFormat::RGBA8_UNORM;
        }

        [[nodiscard]] auto text_raster_policy(
            TextCommand const& command, font_provider::SurfaceProps const& surface_props
        ) -> font_provider::RasterPolicy {
            if (!lcd_raster_policy(command.style.raster_policy)) {
                return command.style.raster_policy;
            }
            if (surface_allows_lcd_text(surface_props) && transform_identity(command.transform)) {
                return command.style.raster_policy;
            }
            return grayscale_raster_policy(command.style.raster_policy);
        }

        [[nodiscard]] auto
        glyph_origin_x(TextCommand const& command, font_cache::TextGlyph const& glyph)
            -> QuantizedGlyphOrigin {
            return quantize_glyph_origin(command.position.x + glyph.x + glyph.offset_x);
        }

        [[nodiscard]] auto
        glyph_origin_y(TextCommand const& command, font_cache::TextGlyph const& glyph)
            -> QuantizedGlyphOrigin {
            return quantize_glyph_origin(
                command.position.y + command.run.baseline_y - glyph.offset_y
            );
        }

        [[nodiscard]] auto text_glyph_raster_desc(
            TextCommand const& command,
            font_cache::TextGlyph const& glyph,
            font_provider::SurfaceProps const& surface_props
        ) -> font_provider::GlyphRasterDesc {
            QuantizedGlyphOrigin const x = glyph_origin_x(command, glyph);
            QuantizedGlyphOrigin const y = glyph_origin_y(command, glyph);
            font_provider::GlyphRasterDesc desc = {};
            desc.raster_policy = text_raster_policy(command, surface_props);
            desc.phase_x = x.phase;
            desc.phase_y = y.phase;
            desc.surface_props = surface_props;
            return desc;
        }

        [[nodiscard]] auto text_atlas_key(
            font_cache::TextGlyph const& glyph, font_provider::GlyphRasterDesc const& desc
        ) -> TextAtlasKey {
            return {
                reinterpret_cast<size_t>(glyph.font.handle),
                glyph.font.backend,
                float_bits(glyph.size),
                glyph.glyph_index,
                desc.raster_policy,
                desc.phase_x,
                desc.phase_y,
                desc.surface_props.pixel_geometry,
                desc.surface_props.color_format,
                float_bits(desc.surface_props.text_gamma),
                float_bits(desc.surface_props.text_contrast)
            };
        }

        [[nodiscard]] auto text_atlas_key_equal(TextAtlasKey lhs, TextAtlasKey rhs) -> bool {
            return lhs.font == rhs.font && lhs.backend == rhs.backend &&
                   lhs.size_bits == rhs.size_bits && lhs.glyph_index == rhs.glyph_index &&
                   lhs.raster_policy == rhs.raster_policy && lhs.phase_x == rhs.phase_x &&
                   lhs.phase_y == rhs.phase_y && lhs.pixel_geometry == rhs.pixel_geometry &&
                   lhs.color_format == rhs.color_format &&
                   lhs.text_gamma_bits == rhs.text_gamma_bits &&
                   lhs.text_contrast_bits == rhs.text_contrast_bits;
        }

        [[nodiscard]] auto hash_text_atlas_key(TextAtlasKey key) -> size_t {
            size_t result = key.font;
            result ^=
                static_cast<size_t>(key.backend) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^=
                static_cast<size_t>(key.size_bits) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^= static_cast<size_t>(key.glyph_index) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            result ^= static_cast<size_t>(key.raster_policy) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            result ^=
                static_cast<size_t>(key.phase_x) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^=
                static_cast<size_t>(key.phase_y) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
            result ^= static_cast<size_t>(key.pixel_geometry) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            result ^= static_cast<size_t>(key.color_format) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            result ^= static_cast<size_t>(key.text_gamma_bits) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            result ^= static_cast<size_t>(key.text_contrast_bits) + 0x9e3779b9u + (result << 6u) +
                      (result >> 2u);
            return result;
        }

        [[nodiscard]] auto raster_bytes_per_pixel(font_provider::RasterFormat format) -> uint32_t {
            switch (format) {
            case font_provider::RasterFormat::ALPHA:
                return 1u;
            case font_provider::RasterFormat::LCD_RGB:
                return 4u;
            }

            return 1u;
        }

        [[nodiscard]] auto text_command_visible(TextCommand const& command) -> bool {
            font_cache::TextRun const& run = command.run;
            return run.glyphs != nullptr && run.glyph_count != 0u;
        }

        [[nodiscard]] auto create_texture_bind_group(
            Arena& arena,
            gui::render::Context render_context,
            gui::render::Texture texture,
            gui::render::BindGroup& out_bind_group
        ) -> gui::render::Result {
            gui::render::BindGroupTextureBinding texture_binding = {};
            texture_binding.stage = gui::render::ShaderStage::PIXEL;
            texture_binding.slot = 0u;
            texture_binding.texture = texture;

            gui::render::BindGroupDesc bind_group_desc = {};
            bind_group_desc.textures = &texture_binding;
            bind_group_desc.texture_count = 1u;

            return gui::render::create_bind_group(
                arena, render_context, bind_group_desc, out_bind_group
            );
        }

        [[nodiscard]] auto create_atlas_page(
            TextAtlasImpl& atlas, gui::render::Context render_context, TextAtlasPage*& out_page
        ) -> gui::render::Result {
            gui::render::TextureDesc texture_desc = {};
            texture_desc.size = TEXT_ATLAS_SIZE;
            texture_desc.updatable = true;

            gui::render::Texture texture = {};
            gui::render::Result result =
                gui::render::create_texture(render_context, texture_desc, texture);
            if (gui::render::result_failed(result)) {
                return result;
            }

            gui::render::BindGroup bind_group = {};
            result = create_texture_bind_group(*atlas.arena, render_context, texture, bind_group);
            if (gui::render::result_failed(result)) {
                gui::render::destroy_texture(render_context, texture);
                return result;
            }

            TextAtlasPage* const page = arena_new<TextAtlasPage>(*atlas.arena);
            page->texture = texture;
            page->bind_group = bind_group;
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
            TextAtlasPage& page, uint32_t width, uint32_t height, uint32_t& out_x, uint32_t& out_y
        ) -> bool {
            uint32_t const alloc_width = width + (TEXT_ATLAS_PADDING * 2u);
            uint32_t const alloc_height = height + (TEXT_ATLAS_PADDING * 2u);
            if (alloc_width > TEXT_ATLAS_SIZE.width || alloc_height > TEXT_ATLAS_SIZE.height) {
                return false;
            }

            if (page.x + alloc_width > TEXT_ATLAS_SIZE.width) {
                page.x = 0u;
                page.y += page.row_height;
                page.row_height = 0u;
            }
            if (page.y + alloc_height > TEXT_ATLAS_SIZE.height) {
                return false;
            }

            out_x = page.x + TEXT_ATLAS_PADDING;
            out_y = page.y + TEXT_ATLAS_PADDING;
            page.x += alloc_width;
            page.row_height = std::max(page.row_height, alloc_height);
            return true;
        }

        [[nodiscard]] auto alloc_atlas_rect(
            TextAtlasImpl& atlas,
            gui::render::Context render_context,
            uint32_t width,
            uint32_t height,
            TextAtlasPage*& out_page,
            uint32_t& out_x,
            uint32_t& out_y
        ) -> bool {
            for (TextAtlasPage* page = atlas.first_page; page != nullptr; page = page->next) {
                if (alloc_page_rect(*page, width, height, out_x, out_y)) {
                    out_page = page;
                    return true;
                }
            }

            TextAtlasPage* page = nullptr;
            gui::render::Result const result = create_atlas_page(atlas, render_context, page);
            ASSERT(gui::render::result_succeeded(result));
            return gui::render::result_succeeded(result) &&
                   alloc_page_rect(*page, width, height, out_x, out_y);
        }

        [[nodiscard]] auto find_text_atlas_entry(TextAtlasImpl& atlas, TextAtlasKey key)
            -> TextAtlasEntry* {
            if (atlas.slots == nullptr || atlas.slot_count == 0u) {
                return nullptr;
            }

            size_t const slot_index = hash_text_atlas_key(key) % atlas.slot_count;
            for (TextAtlasEntry* entry = atlas.slots[slot_index]; entry != nullptr;
                 entry = entry->next) {
                if (text_atlas_key_equal(entry->key, key)) {
                    return entry;
                }
            }
            return nullptr;
        }

        auto fill_atlas_pixels(
            font_provider::GlyphRaster const& raster, uint8_t* upload_pixels, uint32_t upload_width
        ) -> void {
            for (uint32_t y = 0u; y < raster.size.height; ++y) {
                uint8_t const* const src = raster.pixels + (static_cast<size_t>(y) * raster.stride);
                uint8_t* const dst =
                    upload_pixels +
                    (static_cast<size_t>(y + TEXT_ATLAS_PADDING) * upload_width * 4u) +
                    (TEXT_ATLAS_PADDING * 4u);
                if (raster.format == font_provider::RasterFormat::LCD_RGB) {
                    for (uint32_t x = 0u; x < raster.size.width; ++x) {
                        uint8_t const r = src[x * 4u + 0u];
                        uint8_t const g = src[x * 4u + 1u];
                        uint8_t const b = src[x * 4u + 2u];
                        dst[x * 4u + 0u] = r;
                        dst[x * 4u + 1u] = g;
                        dst[x * 4u + 2u] = b;
                        dst[x * 4u + 3u] = std::max(std::max(r, g), b);
                    }
                } else {
                    for (uint32_t x = 0u; x < raster.size.width; ++x) {
                        uint8_t const coverage = src[x];
                        dst[x * 4u + 0u] = coverage;
                        dst[x * 4u + 1u] = coverage;
                        dst[x * 4u + 2u] = coverage;
                        dst[x * 4u + 3u] = coverage;
                    }
                }
            }
        }

        [[nodiscard]] auto ensure_text_atlas_entry(
            Arena& upload_arena,
            TextAtlasImpl& atlas,
            gui::render::Context render_context,
            TextCommand const& command,
            font_cache::TextGlyph const& glyph,
            font_provider::SurfaceProps const& surface_props
        ) -> TextAtlasEntry* {
            font_provider::GlyphRasterDesc const desc =
                text_glyph_raster_desc(command, glyph, surface_props);
            TextAtlasKey const key = text_atlas_key(glyph, desc);
            TextAtlasEntry* entry = find_text_atlas_entry(atlas, key);
            if (entry != nullptr) {
                return entry;
            }

            font_provider::GlyphRaster const raster =
                font_cache::glyph_raster(command.style.font, glyph, desc);
            if (raster.pixels == nullptr || raster.size.width == 0u || raster.size.height == 0u) {
                return nullptr;
            }
            uint32_t const bytes_per_pixel = raster_bytes_per_pixel(raster.format);
            ASSERT(raster.stride >= raster.size.width * bytes_per_pixel);

            TextAtlasPage* page = nullptr;
            uint32_t atlas_x = 0u;
            uint32_t atlas_y = 0u;
            if (!alloc_atlas_rect(
                    atlas,
                    render_context,
                    raster.size.width,
                    raster.size.height,
                    page,
                    atlas_x,
                    atlas_y
                )) {
                return nullptr;
            }

            uint32_t const upload_width = raster.size.width + (TEXT_ATLAS_PADDING * 2u);
            uint32_t const upload_height = raster.size.height + (TEXT_ATLAS_PADDING * 2u);
            size_t const upload_size =
                static_cast<size_t>(upload_width) * static_cast<size_t>(upload_height) * 4u;
            uint8_t* const upload_pixels = arena_alloc<uint8_t>(upload_arena, upload_size);
            std::memset(upload_pixels, 0, upload_size);
            fill_atlas_pixels(raster, upload_pixels, upload_width);

            gui::render::TextureUpdateDesc update_desc = {};
            update_desc.x = atlas_x - TEXT_ATLAS_PADDING;
            update_desc.y = atlas_y - TEXT_ATLAS_PADDING;
            update_desc.size = {upload_width, upload_height};
            update_desc.bytes_per_row = upload_width * 4u;
            update_desc.pixels = upload_pixels;

            gui::render::Result const result =
                gui::render::update_texture(render_context, page->texture, update_desc);
            ASSERT(gui::render::result_succeeded(result));
            if (gui::render::result_failed(result)) {
                return nullptr;
            }

            size_t const slot_index = hash_text_atlas_key(key) % atlas.slot_count;
            entry = arena_new<TextAtlasEntry>(*atlas.arena);
            entry->key = key;
            entry->page = page;
            entry->uv_rect[0u] =
                static_cast<float>(atlas_x) / static_cast<float>(TEXT_ATLAS_SIZE.width);
            entry->uv_rect[1u] =
                static_cast<float>(atlas_y) / static_cast<float>(TEXT_ATLAS_SIZE.height);
            entry->uv_rect[2u] = static_cast<float>(atlas_x + raster.size.width) /
                                 static_cast<float>(TEXT_ATLAS_SIZE.width);
            entry->uv_rect[3u] = static_cast<float>(atlas_y + raster.size.height) /
                                 static_cast<float>(TEXT_ATLAS_SIZE.height);
            entry->offset_x = raster.offset_x;
            entry->offset_y = raster.offset_y;
            entry->width = raster.size.width;
            entry->height = raster.size.height;
            entry->format = raster.format;
            entry->next = atlas.slots[slot_index];
            atlas.slots[slot_index] = entry;
            return entry;
        }

        auto write_text_atlas_piece(
            TextCommand const& command,
            font_cache::TextGlyph const& glyph,
            TextAtlasEntry const& entry,
            TextAtlasPiece& out_piece
        ) -> void {
            out_piece = {};
            out_piece.bind_group = entry.page->bind_group;
            std::memcpy(out_piece.uv_rect, entry.uv_rect, sizeof(out_piece.uv_rect));
            QuantizedGlyphOrigin const x = glyph_origin_x(command, glyph);
            QuantizedGlyphOrigin const y = glyph_origin_y(command, glyph);
            out_piece.x = glyph_origin_base(x) + entry.offset_x;
            out_piece.y = glyph_origin_base(y) + entry.offset_y;
            out_piece.width = entry.width;
            out_piece.height = entry.height;
            out_piece.format = entry.format;
        }

        auto append_text_subrun(
            PreparedText& prepared_text, TextAtlasSubRunRange& range, TextAtlasPiece const& piece
        ) -> void {
            if (range.subrun_count != 0u) {
                TextAtlasSubRun& last =
                    prepared_text.subruns[range.first_subrun + range.subrun_count - 1u];
                if (last.kind == TextAtlasSubRunKind::DIRECT_MASK &&
                    last.bind_group.handle == piece.bind_group.handle &&
                    last.format == piece.format &&
                    last.first_piece + last.piece_count == prepared_text.piece_count) {
                    last.piece_count += 1u;
                    return;
                }
            }

            ASSERT(prepared_text.subruns != nullptr);
            ASSERT(prepared_text.subrun_count <= prepared_text.piece_count);
            TextAtlasSubRun& subrun = prepared_text.subruns[prepared_text.subrun_count];
            subrun = {};
            subrun.first_piece = prepared_text.piece_count;
            subrun.piece_count = 1u;
            subrun.bind_group = piece.bind_group;
            subrun.format = piece.format;
            prepared_text.subrun_count += 1u;
            range.subrun_count += 1u;
        }

        [[nodiscard]] auto
        text_piece_capacity(Context draw_context, size_t first_command, size_t end_command)
            -> size_t {
            size_t result = 0u;
            for (size_t index = first_command; index < end_command; ++index) {
                Command const* const draw_command = command(draw_context, index);
                ASSERT(draw_command != nullptr);
                if (draw_command->kind == CommandKind::TEXT) {
                    TextCommand const* const text = text_command(draw_context, draw_command->index);
                    ASSERT(text != nullptr);
                    if (text_command_visible(*text)) {
                        result += text->run.glyph_count;
                    }
                } else if (draw_command->kind == CommandKind::LAYER_BEGIN) {
                    LayerCommand const* const layer =
                        layer_command(draw_context, draw_command->index);
                    ASSERT(layer != nullptr);
                    index = layer->end_command_index;
                }
            }
            return result;
        }

    } // namespace

    auto text_atlas_valid(TextAtlas atlas) -> bool {
        return atlas.handle != nullptr;
    }

    auto create_text_atlas(
        Arena& arena, gui::render::Context render_context, size_t slot_count, TextAtlas& out_atlas
    ) -> gui::render::Result {
        ASSERT(out_atlas.handle == nullptr);

        TextAtlasImpl* const atlas = arena_new<TextAtlasImpl>(arena);
        atlas->arena = &arena;
        atlas->slot_count = slot_count;
        if (slot_count != 0u) {
            atlas->slots = arena_alloc<TextAtlasEntry*>(arena, slot_count);
            std::memset(atlas->slots, 0, sizeof(TextAtlasEntry*) * slot_count);
            TextAtlasPage* page = nullptr;
            gui::render::Result const result = create_atlas_page(*atlas, render_context, page);
            if (gui::render::result_failed(result)) {
                return result;
            }
        }

        out_atlas.handle = atlas;
        return gui::render::Result::OK;
    }

    auto destroy_text_atlas(gui::render::Context render_context, TextAtlas& atlas) -> void {
        TextAtlasImpl* const impl = atlas_from_handle(atlas);
        if (impl == nullptr) {
            return;
        }

        for (TextAtlasPage* page = impl->first_page; page != nullptr; page = page->next) {
            if (gui::render::bind_group_valid(page->bind_group)) {
                gui::render::destroy_bind_group(render_context, page->bind_group);
            }
            if (gui::render::texture_valid(page->texture)) {
                gui::render::destroy_texture(render_context, page->texture);
            }
        }

        impl->first_page = nullptr;
        impl->last_page = nullptr;
        impl->slots = nullptr;
        impl->slot_count = 0u;
        impl->arena = nullptr;
        atlas.handle = nullptr;
    }

    auto prepare_text_pieces(
        Arena& arena,
        TextAtlas atlas,
        gui::render::Context render_context,
        Context draw_context,
        size_t first_command,
        size_t end_command,
        font_provider::SurfaceProps const& surface_props,
        PreparedText& out_text
    ) -> bool {
        out_text = {};
        size_t const piece_capacity = text_piece_capacity(draw_context, first_command, end_command);
        if (piece_capacity == 0u) {
            return true;
        }

        TextAtlasImpl* const impl = atlas_from_handle(atlas);
        if (impl == nullptr || impl->slot_count == 0u) {
            return false;
        }

        size_t const range_count = text_command_count(draw_context);
        out_text.ranges = arena_alloc<TextAtlasSubRunRange>(arena, range_count);
        std::memset(out_text.ranges, 0, sizeof(TextAtlasSubRunRange) * range_count);
        out_text.range_count = range_count;
        out_text.pieces = arena_alloc<TextAtlasPiece>(arena, piece_capacity);
        out_text.subruns = arena_alloc<TextAtlasSubRun>(arena, piece_capacity);
        ArenaTemp upload_temp = begin_thread_temp_arena();
        for (size_t index = first_command; index < end_command; ++index) {
            Command const* const draw_command = command(draw_context, index);
            ASSERT(draw_command != nullptr);
            if (draw_command->kind == CommandKind::TEXT) {
                TextCommand const* const text = text_command(draw_context, draw_command->index);
                ASSERT(text != nullptr);
                TextAtlasSubRunRange& range = out_text.ranges[draw_command->index];
                range.first_subrun = out_text.subrun_count;
                if (!text_command_visible(*text)) {
                    continue;
                }

                for (size_t glyph_index = 0u; glyph_index < text->run.glyph_count; ++glyph_index) {
                    font_cache::TextGlyph const& glyph = text->run.glyphs[glyph_index];
                    TextAtlasEntry const* const entry = ensure_text_atlas_entry(
                        *upload_temp.arena(), *impl, render_context, *text, glyph, surface_props
                    );
                    if (entry == nullptr || entry->page == nullptr) {
                        continue;
                    }

                    ASSERT(out_text.piece_count < piece_capacity);
                    write_text_atlas_piece(
                        *text, glyph, *entry, out_text.pieces[out_text.piece_count]
                    );
                    append_text_subrun(out_text, range, out_text.pieces[out_text.piece_count]);
                    out_text.piece_count += 1u;
                }
            } else if (draw_command->kind == CommandKind::LAYER_BEGIN) {
                LayerCommand const* const layer = layer_command(draw_context, draw_command->index);
                ASSERT(layer != nullptr);
                index = layer->end_command_index;
            }
        }
        return true;
    }

} // namespace gui::draw
