#pragma once

#include <render/render.h>

namespace gui::render::d3d11 {

    [[nodiscard]] auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context)
        -> Result;
    auto destroy_context(Context& context) -> void;

    [[nodiscard]] auto
    create_window(Arena& arena, Context context, WindowDesc const& desc, Window& out_window)
        -> Result;
    auto destroy_window(Window& window) -> void;

    [[nodiscard]] auto resize_window(Context context, Window window, SizeU32 size) -> Result;
    [[nodiscard]] auto begin_frame(Context context) -> Result;
    [[nodiscard]] auto clear_window(Context context, Window window, Color color) -> Result;
    [[nodiscard]] auto present_window(Window window) -> Result;

    [[nodiscard]] auto window_size(Window window) -> SizeU32;

    [[nodiscard]] auto native_device(Context context) -> void*;
    [[nodiscard]] auto native_device_context(Context context) -> void*;
    [[nodiscard]] auto native_swap_chain(Window window) -> void*;
    [[nodiscard]] auto native_render_target_view(Window window) -> void*;

} // namespace gui::render::d3d11
