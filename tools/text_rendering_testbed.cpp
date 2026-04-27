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
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <draw/draw.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_text_rendering_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;

    struct TextVertex {
        float position[2];
        float uv[2];
        float color[4];
    };

    struct TextPipeline {
        ID3D11VertexShader* vertex_shader = nullptr;
        ID3D11PixelShader* pixel_shader = nullptr;
        ID3D11InputLayout* input_layout = nullptr;
        ID3D11Buffer* vertex_buffer = nullptr;
        ID3D11SamplerState* sampler = nullptr;
        ID3D11BlendState* blend_state = nullptr;
    };

    struct TextState {
        gui::font_provider::Context provider = {};
        gui::font_cache::Cache cache = {};
        gui::font_cache::Font font = {};
        gui::draw::Context draw_context = {};
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

    auto log_render_result(char const* operation, gui::render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::render::result_name(result));
    }

    auto log_font_result(char const* operation, gui::font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::font_provider::result_name(result));
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

    auto destroy_pipeline(TextPipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        release_com(pipeline->blend_state);
        release_com(pipeline->sampler);
        release_com(pipeline->vertex_buffer);
        release_com(pipeline->input_layout);
        release_com(pipeline->pixel_shader);
        release_com(pipeline->vertex_shader);
    }

    [[nodiscard]] auto create_pipeline(ID3D11Device* device, TextPipeline* pipeline) -> bool {
        constexpr StrRef SHADER_SOURCE =
            "Texture2D g_text_texture : register(t0);\n"
            "SamplerState g_text_sampler : register(s0);\n"
            "struct VSInput\n"
            "{\n"
            "    float2 position : POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "struct PSInput\n"
            "{\n"
            "    float4 position : SV_POSITION;\n"
            "    float2 uv : TEXCOORD0;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "PSInput vs_main(VSInput input)\n"
            "{\n"
            "    PSInput output;\n"
            "    output.position = float4(input.position, 0.0f, 1.0f);\n"
            "    output.uv = input.uv;\n"
            "    output.color = input.color;\n"
            "    return output;\n"
            "}\n"
            "float4 ps_main(PSInput input) : SV_Target\n"
            "{\n"
            "    float4 sample_value = g_text_texture.Sample(g_text_sampler, input.uv);\n"
            "    return float4(input.color.rgb * sample_value.rgb, input.color.a * "
            "sample_value.a);\n"
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
             static_cast<UINT>(offsetof(TextVertex, position)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0u},
            {"TEXCOORD",
             0u,
             DXGI_FORMAT_R32G32_FLOAT,
             0u,
             static_cast<UINT>(offsetof(TextVertex, uv)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0u},
            {"COLOR",
             0u,
             DXGI_FORMAT_R32G32B32A32_FLOAT,
             0u,
             static_cast<UINT>(offsetof(TextVertex, color)),
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

        D3D11_BUFFER_DESC vertex_desc = {};
        vertex_desc.ByteWidth = sizeof(TextVertex) * 6u;
        vertex_desc.Usage = D3D11_USAGE_DYNAMIC;
        vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(&vertex_desc, nullptr, &pipeline->vertex_buffer);
        if (FAILED(hr)) {
            return false;
        }

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sampler_desc, &pipeline->sampler);
        if (FAILED(hr)) {
            return false;
        }

        D3D11_BLEND_DESC blend_desc = {};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device->CreateBlendState(&blend_desc, &pipeline->blend_state);
        return SUCCEEDED(hr);
    }

    [[nodiscard]] auto create_text_texture(ID3D11Device* device,
                                           gui::font_cache::TextRun const& run,
                                           ID3D11ShaderResourceView** out_view) -> bool {
        if (device == nullptr || out_view == nullptr || run.rgba_pixels == nullptr ||
            run.size.width == 0u || run.size.height == 0u) {
            return false;
        }

        ID3D11Texture2D* texture = nullptr;

        D3D11_TEXTURE2D_DESC texture_desc = {};
        texture_desc.Width = run.size.width;
        texture_desc.Height = run.size.height;
        texture_desc.MipLevels = 1u;
        texture_desc.ArraySize = 1u;
        texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture_desc.SampleDesc.Count = 1u;
        texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
        texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA texture_data = {};
        texture_data.pSysMem = run.rgba_pixels;
        texture_data.SysMemPitch = run.stride;

        HRESULT hr = device->CreateTexture2D(&texture_desc, &texture_data, &texture);
        if (FAILED(hr) || texture == nullptr) {
            release_com(texture);
            return false;
        }

        hr = device->CreateShaderResourceView(texture, nullptr, out_view);
        release_com(texture);
        return SUCCEEDED(hr) && *out_view != nullptr;
    }

    [[nodiscard]] auto pixel_to_ndc_x(float value, float width) -> float {
        return ((value / width) * 2.0f) - 1.0f;
    }

    [[nodiscard]] auto pixel_to_ndc_y(float value, float height) -> float {
        return 1.0f - ((value / height) * 2.0f);
    }

    [[nodiscard]] auto render_text_command(ID3D11Device* device,
                                           ID3D11DeviceContext* device_context,
                                           TextPipeline const& pipeline,
                                           gui::render::SizeU32 window_size,
                                           gui::draw::TextCommand const& command) -> bool {
        gui::font_cache::TextRun const& run = command.run;
        if (run.rgba_pixels == nullptr || run.size.width == 0u || run.size.height == 0u) {
            return true;
        }

        ID3D11ShaderResourceView* texture_view = nullptr;
        if (!create_text_texture(device, run, &texture_view)) {
            return false;
        }

        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        float const x0 = command.position.x;
        float const y0 = command.position.y;
        float const x1 = x0 + static_cast<float>(run.size.width);
        float const y1 = y0 + static_cast<float>(run.size.height);
        gui::draw::Color const color = command.style.color;

        TextVertex const vertices[] = {
            {{pixel_to_ndc_x(x0, window_width), pixel_to_ndc_y(y0, window_height)},
             {0.0f, 0.0f},
             {color.r, color.g, color.b, color.a}},
            {{pixel_to_ndc_x(x1, window_width), pixel_to_ndc_y(y0, window_height)},
             {1.0f, 0.0f},
             {color.r, color.g, color.b, color.a}},
            {{pixel_to_ndc_x(x1, window_width), pixel_to_ndc_y(y1, window_height)},
             {1.0f, 1.0f},
             {color.r, color.g, color.b, color.a}},
            {{pixel_to_ndc_x(x0, window_width), pixel_to_ndc_y(y0, window_height)},
             {0.0f, 0.0f},
             {color.r, color.g, color.b, color.a}},
            {{pixel_to_ndc_x(x1, window_width), pixel_to_ndc_y(y1, window_height)},
             {1.0f, 1.0f},
             {color.r, color.g, color.b, color.a}},
            {{pixel_to_ndc_x(x0, window_width), pixel_to_ndc_y(y1, window_height)},
             {0.0f, 1.0f},
             {color.r, color.g, color.b, color.a}},
        };

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr =
            device_context->Map(pipeline.vertex_buffer, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(hr)) {
            release_com(texture_view);
            return false;
        }
        std::memcpy(mapped.pData, vertices, sizeof(vertices));
        device_context->Unmap(pipeline.vertex_buffer, 0u);

        UINT const stride = sizeof(TextVertex);
        UINT const offset = 0u;
        float blend_factor[] = {0.0f, 0.0f, 0.0f, 0.0f};

        device_context->IASetInputLayout(pipeline.input_layout);
        device_context->IASetVertexBuffers(0u, 1u, &pipeline.vertex_buffer, &stride, &offset);
        device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device_context->VSSetShader(pipeline.vertex_shader, nullptr, 0u);
        device_context->PSSetShader(pipeline.pixel_shader, nullptr, 0u);
        device_context->PSSetSamplers(0u, 1u, &pipeline.sampler);
        device_context->PSSetShaderResources(0u, 1u, &texture_view);
        device_context->OMSetBlendState(pipeline.blend_state, blend_factor, 0xffffffffu);
        device_context->Draw(6u, 0u);

        ID3D11ShaderResourceView* null_view = nullptr;
        device_context->PSSetShaderResources(0u, 1u, &null_view);
        release_com(texture_view);
        return true;
    }

    [[nodiscard]] auto render_text_commands(gui::render::Context render_context,
                                            gui::render::Window render_window,
                                            TextPipeline const& pipeline,
                                            gui::draw::Context draw_context) -> bool {
        ID3D11Device* const device =
            static_cast<ID3D11Device*>(gui::render::native_device(render_context));
        ID3D11DeviceContext* const device_context =
            static_cast<ID3D11DeviceContext*>(gui::render::native_device_context(render_context));
        gui::render::SizeU32 const window_size = gui::render::window_size(render_window);

        if (device == nullptr || device_context == nullptr || window_size.width == 0u ||
            window_size.height == 0u) {
            return false;
        }

        size_t const command_count = gui::draw::text_command_count(draw_context);
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::TextCommand const* const command =
                gui::draw::text_command(draw_context, index);
            if (command == nullptr ||
                !render_text_command(device, device_context, pipeline, window_size, *command)) {
                return false;
            }
        }

        return true;
    }

    auto destroy_text_state(TextState* text_state) -> void {
        if (text_state == nullptr) {
            return;
        }

        if (gui::draw::context_valid(text_state->draw_context)) {
            gui::draw::destroy_context(text_state->draw_context);
        }
        if (gui::font_cache::cache_valid(text_state->cache)) {
            gui::font_cache::destroy_cache(text_state->cache);
        }
        if (gui::font_provider::context_valid(text_state->provider)) {
            gui::font_provider::destroy_context(text_state->provider);
        }
        text_state->font = {};
    }

    [[nodiscard]] auto create_text_state(Arena& arena, TextState* text_state) -> bool {
        gui::font_provider::Result font_result =
            gui::font_provider::create_context(arena, {}, text_state->provider);
        if (gui::font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        gui::font_cache::create_cache(arena, text_state->provider, {}, text_state->cache);

        gui::font_cache::open_system_font(text_state->cache, "Segoe UI", text_state->font);

        gui::draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = text_state->cache;
        gui::draw::create_context(arena, draw_desc, text_state->draw_context);

        return true;
    }

    auto build_text_commands(TextState* text_state) -> void {
        gui::draw::begin_frame(text_state->draw_context);

        gui::draw::TextStyle title = {};
        title.font = text_state->font;
        title.size = 42.0f;
        title.color = {0.94f, 0.97f, 1.0f, 1.0f};

        gui::draw::TextStyle accent = title;
        accent.color = {0.25f, 0.78f, 1.0f, 1.0f};

        gui::draw::TextStyle body = {};
        body.font = text_state->font;
        body.size = 22.0f;
        body.color = {0.70f, 0.84f, 0.90f, 1.0f};

        gui::draw::TextStyle caption = {};
        caption.font = text_state->font;
        caption.size = 18.0f;
        caption.color = {0.58f, 0.98f, 0.64f, 1.0f};

        float title_advance = 0.0f;
        gui::draw::draw_text(
            text_state->draw_context, {72.0f, 72.0f}, title, "Text rendering ", &title_advance);

        gui::draw::draw_text(
            text_state->draw_context, {72.0f + title_advance, 72.0f}, accent, "testbed", nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 150.0f},
                             body,
                             "DirectWrite rasterization through font_provider",
                             nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 194.0f},
                             body,
                             "font_cache owns cached RGBA text runs",
                             nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {74.0f, 252.0f},
                             caption,
                             "draw records text commands, then this testbed submits textured quads",
                             nullptr);

        gui::draw::end_frame(text_state->draw_context);
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
                                          L"gui_framework text rendering testbed",
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

    Arena app_arena = {};
    app_arena.init();

    gui::render::Context render_context = {};
    gui::render::ContextDesc context_desc = {};
    context_desc.backend = gui::render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    gui::render::Result render_result =
        gui::render::create_context(app_arena, context_desc, render_context);
    if (gui::render::result_failed(render_result)) {
        log_render_result("render::create_context", render_result);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    gui::render::Window render_window = {};
    gui::render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = gui::render::PresentMode::VSYNC;

    render_result =
        gui::render::create_window(app_arena, render_context, window_desc, render_window);
    if (gui::render::result_failed(render_result)) {
        log_render_result("render::create_window", render_result);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    ID3D11Device* const device =
        static_cast<ID3D11Device*>(gui::render::native_device(render_context));
    TextPipeline pipeline = {};
    TextState text_state = {};

    if (device == nullptr || !create_pipeline(device, &pipeline) ||
        !create_text_state(app_arena, &text_state)) {
        fmt::eprintf("failed to initialize text rendering testbed\n");
        destroy_text_state(&text_state);
        destroy_pipeline(&pipeline);
        gui::render::destroy_window(render_window);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

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
            render_result =
                gui::render::resize_window(render_context, render_window, app_state.pending_size);
            if (gui::render::result_failed(render_result)) {
                log_render_result("render::resize_window", render_result);
                app_state.running = false;
                continue;
            }
            app_state.resize_pending = false;
        }

        render_result = gui::render::begin_frame(render_context);
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::begin_frame", render_result);
            break;
        }

        build_text_commands(&text_state);

        gui::render::RenderPassDesc pass_desc = {};
        pass_desc.color.window = render_window;
        pass_desc.color.clear_color = {0.025f, 0.045f, 0.055f, 1.0f};

        render_result = gui::render::begin_render_pass(render_context, pass_desc);
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::begin_render_pass", render_result);
            break;
        }

        bool const rendered =
            render_text_commands(render_context, render_window, pipeline, text_state.draw_context);
        gui::render::end_render_pass(render_context);
        if (!rendered) {
            fmt::eprintf("failed to render text commands\n");
            break;
        }

        render_result = gui::render::present_window(render_window);
        if (render_result == gui::render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::present_window", render_result);
            break;
        }
    }

    destroy_text_state(&text_state);
    destroy_pipeline(&pipeline);
    gui::render::destroy_window(render_window);
    gui::render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
