#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "render_d3d12.h"

#include "render_backend.h"

#include <base/config.h>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>

#if BASE_PLATFORM_WINDOWS

namespace gui::render::d3d12 {
    namespace {

        constexpr uint32_t DESCRIPTOR_SLOT_COUNT = 16u;
        constexpr uint32_t FRAME_RESOURCE_COUNT = 2u;
        constexpr uint32_t FRAME_SHADER_DESCRIPTOR_COUNT = 2048u;
        constexpr uint32_t SHADER_DESCRIPTOR_COUNT =
            FRAME_RESOURCE_COUNT * FRAME_SHADER_DESCRIPTOR_COUNT;
        constexpr uint32_t FRAME_SAMPLER_DESCRIPTOR_COUNT = 256u;
        constexpr uint32_t SAMPLER_DESCRIPTOR_COUNT =
            FRAME_RESOURCE_COUNT * FRAME_SAMPLER_DESCRIPTOR_COUNT;
        constexpr size_t FRAME_VERTEX_BUFFER_DEFAULT_SIZE = 64u * 1024u;
        constexpr size_t DEFERRED_RELEASE_CAPACITY = 4096u;

        enum RootParameter : uint32_t {
            ROOT_VS_CBV,
            ROOT_PS_CBV,
            ROOT_VS_SRV,
            ROOT_PS_SRV,
            ROOT_VS_SAMPLER,
            ROOT_PS_SAMPLER,
            ROOT_COUNT,
        };

        struct D3D12Context;

        struct D3D12Buffer {
            BufferHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            ID3D12Resource* resource = nullptr;
            BufferBinding binding = BufferBinding::VERTEX;
            BufferUsage usage = BufferUsage::IMMUTABLE;
            size_t byte_size = 0u;
        };

        struct D3D12Texture {
            TextureHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            ID3D12Resource* resource = nullptr;
        };

        struct D3D12Sampler {
            SamplerHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            D3D12_SAMPLER_DESC desc = {};
        };

        struct D3D12Shader {
            ShaderHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            uint8_t* bytecode = nullptr;
            size_t byte_size = 0u;
            ShaderStage stage = ShaderStage::VERTEX;
        };

        struct D3D12Pipeline {
            PipelineHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            ID3D12PipelineState* pipeline_state = nullptr;
            PrimitiveTopology topology = PrimitiveTopology::TRIANGLE_LIST;
        };

        struct D3D12DescriptorTable {
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
            uint32_t mask = 0u;
        };

        struct D3D12BufferBinding {
            D3D12Buffer* buffer = nullptr;
            ShaderStage stage = ShaderStage::VERTEX;
            uint32_t slot = 0u;
        };

        struct D3D12TextureBinding {
            D3D12Texture* texture = nullptr;
            ShaderStage stage = ShaderStage::PIXEL;
            uint32_t slot = 0u;
        };

        struct D3D12SamplerBinding {
            D3D12Sampler* sampler = nullptr;
            ShaderStage stage = ShaderStage::PIXEL;
            uint32_t slot = 0u;
        };

        struct D3D12BindGroup {
            BindGroupHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            D3D12BufferBinding* buffers = nullptr;
            size_t buffer_count = 0u;
            D3D12TextureBinding* textures = nullptr;
            size_t texture_count = 0u;
            D3D12SamplerBinding* samplers = nullptr;
            size_t sampler_count = 0u;
        };

        struct D3D12FrameBuffer {
            D3D12Buffer buffer = {};
            uint8_t* mapped_data = nullptr;
            size_t capacity = 0u;
            size_t used_size = 0u;
        };

        struct D3D12FrameResource {
            ID3D12CommandAllocator* command_allocator = nullptr;
            D3D12FrameBuffer frame_vertex_buffer = {};
            uint32_t frame_shader_descriptor_count = 0u;
            uint32_t frame_sampler_descriptor_count = 0u;
            uint64_t fence_value = 0u;
        };

        struct D3D12DeferredRelease {
            IUnknown* object = nullptr;
            uint64_t fence_value = 0u;
        };

        struct D3D12Window {
            WindowHeader header = {Backend::D3D12};
            D3D12Context* context = nullptr;
            IDXGISwapChain3* swap_chain = nullptr;
            ID3D12DescriptorHeap* rtv_heap = nullptr;
            ID3D12Resource** back_buffers = nullptr;
            uint32_t buffer_count = 0u;
            uint32_t frame_index = 0u;
            uint32_t rtv_descriptor_size = 0u;
            SizeU32 size = {};
            PresentMode present_mode = PresentMode::VSYNC;
        };

        struct D3D12Context {
            ContextHeader header = {Backend::D3D12};
            Arena* arena = nullptr;
            IDXGIFactory4* factory = nullptr;
            IDXGIAdapter1* adapter = nullptr;
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* command_queue = nullptr;
            ID3D12CommandAllocator* upload_command_allocator = nullptr;
            ID3D12GraphicsCommandList* command_list = nullptr;
            ID3D12Fence* fence = nullptr;
            HANDLE fence_event = nullptr;
            ID3D12RootSignature* root_signature = nullptr;
            ID3D12DescriptorHeap* shader_heap = nullptr;
            ID3D12DescriptorHeap* sampler_heap = nullptr;
            uint32_t shader_descriptor_size = 0u;
            uint32_t sampler_descriptor_size = 0u;
            uint64_t next_fence_value = 1u;
            uint64_t submitted_fence_value = 0u;
            uint32_t next_frame_resource_index = 0u;
            uint32_t active_frame_resource_index = 0u;
            D3D12Window* active_window = nullptr;
            D3D12FrameResource frame_resources[FRAME_RESOURCE_COUNT] = {};
            D3D12DescriptorTable bound_tables[ROOT_COUNT] = {};
            D3D12DeferredRelease* deferred_releases = nullptr;
            size_t deferred_release_count = 0u;
            bool frame_active = false;
            bool render_pass_active = false;
        };

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
            }
        }

        [[nodiscard]] auto context_from_handle(Context context) -> D3D12Context* {
            ASSERT(context_backend(context) == Backend::D3D12);
            return static_cast<D3D12Context*>(context.handle);
        }

        [[nodiscard]] auto window_from_handle(Window window) -> D3D12Window* {
            ASSERT(window_backend(window) == Backend::D3D12);
            return static_cast<D3D12Window*>(window.handle);
        }

        [[nodiscard]] auto buffer_from_handle(Buffer buffer) -> D3D12Buffer* {
            ASSERT(buffer_backend(buffer) == Backend::D3D12);
            return static_cast<D3D12Buffer*>(buffer.handle);
        }

        [[nodiscard]] auto texture_from_handle(Texture texture) -> D3D12Texture* {
            ASSERT(texture_backend(texture) == Backend::D3D12);
            return static_cast<D3D12Texture*>(texture.handle);
        }

        [[nodiscard]] auto sampler_from_handle(Sampler sampler) -> D3D12Sampler* {
            ASSERT(sampler_backend(sampler) == Backend::D3D12);
            return static_cast<D3D12Sampler*>(sampler.handle);
        }

        [[nodiscard]] auto shader_from_handle(Shader shader) -> D3D12Shader* {
            ASSERT(shader_backend(shader) == Backend::D3D12);
            return static_cast<D3D12Shader*>(shader.handle);
        }

        [[nodiscard]] auto pipeline_from_handle(Pipeline pipeline) -> D3D12Pipeline* {
            ASSERT(pipeline_backend(pipeline) == Backend::D3D12);
            return static_cast<D3D12Pipeline*>(pipeline.handle);
        }

        [[nodiscard]] auto bind_group_from_handle(BindGroup bind_group) -> D3D12BindGroup* {
            ASSERT(bind_group_backend(bind_group) == Backend::D3D12);
            return static_cast<D3D12BindGroup*>(bind_group.handle);
        }

        [[nodiscard]] auto align_up(size_t value, size_t alignment) -> size_t {
            return ((value + alignment - 1u) / alignment) * alignment;
        }

        [[nodiscard]] auto buffer_resource_size(BufferBinding binding, size_t byte_size) -> size_t {
            if (binding == BufferBinding::UNIFORM) {
                return align_up(byte_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
            }
            return byte_size;
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

        [[nodiscard]] auto d3d_topology(PrimitiveTopology topology) -> D3D12_PRIMITIVE_TOPOLOGY {
            switch (topology) {
            case PrimitiveTopology::TRIANGLE_LIST:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }

            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }

        [[nodiscard]] auto shader_target(ShaderStage stage) -> char const* {
            switch (stage) {
            case ShaderStage::VERTEX:
                return "vs_5_0";
            case ShaderStage::PIXEL:
                return "ps_5_0";
            }

            return nullptr;
        }

        [[nodiscard]] auto rtv_handle(D3D12Window const* window, uint32_t index)
            -> D3D12_CPU_DESCRIPTOR_HANDLE {
            D3D12_CPU_DESCRIPTOR_HANDLE handle =
                window->rtv_heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * window->rtv_descriptor_size;
            return handle;
        }

        [[nodiscard]] auto shader_cpu_handle(D3D12Context const* context, uint32_t index)
            -> D3D12_CPU_DESCRIPTOR_HANDLE {
            D3D12_CPU_DESCRIPTOR_HANDLE handle =
                context->shader_heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * context->shader_descriptor_size;
            return handle;
        }

        [[nodiscard]] auto shader_gpu_handle(D3D12Context const* context, uint32_t index)
            -> D3D12_GPU_DESCRIPTOR_HANDLE {
            D3D12_GPU_DESCRIPTOR_HANDLE handle =
                context->shader_heap->GetGPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<UINT64>(index) * context->shader_descriptor_size;
            return handle;
        }

        [[nodiscard]] auto sampler_cpu_handle(D3D12Context const* context, uint32_t index)
            -> D3D12_CPU_DESCRIPTOR_HANDLE {
            D3D12_CPU_DESCRIPTOR_HANDLE handle =
                context->sampler_heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index) * context->sampler_descriptor_size;
            return handle;
        }

        [[nodiscard]] auto sampler_gpu_handle(D3D12Context const* context, uint32_t index)
            -> D3D12_GPU_DESCRIPTOR_HANDLE {
            D3D12_GPU_DESCRIPTOR_HANDLE handle =
                context->sampler_heap->GetGPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<UINT64>(index) * context->sampler_descriptor_size;
            return handle;
        }

        [[nodiscard]] auto allocate_frame_shader_descriptors(D3D12Context* context, uint32_t count)
            -> uint32_t {
            ASSERT(context->frame_active);
            D3D12FrameResource& frame =
                context->frame_resources[context->active_frame_resource_index];
            ASSERT(frame.frame_shader_descriptor_count + count <= FRAME_SHADER_DESCRIPTOR_COUNT);
            uint32_t const index =
                (context->active_frame_resource_index * FRAME_SHADER_DESCRIPTOR_COUNT) +
                frame.frame_shader_descriptor_count;
            frame.frame_shader_descriptor_count += count;
            return index;
        }

        [[nodiscard]] auto allocate_frame_sampler_descriptors(D3D12Context* context, uint32_t count)
            -> uint32_t {
            ASSERT(context->frame_active);
            D3D12FrameResource& frame =
                context->frame_resources[context->active_frame_resource_index];
            ASSERT(frame.frame_sampler_descriptor_count + count <= FRAME_SAMPLER_DESCRIPTOR_COUNT);
            uint32_t const index =
                (context->active_frame_resource_index * FRAME_SAMPLER_DESCRIPTOR_COUNT) +
                frame.frame_sampler_descriptor_count;
            frame.frame_sampler_descriptor_count += count;
            return index;
        }

        [[nodiscard]] auto root_uses_sampler(uint32_t root_parameter) -> bool {
            return root_parameter == ROOT_VS_SAMPLER || root_parameter == ROOT_PS_SAMPLER;
        }

        [[nodiscard]] auto descriptor_mask(uint32_t slot) -> uint32_t {
            return 1u << slot;
        }

        [[nodiscard]] auto descriptor_cpu_handle(D3D12Context const* context,
                                                 uint32_t root_parameter,
                                                 uint32_t index) -> D3D12_CPU_DESCRIPTOR_HANDLE {
            return root_uses_sampler(root_parameter) ? sampler_cpu_handle(context, index)
                                                     : shader_cpu_handle(context, index);
        }

        [[nodiscard]] auto descriptor_heap_type(uint32_t root_parameter)
            -> D3D12_DESCRIPTOR_HEAP_TYPE {
            return root_uses_sampler(root_parameter) ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                                                     : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        }

        [[nodiscard]] auto descriptor_index(D3D12Context const* context,
                                            uint32_t root_parameter,
                                            D3D12_GPU_DESCRIPTOR_HANDLE handle) -> uint32_t {
            D3D12_GPU_DESCRIPTOR_HANDLE const base =
                root_uses_sampler(root_parameter)
                    ? context->sampler_heap->GetGPUDescriptorHandleForHeapStart()
                    : context->shader_heap->GetGPUDescriptorHandleForHeapStart();
            uint32_t const size = root_uses_sampler(root_parameter)
                                      ? context->sampler_descriptor_size
                                      : context->shader_descriptor_size;
            return static_cast<uint32_t>((handle.ptr - base.ptr) / size);
        }

        [[nodiscard]] auto descriptor_count(uint32_t mask) -> uint32_t {
            ASSERT(mask != 0u);
            uint32_t count = 0u;
            while (mask != 0u) {
                ++count;
                mask >>= 1u;
            }
            return count;
        }

        [[nodiscard]] auto allocate_frame_descriptor_table(D3D12Context* context,
                                                           uint32_t root_parameter,
                                                           uint32_t mask)
            -> D3D12_GPU_DESCRIPTOR_HANDLE {
            if (root_uses_sampler(root_parameter)) {
                uint32_t const index =
                    allocate_frame_sampler_descriptors(context, descriptor_count(mask));
                return sampler_gpu_handle(context, index);
            }

            uint32_t const index =
                allocate_frame_shader_descriptors(context, DESCRIPTOR_SLOT_COUNT);
            return shader_gpu_handle(context, index);
        }

        auto copy_descriptor(D3D12Context* context,
                             uint32_t root_parameter,
                             D3D12_GPU_DESCRIPTOR_HANDLE target_table,
                             D3D12_GPU_DESCRIPTOR_HANDLE source_table,
                             uint32_t slot) -> void {
            uint32_t const target_index =
                descriptor_index(context, root_parameter, target_table) + slot;
            uint32_t const source_index =
                descriptor_index(context, root_parameter, source_table) + slot;
            context->device->CopyDescriptorsSimple(
                1u,
                descriptor_cpu_handle(context, root_parameter, target_index),
                descriptor_cpu_handle(context, root_parameter, source_index),
                descriptor_heap_type(root_parameter));
        }

        auto copy_table(D3D12Context* context,
                        uint32_t root_parameter,
                        D3D12_GPU_DESCRIPTOR_HANDLE target,
                        D3D12DescriptorTable source) -> void {
            for (uint32_t slot = 0u; slot < DESCRIPTOR_SLOT_COUNT; ++slot) {
                if ((source.mask & descriptor_mask(slot)) != 0u) {
                    copy_descriptor(context, root_parameter, target, source.gpu, slot);
                }
            }
        }

        [[nodiscard]] auto writable_descriptor_table(D3D12Context* context,
                                                     D3D12DescriptorTable* tables,
                                                     uint32_t root_parameter,
                                                     uint32_t update_mask)
            -> D3D12DescriptorTable& {
            D3D12DescriptorTable& table = tables[root_parameter];
            if (table.gpu.ptr == 0u) {
                table.mask = context->bound_tables[root_parameter].mask | update_mask;
                table.gpu = allocate_frame_descriptor_table(context, root_parameter, table.mask);
                copy_table(
                    context, root_parameter, table.gpu, context->bound_tables[root_parameter]);
            } else {
                ASSERT((update_mask & ~table.mask) == 0u);
            }
            return table;
        }

        auto commit_descriptor_table(D3D12Context* context,
                                     uint32_t root_parameter,
                                     D3D12DescriptorTable table) -> void {
            context->bound_tables[root_parameter] = table;
            context->command_list->SetGraphicsRootDescriptorTable(root_parameter, table.gpu);
        }

        auto bind_current_descriptor_tables(D3D12Context* context) -> void {
            for (uint32_t root_parameter = 0u; root_parameter < ROOT_COUNT; ++root_parameter) {
                D3D12DescriptorTable const table = context->bound_tables[root_parameter];
                if (table.mask != 0u) {
                    context->command_list->SetGraphicsRootDescriptorTable(root_parameter,
                                                                          table.gpu);
                }
            }
        }

        auto reset_bound_descriptor_tables(D3D12Context* context) -> void {
            for (uint32_t root_parameter = 0u; root_parameter < ROOT_COUNT; ++root_parameter) {
                context->bound_tables[root_parameter] = {};
            }
        }

        auto release_completed_deferred(D3D12Context* context) -> void {
            if (context->fence == nullptr) {
                return;
            }

            uint64_t const completed = context->fence->GetCompletedValue();
            size_t write_index = 0u;
            for (size_t index = 0u; index < context->deferred_release_count; ++index) {
                D3D12DeferredRelease release = context->deferred_releases[index];
                if (release.object != nullptr && release.fence_value != 0u &&
                    completed >= release.fence_value) {
                    release.object->Release();
                    continue;
                }

                context->deferred_releases[write_index] = release;
                ++write_index;
            }

            context->deferred_release_count = write_index;
        }

        auto release_all_deferred(D3D12Context* context) -> void {
            for (size_t index = 0u; index < context->deferred_release_count; ++index) {
                release_com(context->deferred_releases[index].object);
                context->deferred_releases[index].fence_value = 0u;
            }
            context->deferred_release_count = 0u;
        }

        auto defer_release(D3D12Context* context, IUnknown* object) -> void {
            if (object == nullptr) {
                return;
            }

            uint64_t const fence_value =
                context->frame_active ? 0u : context->submitted_fence_value;
            if (fence_value != 0u && context->fence->GetCompletedValue() >= fence_value) {
                object->Release();
                return;
            }

            ASSERT(context->deferred_release_count < DEFERRED_RELEASE_CAPACITY);
            D3D12DeferredRelease& release =
                context->deferred_releases[context->deferred_release_count];
            release.object = object;
            release.fence_value = fence_value;
            ++context->deferred_release_count;
        }

        template <typename T> auto defer_release_com(D3D12Context* context, T*& object) -> void {
            defer_release(context, object);
            object = nullptr;
        }

        auto mark_frame_deferred_releases(D3D12Context* context, uint64_t fence_value) -> void {
            for (size_t index = 0u; index < context->deferred_release_count; ++index) {
                if (context->deferred_releases[index].fence_value == 0u) {
                    context->deferred_releases[index].fence_value = fence_value;
                }
            }
        }

        auto wait_for_fence(D3D12Context* context, uint64_t fence_value) -> void {
            if (fence_value != 0u && context->fence->GetCompletedValue() < fence_value) {
                HRESULT const hr =
                    context->fence->SetEventOnCompletion(fence_value, context->fence_event);
                ASSERT(SUCCEEDED(hr));
                BASE_UNUSED(hr);
                WaitForSingleObject(context->fence_event, INFINITE);
            }

            release_completed_deferred(context);
        }

        auto wait_for_gpu(D3D12Context* context) -> void {
            wait_for_fence(context, context->submitted_fence_value);
        }

        [[nodiscard]] auto can_wait_for_gpu(D3D12Context const* context) -> bool {
            return context->fence != nullptr && context->fence_event != nullptr;
        }

        auto wait_for_frame_resource(D3D12Context* context, D3D12FrameResource* frame) -> void {
            wait_for_fence(context, frame->fence_value);
            frame->fence_value = 0u;
        }

        auto signal_gpu(D3D12Context* context) -> uint64_t {
            uint64_t const value = context->next_fence_value;
            ++context->next_fence_value;
            HRESULT const hr = context->command_queue->Signal(context->fence, value);
            ASSERT(SUCCEEDED(hr));
            BASE_UNUSED(hr);
            context->submitted_fence_value = value;
            mark_frame_deferred_releases(context, value);
            return value;
        }

        [[nodiscard]] auto create_buffer_resource(D3D12Context* context,
                                                  size_t byte_size,
                                                  D3D12_HEAP_TYPE heap_type,
                                                  D3D12_RESOURCE_STATES initial_state,
                                                  ID3D12Resource*& out_resource) -> Result {
            D3D12_HEAP_PROPERTIES heap_properties = {};
            heap_properties.Type = heap_type;

            D3D12_RESOURCE_DESC resource_desc = {};
            resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resource_desc.Width = byte_size;
            resource_desc.Height = 1u;
            resource_desc.DepthOrArraySize = 1u;
            resource_desc.MipLevels = 1u;
            resource_desc.SampleDesc.Count = 1u;
            resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT const hr =
                context->device->CreateCommittedResource(&heap_properties,
                                                         D3D12_HEAP_FLAG_NONE,
                                                         &resource_desc,
                                                         initial_state,
                                                         nullptr,
                                                         IID_PPV_ARGS(&out_resource));
            if (FAILED(hr) || out_resource == nullptr) {
                return Result::BUFFER_CREATION_FAILED;
            }

            return Result::OK;
        }

        [[nodiscard]] auto create_upload_buffer(D3D12Context* context,
                                                size_t byte_size,
                                                ID3D12Resource*& out_resource,
                                                uint8_t*& out_mapped_data) -> Result {
            Result const result = create_buffer_resource(context,
                                                         byte_size,
                                                         D3D12_HEAP_TYPE_UPLOAD,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ,
                                                         out_resource);
            if (result_failed(result)) {
                return result;
            }

            HRESULT const hr =
                out_resource->Map(0u, nullptr, reinterpret_cast<void**>(&out_mapped_data));
            if (FAILED(hr) || out_mapped_data == nullptr) {
                release_com(out_resource);
                return Result::BUFFER_CREATION_FAILED;
            }

            return Result::OK;
        }

        auto record_buffer_upload(D3D12Context* context,
                                  ID3D12Resource* buffer,
                                  ID3D12Resource* upload,
                                  size_t byte_size) -> void {
            context->command_list->CopyBufferRegion(buffer, 0u, upload, 0u, byte_size);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            context->command_list->ResourceBarrier(1u, &barrier);
        }

        [[nodiscard]] auto upload_buffer(D3D12Context* context,
                                         ID3D12Resource* buffer,
                                         ID3D12Resource* upload,
                                         size_t byte_size) -> Result {
            if (context->frame_active) {
                record_buffer_upload(context, buffer, upload, byte_size);
                return Result::OK;
            }

            wait_for_gpu(context);
            HRESULT hr = context->upload_command_allocator->Reset();
            if (FAILED(hr)) {
                return Result::BUFFER_CREATION_FAILED;
            }

            hr = context->command_list->Reset(context->upload_command_allocator, nullptr);
            if (FAILED(hr)) {
                return Result::BUFFER_CREATION_FAILED;
            }

            record_buffer_upload(context, buffer, upload, byte_size);

            hr = context->command_list->Close();
            if (FAILED(hr)) {
                return Result::BUFFER_CREATION_FAILED;
            }

            ID3D12CommandList* command_lists[] = {context->command_list};
            context->command_queue->ExecuteCommandLists(1u, command_lists);
            signal_gpu(context);
            wait_for_gpu(context);
            return Result::OK;
        }

        auto release_frame_buffer(D3D12Context* context, D3D12FrameBuffer* frame_buffer) -> void {
            if (frame_buffer->buffer.resource != nullptr && frame_buffer->mapped_data != nullptr) {
                frame_buffer->buffer.resource->Unmap(0u, nullptr);
                frame_buffer->mapped_data = nullptr;
            }

            defer_release_com(context, frame_buffer->buffer.resource);
            frame_buffer->buffer.context = nullptr;
            frame_buffer->buffer.byte_size = 0u;
            frame_buffer->capacity = 0u;
            frame_buffer->used_size = 0u;
        }

        auto ensure_frame_buffer(D3D12Context* context,
                                 D3D12FrameBuffer* frame_buffer,
                                 size_t byte_size) -> void {
            if (byte_size <= frame_buffer->capacity) {
                return;
            }

            release_frame_buffer(context, frame_buffer);

            ID3D12Resource* resource = nullptr;
            uint8_t* mapped_data = nullptr;
            Result const result = create_upload_buffer(context, byte_size, resource, mapped_data);
            ASSERT(result_succeeded(result));

            frame_buffer->buffer.context = context;
            frame_buffer->buffer.resource = resource;
            frame_buffer->buffer.binding = BufferBinding::VERTEX;
            frame_buffer->buffer.usage = BufferUsage::DYNAMIC;
            frame_buffer->buffer.byte_size = byte_size;
            frame_buffer->mapped_data = mapped_data;
            frame_buffer->capacity = byte_size;
        }

        [[nodiscard]] auto create_render_targets(D3D12Context* context, D3D12Window* window)
            -> Result {
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap_desc.NumDescriptors = window->buffer_count;

            HRESULT hr =
                context->device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&window->rtv_heap));
            if (FAILED(hr) || window->rtv_heap == nullptr) {
                return Result::RENDER_TARGET_CREATION_FAILED;
            }

            window->rtv_descriptor_size =
                context->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            for (uint32_t index = 0u; index < window->buffer_count; ++index) {
                hr = window->swap_chain->GetBuffer(index,
                                                   IID_PPV_ARGS(&window->back_buffers[index]));
                if (FAILED(hr) || window->back_buffers[index] == nullptr) {
                    return Result::RENDER_TARGET_CREATION_FAILED;
                }

                context->device->CreateRenderTargetView(
                    window->back_buffers[index], nullptr, rtv_handle(window, index));
            }

            window->frame_index = window->swap_chain->GetCurrentBackBufferIndex();
            return Result::OK;
        }

        auto release_render_targets(D3D12Window* window) -> void {
            if (window->back_buffers != nullptr) {
                for (uint32_t index = 0u; index < window->buffer_count; ++index) {
                    release_com(window->back_buffers[index]);
                }
            }

            release_com(window->rtv_heap);
        }

        auto destroy_pipeline_impl(D3D12Context* context, D3D12Pipeline* pipeline) -> void {
            defer_release_com(context, pipeline->pipeline_state);
            pipeline->context = nullptr;
            pipeline->topology = PrimitiveTopology::TRIANGLE_LIST;
        }

        auto destroy_context_impl(D3D12Context* context) -> void {
            bool const can_wait = can_wait_for_gpu(context);
            if (can_wait) {
                wait_for_gpu(context);
            }

            for (uint32_t index = 0u; index < FRAME_RESOURCE_COUNT; ++index) {
                release_frame_buffer(context, &context->frame_resources[index].frame_vertex_buffer);
            }
            if (can_wait) {
                wait_for_gpu(context);
            }
            release_all_deferred(context);

            if (context->fence_event != nullptr) {
                CloseHandle(context->fence_event);
                context->fence_event = nullptr;
            }

            release_com(context->sampler_heap);
            release_com(context->shader_heap);
            release_com(context->root_signature);
            release_com(context->fence);
            release_com(context->command_list);
            for (uint32_t index = 0u; index < FRAME_RESOURCE_COUNT; ++index) {
                release_com(context->frame_resources[index].command_allocator);
            }
            release_com(context->upload_command_allocator);
            release_com(context->command_queue);
            release_com(context->device);
            release_com(context->adapter);
            release_com(context->factory);
        }

        auto destroy_window_impl(D3D12Window* window) -> void {
            if (window->context != nullptr) {
                wait_for_gpu(window->context);
            }

            release_render_targets(window);
            release_com(window->swap_chain);
            window->context = nullptr;
            window->buffer_count = 0u;
            window->frame_index = 0u;
            window->rtv_descriptor_size = 0u;
            window->size = {};
            window->present_mode = PresentMode::VSYNC;
        }

        [[nodiscard]] auto create_factory(D3D12Context* context, bool enable_debug_layer)
            -> Result {
            UINT factory_flags = 0u;
            if (enable_debug_layer) {
                ID3D12Debug* debug = nullptr;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                    debug->EnableDebugLayer();
                    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
                }
                release_com(debug);
            }

            HRESULT const hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&context->factory));
            if (FAILED(hr) || context->factory == nullptr) {
                return Result::FACTORY_CREATION_FAILED;
            }

            return Result::OK;
        }

        [[nodiscard]] auto create_device(D3D12Context* context) -> Result {
            IDXGIAdapter1* adapter = nullptr;
            for (UINT index = 0u;
                 context->factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
                 ++index) {
                DXGI_ADAPTER_DESC1 adapter_desc = {};
                adapter->GetDesc1(&adapter_desc);
                if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u &&
                    SUCCEEDED(D3D12CreateDevice(
                        adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                    context->adapter = adapter;
                    adapter = nullptr;
                    break;
                }
                release_com(adapter);
            }

            if (context->adapter == nullptr &&
                FAILED(context->factory->EnumWarpAdapter(IID_PPV_ARGS(&context->adapter)))) {
                return Result::DEVICE_CREATION_FAILED;
            }

            HRESULT const hr = D3D12CreateDevice(
                context->adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&context->device));
            if (FAILED(hr) || context->device == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            return Result::OK;
        }

        [[nodiscard]] auto create_command_objects(D3D12Context* context) -> Result {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            HRESULT hr = context->device->CreateCommandQueue(&queue_desc,
                                                             IID_PPV_ARGS(&context->command_queue));
            if (FAILED(hr) || context->command_queue == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            hr = context->device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context->upload_command_allocator));
            if (FAILED(hr) || context->upload_command_allocator == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            for (uint32_t index = 0u; index < FRAME_RESOURCE_COUNT; ++index) {
                hr = context->device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&context->frame_resources[index].command_allocator));
                if (FAILED(hr) || context->frame_resources[index].command_allocator == nullptr) {
                    return Result::DEVICE_CREATION_FAILED;
                }
            }

            hr = context->device->CreateCommandList(0u,
                                                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    context->upload_command_allocator,
                                                    nullptr,
                                                    IID_PPV_ARGS(&context->command_list));
            if (FAILED(hr) || context->command_list == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            hr = context->command_list->Close();
            if (FAILED(hr)) {
                return Result::DEVICE_CREATION_FAILED;
            }

            hr = context->device->CreateFence(
                0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context->fence));
            if (FAILED(hr) || context->fence == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            context->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (context->fence_event == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            return Result::OK;
        }

        [[nodiscard]] auto create_descriptor_heaps(D3D12Context* context) -> Result {
            D3D12_DESCRIPTOR_HEAP_DESC shader_heap_desc = {};
            shader_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            shader_heap_desc.NumDescriptors = SHADER_DESCRIPTOR_COUNT;
            shader_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            HRESULT hr = context->device->CreateDescriptorHeap(&shader_heap_desc,
                                                               IID_PPV_ARGS(&context->shader_heap));
            if (FAILED(hr) || context->shader_heap == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
            sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            sampler_heap_desc.NumDescriptors = SAMPLER_DESCRIPTOR_COUNT;
            sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            hr = context->device->CreateDescriptorHeap(&sampler_heap_desc,
                                                       IID_PPV_ARGS(&context->sampler_heap));
            if (FAILED(hr) || context->sampler_heap == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            context->shader_descriptor_size = context->device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context->sampler_descriptor_size = context->device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            return Result::OK;
        }

        [[nodiscard]] auto create_root_signature(D3D12Context* context) -> Result {
            D3D12_DESCRIPTOR_RANGE ranges[ROOT_COUNT] = {};
            ranges[ROOT_VS_CBV] = {
                D3D12_DESCRIPTOR_RANGE_TYPE_CBV, DESCRIPTOR_SLOT_COUNT, 0u, 0u, 0u};
            ranges[ROOT_PS_CBV] = ranges[ROOT_VS_CBV];
            ranges[ROOT_VS_SRV] = {
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV, DESCRIPTOR_SLOT_COUNT, 0u, 0u, 0u};
            ranges[ROOT_PS_SRV] = ranges[ROOT_VS_SRV];
            ranges[ROOT_VS_SAMPLER] = {
                D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, DESCRIPTOR_SLOT_COUNT, 0u, 0u, 0u};
            ranges[ROOT_PS_SAMPLER] = ranges[ROOT_VS_SAMPLER];

            D3D12_ROOT_PARAMETER parameters[ROOT_COUNT] = {};
            for (uint32_t index = 0u; index < ROOT_COUNT; ++index) {
                parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[index].DescriptorTable.NumDescriptorRanges = 1u;
                parameters[index].DescriptorTable.pDescriptorRanges = &ranges[index];
            }

            parameters[ROOT_VS_CBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[ROOT_PS_CBV].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[ROOT_VS_SRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[ROOT_PS_SRV].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[ROOT_VS_SAMPLER].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[ROOT_PS_SAMPLER].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC desc = {};
            desc.NumParameters = ROOT_COUNT;
            desc.pParameters = parameters;
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ID3DBlob* signature = nullptr;
            ID3DBlob* error = nullptr;
            HRESULT hr = D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
            release_com(error);
            if (FAILED(hr) || signature == nullptr) {
                release_com(signature);
                return Result::DEVICE_CREATION_FAILED;
            }

            hr = context->device->CreateRootSignature(0u,
                                                      signature->GetBufferPointer(),
                                                      signature->GetBufferSize(),
                                                      IID_PPV_ARGS(&context->root_signature));
            release_com(signature);
            if (FAILED(hr) || context->root_signature == nullptr) {
                return Result::DEVICE_CREATION_FAILED;
            }

            return Result::OK;
        }

        auto record_texture_upload(D3D12Context* context,
                                   ID3D12Resource* texture,
                                   ID3D12Resource* upload_resource,
                                   D3D12_PLACED_SUBRESOURCE_FOOTPRINT const& layout) -> void {
            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = upload_resource;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = layout;

            D3D12_TEXTURE_COPY_LOCATION target = {};
            target.pResource = texture;
            target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            target.SubresourceIndex = 0u;

            context->command_list->CopyTextureRegion(&target, 0u, 0u, 0u, &source, nullptr);

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter =
                static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            context->command_list->ResourceBarrier(1u, &barrier);
        }

        [[nodiscard]] auto upload_texture(D3D12Context* context,
                                          ID3D12Resource* texture,
                                          ID3D12Resource* upload_resource,
                                          D3D12_PLACED_SUBRESOURCE_FOOTPRINT const& layout)
            -> Result {
            if (context->frame_active) {
                record_texture_upload(context, texture, upload_resource, layout);
                return Result::OK;
            }

            wait_for_gpu(context);
            HRESULT hr = context->upload_command_allocator->Reset();
            if (FAILED(hr)) {
                return Result::TEXTURE_CREATION_FAILED;
            }

            hr = context->command_list->Reset(context->upload_command_allocator, nullptr);
            if (FAILED(hr)) {
                return Result::TEXTURE_CREATION_FAILED;
            }

            record_texture_upload(context, texture, upload_resource, layout);

            hr = context->command_list->Close();
            if (FAILED(hr)) {
                return Result::TEXTURE_CREATION_FAILED;
            }

            ID3D12CommandList* command_lists[] = {context->command_list};
            context->command_queue->ExecuteCommandLists(1u, command_lists);
            signal_gpu(context);
            wait_for_gpu(context);
            return Result::OK;
        }

        [[nodiscard]] auto blend_desc(BlendMode blend_mode) -> D3D12_BLEND_DESC {
            D3D12_BLEND_DESC desc = {};
            desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            if (blend_mode == BlendMode::ALPHA) {
                desc.RenderTarget[0].BlendEnable = TRUE;
                desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }

            return desc;
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        ArenaMarker const marker = arena.marker();
        D3D12Context* context = arena_new<D3D12Context>(arena);
        context->arena = &arena;
        context->deferred_releases =
            arena_alloc<D3D12DeferredRelease>(arena, DEFERRED_RELEASE_CAPACITY);

        Result result = create_factory(context, desc.enable_debug_layer);
        if (result_failed(result)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return result;
        }

        result = create_device(context);
        if (result_failed(result)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return result;
        }

        result = create_command_objects(context);
        if (result_failed(result)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return result;
        }

        result = create_descriptor_heaps(context);
        if (result_failed(result)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return result;
        }

        result = create_root_signature(context);
        if (result_failed(result)) {
            destroy_context_impl(context);
            arena.reset_to(marker);
            return result;
        }

        out_context.handle = context;
        return Result::OK;
    }

    auto destroy_context(Context& context) -> void {
        D3D12Context* impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        ASSERT(!impl->frame_active);
        ASSERT(!impl->render_pass_active);
        destroy_context_impl(impl);
        context.handle = nullptr;
    }

    auto create_window(Arena& arena, Context context, WindowDesc const& desc, Window& out_window)
        -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        ArenaMarker const marker = arena.marker();
        D3D12Window* window = arena_new<D3D12Window>(arena);
        window->context = context_impl;
        window->back_buffers = arena_alloc<ID3D12Resource*>(arena, desc.buffer_count);
        for (uint32_t index = 0u; index < desc.buffer_count; ++index) {
            window->back_buffers[index] = nullptr;
        }

        DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
        swap_desc.Width = desc.size.width;
        swap_desc.Height = desc.size.height;
        swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.SampleDesc.Count = 1u;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = desc.buffer_count;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_desc.Scaling = DXGI_SCALING_STRETCH;

        IDXGISwapChain1* swap_chain = nullptr;
        HRESULT hr =
            context_impl->factory->CreateSwapChainForHwnd(context_impl->command_queue,
                                                          static_cast<HWND>(desc.native_window),
                                                          &swap_desc,
                                                          nullptr,
                                                          nullptr,
                                                          &swap_chain);
        if (FAILED(hr) || swap_chain == nullptr) {
            destroy_window_impl(window);
            arena.reset_to(marker);
            return Result::WINDOW_CREATION_FAILED;
        }

        hr = swap_chain->QueryInterface(IID_PPV_ARGS(&window->swap_chain));
        release_com(swap_chain);
        if (FAILED(hr) || window->swap_chain == nullptr) {
            destroy_window_impl(window);
            arena.reset_to(marker);
            return Result::WINDOW_CREATION_FAILED;
        }

        context_impl->factory->MakeWindowAssociation(static_cast<HWND>(desc.native_window),
                                                     DXGI_MWA_NO_ALT_ENTER);

        window->buffer_count = desc.buffer_count;
        window->size = desc.size;
        window->present_mode = desc.present_mode;

        Result const render_target_result = create_render_targets(context_impl, window);
        if (result_failed(render_target_result)) {
            destroy_window_impl(window);
            arena.reset_to(marker);
            return render_target_result;
        }

        out_window.handle = window;
        return Result::OK;
    }

    auto destroy_window(Window& window) -> void {
        D3D12Window* impl = window_from_handle(window);
        ASSERT(impl != nullptr);
        destroy_window_impl(impl);
        window.handle = nullptr;
    }

    auto create_buffer(Context context, BufferDesc const& desc, Buffer& out_buffer) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        size_t const resource_size = buffer_resource_size(desc.binding, desc.byte_size);
        ID3D12Resource* resource = nullptr;
        if (desc.usage == BufferUsage::DYNAMIC) {
            uint8_t* mapped_data = nullptr;
            Result const result =
                create_upload_buffer(context_impl, resource_size, resource, mapped_data);
            if (result_failed(result)) {
                return result;
            }
            if (desc.initial_data != nullptr) {
                std::memcpy(mapped_data, desc.initial_data, desc.byte_size);
            }
            resource->Unmap(0u, nullptr);
        } else {
            Result result = create_buffer_resource(context_impl,
                                                   resource_size,
                                                   D3D12_HEAP_TYPE_DEFAULT,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                   resource);
            if (result_failed(result)) {
                return result;
            }

            ID3D12Resource* upload = nullptr;
            uint8_t* mapped_data = nullptr;
            result = create_upload_buffer(context_impl, resource_size, upload, mapped_data);
            if (result_failed(result)) {
                release_com(resource);
                return result;
            }

            std::memcpy(mapped_data, desc.initial_data, desc.byte_size);
            upload->Unmap(0u, nullptr);

            result = upload_buffer(context_impl, resource, upload, desc.byte_size);
            if (result_failed(result)) {
                defer_release(context_impl, upload);
                defer_release(context_impl, resource);
                return result;
            }
            defer_release(context_impl, upload);
        }

        D3D12Buffer* buffer = arena_new<D3D12Buffer>(*context_impl->arena);
        buffer->context = context_impl;
        buffer->resource = resource;
        buffer->binding = desc.binding;
        buffer->usage = desc.usage;
        buffer->byte_size = desc.byte_size;
        out_buffer.handle = buffer;
        return Result::OK;
    }

    auto destroy_buffer(Context context, Buffer& buffer) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Buffer* buffer_impl = buffer_from_handle(buffer);
        ASSERT(context_impl != nullptr);
        ASSERT(buffer_impl != nullptr);
        ASSERT(buffer_impl->context == context_impl);
        defer_release_com(context_impl, buffer_impl->resource);
        buffer_impl->context = nullptr;
        buffer_impl->byte_size = 0u;
        buffer.handle = nullptr;
    }

    auto update_buffer(Context context, Buffer buffer, void const* data, size_t byte_size) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Buffer* buffer_impl = buffer_from_handle(buffer);
        ASSERT(context_impl != nullptr);
        ASSERT(buffer_impl != nullptr);
        ASSERT(buffer_impl->context == context_impl);
        ASSERT(buffer_impl->resource != nullptr);
        ASSERT(buffer_impl->usage == BufferUsage::DYNAMIC);
        ASSERT(byte_size <= buffer_impl->byte_size);

        uint8_t* mapped_data = nullptr;
        HRESULT const hr =
            buffer_impl->resource->Map(0u, nullptr, reinterpret_cast<void**>(&mapped_data));
        ASSERT(SUCCEEDED(hr));
        ASSERT(mapped_data != nullptr);
        std::memcpy(mapped_data, data, byte_size);
        buffer_impl->resource->Unmap(0u, nullptr);
        BASE_UNUSED(hr);
    }

    auto allocate_frame_vertex_buffer(Context context, size_t byte_size, size_t byte_alignment)
        -> FrameBufferSlice {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->frame_active);

        D3D12FrameResource& frame =
            context_impl->frame_resources[context_impl->active_frame_resource_index];
        D3D12FrameBuffer& frame_buffer = frame.frame_vertex_buffer;
        size_t const offset = align_up(frame_buffer.used_size, byte_alignment);
        size_t const needed_size = offset + byte_size;
        if (needed_size > frame_buffer.capacity) {
            ASSERT(frame_buffer.used_size == 0u);
            size_t new_capacity = frame_buffer.capacity == 0u ? FRAME_VERTEX_BUFFER_DEFAULT_SIZE
                                                              : frame_buffer.capacity * 2u;
            while (new_capacity < needed_size) {
                new_capacity *= 2u;
            }
            ensure_frame_buffer(context_impl, &frame_buffer, new_capacity);
        }

        frame_buffer.used_size = needed_size;
        return {{&frame_buffer.buffer}, frame_buffer.mapped_data + offset, offset, byte_size};
    }

    auto commit_frame_uploads(Context context) -> void {
        BASE_UNUSED(context);
    }

    auto create_texture(Context context, TextureDesc const& desc, Texture& out_texture) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D12_RESOURCE_DESC texture_desc = {};
        texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture_desc.Width = desc.size.width;
        texture_desc.Height = desc.size.height;
        texture_desc.DepthOrArraySize = 1u;
        texture_desc.MipLevels = 1u;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1u;

        D3D12_HEAP_PROPERTIES default_heap = {};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        ID3D12Resource* texture = nullptr;
        HRESULT hr = context_impl->device->CreateCommittedResource(&default_heap,
                                                                   D3D12_HEAP_FLAG_NONE,
                                                                   &texture_desc,
                                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                                   nullptr,
                                                                   IID_PPV_ARGS(&texture));
        if (FAILED(hr) || texture == nullptr) {
            release_com(texture);
            return Result::TEXTURE_CREATION_FAILED;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
        UINT row_count = 0u;
        UINT64 row_size = 0u;
        UINT64 upload_size = 0u;
        context_impl->device->GetCopyableFootprints(
            &texture_desc, 0u, 1u, 0u, &layout, &row_count, &row_size, &upload_size);

        ID3D12Resource* upload = nullptr;
        uint8_t* upload_data = nullptr;
        Result result = create_upload_buffer(
            context_impl, static_cast<size_t>(upload_size), upload, upload_data);
        if (result_failed(result)) {
            release_com(texture);
            return Result::TEXTURE_CREATION_FAILED;
        }

        uint8_t const* const source = static_cast<uint8_t const*>(desc.rgba_pixels);
        for (UINT row = 0u; row < row_count; ++row) {
            std::memcpy(upload_data + layout.Offset +
                            (static_cast<size_t>(row) * layout.Footprint.RowPitch),
                        source + (static_cast<size_t>(row) * desc.bytes_per_row),
                        static_cast<size_t>(row_size));
        }

        upload->Unmap(0u, nullptr);
        upload_data = nullptr;

        result = upload_texture(context_impl, texture, upload, layout);
        if (result_failed(result)) {
            defer_release(context_impl, upload);
            defer_release(context_impl, texture);
            return result;
        }

        defer_release(context_impl, upload);
        D3D12Texture* texture_impl = arena_new<D3D12Texture>(*context_impl->arena);
        texture_impl->context = context_impl;
        texture_impl->resource = texture;
        out_texture.handle = texture_impl;
        return Result::OK;
    }

    auto destroy_texture(Context context, Texture& texture) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Texture* texture_impl = texture_from_handle(texture);
        ASSERT(context_impl != nullptr);
        ASSERT(texture_impl != nullptr);
        ASSERT(texture_impl->context == context_impl);
        defer_release_com(context_impl, texture_impl->resource);
        texture_impl->context = nullptr;
        texture.handle = nullptr;
    }

    auto create_sampler(Context context, Sampler& out_sampler) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D12Sampler* sampler = arena_new<D3D12Sampler>(*context_impl->arena);
        sampler->context = context_impl;
        sampler->desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler->desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler->desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler->desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler->desc.MaxLOD = D3D12_FLOAT32_MAX;
        out_sampler.handle = sampler;
        return Result::OK;
    }

    auto destroy_sampler(Context context, Sampler& sampler) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Sampler* sampler_impl = sampler_from_handle(sampler);
        ASSERT(context_impl != nullptr);
        ASSERT(sampler_impl != nullptr);
        ASSERT(sampler_impl->context == context_impl);
        sampler_impl->context = nullptr;
        sampler.handle = nullptr;
    }

    auto create_shader(Arena& arena, Context context, ShaderDesc const& desc, Shader& out_shader)
        -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        ArenaMarker const marker = arena.marker();
        D3D12Shader* shader = arena_new<D3D12Shader>(arena);
        shader->context = context_impl;
        shader->bytecode = arena_alloc<uint8_t>(arena, desc.byte_size);
        std::memcpy(shader->bytecode, desc.bytecode, desc.byte_size);
        shader->byte_size = desc.byte_size;
        shader->stage = desc.stage;

        if (shader->bytecode == nullptr) {
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
            ::gui::render::d3d12::create_shader(arena, context, shader_desc, out_shader);
        release_com(shader_blob);
        return result;
    }

    auto destroy_shader(Context context, Shader& shader) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Shader* shader_impl = shader_from_handle(shader);
        ASSERT(context_impl != nullptr);
        ASSERT(shader_impl != nullptr);
        ASSERT(shader_impl->context == context_impl);
        shader_impl->context = nullptr;
        shader_impl->bytecode = nullptr;
        shader_impl->byte_size = 0u;
        shader_impl->stage = ShaderStage::VERTEX;
        shader.handle = nullptr;
    }

    auto
    create_pipeline(Arena& arena, Context context, PipelineDesc const& desc, Pipeline& out_pipeline)
        -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Shader* vertex_shader = shader_from_handle(desc.vertex_shader);
        D3D12Shader* pixel_shader = shader_from_handle(desc.pixel_shader);
        ASSERT(context_impl != nullptr);
        ASSERT(vertex_shader != nullptr);
        ASSERT(pixel_shader != nullptr);
        ASSERT(vertex_shader->context == context_impl);
        ASSERT(pixel_shader->context == context_impl);

        ArenaMarker const marker = arena.marker();
        D3D12Pipeline* pipeline = arena_new<D3D12Pipeline>(arena);
        pipeline->context = context_impl;

        ArenaTemp input_temp = begin_thread_temp_arena();
        D3D12_INPUT_ELEMENT_DESC* input_elements = nullptr;
        if (desc.vertex_attribute_count != 0u) {
            input_elements = arena_alloc<D3D12_INPUT_ELEMENT_DESC>(*input_temp.arena(),
                                                                   desc.vertex_attribute_count);
            for (size_t index = 0u; index < desc.vertex_attribute_count; ++index) {
                VertexAttributeDesc const& attribute = desc.vertex_attributes[index];
                ASSERT(attribute.semantic_name != nullptr);

                D3D12_INPUT_ELEMENT_DESC& element = input_elements[index];
                element.SemanticName = attribute.semantic_name;
                element.SemanticIndex = attribute.semantic_index;
                element.Format = d3d_format(attribute.format);
                element.InputSlot = attribute.buffer_slot;
                element.AlignedByteOffset = attribute.byte_offset;
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                element.InstanceDataStepRate = 0u;
            }
        }

        D3D12_RASTERIZER_DESC rasterizer_desc = {};
        rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer_desc.DepthClipEnable = TRUE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};
        pipeline_desc.pRootSignature = context_impl->root_signature;
        pipeline_desc.VS = {vertex_shader->bytecode, vertex_shader->byte_size};
        pipeline_desc.PS = {pixel_shader->bytecode, pixel_shader->byte_size};
        pipeline_desc.BlendState = blend_desc(desc.blend_mode);
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState = rasterizer_desc;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.InputLayout = {input_elements,
                                     static_cast<UINT>(desc.vertex_attribute_count)};
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1u;
        pipeline_desc.RTVFormats[0u] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1u;

        HRESULT const hr = context_impl->device->CreateGraphicsPipelineState(
            &pipeline_desc, IID_PPV_ARGS(&pipeline->pipeline_state));
        if (FAILED(hr) || pipeline->pipeline_state == nullptr) {
            destroy_pipeline_impl(context_impl, pipeline);
            arena.reset_to(marker);
            return Result::PIPELINE_CREATION_FAILED;
        }

        pipeline->topology = desc.topology;
        out_pipeline.handle = pipeline;
        return Result::OK;
    }

    auto destroy_pipeline(Context context, Pipeline& pipeline) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Pipeline* pipeline_impl = pipeline_from_handle(pipeline);
        ASSERT(context_impl != nullptr);
        ASSERT(pipeline_impl != nullptr);
        ASSERT(pipeline_impl->context == context_impl);
        destroy_pipeline_impl(context_impl, pipeline_impl);
        pipeline.handle = nullptr;
    }

    auto bind_pipeline(Context context, Pipeline pipeline) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Pipeline* pipeline_impl = pipeline_from_handle(pipeline);
        ASSERT(context_impl != nullptr);
        ASSERT(pipeline_impl != nullptr);
        ASSERT(pipeline_impl->context == context_impl);

        ID3D12DescriptorHeap* heaps[] = {context_impl->shader_heap, context_impl->sampler_heap};
        context_impl->command_list->SetDescriptorHeaps(sizeof(heaps) / sizeof(heaps[0u]), heaps);
        context_impl->command_list->SetGraphicsRootSignature(context_impl->root_signature);
        context_impl->command_list->SetPipelineState(pipeline_impl->pipeline_state);
        context_impl->command_list->IASetPrimitiveTopology(d3d_topology(pipeline_impl->topology));
        bind_current_descriptor_tables(context_impl);
    }

    auto create_bind_group(Arena& arena,
                           Context context,
                           BindGroupDesc const& desc,
                           BindGroup& out_group) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);

        D3D12BindGroup* group = arena_new<D3D12BindGroup>(arena);
        group->context = context_impl;

        if (desc.buffer_count != 0u) {
            group->buffers = arena_alloc<D3D12BufferBinding>(arena, desc.buffer_count);
            group->buffer_count = desc.buffer_count;
            for (size_t index = 0u; index < desc.buffer_count; ++index) {
                BindGroupBufferBinding const& source = desc.buffers[index];
                ASSERT(source.slot < BIND_GROUP_SLOT_COUNT);

                D3D12Buffer* buffer = buffer_from_handle(source.buffer);
                ASSERT(buffer != nullptr);
                ASSERT(buffer->context == context_impl);
                ASSERT(buffer->resource != nullptr);
                ASSERT(buffer->binding == BufferBinding::UNIFORM);

                D3D12BufferBinding& target = group->buffers[index];
                target.buffer = buffer;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        if (desc.texture_count != 0u) {
            group->textures = arena_alloc<D3D12TextureBinding>(arena, desc.texture_count);
            group->texture_count = desc.texture_count;
            for (size_t index = 0u; index < desc.texture_count; ++index) {
                BindGroupTextureBinding const& source = desc.textures[index];
                ASSERT(source.slot < BIND_GROUP_SLOT_COUNT);

                D3D12Texture* texture = texture_from_handle(source.texture);
                ASSERT(texture != nullptr);
                ASSERT(texture->context == context_impl);
                ASSERT(texture->resource != nullptr);

                D3D12TextureBinding& target = group->textures[index];
                target.texture = texture;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        if (desc.sampler_count != 0u) {
            group->samplers = arena_alloc<D3D12SamplerBinding>(arena, desc.sampler_count);
            group->sampler_count = desc.sampler_count;
            for (size_t index = 0u; index < desc.sampler_count; ++index) {
                BindGroupSamplerBinding const& source = desc.samplers[index];
                ASSERT(source.slot < BIND_GROUP_SLOT_COUNT);

                D3D12Sampler* sampler = sampler_from_handle(source.sampler);
                ASSERT(sampler != nullptr);
                ASSERT(sampler->context == context_impl);

                D3D12SamplerBinding& target = group->samplers[index];
                target.sampler = sampler;
                target.stage = source.stage;
                target.slot = source.slot;
            }
        }

        out_group.handle = group;
        return Result::OK;
    }

    auto destroy_bind_group(Context context, BindGroup& bind_group) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12BindGroup* group = bind_group_from_handle(bind_group);
        ASSERT(context_impl != nullptr);
        ASSERT(group != nullptr);
        ASSERT(group->context == context_impl);
        group->context = nullptr;
        group->buffers = nullptr;
        group->buffer_count = 0u;
        group->textures = nullptr;
        group->texture_count = 0u;
        group->samplers = nullptr;
        group->sampler_count = 0u;
        bind_group.handle = nullptr;
    }

    auto bind_group(Context context, BindGroup bind_group) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12BindGroup* group = bind_group_from_handle(bind_group);
        ASSERT(context_impl != nullptr);
        ASSERT(group != nullptr);
        ASSERT(group->context == context_impl);
        ASSERT(context_impl->frame_active);

        D3D12DescriptorTable tables[ROOT_COUNT] = {};
        uint32_t update_masks[ROOT_COUNT] = {};

        for (size_t index = 0u; index < group->buffer_count; ++index) {
            D3D12BufferBinding const& binding = group->buffers[index];
            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_CBV : ROOT_PS_CBV;
            update_masks[root_parameter] |= descriptor_mask(binding.slot);
        }

        for (size_t index = 0u; index < group->texture_count; ++index) {
            D3D12TextureBinding const& binding = group->textures[index];
            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_SRV : ROOT_PS_SRV;
            update_masks[root_parameter] |= descriptor_mask(binding.slot);
        }

        for (size_t index = 0u; index < group->sampler_count; ++index) {
            D3D12SamplerBinding const& binding = group->samplers[index];
            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_SAMPLER : ROOT_PS_SAMPLER;
            update_masks[root_parameter] |= descriptor_mask(binding.slot);
        }

        for (size_t index = 0u; index < group->buffer_count; ++index) {
            D3D12BufferBinding const& binding = group->buffers[index];
            D3D12Buffer* buffer = binding.buffer;
            ASSERT(buffer != nullptr);
            ASSERT(buffer->context == context_impl);
            ASSERT(buffer->resource != nullptr);
            ASSERT(buffer->binding == BufferBinding::UNIFORM);

            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_CBV : ROOT_PS_CBV;
            D3D12DescriptorTable& table = writable_descriptor_table(
                context_impl, tables, root_parameter, update_masks[root_parameter]);

            uint32_t const descriptor_offset =
                descriptor_index(context_impl, root_parameter, table.gpu);
            D3D12_CONSTANT_BUFFER_VIEW_DESC view_desc = {};
            view_desc.BufferLocation = buffer->resource->GetGPUVirtualAddress();
            view_desc.SizeInBytes =
                static_cast<UINT>(buffer_resource_size(buffer->binding, buffer->byte_size));
            context_impl->device->CreateConstantBufferView(
                &view_desc,
                descriptor_cpu_handle(
                    context_impl, root_parameter, descriptor_offset + binding.slot));
        }

        for (size_t index = 0u; index < group->texture_count; ++index) {
            D3D12TextureBinding const& binding = group->textures[index];
            D3D12Texture* texture = binding.texture;
            ASSERT(texture != nullptr);
            ASSERT(texture->context == context_impl);
            ASSERT(texture->resource != nullptr);
            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_SRV : ROOT_PS_SRV;
            D3D12DescriptorTable& table = writable_descriptor_table(
                context_impl, tables, root_parameter, update_masks[root_parameter]);

            uint32_t const descriptor_offset =
                descriptor_index(context_impl, root_parameter, table.gpu);
            D3D12_SHADER_RESOURCE_VIEW_DESC view_desc = {};
            view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view_desc.Texture2D.MipLevels = 1u;
            context_impl->device->CreateShaderResourceView(
                texture->resource,
                &view_desc,
                descriptor_cpu_handle(
                    context_impl, root_parameter, descriptor_offset + binding.slot));
        }

        for (size_t index = 0u; index < group->sampler_count; ++index) {
            D3D12SamplerBinding const& binding = group->samplers[index];
            D3D12Sampler* sampler = binding.sampler;
            ASSERT(sampler != nullptr);
            ASSERT(sampler->context == context_impl);
            uint32_t const root_parameter =
                binding.stage == ShaderStage::VERTEX ? ROOT_VS_SAMPLER : ROOT_PS_SAMPLER;
            D3D12DescriptorTable& table = writable_descriptor_table(
                context_impl, tables, root_parameter, update_masks[root_parameter]);

            uint32_t const descriptor_offset =
                descriptor_index(context_impl, root_parameter, table.gpu);
            context_impl->device->CreateSampler(
                &sampler->desc,
                descriptor_cpu_handle(
                    context_impl, root_parameter, descriptor_offset + binding.slot));
        }

        for (uint32_t root_parameter = 0u; root_parameter < ROOT_COUNT; ++root_parameter) {
            if (tables[root_parameter].gpu.ptr != 0u) {
                commit_descriptor_table(context_impl, root_parameter, tables[root_parameter]);
            }
        }
    }

    auto draw(Context context, DrawDesc const& desc) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->render_pass_active);

        ::gui::render::d3d12::bind_pipeline(context, desc.pipeline);
        for (size_t index = 0u; index < desc.bind_group_count; ++index) {
            ::gui::render::d3d12::bind_group(context, desc.bind_groups[index]);
        }

        for (size_t index = 0u; index < desc.vertex_buffer_count; ++index) {
            VertexBufferBinding const& binding = desc.vertex_buffers[index];
            D3D12Buffer* buffer = buffer_from_handle(binding.buffer);
            ASSERT(buffer != nullptr);
            ASSERT(buffer->context == context_impl);
            ASSERT(buffer->resource != nullptr);
            ASSERT(buffer->binding == BufferBinding::VERTEX);
            ASSERT(binding.byte_offset <= buffer->byte_size);

            D3D12_VERTEX_BUFFER_VIEW view = {};
            view.BufferLocation = buffer->resource->GetGPUVirtualAddress() + binding.byte_offset;
            view.SizeInBytes = static_cast<UINT>(buffer->byte_size - binding.byte_offset);
            view.StrideInBytes = binding.byte_stride;
            context_impl->command_list->IASetVertexBuffers(binding.slot, 1u, &view);
        }

        context_impl->command_list->DrawInstanced(desc.vertex_count, 1u, desc.first_vertex, 0u);
    }

    auto resize_window(Context context, Window window, SizeU32 size) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Window* window_impl = window_from_handle(window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(!context_impl->frame_active);
        ASSERT(!context_impl->render_pass_active);

        wait_for_gpu(context_impl);
        release_render_targets(window_impl);

        HRESULT const hr = window_impl->swap_chain->ResizeBuffers(
            0u, size.width, size.height, DXGI_FORMAT_UNKNOWN, 0u);
        if (FAILED(hr)) {
            return Result::RESIZE_FAILED;
        }

        window_impl->size = size;
        Result const render_target_result = create_render_targets(context_impl, window_impl);
        if (result_failed(render_target_result)) {
            return render_target_result;
        }

        return Result::OK;
    }

    auto begin_frame(Context context) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(!context_impl->frame_active);
        ASSERT(!context_impl->render_pass_active);

        uint32_t const frame_index = context_impl->next_frame_resource_index;
        D3D12FrameResource& frame = context_impl->frame_resources[frame_index];
        wait_for_frame_resource(context_impl, &frame);

        context_impl->active_frame_resource_index = frame_index;
        context_impl->next_frame_resource_index = (frame_index + 1u) % FRAME_RESOURCE_COUNT;
        frame.frame_shader_descriptor_count = 0u;
        frame.frame_sampler_descriptor_count = 0u;
        frame.frame_vertex_buffer.used_size = 0u;
        reset_bound_descriptor_tables(context_impl);

        HRESULT hr = frame.command_allocator->Reset();
        ASSERT(SUCCEEDED(hr));
        hr = context_impl->command_list->Reset(frame.command_allocator, nullptr);
        ASSERT(SUCCEEDED(hr));
        BASE_UNUSED(hr);
        context_impl->frame_active = true;
    }

    auto begin_render_pass(Context context, WindowRenderPassDesc const& desc) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Window* window_impl = window_from_handle(desc.window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(context_impl->frame_active);
        ASSERT(!context_impl->render_pass_active);

        window_impl->frame_index = window_impl->swap_chain->GetCurrentBackBufferIndex();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = window_impl->back_buffers[window_impl->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context_impl->command_list->ResourceBarrier(1u, &barrier);

        D3D12_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(window_impl->size.width);
        viewport.Height = static_cast<float>(window_impl->size.height);
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.right = static_cast<LONG>(window_impl->size.width);
        scissor.bottom = static_cast<LONG>(window_impl->size.height);

        D3D12_CPU_DESCRIPTOR_HANDLE const target =
            rtv_handle(window_impl, window_impl->frame_index);
        context_impl->command_list->RSSetViewports(1u, &viewport);
        context_impl->command_list->RSSetScissorRects(1u, &scissor);
        context_impl->command_list->OMSetRenderTargets(1u, &target, FALSE, nullptr);

        switch (desc.load_op) {
        case LoadOp::LOAD:
        case LoadOp::DONT_CARE:
            break;
        case LoadOp::CLEAR: {
            float const clear_color[] = {
                desc.clear_color.r, desc.clear_color.g, desc.clear_color.b, desc.clear_color.a};
            context_impl->command_list->ClearRenderTargetView(target, clear_color, 0u, nullptr);
            break;
        }
        }

        context_impl->active_window = window_impl;
        context_impl->render_pass_active = true;
        return Result::OK;
    }

    auto end_render_pass(Context context) -> void {
        D3D12Context* context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->render_pass_active);
        ASSERT(context_impl->active_window != nullptr);

        D3D12Window* const window = context_impl->active_window;
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = window->back_buffers[window->frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context_impl->command_list->ResourceBarrier(1u, &barrier);

        context_impl->active_window = nullptr;
        context_impl->render_pass_active = false;
    }

    auto present_window(Context context, Window window) -> Result {
        D3D12Context* context_impl = context_from_handle(context);
        D3D12Window* window_impl = window_from_handle(window);
        ASSERT(context_impl != nullptr);
        ASSERT(window_impl != nullptr);
        ASSERT(window_impl->context == context_impl);
        ASSERT(context_impl->frame_active);
        ASSERT(!context_impl->render_pass_active);

        HRESULT hr = context_impl->command_list->Close();
        if (FAILED(hr)) {
            context_impl->frame_active = false;
            return Result::PRESENT_FAILED;
        }

        ID3D12CommandList* command_lists[] = {context_impl->command_list};
        context_impl->command_queue->ExecuteCommandLists(1u, command_lists);

        UINT const sync_interval = window_impl->present_mode == PresentMode::VSYNC ? 1u : 0u;
        hr = window_impl->swap_chain->Present(sync_interval, 0u);
        D3D12FrameResource& frame =
            context_impl->frame_resources[context_impl->active_frame_resource_index];
        frame.fence_value = signal_gpu(context_impl);
        window_impl->frame_index = window_impl->swap_chain->GetCurrentBackBufferIndex();
        context_impl->frame_active = false;

        if (hr == DXGI_STATUS_OCCLUDED) {
            return Result::OCCLUDED;
        }
        if (FAILED(hr)) {
            return Result::PRESENT_FAILED;
        }

        return Result::OK;
    }

    auto window_size(Window window) -> SizeU32 {
        D3D12Window const* const window_impl = window_from_handle(window);
        ASSERT(window_impl != nullptr);
        return window_impl->size;
    }

    auto native_device(Context context) -> void* {
        D3D12Context const* const context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        return context_impl->device;
    }

    auto active_render_pass_command_list(Context context) -> ID3D12GraphicsCommandList* {
        D3D12Context const* const context_impl = context_from_handle(context);
        ASSERT(context_impl != nullptr);
        ASSERT(context_impl->render_pass_active);
        return context_impl->command_list;
    }

    auto native_swap_chain(Window window) -> void* {
        D3D12Window const* const window_impl = window_from_handle(window);
        ASSERT(window_impl != nullptr);
        return window_impl->swap_chain;
    }

} // namespace gui::render::d3d12

#endif
