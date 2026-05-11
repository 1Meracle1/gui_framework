#include "draw_renderer_state.h"

namespace gui::draw::renderer_state {
    namespace {

        [[nodiscard]] auto color_with_opacity(Color color, float opacity) -> Color {
            color.a *= opacity;
            return color;
        }

        [[nodiscard]] auto transform_identity(Transform2D transform) -> bool {
            return transform.x_axis.x == 1.0f && transform.x_axis.y == 0.0f &&
                   transform.y_axis.x == 0.0f && transform.y_axis.y == 1.0f &&
                   transform.translation.x == 0.0f && transform.translation.y == 0.0f;
        }

        [[nodiscard]] auto transform_axis_aligned(Transform2D transform) -> bool {
            bool const diagonal = transform.x_axis.y == 0.0f && transform.y_axis.x == 0.0f;
            bool const quarter_turn = transform.x_axis.x == 0.0f && transform.y_axis.y == 0.0f;
            return diagonal || quarter_turn;
        }

        [[nodiscard]] auto layer_blend_mode(LayerBlendMode mode) -> PaintBlendMode {
            switch (mode) {
            case LayerBlendMode::NORMAL:
            case LayerBlendMode::PREMULTIPLIED_NORMAL:
                return PaintBlendMode::PREMULTIPLIED_SOURCE_OVER;
            case LayerBlendMode::ADDITIVE:
                return PaintBlendMode::ADDITIVE;
            case LayerBlendMode::MULTIPLY:
                return PaintBlendMode::MULTIPLY;
            case LayerBlendMode::SCREEN:
                return PaintBlendMode::SCREEN;
            }

            return PaintBlendMode::PREMULTIPLIED_SOURCE_OVER;
        }

        [[nodiscard]] auto target_color_format(gui::render::TextureFormat format)
            -> TargetColorFormat {
            switch (format) {
            case gui::render::TextureFormat::RGBA8_UNORM:
                return TargetColorFormat::RGBA8_UNORM;
            case gui::render::TextureFormat::R8_UNORM:
                return TargetColorFormat::R8_UNORM;
            }

            return TargetColorFormat::RGBA8_UNORM;
        }

        [[nodiscard]] auto lcd_raster_policy(font_provider::RasterPolicy raster_policy) -> bool {
            return raster_policy == font_provider::RasterPolicy::LCD_SHARP_HINTED ||
                   raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED;
        }

        [[nodiscard]] auto grayscale_raster_policy(font_provider::RasterPolicy raster_policy)
            -> font_provider::RasterPolicy {
            return raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED
                       ? font_provider::RasterPolicy::SMOOTH_HINTED
                       : font_provider::RasterPolicy::SHARP_HINTED;
        }

        [[nodiscard]] auto surface_allows_lcd_text(SurfaceProps const& props) -> bool {
            return props.pixel_geometry != PixelGeometry::UNKNOWN &&
                   props.color_format == TargetColorFormat::RGBA8_UNORM;
        }

        [[nodiscard]] auto premultiplied_paint(Color color, float opacity) -> Paint {
            Paint paint = {};
            paint.color = color_with_opacity(color, opacity);
            paint.blend_mode = PaintBlendMode::PREMULTIPLIED_SOURCE_OVER;
            paint.coverage_mode = CoverageMode::ANALYTIC;
            paint.aa_mode = AntiAliasMode::GRAYSCALE;
            paint.source_premultiplied = true;
            return paint;
        }

    } // namespace

    auto default_surface_props() -> SurfaceProps {
        return {};
    }

    auto surface_props_from_target_format(gui::render::TextureFormat format) -> SurfaceProps {
        SurfaceProps result = default_surface_props();
        result.color_format = target_color_format(format);
        return result;
    }

    auto transform_from_draw(Transform2D transform) -> Transform {
        return {transform, transform_identity(transform), transform_axis_aligned(transform)};
    }

    auto clip_from_rect(Rect rect) -> Clip {
        Clip clip = {};
        clip.rect = rect;
        return clip;
    }

    auto paint_from_color(Color color, float opacity) -> Paint {
        Paint paint = {};
        paint.color = color_with_opacity(color, opacity);
        return paint;
    }

    auto image_paint_from_color(gui::render::Texture texture, Color color, float opacity) -> Paint {
        Paint paint = paint_from_color(color, opacity);
        paint.texture = texture;
        paint.coverage_mode = CoverageMode::NONE;
        return paint;
    }

    auto box_fill_paint(BoxStyle const& style, float opacity) -> Paint {
        Paint paint = premultiplied_paint(style.fill_color, opacity);
        paint.texture = style.texture;
        return paint;
    }

    auto box_border_paint(BoxStyle const& style, float opacity) -> Paint {
        return premultiplied_paint(style.border_color, opacity);
    }

    auto box_shadow_paint(BoxStyle const& style, float opacity) -> Paint {
        return premultiplied_paint(style.shadow.color, opacity);
    }

    auto layer_state_from_desc(LayerDesc const& desc, Rect clip_rect) -> LayerState {
        LayerState state = {};
        state.paint.color = {1.0f, 1.0f, 1.0f, desc.opacity};
        state.paint.blend_mode = layer_blend_mode(desc.blend_mode);
        state.paint.coverage_mode = CoverageMode::NONE;
        state.paint.aa_mode = AntiAliasMode::NONE;
        state.paint.source_premultiplied = true;
        state.clip = clip_from_rect(clip_rect);
        state.clip.radius = desc.clip_radius;
        state.clip.aa_mode =
            desc.clip_radius > 0.0f ? AntiAliasMode::GRAYSCALE : AntiAliasMode::NONE;
        state.filter_kind = desc.filter_kind;
        state.filter_radius = desc.filter_radius;
        state.drop_shadow = desc.drop_shadow;
        state.isolated = desc.isolated;
        return state;
    }

    auto text_paint_from_style(
        TextStyle const& style,
        Transform2D transform,
        float opacity,
        SurfaceProps const& surface_props,
        bool root_target
    ) -> TextPaint {
        bool const lcd_allowed = root_target && transform_identity(transform) &&
                                 lcd_raster_policy(style.raster_policy) &&
                                 surface_allows_lcd_text(surface_props);

        TextPaint text = {};
        text.paint = premultiplied_paint(style.color, opacity);
        text.paint.coverage_mode = lcd_allowed ? CoverageMode::LCD_MASK : CoverageMode::ALPHA_MASK;
        text.paint.aa_mode = lcd_allowed ? AntiAliasMode::LCD : AntiAliasMode::GRAYSCALE;
        text.requested_raster_policy = style.raster_policy;
        text.resolved_raster_policy = lcd_allowed || !lcd_raster_policy(style.raster_policy)
                                          ? style.raster_policy
                                          : grayscale_raster_policy(style.raster_policy);
        text.surface_props = surface_props;
        text.lcd_allowed = lcd_allowed;
        return text;
    }

} // namespace gui::draw::renderer_state
