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
        gui::render::Shader vertex_shader = {};
        gui::render::Shader pixel_shader = {};
        gui::render::Pipeline pipeline = {};
        gui::render::Sampler sampler = {};
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

    auto destroy_pipeline(gui::render::Context context, TextPipeline* pipeline) -> void {
        if (pipeline == nullptr) {
            return;
        }

        if (gui::render::sampler_valid(pipeline->sampler)) {
            gui::render::destroy_sampler(context, pipeline->sampler);
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
                                       TextPipeline* pipeline) -> bool {
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

        gui::render::VertexAttributeDesc input_elements[] = {
            {
                "POSITION",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, position)),
            },
            {
                "TEXCOORD",
                0u,
                gui::render::VertexFormat::FLOAT32_2,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, uv)),
            },
            {
                "COLOR",
                0u,
                gui::render::VertexFormat::FLOAT32_4,
                0u,
                static_cast<uint32_t>(offsetof(TextVertex, color)),
            },
        };

        gui::render::PipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = pipeline->vertex_shader;
        pipeline_desc.pixel_shader = pipeline->pixel_shader;
        pipeline_desc.vertex_attributes = input_elements;
        pipeline_desc.vertex_attribute_count = sizeof(input_elements) / sizeof(input_elements[0u]);
        pipeline_desc.blend_mode = gui::render::BlendMode::ALPHA;

        result =
            gui::render::create_pipeline(arena, render_context, pipeline_desc, pipeline->pipeline);
        if (gui::render::result_failed(result)) {
            return false;
        }

        result = gui::render::create_sampler(render_context, pipeline->sampler);
        return gui::render::result_succeeded(result);
    }

    [[nodiscard]] auto create_text_texture(gui::render::Context context,
                                           gui::font_cache::TextRun const& run,
                                           gui::render::Texture& out_texture) -> bool {
        ASSERT(run.rgba_pixels != nullptr);
        ASSERT(run.size.width != 0u);
        ASSERT(run.size.height != 0u);

        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = {run.size.width, run.size.height};
        texture_desc.bytes_per_row = run.stride;
        texture_desc.rgba_pixels = run.rgba_pixels;

        gui::render::Result const result =
            gui::render::create_texture(context, texture_desc, out_texture);
        return gui::render::result_succeeded(result);
    }

    [[nodiscard]] auto pixel_to_ndc_x(float value, float width) -> float {
        return ((value / width) * 2.0f) - 1.0f;
    }

    [[nodiscard]] auto pixel_to_ndc_y(float value, float height) -> float {
        return 1.0f - ((value / height) * 2.0f);
    }

    [[nodiscard]] auto text_command_visible(gui::draw::TextCommand const& command) -> bool {
        gui::font_cache::TextRun const& run = command.run;
        return run.rgba_pixels != nullptr && run.size.width != 0u && run.size.height != 0u;
    }

    auto write_text_vertices(TextVertex* vertices,
                             gui::render::SizeU32 window_size,
                             gui::draw::TextCommand const& command) -> void {
        gui::font_cache::TextRun const& run = command.run;
        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        float const x0 = command.position.x;
        float const y0 = command.position.y;
        float const x1 = x0 + static_cast<float>(run.size.width);
        float const y1 = y0 + static_cast<float>(run.size.height);
        gui::draw::Color const color = command.style.color;

        vertices[0u] = {{pixel_to_ndc_x(x0, window_width), pixel_to_ndc_y(y0, window_height)},
                        {0.0f, 0.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[1u] = {{pixel_to_ndc_x(x1, window_width), pixel_to_ndc_y(y0, window_height)},
                        {1.0f, 0.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[2u] = {{pixel_to_ndc_x(x1, window_width), pixel_to_ndc_y(y1, window_height)},
                        {1.0f, 1.0f},
                        {color.r, color.g, color.b, color.a}};
        vertices[3u] = vertices[0u];
        vertices[4u] = vertices[2u];
        vertices[5u] = {{pixel_to_ndc_x(x0, window_width), pixel_to_ndc_y(y1, window_height)},
                        {0.0f, 1.0f},
                        {color.r, color.g, color.b, color.a}};
    }

    auto render_text_commands(gui::render::Context render_context,
                              gui::render::Window render_window,
                              TextPipeline const& pipeline,
                              gui::draw::Context draw_context) -> void {
        gui::render::SizeU32 const window_size = gui::render::window_size(render_window);

        ASSERT(window_size.width != 0u);
        ASSERT(window_size.height != 0u);

        size_t const command_count = gui::draw::text_command_count(draw_context);
        if (command_count == 0u) {
            return;
        }

        gui::render::FrameBufferSlice const upload =
            gui::render::allocate_frame_buffer(render_context,
                                               gui::render::BufferBinding::VERTEX,
                                               command_count * 6u * sizeof(TextVertex),
                                               alignof(TextVertex));

        TextVertex* const vertices = static_cast<TextVertex*>(upload.data);
        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::TextCommand const* const command =
                gui::draw::text_command(draw_context, index);
            ASSERT(command != nullptr);
            if (text_command_visible(*command)) {
                write_text_vertices(vertices + (index * 6u), window_size, *command);
            }
        }

        gui::render::commit_frame_uploads(render_context);

        gui::render::VertexBufferBinding vertex_buffer = {};
        vertex_buffer.buffer = upload.buffer;
        vertex_buffer.byte_stride = static_cast<uint32_t>(sizeof(TextVertex));
        vertex_buffer.byte_offset = static_cast<uint32_t>(upload.byte_offset);

        for (size_t index = 0u; index < command_count; ++index) {
            gui::draw::TextCommand const* const command =
                gui::draw::text_command(draw_context, index);
            ASSERT(command != nullptr);
            if (!text_command_visible(*command)) {
                continue;
            }

            gui::render::Texture texture = {};
            bool const texture_created = create_text_texture(render_context, command->run, texture);
            ASSERT(texture_created);

            gui::render::BindGroupTextureBinding texture_binding = {};
            texture_binding.stage = gui::render::ShaderStage::PIXEL;
            texture_binding.slot = 0u;
            texture_binding.texture = texture;

            gui::render::BindGroupSamplerBinding sampler_binding = {};
            sampler_binding.stage = gui::render::ShaderStage::PIXEL;
            sampler_binding.slot = 0u;
            sampler_binding.sampler = pipeline.sampler;

            ArenaTemp temp = begin_thread_temp_arena();
            gui::render::BindGroup bind_group = {};
            gui::render::BindGroupDesc bind_group_desc = {};
            bind_group_desc.textures = &texture_binding;
            bind_group_desc.texture_count = 1u;
            bind_group_desc.samplers = &sampler_binding;
            bind_group_desc.sampler_count = 1u;

            gui::render::Result const bind_result = gui::render::create_bind_group(
                *temp.arena(), render_context, bind_group_desc, bind_group);
            ASSERT(gui::render::result_succeeded(bind_result));
            if (gui::render::result_succeeded(bind_result)) {
                gui::render::DrawDesc draw_desc = {};
                draw_desc.pipeline = pipeline.pipeline;
                draw_desc.vertex_buffers = &vertex_buffer;
                draw_desc.vertex_buffer_count = 1u;
                draw_desc.bind_groups = &bind_group;
                draw_desc.bind_group_count = 1u;
                draw_desc.vertex_count = 6u;
                draw_desc.first_vertex = static_cast<uint32_t>(index * 6u);

                gui::render::draw(render_context, draw_desc);
                gui::render::destroy_bind_group(render_context, bind_group);
            }

            gui::render::destroy_texture(render_context, texture);
        }
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

    TextPipeline pipeline = {};
    TextState text_state = {};

    if (!create_pipeline(app_arena, render_context, &pipeline) ||
        !create_text_state(app_arena, &text_state)) {
        fmt::eprintf("failed to initialize text rendering testbed\n");
        destroy_text_state(&text_state);
        destroy_pipeline(render_context, &pipeline);
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

        gui::render::begin_frame(render_context);

        build_text_commands(&text_state);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.025f, 0.045f, 0.055f, 1.0f};

        render_result = gui::render::begin_render_pass(render_context, pass_desc);
        if (gui::render::result_failed(render_result)) {
            log_render_result("render::begin_render_pass", render_result);
            break;
        }

        render_text_commands(render_context, render_window, pipeline, text_state.draw_context);
        gui::render::end_render_pass(render_context);

        render_result = gui::render::present_window(render_context, render_window);
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
