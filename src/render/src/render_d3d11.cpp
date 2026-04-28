#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "render_d3d11.h"

#include "render_backend.h"

#include <algorithm>
#include <base/config.h>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>

namespace gui::render::d3d11 {
    namespace {

        constexpr size_t FRAME_VERTEX_BUFFER_DEFAULT_SIZE = 64u * 1024u;
        constexpr size_t MAX_D3D11_BUFFER_BYTE_SIZE = 0xffffffffu;
        constexpr size_t D3D11_UNIFORM_BUFFER_ALIGNMENT = 16u;

        struct D3D11Context;

        struct D3D11Buffer {
            BufferHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            ID3D11Buffer* resource = nullptr;
            BufferBinding binding = BufferBinding::VERTEX;
            BufferUsage usage = BufferUsage::IMMUTABLE;
            size_t byte_size = 0u;
        };

        struct D3D11FrameBuffer {
            D3D11Buffer buffer = {};
            uint8_t* mapped_data = nullptr;
            size_t capacity = 0u;
            size_t used_size = 0u;
        };

        struct D3D11Context {
            ContextHeader header = {Backend::D3D11};
            Arena* arena = nullptr;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* device_context = nullptr;
            IDXGIFactory* factory = nullptr;
            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
            D3D11FrameBuffer frame_vertex_buffer = {};
            bool render_pass_active = false;
        };

        struct D3D11Window {
            WindowHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            IDXGISwapChain* swap_chain = nullptr;
            ID3D11RenderTargetView* render_target_view = nullptr;
            SizeU32 size = {};
            PresentMode present_mode = PresentMode::VSYNC;
        };

        struct D3D11Texture {
            TextureHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            ID3D11ShaderResourceView* view = nullptr;
        };

        struct D3D11Sampler {
            SamplerHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            ID3D11SamplerState* state = nullptr;
        };

        struct D3D11Shader {
            ShaderHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            ID3D11VertexShader* vertex_shader = nullptr;
            ID3D11PixelShader* pixel_shader = nullptr;
            void const* bytecode = nullptr;
            size_t byte_size = 0u;
            ShaderStage stage = ShaderStage::VERTEX;
        };

        struct D3D11Pipeline {
            PipelineHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            ID3D11VertexShader* vertex_shader = nullptr;
            ID3D11PixelShader* pixel_shader = nullptr;
            ID3D11InputLayout* input_layout = nullptr;
            ID3D11BlendState* blend_state = nullptr;
            PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
        };

        struct D3D11BufferBinding {
            D3D11Buffer* buffer = nullptr;
            ShaderStage stage = ShaderStage::VERTEX;
            uint32_t slot = 0u;
        };

        struct D3D11TextureBinding {
            D3D11Texture* texture = nullptr;
            ShaderStage stage = ShaderStage::PIXEL;
            uint32_t slot = 0u;
        };

        struct D3D11SamplerBinding {
            D3D11Sampler* sampler = nullptr;
            ShaderStage stage = ShaderStage::PIXEL;
            uint32_t slot = 0u;
        };

        struct D3D11BindGroup {
            BindGroupHeader header = {Backend::D3D11};
            D3D11Context* context = nullptr;
            D3D11BufferBinding* buffers = nullptr;
            size_t buffer_count = 0u;
            D3D11TextureBinding* textures = nullptr;
            size_t texture_count = 0u;
            D3D11SamplerBinding* samplers = nullptr;
            size_t sampler_count = 0u;
        };

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
            }
        }

        [[nodiscard]] auto context_from_handle(Context context) -> D3D11Context* {
            ASSERT(context_backend(context) == Backend::D3D11);
            return static_cast<D3D11Context*>(context.handle);
        }

        [[nodiscard]] auto window_from_handle(Window window) -> D3D11Window* {
            ASSERT(window_backend(window) == Backend::D3D11);
            return static_cast<D3D11Window*>(window.handle);
        }

        [[nodiscard]] auto buffer_from_handle(Buffer buffer) -> D3D11Buffer* {
            ASSERT(buffer_backend(buffer) == Backend::D3D11);
            return static_cast<D3D11Buffer*>(buffer.handle);
        }

        [[nodiscard]] auto texture_from_handle(Texture texture) -> D3D11Texture* {
            ASSERT(texture_backend(texture) == Backend::D3D11);
            return static_cast<D3D11Texture*>(texture.handle);
        }

        [[nodiscard]] auto sampler_from_handle(Sampler sampler) -> D3D11Sampler* {
            ASSERT(sampler_backend(sampler) == Backend::D3D11);
            return static_cast<D3D11Sampler*>(sampler.handle);
        }

        [[nodiscard]] auto shader_from_handle(Shader shader) -> D3D11Shader* {
            ASSERT(shader_backend(shader) == Backend::D3D11);
            return static_cast<D3D11Shader*>(shader.handle);
        }

        [[nodiscard]] auto pipeline_from_handle(Pipeline pipeline) -> D3D11Pipeline* {
            ASSERT(pipeline_backend(pipeline) == Backend::D3D11);
            return static_cast<D3D11Pipeline*>(pipeline.handle);
        }

        [[nodiscard]] auto bind_group_from_handle(BindGroup bind_group) -> D3D11BindGroup* {
            ASSERT(bind_group_backend(bind_group) == Backend::D3D11);
            return static_cast<D3D11BindGroup*>(bind_group.handle);
        }

        [[nodiscard]] auto buffer_byte_width(BufferDesc const& desc, UINT& out_byte_width) -> bool {
            size_t byte_width = desc.byte_size;
            if (desc.binding == BufferBinding::UNIFORM) {
                if (byte_width >
                    MAX_D3D11_BUFFER_BYTE_SIZE - (D3D11_UNIFORM_BUFFER_ALIGNMENT - 1u)) {
                    return false;
                }
                byte_width = (byte_width + (D3D11_UNIFORM_BUFFER_ALIGNMENT - 1u)) &
                             ~(D3D11_UNIFORM_BUFFER_ALIGNMENT - 1u);
            }
            if (byte_width > MAX_D3D11_BUFFER_BYTE_SIZE) {
                return false;
            }
            out_byte_width = static_cast<UINT>(byte_width);
            return true;
        }

        [[nodiscard]] auto align_up(size_t value, size_t alignment) -> size_t {
            return ((value + alignment - 1u) / alignment) * alignment;
        }

        auto commit_frame_buffer(D3D11Context* context, D3D11FrameBuffer* frame_buffer) -> void {
            if (frame_buffer->mapped_data != nullptr) {
                context->device_context->Unmap(frame_buffer->buffer.resource, 0u);
                frame_buffer->mapped_data = nullptr;
            }
        }

        auto release_frame_buffer(D3D11Context* context, D3D11FrameBuffer* frame_buffer) -> void {
            commit_frame_buffer(context, frame_buffer);
            release_com(frame_buffer->buffer.resource);
            frame_buffer->buffer.context = nullptr;
            frame_buffer->buffer.byte_size = 0u;
            frame_buffer->capacity = 0u;
            frame_buffer->used_size = 0u;
        }

        auto create_frame_vertex_buffer(D3D11Context* context, size_t byte_size) -> void {
            ASSERT(byte_size <= MAX_D3D11_BUFFER_BYTE_SIZE);

            D3D11FrameBuffer& frame_buffer = context->frame_vertex_buffer;
            release_frame_buffer(context, &frame_buffer);

            D3D11_BUFFER_DESC buffer_desc = {};
            buffer_desc.ByteWidth = static_cast<UINT>(byte_size);
            buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
            buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            ID3D11Buffer* resource = nullptr;
            HRESULT const hr = context->device->CreateBuffer(&buffer_desc, nullptr, &resource);
            ASSERT(SUCCEEDED(hr));
            ASSERT(resource != nullptr);

            frame_buffer.buffer.context = context;
            frame_buffer.buffer.resource = resource;
            frame_buffer.buffer.binding = BufferBinding::VERTEX;
            frame_buffer.buffer.usage = BufferUsage::DYNAMIC;
            frame_buffer.buffer.byte_size = byte_size;
            frame_buffer.capacity = byte_size;
        }

        [[nodiscard]] auto d3d_format(VertexFormat format) -> DXGI_FORMAT {
            switch (format) {
            case VertexFormat::FLOAT32_2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case VertexFormat::FLOAT32_3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case VertexFormat::FLOAT32_4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }

            return DXGI_FORMAT_UNKNOWN;
        }

        [[nodiscard]] auto d3d_topology(PrimitiveTopology topology) -> D3D_PRIMITIVE_TOPOLOGY {
            switch (topology) {
            case PrimitiveTopology::TRIANGLE_LIST:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }

            return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }

        [[nodiscard]] auto shader_target(ShaderStage stage) -> char const* {
            switch (stage) {
            case ShaderStage::VERTEX:
                return "vs_4_0";
            case ShaderStage::PIXEL:
                return "ps_4_0";
            }

            return nullptr;
        }

        [[nodiscard]] auto try_create_device(D3D_DRIVER_TYPE driver_type,
                                             UINT creation_flags,
                                             D3D11Context& out_context) -> HRESULT {
            D3D_FEATURE_LEVEL levels_with_11_1[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };
            D3D_FEATURE_LEVEL levels_without_11_1[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };

            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* device_context = nullptr;
            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;

            HRESULT hr = D3D11CreateDevice(
                nullptr,
                driver_type,
                nullptr,
                creation_flags,
                levels_with_11_1,
                static_cast<UINT>(sizeof(levels_with_11_1) / sizeof(levels_with_11_1[0u])),
                D3D11_SDK_VERSION,
                &device,
                &feature_level,
                &device_context);

            if (hr == E_INVALIDARG) {
                release_com(device);
                release_com(device_context);
                hr = D3D11CreateDevice(nullptr,
                                       driver_type,
                                       nullptr,
                                       creation_flags,
                                       levels_without_11_1,
                                       static_cast<UINT>(sizeof(levels_without_11_1) /
                                                         sizeof(levels_without_11_1[0u])),
                                       D3D11_SDK_VERSION,
                                       &device,
                                       &feature_level,
                                       &device_context);
            }

            if (FAILED(hr)) {
                release_com(device);
                release_com(device_context);
                return hr;
            }

            out_context.device = device;
            out_context.device_context = device_context;
            out_context.feature_level = feature_level;
            return hr;
        }

        [[nodiscard]] auto init_factory(D3D11Context* context) -> bool {
            IDXGIDevice* dxgi_device = nullptr;
            IDXGIAdapter* adapter = nullptr;

            HRESULT hr = context->device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
            if (SUCCEEDED(hr)) {
                hr = dxgi_device->GetAdapter(&adapter);
            }
            if (SUCCEEDED(hr)) {
                hr = adapter->GetParent(IID_PPV_ARGS(&context->factory));
            }

            release_com(adapter);
            release_com(dxgi_device);
            return SUCCEEDED(hr) && context->factory != nullptr;
        }

        auto destroy_context_impl(D3D11Context* context) -> void {
            if (context->device_context != nullptr) {
                context->device_context->ClearState();
            }

            release_frame_buffer(context, &context->frame_vertex_buffer);
            release_com(context->factory);
            release_com(context->device_context);
            release_com(context->device);
        }

        [[nodiscard]] auto create_render_target(D3D11Context* context, D3D11Window* window)
            -> Result {
            ID3D11Texture2D* back_buffer = nullptr;
            HRESULT hr = window->swap_chain->GetBuffer(0u, IID_PPV_ARGS(&back_buffer));
            if (FAILED(hr)) {
                return Result::RENDER_TARGET_CREATION_FAILED;
            }

            hr = context->device->CreateRenderTargetView(
                back_buffer, nullptr, &window->render_target_view);
            release_com(back_buffer);

            if (FAILED(hr) || window->render_target_view == nullptr) {
                return Result::RENDER_TARGET_CREATION_FAILED;
            }

            return Result::OK;
        }

        auto release_render_target(D3D11Window* window) -> void {
            release_com(window->render_target_view);
        }

        auto destroy_window_impl(D3D11Window* window) -> void {
            release_render_target(window);
            release_com(window->swap_chain);
            window->context = nullptr;
            window->size = {};
            window->present_mode = PresentMode::VSYNC;
        }

        auto destroy_shader_impl(D3D11Shader* shader) -> void {
            release_com(shader->pixel_shader);
            release_com(shader->vertex_shader);
            shader->context = nullptr;
            shader->bytecode = nullptr;
            shader->byte_size = 0u;
            shader->stage = ShaderStage::VERTEX;
        }

        auto destroy_pipeline_impl(D3D11Pipeline* pipeline) -> void {
            release_com(pipeline->blend_state);
            release_com(pipeline->input_layout);
            release_com(pipeline->pixel_shader);
            release_com(pipeline->vertex_shader);
            pipeline->context = nullptr;
            pipeline->topology = PrimitiveTopology::TRIANGLE_LIST;
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        ArenaMarker const marker = arena.marker();
        D3D11Context* context = arena_new<D3D11Context>(arena);
        context->arena = &arena;

        UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (desc.enable_debug_layer) {
            creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        HRESULT hr = try_create_device(D3D_DRIVER_TYPE_HARDWARE, creation_flags, *context);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && desc.enable_debug_layer) {
            creation_flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            hr = try_create_device(D3D_DRIVER_TYPE_HARDWARE, creation_flags, *context);
        }

        if (FAILED(hr)) {
            hr = try_create_device(D3D_DRIVER_TYPE_WARP, creation_flags, *context);
            if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING &&
                (creation_flags & D3D11_CREATE_DEVICE_DEBUG) != 0u) {
                creation_flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
                hr = try_create_device(D3D_DRIVER_TYPE_WARP, creation_flags, *context);
            }
        }

        if (FAILED(hr)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return Result::DEVICE_CREATION_FAILED;
        }

        if (!init_factory(context)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return Result::FACTORY_CREATION_FAILED;
        }

        out_context.handle = context;
        return Result::OK;
    }

    auto destroy_context(Context& context) -> void {
        D3D11Context* impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        destroy_context_impl(impl);
        context.handle = nullptr;
    }

    auto create_window(Arena& arena, Context context, WindowDesc const& desc, Window& out_window)
        -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        ArenaMarker const marker = arena.marker();
        D3D11Window* window = arena_new<D3D11Window>(arena);
        window->context = context_impl;

        DXGI_SWAP_CHAIN_DESC swap_desc = {};
        swap_desc.BufferCount = desc.buffer_count;
        swap_desc.BufferDesc.Width = desc.size.width;
        swap_desc.BufferDesc.Height = desc.size.height;
        swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferDesc.RefreshRate.Numerator = 60u;
        swap_desc.BufferDesc.RefreshRate.Denominator = 1u;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.OutputWindow = static_cast<HWND>(desc.native_window);
        swap_desc.SampleDesc.Count = 1u;
        swap_desc.SampleDesc.Quality = 0u;
        swap_desc.Windowed = TRUE;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = context_impl->factory->CreateSwapChain(
            context_impl->device, &swap_desc, &window->swap_chain);
        if (FAILED(hr) || window->swap_chain == nullptr) {
            arena.reset_to(marker);
            return Result::WINDOW_CREATION_FAILED;
        }

        context_impl->factory->MakeWindowAssociation(swap_desc.OutputWindow, DXGI_MWA_NO_ALT_ENTER);

        Result const render_target_result = create_render_target(context_impl, window);
        if (result_failed(render_target_result)) {
            destroy_window_impl(window);
            arena.reset_to(marker);
            return render_target_result;
        }

        window->size = desc.size;
        window->present_mode = desc.present_mode;
        out_window.handle = window;
        return Result::OK;
    }

    auto destroy_window(Window& window) -> void {
        D3D11Window* impl = window_from_handle(window);
        ASSERT(impl != nullptr);
        destroy_window_impl(impl);
        window.handle = nullptr;
    }

    auto create_buffer(Context context, BufferDesc const& desc, Buffer& out_buffer) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        UINT byte_width = 0u;
        if (!buffer_byte_width(desc, byte_width)) {
            return Result::BUFFER_CREATION_FAILED;
        }

        D3D11_BUFFER_DESC buffer_desc = {};
        buffer_desc.ByteWidth = byte_width;
        buffer_desc.Usage =
            desc.usage == BufferUsage::DYNAMIC ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        buffer_desc.BindFlags = desc.binding == BufferBinding::UNIFORM ? D3D11_BIND_CONSTANT_BUFFER
                                                                       : D3D11_BIND_VERTEX_BUFFER;
        buffer_desc.CPUAccessFlags =
            desc.usage == BufferUsage::DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0u;

        D3D11_SUBRESOURCE_DATA initial_data = {};
        initial_data.pSysMem = desc.initial_data;

        ID3D11Buffer* resource = nullptr;
        HRESULT const hr = context_impl->device->CreateBuffer(
            &buffer_desc, desc.initial_data != nullptr ? &initial_data : nullptr, &resource);
        if (FAILED(hr) || resource == nullptr) {
            return Result::BUFFER_CREATION_FAILED;
        }

        D3D11Buffer* buffer = arena_new<D3D11Buffer>(*context_impl->arena);
        buffer->context = context_impl;
        buffer->resource = resource;
        buffer->binding = desc.binding;
        buffer->usage = desc.usage;
        buffer->byte_size = std::min(desc.byte_size, static_cast<size_t>(byte_width));
        out_buffer.handle = buffer;
        return Result::OK;
    }

    auto destroy_buffer(Context context, Buffer& buffer) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Buffer* impl = buffer_from_handle(buffer);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        release_com(impl->resource);
        impl->context = nullptr;
        impl->byte_size = 0u;
        buffer.handle = nullptr;
    }

    auto update_buffer(Context context, Buffer buffer, void const* data, size_t byte_size) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Buffer* buffer_impl = buffer_from_handle(buffer);
        ASSERT(context_impl != nullptr);
        ASSERT(buffer_impl != nullptr);
        ASSERT(buffer_impl->context == context_impl);
        ASSERT(buffer_impl->resource != nullptr);
        ASSERT(buffer_impl->usage == BufferUsage::DYNAMIC);
        ASSERT(byte_size <= buffer_impl->byte_size);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT const hr = context_impl->device_context->Map(
            buffer_impl->resource, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        ASSERT(SUCCEEDED(hr));

        std::memcpy(mapped.pData, data, byte_size);
        context_impl->device_context->Unmap(buffer_impl->resource, 0u);
    }

    auto allocate_frame_vertex_buffer(Context context, size_t byte_size, size_t byte_alignment)
        -> FrameBufferSlice {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D11FrameBuffer& frame_buffer = context_impl->frame_vertex_buffer;
        size_t const offset = align_up(frame_buffer.used_size, byte_alignment);
        size_t const needed_size = offset + byte_size;
        if (needed_size > frame_buffer.capacity) {
            ASSERT(frame_buffer.used_size == 0u);
            ASSERT(frame_buffer.mapped_data == nullptr);
            size_t const capacity = std::max(needed_size, FRAME_VERTEX_BUFFER_DEFAULT_SIZE);
            create_frame_vertex_buffer(context_impl, capacity);
        }

        if (frame_buffer.mapped_data == nullptr) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            D3D11_MAP const map_type = frame_buffer.used_size == 0u ? D3D11_MAP_WRITE_DISCARD
                                                                    : D3D11_MAP_WRITE_NO_OVERWRITE;
            HRESULT const hr = context_impl->device_context->Map(
                frame_buffer.buffer.resource, 0u, map_type, 0u, &mapped);
            ASSERT(SUCCEEDED(hr));
            frame_buffer.mapped_data = static_cast<uint8_t*>(mapped.pData);
        }

        frame_buffer.used_size = needed_size;
        return {{&frame_buffer.buffer}, frame_buffer.mapped_data + offset, offset, byte_size};
    }

    auto commit_frame_uploads(Context context) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        commit_frame_buffer(context_impl, &context_impl->frame_vertex_buffer);
    }

    auto create_texture(Context context, TextureDesc const& desc, Texture& out_texture) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D11_TEXTURE2D_DESC texture_desc = {};
        texture_desc.Width = desc.size.width;
        texture_desc.Height = desc.size.height;
        texture_desc.MipLevels = 1u;
        texture_desc.ArraySize = 1u;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1u;
        texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
        texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA texture_data = {};
        texture_data.pSysMem = desc.rgba_pixels;
        texture_data.SysMemPitch = desc.bytes_per_row;

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = context_impl->device->CreateTexture2D(&texture_desc, &texture_data, &texture);
        if (FAILED(hr) || texture == nullptr) {
            release_com(texture);
            return Result::TEXTURE_CREATION_FAILED;
        }

        ID3D11ShaderResourceView* view = nullptr;
        hr = context_impl->device->CreateShaderResourceView(texture, nullptr, &view);
        release_com(texture);
        if (FAILED(hr) || view == nullptr) {
            release_com(view);
            return Result::TEXTURE_CREATION_FAILED;
        }

        D3D11Texture* texture_impl = arena_new<D3D11Texture>(*context_impl->arena);
        texture_impl->context = context_impl;
        texture_impl->view = view;
        out_texture.handle = texture_impl;
        return Result::OK;
    }

    auto destroy_texture(Context context, Texture& texture) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Texture* impl = texture_from_handle(texture);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        release_com(impl->view);
        impl->context = nullptr;
        texture.handle = nullptr;
    }

    auto create_sampler(Context context, Sampler& out_sampler) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

        ID3D11SamplerState* sampler = nullptr;
        HRESULT const hr = context_impl->device->CreateSamplerState(&sampler_desc, &sampler);
        if (FAILED(hr) || sampler == nullptr) {
            release_com(sampler);
            return Result::SAMPLER_CREATION_FAILED;
        }

        D3D11Sampler* sampler_impl = arena_new<D3D11Sampler>(*context_impl->arena);
        sampler_impl->context = context_impl;
        sampler_impl->state = sampler;
        out_sampler.handle = sampler_impl;
        return Result::OK;
    }

    auto destroy_sampler(Context context, Sampler& sampler) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Sampler* impl = sampler_from_handle(sampler);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        release_com(impl->state);
        impl->context = nullptr;
        sampler.handle = nullptr;
    }

    auto create_shader(Arena& arena, Context context, ShaderDesc const& desc, Shader& out_shader)
        -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        ArenaMarker const marker = arena.marker();
        D3D11Shader* shader = arena_new<D3D11Shader>(arena);
        uint8_t* const bytecode = arena_alloc<uint8_t>(arena, desc.byte_size);
        std::memcpy(bytecode, desc.bytecode, desc.byte_size);

        shader->context = context_impl;
        shader->bytecode = bytecode;
        shader->byte_size = desc.byte_size;
        shader->stage = desc.stage;

        HRESULT hr = E_INVALIDARG;
        switch (desc.stage) {
        case ShaderStage::VERTEX:
            hr = context_impl->device->CreateVertexShader(
                bytecode, desc.byte_size, nullptr, &shader->vertex_shader);
            break;
        case ShaderStage::PIXEL:
            hr = context_impl->device->CreatePixelShader(
                bytecode, desc.byte_size, nullptr, &shader->pixel_shader);
            break;
        }

        if (FAILED(hr)) {
            destroy_shader_impl(shader);
            arena.reset_to(marker);
            return Result::SHADER_CREATION_FAILED;
        }

        out_shader.handle = shader;
        return Result::OK;
    }

    auto create_shader_from_source(Arena& arena,
                                   Context context,
                                   ShaderSourceDesc const& desc,
                                   Shader& out_shader) -> Result {
        ID3DBlob* shader_blob = nullptr;
        UINT compile_flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if BASE_DEBUG
        compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        char const* const target = shader_target(desc.stage);
        ASSERT(target != nullptr);

        HRESULT const hr = D3DCompile(desc.source.data(),
                                      desc.source.size(),
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      desc.entry_point,
                                      target,
                                      compile_flags,
                                      0u,
                                      &shader_blob,
                                      nullptr);
        if (FAILED(hr) || shader_blob == nullptr) {
            release_com(shader_blob);
            return Result::SHADER_COMPILATION_FAILED;
        }

        ShaderDesc shader_desc = {};
        shader_desc.stage = desc.stage;
        shader_desc.bytecode = shader_blob->GetBufferPointer();
        shader_desc.byte_size = shader_blob->GetBufferSize();

        Result const result =
            ::gui::render::d3d11::create_shader(arena, context, shader_desc, out_shader);
        release_com(shader_blob);
        return result;
    }

    auto destroy_shader(Context context, Shader& shader) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Shader* impl = shader_from_handle(shader);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        destroy_shader_impl(impl);
        shader.handle = nullptr;
    }

    auto
    create_pipeline(Arena& arena, Context context, PipelineDesc const& desc, Pipeline& out_pipeline)
        -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Shader* vertex_shader = shader_from_handle(desc.vertex_shader);
        D3D11Shader* pixel_shader = shader_from_handle(desc.pixel_shader);
        ASSERT(context_impl != nullptr);
        ASSERT(vertex_shader != nullptr);
        ASSERT(pixel_shader != nullptr);
        ASSERT(vertex_shader->context == context_impl);
        ASSERT(pixel_shader->context == context_impl);
        ASSERT(desc.vertex_attribute_count <= static_cast<size_t>(UINT_MAX));

        if (vertex_shader->stage != ShaderStage::VERTEX ||
            pixel_shader->stage != ShaderStage::PIXEL || vertex_shader->vertex_shader == nullptr ||
            pixel_shader->pixel_shader == nullptr) {
            return Result::PIPELINE_CREATION_FAILED;
        }

        ArenaMarker const marker = arena.marker();
        D3D11Pipeline* pipeline = arena_new<D3D11Pipeline>(arena);
        pipeline->context = context_impl;
        pipeline->topology = desc.topology;

        if (desc.vertex_attribute_count != 0u) {
            ArenaTemp temp = begin_thread_temp_arena();
            D3D11_INPUT_ELEMENT_DESC* input_elements =
                arena_alloc<D3D11_INPUT_ELEMENT_DESC>(*temp.arena(), desc.vertex_attribute_count);

            for (size_t index = 0u; index < desc.vertex_attribute_count; ++index) {
                VertexAttributeDesc const& attribute = desc.vertex_attributes[index];
                ASSERT(attribute.semantic_name != nullptr);

                D3D11_INPUT_ELEMENT_DESC& element = input_elements[index];
                element.SemanticName = attribute.semantic_name;
                element.SemanticIndex = attribute.semantic_index;
                element.Format = d3d_format(attribute.format);
                element.InputSlot = attribute.buffer_slot;
                element.AlignedByteOffset = attribute.byte_offset;
                element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                element.InstanceDataStepRate = 0u;
            }

            HRESULT const hr = context_impl->device->CreateInputLayout(
                input_elements,
                static_cast<UINT>(desc.vertex_attribute_count),
                vertex_shader->bytecode,
                vertex_shader->byte_size,
                &pipeline->input_layout);
            if (FAILED(hr)) {
                destroy_pipeline_impl(pipeline);
                arena.reset_to(marker);
                return Result::PIPELINE_CREATION_FAILED;
            }
        }

        if (desc.blend_mode == BlendMode::ALPHA) {
            D3D11_BLEND_DESC blend_desc = {};
            blend_desc.RenderTarget[0].BlendEnable = TRUE;
            blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

            HRESULT const hr =
                context_impl->device->CreateBlendState(&blend_desc, &pipeline->blend_state);
            if (FAILED(hr)) {
                destroy_pipeline_impl(pipeline);
                arena.reset_to(marker);
                return Result::PIPELINE_CREATION_FAILED;
            }
        }

        pipeline->vertex_shader = vertex_shader->vertex_shader;
        pipeline->pixel_shader = pixel_shader->pixel_shader;
        pipeline->vertex_shader->AddRef();
        pipeline->pixel_shader->AddRef();

        out_pipeline.handle = pipeline;
        return Result::OK;
    }

    auto destroy_pipeline(Context context, Pipeline& pipeline) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Pipeline* impl = pipeline_from_handle(pipeline);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        destroy_pipeline_impl(impl);
        pipeline.handle = nullptr;
    }

    auto bind_pipeline(Context context, Pipeline pipeline) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Pipeline* pipeline_impl = pipeline_from_handle(pipeline);
        ASSERT(context_impl != nullptr);
        ASSERT(pipeline_impl != nullptr);
        ASSERT(pipeline_impl->context == context_impl);

        context_impl->device_context->IASetInputLayout(pipeline_impl->input_layout);
        context_impl->device_context->IASetPrimitiveTopology(d3d_topology(pipeline_impl->topology));
        context_impl->device_context->VSSetShader(pipeline_impl->vertex_shader, nullptr, 0u);
        context_impl->device_context->PSSetShader(pipeline_impl->pixel_shader, nullptr, 0u);
        float blend_factor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        context_impl->device_context->OMSetBlendState(
            pipeline_impl->blend_state, blend_factor, 0xffffffffu);
    }

    auto create_bind_group(Arena& arena,
                           Context context,
                           BindGroupDesc const& desc,
                           BindGroup& out_group) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D11BindGroup* group = arena_new<D3D11BindGroup>(arena);
        group->context = context_impl;

        if (desc.buffer_count != 0u) {
            group->buffers = arena_alloc<D3D11BufferBinding>(arena, desc.buffer_count);
            group->buffer_count = desc.buffer_count;
            for (size_t index = 0u; index < desc.buffer_count; ++index) {
                BindGroupBufferBinding const& source = desc.buffers[index];
                ASSERT(source.slot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
                D3D11Buffer* buffer = buffer_from_handle(source.buffer);
                ASSERT(buffer != nullptr);
                ASSERT(buffer->context == context_impl);
                ASSERT(buffer->resource != nullptr);
                ASSERT(buffer->binding == BufferBinding::UNIFORM);

                D3D11BufferBinding& target = group->buffers[index];
                target.buffer = buffer;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        if (desc.texture_count != 0u) {
            group->textures = arena_alloc<D3D11TextureBinding>(arena, desc.texture_count);
            group->texture_count = desc.texture_count;
            for (size_t index = 0u; index < desc.texture_count; ++index) {
                BindGroupTextureBinding const& source = desc.textures[index];
                ASSERT(source.slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                D3D11Texture* texture = texture_from_handle(source.texture);
                ASSERT(texture != nullptr);
                ASSERT(texture->context == context_impl);
                ASSERT(texture->view != nullptr);

                D3D11TextureBinding& target = group->textures[index];
                target.texture = texture;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        if (desc.sampler_count != 0u) {
            group->samplers = arena_alloc<D3D11SamplerBinding>(arena, desc.sampler_count);
            group->sampler_count = desc.sampler_count;
            for (size_t index = 0u; index < desc.sampler_count; ++index) {
                BindGroupSamplerBinding const& source = desc.samplers[index];
                ASSERT(source.slot < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
                D3D11Sampler* sampler = sampler_from_handle(source.sampler);
                ASSERT(sampler != nullptr);
                ASSERT(sampler->context == context_impl);
                ASSERT(sampler->state != nullptr);

                D3D11SamplerBinding& target = group->samplers[index];
                target.sampler = sampler;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        out_group.handle = group;
        return Result::OK;
    }

    auto destroy_bind_group(Context context, BindGroup& group) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11BindGroup* impl = bind_group_from_handle(group);
        ASSERT(context_impl != nullptr);
        ASSERT(impl != nullptr);
        ASSERT(impl->context == context_impl);
        impl->context = nullptr;
        impl->buffers = nullptr;
        impl->buffer_count = 0u;
        impl->textures = nullptr;
        impl->texture_count = 0u;
        impl->samplers = nullptr;
        impl->sampler_count = 0u;
        group.handle = nullptr;
    }

    auto bind_group(Context context, BindGroup group) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11BindGroup* group_impl = bind_group_from_handle(group);
        ASSERT(context_impl != nullptr);
        ASSERT(group_impl != nullptr);
        ASSERT(group_impl->context == context_impl);

        ID3D11DeviceContext* const device_context = context_impl->device_context;
        for (size_t index = 0u; index < group_impl->buffer_count; ++index) {
            D3D11BufferBinding const& binding = group_impl->buffers[index];
            ASSERT(binding.buffer != nullptr);
            ASSERT(binding.buffer->context == context_impl);
            ASSERT(binding.buffer->resource != nullptr);
            ASSERT(binding.buffer->binding == BufferBinding::UNIFORM);
            ID3D11Buffer* buffer = binding.buffer->resource;
            switch (binding.stage) {
            case ShaderStage::VERTEX:
                device_context->VSSetConstantBuffers(binding.slot, 1u, &buffer);
                break;
            case ShaderStage::PIXEL:
                device_context->PSSetConstantBuffers(binding.slot, 1u, &buffer);
                break;
            }
        }

        for (size_t index = 0u; index < group_impl->texture_count; ++index) {
            D3D11TextureBinding const& binding = group_impl->textures[index];
            ASSERT(binding.texture != nullptr);
            ASSERT(binding.texture->context == context_impl);
            ASSERT(binding.texture->view != nullptr);
            ID3D11ShaderResourceView* texture = binding.texture->view;
            switch (binding.stage) {
            case ShaderStage::VERTEX:
                device_context->VSSetShaderResources(binding.slot, 1u, &texture);
                break;
            case ShaderStage::PIXEL:
                device_context->PSSetShaderResources(binding.slot, 1u, &texture);
                break;
            }
        }

        for (size_t index = 0u; index < group_impl->sampler_count; ++index) {
            D3D11SamplerBinding const& binding = group_impl->samplers[index];
            ASSERT(binding.sampler != nullptr);
            ASSERT(binding.sampler->context == context_impl);
            ASSERT(binding.sampler->state != nullptr);
            ID3D11SamplerState* sampler = binding.sampler->state;
            switch (binding.stage) {
            case ShaderStage::VERTEX:
                device_context->VSSetSamplers(binding.slot, 1u, &sampler);
                break;
            case ShaderStage::PIXEL:
                device_context->PSSetSamplers(binding.slot, 1u, &sampler);
                break;
            }
        }
    }

    auto draw(Context context, DrawDesc const& desc) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->render_pass_active);

        ::gui::render::d3d11::bind_pipeline(context, desc.pipeline);
        for (size_t index = 0u; index < desc.bind_group_count; ++index) {
            ::gui::render::d3d11::bind_group(context, desc.bind_groups[index]);
        }

        ID3D11DeviceContext* const device_context = context_impl->device_context;
        for (size_t index = 0u; index < desc.vertex_buffer_count; ++index) {
            VertexBufferBinding const& binding = desc.vertex_buffers[index];
            ASSERT(binding.slot < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);

            D3D11Buffer* buffer_impl = buffer_from_handle(binding.buffer);
            ASSERT(buffer_impl != nullptr);
            ASSERT(buffer_impl->context == context_impl);
            ASSERT(buffer_impl->resource != nullptr);
            ASSERT(buffer_impl->binding == BufferBinding::VERTEX);
            ASSERT(binding.byte_offset <= buffer_impl->byte_size);
            ID3D11Buffer* const buffer = buffer_impl->resource;
            UINT const stride = binding.byte_stride;
            UINT const offset = binding.byte_offset;
            device_context->IASetVertexBuffers(binding.slot, 1u, &buffer, &stride, &offset);
        }

        device_context->Draw(desc.vertex_count, desc.first_vertex);
    }

    auto resize_window(Context context, Window window, SizeU32 size) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Window* window_impl = window_from_handle(window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(!context_impl->render_pass_active);

        context_impl->device_context->OMSetRenderTargets(0u, nullptr, nullptr);
        context_impl->device_context->Flush();
        release_render_target(window_impl);

        HRESULT const hr = window_impl->swap_chain->ResizeBuffers(
            0u, size.width, size.height, DXGI_FORMAT_UNKNOWN, 0u);
        if (FAILED(hr)) {
            return Result::RESIZE_FAILED;
        }

        Result const render_target_result = create_render_target(context_impl, window_impl);
        if (result_failed(render_target_result)) {
            return render_target_result;
        }

        window_impl->size = size;
        return Result::OK;
    }

    auto begin_frame(Context context) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(!context_impl->render_pass_active);
        commit_frame_buffer(context_impl, &context_impl->frame_vertex_buffer);
        context_impl->frame_vertex_buffer.used_size = 0u;
    }

    auto begin_render_pass(Context context, WindowRenderPassDesc const& desc) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Window* window_impl = window_from_handle(desc.window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(window_impl->render_target_view != nullptr);
        ASSERT(!context_impl->render_pass_active);

        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(window_impl->size.width);
        viewport.Height = static_cast<float>(window_impl->size.height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        context_impl->device_context->OMSetRenderTargets(
            1u, &window_impl->render_target_view, nullptr);
        context_impl->device_context->RSSetViewports(1u, &viewport);

        switch (desc.load_op) {
        case LoadOp::LOAD:
        case LoadOp::DONT_CARE:
            break;
        case LoadOp::CLEAR: {
            Color const color = desc.clear_color;
            float const clear_color[] = {color.r, color.g, color.b, color.a};
            context_impl->device_context->ClearRenderTargetView(window_impl->render_target_view,
                                                                clear_color);
            break;
        }
        }

        context_impl->render_pass_active = true;
        return Result::OK;
    }

    auto end_render_pass(Context context) -> void {
        D3D11Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->render_pass_active);
        context_impl->device_context->OMSetRenderTargets(0u, nullptr, nullptr);
        context_impl->render_pass_active = false;
    }

    auto present_window(Context context, Window window) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Window* window_impl = window_from_handle(window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(window_impl->swap_chain != nullptr);
        ASSERT(!context_impl->render_pass_active);

        UINT const sync_interval = window_impl->present_mode == PresentMode::VSYNC ? 1u : 0u;
        HRESULT const hr = window_impl->swap_chain->Present(sync_interval, 0u);
        if (hr == DXGI_STATUS_OCCLUDED) {
            return Result::OCCLUDED;
        }
        if (FAILED(hr)) {
            return Result::PRESENT_FAILED;
        }

        return Result::OK;
    }

    auto window_size(Window window) -> SizeU32 {
        D3D11Window const* const window_impl = window_from_handle(window);
        ASSERT(window_impl != nullptr);
        return window_impl->size;
    }

    auto native_device(Context context) -> void* {
        D3D11Context const* const context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        return context_impl->device;
    }

    auto native_swap_chain(Window window) -> void* {
        D3D11Window const* const window_impl = window_from_handle(window);
        ASSERT(window_impl != nullptr);
        return window_impl->swap_chain;
    }

} // namespace gui::render::d3d11
