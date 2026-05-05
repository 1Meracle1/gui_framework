#pragma once

#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <font_provider/font_provider.h>

namespace gui::font_cache {

    struct CacheDesc {
        size_t cache_slot_count = 1024u;
        size_t arena_reserve_size = 16u * 1024u * 1024u;
        size_t arena_commit_size = DEFAULT_ARENA_COMMIT_SIZE;
    };

    struct Cache {
        void* handle = nullptr;
    };

    struct Font {
        void* handle = nullptr;
    };

    struct TextGlyph {
        font_provider::Font font = {};
        uint16_t glyph_index = 0u;
        uint32_t cluster = 0u;
        float size = 0.0f;
        float x = 0.0f;
        float advance = 0.0f;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        font_provider::GlyphRaster raster = {};
    };

    struct TextRun {
        font_provider::SizeU32 size = {};
        uint32_t stride = 0u;
        uint8_t const* pixels = nullptr;
        font_provider::RasterFormat format = font_provider::RasterFormat::ALPHA;
        float advance = 0.0f;
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float baseline_y = 0.0f;
        float offset_y = 0.0f;
        float height = 0.0f;
        TextGlyph const* glyphs = nullptr;
        size_t glyph_count = 0u;
    };

    [[nodiscard]] auto cache_valid(Cache cache) -> bool;
    [[nodiscard]] auto font_valid(Font font) -> bool;

    auto create_cache(
        Arena& arena, font_provider::Context provider, CacheDesc const& desc, Cache& out_cache
    ) -> void;
    auto destroy_cache(Cache& cache) -> void;
    auto clear_cache(Cache cache) -> void;

    auto open_system_font(Cache cache, StrRef family_name, Font& out_font) -> void;
    auto open_font_file(Cache cache, StrRef file_path, Font& out_font) -> void;
    auto open_font_data(Cache cache, Slice<uint8_t const> data, Font& out_font) -> void;
    auto metrics_from_font(Font font, float size, font_provider::Metrics& out_metrics) -> void;
    [[nodiscard]] auto text_advance(Font font, float size, StrRef text) -> float;

    auto text_run(Cache cache, Font font, float size, StrRef text, TextRun& out_run) -> void;
    [[nodiscard]] auto
    glyph_raster(Font font, TextGlyph const& glyph, uint8_t phase_x, uint8_t phase_y)
        -> font_provider::GlyphRaster;

} // namespace gui::font_cache
