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
#include <cstdint>
#include <draw/draw.h>
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_text_rendering_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;
    constexpr gui::render::SizeU32 SAMPLE_TEXTURE_SIZE = {2u, 2u};
    constexpr uint8_t SAMPLE_TEXTURE_RGBA[] = {
        245u, 70u, 52u, 255u, 34u, 114u, 245u, 255u, 43u, 194u, 105u, 255u, 172u, 82u, 229u, 255u};

    struct TextState {
        gui::font_provider::Context provider = {};
        gui::font_cache::Cache cache = {};
        gui::font_cache::Font font = {};
        gui::draw::Context draw_context = {};
        gui::render::Texture sample_texture = {};
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

    [[nodiscard]] auto create_rgba_texture(gui::render::Context context,
                                           gui::render::SizeU32 size,
                                           uint8_t const* pixels,
                                           gui::render::Texture& out_texture) -> bool {
        gui::render::TextureDesc texture_desc = {};
        texture_desc.size = size;
        texture_desc.bytes_per_row = size.width * 4u;
        texture_desc.rgba_pixels = pixels;

        gui::render::Result const result =
            gui::render::create_texture(context, texture_desc, out_texture);
        return gui::render::result_succeeded(result);
    }

    auto destroy_text_state(gui::render::Context render_context, TextState* text_state) -> void {
        if (text_state == nullptr) {
            return;
        }

        if (gui::render::texture_valid(text_state->sample_texture)) {
            gui::render::destroy_texture(render_context, text_state->sample_texture);
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

    [[nodiscard]] auto create_text_state(Arena& arena,
                                         gui::render::Context render_context,
                                         TextState* text_state) -> bool {
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

        return create_rgba_texture(
            render_context, SAMPLE_TEXTURE_SIZE, SAMPLE_TEXTURE_RGBA, text_state->sample_texture);
    }

    auto build_draw_commands(TextState* text_state) -> void {
        gui::draw::begin_frame(text_state->draw_context);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{64.0f, 330.0f}, {238.0f, 462.0f}},
                                    {0.94f, 0.26f, 0.20f, 0.86f},
                                    0.0f);
        gui::draw::draw_rect_filled_multicolor(text_state->draw_context,
                                               {{276.0f, 330.0f}, {468.0f, 462.0f}},
                                               {0.22f, 0.72f, 1.0f, 0.92f},
                                               {0.88f, 0.36f, 1.0f, 0.92f},
                                               {0.98f, 0.78f, 0.24f, 0.92f},
                                               {0.28f, 0.95f, 0.54f, 0.92f});
        gui::draw::draw_line(text_state->draw_context,
                             {524.0f, 352.0f},
                             {708.0f, 428.0f},
                             {0.94f, 0.97f, 1.0f, 1.0f},
                             5.0f);

        gui::draw::Vec2 const polyline[] = {{522.0f, 458.0f},
                                            {572.0f, 386.0f},
                                            {630.0f, 454.0f},
                                            {690.0f, 374.0f},
                                            {734.0f, 446.0f}};
        gui::draw::draw_polyline(
            text_state->draw_context, polyline, {0.25f, 0.78f, 1.0f, 0.95f}, 4.0f, false);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{784.0f, 330.0f}, {986.0f, 462.0f}},
                                    {0.21f, 0.52f, 0.88f, 0.62f},
                                    30.0f);
        gui::draw::draw_rect(text_state->draw_context,
                             {{784.0f, 330.0f}, {986.0f, 462.0f}},
                             {0.85f, 0.95f, 1.0f, 1.0f},
                             4.0f,
                             30.0f);
        gui::draw::draw_circle_filled(
            text_state->draw_context, {1092.0f, 396.0f}, 54.0f, {0.58f, 0.98f, 0.64f, 0.66f}, 32);
        gui::draw::draw_ellipse(text_state->draw_context,
                                {1092.0f, 396.0f},
                                {82.0f, 44.0f},
                                {0.94f, 0.97f, 1.0f, 1.0f},
                                4.0f,
                                32);
        gui::draw::draw_image(text_state->draw_context,
                              text_state->sample_texture,
                              {{784.0f, 506.0f}, {986.0f, 638.0f}},
                              {{0.0f, 0.0f}, {1.0f, 1.0f}},
                              {1.0f, 1.0f, 1.0f, 0.92f});

        gui::draw::path_line_to(text_state->draw_context, {92.0f, 566.0f});
        gui::draw::path_line_to(text_state->draw_context, {190.0f, 506.0f});
        gui::draw::path_line_to(text_state->draw_context, {248.0f, 620.0f});
        gui::draw::path_fill_convex(text_state->draw_context, {0.97f, 0.68f, 0.22f, 0.88f});

        gui::draw::path_line_to(text_state->draw_context, {324.0f, 610.0f});
        gui::draw::path_bezier_cubic_to(
            text_state->draw_context, {410.0f, 474.0f}, {526.0f, 690.0f}, {616.0f, 534.0f}, 24);
        gui::draw::path_stroke(text_state->draw_context, {1.0f, 1.0f, 1.0f, 0.9f}, false, 5.0f);

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

        gui::draw::TextStyle clip_text = caption;
        clip_text.color = {0.94f, 0.97f, 1.0f, 1.0f};

        auto draw_blend_sample = [](gui::draw::Context draw_context,
                                    float x,
                                    gui::draw::LayerBlendMode blend_mode) -> void {
            gui::draw::Rect const bounds = {{x, 146.0f}, {x + 72.0f, 238.0f}};
            gui::draw::draw_rect_filled(draw_context, bounds, {0.05f, 0.07f, 0.08f, 1.0f}, 8.0f);
            gui::draw::draw_rect_filled(draw_context,
                                        {{x + 8.0f, 154.0f}, {x + 52.0f, 214.0f}},
                                        {0.20f, 0.56f, 0.96f, 1.0f},
                                        6.0f);
            gui::draw::draw_rect_filled(draw_context,
                                        {{x + 28.0f, 172.0f}, {x + 64.0f, 230.0f}},
                                        {0.98f, 0.70f, 0.20f, 1.0f},
                                        6.0f);

            gui::draw::LayerDesc layer = {};
            layer.bounds = bounds;
            layer.blend_mode = blend_mode;
            gui::draw::push_layer(draw_context, layer);
            gui::draw::draw_rect_filled(draw_context,
                                        {{x + 18.0f, 162.0f}, {x + 62.0f, 222.0f}},
                                        {0.95f, 0.16f, 0.48f, 0.78f},
                                        6.0f);
            gui::draw::pop_layer(draw_context);
        };

        draw_blend_sample(text_state->draw_context, 510.0f, gui::draw::LayerBlendMode::NORMAL);
        draw_blend_sample(text_state->draw_context, 590.0f, gui::draw::LayerBlendMode::ADDITIVE);
        draw_blend_sample(text_state->draw_context, 670.0f, gui::draw::LayerBlendMode::MULTIPLY);
        draw_blend_sample(text_state->draw_context, 750.0f, gui::draw::LayerBlendMode::SCREEN);

        gui::draw::Rect const outer_clip = {{836.0f, 72.0f}, {1210.0f, 252.0f}};
        gui::draw::Rect const inner_clip = {{978.0f, 118.0f}, {1134.0f, 214.0f}};
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{820.0f, 58.0f}, {1224.0f, 266.0f}},
                                    {0.02f, 0.06f, 0.07f, 0.86f},
                                    0.0f);
        gui::draw::push_clip_rect(text_state->draw_context, outer_clip);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{784.0f, 86.0f}, {1260.0f, 158.0f}},
                                    {0.25f, 0.78f, 1.0f, 0.34f},
                                    0.0f);
        gui::draw::draw_text(text_state->draw_context,
                             {850.0f, 88.0f},
                             clip_text,
                             "outer clipped text runs past the right edge",
                             nullptr);
        gui::draw::push_clip_rect(text_state->draw_context, inner_clip);
        gui::draw::draw_rect_filled_multicolor(text_state->draw_context,
                                               {{926.0f, 100.0f}, {1186.0f, 232.0f}},
                                               {0.98f, 0.78f, 0.24f, 0.88f},
                                               {0.25f, 0.78f, 1.0f, 0.88f},
                                               {0.88f, 0.36f, 1.0f, 0.88f},
                                               {0.28f, 0.95f, 0.54f, 0.88f});
        gui::draw::draw_text(
            text_state->draw_context, {986.0f, 132.0f}, clip_text, "UNDER stripe clipped", nullptr);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{948.0f, 140.0f}, {1164.0f, 168.0f}},
                                    {0.02f, 0.04f, 0.05f, 0.78f},
                                    0.0f);
        gui::draw::draw_text(
            text_state->draw_context, {986.0f, 178.0f}, caption, "OVER stripe clipped", nullptr);
        gui::draw::pop_clip_rect(text_state->draw_context);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{864.0f, 200.0f}, {1188.0f, 244.0f}},
                                    {0.97f, 0.68f, 0.22f, 0.36f},
                                    0.0f);
        gui::draw::pop_clip_rect(text_state->draw_context);
        gui::draw::draw_rect(
            text_state->draw_context, outer_clip, {0.94f, 0.97f, 1.0f, 0.84f}, 2.0f, 0.0f);
        gui::draw::draw_rect(
            text_state->draw_context, inner_clip, {0.97f, 0.68f, 0.22f, 0.96f}, 2.0f, 0.0f);

        float title_advance = 0.0f;
        gui::draw::draw_text(
            text_state->draw_context, {72.0f, 72.0f}, title, "Text rendering ", &title_advance);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{72.0f, 126.0f}, {72.0f + title_advance + 140.0f, 130.0f}},
                                    {0.25f, 0.78f, 1.0f, 0.65f},
                                    0.0f);

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
                             "draw records primitive commands and text commands for gui::render",
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

    gui::draw::Renderer draw_renderer = {};
    TextState text_state = {};

    gui::draw::RendererDesc const renderer_desc = {};
    render_result =
        gui::draw::create_renderer(app_arena, render_context, renderer_desc, draw_renderer);
    if (gui::render::result_failed(render_result)) {
        log_render_result("draw::create_renderer", render_result);
        gui::render::destroy_window(render_window);
        gui::render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    if (!create_text_state(app_arena, render_context, &text_state)) {
        fmt::eprintf("failed to initialize text rendering testbed\n");
        destroy_text_state(render_context, &text_state);
        gui::draw::destroy_renderer(render_context, draw_renderer);
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

        build_draw_commands(&text_state);

        gui::render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.025f, 0.045f, 0.055f, 1.0f};

        render_result = gui::draw::render_commands_to_window(
            draw_renderer, render_context, pass_desc, text_state.draw_context);
        if (gui::render::result_failed(render_result)) {
            log_render_result("draw::render_commands_to_window", render_result);
            break;
        }

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

    destroy_text_state(render_context, &text_state);
    gui::draw::destroy_renderer(render_context, draw_renderer);
    gui::render::destroy_window(render_window);
    gui::render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
