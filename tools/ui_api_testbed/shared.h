#pragma once

#include <base/config.h>
#include <cstddef>
#include <cstdint>
#include <gui/gui.h>
#include <render/render.h>

namespace ui_api_testbed {

#if defined(_WIN32) && BASE_DEBUG
    inline constexpr bool HOT_RELOAD_ENABLED = true;
#else
    inline constexpr bool HOT_RELOAD_ENABLED = false;
#endif

    inline constexpr uint32_t MODULE_API_VERSION = 1u;
    inline constexpr size_t MODULE_STORAGE_SIZE = 64u * 1024u;
    inline constexpr size_t MODULE_STORAGE_ALIGNMENT = 64u;
    inline constexpr wchar_t MODULE_FILE_NAME[] = L"ui_api_testbed_module.dll";
    inline constexpr uint32_t HOT_RELOAD_POLL_MS = 250u;

    struct DrawCommandCounts {
        size_t command_count = 0u;
        size_t primitive_count = 0u;
        size_t batch_count = 0u;
        size_t styled_rect_count = 0u;
        size_t text_count = 0u;
        size_t layer_count = 0u;
    };

    struct FrameResult {
        gui::Frame frame = {};
        gui::Id mouse_hit_id = {};
        gui::render::Result render_result = gui::render::Result::OK;
        bool redraw_pending = false;
        DrawCommandCounts draw_counts = {};
    };

    using ModuleCreateFn =
        bool (*)(void* storage, gui::render::Context render_context, void* native_window);
    using ModuleDestroyFn = void (*)(void* storage, gui::render::Context render_context);
    using ModuleRenderFrameFn = FrameResult (*)(
        void* storage,
        gui::render::Context render_context,
        gui::render::Window render_window,
        gui::render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    );

    struct ModuleApi {
        uint32_t version = 0u;
        size_t runtime_size = 0u;
        size_t runtime_alignment = 0u;
        ModuleCreateFn create = nullptr;
        ModuleDestroyFn destroy = nullptr;
        ModuleRenderFrameFn render_frame = nullptr;
    };

} // namespace ui_api_testbed
