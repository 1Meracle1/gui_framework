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
        gui::render::SizeU32 const size = gui::render::window_size(window_handle);

        TEST_EXPECT(context, !gui::render::context_valid(context_handle));
        TEST_EXPECT(context, !gui::render::window_valid(window_handle));
        TEST_EXPECT(context, size.width == 0u);
        TEST_EXPECT(context, size.height == 0u);
        TEST_EXPECT(context, gui::render::native_device(context_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_device_context(context_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_swap_chain(window_handle) == nullptr);
        TEST_EXPECT(context, gui::render::native_render_target_view(window_handle) == nullptr);
    }

} // namespace

TEST_MAIN()
