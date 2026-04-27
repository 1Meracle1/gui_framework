#include <base/config.h>
#include <font_provider/font_provider.h>
#include <test/test.h>

namespace {

    [[nodiscard]] auto raster_has_alpha(gui::font_provider::RasterResult const& raster) -> bool {
        if (raster.rgba_pixels == nullptr) {
            return false;
        }

        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const line =
                raster.rgba_pixels + (static_cast<size_t>(y) * raster.stride);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                if (line[(static_cast<size_t>(x) * 4u) + 3u] != 0u) {
                    return true;
                }
            }
        }

        return false;
    }

    TEST_CASE(font_provider_result_helpers_classify_status_and_errors) {
        TEST_EXPECT(context, gui::font_provider::result_succeeded(gui::font_provider::Result::OK));
        TEST_EXPECT(context, !gui::font_provider::result_failed(gui::font_provider::Result::OK));
        TEST_EXPECT(
            context,
            gui::font_provider::result_failed(gui::font_provider::Result::INVALID_ARGUMENT));
        TEST_EXPECT(context,
                    gui::font_provider::result_name(gui::font_provider::Result::OK)[0] != '\0');
        TEST_EXPECT(context,
                    gui::font_provider::backend_name(gui::font_provider::Backend::DEFAULT)[0] !=
                        '\0');
    }

    TEST_CASE(font_provider_handles_start_empty_and_validate_by_handle_value) {
        gui::font_provider::Context const provider = {};
        gui::font_provider::Font const font = {};
        Arena owner_arena = {};

        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
        TEST_EXPECT(context, !gui::font_provider::font_valid(font));
        TEST_EXPECT(context, gui::font_provider::native_factory(provider) == nullptr);
        TEST_EXPECT(context, gui::font_provider::native_font_face(font) == nullptr);
        TEST_EXPECT(context,
                    gui::font_provider::create_context(owner_arena, {}, nullptr) ==
                        gui::font_provider::Result::INVALID_ARGUMENT);
        TEST_EXPECT(context,
                    gui::font_provider::open_font(owner_arena, provider, {}, nullptr) ==
                        gui::font_provider::Result::INVALID_ARGUMENT);
    }

    TEST_CASE(font_provider_can_create_default_font_and_raster_text_on_supported_platforms) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const create_result =
            gui::font_provider::create_context(owner_arena, {}, &provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, create_result == gui::font_provider::Result::OK);
        TEST_EXPECT(context, gui::font_provider::context_valid(provider));
        TEST_EXPECT(context, gui::font_provider::native_factory(provider) != nullptr);

        gui::font_provider::Font font = {};
        TEST_EXPECT(context,
                    gui::font_provider::open_font(owner_arena, provider, {}, &font) ==
                        gui::font_provider::Result::OK);
        TEST_EXPECT(context, gui::font_provider::font_valid(font));
        TEST_EXPECT(context, gui::font_provider::native_font_face(font) != nullptr);

        gui::font_provider::Metrics metrics = {};
        TEST_EXPECT(context,
                    gui::font_provider::metrics_from_font(font, 16.0f, &metrics) ==
                        gui::font_provider::Result::OK);
        TEST_EXPECT(context, metrics.ascent > 0.0f);
        TEST_EXPECT(context, metrics.descent >= 0.0f);

        Arena arena = {};
        arena.init({4u * 1024u * 1024u, DEFAULT_ARENA_COMMIT_SIZE});
        gui::font_provider::RasterResult raster = {};
        TEST_EXPECT(context,
                    gui::font_provider::raster_text(font, 16.0f, "hello", arena, &raster) ==
                        gui::font_provider::Result::OK);
        TEST_EXPECT(context, raster.size.width > 0u);
        TEST_EXPECT(context, raster.size.height > 0u);
        TEST_EXPECT(context, raster.stride >= raster.size.width * 4u);
        TEST_EXPECT(context, raster.rgba_pixels != nullptr);
        TEST_EXPECT(context, raster.advance > 0.0f);
        TEST_EXPECT(context, raster_has_alpha(raster));
        arena.destroy();

        gui::font_provider::close_font(&font);
        TEST_EXPECT(context, !gui::font_provider::font_valid(font));
        gui::font_provider::destroy_context(&provider);
        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
#else
        TEST_EXPECT(context, create_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
        TEST_EXPECT(context, !gui::font_provider::context_valid(provider));
#endif
    }

} // namespace

TEST_MAIN()
