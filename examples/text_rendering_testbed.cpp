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

    auto build_draw_commands(TextState* text_state) -> void {
        gui::draw::begin_frame(text_state->draw_context);

        gui::draw::TextStyle title = {};
        title.font = text_state->font;
        title.size = 56.0f;
        title.color = {0.94f, 0.97f, 1.0f, 1.0f};

        gui::draw::TextStyle body = {};
        body.font = text_state->font;
        body.size = 24.0f;
        body.color = {0.76f, 0.86f, 0.90f, 1.0f};

        gui::draw::TextStyle lead = body;
        lead.size = 32.0f;
        lead.color = {0.98f, 0.78f, 0.32f, 1.0f};

        gui::draw::TextStyle small = body;
        small.size = 16.0f;
        small.color = {0.58f, 0.68f, 0.72f, 1.0f};

        gui::draw::TextStyle accent = body;
        accent.color = {0.28f, 0.78f, 1.0f, 1.0f};

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{56.0f, 48.0f}, {1224.0f, 672.0f}},
                                    {0.035f, 0.060f, 0.070f, 1.0f},
                                    10.0f);
        gui::draw::draw_rect(text_state->draw_context,
                             {{56.0f, 48.0f}, {1224.0f, 672.0f}},
                             {0.18f, 0.27f, 0.30f, 1.0f},
                             1.0f,
                             10.0f);

        float title_advance = 0.0f;
        gui::draw::draw_text(
            text_state->draw_context, {88.0f, 86.0f}, title, "Text rendering", &title_advance);
        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{88.0f, 154.0f}, {88.0f + title_advance, 158.0f}},
                                    {0.28f, 0.78f, 1.0f, 0.86f},
                                    0.0f);

        gui::draw::draw_text(text_state->draw_context,
                             {88.0f, 196.0f},
                             lead,
                             "DirectWrite glyph rasterization through font_cache.",
                             nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {88.0f, 258.0f},
                             body,
                             "The quick brown fox jumps over the lazy dog.",
                             nullptr);
        gui::draw::draw_text(text_state->draw_context,
                             {88.0f, 302.0f},
                             body,
                             "0123456789  +-*/=  ()[]{}  .,;:!?",
                             nullptr);
        gui::draw::draw_text(text_state->draw_context,
                             {88.0f, 346.0f},
                             accent,
                             "Colored text uses the same cached text runs.",
                             nullptr);

        gui::draw::draw_rect_filled(text_state->draw_context,
                                    {{84.0f, 418.0f}, {1196.0f, 419.0f}},
                                    {0.18f, 0.27f, 0.30f, 1.0f},
                                    0.0f);

        gui::draw::TextStyle size_18 = body;
        size_18.size = 18.0f;
        gui::draw::TextStyle size_28 = body;
        size_28.size = 28.0f;
        gui::draw::TextStyle size_40 = body;
        size_40.size = 40.0f;

        gui::draw::draw_text(
            text_state->draw_context, {88.0f, 462.0f}, size_18, "18 px sample", nullptr);
        gui::draw::draw_text(
            text_state->draw_context, {88.0f, 508.0f}, size_28, "28 px sample", nullptr);
        gui::draw::draw_text(
            text_state->draw_context, {88.0f, 566.0f}, size_40, "40 px sample", nullptr);

        gui::draw::draw_text(text_state->draw_context,
                             {596.0f, 466.0f},
                             small,
                             "Small text remains stable without animated draw effects.",
                             nullptr);
        gui::draw::draw_text(text_state->draw_context,
                             {596.0f, 506.0f},
                             small,
                             "font_provider opens Segoe UI and rasterizes RGBA glyph runs.",
                             nullptr);
        gui::draw::draw_text(text_state->draw_context,
                             {596.0f, 546.0f},
                             small,
                             "gui::draw records text commands for the renderer.",
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

    if (!create_text_state(app_arena, &text_state)) {
        fmt::eprintf("failed to initialize text rendering testbed\n");
        destroy_text_state(&text_state);
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

    destroy_text_state(&text_state);
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
