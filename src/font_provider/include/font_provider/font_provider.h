#pragma once

#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <cstdint>

namespace gui::font_provider {

    inline constexpr uint8_t GLYPH_RASTER_PHASE_COUNT = 4u;

    enum class Backend : uint8_t {
        DEFAULT,
        DWRITE,
        FREETYPE,
        CORE_TEXT,
    };

    enum class Result : int8_t {
        OK = 0,

        UNSUPPORTED_PLATFORM = -1,
        UNSUPPORTED_BACKEND = -2,
        BACKEND_FAILURE = -3,
    };

    struct SizeU32 {
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct ContextDesc {
        Backend backend = Backend::DEFAULT;
    };

    struct Context {
        void* handle = nullptr;
        Backend backend = Backend::DEFAULT;
    };

    struct FontDesc {
        StrRef family_name = {};
        StrRef file_path = {};
        Slice<uint8_t const> data = {};
    };

    struct Font {
        void* handle = nullptr;
        Backend backend = Backend::DEFAULT;
    };

    struct Metrics {
        float line_gap = 0.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float capital_height = 0.0f;
    };

    enum class RasterFormat : uint8_t {
        ALPHA,
        LCD_RGB,
    };

    struct RasterResult {
        SizeU32 size = {};
        uint32_t stride = 0u;
        uint8_t* pixels = nullptr;
        RasterFormat format = RasterFormat::ALPHA;
        float advance = 0.0f;
        float offset_y = 0.0f;
        float height = 0.0f;
    };

    struct GlyphRaster {
        SizeU32 size = {};
        uint32_t stride = 0u;
        uint8_t* pixels = nullptr;
        RasterFormat format = RasterFormat::ALPHA;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
    };

    struct ShapedGlyph {
        Font font = {};
        uint16_t glyph_index = 0u;
        uint32_t cluster = 0u;
        float size = 0.0f;
        float x = 0.0f;
        float advance = 0.0f;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
    };

    struct ShapedText {
        ShapedGlyph* glyphs = nullptr;
        size_t glyph_count = 0u;
        float advance = 0.0f;
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float baseline_y = 0.0f;
        float height = 0.0f;
        SizeU32 size = {};
    };

    [[nodiscard]] auto result_succeeded(Result result) -> bool;
    [[nodiscard]] auto result_failed(Result result) -> bool;
    [[nodiscard]] auto result_name(Result result) -> char const*;
    [[nodiscard]] auto backend_name(Backend backend) -> char const*;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto font_valid(Font font) -> bool;

    [[nodiscard]] auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context)
        -> Result;
    auto destroy_context(Context& context) -> void;

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void;
    auto close_font(Font& font) -> void;

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void;
    [[nodiscard]] auto text_advance(Font font, float size, StrRef text) -> float;
    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text) -> void;
    auto
    raster_glyph(Font font, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster)
        -> void;
    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        uint8_t phase_x,
        uint8_t phase_y,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void;
    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void;

    [[nodiscard]] auto native_factory(Context context) -> void*;
    [[nodiscard]] auto native_font_face(Font font) -> void*;

} // namespace gui::font_provider
