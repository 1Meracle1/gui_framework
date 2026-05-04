#include <base/config.h>
#include <font_cache/font_cache.h>
#include <test/test.h>

namespace {

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
        TEST_EXPECT(context, first.glyphs == second.glyphs);
        TEST_EXPECT(context, first.glyph_count > 2u);
        TEST_EXPECT(context, first.glyphs[1u].glyph_index != 0u);
        TEST_EXPECT(context, first.glyphs[1u].x > first.glyphs[0u].x);
        TEST_EXPECT(context, first.glyphs[2u].x > first.glyphs[1u].x);
        TEST_EXPECT(context, first.format == gui::font_provider::RasterFormat::ALPHA);
        TEST_EXPECT(
            context, first.glyphs[0u].raster.format == gui::font_provider::RasterFormat::ALPHA
        );
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

        gui::font_cache::destroy_cache(cache);
        TEST_EXPECT(context, !gui::font_cache::cache_valid(cache));
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

} // namespace

TEST_MAIN()
