#pragma once

#include <font_provider/font_provider.h>

namespace gui::font_provider::platform {

    [[nodiscard]] auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context)
        -> Result;
    auto destroy_context(Context& context) -> void;

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void;
    auto close_font(Font& font) -> void;

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void;
    [[nodiscard]] auto text_advance(Font font, float size, StrRef text) -> float;
    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void;

    [[nodiscard]] auto native_factory(Context context) -> void*;
    [[nodiscard]] auto native_font_face(Font font) -> void*;

} // namespace gui::font_provider::platform
