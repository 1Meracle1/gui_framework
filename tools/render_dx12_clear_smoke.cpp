#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <base/assert.h>
#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <render_d3d12.h>

// clang-format off
#include <render/render.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
// clang-format on

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_render_dx12_clear_smoke";
    constexpr UINT READBACK_ROW_PITCH = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    constexpr UINT SMOKE_SAMPLE_X = 79u;
    constexpr UINT SMOKE_SAMPLE_Y = 32u;
    constexpr uint8_t SMOKE_TEXTURE_RGBA[] = {230u, 50u, 25u, 255u};

    struct Vertex {
        float position[2];
        float color[3];
    };

    struct Constants {
        float value[4];
    };

    constexpr Vertex DRAW_VERTICES[] = {
        {{0.0f, 0.55f}, {1.0f, 0.15f, 0.05f}},
        {{0.5f, -0.35f}, {0.1f, 0.85f, 0.25f}},
        {{-0.5f, -0.35f}, {0.1f, 0.25f, 1.0f}},
    };

    struct DrawSmoke {
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Buffer vertex_buffer = {};
        gui::render::Buffer vertex_offset_constants = {};
        gui::render::Buffer vertex_scale_constants = {};
        gui::render::Buffer pixel_constants = {};
        gui::render::Texture texture = {};
        gui::render::Sampler sampler = {};
        gui::render::BindGroup vertex_offset_bind_group = {};
        gui::render::BindGroup vertex_scale_bind_group = {};
        gui::render::BindGroup pixel_bind_group = {};
        gui::render::BindGroup texture_bind_group = {};
        ID3D12Resource* readback = nullptr;
    };

    std::jmp_buf active_assert_jump_buffer;

    auto log_result(char const* operation, gui::render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::render::result_name(result));
    }

    auto log_hresult(char const* operation, HRESULT hr) -> void {
        fmt::eprintf("%s failed: 0x%08x\n", operation, static_cast<uint32_t>(hr));
    }

    template <typename T> auto release_com(T*& value) -> void {
        if (value != nullptr) {
            value->Release();
            value = nullptr;
        }
    }

    auto smoke_assert_handler(char const* expression, char const* file, uint32_t line) -> void {
        BASE_UNUSED(expression);
        BASE_UNUSED(file);
        BASE_UNUSED(line);
        std::longjmp(active_assert_jump_buffer, 1);
    }

    auto CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    auto pump_messages() -> void {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    [[nodiscard]] auto create_window(HINSTANCE instance) -> HWND {
        WNDCLASSEXW window_class = {};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return nullptr;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = 96;
        rect.bottom = 64;
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            return nullptr;
        }

        HWND const hwnd = CreateWindowExW(0u,
                                          WINDOW_CLASS_NAME,
                                          L"gui_framework dx12 clear smoke",
                                          style,
                                          CW_USEDEFAULT,
                                          CW_USEDEFAULT,
                                          rect.right - rect.left,
                                          rect.bottom - rect.top,
                                          nullptr,
                                          nullptr,
                                          instance,
                                          nullptr);
        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            return nullptr;
        }

        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        pump_messages();
        return hwnd;
    }

    auto destroy_draw_smoke(gui::render::Context context, DrawSmoke* smoke) -> void {
        release_com(smoke->readback);
        if (gui::render::bind_group_valid(smoke->pixel_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->pixel_bind_group);
        }
        if (gui::render::bind_group_valid(smoke->texture_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->texture_bind_group);
        }
        if (gui::render::bind_group_valid(smoke->vertex_scale_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->vertex_scale_bind_group);
        }
        if (gui::render::bind_group_valid(smoke->vertex_offset_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->vertex_offset_bind_group);
        }
        if (gui::render::sampler_valid(smoke->sampler)) {
            gui::render::destroy_sampler(context, smoke->sampler);
        }
        if (gui::render::texture_valid(smoke->texture)) {
            gui::render::destroy_texture(context, smoke->texture);
        }
        if (gui::render::buffer_valid(smoke->pixel_constants)) {
            gui::render::destroy_buffer(context, smoke->pixel_constants);
        }
        if (gui::render::buffer_valid(smoke->vertex_buffer)) {
            gui::render::destroy_buffer(context, smoke->vertex_buffer);
        }
        if (gui::render::buffer_valid(smoke->vertex_scale_constants)) {
            gui::render::destroy_buffer(context, smoke->vertex_scale_constants);
        }
        if (gui::render::buffer_valid(smoke->vertex_offset_constants)) {
            gui::render::destroy_buffer(context, smoke->vertex_offset_constants);
        }
        if (gui::render::pipeline_valid(smoke->pipeline)) {
            gui::render::destroy_pipeline(context, smoke->pipeline);
        }
        if (gui::render::shader_valid(smoke->pixel_shader)) {
            gui::render::destroy_shader(context, smoke->pixel_shader);
        }
        if (gui::render::shader_valid(smoke->vertex_shader)) {
            gui::render::destroy_shader(context, smoke->vertex_shader);
        }
    }

    [[nodiscard]] auto create_readback(gui::render::Context context, DrawSmoke* smoke) -> bool {
        ID3D12Device* const device =
            static_cast<ID3D12Device*>(gui::render::native_device(context));
        if (device == nullptr) {
            return false;
        }

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width = READBACK_ROW_PITCH;
        resource_desc.Height = 1u;
        resource_desc.DepthOrArraySize = 1u;
        resource_desc.MipLevels = 1u;
        resource_desc.SampleDesc.Count = 1u;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT const hr = device->CreateCommittedResource(&heap_properties,
                                                           D3D12_HEAP_FLAG_NONE,
                                                           &resource_desc,
                                                           D3D12_RESOURCE_STATE_COPY_DEST,
                                                           nullptr,
                                                           IID_PPV_ARGS(&smoke->readback));
        if (FAILED(hr) || smoke->readback == nullptr) {
            log_hresult("ID3D12Device::CreateCommittedResource(readback)", hr);
            return false;
        }

        return true;
    }

    [[nodiscard]] auto bind_transient_texture_sampler_groups(Arena& arena,
                                                             gui::render::Context context,
                                                             DrawSmoke const& smoke) -> bool {
        constexpr size_t TRANSIENT_BIND_GROUP_COUNT = 129u;

        gui::render::BindGroupTextureBinding texture_binding = {};
        texture_binding.stage = gui::render::ShaderStage::PIXEL;
        texture_binding.texture = smoke.texture;

        gui::render::BindGroupSamplerBinding sampler_binding = {};
        sampler_binding.stage = gui::render::ShaderStage::PIXEL;
        sampler_binding.sampler = smoke.sampler;

        gui::render::BindGroupDesc desc = {};
        desc.textures = &texture_binding;
        desc.texture_count = 1u;
        desc.samplers = &sampler_binding;
        desc.sampler_count = 1u;

        ArenaMarker const marker = arena.marker();
        gui::render::bind_pipeline(context, smoke.pipeline);
        for (size_t index = 0u; index < TRANSIENT_BIND_GROUP_COUNT; ++index) {
            gui::render::BindGroup bind_group = {};
            gui::render::Result const result =
                gui::render::create_bind_group(arena, context, desc, bind_group);
            if (gui::render::result_failed(result)) {
                log_result("render::create_bind_group(transient texture)", result);
                arena.reset_to(marker);
                return false;
            }
            gui::render::bind_group(context, bind_group);
            gui::render::destroy_bind_group(context, bind_group);
            arena.reset_to(marker);
        }
        return true;
    }

    [[nodiscard]] auto
    create_draw_smoke(Arena& arena, gui::render::Context context, DrawSmoke* smoke) -> bool {
        constexpr StrRef SHADER_SOURCE =
            "Texture2D g_smoke_texture : register(t0);\n"
            "SamplerState g_smoke_sampler : register(s0);\n"
            "cbuffer VertexOffsetConstants : register(b0)\n"
            "{\n"
            "    float4 g_offset;\n"
            "};\n"
            "cbuffer VertexScaleConstants : register(b1)\n"
            "{\n"
            "    float4 g_scale;\n"
            "};\n"
            "cbuffer PixelConstants : register(b0)\n"
            "{\n"
            "    float4 g_tint;\n"
            "};\n"
            "struct VSInput\n"
            "{\n"
            "    float2 position : POSITION;\n"
            "    float3 color : COLOR0;\n"
            "};\n"
            "struct PSInput\n"
            "{\n"
            "    float4 position : SV_POSITION;\n"
            "    float3 color : COLOR0;\n"
            "};\n"
            "PSInput vs_main(VSInput input)\n"
            "{\n"
            "    PSInput output;\n"
            "    output.position = float4((input.position * g_scale.xy) + g_offset.xy, 0.0f, "
            "1.0f);\n"
            "    output.color = input.color;\n"
            "    return output;\n"
            "}\n"
            "float4 ps_main(PSInput input) : SV_Target\n"
            "{\n"
            "    float4 sample_value = g_smoke_texture.Sample(g_smoke_sampler, float2(0.5f, "
            "0.5f));\n"
            "    return float4(g_tint.rgb * sample_value.rgb + (input.color * 0.0f), g_tint.a * "
            "sample_value.a);\n"
            "}\n";

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, context, shader_desc, smoke->vertex_shader);
        if (gui::render::result_failed(result)) {
            log_result("render::create_shader_from_source(vs)", result);
            return false;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";
        result = gui::render::create_shader_from_source(
            arena, context, shader_desc, smoke->pixel_shader);
        if (gui::render::result_failed(result)) {
            log_result("render::create_shader_from_source(ps)", result);
            return false;
        }

        gui::render::VertexAttributeDesc attributes[] = {
            {"POSITION",
             0u,
             gui::render::VertexFormat::FLOAT32_2,
             0u,
             static_cast<uint32_t>(offsetof(Vertex, position))},
            {"COLOR",
             0u,
             gui::render::VertexFormat::FLOAT32_3,
             0u,
             static_cast<uint32_t>(offsetof(Vertex, color))},
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = smoke->vertex_shader;
        pipeline_desc.pixel_shader = smoke->pixel_shader;
        pipeline_desc.vertex_attributes = attributes;
        pipeline_desc.vertex_attribute_count = sizeof(attributes) / sizeof(attributes[0u]);

        result = gui::render::create_pipeline(arena, context, pipeline_desc, smoke->pipeline);
        if (gui::render::result_failed(result)) {
            log_result("render::create_pipeline", result);
            return false;
        }

        gui::render::BufferDesc vertex_desc = {};
        vertex_desc.binding = gui::render::BufferBinding::VERTEX;
        vertex_desc.usage = gui::render::BufferUsage::IMMUTABLE;
        vertex_desc.byte_size = sizeof(DRAW_VERTICES);
        vertex_desc.initial_data = DRAW_VERTICES;

        result = gui::render::create_buffer(context, vertex_desc, smoke->vertex_buffer);
        if (gui::render::result_failed(result)) {
            log_result("render::create_buffer(vertex buffer)", result);
            return false;
        }

        gui::render::BufferDesc constants_desc = {};
        constants_desc.binding = gui::render::BufferBinding::UNIFORM;
        constants_desc.usage = gui::render::BufferUsage::DYNAMIC;
        constants_desc.byte_size = sizeof(Constants);

        result =
            gui::render::create_buffer(context, constants_desc, smoke->vertex_offset_constants);
        if (gui::render::result_failed(result)) {
            log_result("render::create_buffer(vertex offset constants)", result);
            return false;
        }

        result = gui::render::create_buffer(context, constants_desc, smoke->vertex_scale_constants);
        if (gui::render::result_failed(result)) {
            log_result("render::create_buffer(vertex scale constants)", result);
            return false;
        }

        result = gui::render::create_buffer(context, constants_desc, smoke->pixel_constants);
        if (gui::render::result_failed(result)) {
            log_result("render::create_buffer(pixel constants)", result);
            return false;
        }

        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = {1u, 1u};
        texture_desc.bytes_per_row = sizeof(SMOKE_TEXTURE_RGBA);
        texture_desc.rgba_pixels = SMOKE_TEXTURE_RGBA;

        result = gui::render::create_texture(context, texture_desc, smoke->texture);
        if (gui::render::result_failed(result)) {
            log_result("render::create_texture", result);
            return false;
        }

        result = gui::render::create_sampler(context, smoke->sampler);
        if (gui::render::result_failed(result)) {
            log_result("render::create_sampler", result);
            return false;
        }

        gui::render::BindGroupBufferBinding vertex_offset_binding = {};
        vertex_offset_binding.stage = gui::render::ShaderStage::VERTEX;
        vertex_offset_binding.slot = 0u;
        vertex_offset_binding.buffer = smoke->vertex_offset_constants;

        gui::render::BindGroupDesc bind_group_desc = {};
        bind_group_desc.buffers = &vertex_offset_binding;
        bind_group_desc.buffer_count = 1u;

        result = gui::render::create_bind_group(
            arena, context, bind_group_desc, smoke->vertex_offset_bind_group);
        if (gui::render::result_failed(result)) {
            log_result("render::create_bind_group(vertex offset)", result);
            return false;
        }

        gui::render::BindGroupBufferBinding vertex_scale_binding = {};
        vertex_scale_binding.stage = gui::render::ShaderStage::VERTEX;
        vertex_scale_binding.slot = 1u;
        vertex_scale_binding.buffer = smoke->vertex_scale_constants;
        bind_group_desc.buffers = &vertex_scale_binding;

        result = gui::render::create_bind_group(
            arena, context, bind_group_desc, smoke->vertex_scale_bind_group);
        if (gui::render::result_failed(result)) {
            log_result("render::create_bind_group(vertex scale)", result);
            return false;
        }

        gui::render::BindGroupBufferBinding pixel_binding = {};
        pixel_binding.stage = gui::render::ShaderStage::PIXEL;
        pixel_binding.buffer = smoke->pixel_constants;
        bind_group_desc.buffers = &pixel_binding;

        result = gui::render::create_bind_group(
            arena, context, bind_group_desc, smoke->pixel_bind_group);
        if (gui::render::result_failed(result)) {
            log_result("render::create_bind_group(pixel)", result);
            return false;
        }

        gui::render::BindGroupSamplerBinding sampler_binding = {};
        sampler_binding.stage = gui::render::ShaderStage::PIXEL;
        sampler_binding.slot = 0u;
        sampler_binding.sampler = smoke->sampler;

        gui::render::BindGroupTextureBinding texture_binding = {};
        texture_binding.stage = gui::render::ShaderStage::PIXEL;
        texture_binding.slot = 0u;
        texture_binding.texture = smoke->texture;
        bind_group_desc.buffers = nullptr;
        bind_group_desc.buffer_count = 0u;
        bind_group_desc.textures = &texture_binding;
        bind_group_desc.texture_count = 1u;
        bind_group_desc.samplers = &sampler_binding;
        bind_group_desc.sampler_count = 1u;

        result = gui::render::create_bind_group(
            arena, context, bind_group_desc, smoke->texture_bind_group);
        if (gui::render::result_failed(result)) {
            log_result("render::create_bind_group(texture)", result);
            return false;
        }

        return create_readback(context, smoke);
    }

    auto draw_smoke_triangle(gui::render::Context context, DrawSmoke const& smoke) -> void {
        Constants vertex_constants = {};
        vertex_constants.value[0] = 0.65f;
        vertex_constants.value[3] = 1.0f;

        Constants vertex_scale = {};
        vertex_scale.value[0] = 0.5f;
        vertex_scale.value[1] = 0.5f;
        vertex_scale.value[3] = 1.0f;

        Constants pixel_constants = {};
        pixel_constants.value[0] = 1.0f;
        pixel_constants.value[1] = 1.0f;
        pixel_constants.value[2] = 1.0f;
        pixel_constants.value[3] = 1.0f;

        gui::render::update_buffer(
            context, smoke.vertex_offset_constants, &vertex_constants, sizeof(vertex_constants));
        gui::render::update_buffer(
            context, smoke.vertex_scale_constants, &vertex_scale, sizeof(vertex_scale));
        gui::render::update_buffer(
            context, smoke.pixel_constants, &pixel_constants, sizeof(pixel_constants));

        gui::render::VertexBufferBinding vertex_buffer = {};
        vertex_buffer.buffer = smoke.vertex_buffer;
        vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(Vertex));

        gui::render::bind_pipeline(context, smoke.pipeline);
        gui::render::bind_group(context, smoke.vertex_offset_bind_group);
        gui::render::bind_group(context, smoke.vertex_scale_bind_group);
        gui::render::bind_group(context, smoke.pixel_bind_group);
        gui::render::bind_group(context, smoke.texture_bind_group);

        gui::render::DrawDesc draw_desc = {};
        draw_desc.vertex_buffers = &vertex_buffer;
        draw_desc.vertex_buffer_count = 1u;
        draw_desc.vertex_count = 3u;

        gui::render::draw(context, draw_desc);
    }

    [[nodiscard]] auto capture_smoke_pixel(gui::render::Context context,
                                           gui::render::Window window,
                                           ID3D12Resource* readback) -> bool {
        IDXGISwapChain3* const swap_chain =
            static_cast<IDXGISwapChain3*>(gui::render::native_swap_chain(window));
        ID3D12GraphicsCommandList* const command_list =
            gui::render::d3d12::active_render_pass_command_list(context);
        if (swap_chain == nullptr || command_list == nullptr || readback == nullptr) {
            return false;
        }

        ID3D12Resource* back_buffer = nullptr;
        HRESULT const hr = swap_chain->GetBuffer(swap_chain->GetCurrentBackBufferIndex(),
                                                 IID_PPV_ARGS(&back_buffer));
        if (FAILED(hr) || back_buffer == nullptr) {
            log_hresult("IDXGISwapChain3::GetBuffer", hr);
            return false;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = back_buffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &barrier);

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = back_buffer;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0u;

        D3D12_TEXTURE_COPY_LOCATION target = {};
        target.pResource = readback;
        target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        target.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        target.PlacedFootprint.Footprint.Width = 1u;
        target.PlacedFootprint.Footprint.Height = 1u;
        target.PlacedFootprint.Footprint.Depth = 1u;
        target.PlacedFootprint.Footprint.RowPitch = READBACK_ROW_PITCH;

        D3D12_BOX source_box = {};
        source_box.left = SMOKE_SAMPLE_X;
        source_box.top = SMOKE_SAMPLE_Y;
        source_box.front = 0u;
        source_box.right = SMOKE_SAMPLE_X + 1u;
        source_box.bottom = SMOKE_SAMPLE_Y + 1u;
        source_box.back = 1u;
        command_list->CopyTextureRegion(&target, 0u, 0u, 0u, &source, &source_box);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list->ResourceBarrier(1u, &barrier);

        release_com(back_buffer);
        return true;
    }

    [[nodiscard]] auto verify_smoke_pixel(DrawSmoke* smoke) -> bool {
        void* mapped = nullptr;
        D3D12_RANGE read_range = {0u, 4u};
        HRESULT const hr = smoke->readback->Map(0u, &read_range, &mapped);
        if (FAILED(hr) || mapped == nullptr) {
            log_hresult("ID3D12Resource::Map(readback)", hr);
            return false;
        }

        uint8_t const* const data = static_cast<uint8_t const*>(mapped);
        uint8_t const r = data[0u];
        uint8_t const g = data[1u];
        uint8_t const b = data[2u];
        uint8_t const a = data[3u];
        D3D12_RANGE write_range = {0u, 0u};
        smoke->readback->Unmap(0u, &write_range);

        bool const ok =
            r > 200u && r < 245u && g > 35u && g < 70u && b > 10u && b < 45u && a > 200u;
        if (!ok) {
            fmt::eprintf("DX12 draw binding smoke failed: pixel=(%u,%u,%u,%u)\n",
                         static_cast<uint32_t>(r),
                         static_cast<uint32_t>(g),
                         static_cast<uint32_t>(b),
                         static_cast<uint32_t>(a));
        }
        return ok;
    }

    [[nodiscard]] auto verify_frame_upload_alignment(gui::render::Context context) -> bool {
        gui::render::FrameBufferSlice const first =
            gui::render::allocate_frame_vertex_buffer(context, 1u, 1u);
        gui::render::FrameBufferSlice const second =
            gui::render::allocate_frame_vertex_buffer(context, 1u, 3u);
        if (first.data == nullptr || second.data == nullptr || first.byte_offset != 0u ||
            second.byte_offset != 3u) {
            fmt::eprintf("DX12 frame upload alignment smoke failed: offsets=%zu,%zu\n",
                         first.byte_offset,
                         second.byte_offset);
            return false;
        }
        return true;
    }

    [[nodiscard]] auto clear_present(Arena& arena,
                                     gui::render::Context context,
                                     gui::render::Window window,
                                     gui::render::Color color,
                                     DrawSmoke const* smoke,
                                     bool capture_pixel) -> bool {
        gui::render::begin_frame(context);
        bool const aligned = verify_frame_upload_alignment(context);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = window;
        pass_desc.clear_color = color;

        gui::render::Result result = gui::render::begin_render_pass(context, pass_desc);
        if (gui::render::result_failed(result)) {
            log_result("render::begin_render_pass", result);
            return false;
        }

        if (smoke != nullptr) {
            if (!bind_transient_texture_sampler_groups(arena, context, *smoke)) {
                return false;
            }
            draw_smoke_triangle(context, *smoke);
        }

        bool captured = true;
        if (capture_pixel && smoke != nullptr) {
            captured = capture_smoke_pixel(context, window, smoke->readback);
        }

        gui::render::end_render_pass(context);

        result = gui::render::present_window(context, window);
        if (gui::render::result_failed(result)) {
            log_result("render::present_window", result);
            return false;
        }

        return captured && aligned;
    }

    [[nodiscard]] auto expect_present_asserts_with_active_pass(gui::render::Context context,
                                                               gui::render::Window window) -> bool {
        gui::render::begin_frame(context);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = window;
        pass_desc.clear_color = {0.04f, 0.04f, 0.04f, 1.0f};

        gui::render::Result result = gui::render::begin_render_pass(context, pass_desc);
        if (gui::render::result_failed(result)) {
            log_result("render::begin_render_pass(assert smoke)", result);
            return false;
        }

        base::set_assert_handler(smoke_assert_handler);
        bool asserted = false;

#if BASE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
        if (setjmp(active_assert_jump_buffer) == 0) {
            BASE_UNUSED(gui::render::present_window(context, window));
        } else {
            asserted = true;
        }
#if BASE_COMPILER_MSVC
#pragma warning(pop)
#endif

        base::set_assert_handler(nullptr);

        if (!asserted) {
            fmt::eprintf("DX12 present active pass smoke failed: present did not assert\n");
            return false;
        }

        gui::render::end_render_pass(context);
        result = gui::render::present_window(context, window);
        if (gui::render::result_failed(result)) {
            log_result("render::present_window(assert smoke cleanup)", result);
            return false;
        }

        return true;
    }

} // namespace

auto main() -> int {
    base::install_crash_handlers();

    HINSTANCE const instance = GetModuleHandleW(nullptr);
    HWND const hwnd = create_window(instance);
    if (hwnd == nullptr) {
        return 1;
    }

    Arena arena = {};
    arena.init();

    gui::render::Context context = {};
    gui::render::ContextDesc context_desc = {};
    context_desc.backend = gui::render::Backend::D3D12;

    gui::render::Result result = gui::render::create_context(arena, context_desc, context);
    if (gui::render::result_failed(result)) {
        log_result("render::create_context", result);
        DestroyWindow(hwnd);
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return 1;
    }

    gui::render::Window window = {};
    gui::render::WindowDesc window_desc = {};
    window_desc.native_window = hwnd;
    window_desc.size = {96u, 64u};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = gui::render::PresentMode::IMMEDIATE;

    result = gui::render::create_window(arena, context, window_desc, window);
    if (gui::render::result_failed(result)) {
        log_result("render::create_window", result);
        gui::render::destroy_context(context);
        DestroyWindow(hwnd);
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return 1;
    }

    DrawSmoke draw_smoke = {};
    bool ok = create_draw_smoke(arena, context, &draw_smoke);
    if (ok) {
        ok = clear_present(arena, context, window, {0.1f, 0.2f, 0.35f, 1.0f}, &draw_smoke, true);
    }
    pump_messages();

    if (ok) {
        result = gui::render::resize_window(context, window, {128u, 72u});
        if (gui::render::result_failed(result)) {
            log_result("render::resize_window", result);
            ok = false;
        }
    }

    if (ok) {
        ok = verify_smoke_pixel(&draw_smoke);
    }

    if (ok) {
        ok = clear_present(arena, context, window, {0.35f, 0.1f, 0.2f, 1.0f}, &draw_smoke, false);
    }

    if (ok) {
        ok = expect_present_asserts_with_active_pass(context, window);
    }

    gui::render::destroy_window(window);
    destroy_draw_smoke(context, &draw_smoke);
    gui::render::destroy_context(context);
    DestroyWindow(hwnd);
    UnregisterClassW(WINDOW_CLASS_NAME, instance);
    return ok ? 0 : 1;
}
