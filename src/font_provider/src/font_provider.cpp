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
        case Result::INVALID_ARGUMENT:
            return "invalid argument";
        case Result::OUT_OF_MEMORY:
            return "out of memory";
        case Result::BACKEND_FAILURE:
            return "backend failure";
        case Result::FONT_NOT_FOUND:
            return "font not found";
        case Result::TEXT_CONVERSION_FAILED:
            return "text conversion failed";
        case Result::RASTERIZATION_FAILED:
            return "rasterization failed";
        }

        return "unknown";
    }

    auto backend_name(Backend backend) -> char const* {
        switch (backend) {
        case Backend::DEFAULT:
            return "default";
        case Backend::DWRITE:
            return "dwrite";
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

    auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context) -> Result {
        if (out_context == nullptr || out_context->handle != nullptr) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        if (desc.backend != Backend::DEFAULT && desc.backend != Backend::DWRITE) {
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

    auto destroy_context(Context* context) -> void {
        if (context == nullptr || context->handle == nullptr) {
            return;
        }

        platform::destroy_context(context);
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font* out_font) -> Result {
        if (!context_valid(context) || out_font == nullptr || out_font->handle != nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        return platform::open_font(arena, context, desc, out_font);
    }

    auto close_font(Font* font) -> void {
        if (font == nullptr || font->handle == nullptr) {
            return;
        }

        platform::close_font(font);
    }

    auto metrics_from_font(Font font, float size, Metrics* out_metrics) -> Result {
        if (!font_valid(font) || size <= 0.0f || out_metrics == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        return platform::metrics_from_font(font, size, out_metrics);
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult* out_raster)
        -> Result {
        if (!font_valid(font) || size <= 0.0f || out_raster == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        *out_raster = {};
        if (text.empty()) {
            return Result::OK;
        }

        return platform::raster_text(font, size, text, arena, out_raster);
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
