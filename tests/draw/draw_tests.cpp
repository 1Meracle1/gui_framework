#include <base/config.h>
#include <draw/draw.h>
#include <test/test.h>

namespace {

    TEST_CASE(draw_context_starts_empty) {
        gui::draw::Context const draw_context = {};

        TEST_EXPECT(context, !gui::draw::context_valid(draw_context));
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::text_command(draw_context, 0u) == nullptr);
    }

    TEST_CASE(draw_records_text_commands_on_supported_platforms) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(owner_arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(owner_arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, {}, font);

        gui::draw::Context draw_context = {};
        gui::draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = cache;
        gui::draw::create_context(owner_arena, draw_desc, draw_context);
        TEST_EXPECT(context, gui::draw::context_valid(draw_context));

        gui::draw::begin_frame(draw_context);
        gui::draw::TextStyle style = {};
        style.font = font;
        style.size = 20.0f;
        float advance = 0.0f;
        gui::draw::draw_text(draw_context, {8.0f, 12.0f}, style, "draw text", &advance);
        TEST_EXPECT(context, advance > 0.0f);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 1u);

        gui::draw::TextCommand const* command = gui::draw::text_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->position.x == 8.0f);
        TEST_EXPECT(context, command->position.y == 12.0f);
        TEST_EXPECT(context, command->text == StrRef("draw text"));
        TEST_EXPECT(context, command->run.rgba_pixels != nullptr);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

} // namespace

TEST_MAIN()
