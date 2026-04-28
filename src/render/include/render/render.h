#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

namespace gui::render {

    enum class Backend : uint8_t {
        D3D11,
        D3D12,
    };

    enum class Result : int8_t {
        OK = 0,
        OCCLUDED = 1,

        UNSUPPORTED_PLATFORM = -1,
        UNSUPPORTED_BACKEND = -2,
        OUT_OF_MEMORY = -3,
        DEVICE_CREATION_FAILED = -4,
        FACTORY_CREATION_FAILED = -5,
        WINDOW_CREATION_FAILED = -6,
        RENDER_TARGET_CREATION_FAILED = -7,
        RESIZE_FAILED = -8,
        PRESENT_FAILED = -9,
        BUFFER_CREATION_FAILED = -10,
        SHADER_CREATION_FAILED = -11,
        PIPELINE_CREATION_FAILED = -12,
        SHADER_COMPILATION_FAILED = -13,
        TEXTURE_CREATION_FAILED = -14,
        SAMPLER_CREATION_FAILED = -15,
    };

    enum class PresentMode : uint8_t {
        IMMEDIATE,
        VSYNC,
    };

    enum class LoadOp : uint8_t {
        LOAD,
        CLEAR,
        // Initial contents are undefined. The backend may preserve, clear, or discard them.
        DONT_CARE,
    };

    enum class BufferBinding : uint8_t {
        VERTEX,
        UNIFORM,
    };

    enum class BufferUsage : uint8_t {
        IMMUTABLE,
        DYNAMIC,
    };

    enum class ShaderStage : uint8_t {
        VERTEX,
        PIXEL,
    };

    enum class VertexFormat : uint8_t {
        FLOAT32_2,
        FLOAT32_3,
        FLOAT32_4,
    };

    enum class PrimitiveTopology : uint8_t {
        TRIANGLE_LIST,
    };

    enum class BlendMode : uint8_t {
        OPAQUE,
        ALPHA,
    };

    struct SizeU32 {
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    struct ContextDesc {
        Backend backend = Backend::D3D11;
        bool enable_debug_layer = false;
    };

    struct WindowDesc {
        // HWND on Windows. Kept opaque so this header does not include windows.h.
        void* native_window = nullptr;
        SizeU32 size = {};
        uint32_t buffer_count = 2u;
        PresentMode present_mode = PresentMode::VSYNC;
    };

    struct Context {
        void* handle = nullptr;
    };

    struct Window {
        void* handle = nullptr;
    };

    struct Buffer {
        void* handle = nullptr;
    };

    struct FrameBufferSlice {
        Buffer buffer = {};
        void* data = nullptr;
        size_t byte_offset = 0u;
        size_t byte_size = 0u;
    };

    struct Texture {
        void* handle = nullptr;
    };

    struct Sampler {
        void* handle = nullptr;
    };

    struct Shader {
        void* handle = nullptr;
    };

    struct Pipeline {
        void* handle = nullptr;
    };

    struct BindGroup {
        void* handle = nullptr;
    };

    struct WindowRenderPassDesc {
        Window window = {};
        LoadOp load_op = LoadOp::CLEAR;
        Color clear_color = {};
    };

    struct BufferDesc {
        BufferBinding binding = BufferBinding::VERTEX;
        BufferUsage usage = BufferUsage::IMMUTABLE;
        size_t byte_size = 0u;
        void const* initial_data = nullptr;
    };

    struct TextureDesc {
        SizeU32 size = {};
        uint32_t bytes_per_row = 0u;
        void const* rgba_pixels = nullptr;
    };

    struct ShaderDesc {
        ShaderStage stage = ShaderStage::VERTEX;
        void const* bytecode = nullptr;
        size_t byte_size = 0u;
    };

    struct ShaderSourceDesc {
        ShaderStage stage = ShaderStage::VERTEX;
        StrRef source = {};
        char const* entry_point = nullptr;
    };

    struct VertexAttributeDesc {
        char const* semantic_name = nullptr;
        uint32_t semantic_index = 0u;
        VertexFormat format = VertexFormat::FLOAT32_2;
        uint32_t buffer_slot = 0u;
        uint32_t byte_offset = 0u;
    };

    struct PipelineDesc {
        Shader vertex_shader = {};
        Shader pixel_shader = {};
        VertexAttributeDesc const* vertex_attributes = nullptr;
        size_t vertex_attribute_count = 0u;
        PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
        BlendMode blend_mode = BlendMode::OPAQUE;
    };

    struct BindGroupBufferBinding {
        ShaderStage stage = ShaderStage::VERTEX;
        uint32_t slot = 0u;
        Buffer buffer = {};
    };

    struct BindGroupTextureBinding {
        ShaderStage stage = ShaderStage::PIXEL;
        uint32_t slot = 0u;
        Texture texture = {};
    };

    struct BindGroupSamplerBinding {
        ShaderStage stage = ShaderStage::PIXEL;
        uint32_t slot = 0u;
        Sampler sampler = {};
    };

    struct VertexBufferBinding {
        Buffer buffer = {};
        uint32_t slot = 0u;
        uint32_t byte_stride = 0u;
        uint32_t byte_offset = 0u;
    };

    // Resource handles are referenced, not owned.
    struct BindGroupDesc {
        BindGroupBufferBinding const* buffers = nullptr;
        size_t buffer_count = 0u;
        BindGroupTextureBinding const* textures = nullptr;
        size_t texture_count = 0u;
        BindGroupSamplerBinding const* samplers = nullptr;
        size_t sampler_count = 0u;
    };

    struct DrawDesc {
        Pipeline pipeline = {};
        VertexBufferBinding const* vertex_buffers = nullptr;
        size_t vertex_buffer_count = 0u;
        BindGroup const* bind_groups = nullptr;
        size_t bind_group_count = 0u;
        uint32_t vertex_count = 0u;
        uint32_t first_vertex = 0u;
    };

    [[nodiscard]] auto result_succeeded(Result result) -> bool;
    [[nodiscard]] auto result_failed(Result result) -> bool;
    [[nodiscard]] auto result_name(Result result) -> char const*;
    [[nodiscard]] auto backend_name(Backend backend) -> char const*;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto window_valid(Window window) -> bool;
    [[nodiscard]] auto buffer_valid(Buffer buffer) -> bool;
    [[nodiscard]] auto texture_valid(Texture texture) -> bool;
    [[nodiscard]] auto sampler_valid(Sampler sampler) -> bool;
    [[nodiscard]] auto shader_valid(Shader shader) -> bool;
    [[nodiscard]] auto pipeline_valid(Pipeline pipeline) -> bool;
    [[nodiscard]] auto bind_group_valid(BindGroup bind_group) -> bool;

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

} // namespace gui::render
