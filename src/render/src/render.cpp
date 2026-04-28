#include <base/config.h>
#include <render/render.h>

#if BASE_PLATFORM_WINDOWS
#include "render_d3d11.h"
#endif

namespace gui::render {

    auto result_succeeded(Result result) -> bool {
        return static_cast<int32_t>(result) >= 0;
    }

    auto result_failed(Result result) -> bool {
        return !result_succeeded(result);
    }

    auto result_name(Result result) -> char const* {
        switch (result) {
        case Result::OK:
            return "ok";
        case Result::OCCLUDED:
            return "occluded";
        case Result::UNSUPPORTED_PLATFORM:
            return "unsupported platform";
        case Result::UNSUPPORTED_BACKEND:
            return "unsupported backend";
        case Result::OUT_OF_MEMORY:
            return "out of memory";
        case Result::DEVICE_CREATION_FAILED:
            return "device creation failed";
        case Result::FACTORY_CREATION_FAILED:
            return "factory creation failed";
        case Result::WINDOW_CREATION_FAILED:
            return "window creation failed";
        case Result::RENDER_TARGET_CREATION_FAILED:
            return "render target creation failed";
        case Result::RESIZE_FAILED:
            return "resize failed";
        case Result::PRESENT_FAILED:
            return "present failed";
        case Result::BUFFER_CREATION_FAILED:
            return "buffer creation failed";
        case Result::SHADER_CREATION_FAILED:
            return "shader creation failed";
        case Result::PIPELINE_CREATION_FAILED:
            return "pipeline creation failed";
        case Result::SHADER_COMPILATION_FAILED:
            return "shader compilation failed";
        case Result::TEXTURE_CREATION_FAILED:
            return "texture creation failed";
        case Result::SAMPLER_CREATION_FAILED:
            return "sampler creation failed";
        }

        return "unknown";
    }

    auto backend_name(Backend backend) -> char const* {
        switch (backend) {
        case Backend::D3D11:
            return "d3d11";
        }

        return "unknown";
    }

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto window_valid(Window window) -> bool {
        return window.handle != nullptr;
    }

    auto buffer_valid(Buffer buffer) -> bool {
        return buffer.handle != nullptr;
    }

    auto texture_valid(Texture texture) -> bool {
        return texture.handle != nullptr;
    }

    auto sampler_valid(Sampler sampler) -> bool {
        return sampler.handle != nullptr;
    }

    auto shader_valid(Shader shader) -> bool {
        return shader.handle != nullptr;
    }

    auto pipeline_valid(Pipeline pipeline) -> bool {
        return pipeline.handle != nullptr;
    }

    auto bind_group_valid(BindGroup bind_group) -> bool {
        return bind_group.handle != nullptr;
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        ASSERT(out_context.handle == nullptr);
        if (desc.backend != Backend::D3D11) {
            return Result::UNSUPPORTED_BACKEND;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_context(arena, desc, out_context);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_context(Context& context) -> void {
        ASSERT(context.handle != nullptr);

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_context(context);
#else
        context.handle = nullptr;
#endif
    }

    auto create_window(Arena& arena, Context context, WindowDesc const& desc, Window& out_window)
        -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_window.handle == nullptr);
        ASSERT(desc.native_window != nullptr);
        ASSERT(desc.size.width != 0u);
        ASSERT(desc.size.height != 0u);
        ASSERT(desc.buffer_count != 0u);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_window(arena, context, desc, out_window);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_window);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_window(Window& window) -> void {
        ASSERT(window.handle != nullptr);

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_window(window);
#else
        window.handle = nullptr;
#endif
    }

    auto create_buffer(Context context, BufferDesc const& desc, Buffer& out_buffer) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_buffer.handle == nullptr);
        ASSERT(desc.byte_size != 0u);
        ASSERT(desc.usage != BufferUsage::IMMUTABLE || desc.initial_data != nullptr);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_buffer(context, desc, out_buffer);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_buffer);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_buffer(Context context, Buffer& buffer) -> void {
        ASSERT(context_valid(context));
        ASSERT(buffer_valid(buffer));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_buffer(context, buffer);
#else
        BASE_UNUSED(context);
        buffer.handle = nullptr;
#endif
    }

    auto update_buffer(Context context, Buffer buffer, void const* data, size_t byte_size) -> void {
        ASSERT(context_valid(context));
        ASSERT(buffer_valid(buffer));
        ASSERT(data != nullptr);
        ASSERT(byte_size != 0u);

#if BASE_PLATFORM_WINDOWS
        d3d11::update_buffer(context, buffer, data, byte_size);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(buffer);
        BASE_UNUSED(data);
        BASE_UNUSED(byte_size);
#endif
    }

    auto allocate_frame_buffer(Context context,
                               BufferBinding binding,
                               size_t byte_size,
                               size_t byte_alignment) -> FrameBufferSlice {
        ASSERT(context_valid(context));
        ASSERT(byte_size != 0u);
        ASSERT(byte_alignment != 0u);

#if BASE_PLATFORM_WINDOWS
        return d3d11::allocate_frame_buffer(context, binding, byte_size, byte_alignment);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(binding);
        BASE_UNUSED(byte_size);
        BASE_UNUSED(byte_alignment);
        return {};
#endif
    }

    auto commit_frame_uploads(Context context) -> void {
        ASSERT(context_valid(context));

#if BASE_PLATFORM_WINDOWS
        d3d11::commit_frame_uploads(context);
#else
        BASE_UNUSED(context);
#endif
    }

    auto create_texture(Context context, TextureDesc const& desc, Texture& out_texture) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_texture.handle == nullptr);
        ASSERT(desc.size.width != 0u);
        ASSERT(desc.size.height != 0u);
        ASSERT(desc.bytes_per_row >= desc.size.width * 4u);
        ASSERT(desc.rgba_pixels != nullptr);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_texture(context, desc, out_texture);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_texture);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_texture(Context context, Texture& texture) -> void {
        ASSERT(context_valid(context));
        ASSERT(texture_valid(texture));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_texture(context, texture);
#else
        BASE_UNUSED(context);
        texture.handle = nullptr;
#endif
    }

    auto create_sampler(Context context, Sampler& out_sampler) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_sampler.handle == nullptr);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_sampler(context, out_sampler);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(out_sampler);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_sampler(Context context, Sampler& sampler) -> void {
        ASSERT(context_valid(context));
        ASSERT(sampler_valid(sampler));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_sampler(context, sampler);
#else
        BASE_UNUSED(context);
        sampler.handle = nullptr;
#endif
    }

    auto create_shader(Arena& arena, Context context, ShaderDesc const& desc, Shader& out_shader)
        -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_shader.handle == nullptr);
        ASSERT(desc.bytecode != nullptr);
        ASSERT(desc.byte_size != 0u);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_shader(arena, context, desc, out_shader);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_shader(Context context, Shader& shader) -> void {
        ASSERT(context_valid(context));
        ASSERT(shader_valid(shader));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_shader(context, shader);
#else
        BASE_UNUSED(context);
        shader.handle = nullptr;
#endif
    }

    auto create_shader_from_source(Arena& arena,
                                   Context context,
                                   ShaderSourceDesc const& desc,
                                   Shader& out_shader) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_shader.handle == nullptr);
        ASSERT(!desc.source.empty());
        ASSERT(desc.entry_point != nullptr);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_shader_from_source(arena, context, desc, out_shader);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto
    create_pipeline(Arena& arena, Context context, PipelineDesc const& desc, Pipeline& out_pipeline)
        -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_pipeline.handle == nullptr);
        ASSERT(shader_valid(desc.vertex_shader));
        ASSERT(shader_valid(desc.pixel_shader));
        ASSERT(desc.vertex_attribute_count == 0u || desc.vertex_attributes != nullptr);

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_pipeline(arena, context, desc, out_pipeline);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_pipeline(Context context, Pipeline& pipeline) -> void {
        ASSERT(context_valid(context));
        ASSERT(pipeline_valid(pipeline));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_pipeline(context, pipeline);
#else
        BASE_UNUSED(context);
        pipeline.handle = nullptr;
#endif
    }

    auto bind_pipeline(Context context, Pipeline pipeline) -> void {
        ASSERT(context_valid(context));
        ASSERT(pipeline_valid(pipeline));

#if BASE_PLATFORM_WINDOWS
        d3d11::bind_pipeline(context, pipeline);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(pipeline);
#endif
    }

    auto create_bind_group(Arena& arena,
                           Context context,
                           BindGroupDesc const& desc,
                           BindGroup& out_group) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_group.handle == nullptr);
        ASSERT(desc.buffer_count == 0u || desc.buffers != nullptr);
        ASSERT(desc.texture_count == 0u || desc.textures != nullptr);
        ASSERT(desc.sampler_count == 0u || desc.samplers != nullptr);

        for (size_t index = 0u; index < desc.buffer_count; ++index) {
            ASSERT(buffer_valid(desc.buffers[index].buffer));
        }
        for (size_t index = 0u; index < desc.texture_count; ++index) {
            ASSERT(texture_valid(desc.textures[index].texture));
        }
        for (size_t index = 0u; index < desc.sampler_count; ++index) {
            ASSERT(sampler_valid(desc.samplers[index].sampler));
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_bind_group(arena, context, desc, out_group);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_group);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_bind_group(Context context, BindGroup& group) -> void {
        ASSERT(context_valid(context));
        ASSERT(bind_group_valid(group));

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_bind_group(context, group);
#else
        BASE_UNUSED(context);
        group.handle = nullptr;
#endif
    }

    auto bind_group(Context context, BindGroup group) -> void {
        ASSERT(context_valid(context));
        ASSERT(bind_group_valid(group));

#if BASE_PLATFORM_WINDOWS
        d3d11::bind_group(context, group);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(group);
#endif
    }

    auto draw(Context context, DrawDesc const& desc) -> void {
        ASSERT(context_valid(context));
        ASSERT(pipeline_valid(desc.pipeline));
        ASSERT(desc.vertex_count != 0u);
        ASSERT(desc.vertex_buffer_count == 0u || desc.vertex_buffers != nullptr);
        ASSERT(desc.bind_group_count == 0u || desc.bind_groups != nullptr);

        for (size_t index = 0u; index < desc.vertex_buffer_count; ++index) {
            ASSERT(buffer_valid(desc.vertex_buffers[index].buffer));
            ASSERT(desc.vertex_buffers[index].byte_stride != 0u);
        }
        for (size_t index = 0u; index < desc.bind_group_count; ++index) {
            ASSERT(bind_group_valid(desc.bind_groups[index]));
        }

#if BASE_PLATFORM_WINDOWS
        d3d11::draw(context, desc);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
#endif
    }

    auto resize_window(Context context, Window window, SizeU32 size) -> Result {
        ASSERT(context_valid(context));
        ASSERT(window_valid(window));
        ASSERT(size.width != 0u);
        ASSERT(size.height != 0u);

#if BASE_PLATFORM_WINDOWS
        return d3d11::resize_window(context, window, size);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(window);
        BASE_UNUSED(size);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto begin_frame(Context context) -> void {
        ASSERT(context_valid(context));

#if BASE_PLATFORM_WINDOWS
        d3d11::begin_frame(context);
#else
        BASE_UNUSED(context);
#endif
    }

    auto begin_render_pass(Context context, RenderPassDesc const& desc) -> Result {
        ASSERT(context_valid(context));
        ASSERT(window_valid(desc.color.window));

#if BASE_PLATFORM_WINDOWS
        return d3d11::begin_render_pass(context, desc);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto end_render_pass(Context context) -> void {
        ASSERT(context_valid(context));

#if BASE_PLATFORM_WINDOWS
        d3d11::end_render_pass(context);
#else
        BASE_UNUSED(context);
#endif
    }

    auto clear_window(Context context, Window window, Color color) -> Result {
        ASSERT(context_valid(context));
        ASSERT(window_valid(window));

        RenderPassDesc desc = {};
        desc.color.window = window;
        desc.color.load_op = LoadOp::CLEAR;
        desc.color.clear_color = color;

        Result const result = begin_render_pass(context, desc);
        if (result_failed(result)) {
            return result;
        }

        end_render_pass(context);
        return Result::OK;
    }

    auto present_window(Window window) -> Result {
        ASSERT(window_valid(window));

#if BASE_PLATFORM_WINDOWS
        return d3d11::present_window(window);
#else
        BASE_UNUSED(window);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto window_size(Window window) -> SizeU32 {
        if (!window_valid(window)) {
            return {};
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::window_size(window);
#else
        BASE_UNUSED(window);
        return {};
#endif
    }

    auto native_device(Context context) -> void* {
        if (!context_valid(context)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_device(context);
#else
        BASE_UNUSED(context);
        return nullptr;
#endif
    }

    auto native_device_context(Context context) -> void* {
        if (!context_valid(context)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_device_context(context);
#else
        BASE_UNUSED(context);
        return nullptr;
#endif
    }

    auto native_swap_chain(Window window) -> void* {
        if (!window_valid(window)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_swap_chain(window);
#else
        BASE_UNUSED(window);
        return nullptr;
#endif
    }

    auto native_render_target_view(Window window) -> void* {
        if (!window_valid(window)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_render_target_view(window);
#else
        BASE_UNUSED(window);
        return nullptr;
#endif
    }

} // namespace gui::render
