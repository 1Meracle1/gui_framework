#include <algorithm>
#include <base/config.h>
#include <font_provider/font_provider.h>
#include <test/test.h>

#if BASE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#if BASE_PLATFORM_WINDOWS
    [[nodiscard]] auto file_exists(char const* path) -> bool {
        DWORD const attributes = GetFileAttributesA(path);
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
    }

#endif

    [[nodiscard]] auto shaped_has_missing_glyph(gui::font_provider::ShapedText const& shaped)
        -> bool {
        for (size_t index = 0u; index < shaped.glyph_count; ++index) {
            if (shaped.glyphs[index].glyph_index == 0u) {
                return true;
            }
        }

        return false;
    }

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

    [[nodiscard]] auto glyph_has_lcd_coverage(gui::font_provider::GlyphRaster const& raster)
        -> bool {
        if (raster.format != gui::font_provider::RasterFormat::LCD_RGB ||
            raster.pixels == nullptr) {
            return false;
        }

        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t const* const pixel = line + x * 4u;
                if (pixel[0u] != pixel[1u] || pixel[1u] != pixel[2u]) {
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

        gui::font_provider::ShapedText combining = {};
        gui::font_provider::shape_text(font, 20.0f, "e\xcc\x81", arena, combining);
        TEST_EXPECT(context, combining.glyph_count > 0u);
        TEST_EXPECT(context, combining.advance > 0.0f);
        TEST_EXPECT(context, !shaped_has_missing_glyph(combining));
        TEST_EXPECT(
            context, combining.advance < gui::font_provider::text_advance(font, 20.0f, "e ") + 1.0f
        );

        gui::font_provider::ShapedText fallback = {};
        gui::font_provider::shape_text(font, 20.0f, "\xf0\x9f\x98\x80", arena, fallback);
        TEST_EXPECT(context, fallback.glyph_count > 0u);
        TEST_EXPECT(context, !shaped_has_missing_glyph(fallback));
        bool fallback_font_seen = false;
        for (size_t index = 0u; index < fallback.glyph_count; ++index) {
            TEST_EXPECT(context, gui::font_provider::font_valid(fallback.glyphs[index].font));
            TEST_EXPECT(
                context,
                gui::font_provider::native_font_face(fallback.glyphs[index].font) != nullptr
            );
            TEST_EXPECT(context, fallback.glyphs[index].size > 0.0f);
            fallback_font_seen |= fallback.glyphs[index].font.handle != font.handle;
        }
        TEST_EXPECT(context, fallback_font_seen);

        gui::font_provider::ShapedText fallback_again = {};
        gui::font_provider::shape_text(font, 20.0f, "\xf0\x9f\x98\x80", arena, fallback_again);
        TEST_EXPECT(context, fallback_again.glyph_count == fallback.glyph_count);
        if (fallback.glyph_count != 0u && fallback_again.glyph_count != 0u) {
            TEST_EXPECT(
                context, fallback_again.glyphs[0u].font.handle == fallback.glyphs[0u].font.handle
            );
        }

        gui::font_provider::ShapedText rtl = {};
        gui::font_provider::shape_text(font, 20.0f, "\xd7\x90\xd7\x91", arena, rtl);
        TEST_EXPECT(context, rtl.glyph_count >= 2u);
        if (rtl.glyph_count >= 2u) {
            TEST_EXPECT(context, rtl.glyphs[0u].cluster > rtl.glyphs[1u].cluster);
        }

        gui::font_provider::ShapedText rtl_mark = {};
        gui::font_provider::shape_text(font, 20.0f, "\xd7\x90\xd6\xb7\xd7\x91", arena, rtl_mark);
        TEST_EXPECT(context, rtl_mark.glyph_count >= 2u);
        if (rtl_mark.glyph_count >= 2u) {
            TEST_EXPECT(
                context,
                rtl_mark.glyphs[0u].cluster > rtl_mark.glyphs[rtl_mark.glyph_count - 1u].cluster
            );
        }

        char const* const consolas_path = "C:\\Windows\\Fonts\\consola.ttf";
        if (file_exists(consolas_path)) {
            gui::font_provider::FontDesc file_desc = {};
            file_desc.file_path = consolas_path;
            gui::font_provider::Font file_font = {};
            gui::font_provider::open_font(owner_arena, provider, file_desc, file_font);

            gui::font_provider::ShapedText file_fallback = {};
            gui::font_provider::shape_text(
                file_font, 20.0f, "\xf0\x9f\x98\x80", arena, file_fallback
            );
            TEST_EXPECT(context, !shaped_has_missing_glyph(file_fallback));
            bool file_fallback_font_seen = false;
            for (size_t index = 0u; index < file_fallback.glyph_count; ++index) {
                file_fallback_font_seen |=
                    file_fallback.glyphs[index].font.handle != file_font.handle;
            }
            TEST_EXPECT(context, file_fallback_font_seen);

            gui::font_provider::ShapedText keycap_fallback = {};
            gui::font_provider::shape_text(
                file_font, 20.0f, "1\xef\xb8\x8f\xe2\x83\xa3", arena, keycap_fallback
            );
            TEST_EXPECT(context, !shaped_has_missing_glyph(keycap_fallback));
            bool keycap_base_fallback = false;
            for (size_t index = 0u; index < keycap_fallback.glyph_count; ++index) {
                keycap_base_fallback |=
                    keycap_fallback.glyphs[index].cluster == 0u &&
                    keycap_fallback.glyphs[index].font.handle != file_font.handle;
            }
            TEST_EXPECT(context, keycap_base_fallback);

            gui::font_provider::ShapedText variation_fallback = {};
            gui::font_provider::shape_text(
                file_font, 20.0f, "\xe2\x9d\xa4\xef\xb8\x8f", arena, variation_fallback
            );
            TEST_EXPECT(context, variation_fallback.glyph_count > 0u);
            TEST_EXPECT(context, !shaped_has_missing_glyph(variation_fallback));

            gui::font_provider::ShapedText zwj_fallback = {};
            gui::font_provider::shape_text(
                file_font,
                20.0f,
                "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
                arena,
                zwj_fallback
            );
            TEST_EXPECT(context, zwj_fallback.glyph_count > 0u);
            TEST_EXPECT(context, !shaped_has_missing_glyph(zwj_fallback));
            gui::font_provider::close_font(file_font);
        }

        gui::font_provider::GlyphRaster glyph = {};
        gui::font_provider::raster_glyph(font, 20.0f, shaped.glyphs[0u].glyph_index, arena, glyph);
        TEST_EXPECT(context, glyph.size.width > 0u);
        TEST_EXPECT(context, glyph.size.height > 0u);
        TEST_EXPECT(context, glyph.stride == glyph.size.width * 4u);
        TEST_EXPECT(context, glyph.pixels != nullptr);
        TEST_EXPECT(context, glyph.format == gui::font_provider::RasterFormat::LCD_RGB);
        TEST_EXPECT(context, glyph_has_antialias_coverage(glyph));
        TEST_EXPECT(context, glyph_has_lcd_coverage(glyph));

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
        TEST_EXPECT(context, phased_glyph.format == gui::font_provider::RasterFormat::LCD_RGB);

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
