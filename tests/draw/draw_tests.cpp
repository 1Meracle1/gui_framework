#include <base/config.h>
#include <draw/draw.h>
#include <draw/draw_renderer.h>
#include <test/test.h>

namespace {

    auto expect_position(test::Context* context, gui::draw::Vec2 position, float x, float y)
        -> void {
        TEST_EXPECT(context, position.x == x);
        TEST_EXPECT(context, position.y == y);
    }

    auto expect_rect(test::Context* context, gui::draw::Rect rect, gui::draw::Rect expected)
        -> void {
        expect_position(context, rect.min, expected.min.x, expected.min.y);
        expect_position(context, rect.max, expected.max.x, expected.max.y);
    }

    auto expect_transform(
        test::Context* context, gui::draw::Transform2D transform, gui::draw::Transform2D expected
    ) -> void {
        expect_position(context, transform.x_axis, expected.x_axis.x, expected.x_axis.y);
        expect_position(context, transform.y_axis, expected.y_axis.x, expected.y_axis.y);
        expect_position(
            context, transform.translation, expected.translation.x, expected.translation.y
        );
    }

    TEST_CASE(draw_context_starts_empty) {
        gui::draw::Context const draw_context = {};

        TEST_EXPECT(context, !gui::draw::context_valid(draw_context));
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::primitive_command(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::primitive_batch(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::command(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::layer_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::layer_command(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::styled_rect_command(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::text_command(draw_context, 0u) == nullptr);
        TEST_EXPECT(context, gui::draw::top_clip_rect(draw_context).min.x < -1000000.0f);
        expect_transform(context, gui::draw::top_transform(draw_context), {});
        TEST_EXPECT(context, gui::draw::top_opacity(draw_context) == 1.0f);
    }

    TEST_CASE(draw_renderer_handle_starts_empty) {
        gui::draw::RendererDesc const desc = {};
        gui::draw::Renderer const renderer = {};

        BASE_UNUSED(desc);
        TEST_EXPECT(context, !gui::draw::renderer_valid(renderer));
    }

    TEST_CASE(draw_renderer_create_builds_pipelines) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::render::Context render_context = {};
        gui::render::Result const context_result =
            gui::render::create_context(owner_arena, {}, render_context);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, context_result == gui::render::Result::OK);
        if (context_result == gui::render::Result::OK) {
            gui::draw::Renderer renderer = {};
            gui::render::Result const renderer_result =
                gui::draw::create_renderer(owner_arena, render_context, {}, renderer);
            TEST_EXPECT(context, renderer_result == gui::render::Result::OK);
            TEST_EXPECT(context, gui::draw::renderer_valid(renderer));
            if (gui::draw::renderer_valid(renderer)) {
                gui::draw::destroy_renderer(render_context, renderer);
            }
            gui::render::destroy_context(render_context);
        }
#else
        TEST_EXPECT(context, context_result == gui::render::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(draw_renderer_create_builds_d3d12_pipelines) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::render::ContextDesc desc = {};
        desc.backend = gui::render::Backend::D3D12;

        gui::render::Context render_context = {};
        gui::render::Result const context_result =
            gui::render::create_context(owner_arena, desc, render_context);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, context_result == gui::render::Result::OK);
        if (context_result == gui::render::Result::OK) {
            gui::draw::Renderer renderer = {};
            gui::render::Result const renderer_result =
                gui::draw::create_renderer(owner_arena, render_context, {}, renderer);
            TEST_EXPECT(context, renderer_result == gui::render::Result::OK);
            TEST_EXPECT(context, gui::draw::renderer_valid(renderer));
            if (gui::draw::renderer_valid(renderer)) {
                gui::draw::destroy_renderer(render_context, renderer);
            }
            gui::render::destroy_context(render_context);
        }
#else
        TEST_EXPECT(context, context_result == gui::render::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(draw_state_stacks_push_pop_top) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const clip0 = {{1.0f, 2.0f}, {8.0f, 9.0f}};
        gui::draw::Rect const clip1 = {{5.0f, 6.0f}, {10.0f, 12.0f}};
        gui::draw::Rect const clip01 = {{5.0f, 6.0f}, {8.0f, 9.0f}};
        gui::draw::push_clip_rect(draw_context, clip0);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clip0);
        gui::draw::push_clip_rect(draw_context, clip1);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clip01);
        expect_rect(context, gui::draw::pop_clip_rect(draw_context), clip01);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clip0);
        expect_rect(context, gui::draw::pop_clip_rect(draw_context), clip0);
        TEST_EXPECT(context, gui::draw::top_clip_rect(draw_context).min.x < -1000000.0f);

        gui::draw::Transform2D const transform0 = {{2.0f, 0.0f}, {0.0f, 3.0f}, {4.0f, 5.0f}};
        gui::draw::Transform2D const transform1 = {{0.0f, 1.0f}, {-1.0f, 0.0f}, {8.0f, 9.0f}};
        expect_transform(context, gui::draw::push_transform(draw_context, transform0), {});
        expect_transform(context, gui::draw::top_transform(draw_context), transform0);
        expect_transform(context, gui::draw::push_transform(draw_context, transform1), transform0);
        expect_transform(context, gui::draw::pop_transform(draw_context), transform1);
        expect_transform(context, gui::draw::top_transform(draw_context), transform0);
        expect_transform(context, gui::draw::pop_transform(draw_context), transform0);
        expect_transform(context, gui::draw::top_transform(draw_context), {});

        TEST_EXPECT(context, gui::draw::push_opacity(draw_context, 0.5f) == 1.0f);
        TEST_EXPECT(context, gui::draw::top_opacity(draw_context) == 0.5f);
        TEST_EXPECT(context, gui::draw::push_opacity(draw_context, 0.25f) == 0.5f);
        TEST_EXPECT(context, gui::draw::pop_opacity(draw_context) == 0.25f);
        TEST_EXPECT(context, gui::draw::top_opacity(draw_context) == 0.5f);
        TEST_EXPECT(context, gui::draw::pop_opacity(draw_context) == 0.5f);
        TEST_EXPECT(context, gui::draw::top_opacity(draw_context) == 1.0f);

        gui::draw::push_clip_rect(draw_context, clip0);
        gui::draw::push_transform(draw_context, transform0);
        gui::draw::push_opacity(draw_context, 0.5f);
        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::top_clip_rect(draw_context).min.x < -1000000.0f);
        expect_transform(context, gui::draw::top_transform(draw_context), {});
        TEST_EXPECT(context, gui::draw::top_opacity(draw_context) == 1.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_nested_clip_rects_intersect_and_pop_restore) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const outer = {{10.0f, 20.0f}, {110.0f, 120.0f}};
        gui::draw::Rect const inner = {{40.0f, 0.0f}, {140.0f, 80.0f}};
        gui::draw::Rect const clipped = {{40.0f, 20.0f}, {110.0f, 80.0f}};

        gui::draw::push_clip_rect(draw_context, outer);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), outer);
        expect_rect(context, gui::draw::push_clip_rect(draw_context, inner), outer);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clipped);
        expect_rect(context, gui::draw::pop_clip_rect(draw_context), clipped);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), outer);
        expect_rect(context, gui::draw::pop_clip_rect(draw_context), outer);
        TEST_EXPECT(context, gui::draw::top_clip_rect(draw_context).min.x < -1000000.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_filled_rect_primitive_vertices) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{1.0f, 2.0f}, {11.0f, 7.0f}}, {0.25f, 0.5f, 0.75f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 1u);

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->vertex_count == 30u);
        TEST_EXPECT(context, !gui::render::texture_valid(command->texture));
        TEST_EXPECT(context, command->vertices[0u].position.x == 1.0f);
        TEST_EXPECT(context, command->vertices[0u].position.y == 2.0f);
        TEST_EXPECT(context, command->vertices[0u].uv.x == 0.0f);
        TEST_EXPECT(context, command->vertices[0u].uv.y == 0.0f);
        TEST_EXPECT(context, command->vertices[2u].position.x == 11.0f);
        TEST_EXPECT(context, command->vertices[2u].position.y == 7.0f);
        TEST_EXPECT(context, command->vertices[5u].color.b == 0.75f);
        expect_position(context, command->vertices[8u].position, 12.0f, 1.0f);
        expect_position(context, command->vertices[11u].position, 0.0f, 1.0f);
        TEST_EXPECT(context, command->vertices[6u].color.a == 1.0f);
        TEST_EXPECT(context, command->vertices[8u].color.a == 0.0f);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_direct_filled_triangle_and_quad_aa) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_triangle_filled(
            draw_context, {0.0f, 0.0f}, {4.0f, 0.0f}, {0.0f, 4.0f}, {1.0f, 0.5f, 0.25f, 1.0f}
        );
        gui::draw::draw_quad_filled(
            draw_context,
            {6.0f, 0.0f},
            {10.0f, 0.0f},
            {10.0f, 4.0f},
            {6.0f, 4.0f},
            {0.25f, 0.5f, 1.0f, 1.0f}
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 2u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 1u);

        gui::draw::PrimitiveBatch const* batch = gui::draw::primitive_batch(draw_context, 0u);
        TEST_EXPECT(context, batch != nullptr);
        TEST_EXPECT(context, batch->command_count == 2u);
        TEST_EXPECT(context, batch->vertex_count == 51u);

        gui::draw::PrimitiveCommand const* triangle_command =
            gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, triangle_command != nullptr);
        TEST_EXPECT(context, triangle_command->vertex_count == 21u);
        expect_position(context, triangle_command->vertices[0u].position, 0.0f, 0.0f);
        expect_position(context, triangle_command->vertices[1u].position, 4.0f, 0.0f);
        expect_position(context, triangle_command->vertices[2u].position, 0.0f, 4.0f);
        TEST_EXPECT(context, triangle_command->vertices[3u].color.a == 1.0f);
        TEST_EXPECT(context, triangle_command->vertices[5u].color.a == 0.0f);

        gui::draw::PrimitiveCommand const* quad_command =
            gui::draw::primitive_command(draw_context, 1u);
        TEST_EXPECT(context, quad_command != nullptr);
        TEST_EXPECT(context, quad_command->vertex_count == 30u);
        expect_position(context, quad_command->vertices[0u].position, 6.0f, 0.0f);
        expect_position(context, quad_command->vertices[2u].position, 10.0f, 4.0f);
        expect_position(context, quad_command->vertices[5u].position, 6.0f, 4.0f);
        TEST_EXPECT(context, quad_command->vertices[6u].color.a == 1.0f);
        TEST_EXPECT(context, quad_command->vertices[8u].color.a == 0.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_batches_adjacent_compatible_primitive_commands) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture const texture = {&texture_storage};

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f
        );
        gui::draw::draw_triangle_filled(
            draw_context, {6.0f, 0.0f}, {10.0f, 0.0f}, {6.0f, 4.0f}, {0.0f, 1.0f, 0.0f, 1.0f}
        );
        gui::draw::draw_image(
            draw_context,
            texture,
            {{12.0f, 0.0f}, {16.0f, 4.0f}},
            {{0.0f, 0.0f}, {1.0f, 1.0f}},
            {1.0f, 1.0f, 1.0f, 1.0f}
        );
        gui::draw::draw_image(
            draw_context,
            texture,
            {{18.0f, 0.0f}, {22.0f, 4.0f}},
            {{0.0f, 0.0f}, {1.0f, 1.0f}},
            {1.0f, 1.0f, 1.0f, 1.0f}
        );
        gui::draw::draw_rect_filled(
            draw_context, {{24.0f, 0.0f}, {28.0f, 4.0f}}, {0.0f, 0.0f, 1.0f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 5u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 3u);

        gui::draw::PrimitiveBatch const* solid_batch = gui::draw::primitive_batch(draw_context, 0u);
        TEST_EXPECT(context, solid_batch != nullptr);
        TEST_EXPECT(context, solid_batch->command_index == 0u);
        TEST_EXPECT(context, solid_batch->command_count == 2u);
        TEST_EXPECT(context, solid_batch->vertex_count == 51u);
        TEST_EXPECT(context, !gui::render::texture_valid(solid_batch->texture));

        gui::draw::PrimitiveBatch const* textured_batch =
            gui::draw::primitive_batch(draw_context, 1u);
        TEST_EXPECT(context, textured_batch != nullptr);
        TEST_EXPECT(context, textured_batch->command_index == 2u);
        TEST_EXPECT(context, textured_batch->command_count == 2u);
        TEST_EXPECT(context, textured_batch->vertex_count == 12u);
        TEST_EXPECT(context, textured_batch->texture.handle == texture.handle);

        gui::draw::PrimitiveBatch const* final_solid_batch =
            gui::draw::primitive_batch(draw_context, 2u);
        TEST_EXPECT(context, final_solid_batch != nullptr);
        TEST_EXPECT(context, final_solid_batch->command_index == 4u);
        TEST_EXPECT(context, final_solid_batch->command_count == 1u);
        TEST_EXPECT(context, final_solid_batch->vertex_count == 30u);

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 2u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->texture.handle == texture.handle);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_intersected_clip_metadata_for_primitives) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const outer = {{10.0f, 20.0f}, {110.0f, 120.0f}};
        gui::draw::Rect const inner = {{40.0f, 0.0f}, {140.0f, 80.0f}};
        gui::draw::Rect const clipped = {{40.0f, 20.0f}, {110.0f, 80.0f}};

        gui::draw::push_clip_rect(draw_context, outer);
        gui::draw::push_clip_rect(draw_context, inner);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::pop_clip_rect(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{6.0f, 0.0f}, {10.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 2u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 2u);

        gui::draw::PrimitiveCommand const* first_command =
            gui::draw::primitive_command(draw_context, 0u);
        gui::draw::PrimitiveBatch const* first_batch = gui::draw::primitive_batch(draw_context, 0u);
        TEST_EXPECT(context, first_command != nullptr);
        TEST_EXPECT(context, first_batch != nullptr);
        expect_rect(context, first_command->clip_rect, clipped);
        expect_rect(context, first_batch->clip_rect, clipped);

        gui::draw::PrimitiveCommand const* second_command =
            gui::draw::primitive_command(draw_context, 1u);
        gui::draw::PrimitiveBatch const* second_batch =
            gui::draw::primitive_batch(draw_context, 1u);
        TEST_EXPECT(context, second_command != nullptr);
        TEST_EXPECT(context, second_batch != nullptr);
        expect_rect(context, second_command->clip_rect, outer);
        expect_rect(context, second_batch->clip_rect, outer);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_clip_rects_are_screen_space_across_transforms) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::Transform2D const transform = {{2.0f, 0.0f}, {0.0f, 3.0f}, {10.0f, 20.0f}};

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const clip_after_transform = {{1.0f, 2.0f}, {5.0f, 6.0f}};
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_clip_rect(draw_context, clip_after_transform);
        gui::draw::draw_triangle_filled(
            draw_context, {1.0f, 2.0f}, {3.0f, 2.0f}, {1.0f, 4.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
        );

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        expect_rect(context, command->clip_rect, clip_after_transform);
        expect_transform(context, command->transform, transform);
        expect_position(context, command->vertices[0u].position, 12.0f, 26.0f);
        expect_position(context, command->vertices[2u].position, 12.0f, 32.0f);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const clip_before_transform = {{20.0f, 30.0f}, {60.0f, 70.0f}};
        gui::draw::push_clip_rect(draw_context, clip_before_transform);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::draw_triangle_filled(
            draw_context, {2.0f, 1.0f}, {4.0f, 1.0f}, {2.0f, 3.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
        );

        command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        expect_rect(context, command->clip_rect, clip_before_transform);
        expect_transform(context, command->transform, transform);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const outer = {{10.0f, 10.0f}, {80.0f, 80.0f}};
        gui::draw::Rect const inner = {{30.0f, 0.0f}, {100.0f, 40.0f}};
        gui::draw::Rect const clipped = {{30.0f, 10.0f}, {80.0f, 40.0f}};
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_clip_rect(draw_context, outer);
        gui::draw::push_clip_rect(draw_context, inner);
        gui::draw::draw_triangle_filled(
            draw_context, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
        );

        command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        expect_rect(context, command->clip_rect, clipped);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clipped);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_batches_break_on_state_changes) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        gui::draw::Rect const clip = {{1.0f, 1.0f}, {8.0f, 8.0f}};
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::draw_rect_filled(
            draw_context, {{6.0f, 0.0f}, {10.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::draw_rect_filled(
            draw_context, {{12.0f, 0.0f}, {16.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::pop_clip_rect(draw_context);

        gui::draw::Transform2D const transform = {{1.0f, 0.0f}, {0.0f, 1.0f}, {2.0f, 3.0f}};
        gui::draw::push_transform(draw_context, transform);
        gui::draw::draw_rect_filled(
            draw_context, {{18.0f, 0.0f}, {22.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::pop_transform(draw_context);

        gui::draw::push_opacity(draw_context, 0.5f);
        gui::draw::draw_rect_filled(
            draw_context, {{24.0f, 0.0f}, {28.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::pop_opacity(draw_context);

        gui::draw::draw_rect_filled(
            draw_context, {{30.0f, 0.0f}, {34.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 6u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 5u);

        gui::draw::PrimitiveBatch const* clip_batch = gui::draw::primitive_batch(draw_context, 1u);
        TEST_EXPECT(context, clip_batch != nullptr);
        TEST_EXPECT(context, clip_batch->command_index == 1u);
        TEST_EXPECT(context, clip_batch->command_count == 2u);
        TEST_EXPECT(context, clip_batch->vertex_count == 60u);
        expect_rect(context, clip_batch->clip_rect, clip);

        gui::draw::PrimitiveBatch const* transform_batch =
            gui::draw::primitive_batch(draw_context, 2u);
        TEST_EXPECT(context, transform_batch != nullptr);
        TEST_EXPECT(context, transform_batch->command_index == 3u);
        TEST_EXPECT(context, transform_batch->command_count == 1u);
        expect_transform(context, transform_batch->transform, transform);

        gui::draw::PrimitiveBatch const* opacity_batch =
            gui::draw::primitive_batch(draw_context, 3u);
        TEST_EXPECT(context, opacity_batch != nullptr);
        TEST_EXPECT(context, opacity_batch->command_index == 4u);
        TEST_EXPECT(context, opacity_batch->command_count == 1u);
        TEST_EXPECT(context, opacity_batch->opacity == 0.5f);

        gui::draw::PrimitiveBatch const* final_batch = gui::draw::primitive_batch(draw_context, 4u);
        TEST_EXPECT(context, final_batch != nullptr);
        TEST_EXPECT(context, final_batch->command_index == 5u);
        TEST_EXPECT(context, final_batch->command_count == 1u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_mixed_primitive_text_command_order) {
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

        gui::draw::TextStyle style = {};
        style.font = font;
        style.size = 16.0f;

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::draw_triangle_filled(
            draw_context, {6.0f, 0.0f}, {10.0f, 0.0f}, {6.0f, 4.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
        );
        gui::draw::draw_text(draw_context, {0.0f, 8.0f}, style, "middle", nullptr);
        gui::draw::draw_rect_filled(
            draw_context, {{12.0f, 0.0f}, {16.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );
        gui::draw::draw_text(draw_context, {0.0f, 24.0f}, style, "end", nullptr);
        gui::draw::draw_rect_filled(
            draw_context, {{18.0f, 0.0f}, {22.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 4u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 3u);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 2u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 5u);

        gui::draw::PrimitiveBatch const* first_batch = gui::draw::primitive_batch(draw_context, 0u);
        TEST_EXPECT(context, first_batch != nullptr);
        TEST_EXPECT(context, first_batch->command_index == 0u);
        TEST_EXPECT(context, first_batch->command_count == 2u);

        gui::draw::PrimitiveBatch const* middle_batch =
            gui::draw::primitive_batch(draw_context, 1u);
        TEST_EXPECT(context, middle_batch != nullptr);
        TEST_EXPECT(context, middle_batch->command_index == 2u);
        TEST_EXPECT(context, middle_batch->command_count == 1u);

        gui::draw::PrimitiveBatch const* final_batch = gui::draw::primitive_batch(draw_context, 2u);
        TEST_EXPECT(context, final_batch != nullptr);
        TEST_EXPECT(context, final_batch->command_index == 3u);
        TEST_EXPECT(context, final_batch->command_count == 1u);

        gui::draw::Command const* command0 = gui::draw::command(draw_context, 0u);
        gui::draw::Command const* command1 = gui::draw::command(draw_context, 1u);
        gui::draw::Command const* command2 = gui::draw::command(draw_context, 2u);
        gui::draw::Command const* command3 = gui::draw::command(draw_context, 3u);
        gui::draw::Command const* command4 = gui::draw::command(draw_context, 4u);
        TEST_EXPECT(context, command0 != nullptr);
        TEST_EXPECT(context, command0->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command0->index == 0u);
        TEST_EXPECT(context, command1 != nullptr);
        TEST_EXPECT(context, command1->kind == gui::draw::CommandKind::TEXT);
        TEST_EXPECT(context, command1->index == 0u);
        TEST_EXPECT(context, command2 != nullptr);
        TEST_EXPECT(context, command2->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command2->index == 1u);
        TEST_EXPECT(context, command3 != nullptr);
        TEST_EXPECT(context, command3->kind == gui::draw::CommandKind::TEXT);
        TEST_EXPECT(context, command3->index == 1u);
        TEST_EXPECT(context, command4 != nullptr);
        TEST_EXPECT(context, command4->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command4->index == 2u);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(draw_records_image_rect_texture_uvs_and_state) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture const texture = {&texture_storage};

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const clip = {{2.0f, 3.0f}, {20.0f, 30.0f}};
        gui::draw::Transform2D const transform = {{2.0f, 0.0f}, {0.0f, 3.0f}, {5.0f, 7.0f}};
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_opacity(draw_context, 0.25f);
        gui::draw::draw_image(
            draw_context,
            texture,
            {{1.0f, 2.0f}, {4.0f, 6.0f}},
            {{0.1f, 0.2f}, {0.7f, 0.8f}},
            {0.4f, 0.5f, 0.6f, 0.8f}
        );

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->texture.handle == texture.handle);
        TEST_EXPECT(context, command->vertex_count == 6u);
        expect_rect(context, command->clip_rect, clip);
        expect_transform(context, command->transform, transform);
        TEST_EXPECT(context, command->opacity == 0.25f);
        expect_position(context, command->vertices[0u].position, 7.0f, 13.0f);
        expect_position(context, command->vertices[2u].position, 13.0f, 25.0f);
        expect_position(context, command->vertices[0u].uv, 0.1f, 0.2f);
        expect_position(context, command->vertices[1u].uv, 0.7f, 0.2f);
        expect_position(context, command->vertices[2u].uv, 0.7f, 0.8f);
        expect_position(context, command->vertices[5u].uv, 0.1f, 0.8f);
        TEST_EXPECT(context, command->vertices[0u].color.a == 0.2f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_styled_rect_commands_and_order) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture const texture = {&texture_storage};

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        gui::draw::Rect const clip = {{2.0f, 3.0f}, {20.0f, 30.0f}};
        gui::draw::Transform2D const transform = {{2.0f, 0.0f}, {0.0f, 3.0f}, {5.0f, 7.0f}};
        gui::draw::BoxStyle style = {};
        style.fill_color = {0.2f, 0.3f, 0.4f, 0.8f};
        style.texture = texture;
        style.uv_rect = {{0.1f, 0.2f}, {0.7f, 0.8f}};
        style.border_color = {0.9f, 0.8f, 0.7f, 0.6f};
        style.border_thickness = 2.0f;
        style.radius = 4.0f;
        style.softness = 1.5f;
        style.shadow.offset = {3.0f, 4.0f};
        style.shadow.blur_radius = 8.0f;
        style.shadow.spread = 2.0f;
        style.shadow.color = {0.1f, 0.2f, 0.3f, 0.4f};
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_opacity(draw_context, 0.25f);
        gui::draw::draw_rect_styled(draw_context, {{1.0f, 2.0f}, {11.0f, 8.0f}}, style);
        gui::draw::pop_opacity(draw_context);
        gui::draw::pop_transform(draw_context);
        gui::draw::pop_clip_rect(draw_context);

        gui::draw::draw_triangle_filled(
            draw_context, {6.0f, 0.0f}, {10.0f, 0.0f}, {6.0f, 4.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 2u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 2u);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 1u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 3u);

        gui::draw::StyledRectCommand const* styled =
            gui::draw::styled_rect_command(draw_context, 0u);
        TEST_EXPECT(context, styled != nullptr);
        expect_rect(context, styled->rect, {{1.0f, 2.0f}, {11.0f, 8.0f}});
        expect_rect(context, styled->clip_rect, clip);
        expect_transform(context, styled->transform, transform);
        TEST_EXPECT(context, styled->opacity == 0.25f);
        TEST_EXPECT(context, styled->style.texture.handle == texture.handle);
        expect_rect(context, styled->style.uv_rect, style.uv_rect);
        TEST_EXPECT(context, styled->style.fill_color.g == 0.3f);
        TEST_EXPECT(context, styled->style.border_color.r == 0.9f);
        TEST_EXPECT(context, styled->style.border_thickness == 2.0f);
        TEST_EXPECT(context, styled->style.radius == 3.0f);
        TEST_EXPECT(context, styled->style.softness == 1.5f);
        TEST_EXPECT(context, styled->style.shadow.offset.x == 3.0f);
        TEST_EXPECT(context, styled->style.shadow.offset.y == 4.0f);
        TEST_EXPECT(context, styled->style.shadow.blur_radius == 8.0f);
        TEST_EXPECT(context, styled->style.shadow.spread == 2.0f);
        TEST_EXPECT(context, styled->style.shadow.color.g == 0.2f);
        TEST_EXPECT(context, !styled->style.shadow.inset);

        gui::draw::Command const* command0 = gui::draw::command(draw_context, 0u);
        gui::draw::Command const* command1 = gui::draw::command(draw_context, 1u);
        gui::draw::Command const* command2 = gui::draw::command(draw_context, 2u);
        TEST_EXPECT(context, command0 != nullptr);
        TEST_EXPECT(context, command0->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command0->index == 0u);
        TEST_EXPECT(context, command1 != nullptr);
        TEST_EXPECT(context, command1->kind == gui::draw::CommandKind::STYLED_RECT);
        TEST_EXPECT(context, command1->index == 0u);
        TEST_EXPECT(context, command2 != nullptr);
        TEST_EXPECT(context, command2->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command2->index == 1u);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_shadow_only_styled_rect_and_defaults) {
        gui::draw::BoxShadow const default_shadow = {};
        TEST_EXPECT(context, default_shadow.color.a == 0.0f);
        TEST_EXPECT(context, default_shadow.blur_radius == 0.0f);
        TEST_EXPECT(context, default_shadow.spread == 0.0f);
        TEST_EXPECT(context, !default_shadow.inset);

        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::BoxStyle style = {};
        style.fill_color = {0.0f, 0.0f, 0.0f, 0.0f};
        style.border_color = {0.0f, 0.0f, 0.0f, 0.0f};

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_styled(draw_context, {{0.0f, 0.0f}, {20.0f, 10.0f}}, style);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 0u);

        style.radius = 3.0f;
        style.shadow.offset = {2.0f, 3.0f};
        style.shadow.blur_radius = -4.0f;
        style.shadow.spread = 1.0f;
        style.shadow.color = {0.0f, 0.0f, 0.0f, 0.5f};
        gui::draw::draw_rect_styled(draw_context, {{0.0f, 0.0f}, {20.0f, 10.0f}}, style);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 1u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 1u);

        gui::draw::StyledRectCommand const* styled =
            gui::draw::styled_rect_command(draw_context, 0u);
        TEST_EXPECT(context, styled != nullptr);
        TEST_EXPECT(context, styled->style.shadow.offset.x == 2.0f);
        TEST_EXPECT(context, styled->style.shadow.offset.y == 3.0f);
        TEST_EXPECT(context, styled->style.shadow.blur_radius == 0.0f);
        TEST_EXPECT(context, styled->style.shadow.spread == 1.0f);
        TEST_EXPECT(context, styled->style.shadow.color.a == 0.5f);

        gui::draw::Command const* command = gui::draw::command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->kind == gui::draw::CommandKind::STYLED_RECT);
        TEST_EXPECT(context, command->index == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_layer_boundaries_and_group_opacity) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect_filled(
            draw_context, {{0.0f, 0.0f}, {4.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        gui::draw::Rect const clip = {{0.0f, 0.0f}, {50.0f, 50.0f}};
        gui::draw::Rect const bounds = {{10.0f, 10.0f}, {70.0f, 70.0f}};
        gui::draw::Rect const clipped_bounds = {{10.0f, 10.0f}, {50.0f, 50.0f}};
        gui::draw::LayerDesc layer = {};
        layer.bounds = bounds;
        layer.opacity = 0.5f;
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::push_layer(draw_context, layer);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clipped_bounds);
        gui::draw::draw_rect_filled(
            draw_context, {{12.0f, 12.0f}, {36.0f, 36.0f}}, {1.0f, 0.0f, 0.0f, 0.5f}, 0.0f
        );
        gui::draw::draw_rect_filled(
            draw_context, {{24.0f, 24.0f}, {48.0f, 48.0f}}, {0.0f, 0.0f, 1.0f, 0.5f}, 0.0f
        );
        gui::draw::pop_layer(draw_context);
        expect_rect(context, gui::draw::top_clip_rect(draw_context), clip);
        gui::draw::pop_clip_rect(draw_context);

        gui::draw::draw_rect_filled(
            draw_context, {{80.0f, 0.0f}, {84.0f, 4.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f
        );

        TEST_EXPECT(context, gui::draw::layer_command_count(draw_context) == 1u);
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 4u);
        TEST_EXPECT(context, gui::draw::primitive_batch_count(draw_context) == 3u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 5u);

        gui::draw::LayerCommand const* layer_command = gui::draw::layer_command(draw_context, 0u);
        TEST_EXPECT(context, layer_command != nullptr);
        expect_rect(context, layer_command->desc.bounds, bounds);
        expect_rect(context, layer_command->clip_rect, clipped_bounds);
        TEST_EXPECT(context, layer_command->desc.opacity == 0.5f);
        TEST_EXPECT(context, layer_command->desc.clip_radius == 0.0f);
        TEST_EXPECT(context, layer_command->desc.blend_mode == gui::draw::LayerBlendMode::NORMAL);
        TEST_EXPECT(context, layer_command->desc.isolated);
        TEST_EXPECT(context, layer_command->desc.filter_kind == gui::draw::FilterKind::NONE);
        TEST_EXPECT(context, layer_command->desc.filter_radius == 0.0f);
        TEST_EXPECT(context, layer_command->desc.drop_shadow.color.a == 0.0f);
        TEST_EXPECT(context, layer_command->begin_command_index == 1u);
        TEST_EXPECT(context, layer_command->end_command_index == 3u);

        gui::draw::PrimitiveCommand const* first_child =
            gui::draw::primitive_command(draw_context, 1u);
        gui::draw::PrimitiveCommand const* second_child =
            gui::draw::primitive_command(draw_context, 2u);
        TEST_EXPECT(context, first_child != nullptr);
        TEST_EXPECT(context, second_child != nullptr);
        expect_rect(context, first_child->clip_rect, clipped_bounds);
        expect_rect(context, second_child->clip_rect, clipped_bounds);
        TEST_EXPECT(context, first_child->vertices[0u].color.a == 0.5f);
        TEST_EXPECT(context, second_child->vertices[0u].color.a == 0.5f);

        gui::draw::Command const* command0 = gui::draw::command(draw_context, 0u);
        gui::draw::Command const* command1 = gui::draw::command(draw_context, 1u);
        gui::draw::Command const* command2 = gui::draw::command(draw_context, 2u);
        gui::draw::Command const* command3 = gui::draw::command(draw_context, 3u);
        gui::draw::Command const* command4 = gui::draw::command(draw_context, 4u);
        TEST_EXPECT(context, command0 != nullptr);
        TEST_EXPECT(context, command0->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command1 != nullptr);
        TEST_EXPECT(context, command1->kind == gui::draw::CommandKind::LAYER_BEGIN);
        TEST_EXPECT(context, command1->index == 0u);
        TEST_EXPECT(context, command2 != nullptr);
        TEST_EXPECT(context, command2->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);
        TEST_EXPECT(context, command3 != nullptr);
        TEST_EXPECT(context, command3->kind == gui::draw::CommandKind::LAYER_END);
        TEST_EXPECT(context, command3->index == 0u);
        TEST_EXPECT(context, command4 != nullptr);
        TEST_EXPECT(context, command4->kind == gui::draw::CommandKind::PRIMITIVE_BATCH);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::layer_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_layer_filter_and_drop_shadow) {
        gui::draw::LayerDesc const default_layer = {};
        TEST_EXPECT(context, default_layer.blend_mode == gui::draw::LayerBlendMode::NORMAL);
        TEST_EXPECT(context, default_layer.isolated);
        TEST_EXPECT(context, default_layer.clip_radius == 0.0f);
        TEST_EXPECT(context, default_layer.filter_kind == gui::draw::FilterKind::NONE);
        TEST_EXPECT(context, default_layer.filter_radius == 0.0f);
        TEST_EXPECT(context, default_layer.drop_shadow.blur_radius == 0.0f);
        TEST_EXPECT(context, default_layer.drop_shadow.color.a == 0.0f);

        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::LayerDesc layer = {};
        layer.bounds = {{0.0f, 0.0f}, {80.0f, 60.0f}};
        layer.blend_mode = gui::draw::LayerBlendMode::SCREEN;
        // Layers are currently always rendered as isolated groups.
        layer.isolated = false;
        layer.clip_radius = 12.0f;
        layer.filter_kind = gui::draw::FilterKind::BLUR;
        layer.filter_radius = 6.0f;
        layer.drop_shadow.offset = {3.0f, 4.0f};
        layer.drop_shadow.blur_radius = 5.0f;
        layer.drop_shadow.color = {0.1f, 0.2f, 0.3f, 0.4f};
        gui::draw::push_layer(draw_context, layer);
        gui::draw::pop_layer(draw_context);

        gui::draw::LayerCommand const* command = gui::draw::layer_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->desc.blend_mode == gui::draw::LayerBlendMode::SCREEN);
        TEST_EXPECT(context, command->desc.isolated);
        TEST_EXPECT(context, command->desc.clip_radius == 12.0f);
        TEST_EXPECT(context, command->desc.filter_kind == gui::draw::FilterKind::BLUR);
        TEST_EXPECT(context, command->desc.filter_radius == 6.0f);
        TEST_EXPECT(context, command->desc.drop_shadow.offset.x == 3.0f);
        TEST_EXPECT(context, command->desc.drop_shadow.offset.y == 4.0f);
        TEST_EXPECT(context, command->desc.drop_shadow.blur_radius == 5.0f);
        TEST_EXPECT(context, command->desc.drop_shadow.color.g == 0.2f);
        TEST_EXPECT(
            context,
            gui::draw::LayerBlendMode::PREMULTIPLIED_NORMAL != gui::draw::LayerBlendMode::ADDITIVE
        );
        TEST_EXPECT(
            context, gui::draw::LayerBlendMode::MULTIPLY != gui::draw::LayerBlendMode::SCREEN
        );

        gui::draw::begin_frame(draw_context);
        layer.clip_radius = 100.0f;
        layer.filter_radius = -1.0f;
        layer.drop_shadow.blur_radius = -2.0f;
        gui::draw::push_layer(draw_context, layer);
        gui::draw::pop_layer(draw_context);
        command = gui::draw::layer_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->desc.clip_radius == 30.0f);
        TEST_EXPECT(context, command->desc.filter_radius == 0.0f);
        TEST_EXPECT(context, command->desc.drop_shadow.blur_radius == 0.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_stamps_state_and_applies_transform_opacity_to_primitives) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::Rect const clip = {{2.0f, 3.0f}, {20.0f, 30.0f}};
        gui::draw::Transform2D const transform = {{2.0f, 0.0f}, {0.0f, 3.0f}, {5.0f, 7.0f}};
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_opacity(draw_context, 0.25f);
        gui::draw::draw_rect_filled(
            draw_context, {{1.0f, 2.0f}, {4.0f, 6.0f}}, {0.4f, 0.5f, 0.6f, 0.8f}, 0.0f
        );

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        expect_rect(context, command->clip_rect, clip);
        expect_transform(context, command->transform, transform);
        TEST_EXPECT(context, command->opacity == 0.25f);
        expect_position(context, command->vertices[0u].position, 7.0f, 13.0f);
        expect_position(context, command->vertices[2u].position, 13.0f, 25.0f);
        expect_position(context, command->vertices[8u].position, 15.0f, 10.0f);
        TEST_EXPECT(context, command->vertices[0u].color.a == 0.2f);
        TEST_EXPECT(context, command->vertices[8u].color.a == 0.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_open_polyline_with_joined_corner) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_polyline(
            draw_context,
            {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}},
            {1.0f, 1.0f, 1.0f, 1.0f},
            2.0f,
            false
        );

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 1u);

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->vertex_count == 48u);
        expect_position(context, command->vertices[1u].position, 9.0f, 1.0f);
        expect_position(context, command->vertices[6u].position, 9.0f, 1.0f);
        expect_position(context, command->vertices[2u].position, 11.0f, -1.0f);
        expect_position(context, command->vertices[11u].position, 11.0f, -1.0f);
        TEST_EXPECT(context, command->vertices[12u].color.a == 1.0f);
        TEST_EXPECT(context, command->vertices[14u].color.a == 0.0f);
        expect_position(context, command->vertices[36u].position, 0.0f, -1.0f);
        expect_position(context, command->vertices[38u].position, -1.0f, 2.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_stroke_aa_records_caps_subpixel_alpha_and_state) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::Rect const clip = {{0.0f, 0.0f}, {20.0f, 20.0f}};
        gui::draw::Transform2D const transform = {{1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 2.0f}};

        gui::draw::begin_frame(draw_context);
        gui::draw::push_clip_rect(draw_context, clip);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_opacity(draw_context, 0.5f);
        gui::draw::draw_line(
            draw_context, {0.0f, 0.0f}, {10.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0.5f
        );

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->vertex_count == 30u);
        expect_rect(context, command->clip_rect, clip);
        expect_transform(context, command->transform, transform);
        TEST_EXPECT(context, command->opacity == 0.5f);
        expect_position(context, command->vertices[0u].position, 1.0f, 2.5f);
        expect_position(context, command->vertices[8u].position, 11.0f, 3.5f);
        expect_position(context, command->vertices[20u].position, 0.0f, 3.5f);
        TEST_EXPECT(context, command->vertices[0u].color.a == 0.25f);
        TEST_EXPECT(context, command->vertices[8u].color.a == 0.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_closed_triangle_and_rect_strokes) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_triangle(
            draw_context, {0.0f, 0.0f}, {10.0f, 0.0f}, {0.0f, 10.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f
        );

        gui::draw::PrimitiveCommand const* triangle_command =
            gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, triangle_command != nullptr);
        TEST_EXPECT(context, triangle_command->vertex_count == 54u);
        expect_position(context, triangle_command->vertices[0u].position, 1.0f, 1.0f);
        expect_position(context, triangle_command->vertices[13u].position, 1.0f, 1.0f);
        expect_position(context, triangle_command->vertices[14u].position, -1.0f, -1.0f);
        TEST_EXPECT(context, triangle_command->vertices[18u].color.a == 1.0f);
        TEST_EXPECT(context, triangle_command->vertices[20u].color.a == 0.0f);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_rect(
            draw_context, {{0.0f, 0.0f}, {10.0f, 8.0f}}, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f, 0.0f
        );

        gui::draw::PrimitiveCommand const* rect_command =
            gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, rect_command != nullptr);
        TEST_EXPECT(context, rect_command->vertex_count == 72u);
        expect_position(context, rect_command->vertices[0u].position, 1.0f, 1.0f);
        expect_position(context, rect_command->vertices[19u].position, 1.0f, 1.0f);
        expect_position(context, rect_command->vertices[20u].position, -1.0f, -1.0f);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_stroke_skips_degenerate_segments) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_polyline(
            draw_context,
            {{0.0f, 0.0f}, {0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 4.0f}},
            {1.0f, 1.0f, 1.0f, 1.0f},
            2.0f,
            false
        );

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->vertex_count == 48u);
        expect_position(context, command->vertices[1u].position, 7.0f, 1.0f);
        expect_position(context, command->vertices[6u].position, 7.0f, 1.0f);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_polyline(
            draw_context,
            {{2.0f, 3.0f}, {2.0f, 3.0f}, {2.0f, 3.0f}},
            {1.0f, 1.0f, 1.0f, 1.0f},
            2.0f,
            false
        );
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_fill_convex_compacts_degenerate_edges) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::path_line_to(draw_context, {0.0f, 0.0f});
        gui::draw::path_line_to(draw_context, {0.0f, 0.0f});
        gui::draw::path_line_to(draw_context, {4.0f, 0.0f});
        gui::draw::path_line_to(draw_context, {0.0f, 4.0f});
        gui::draw::path_line_to(draw_context, {0.0f, 0.0f});
        gui::draw::path_fill_convex(draw_context, {1.0f, 1.0f, 1.0f, 1.0f});

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->vertex_count == 21u);
        expect_position(context, command->vertices[0u].position, 0.0f, 0.0f);
        expect_position(context, command->vertices[1u].position, 4.0f, 0.0f);
        expect_position(context, command->vertices[2u].position, 0.0f, 4.0f);
        TEST_EXPECT(context, command->vertices[3u].color.a == 1.0f);
        TEST_EXPECT(context, command->vertices[5u].color.a == 0.0f);

        gui::draw::begin_frame(draw_context);
        gui::draw::path_line_to(draw_context, {2.0f, 3.0f});
        gui::draw::path_line_to(draw_context, {2.0f, 3.0f});
        gui::draw::path_line_to(draw_context, {2.0f, 3.0f});
        gui::draw::path_fill_convex(draw_context, {1.0f, 1.0f, 1.0f, 1.0f});
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_circle_and_ellipse_segment_counts_are_clamped) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_circle_filled(
            draw_context, {4.0f, 4.0f}, 8.0f, {1.0f, 1.0f, 1.0f, 1.0f}, 1
        );
        gui::draw::draw_ellipse(
            draw_context, {16.0f, 16.0f}, {8.0f, 4.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f, 200
        );

        gui::draw::PrimitiveCommand const* circle_command =
            gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, circle_command != nullptr);
        TEST_EXPECT(context, circle_command->vertex_count == 21u);

        gui::draw::PrimitiveCommand const* ellipse_command =
            gui::draw::primitive_command(draw_context, 1u);
        TEST_EXPECT(context, ellipse_command != nullptr);
        TEST_EXPECT(context, ellipse_command->vertex_count == 2304u);

        gui::draw::destroy_context(draw_context);
    }

    TEST_CASE(draw_records_lines_gradients_circles_and_paths) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::draw::Context draw_context = {};
        gui::draw::create_context(owner_arena, {}, draw_context);

        gui::draw::begin_frame(draw_context);
        gui::draw::draw_line(
            draw_context, {0.0f, 0.0f}, {10.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, 2.0f
        );
        gui::draw::draw_rect_filled_multicolor(
            draw_context,
            {{0.0f, 0.0f}, {4.0f, 3.0f}},
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f}
        );
        gui::draw::draw_circle_filled(
            draw_context, {8.0f, 8.0f}, 5.0f, {0.0f, 1.0f, 1.0f, 1.0f}, 8
        );
        gui::draw::path_line_to(draw_context, {0.0f, 0.0f});
        gui::draw::path_line_to(draw_context, {2.0f, 0.0f});
        gui::draw::path_line_to(draw_context, {0.0f, 2.0f});
        gui::draw::path_fill_convex(draw_context, {1.0f, 1.0f, 0.0f, 1.0f});

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 4u);

        gui::draw::PrimitiveCommand const* line_command =
            gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, line_command != nullptr);
        TEST_EXPECT(context, line_command->vertex_count == 30u);
        TEST_EXPECT(context, line_command->vertices[0u].position.y == 1.0f);
        TEST_EXPECT(context, line_command->vertices[2u].position.y == -1.0f);
        TEST_EXPECT(context, line_command->vertices[8u].color.a == 0.0f);

        gui::draw::PrimitiveCommand const* gradient_command =
            gui::draw::primitive_command(draw_context, 1u);
        TEST_EXPECT(context, gradient_command != nullptr);
        TEST_EXPECT(context, gradient_command->vertex_count == 6u);
        TEST_EXPECT(context, gradient_command->vertices[1u].color.g == 1.0f);
        TEST_EXPECT(context, gradient_command->vertices[4u].color.b == 1.0f);

        gui::draw::PrimitiveCommand const* circle_command =
            gui::draw::primitive_command(draw_context, 2u);
        TEST_EXPECT(context, circle_command != nullptr);
        TEST_EXPECT(context, circle_command->vertex_count == 66u);

        gui::draw::PrimitiveCommand const* path_command =
            gui::draw::primitive_command(draw_context, 3u);
        TEST_EXPECT(context, path_command != nullptr);
        TEST_EXPECT(context, path_command->vertex_count == 21u);

        gui::draw::destroy_context(draw_context);
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
        style.raster_policy = gui::font_provider::RasterPolicy::SMOOTH_HINTED;
        gui::draw::Rect const outer = {{1.0f, 2.0f}, {50.0f, 60.0f}};
        gui::draw::Rect const inner = {{10.0f, 0.0f}, {30.0f, 40.0f}};
        gui::draw::Rect const clipped = {{10.0f, 2.0f}, {30.0f, 40.0f}};
        gui::draw::Transform2D const transform = {{1.0f, 0.0f}, {0.0f, 1.0f}, {3.0f, 4.0f}};
        gui::draw::push_clip_rect(draw_context, outer);
        gui::draw::push_clip_rect(draw_context, inner);
        gui::draw::push_transform(draw_context, transform);
        gui::draw::push_opacity(draw_context, 0.5f);
        float advance = 0.0f;
        gui::draw::draw_text(draw_context, {8.0f, 12.0f}, style, "draw text", &advance);
        TEST_EXPECT(context, advance > 0.0f);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 1u);

        gui::draw::TextCommand const* command = gui::draw::text_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        TEST_EXPECT(context, command->position.x == 8.0f);
        TEST_EXPECT(context, command->position.y == 12.0f);
        TEST_EXPECT(context, command->style.raster_policy == style.raster_policy);
        TEST_EXPECT(context, command->text == StrRef("draw text"));
        TEST_EXPECT(context, command->run.glyphs != nullptr);
        TEST_EXPECT(context, command->run.glyph_count > 0u);
        expect_rect(context, command->clip_rect, clipped);
        expect_transform(context, command->transform, transform);
        TEST_EXPECT(context, command->opacity == 0.5f);

        gui::draw::begin_frame(draw_context);
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(draw_begin_frame_releases_previous_text_rasters) {
        Arena owner_arena = {};
        owner_arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(owner_arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::CacheDesc cache_desc = {};
        cache_desc.arena_reserve_size = 4u * 1024u * 1024u;
        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(owner_arena, provider, cache_desc, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, {}, font);

        gui::draw::Context draw_context = {};
        gui::draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = cache;
        gui::draw::create_context(owner_arena, draw_desc, draw_context);

        gui::draw::TextStyle style = {};
        style.font = font;
        style.size = 18.0f;

        for (size_t index = 0u; index < 64u; ++index) {
            char text[] = "dynamic text 0000";
            text[13u] = static_cast<char>('0' + (index / 1000u) % 10u);
            text[14u] = static_cast<char>('0' + (index / 100u) % 10u);
            text[15u] = static_cast<char>('0' + (index / 10u) % 10u);
            text[16u] = static_cast<char>('0' + index % 10u);

            gui::draw::begin_frame(draw_context);
            gui::draw::draw_text(draw_context, {0.0f, 0.0f}, style, text, nullptr);
            gui::draw::end_frame(draw_context);
            TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 1u);
        }

        gui::draw::destroy_context(draw_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

} // namespace

TEST_MAIN()
