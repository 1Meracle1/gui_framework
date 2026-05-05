#include "font_provider_win32_backends.h"

namespace gui::font_provider::platform {
    namespace {

        [[nodiscard]] auto resolve_backend(Backend backend) -> Backend {
            return backend == Backend::DEFAULT ? Backend::DWRITE : backend;
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        ContextDesc backend_desc = desc;
        backend_desc.backend = resolve_backend(desc.backend);

        Result result = Result::UNSUPPORTED_BACKEND;
        if (backend_desc.backend == Backend::FREETYPE) {
            result = freetype::create_context(arena, backend_desc, out_context);
        } else if (backend_desc.backend == Backend::DWRITE) {
            result = dwrite::create_context(arena, backend_desc, out_context);
        }

        if (result_succeeded(result)) {
            out_context.backend = backend_desc.backend;
        }
        return result;
    }

    auto destroy_context(Context& context) -> void {
        Backend const backend = context.backend;
        if (backend == Backend::FREETYPE) {
            freetype::destroy_context(context);
        } else {
            dwrite::destroy_context(context);
        }
        context.backend = Backend::DEFAULT;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        if (context.backend == Backend::FREETYPE) {
            freetype::open_font(arena, context, desc, out_font);
        } else {
            dwrite::open_font(arena, context, desc, out_font);
        }
        if (font_valid(out_font)) {
            out_font.backend = context.backend;
        }
    }

    auto close_font(Font& font) -> void {
        Backend const backend = font.backend;
        if (backend == Backend::FREETYPE) {
            freetype::close_font(font);
        } else {
            dwrite::close_font(font);
        }
        font.backend = Backend::DEFAULT;
    }

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void {
        if (font.backend == Backend::FREETYPE) {
            freetype::metrics_from_font(font, size, out_metrics);
        } else {
            dwrite::metrics_from_font(font, size, out_metrics);
        }
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        return font.backend == Backend::FREETYPE ? freetype::text_advance(font, size, text)
                                                 : dwrite::text_advance(font, size, text);
    }

    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text)
        -> void {
        if (font.backend == Backend::FREETYPE) {
            freetype::shape_text(font, size, text, arena, out_text);
        } else {
            dwrite::shape_text(font, size, text, arena, out_text);
        }
    }

    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        RasterPolicy raster_policy,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void {
        if (font.backend == Backend::FREETYPE) {
            freetype::raster_glyph(font, size, glyph_index, raster_policy, arena, out_raster);
        } else {
            dwrite::raster_glyph(font, size, glyph_index, raster_policy, arena, out_raster);
        }
    }

    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        RasterPolicy raster_policy,
        uint8_t phase_x,
        uint8_t phase_y,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void {
        if (font.backend == Backend::FREETYPE) {
            freetype::raster_glyph(
                font, size, glyph_index, raster_policy, phase_x, phase_y, arena, out_raster
            );
        } else {
            dwrite::raster_glyph(
                font, size, glyph_index, raster_policy, phase_x, phase_y, arena, out_raster
            );
        }
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void {
        if (font.backend == Backend::FREETYPE) {
            freetype::raster_text(font, size, text, arena, out_raster);
        } else {
            dwrite::raster_text(font, size, text, arena, out_raster);
        }
    }

    auto native_factory(Context context) -> void* {
        return context.backend == Backend::FREETYPE ? freetype::native_factory(context)
                                                    : dwrite::native_factory(context);
    }

    auto native_font_face(Font font) -> void* {
        return font.backend == Backend::FREETYPE ? freetype::native_font_face(font)
                                                 : dwrite::native_font_face(font);
    }

} // namespace gui::font_provider::platform
