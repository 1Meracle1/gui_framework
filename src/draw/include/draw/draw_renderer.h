#pragma once

#include <base/memory.h>
#include <draw/draw.h>
#include <render/render.h>

namespace gui::draw {

    struct RendererDesc {};

    struct Renderer {
        void* handle = nullptr;
    };

    [[nodiscard]] auto renderer_valid(Renderer renderer) -> bool;

    [[nodiscard]] auto create_renderer(Arena& arena,
                                       gui::render::Context render_context,
                                       RendererDesc const& desc,
                                       Renderer& out_renderer) -> gui::render::Result;
    auto destroy_renderer(gui::render::Context render_context, Renderer& renderer) -> void;

    auto render_commands(Renderer renderer,
                         gui::render::Context render_context,
                         gui::render::SizeU32 target_size,
                         Context draw_context) -> void;

} // namespace gui::draw
