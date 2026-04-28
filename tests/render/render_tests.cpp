#include <render/render.h>
#include <test/test.h>

namespace {

    TEST_CASE(render_result_helpers_classify_status_and_errors) {
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OCCLUDED));
        TEST_EXPECT(context, !gui::render::result_failed(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_failed(gui::render::Result::UNSUPPORTED_PLATFORM));
        TEST_EXPECT(context, gui::render::result_name(gui::render::Result::OK)[0] != '\0');
        TEST_EXPECT(context,
                    gui::render::result_name(gui::render::Result::SHADER_CREATION_FAILED)[0] !=
                        '\0');
        TEST_EXPECT(context,
                    gui::render::result_name(gui::render::Result::PIPELINE_CREATION_FAILED)[0] !=
                        '\0');
        TEST_EXPECT(context,
                    gui::render::result_name(gui::render::Result::SHADER_COMPILATION_FAILED)[0] !=
                        '\0');
        TEST_EXPECT(context,
                    gui::render::result_name(gui::render::Result::TEXTURE_CREATION_FAILED)[0] !=
                        '\0');
        TEST_EXPECT(context,
                    gui::render::result_name(gui::render::Result::SAMPLER_CREATION_FAILED)[0] !=
                        '\0');
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
        TEST_EXPECT(context, gui::render::native_device(context_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_device_context(context_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_swap_chain(window_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_render_target_view(window_handle) == nullptr);
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
        TEST_EXPECT(context, desc.bytes_per_row == 0u);
        TEST_EXPECT(context, desc.rgba_pixels == nullptr);
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

    TEST_CASE(render_bind_group_defaults_describe_empty_group) {
        gui::render::BindGroupDesc const desc = {};

        TEST_EXPECT(context, desc.buffers == nullptr);
        TEST_EXPECT(context, desc.buffer_count == 0u);
        TEST_EXPECT(context, desc.textures == nullptr);
        TEST_EXPECT(context, desc.texture_count == 0u);
        TEST_EXPECT(context, desc.samplers == nullptr);
        TEST_EXPECT(context, desc.sampler_count == 0u);
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

        TEST_EXPECT(context, !gui::render::pipeline_valid(desc.pipeline));
        TEST_EXPECT(context, desc.vertex_buffers == nullptr);
        TEST_EXPECT(context, desc.vertex_buffer_count == 0u);
        TEST_EXPECT(context, desc.bind_groups == nullptr);
        TEST_EXPECT(context, desc.bind_group_count == 0u);
        TEST_EXPECT(context, desc.vertex_count == 0u);
        TEST_EXPECT(context, desc.first_vertex == 0u);
    }

    TEST_CASE(render_vertex_attribute_defaults_describe_float2_attribute) {
        gui::render::VertexAttributeDesc const desc = {};

        TEST_EXPECT(context, desc.semantic_name == nullptr);
        TEST_EXPECT(context, desc.semantic_index == 0u);
        TEST_EXPECT(context, desc.format == gui::render::VertexFormat::FLOAT32_2);
        TEST_EXPECT(context, desc.buffer_slot == 0u);
        TEST_EXPECT(context, desc.byte_offset == 0u);
    }

} // namespace

TEST_MAIN()
