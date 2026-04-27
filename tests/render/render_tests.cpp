#include <render/render.h>
#include <test/test.h>

namespace {

    TEST_CASE(render_result_helpers_classify_status_and_errors) {
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_succeeded(gui::render::Result::OCCLUDED));
        TEST_EXPECT(context, !gui::render::result_failed(gui::render::Result::OK));
        TEST_EXPECT(context, gui::render::result_failed(gui::render::Result::UNSUPPORTED_PLATFORM));
        TEST_EXPECT(context, gui::render::result_name(gui::render::Result::OK)[0] != '\0');
        TEST_EXPECT(context, gui::render::backend_name(gui::render::Backend::D3D11)[0] != '\0');
    }

    TEST_CASE(render_handles_start_empty_and_validate_by_handle_value) {
        gui::render::Context const context_handle = {};
        gui::render::Window const window_handle = {};
        gui::render::Buffer const buffer_handle = {};
        gui::render::Texture const texture_handle = {};
        gui::render::Shader const shader_handle = {};
        gui::render::Pipeline const pipeline_handle = {};
        gui::render::BindGroup const bind_group_handle = {};
        gui::render::SizeU32 const size = gui::render::window_size(window_handle);

        TEST_EXPECT(context, !gui::render::context_valid(context_handle));
        TEST_EXPECT(context, !gui::render::window_valid(window_handle));
        TEST_EXPECT(context, !gui::render::buffer_valid(buffer_handle));
        TEST_EXPECT(context, !gui::render::texture_valid(texture_handle));
        TEST_EXPECT(context, !gui::render::shader_valid(shader_handle));
        TEST_EXPECT(context, !gui::render::pipeline_valid(pipeline_handle));
        TEST_EXPECT(context, !gui::render::bind_group_valid(bind_group_handle));
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
        gui::render::Shader const shader_handle = {&value};
        gui::render::Pipeline const pipeline_handle = {&value};
        gui::render::BindGroup const bind_group_handle = {&value};

        TEST_EXPECT(context, gui::render::buffer_valid(buffer_handle));
        TEST_EXPECT(context, gui::render::texture_valid(texture_handle));
        TEST_EXPECT(context, gui::render::shader_valid(shader_handle));
        TEST_EXPECT(context, gui::render::pipeline_valid(pipeline_handle));
        TEST_EXPECT(context, gui::render::bind_group_valid(bind_group_handle));
    }

    TEST_CASE(render_pass_defaults_target_one_window_color_attachment) {
        gui::render::RenderPassDesc const desc = {};

        TEST_EXPECT(context, !gui::render::window_valid(desc.color.window));
        TEST_EXPECT(context, desc.color.load_op == gui::render::LoadOp::CLEAR);
        TEST_EXPECT(context, desc.color.store_op == gui::render::StoreOp::STORE);
        TEST_EXPECT(context, desc.color.clear_color.a == 1.0f);
    }

    TEST_CASE(render_buffer_defaults_describe_immutable_vertex_buffer) {
        gui::render::BufferDesc const desc = {};

        TEST_EXPECT(context, desc.binding == gui::render::BufferBinding::VERTEX);
        TEST_EXPECT(context, desc.usage == gui::render::BufferUsage::IMMUTABLE);
        TEST_EXPECT(context, desc.byte_size == 0u);
        TEST_EXPECT(context, desc.initial_data == nullptr);
    }

} // namespace

TEST_MAIN()
