#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

    struct Vertex {
        float position[2];
        float color[3];
    };

    struct Constants {
        float value[4];
    };

    struct DrawSmoke {
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Buffer vertex_offset_constants = {};
        gui::render::Buffer vertex_scale_constants = {};
        gui::render::Buffer pixel_constants = {};
        gui::render::BindGroup vertex_offset_bind_group = {};
        gui::render::BindGroup vertex_scale_bind_group = {};
        gui::render::BindGroup pixel_bind_group = {};
        ID3D12Resource* readback = nullptr;
    };

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
        if (gui::render::bind_group_valid(smoke->vertex_scale_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->vertex_scale_bind_group);
        }
        if (gui::render::bind_group_valid(smoke->vertex_offset_bind_group)) {
            gui::render::destroy_bind_group(context, smoke->vertex_offset_bind_group);
        }
        if (gui::render::buffer_valid(smoke->pixel_constants)) {
            gui::render::destroy_buffer(context, smoke->pixel_constants);
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

    [[nodiscard]] auto
    create_draw_smoke(Arena& arena, gui::render::Context context, DrawSmoke* smoke) -> bool {
        constexpr StrRef SHADER_SOURCE =
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
            "    return float4(g_tint.rgb + (input.color * 0.0f), g_tint.a);\n"
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

        return create_readback(context, smoke);
    }

    auto draw_smoke_triangle(gui::render::Context context, DrawSmoke const& smoke) -> void {
        Vertex const vertices[] = {
            {{0.0f, 0.55f}, {1.0f, 0.15f, 0.05f}},
            {{0.5f, -0.35f}, {0.1f, 0.85f, 0.25f}},
            {{-0.5f, -0.35f}, {0.1f, 0.25f, 1.0f}},
        };

        gui::render::FrameBufferSlice const upload = gui::render::allocate_frame_buffer(
            context, gui::render::BufferBinding::VERTEX, sizeof(vertices), alignof(Vertex));
        std::memcpy(upload.data, vertices, sizeof(vertices));
        gui::render::commit_frame_uploads(context);

        Constants vertex_constants = {};
        vertex_constants.value[0] = 0.65f;
        vertex_constants.value[3] = 1.0f;

        Constants vertex_scale = {};
        vertex_scale.value[0] = 0.5f;
        vertex_scale.value[1] = 0.5f;
        vertex_scale.value[3] = 1.0f;

        Constants pixel_constants = {};
        pixel_constants.value[0] = 0.9f;
        pixel_constants.value[1] = 0.2f;
        pixel_constants.value[2] = 0.1f;
        pixel_constants.value[3] = 1.0f;

        gui::render::update_buffer(
            context, smoke.vertex_offset_constants, &vertex_constants, sizeof(vertex_constants));
        gui::render::update_buffer(
            context, smoke.vertex_scale_constants, &vertex_scale, sizeof(vertex_scale));
        gui::render::update_buffer(
            context, smoke.pixel_constants, &pixel_constants, sizeof(pixel_constants));

        gui::render::VertexBufferBinding vertex_buffer = {};
        vertex_buffer.buffer = upload.buffer;
        vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(Vertex));
        vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);

        gui::render::BindGroup bind_groups[] = {
            smoke.vertex_offset_bind_group,
            smoke.vertex_scale_bind_group,
            smoke.pixel_bind_group,
        };

        gui::render::DrawDesc draw_desc = {};
        draw_desc.pipeline = smoke.pipeline;
        draw_desc.vertex_buffers = &vertex_buffer;
        draw_desc.vertex_buffer_count = 1u;
        draw_desc.bind_groups = bind_groups;
        draw_desc.bind_group_count = sizeof(bind_groups) / sizeof(bind_groups[0u]);
        draw_desc.vertex_count = 3u;

        gui::render::draw(context, draw_desc);
    }

    [[nodiscard]] auto capture_smoke_pixel(gui::render::Context context,
                                           gui::render::Window window,
                                           ID3D12Resource* readback) -> bool {
        IDXGISwapChain3* const swap_chain =
            static_cast<IDXGISwapChain3*>(gui::render::native_swap_chain(window));
        ID3D12GraphicsCommandList* const command_list =
            static_cast<ID3D12GraphicsCommandList*>(gui::render::native_device_context(context));
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

        bool const ok = r > 180u && g > 30u && g < 90u && b < 60u && a > 200u;
        if (!ok) {
            fmt::eprintf("DX12 bind group slot smoke failed: pixel=(%u,%u,%u,%u)\n",
                         static_cast<uint32_t>(r),
                         static_cast<uint32_t>(g),
                         static_cast<uint32_t>(b),
                         static_cast<uint32_t>(a));
        }
        return ok;
    }

    [[nodiscard]] auto clear_present(gui::render::Context context,
                                     gui::render::Window window,
                                     gui::render::Color color,
                                     DrawSmoke const* smoke,
                                     bool capture_pixel) -> bool {
        gui::render::begin_frame(context);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = window;
        pass_desc.clear_color = color;

        gui::render::Result result = gui::render::begin_render_pass(context, pass_desc);
        if (gui::render::result_failed(result)) {
            log_result("render::begin_render_pass", result);
            return false;
        }

        if (smoke != nullptr) {
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

        return captured;
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
        ok = clear_present(context, window, {0.1f, 0.2f, 0.35f, 1.0f}, &draw_smoke, true);
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
        ok = clear_present(context, window, {0.35f, 0.1f, 0.2f, 1.0f}, &draw_smoke, false);
    }

    gui::render::destroy_window(window);
    destroy_draw_smoke(context, &draw_smoke);
    gui::render::destroy_context(context);
    DestroyWindow(hwnd);
    UnregisterClassW(WINDOW_CLASS_NAME, instance);
    return ok ? 0 : 1;
}
