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

    [[nodiscard]] auto create_buffer(Context context, BufferDesc const& desc, Buffer& out_buffer)
        -> Result;
    auto destroy_buffer(Context context, Buffer& buffer) -> void;
    auto update_buffer(Context context, Buffer buffer, void const* data, size_t byte_size) -> void;
    [[nodiscard]] auto allocate_frame_vertex_buffer(Context context,
                                                    size_t byte_size,
                                                    size_t byte_alignment) -> FrameBufferSlice;
    auto commit_frame_uploads(Context context) -> void;

    [[nodiscard]] auto
    create_texture(Context context, TextureDesc const& desc, Texture& out_texture) -> Result;
    auto destroy_texture(Context context, Texture& texture) -> void;
    [[nodiscard]] auto create_sampler(Context context, Sampler& out_sampler) -> Result;
    auto destroy_sampler(Context context, Sampler& sampler) -> void;

    [[nodiscard]] auto
    create_shader(Arena& arena, Context context, ShaderDesc const& desc, Shader& out_shader)
        -> Result;
    [[nodiscard]] auto create_shader_from_source(Arena& arena,
                                                 Context context,
                                                 ShaderSourceDesc const& desc,
                                                 Shader& out_shader) -> Result;
    auto destroy_shader(Context context, Shader& shader) -> void;

    [[nodiscard]] auto
    create_pipeline(Arena& arena, Context context, PipelineDesc const& desc, Pipeline& out_pipeline)
        -> Result;
    auto destroy_pipeline(Context context, Pipeline& pipeline) -> void;
    auto bind_pipeline(Context context, Pipeline pipeline) -> void;

    [[nodiscard]] auto create_bind_group(Arena& arena,
                                         Context context,
                                         BindGroupDesc const& desc,
                                         BindGroup& out_group) -> Result;
    auto destroy_bind_group(Context context, BindGroup& bind_group) -> void;
    auto bind_group(Context context, BindGroup bind_group) -> void;
    auto draw(Context context, DrawDesc const& desc) -> void;

    [[nodiscard]] auto resize_window(Context context, Window window, SizeU32 size) -> Result;
    auto begin_frame(Context context) -> void;
    [[nodiscard]] auto begin_render_pass(Context context, WindowRenderPassDesc const& desc)
        -> Result;
    auto end_render_pass(Context context) -> void;
    [[nodiscard]] auto present_window(Context context, Window window) -> Result;

    [[nodiscard]] auto window_size(Window window) -> SizeU32;

    [[nodiscard]] auto native_device(Context context) -> void*;
    [[nodiscard]] auto native_swap_chain(Window window) -> void*;

} // namespace gui::render::d3d11
