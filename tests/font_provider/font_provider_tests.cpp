#include <algorithm>
#include <base/config.h>
#include <font_provider/font_provider.h>
#include <test/test.h>

namespace {

    [[nodiscard]] auto raster_has_coverage(gui::font_provider::RasterResult const& raster) -> bool {
        if (raster.pixels == nullptr) {
            return false;
        }

        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                if (line[x] != 0u) {
                    return true;
                }
            }
        }

        return false;
    }

    [[nodiscard]] auto raster_has_antialias_coverage(gui::font_provider::RasterResult const& raster)
        -> bool {
        if (raster.pixels == nullptr) {
            return false;
        }

        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t const coverage = line[x];
                if (coverage != 0u && coverage != 255u) {
                    return true;
                }
            }
        }

        return false;
    }

    [[nodiscard]] auto glyph_has_antialias_coverage(gui::font_provider::GlyphRaster const& raster)
        -> bool {
        if (raster.pixels == nullptr) {
            return false;
        }

        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t const* const pixel =
                    raster.format == gui::font_provider::RasterFormat::LCD_RGB ? line + x * 4u
                                                                               : line + x;
                uint8_t const coverage = raster.format == gui::font_provider::RasterFormat::LCD_RGB
                                             ? std::max(pixel[0u], std::max(pixel[1u], pixel[2u]))
                                             : pixel[0u];
                if (coverage != 0u && coverage != 255u) {
                    return true;
                }
            }
        }

        return false;
    }

    [[nodiscard]] auto
    glyph_band_coverage(gui::font_provider::GlyphRaster const& raster, uint32_t y0, uint32_t y1)
        -> uint64_t {
        uint64_t coverage = 0u;
        for (uint32_t y = y0; y < y1; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t const* const pixel =
                    raster.format == gui::font_provider::RasterFormat::LCD_RGB ? line + x * 4u
                                                                               : line + x;
                coverage += raster.format == gui::font_provider::RasterFormat::LCD_RGB
                                ? std::max(pixel[0u], std::max(pixel[1u], pixel[2u]))
                                : pixel[0u];
            }
        }
        return coverage;
    }

    TEST_CASE(font_provider_result_helpers_classify_status_and_errors) {
        TEST_EXPECT(context, gui::font_provider::result_succeeded(gui::font_provider::Result::OK));
        TEST_EXPECT(context, !gui::font_provider::result_failed(gui::font_provider::Result::OK));
        TEST_EXPECT(
            context,
            gui::font_provider::result_failed(gui::font_provider::Result::UNSUPPORTED_PLATFORM)
        );
        TEST_EXPECT(
            context, gui::font_provider::result_name(gui::font_provider::Result::OK)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::font_provider::backend_name(gui::font_provider::Backend::DEFAULT)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::font_provider::backend_name(gui::font_provider::Backend::FREETYPE)[0] != '\0'
        );
    }

    TEST_CASE(font_provider_handles_start_empty_and_validate_by_handle_value) {
        gui::font_provider::Context const provider = {};
        gui::font_provider::Font const font = {};

        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
        TEST_EXPECT(context, !gui::font_provider::font_valid(font));
        TEST_EXPECT(context, gui::font_provider::native_factory(provider) == nullptr);
        TEST_EXPECT(context, gui::font_provider::native_font_face(font) == nullptr);
    }

    TEST_CASE(font_provider_can_create_default_font_and_raster_text_on_supported_platforms) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const create_result =
            gui::font_provider::create_context(owner_arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, create_result == gui::font_provider::Result::OK);
        TEST_EXPECT(context, gui::font_provider::context_valid(provider));
        TEST_EXPECT(context, gui::font_provider::native_factory(provider) != nullptr);

        gui::font_provider::Font font = {};
        gui::font_provider::open_font(owner_arena, provider, {}, font);
        TEST_EXPECT(context, gui::font_provider::font_valid(font));
        TEST_EXPECT(context, gui::font_provider::native_font_face(font) != nullptr);

        gui::font_provider::Metrics metrics = {};
        gui::font_provider::metrics_from_font(font, 16.0f, metrics);
        TEST_EXPECT(context, metrics.ascent > 0.0f);
        TEST_EXPECT(context, metrics.descent >= 0.0f);
        TEST_EXPECT(context, gui::font_provider::text_advance(font, 16.0f, "hello") > 0.0f);
        TEST_EXPECT(
            context,
            gui::font_provider::text_advance(font, 16.0f, "iiii") <
                gui::font_provider::text_advance(font, 16.0f, "WWWW")
        );

        Arena arena = {};
        arena.init({4u * 1024u * 1024u, DEFAULT_ARENA_COMMIT_SIZE});
        gui::font_provider::ShapedText shaped = {};
        gui::font_provider::shape_text(font, 20.0f, "fi", arena, shaped);
        TEST_EXPECT(context, shaped.glyphs != nullptr);
        TEST_EXPECT(context, shaped.glyph_count > 0u);
        TEST_EXPECT(context, shaped.advance > 0.0f);
        TEST_EXPECT(context, shaped.size.width > 0u);
        TEST_EXPECT(context, gui::font_provider::font_valid(shaped.glyphs[0u].font));
        TEST_EXPECT(context, shaped.glyphs[0u].font.handle == font.handle);
        TEST_EXPECT(context, shaped.glyphs[0u].size == 20.0f);

        gui::font_provider::ShapedText utf8 = {};
        gui::font_provider::shape_text(font, 20.0f, "e\xcc\x81", arena, utf8);
        TEST_EXPECT(context, utf8.glyph_count > 0u);
        TEST_EXPECT(context, utf8.advance > 0.0f);
        for (size_t index = 0u; index < utf8.glyph_count; ++index) {
            TEST_EXPECT(context, utf8.glyphs[index].font.handle == font.handle);
        }

        gui::font_provider::GlyphRaster glyph = {};
        gui::font_provider::raster_glyph(font, 20.0f, shaped.glyphs[0u].glyph_index, arena, glyph);
        TEST_EXPECT(context, glyph.size.width > 0u);
        TEST_EXPECT(context, glyph.size.height > 0u);
        TEST_EXPECT(context, glyph.stride == glyph.size.width);
        TEST_EXPECT(context, glyph.pixels != nullptr);
        TEST_EXPECT(context, glyph.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(context, glyph_has_antialias_coverage(glyph));

        gui::font_provider::ShapedText t_shaped = {};
        gui::font_provider::shape_text(font, 32.0f, "T", arena, t_shaped);
        gui::font_provider::GlyphRaster t_glyph = {};
        gui::font_provider::raster_glyph(
            font, 32.0f, t_shaped.glyphs[0u].glyph_index, arena, t_glyph
        );
        uint32_t const band_height = std::max(1u, t_glyph.size.height / 3u);
        TEST_EXPECT(
            context,
            glyph_band_coverage(t_glyph, 0u, band_height) >
                glyph_band_coverage(t_glyph, t_glyph.size.height - band_height, t_glyph.size.height)
        );

        gui::font_provider::GlyphRaster phased_glyph = {};
        gui::font_provider::raster_glyph(
            font, 20.0f, shaped.glyphs[0u].glyph_index, 1u, 0u, arena, phased_glyph
        );
        TEST_EXPECT(context, phased_glyph.size.width > 0u);
        TEST_EXPECT(context, phased_glyph.size.height > 0u);
        TEST_EXPECT(context, phased_glyph.format == gui::font_provider::RasterFormat::ALPHA);

        gui::font_provider::GlyphRaster smooth_glyph = {};
        gui::font_provider::raster_glyph(
            font,
            20.0f,
            shaped.glyphs[0u].glyph_index,
            gui::font_provider::RasterPolicy::SMOOTH_HINTED,
            1u,
            0u,
            arena,
            smooth_glyph
        );
        TEST_EXPECT(context, smooth_glyph.size.width > 0u);
        TEST_EXPECT(context, smooth_glyph.size.height > 0u);
        TEST_EXPECT(context, smooth_glyph.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(context, glyph_has_antialias_coverage(smooth_glyph));

        gui::font_provider::RasterResult raster = {};
        gui::font_provider::raster_text(font, 16.0f, "hello", arena, raster);
        TEST_EXPECT(context, raster.size.width > 0u);
        TEST_EXPECT(context, raster.size.height > 0u);
        TEST_EXPECT(context, raster.stride == raster.size.width);
        TEST_EXPECT(context, raster.pixels != nullptr);
        TEST_EXPECT(context, raster.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(context, raster.advance > 0.0f);
        TEST_EXPECT(context, raster_has_coverage(raster));
        TEST_EXPECT(context, raster_has_antialias_coverage(raster));
        arena.destroy();

        gui::font_provider::close_font(font);
        TEST_EXPECT(context, !gui::font_provider::font_valid(font));
        gui::font_provider::destroy_context(provider);
        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
#else
        TEST_EXPECT(context, create_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
#endif
    }

    TEST_CASE(font_provider_can_use_freetype_backend_on_windows) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::ContextDesc desc = {};
        desc.backend = gui::font_provider::Backend::FREETYPE;
        gui::font_provider::Result const create_result =
            gui::font_provider::create_context(owner_arena, desc, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, create_result == gui::font_provider::Result::OK);
        TEST_EXPECT(context, gui::font_provider::context_valid(provider));
        TEST_EXPECT(context, provider.backend == gui::font_provider::Backend::FREETYPE);
        TEST_EXPECT(context, gui::font_provider::native_factory(provider) != nullptr);

        gui::font_provider::Font font = {};
        gui::font_provider::open_font(owner_arena, provider, {}, font);
        TEST_EXPECT(context, gui::font_provider::font_valid(font));
        TEST_EXPECT(context, font.backend == gui::font_provider::Backend::FREETYPE);
        TEST_EXPECT(context, gui::font_provider::native_font_face(font) != nullptr);

        Arena arena = {};
        arena.init({1024u * 1024u, DEFAULT_ARENA_COMMIT_SIZE});
        gui::font_provider::ShapedText shaped = {};
        gui::font_provider::shape_text(font, 18.0f, "FreeType", arena, shaped);
        TEST_EXPECT(context, shaped.glyph_count == 8u);
        TEST_EXPECT(context, shaped.advance > 0.0f);
        for (size_t index = 0u; index < shaped.glyph_count; ++index) {
            TEST_EXPECT(context, shaped.glyphs[index].offset_x == 0.0f);
            TEST_EXPECT(context, shaped.glyphs[index].offset_y == 0.0f);
        }

        gui::font_provider::GlyphRaster glyph = {};
        gui::font_provider::raster_glyph(font, 18.0f, shaped.glyphs[0u].glyph_index, arena, glyph);
        TEST_EXPECT(context, glyph.size.width > 0u);
        TEST_EXPECT(context, glyph.size.height > 0u);
        TEST_EXPECT(context, glyph.stride == glyph.size.width);
        TEST_EXPECT(context, glyph.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(context, glyph_has_antialias_coverage(glyph));

        gui::font_provider::GlyphRaster smooth_glyph = {};
        gui::font_provider::raster_glyph(
            font,
            18.0f,
            shaped.glyphs[0u].glyph_index,
            gui::font_provider::RasterPolicy::SMOOTH_HINTED,
            arena,
            smooth_glyph
        );
        TEST_EXPECT(context, smooth_glyph.size.width > 0u);
        TEST_EXPECT(context, smooth_glyph.size.height > 0u);
        TEST_EXPECT(context, smooth_glyph.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(context, glyph_has_antialias_coverage(smooth_glyph));

        gui::font_provider::RasterResult raster = {};
        gui::font_provider::raster_text(font, 18.0f, "FreeType", arena, raster);
        TEST_EXPECT(context, raster_has_coverage(raster));
        TEST_EXPECT(context, raster_has_antialias_coverage(raster));
        arena.destroy();

        gui::font_provider::close_font(font);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, create_result == gui::font_provider::Result::UNSUPPORTED_BACKEND);
        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
#endif

        owner_arena.destroy();
    }

} // namespace

TEST_MAIN()
