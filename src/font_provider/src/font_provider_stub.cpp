#include "font_provider_platform.h"

#include <base/config.h>

namespace gui::font_provider::platform {

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        BASE_UNUSED(arena);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_context);
        return Result::UNSUPPORTED_PLATFORM;
    }

    auto destroy_context(Context& context) -> void {
        context.handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_font);
    }

    auto close_font(Font& font) -> void {
        font.handle = nullptr;
    }

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        out_metrics = {};
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        BASE_UNUSED(text);
        return 0.0f;
    }

    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text)
        -> void {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        BASE_UNUSED(text);
        BASE_UNUSED(arena);
        out_text = {};
    }

    auto
    raster_glyph(Font font, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster)
        -> void {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        BASE_UNUSED(glyph_index);
        BASE_UNUSED(arena);
        out_raster = {};
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        BASE_UNUSED(text);
        BASE_UNUSED(arena);
        out_raster = {};
    }

    auto native_factory(Context context) -> void* {
        BASE_UNUSED(context);
        return nullptr;
    }

    auto native_font_face(Font font) -> void* {
        BASE_UNUSED(font);
        return nullptr;
    }

} // namespace gui::font_provider::platform
