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
#include <cmath>
#include <cstddef>
#include <cstdint>
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
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Buffer vertex_buffer = {};
        gui::render::Buffer transform_constants = {};
        gui::render::Buffer tint_constants = {};
        gui::render::BindGroup transform_bind_group = {};
        gui::render::BindGroup tint_bind_group = {};
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        gui::render::SizeU32 pending_size = {};
    };

    AppState* global_app_state = nullptr;

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    auto log_result(char const* operation, gui::render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, gui::render::result_name(result));
    }

    auto destroy_pipeline(gui::render::Context context, TrianglePipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        if (gui::render::bind_group_valid(pipeline->tint_bind_group)) {
            gui::render::destroy_bind_group(context, pipeline->tint_bind_group);
        }
        if (gui::render::bind_group_valid(pipeline->transform_bind_group)) {
            gui::render::destroy_bind_group(context, pipeline->transform_bind_group);
        }
        if (gui::render::buffer_valid(pipeline->tint_constants)) {
            gui::render::destroy_buffer(context, pipeline->tint_constants);
        }
        if (gui::render::buffer_valid(pipeline->transform_constants)) {
            gui::render::destroy_buffer(context, pipeline->transform_constants);
        }
        if (gui::render::buffer_valid(pipeline->vertex_buffer)) {
            gui::render::destroy_buffer(context, pipeline->vertex_buffer);
        }
        if (gui::render::pipeline_valid(pipeline->pipeline)) {
            gui::render::destroy_pipeline(context, pipeline->pipeline);
        }
        if (gui::render::shader_valid(pipeline->pixel_shader)) {
            gui::render::destroy_shader(context, pipeline->pixel_shader);
        }
        if (gui::render::shader_valid(pipeline->vertex_shader)) {
            gui::render::destroy_shader(context, pipeline->vertex_shader);
        }
    }

    [[nodiscard]] auto create_pipeline(Arena& arena,
                                       gui::render::Context render_context,
                                       TrianglePipeline* pipeline) -> bool {
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

        gui::render::ShaderSourceDesc shader_desc = {};
        shader_desc.source = SHADER_SOURCE;
        shader_desc.stage = gui::render::ShaderStage::VERTEX;
        shader_desc.entry_point = "vs_main";

        gui::render::Result result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->vertex_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        shader_desc.stage = gui::render::ShaderStage::PIXEL;
        shader_desc.entry_point = "ps_main";

        result = gui::render::create_shader_from_source(
            arena, render_context, shader_desc, pipeline->pixel_shader);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::VertexAttributeDesc input_attributes[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(Vertex, position)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_3,
                0u,
                static_cast<uint32_t>(offsetof(Vertex, color)),
            },
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = pipeline->vertex_shader;
        pipeline_desc.pixel_shader = pipeline->pixel_shader;
        pipeline_desc.vertex_attributes = input_attributes;
        pipeline_desc.vertex_attribute_count =
            sizeof(input_attributes) / sizeof(input_attributes[0u]);

        result =
            gui::render::create_pipeline(arena, render_context, pipeline_desc, pipeline->pipeline);
        if (gui::render::result_failed(result)) {
            return false;
        }

        Vertex const vertices[] = {
            {{0.0f, 0.62f}, {1.0f, 0.15f, 0.05f}},
            {{0.55f, -0.42f}, {0.0f, 0.9f, 0.25f}},
            {{-0.55f, -0.42f}, {0.1f, 0.35f, 1.0f}},
        };

        gui::render::BufferDesc vertex_desc = {};
        vertex_desc.byte_size = sizeof(vertices);
        vertex_desc.initial_data = vertices;

        result = gui::render::create_buffer(render_context, vertex_desc, pipeline->vertex_buffer);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::BufferDesc constant_desc = {};
        constant_desc.binding = gui::render::BufferBinding::UNIFORM;
        constant_desc.usage = gui::render::BufferUsage::DYNAMIC;
        constant_desc.byte_size = sizeof(TransformConstants);
        result = gui::render::create_buffer(
            render_context, constant_desc, pipeline->transform_constants);
        if (gui::render::result_failed(result)) {
            return false;
        }

        constant_desc.byte_size = sizeof(TintConstants);
        result =
            gui::render::create_buffer(render_context, constant_desc, pipeline->tint_constants);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::BindGroupBufferBinding transform_binding = {};
        transform_binding.stage = gui::render::ShaderStage::VERTEX;
        transform_binding.slot = 0u;
        transform_binding.buffer = pipeline->transform_constants;

        gui::render::BindGroupDesc bind_group_desc = {};
        bind_group_desc.slot = gui::render::BindGroupSlot::DRAW;
        bind_group_desc.buffers = &transform_binding;
        bind_group_desc.buffer_count = 1u;

        result = gui::render::create_bind_group(
            arena, render_context, bind_group_desc, pipeline->transform_bind_group);
        if (gui::render::result_failed(result)) {
            return false;
        }

        gui::render::BindGroupBufferBinding tint_binding = {};
        tint_binding.stage = gui::render::ShaderStage::PIXEL;
        tint_binding.slot = 0u;
        tint_binding.buffer = pipeline->tint_constants;

        bind_group_desc.slot = gui::render::BindGroupSlot::MATERIAL;
        bind_group_desc.buffers = &tint_binding;

        result = gui::render::create_bind_group(
            arena, render_context, bind_group_desc, pipeline->tint_bind_group);
        return gui::render::result_succeeded(result);
    }

    auto render_triangle(gui::render::Context context,
                         TrianglePipeline const& pipeline,
                         float time_seconds) -> void {
        TransformConstants transform = {};
        transform.offset[0] = 0.15f * static_cast<float>(std::sin(time_seconds));
        transform.offset[1] = 0.08f * static_cast<float>(std::cos(time_seconds * 0.75f));

        TintConstants tint = {};
        tint.color_scale[0] = 0.95f;
        tint.color_scale[1] =
            0.75f + (0.25f * static_cast<float>(std::sin(time_seconds * 1.4f) * 0.5f + 0.5f));
        tint.color_scale[2] = 1.0f;
        tint.color_scale[3] = 1.0f;

        gui::render::update_buffer(
            context, pipeline.transform_constants, &transform, sizeof(transform));
        gui::render::update_buffer(context, pipeline.tint_constants, &tint, sizeof(tint));

        gui::render::VertexBufferBinding vertex_buffer = {};
        vertex_buffer.buffer = pipeline.vertex_buffer;
        vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(Vertex));

        gui::render::BindGroup bind_groups[] = {
            pipeline.transform_bind_group,
            pipeline.tint_bind_group,
        };

        gui::render::DrawDesc draw_desc = {};
        draw_desc.pipeline = pipeline.pipeline;
        draw_desc.vertex_buffers = &vertex_buffer;
        draw_desc.vertex_buffer_count = 1u;
        draw_desc.bind_groups = bind_groups;
        draw_desc.bind_group_count = sizeof(bind_groups) / sizeof(bind_groups[0u]);
        draw_desc.vertex_count = 3u;

        gui::render::draw(context, draw_desc);
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

    Arena app_arena = {};
    app_arena.init();

    gui::render::Context render_context = {};
    gui::render::ContextDesc context_desc = {};
    context_desc.backend = gui::render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    gui::render::Result result =
        gui::render::create_context(app_arena, context_desc, render_context);
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

    result = gui::render::create_window(app_arena, render_context, window_desc, render_window);
    if (gui::render::result_failed(result)) {
        log_result("render::create_window", result);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    TrianglePipeline pipeline = {};

    if (!create_pipeline(app_arena, render_context, &pipeline)) {
        fmt::eprintf("failed to create render triangle pipeline\n");
        destroy_pipeline(render_context, &pipeline);
        gui::render::destroy_window(render_window);
        gui::render::destroy_context(render_context);
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

        gui::render::begin_frame(render_context);

        gui::render::RenderPassDesc pass_desc = {};
        pass_desc.color.window = render_window;
        pass_desc.color.clear_color = {0.03f, 0.08f, 0.11f, 1.0f};

        result = gui::render::begin_render_pass(render_context, pass_desc);
        if (gui::render::result_failed(result)) {
            log_result("render::begin_render_pass", result);
            break;
        }

        uint64_t const elapsed_ticks = GetTickCount64() - start_ticks;
        float const time_seconds = static_cast<float>(elapsed_ticks) * 0.001f;
        render_triangle(render_context, pipeline, time_seconds);
        gui::render::end_render_pass(render_context);

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

    destroy_pipeline(render_context, &pipeline);
    gui::render::destroy_window(render_window);
    gui::render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
