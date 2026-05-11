#pragma once

#include <cstdint>
#include <draw/draw.h>

namespace gui::draw::renderer_state {

    inline constexpr float DEFAULT_TEXT_GAMMA = font_provider::DEFAULT_TEXT_GAMMA;

    enum class PaintBlendMode : uint8_t {
        SOURCE_OVER,
        PREMULTIPLIED_SOURCE_OVER,
        ADDITIVE,
        MULTIPLY,
        SCREEN,
    };

    enum class CoverageMode : uint8_t {
        NONE,
        ANALYTIC,
        ALPHA_MASK,
        LCD_MASK,
    };

    enum class AntiAliasMode : uint8_t {
        NONE,
        GRAYSCALE,
        LCD,
    };

    using PixelGeometry = font_provider::PixelGeometry;
    using TargetColorFormat = font_provider::TargetColorFormat;

    struct Paint {
        Color color = {};
        gui::render::Texture texture = {};
        PaintBlendMode blend_mode = PaintBlendMode::SOURCE_OVER;
        CoverageMode coverage_mode = CoverageMode::ANALYTIC;
        AntiAliasMode aa_mode = AntiAliasMode::GRAYSCALE;
        bool source_premultiplied = false;
    };

    struct Transform {
        Transform2D local_to_device = {};
        bool identity = true;
        bool axis_aligned = true;
    };

    struct Clip {
        Rect rect = {};
        float radius = 0.0f;
        AntiAliasMode aa_mode = AntiAliasMode::NONE;
    };

    using SurfaceProps = font_provider::SurfaceProps;

    struct TextPaint {
        Paint paint = {};
        font_provider::RasterPolicy requested_raster_policy = font_provider::DEFAULT_RASTER_POLICY;
        font_provider::RasterPolicy resolved_raster_policy = font_provider::DEFAULT_RASTER_POLICY;
        SurfaceProps surface_props = {};
        bool lcd_allowed = false;
    };

    struct LayerState {
        Paint paint = {};
        Clip clip = {};
        FilterKind filter_kind = FilterKind::NONE;
        float filter_radius = 0.0f;
        DropShadow drop_shadow = {};
        bool isolated = true;
    };

    [[nodiscard]] auto default_surface_props() -> SurfaceProps;
    [[nodiscard]] auto surface_props_from_target_format(gui::render::TextureFormat format)
        -> SurfaceProps;
    [[nodiscard]] auto transform_from_draw(Transform2D transform) -> Transform;
    [[nodiscard]] auto clip_from_rect(Rect rect) -> Clip;
    [[nodiscard]] auto paint_from_color(Color color, float opacity) -> Paint;
    [[nodiscard]] auto
    image_paint_from_color(gui::render::Texture texture, Color color, float opacity) -> Paint;
    [[nodiscard]] auto box_fill_paint(BoxStyle const& style, float opacity) -> Paint;
    [[nodiscard]] auto box_border_paint(BoxStyle const& style, float opacity) -> Paint;
    [[nodiscard]] auto box_shadow_paint(BoxStyle const& style, float opacity) -> Paint;
    [[nodiscard]] auto layer_state_from_desc(LayerDesc const& desc, Rect clip_rect) -> LayerState;
    [[nodiscard]] auto text_paint_from_style(
        TextStyle const& style,
        Transform2D transform,
        float opacity,
        SurfaceProps const& surface_props,
        bool root_target
    ) -> TextPaint;

} // namespace gui::draw::renderer_state
