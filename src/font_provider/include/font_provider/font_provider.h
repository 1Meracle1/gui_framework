#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <cstdint>

namespace gui::font_provider {

    enum class Backend : uint8_t {
        DEFAULT,
        DWRITE,
        CORE_TEXT,
    };

    enum class Result : int8_t {
        OK = 0,

        UNSUPPORTED_PLATFORM = -1,
        UNSUPPORTED_BACKEND = -2,
        INVALID_ARGUMENT = -3,
        OUT_OF_MEMORY = -4,
        BACKEND_FAILURE = -5,
        FONT_NOT_FOUND = -6,
        TEXT_CONVERSION_FAILED = -7,
        RASTERIZATION_FAILED = -8,
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
    };

    struct FontDesc {
        StrRef family_name = {};
        StrRef file_path = {};
    };

    struct Font {
        void* handle = nullptr;
    };

    struct Metrics {
        float line_gap = 0.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float capital_height = 0.0f;
    };

    struct RasterResult {
        SizeU32 size = {};
        uint32_t stride = 0u;
        uint8_t* rgba_pixels = nullptr;
        float advance = 0.0f;
        float height = 0.0f;
    };

    [[nodiscard]] auto result_succeeded(Result result) -> bool;
    [[nodiscard]] auto result_failed(Result result) -> bool;
    [[nodiscard]] auto result_name(Result result) -> char const*;
    [[nodiscard]] auto backend_name(Backend backend) -> char const*;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto font_valid(Font font) -> bool;

    [[nodiscard]] auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context)
        -> Result;
    auto destroy_context(Context* context) -> void;

    [[nodiscard]] auto
    open_font(Arena& arena, Context context, FontDesc const& desc, Font* out_font) -> Result;
    auto close_font(Font* font) -> void;

    [[nodiscard]] auto metrics_from_font(Font font, float size, Metrics* out_metrics) -> Result;
    [[nodiscard]] auto
    raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult* out_raster)
        -> Result;

    [[nodiscard]] auto native_factory(Context context) -> void*;
    [[nodiscard]] auto native_font_face(Font font) -> void*;

} // namespace gui::font_provider
