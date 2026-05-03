#pragma once

#include <base/memory.h>
#include <gui/gui.h>
#include <gui/hot_reload.h>
#include <render/render.h>

namespace gui {

    struct HotReloadOverlay {
        void* handle = nullptr;
    };

    struct HotReloadOverlayState {
        HotReloadPhase observed_phase = HotReloadPhase::IDLE;
        size_t observed_text_size = 0u;
        size_t observed_line_count = 0u;
        bool observed_truncated = false;
        bool visible = false;
        bool hidden_failure = false;
        bool follow_tail = true;
        bool capture_input = false;
        bool mouse_capture = false;
        bool last_mouse_down = false;
    };

    [[nodiscard]] auto create_hot_reload_overlay(
        Arena& arena, render::Context render_context, void* native_window, HotReloadOverlay* overlay
    ) -> bool;
    auto destroy_hot_reload_overlay(render::Context render_context, HotReloadOverlay* overlay)
        -> void;
    [[nodiscard]] auto update_hot_reload_overlay_state(
        HotReloadOverlayState* state,
        HotReloadStatus overlay,
        bool module_visible,
        render::SizeU32 window_size,
        InputState* input
    ) -> bool;
    auto build_hot_reload_overlay_commands(
        HotReloadOverlay* overlay,
        render::SizeU32 window_size,
        HotReloadStatus status,
        HotReloadOverlayState* state,
        InputState const& input,
        float delta_time
    ) -> void;
    [[nodiscard]] auto render_hot_reload_overlay(
        HotReloadOverlay* overlay, render::Context render_context, render::Window render_window
    ) -> render::Result;

} // namespace gui
