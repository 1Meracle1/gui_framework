#include <base/config.h>
#include <font_cache/font_cache.h>
#include <test/test.h>

namespace {

    [[nodiscard]] auto glyph_coverage_sum(gui::font_provider::GlyphRaster const& raster)
        -> uint64_t {
        uint64_t result = 0u;
        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                if (raster.format == gui::font_provider::RasterFormat::LCD_RGB) {
                    uint8_t const* const pixel = line + x * 4u;
                    result += pixel[0u] + pixel[1u] + pixel[2u];
                } else {
                    result += line[x];
                }
            }
        }
        return result;
    }

    [[nodiscard]] auto glyph_bgr_swap_matches(
        gui::font_provider::GlyphRaster const& rgb, gui::font_provider::GlyphRaster const& bgr
    ) -> bool {
        if (rgb.format != gui::font_provider::RasterFormat::LCD_RGB ||
            bgr.format != gui::font_provider::RasterFormat::LCD_RGB ||
            rgb.size.width != bgr.size.width || rgb.size.height != bgr.size.height) {
            return false;
        }

        for (uint32_t y = 0u; y < rgb.size.height; ++y) {
            uint8_t const* const rgb_line = rgb.pixels + (static_cast<size_t>(y) * rgb.stride);
            uint8_t const* const bgr_line = bgr.pixels + (static_cast<size_t>(y) * bgr.stride);
            for (uint32_t x = 0u; x < rgb.size.width; ++x) {
                uint8_t const* const rgb_pixel = rgb_line + x * 4u;
                uint8_t const* const bgr_pixel = bgr_line + x * 4u;
                if (rgb_pixel[0u] != rgb_pixel[2u]) {
                    return bgr_pixel[0u] == rgb_pixel[2u] && bgr_pixel[1u] == rgb_pixel[1u] &&
                           bgr_pixel[2u] == rgb_pixel[0u];
                }
            }
        }
        return false;
    }

    TEST_CASE(font_cache_handles_start_empty) {
        gui::font_cache::Cache const cache = {};
        gui::font_cache::Font const font = {};

        TEST_EXPECT(context, !gui::font_cache::cache_valid(cache));
        TEST_EXPECT(context, !gui::font_cache::font_valid(font));
    }

    TEST_CASE(font_cache_can_cache_default_text_runs_on_supported_platforms) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(owner_arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(owner_arena, provider, {}, cache);
        TEST_EXPECT(context, gui::font_cache::cache_valid(cache));

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, {}, font);
        TEST_EXPECT(context, gui::font_cache::font_valid(font));

        gui::font_provider::Metrics metrics = {};
        gui::font_cache::metrics_from_font(font, 18.0f, metrics);
        TEST_EXPECT(context, metrics.ascent > 0.0f);

        gui::font_cache::TextRun first = {};
        gui::font_cache::TextRun second = {};
        gui::font_cache::text_run(cache, font, 18.0f, "cached text", first);
        gui::font_cache::text_run(cache, font, 18.0f, "cached text", second);
        TEST_EXPECT(context, first.glyphs != nullptr);
        TEST_EXPECT(context, first.glyph_count > 0u);
        TEST_EXPECT(context, first.runs != nullptr);
        TEST_EXPECT(context, first.run_count > 0u);
        TEST_EXPECT(context, first.glyphs == second.glyphs);
        TEST_EXPECT(context, first.runs == second.runs);
        TEST_EXPECT(context, first.glyph_count > 2u);
        TEST_EXPECT(context, first.glyphs[1u].glyph_index != 0u);
        TEST_EXPECT(context, first.glyphs[1u].x > first.glyphs[0u].x);
        TEST_EXPECT(context, first.glyphs[2u].x > first.glyphs[1u].x);
        TEST_EXPECT(context, gui::font_provider::font_valid(first.glyphs[0u].font));
        TEST_EXPECT(context, first.glyphs[0u].size == 18.0f);
        TEST_EXPECT(context, first.glyphs[0u].run_index < first.run_count);
        TEST_EXPECT(context, first.glyphs[0u].utf8_start < first.glyphs[0u].utf8_end);
        TEST_EXPECT(context, first.glyphs[0u].utf8_end <= StrRef("cached text").size());
        TEST_EXPECT(context, first.runs[0u].first_glyph == 0u);
        TEST_EXPECT(context, first.runs[0u].glyph_count <= first.glyph_count);
        gui::font_provider::GlyphRaster phase0 =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], 0u, 0u);
        gui::font_provider::GlyphRaster phase1 =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], 1u, 0u);
        gui::font_provider::GlyphRaster phase1_again =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], 1u, 0u);
        TEST_EXPECT(context, phase0.format == gui::font_provider::RasterFormat::LCD_RGB);
        TEST_EXPECT(context, phase0.pixels != nullptr);
        TEST_EXPECT(context, phase1.pixels == phase1_again.pixels);
        TEST_EXPECT(context, phase1.pixels != nullptr);
        TEST_EXPECT(context, phase1.pixels != phase0.pixels);

        gui::font_provider::GlyphRasterDesc phase_desc = {};
        phase_desc.phase_x = 1u;
        gui::font_provider::GlyphRaster phase5 =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], phase_desc);
        phase_desc.phase_x = gui::font_provider::GLYPH_RASTER_PHASE_COUNT + 1u;
        gui::font_provider::GlyphRaster phase5_again =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], phase_desc);
        TEST_EXPECT(context, phase5.pixels == phase5_again.pixels);

        gui::font_provider::GlyphRasterDesc boosted_gamma_desc = {};
        boosted_gamma_desc.surface_props.text_gamma = 1.8f;
        gui::font_provider::GlyphRaster boosted_gamma =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], boosted_gamma_desc);
        TEST_EXPECT(context, boosted_gamma.pixels != nullptr);
        TEST_EXPECT(context, boosted_gamma.pixels != phase0.pixels);
        TEST_EXPECT(context, glyph_coverage_sum(boosted_gamma) >= glyph_coverage_sum(phase0));

        gui::font_provider::GlyphRasterDesc lcd_rgb_desc = {};
        lcd_rgb_desc.raster_policy = gui::font_provider::RasterPolicy::LCD_SHARP_HINTED;
        gui::font_provider::GlyphRaster lcd_rgb =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], lcd_rgb_desc);
        TEST_EXPECT(context, lcd_rgb.format == gui::font_provider::RasterFormat::LCD_RGB);

        gui::font_provider::GlyphRasterDesc lcd_bgr_desc = lcd_rgb_desc;
        lcd_bgr_desc.surface_props.pixel_geometry =
            gui::font_provider::PixelGeometry::BGR_HORIZONTAL;
        gui::font_provider::GlyphRaster lcd_bgr =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], lcd_bgr_desc);
        TEST_EXPECT(context, lcd_bgr.pixels != lcd_rgb.pixels);
        TEST_EXPECT(context, glyph_bgr_swap_matches(lcd_rgb, lcd_bgr));

        gui::font_provider::GlyphRasterDesc unknown_geometry_desc = lcd_rgb_desc;
        unknown_geometry_desc.surface_props.pixel_geometry =
            gui::font_provider::PixelGeometry::UNKNOWN;
        gui::font_provider::GlyphRaster unknown_geometry =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], unknown_geometry_desc);
        TEST_EXPECT(context, unknown_geometry.format == gui::font_provider::RasterFormat::ALPHA);

        gui::font_provider::GlyphRasterDesc r8_desc = lcd_rgb_desc;
        r8_desc.surface_props.color_format = gui::font_provider::TargetColorFormat::R8_UNORM;
        gui::font_provider::GlyphRaster r8_raster =
            gui::font_cache::glyph_raster(font, first.glyphs[0u], r8_desc);
        TEST_EXPECT(context, r8_raster.format == gui::font_provider::RasterFormat::ALPHA);

        TEST_EXPECT(context, first.advance == second.advance);
        TEST_EXPECT(context, first.baseline_y > 0.0f);
        TEST_EXPECT(
            context, gui::font_cache::text_advance(font, 18.0f, "cached text") == first.advance
        );
        TEST_EXPECT(
            context,
            gui::font_cache::text_advance(font, 18.0f, "iiii") <
                gui::font_cache::text_advance(font, 18.0f, "WWWW")
        );

        gui::font_cache::Font icon_font = {};
        gui::font_cache::open_system_font(cache, "Segoe MDL2 Assets", icon_font);
        TEST_EXPECT(context, gui::font_cache::font_valid(icon_font));
        gui::font_cache::TextRun fallback = {};
        gui::font_cache::TextRun fallback_again = {};
        gui::font_cache::text_run(cache, icon_font, 18.0f, "A", fallback);
        gui::font_cache::text_run(cache, icon_font, 18.0f, "A", fallback_again);
        TEST_EXPECT(context, fallback.glyph_count > 0u);
        TEST_EXPECT(context, fallback.run_count > 0u);
        TEST_EXPECT(context, fallback.glyphs == fallback_again.glyphs);
        TEST_EXPECT(context, fallback.runs == fallback_again.runs);
        TEST_EXPECT(context, gui::font_provider::font_valid(fallback.glyphs[0u].font));
        TEST_EXPECT(context, fallback.glyphs[0u].glyph_index != 0u);

        gui::font_cache::destroy_cache(cache);
        TEST_EXPECT(context, !gui::font_cache::cache_valid(cache));
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

} // namespace

TEST_MAIN()
