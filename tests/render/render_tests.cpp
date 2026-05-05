#include <base/config.h>
#include <render/render.h>
#include <test/test.h>

#if BASE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef OPAQUE
#undef OPAQUE
#endif
#endif

namespace {

#if BASE_PLATFORM_WINDOWS
    auto CALLBACK render_test_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
        -> LRESULT {
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }

    [[nodiscard]] auto create_render_test_window() -> HWND {
        HINSTANCE const instance = GetModuleHandleA(nullptr);
        char const* const class_name = "gui_framework_render_test_window";

        WNDCLASSEXA window_class = {};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = render_test_window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = class_name;
        if (RegisterClassExA(&window_class) == 0u && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return nullptr;
        }

        return CreateWindowExA(
            0u,
            class_name,
            "render test",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            32,
            32,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
    }

    auto expect_r8_texture_subrect_update(test::Context* context, gui::render::Backend backend)
        -> void {
        Arena owner_arena = {};
        owner_arena.init();

        gui::render::ContextDesc context_desc = {};
        context_desc.backend = backend;
        gui::render::Context render_context = {};
        gui::render::Result result =
            gui::render::create_context(owner_arena, context_desc, render_context);
        TEST_EXPECT(context, result == gui::render::Result::OK);
        if (result != gui::render::Result::OK) {
            owner_arena.destroy();
            return;
        }

        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = {8u, 8u};
        texture_desc.format = gui::render::TextureFormat::R8_UNORM;
        texture_desc.updatable = true;
        gui::render::Texture texture = {};
        result = gui::render::create_texture(render_context, texture_desc, texture);
        TEST_EXPECT(context, result == gui::render::Result::OK);

        uint8_t pixels[] = {0u, 64u, 128u, 255u};
        gui::render::TextureUpdateDesc update_desc = {};
        update_desc.x = 2u;
        update_desc.y = 3u;
        update_desc.size = {2u, 2u};
        update_desc.bytes_per_row = 2u;
        update_desc.pixels = pixels;
        if (gui::render::texture_valid(texture)) {
            result = gui::render::update_texture(render_context, texture, update_desc);
            TEST_EXPECT(context, result == gui::render::Result::OK);

            uint8_t more_pixels[] = {255u, 128u, 64u, 0u, 32u, 96u};
            gui::render::TextureUpdateDesc updates[2u] = {};
            updates[0u].x = 0u;
            updates[0u].y = 0u;
            updates[0u].size = {2u, 1u};
            updates[0u].bytes_per_row = 2u;
            updates[0u].pixels = more_pixels;
            updates[1u].x = 4u;
            updates[1u].y = 5u;
            updates[1u].size = {2u, 2u};
            updates[1u].bytes_per_row = 2u;
            updates[1u].pixels = more_pixels + 2u;
            gui::render::TextureUpdateBatchDesc batch_desc = {};
            batch_desc.updates = updates;
            batch_desc.update_count = 2u;
            result = gui::render::update_texture_batch(render_context, texture, batch_desc);
            TEST_EXPECT(context, result == gui::render::Result::OK);
            gui::render::destroy_texture(render_context, texture);
        }

        gui::render::destroy_context(render_context);
        owner_arena.destroy();
    }

    auto expect_d3d12_frame_active_batch_upload_render(test::Context* context) -> void {
        Arena owner_arena = {};
        owner_arena.init();

        gui::render::ContextDesc context_desc = {};
        context_desc.backend = gui::render::Backend::D3D12;
        gui::render::Context render_context = {};
        gui::render::Result result =
            gui::render::create_context(owner_arena, context_desc, render_context);
        TEST_EXPECT(context, result == gui::render::Result::OK);
        if (result != gui::render::Result::OK) {
            owner_arena.destroy();
            return;
        }

        HWND const hwnd = create_render_test_window();
        TEST_EXPECT(context, hwnd != nullptr);
        if (hwnd == nullptr) {
            gui::render::destroy_context(render_context);
            owner_arena.destroy();
            return;
        }

        gui::render::WindowDesc window_desc = {};
        window_desc.native_window = hwnd;
        window_desc.size = {32u, 32u};
        window_desc.present_mode = gui::render::PresentMode::IMMEDIATE;
        gui::render::Window window = {};
        result = gui::render::create_window(owner_arena, render_context, window_desc, window);
        TEST_EXPECT(context, result == gui::render::Result::OK);

        gui::render::TextureDesc atlas_desc = {};
        atlas_desc.size = {8u, 8u};
        atlas_desc.format = gui::render::TextureFormat::R8_UNORM;
        atlas_desc.updatable = true;
        gui::render::Texture atlas = {};
        if (result == gui::render::Result::OK) {
            result = gui::render::create_texture(render_context, atlas_desc, atlas);
            TEST_EXPECT(context, result == gui::render::Result::OK);
        }

        gui::render::TextureDesc target_desc = {};
        target_desc.size = {8u, 8u};
        target_desc.render_target = true;
        gui::render::Texture target = {};
        if (gui::render::texture_valid(atlas)) {
            result = gui::render::create_texture(render_context, target_desc, target);
            TEST_EXPECT(context, result == gui::render::Result::OK);
        }

        if (gui::render::texture_valid(atlas) && gui::render::texture_valid(target)) {
            uint8_t pixels[] = {255u, 128u, 64u, 32u};
            gui::render::TextureUpdateDesc updates[2u] = {};
            updates[0u].x = 1u;
            updates[0u].y = 1u;
            updates[0u].size = {2u, 1u};
            updates[0u].bytes_per_row = 2u;
            updates[0u].pixels = pixels;
            updates[1u].x = 3u;
            updates[1u].y = 2u;
            updates[1u].size = {2u, 1u};
            updates[1u].bytes_per_row = 2u;
            updates[1u].pixels = pixels + 2u;

            gui::render::begin_frame(render_context);

            gui::render::TextureUpdateBatchDesc batch_desc = {};
            batch_desc.updates = updates;
            batch_desc.update_count = 2u;
            result = gui::render::update_texture_batch(render_context, atlas, batch_desc);
            TEST_EXPECT(context, result == gui::render::Result::OK);

            gui::render::TextureRenderPassDesc pass_desc = {};
            pass_desc.target = target;
            result = gui::render::begin_texture_render_pass(render_context, pass_desc);
            TEST_EXPECT(context, result == gui::render::Result::OK);
            if (result == gui::render::Result::OK) {
                gui::render::end_render_pass(render_context);
            }

            result = gui::render::present_window(render_context, window);
            TEST_EXPECT(
                context,
                result == gui::render::Result::OK || result == gui::render::Result::OCCLUDED
            );
        }

        if (gui::render::texture_valid(target)) {
            gui::render::destroy_texture(render_context, target);
        }
        if (gui::render::texture_valid(atlas)) {
            gui::render::destroy_texture(render_context, atlas);
        }
        if (gui::render::window_valid(window)) {
            gui::render::destroy_window(window);
        }
        DestroyWindow(hwnd);
        gui::render::destroy_context(render_context);
        owner_arena.destroy();
    }
#endif

    TEST_CASE(render_result_helpers_classify_status_and_errors) {
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OCCLUDED));
        TEST_EXPECT(context, !gui::render::result_failed(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_failed(gui::render::Result::UNSUPPORTED_PLATFORM));
        TEST_EXPECT(context, gui::render::result_name(gui::render::Result::OK)[0] != '\0');
        TEST_EXPECT(
            context,
            gui::render::result_name(gui::render::Result::SHADER_CREATION_FAILED)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::render::result_name(gui::render::Result::PIPELINE_CREATION_FAILED)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::render::result_name(gui::render::Result::SHADER_COMPILATION_FAILED)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::render::result_name(gui::render::Result::TEXTURE_CREATION_FAILED)[0] != '\0'
        );
        TEST_EXPECT(
            context,
            gui::render::result_name(gui::render::Result::SAMPLER_CREATION_FAILED)[0] != '\0'
        );
        TEST_EXPECT(
            context, gui::render::result_name(gui::render::Result::IMAGE_LOAD_FAILED)[0] != '\0'
        );
        TEST_EXPECT(context, gui::render::backend_name(gui::render::Backend::D3D11)[0] != '\0');
        TEST_EXPECT(context, gui::render::backend_name(gui::render::Backend::D3D12)[0] != '\0');
    }

    TEST_CASE(render_handles_start_empty_and_validate_by_handle_value) {
        gui::render::Context const context_handle = {};
        gui::render::Window const window_handle = {};
        gui::render::Buffer const buffer_handle = {};
        gui::render::Texture const texture_handle = {};
        gui::render::Sampler const sampler_handle = {};
        gui::render::Shader const shader_handle = {};
        gui::render::Pipeline const pipeline_handle = {};
        gui::render::BindGroup const bind_group_handle = {};
        gui::render::FrameBufferSlice const frame_slice = {};
        gui::render::SizeU32 const size = gui::render::window_size(window_handle);
        gui::render::SizeU32 const texture_size = gui::render::texture_size(texture_handle);

        TEST_EXPECT(context, !gui::render::context_valid(context_handle));
        TEST_EXPECT(context, !gui::render::window_valid(window_handle));
        TEST_EXPECT(context, !gui::render::buffer_valid(buffer_handle));
        TEST_EXPECT(context, !gui::render::texture_valid(texture_handle));
        TEST_EXPECT(context, !gui::render::sampler_valid(sampler_handle));
        TEST_EXPECT(context, !gui::render::shader_valid(shader_handle));
        TEST_EXPECT(context, !gui::render::pipeline_valid(pipeline_handle));
        TEST_EXPECT(context, !gui::render::bind_group_valid(bind_group_handle));
        TEST_EXPECT(context, !gui::render::buffer_valid(frame_slice.buffer));
        TEST_EXPECT(context, frame_slice.data == nullptr);
        TEST_EXPECT(context, frame_slice.byte_offset == 0u);
        TEST_EXPECT(context, frame_slice.byte_size == 0u);
        TEST_EXPECT(context, size.width == 0u);
        TEST_EXPECT(context, size.height == 0u);
        TEST_EXPECT(context, texture_size.width == 0u);
        TEST_EXPECT(context, texture_size.height == 0u);
    }

    TEST_CASE(render_resource_handles_validate_non_null_values) {
        int value = 0;

        gui::render::Buffer const buffer_handle = {&value};
        gui::render::Texture const texture_handle = {&value};
        gui::render::Sampler const sampler_handle = {&value};
        gui::render::Shader const shader_handle = {&value};
        gui::render::Pipeline const pipeline_handle = {&value};
        gui::render::BindGroup const bind_group_handle = {&value};

        TEST_EXPECT(context, gui::render::buffer_valid(buffer_handle));
        TEST_EXPECT(context, gui::render::texture_valid(texture_handle));
        TEST_EXPECT(context, gui::render::sampler_valid(sampler_handle));
        TEST_EXPECT(context, gui::render::shader_valid(shader_handle));
        TEST_EXPECT(context, gui::render::pipeline_valid(pipeline_handle));
        TEST_EXPECT(context, gui::render::bind_group_valid(bind_group_handle));
    }

    TEST_CASE(window_render_pass_defaults_target_invalid_window) {
        gui::render::WindowRenderPassDesc const desc = {};

        TEST_EXPECT(context, !gui::render::window_valid(desc.window));
        TEST_EXPECT(context, desc.load_op == gui::render::LoadOp::CLEAR);
        TEST_EXPECT(context, desc.clear_color.a == 1.0f);
    }

    TEST_CASE(texture_render_pass_defaults_target_invalid_texture) {
        gui::render::TextureRenderPassDesc const desc = {};

        TEST_EXPECT(context, !gui::render::texture_valid(desc.target));
        TEST_EXPECT(context, desc.load_op == gui::render::LoadOp::CLEAR);
        TEST_EXPECT(context, desc.clear_color.a == 1.0f);
    }

    TEST_CASE(render_buffer_defaults_describe_immutable_vertex_buffer) {
        gui::render::BufferDesc const desc = {};

        TEST_EXPECT(context, desc.binding == gui::render::BufferBinding::VERTEX);
        TEST_EXPECT(context, desc.usage == gui::render::BufferUsage::IMMUTABLE);
        TEST_EXPECT(context, desc.byte_size == 0u);
        TEST_EXPECT(context, desc.initial_data == nullptr);
    }

    TEST_CASE(render_shader_defaults_describe_vertex_shader_without_bytecode) {
        gui::render::ShaderDesc const desc = {};

        TEST_EXPECT(context, desc.stage == gui::render::ShaderStage::VERTEX);
        TEST_EXPECT(context, desc.bytecode == nullptr);
        TEST_EXPECT(context, desc.byte_size == 0u);
    }

    TEST_CASE(render_texture_defaults_describe_empty_rgba_upload) {
        gui::render::TextureDesc const desc = {};

        TEST_EXPECT(context, desc.size.width == 0u);
        TEST_EXPECT(context, desc.size.height == 0u);
        TEST_EXPECT(context, desc.format == gui::render::TextureFormat::RGBA8_UNORM);
        TEST_EXPECT(context, desc.bytes_per_row == 0u);
        TEST_EXPECT(context, desc.rgba_pixels == nullptr);
        TEST_EXPECT(context, !desc.render_target);
        TEST_EXPECT(context, !desc.updatable);
    }

    TEST_CASE(render_texture_desc_can_request_r8_upload) {
        uint8_t pixels[8u] = {};
        gui::render::TextureDesc desc = {};
        desc.size = {4u, 2u};
        desc.format = gui::render::TextureFormat::R8_UNORM;
        desc.bytes_per_row = 4u;
        desc.rgba_pixels = pixels;

        TEST_EXPECT(context, desc.format == gui::render::TextureFormat::R8_UNORM);
        TEST_EXPECT(context, desc.bytes_per_row == desc.size.width);
        TEST_EXPECT(context, desc.rgba_pixels == pixels);
    }

    TEST_CASE(render_target_texture_desc_keeps_upload_optional) {
        gui::render::TextureDesc desc = {};
        desc.size = {16u, 8u};
        desc.render_target = true;

        TEST_EXPECT(context, desc.size.width == 16u);
        TEST_EXPECT(context, desc.size.height == 8u);
        TEST_EXPECT(context, desc.bytes_per_row == 0u);
        TEST_EXPECT(context, desc.rgba_pixels == nullptr);
        TEST_EXPECT(context, desc.render_target);
    }

    TEST_CASE(render_updatable_texture_desc_keeps_upload_optional) {
        gui::render::TextureDesc desc = {};
        desc.size = {16u, 8u};
        desc.format = gui::render::TextureFormat::R8_UNORM;
        desc.updatable = true;

        TEST_EXPECT(context, desc.size.width == 16u);
        TEST_EXPECT(context, desc.size.height == 8u);
        TEST_EXPECT(context, desc.bytes_per_row == 0u);
        TEST_EXPECT(context, desc.rgba_pixels == nullptr);
        TEST_EXPECT(context, desc.updatable);
    }

    TEST_CASE(render_texture_update_desc_describes_subrect_upload) {
        uint8_t pixels[4u] = {};
        gui::render::TextureUpdateDesc desc = {};
        desc.x = 2u;
        desc.y = 3u;
        desc.size = {2u, 2u};
        desc.bytes_per_row = 2u;
        desc.pixels = pixels;

        TEST_EXPECT(context, desc.x == 2u);
        TEST_EXPECT(context, desc.y == 3u);
        TEST_EXPECT(context, desc.size.width == 2u);
        TEST_EXPECT(context, desc.bytes_per_row == 2u);
        TEST_EXPECT(context, desc.pixels == pixels);
    }

    TEST_CASE(render_texture_update_batch_desc_describes_multiple_subrect_uploads) {
        gui::render::TextureUpdateDesc updates[1u] = {};
        gui::render::TextureUpdateBatchDesc desc = {};
        desc.updates = updates;
        desc.update_count = 1u;

        TEST_EXPECT(context, desc.updates == updates);
        TEST_EXPECT(context, desc.update_count == 1u);
    }

    TEST_CASE(render_updates_r8_texture_subrect_on_supported_backends) {
#if BASE_PLATFORM_WINDOWS
        expect_r8_texture_subrect_update(context, gui::render::Backend::D3D11);
        expect_r8_texture_subrect_update(context, gui::render::Backend::D3D12);
#else
        TEST_EXPECT(context, true);
#endif
    }

    TEST_CASE(render_d3d12_batches_texture_uploads_during_active_frame) {
#if BASE_PLATFORM_WINDOWS
        expect_d3d12_frame_active_batch_upload_render(context);
#else
        TEST_EXPECT(context, true);
#endif
    }

    TEST_CASE(render_sampler_defaults_describe_linear_clamp) {
        gui::render::SamplerDesc const desc = {};

        TEST_EXPECT(context, desc.filter == gui::render::SamplerFilter::LINEAR);
        TEST_EXPECT(context, desc.address_mode == gui::render::SamplerAddressMode::CLAMP);
    }

    TEST_CASE(render_sampler_desc_can_request_nearest_repeat) {
        gui::render::SamplerDesc desc = {};
        desc.filter = gui::render::SamplerFilter::NEAREST;
        desc.address_mode = gui::render::SamplerAddressMode::REPEAT;

        TEST_EXPECT(context, desc.filter == gui::render::SamplerFilter::NEAREST);
        TEST_EXPECT(context, desc.address_mode == gui::render::SamplerAddressMode::REPEAT);
    }

    TEST_CASE(render_scissor_rect_defaults_describe_empty_rect) {
        gui::render::ScissorRect const rect = {};

        TEST_EXPECT(context, rect.x == 0u);
        TEST_EXPECT(context, rect.y == 0u);
        TEST_EXPECT(context, rect.width == 0u);
        TEST_EXPECT(context, rect.height == 0u);
    }

    TEST_CASE(render_shader_source_defaults_describe_vertex_shader_without_source) {
        gui::render::ShaderSourceDesc const desc = {};

        TEST_EXPECT(context, desc.stage == gui::render::ShaderStage::VERTEX);
        TEST_EXPECT(context, desc.source.empty());
        TEST_EXPECT(context, desc.entry_point == nullptr);
    }

    TEST_CASE(render_pipeline_defaults_describe_empty_triangle_pipeline) {
        gui::render::PipelineDesc const desc = {};

        TEST_EXPECT(context, !gui::render::shader_valid(desc.vertex_shader));
        TEST_EXPECT(context, !gui::render::shader_valid(desc.pixel_shader));
        TEST_EXPECT(context, desc.vertex_attributes == nullptr);
        TEST_EXPECT(context, desc.vertex_attribute_count == 0u);
        TEST_EXPECT(context, desc.topology == gui::render::PrimitiveTopology::TRIANGLE_LIST);
        TEST_EXPECT(context, desc.blend_mode == gui::render::BlendMode::OPAQUE);
    }

    TEST_CASE(render_blend_modes_include_compositor_foundations) {
        TEST_EXPECT(
            context, gui::render::BlendMode::PREMULTIPLIED_ALPHA != gui::render::BlendMode::ALPHA
        );
        TEST_EXPECT(context, gui::render::BlendMode::ADDITIVE != gui::render::BlendMode::OPAQUE);
        TEST_EXPECT(context, gui::render::BlendMode::MULTIPLY != gui::render::BlendMode::SCREEN);
        TEST_EXPECT(
            context, gui::render::BlendMode::DESTINATION_ATTENUATE != gui::render::BlendMode::SCREEN
        );
    }

    TEST_CASE(render_bind_group_defaults_describe_empty_group) {
        gui::render::BindGroupDesc const desc = {};

        TEST_EXPECT(context, desc.buffers == nullptr);
        TEST_EXPECT(context, desc.buffer_count == 0u);
        TEST_EXPECT(context, desc.textures == nullptr);
        TEST_EXPECT(context, desc.texture_count == 0u);
        TEST_EXPECT(context, desc.samplers == nullptr);
        TEST_EXPECT(context, desc.sampler_count == 0u);
    }

    TEST_CASE(render_bind_group_slot_limit_matches_cross_backend_binding) {
        gui::render::BindGroupBufferBinding const buffer = {};
        gui::render::BindGroupTextureBinding const texture = {};
        gui::render::BindGroupSamplerBinding const sampler = {};

        TEST_EXPECT(context, gui::render::BIND_GROUP_SLOT_COUNT == 14u);
        TEST_EXPECT(context, buffer.slot < gui::render::BIND_GROUP_SLOT_COUNT);
        TEST_EXPECT(context, texture.slot < gui::render::BIND_GROUP_SLOT_COUNT);
        TEST_EXPECT(context, sampler.slot < gui::render::BIND_GROUP_SLOT_COUNT);
    }

    TEST_CASE(render_vertex_buffer_binding_defaults_describe_slot_zero) {
        gui::render::VertexBufferBinding const binding = {};

        TEST_EXPECT(context, !gui::render::buffer_valid(binding.buffer));
        TEST_EXPECT(context, binding.slot == 0u);
        TEST_EXPECT(context, binding.byte_stride == 0u);
        TEST_EXPECT(context, binding.byte_offset == 0u);
    }

    TEST_CASE(render_draw_defaults_describe_empty_draw) {
        gui::render::DrawDesc const desc = {};

        TEST_EXPECT(context, desc.vertex_buffers == nullptr);
        TEST_EXPECT(context, desc.vertex_buffer_count == 0u);
        TEST_EXPECT(context, desc.vertex_count == 0u);
        TEST_EXPECT(context, desc.first_vertex == 0u);
        TEST_EXPECT(context, desc.instance_count == 1u);
        TEST_EXPECT(context, desc.first_instance == 0u);
    }

    TEST_CASE(render_vertex_attribute_defaults_describe_float2_attribute) {
        gui::render::VertexAttributeDesc const desc = {};

        TEST_EXPECT(context, desc.semantic_name == nullptr);
        TEST_EXPECT(context, desc.semantic_index == 0u);
        TEST_EXPECT(context, desc.format == gui::render::VertexFormat::FLOAT32_2);
        TEST_EXPECT(context, desc.buffer_slot == 0u);
        TEST_EXPECT(context, desc.byte_offset == 0u);
        TEST_EXPECT(context, !desc.per_instance);
    }

} // namespace

TEST_MAIN()
