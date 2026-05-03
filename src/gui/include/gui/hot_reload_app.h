#pragma once

#include <base/memory.h>
#include <gui/gui.h>
#include <gui/hot_reload.h>
#include <render/render.h>

namespace gui {

    struct HotReloadRuntimeContext {
        render::Context render_context = {};
        void* native_window = nullptr;
    };

    struct HotReloadDrawCommandCounts {
        size_t command_count = 0u;
        size_t primitive_count = 0u;
        size_t batch_count = 0u;
        size_t styled_rect_count = 0u;
        size_t text_count = 0u;
        size_t layer_count = 0u;
    };

    struct HotReloadFrameResult {
        Frame frame = {};
        Id mouse_hit_id = {};
        render::Result render_result = render::Result::OK;
        bool redraw_pending = false;
        HotReloadDrawCommandCounts draw_counts = {};
    };

    using HotReloadRenderFrameFn = auto (*)(
        void* storage,
        render::Context render_context,
        render::Window render_window,
        render::SizeU32 window_size,
        InputState const& input,
        float delta_time
    ) -> HotReloadFrameResult;

    struct HotReloadAppApi {
        HotReloadApi hot_reload = {};
        HotReloadRenderFrameFn render_frame = nullptr;
    };

    struct HotReloadAppModule {
#if GUI_HOT_RELOAD_AVAILABLE
        HotReloadModule hot_reload = {};
#else
        HotReloadAppApi const* api = nullptr;
        void* storage = nullptr;
        bool runtime_valid = false;
#endif
    };

    [[nodiscard]] auto hot_reload_app_api_valid(
        HotReloadAppApi const* api, size_t storage_size, size_t storage_alignment
    ) -> bool;
    auto
    init_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc, Arena& arena)
        -> void;
    [[nodiscard]] auto load_hot_reload_app_module(
        HotReloadAppModule* module, HotReloadDesc const& desc, HotReloadAppApi const* direct_api
    ) -> bool;
    [[nodiscard]] auto
    update_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc) -> bool;
    auto destroy_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc)
        -> void;
    [[nodiscard]] auto hot_reload_app_module_api(HotReloadAppModule const& module)
        -> HotReloadAppApi const*;
    [[nodiscard]] auto hot_reload_app_module_storage(HotReloadAppModule const& module) -> void*;
    [[nodiscard]] auto hot_reload_app_module_status(HotReloadAppModule const& module)
        -> HotReloadStatus;
    [[nodiscard]] auto hot_reload_app_module_status_visible(HotReloadAppModule const& module)
        -> bool;

} // namespace gui
