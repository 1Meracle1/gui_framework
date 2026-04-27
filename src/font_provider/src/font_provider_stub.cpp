#include "font_provider_platform.h"

#include <base/config.h>

namespace gui::font_provider::platform {

    auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context) -> Result {
        BASE_UNUSED(arena);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_context);
        return Result::UNSUPPORTED_PLATFORM;
    }

    auto destroy_context(Context* context) -> void {
        if (context != nullptr) {
            context->handle = nullptr;
        }
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font* out_font) -> Result {
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_font);
        return Result::UNSUPPORTED_PLATFORM;
    }

    auto close_font(Font* font) -> void {
        if (font != nullptr) {
            font->handle = nullptr;
        }
    }

    auto metrics_from_font(Font font, float size, Metrics* out_metrics) -> Result {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        if (out_metrics != nullptr) {
            *out_metrics = {};
        }
        return Result::UNSUPPORTED_PLATFORM;
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult* out_raster)
        -> Result {
        BASE_UNUSED(font);
        BASE_UNUSED(size);
        BASE_UNUSED(text);
        BASE_UNUSED(arena);
        if (out_raster != nullptr) {
            *out_raster = {};
        }
        return Result::UNSUPPORTED_PLATFORM;
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
