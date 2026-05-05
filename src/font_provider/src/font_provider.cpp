#include "font_provider_platform.h"

#include <base/config.h>

namespace gui::font_provider {

    auto result_succeeded(Result result) -> bool {
        return static_cast<int32_t>(result) >= 0;
    }

    auto result_failed(Result result) -> bool {
        return !result_succeeded(result);
    }

    auto result_name(Result result) -> char const* {
        switch (result) {
        case Result::OK:
            return "ok";
        case Result::UNSUPPORTED_PLATFORM:
            return "unsupported platform";
        case Result::UNSUPPORTED_BACKEND:
            return "unsupported backend";
        case Result::BACKEND_FAILURE:
            return "backend failure";
        }

        return "unknown";
    }

    auto backend_name(Backend backend) -> char const* {
        switch (backend) {
        case Backend::DEFAULT:
            return "default";
        case Backend::DWRITE:
            return "dwrite";
        case Backend::FREETYPE:
            return "freetype";
        case Backend::CORE_TEXT:
            return "core text";
        }

        return "unknown";
    }

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto font_valid(Font font) -> bool {
        return font.handle != nullptr;
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        ASSERT(out_context.handle == nullptr);

#if BASE_PLATFORM_WINDOWS
        if (desc.backend != Backend::DEFAULT && desc.backend != Backend::DWRITE &&
            desc.backend != Backend::FREETYPE) {
            return Result::UNSUPPORTED_BACKEND;
        }
#elif BASE_PLATFORM_MACOS
        if (desc.backend != Backend::DEFAULT && desc.backend != Backend::CORE_TEXT) {
            return Result::UNSUPPORTED_BACKEND;
        }
#else
        if (desc.backend != Backend::DEFAULT) {
            return Result::UNSUPPORTED_BACKEND;
        }
#endif

        return platform::create_context(arena, desc, out_context);
    }

    auto destroy_context(Context& context) -> void {
        ASSERT(context.handle != nullptr);

        platform::destroy_context(context);
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        ASSERT(context_valid(context));
        ASSERT(out_font.handle == nullptr);

        platform::open_font(arena, context, desc, out_font);
    }

    auto close_font(Font& font) -> void {
        ASSERT(font.handle != nullptr);

        platform::close_font(font);
    }

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void {
        ASSERT(font_valid(font));
        ASSERT(size > 0.0f);

        platform::metrics_from_font(font, size, out_metrics);
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        ASSERT(font_valid(font));
        ASSERT(size > 0.0f);

        return text.empty() ? 0.0f : platform::text_advance(font, size, text);
    }

    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text)
        -> void {
        ASSERT(font_valid(font));
        ASSERT(size > 0.0f);

        out_text = {};
        if (text.empty()) {
            return;
        }

        platform::shape_text(font, size, text, arena, out_text);
    }

    auto
    raster_glyph(Font font, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster)
        -> void {
        raster_glyph(font, size, glyph_index, 0u, 0u, arena, out_raster);
    }

    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        uint8_t phase_x,
        uint8_t phase_y,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void {
        ASSERT(font_valid(font));
        ASSERT(size > 0.0f);

        out_raster = {};
        platform::raster_glyph(font, size, glyph_index, phase_x, phase_y, arena, out_raster);
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void {
        ASSERT(font_valid(font));
        ASSERT(size > 0.0f);

        out_raster = {};
        if (text.empty()) {
            return;
        }

        platform::raster_text(font, size, text, arena, out_raster);
    }

    auto native_factory(Context context) -> void* {
        if (!context_valid(context)) {
            return nullptr;
        }

        return platform::native_factory(context);
    }

    auto native_font_face(Font font) -> void* {
        if (!font_valid(font)) {
            return nullptr;
        }

        return platform::native_font_face(font);
    }

} // namespace gui::font_provider
