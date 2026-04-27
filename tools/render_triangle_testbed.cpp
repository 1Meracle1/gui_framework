#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/str_ref.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <render/render.h>
#include <windows.h>

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_render_triangle_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;

    struct Vertex {
        float position[2];
        float color[3];
    };

    struct TransformConstants {
        float offset[4];
    };

    struct TintConstants {
        float color_scale[4];
    };

    struct TrianglePipeline {
        ID3D11VertexShader* vertex_shader = nullptr;
        ID3D11PixelShader* pixel_shader = nullptr;
        ID3D11InputLayout* input_layout = nullptr;
        ID3D11Buffer* vertex_buffer = nullptr;
        ID3D11Buffer* transform_constants = nullptr;
        ID3D11Buffer* tint_constants = nullptr;
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        gui::render::SizeU32 pending_size = {};
    };

    AppState* global_app_state = nullptr;

    template <typename T> auto release_com(T*& value) -> void {
        if (value != nullptr) {
            value->Release();
            value = nullptr;
        }
    }

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    auto log_result(char const* operation, gui::render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::render::result_name(result));
    }

    [[nodiscard]] auto
    compile_shader(StrRef source, char const* entry_point, char const* target, ID3DBlob** out_blob)
        -> bool {
        ID3DBlob* error_blob = nullptr;
        UINT compile_flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if BASE_DEBUG
        compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        HRESULT const hr = D3DCompile(source.data(),
                                      source.size(),
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      entry_point,
                                      target,
                                      compile_flags,
                                      0u,
                                      out_blob,
                                      &error_blob);

        if (FAILED(hr)) {
            if (error_blob != nullptr) {
                StrRef const errors(static_cast<char const*>(error_blob->GetBufferPointer()),
                                    error_blob->GetBufferSize());
                fmt::eprintf("shader compile failed (%s/%s):\n%s\n", entry_point, target, errors);
            } else {
                fmt::eprintf("shader compile failed (%s/%s): HRESULT 0x%08x\n",
                             entry_point,
                             target,
                             static_cast<uint32_t>(hr));
            }
            release_com(error_blob);
            return false;
        }

        release_com(error_blob);
        return true;
    }

    [[nodiscard]] auto create_dynamic_constant_buffer(ID3D11Device* device,
                                                      size_t byte_size,
                                                      ID3D11Buffer** out_buffer) -> bool {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>((byte_size + 15u) & ~15u);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT const hr = device->CreateBuffer(&desc, nullptr, out_buffer);
        return SUCCEEDED(hr);
    }

    [[nodiscard]] auto update_buffer(ID3D11DeviceContext* device_context,
                                     ID3D11Buffer* buffer,
                                     void const* data,
                                     size_t byte_size) -> bool {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT const hr = device_context->Map(buffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(hr)) {
            return false;
        }

        std::memcpy(mapped.pData, data, byte_size);
        device_context->Unmap(buffer, 0u);
        return true;
    }

    auto destroy_pipeline(TrianglePipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        release_com(pipeline->tint_constants);
        release_com(pipeline->transform_constants);
        release_com(pipeline->vertex_buffer);
        release_com(pipeline->input_layout);
        release_com(pipeline->pixel_shader);
        release_com(pipeline->vertex_shader);
    }

    [[nodiscard]] auto create_pipeline(ID3D11Device* device, TrianglePipeline* pipeline) -> bool {
        constexpr StrRef SHADER_SOURCE =
            "cbuffer TransformConstants : register(b0)\n"
            "{\n"
            "    float4 g_offset;\n"
            "};\n"
            "cbuffer TintConstants : register(b0)\n"
            "{\n"
            "    float4 g_color_scale;\n"
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
            "    output.position = float4(input.position + g_offset.xy, 0.0f, 1.0f);\n"
            "    output.color = input.color;\n"
            "    return output;\n"
            "}\n"
            "float4 ps_main(PSInput input) : SV_Target\n"
            "{\n"
            "    return float4(input.color * g_color_scale.rgb, g_color_scale.a);\n"
            "}\n";

        ID3DBlob* vertex_blob = nullptr;
        ID3DBlob* pixel_blob = nullptr;

        if (!compile_shader(SHADER_SOURCE, "vs_main", "vs_4_0", &vertex_blob) ||
            !compile_shader(SHADER_SOURCE, "ps_main", "ps_4_0", &pixel_blob)) {
            release_com(pixel_blob);
            release_com(vertex_blob);
            return false;
        }

        HRESULT hr = device->CreateVertexShader(vertex_blob->GetBufferPointer(),
                                                vertex_blob->GetBufferSize(),
                                                nullptr,
                                                &pipeline->vertex_shader);
        if (FAILED(hr)) {
            release_com(pixel_blob);
            release_com(vertex_blob);
            return false;
        }

        hr = device->CreatePixelShader(pixel_blob->GetBufferPointer(),
                                       pixel_blob->GetBufferSize(),
                                       nullptr,
                                       &pipeline->pixel_shader);
        if (FAILED(hr)) {
            release_com(pixel_blob);
            release_com(vertex_blob);
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC input_elements[] = {
            {"POSITION",
             0u,
             DXGI_FORMAT_R32G32_FLOAT,
             0u,
             static_cast<UINT>(offsetof(Vertex, position)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0u},
            {"COLOR",
             0u,
             DXGI_FORMAT_R32G32B32_FLOAT,
             0u,
             static_cast<UINT>(offsetof(Vertex, color)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0u},
        };

        hr = device->CreateInputLayout(
            input_elements,
            static_cast<UINT>(sizeof(input_elements) / sizeof(input_elements[0u])),
            vertex_blob->GetBufferPointer(),
            vertex_blob->GetBufferSize(),
            &pipeline->input_layout);

        release_com(pixel_blob);
        release_com(vertex_blob);

        if (FAILED(hr)) {
            return false;
        }

        Vertex const vertices[] = {
            {{0.0f, 0.62f}, {1.0f, 0.15f, 0.05f}},
            {{0.55f, -0.42f}, {0.0f, 0.9f, 0.25f}},
            {{-0.55f, -0.42f}, {0.1f, 0.35f, 1.0f}},
        };

        D3D11_SUBRESOURCE_DATA vertex_data = {};
        vertex_data.pSysMem = vertices;

        D3D11_BUFFER_DESC vertex_desc = {};
        vertex_desc.ByteWidth = static_cast<UINT>(sizeof(vertices));
        vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        hr = device->CreateBuffer(&vertex_desc, &vertex_data, &pipeline->vertex_buffer);
        if (FAILED(hr)) {
            return false;
        }

        return create_dynamic_constant_buffer(
                   device, sizeof(TransformConstants), &pipeline->transform_constants) &&
               create_dynamic_constant_buffer(
                   device, sizeof(TintConstants), &pipeline->tint_constants);
    }

    [[nodiscard]] auto render_triangle(gui::render::Context context,
                                       gui::render::Window window,
                                       TrianglePipeline const& pipeline,
                                       float time_seconds) -> bool {
        ID3D11DeviceContext* const device_context =
            static_cast<ID3D11DeviceContext*>(gui::render::native_device_context(context));
        ID3D11RenderTargetView* render_target_view =
            static_cast<ID3D11RenderTargetView*>(gui::render::native_render_target_view(window));
        gui::render::SizeU32 const size = gui::render::window_size(window);

        if (device_context == nullptr || render_target_view == nullptr || size.width == 0u ||
            size.height == 0u) {
            return false;
        }

        TransformConstants transform = {};
        transform.offset[0] = 0.15f * static_cast<float>(std::sin(time_seconds));
        transform.offset[1] = 0.08f * static_cast<float>(std::cos(time_seconds * 0.75f));

        TintConstants tint = {};
        tint.color_scale[0] = 0.95f;
        tint.color_scale[1] =
            0.75f + (0.25f * static_cast<float>(std::sin(time_seconds * 1.4f) * 0.5f + 0.5f));
        tint.color_scale[2] = 1.0f;
        tint.color_scale[3] = 1.0f;

        if (!update_buffer(
                device_context, pipeline.transform_constants, &transform, sizeof(transform)) ||
            !update_buffer(device_context, pipeline.tint_constants, &tint, sizeof(tint))) {
            return false;
        }

        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(size.width);
        viewport.Height = static_cast<float>(size.height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        UINT const stride = sizeof(Vertex);
        UINT const offset = 0u;

        device_context->OMSetRenderTargets(1u, &render_target_view, nullptr);
        device_context->RSSetViewports(1u, &viewport);
        device_context->IASetInputLayout(pipeline.input_layout);
        device_context->IASetVertexBuffers(0u, 1u, &pipeline.vertex_buffer, &stride, &offset);
        device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device_context->VSSetShader(pipeline.vertex_shader, nullptr, 0u);
        device_context->VSSetConstantBuffers(0u, 1u, &pipeline.transform_constants);
        device_context->PSSetShader(pipeline.pixel_shader, nullptr, 0u);
        device_context->PSSetConstantBuffers(0u, 1u, &pipeline.tint_constants);
        device_context->Draw(3u, 0u);
        return true;
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                gui::render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                }
            }
            return 0;

        case WM_CLOSE:
            if (global_app_state != nullptr) {
                global_app_state->running = false;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            return false;
        }

        HWND const hwnd = CreateWindowExW(0u,
                                          WINDOW_CLASS_NAME,
                                          L"gui_framework render triangle testbed",
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
            return false;
        }

        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

} // namespace

auto main() -> int {
    base::install_crash_handlers();

    AppState app_state = {};
    global_app_state = &app_state;

    if (!create_testbed_window(&app_state)) {
        return 1;
    }

    gui::render::Context render_context = {};
    gui::render::ContextDesc context_desc = {};
    context_desc.backend = gui::render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    gui::render::Result result = gui::render::create_context(context_desc, &render_context);
    if (gui::render::result_failed(result)) {
        log_result("render::create_context", result);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    gui::render::Window render_window = {};
    gui::render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = gui::render::PresentMode::VSYNC;

    result = gui::render::create_window(render_context, window_desc, &render_window);
    if (gui::render::result_failed(result)) {
        log_result("render::create_window", result);
        gui::render::destroy_context(&render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    ID3D11Device* const device =
        static_cast<ID3D11Device*>(gui::render::native_device(render_context));
    TrianglePipeline pipeline = {};

    if (device == nullptr || !create_pipeline(device, &pipeline)) {
        fmt::eprintf("failed to create D3D11 triangle pipeline\n");
        destroy_pipeline(&pipeline);
        gui::render::destroy_window(&render_window);
        gui::render::destroy_context(&render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    uint64_t const start_ticks = GetTickCount64();

    while (app_state.running) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                app_state.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!app_state.running) {
            break;
        }

        if (app_state.resize_pending) {
            result =
                gui::render::resize_window(render_context, render_window, app_state.pending_size);
            if (gui::render::result_failed(result)) {
                log_result("render::resize_window", result);
                app_state.running = false;
                continue;
            }
            app_state.resize_pending = false;
        }

        result = gui::render::begin_frame(render_context);
        if (gui::render::result_failed(result)) {
            log_result("render::begin_frame", result);
            break;
        }

        result =
            gui::render::clear_window(render_context, render_window, {0.03f, 0.08f, 0.11f, 1.0f});
        if (gui::render::result_failed(result)) {
            log_result("render::clear_window", result);
            break;
        }

        uint64_t const elapsed_ticks = GetTickCount64() - start_ticks;
        float const time_seconds = static_cast<float>(elapsed_ticks) * 0.001f;
        if (!render_triangle(render_context, render_window, pipeline, time_seconds)) {
            fmt::eprintf("failed to render triangle\n");
            break;
        }

        result = gui::render::present_window(render_window);
        if (result == gui::render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (gui::render::result_failed(result)) {
            log_result("render::present_window", result);
            break;
        }
    }

    destroy_pipeline(&pipeline);
    gui::render::destroy_window(&render_window);
    gui::render::destroy_context(&render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
