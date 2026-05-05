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
            uint32_t size_bits = 0u;
            uint16_t glyph_index = 0u;
            uint8_t raster_policy = 0u;
            uint8_t phase_x = 0u;
            uint8_t phase_y = 0u;
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

        [[nodiscard]] auto
        text_raster_policy(TextCommand const& command, bool target_allows_lcd_text)
            -> font_provider::RasterPolicy {
            BASE_UNUSED(command);
            BASE_UNUSED(target_allows_lcd_text);
            return font_provider::RasterPolicy::SHARP_HINTED_ALPHA;
        }

        [[nodiscard]] auto text_atlas_key(
            TextCommand const& command,
            font_cache::TextGlyph const& glyph,
            font_provider::RasterPolicy raster_policy
        ) -> TextAtlasKey {
            QuantizedGlyphOrigin const x = glyph_origin_x(command, glyph);
            QuantizedGlyphOrigin const y = glyph_origin_y(command, glyph);
            return {
                reinterpret_cast<size_t>(glyph.font.handle),
                float_bits(glyph.size),
                glyph.glyph_index,
                static_cast<uint8_t>(raster_policy),
                x.phase,
                y.phase
            };
        }

        [[nodiscard]] auto text_atlas_key_equal(TextAtlasKey lhs, TextAtlasKey rhs) -> bool {
            return lhs.font == rhs.font && lhs.size_bits == rhs.size_bits &&
                   lhs.glyph_index == rhs.glyph_index && lhs.raster_policy == rhs.raster_policy &&
                   lhs.phase_x == rhs.phase_x && lhs.phase_y == rhs.phase_y;
        }

        [[nodiscard]] auto hash_text_atlas_key(TextAtlasKey key) -> size_t {
            size_t result = key.font;
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
                        dst[x * 4u + 0u] = src[x * 4u + 0u];
                        dst[x * 4u + 1u] = src[x * 4u + 1u];
                        dst[x * 4u + 2u] = src[x * 4u + 2u];
                        dst[x * 4u + 3u] = src[x * 4u + 3u];
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
            bool target_allows_lcd_text
        ) -> TextAtlasEntry* {
            font_provider::RasterPolicy const raster_policy =
                text_raster_policy(command, target_allows_lcd_text);
            TextAtlasKey const key = text_atlas_key(command, glyph, raster_policy);
            TextAtlasEntry* entry = find_text_atlas_entry(atlas, key);
            if (entry != nullptr) {
                return entry;
            }

            font_provider::GlyphRaster const raster = font_cache::glyph_raster(
                command.style.font, glyph, raster_policy, key.phase_x, key.phase_y
            );
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
        bool target_allows_lcd_text,
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
        out_text.ranges = arena_alloc<TextAtlasPieceRange>(arena, range_count);
        std::memset(out_text.ranges, 0, sizeof(TextAtlasPieceRange) * range_count);
        out_text.range_count = range_count;
        out_text.pieces = arena_alloc<TextAtlasPiece>(arena, piece_capacity);
        ArenaTemp upload_temp = begin_thread_temp_arena();
        for (size_t index = first_command; index < end_command; ++index) {
            Command const* const draw_command = command(draw_context, index);
            ASSERT(draw_command != nullptr);
            if (draw_command->kind == CommandKind::TEXT) {
                TextCommand const* const text = text_command(draw_context, draw_command->index);
                ASSERT(text != nullptr);
                TextAtlasPieceRange& range = out_text.ranges[draw_command->index];
                range.first_piece = out_text.piece_count;
                if (!text_command_visible(*text)) {
                    continue;
                }

                for (size_t glyph_index = 0u; glyph_index < text->run.glyph_count; ++glyph_index) {
                    font_cache::TextGlyph const& glyph = text->run.glyphs[glyph_index];
                    TextAtlasEntry const* const entry = ensure_text_atlas_entry(
                        *upload_temp.arena(),
                        *impl,
                        render_context,
                        *text,
                        glyph,
                        target_allows_lcd_text
                    );
                    if (entry == nullptr || entry->page == nullptr) {
                        continue;
                    }

                    ASSERT(out_text.piece_count < piece_capacity);
                    write_text_atlas_piece(
                        *text, glyph, *entry, out_text.pieces[out_text.piece_count]
                    );
                    out_text.piece_count += 1u;
                    range.piece_count += 1u;
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
