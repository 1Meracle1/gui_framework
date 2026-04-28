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
#include <cmath>
#include <cstdint>
#include <draw/draw.h>
#include <draw/draw_renderer.h>
#include <render/render.h>
#include <windows.h>

namespace {

    namespace draw = gui::draw;
    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_render_effects_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 720u;

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
    };

    AppState* global_app_state = nullptr;

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    [[nodiscard]] auto wave(float time_seconds, float speed, float phase) -> float {
        return static_cast<float>(std::sin((time_seconds * speed) + phase));
    }

    [[nodiscard]] auto tile_rect(uint32_t column, uint32_t row) -> draw::Rect {
        float const x = 40.0f + (static_cast<float>(column) * 300.0f);
        float const y = 36.0f + (static_cast<float>(row) * 304.0f);
        return {{x, y}, {x + 260.0f, y + 248.0f}};
    }

    [[nodiscard]] auto inset(draw::Rect rect, float amount) -> draw::Rect {
        return {{rect.min.x + amount, rect.min.y + amount},
                {rect.max.x - amount, rect.max.y - amount}};
    }

    [[nodiscard]] auto offset(draw::Rect rect, draw::Vec2 amount) -> draw::Rect {
        return {{rect.min.x + amount.x, rect.min.y + amount.y},
                {rect.max.x + amount.x, rect.max.y + amount.y}};
    }

    auto draw_tile(draw::Context context, draw::Rect rect) -> void {
        draw::BoxStyle style = {};
        style.fill_color = {0.075f, 0.09f, 0.105f, 1.0f};
        style.border_color = {0.28f, 0.34f, 0.36f, 1.0f};
        style.border_thickness = 1.0f;
        style.radius = 10.0f;
        style.softness = 1.0f;
        draw::draw_rect_styled(context, rect, style);
    }

    auto draw_alpha_overlap(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::Rect const rect = inset(tile, 46.0f);
        float const red_x = wave(time_seconds, 0.46f, 0.0f) * 10.0f;
        float const blue_y = wave(time_seconds, 0.38f, 1.8f) * 8.0f;
        float const gold_x = wave(time_seconds, 0.34f, 3.4f) * 7.0f;
        float const gold_y = wave(time_seconds, 0.30f, 2.6f) * 5.0f;
        draw::draw_rect_filled(
            context,
            {{rect.min.x + red_x, rect.min.y}, {rect.max.x - 34.0f + red_x, rect.max.y}},
            {0.95f, 0.22f, 0.18f, 0.58f},
            18.0f);
        draw::draw_rect_filled(context,
                               {{rect.min.x + 54.0f, rect.min.y + 34.0f + blue_y},
                                {rect.max.x, rect.max.y - 22.0f + blue_y}},
                               {0.12f, 0.62f, 1.0f, 0.58f},
                               18.0f);
        draw::draw_rect_filled(context,
                               {{rect.min.x + 96.0f + gold_x, rect.min.y + 72.0f + gold_y},
                                {rect.max.x - 22.0f + gold_x, rect.max.y + 10.0f + gold_y}},
                               {0.98f, 0.74f, 0.18f, 0.58f},
                               18.0f);
    }

    auto draw_group_opacity(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::LayerDesc layer = {};
        layer.bounds = inset(tile, 34.0f);
        layer.opacity = 0.56f + (wave(time_seconds, 0.28f, 1.2f) * 0.08f);
        float const slide = wave(time_seconds, 0.36f, 0.6f) * 8.0f;
        draw::push_layer(context, layer);
        draw::draw_rect_filled(context,
                               {{layer.bounds.min.x + 12.0f + slide, layer.bounds.min.y + 16.0f},
                                {layer.bounds.min.x + 136.0f + slide, layer.bounds.max.y - 16.0f}},
                               {0.95f, 0.22f, 0.18f, 1.0f},
                               20.0f);
        draw::draw_rect_filled(context,
                               {{layer.bounds.min.x + 74.0f - slide, layer.bounds.min.y + 44.0f},
                                {layer.bounds.max.x - 10.0f - slide, layer.bounds.max.y - 8.0f}},
                               {0.12f, 0.62f, 1.0f, 1.0f},
                               20.0f);
        draw::pop_layer(context);
    }

    auto draw_rounded_border(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::BoxStyle style = {};
        style.fill_color = {0.12f, 0.42f, 0.50f, 0.92f};
        style.border_color = {0.98f, 0.80f, 0.22f, 1.0f};
        style.border_thickness = 7.0f + (wave(time_seconds, 0.34f, 2.0f) * 1.0f);
        style.radius = 32.0f + (wave(time_seconds, 0.30f, 0.4f) * 6.0f);
        style.softness = 1.0f;
        draw::draw_rect_styled(context, inset(tile, 46.0f), style);
    }

    auto draw_box_shadow(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        float const lift = wave(time_seconds, 0.38f, 0.0f) * 6.0f;
        draw::BoxStyle style = {};
        style.fill_color = {0.92f, 0.95f, 0.98f, 1.0f};
        style.border_color = {0.12f, 0.18f, 0.20f, 0.55f};
        style.border_thickness = 2.0f;
        style.radius = 24.0f;
        style.softness = 1.0f;
        style.shadow.offset = {18.0f, 20.0f - (lift * 0.65f)};
        style.shadow.blur_radius = 22.0f;
        style.shadow.spread = 4.0f;
        style.shadow.color = {0.0f, 0.0f, 0.0f, 0.44f};
        draw::draw_rect_styled(context, offset(inset(tile, 58.0f), {0.0f, -lift}), style);
    }

    auto draw_blur(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::LayerDesc layer = {};
        layer.bounds = inset(tile, 38.0f);
        layer.filter_kind = draw::FilterKind::BLUR;
        layer.filter_radius = 7.0f;
        float const sweep = wave(time_seconds, 0.32f, 0.9f) * 10.0f;
        draw::push_layer(context, layer);
        draw::draw_rect_filled(context,
                               offset(inset(layer.bounds, 34.0f), {sweep, 0.0f}),
                               {0.20f, 0.76f, 1.0f, 0.92f},
                               8.0f);
        draw::draw_circle_filled(context,
                                 {layer.bounds.min.x + 134.0f - sweep, layer.bounds.min.y + 92.0f},
                                 56.0f,
                                 {0.98f, 0.32f, 0.52f, 0.9f},
                                 32);
        draw::draw_rect_filled(
            context,
            {{layer.bounds.min.x + 86.0f, layer.bounds.min.y + 112.0f + sweep * 0.35f},
             {layer.bounds.max.x - 22.0f, layer.bounds.max.y - 18.0f + sweep * 0.35f}},
            {0.98f, 0.76f, 0.22f, 0.86f},
            12.0f);
        draw::pop_layer(context);
    }

    auto draw_drop_shadow(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::LayerDesc layer = {};
        layer.bounds = inset(tile, 48.0f);
        float const drift = wave(time_seconds, 0.34f, 2.4f) * 6.0f;
        layer.drop_shadow.offset = {18.0f + drift, 16.0f};
        layer.drop_shadow.blur_radius = 9.0f;
        layer.drop_shadow.color = {0.0f, 0.0f, 0.0f, 0.55f};
        draw::push_layer(context, layer);
        draw::draw_circle_filled(context,
                                 {layer.bounds.min.x + 74.0f + drift, layer.bounds.min.y + 70.0f},
                                 48.0f,
                                 {0.28f, 0.92f, 0.55f, 0.95f},
                                 40);
        draw::draw_triangle_filled(
            context,
            {layer.bounds.min.x + 124.0f - drift, layer.bounds.min.y + 38.0f},
            {layer.bounds.max.x - 18.0f - drift, layer.bounds.min.y + 120.0f},
            {layer.bounds.min.x + 104.0f - drift, layer.bounds.max.y - 16.0f},
            {0.22f, 0.48f, 0.96f, 0.95f});
        draw::pop_layer(context);
    }

    auto draw_clipped_layer(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::LayerDesc layer = {};
        layer.bounds = inset(tile, 38.0f);
        layer.clip_radius = 36.0f;
        float const slide = wave(time_seconds, 0.26f, 0.2f) * 16.0f;
        draw::push_layer(context, layer);
        draw::draw_rect_filled(context, layer.bounds, {0.95f, 0.96f, 0.90f, 1.0f}, 0.0f);
        draw::draw_rect_filled(context,
                               {{layer.bounds.min.x - 34.0f + slide, layer.bounds.min.y + 24.0f},
                                {layer.bounds.max.x + 34.0f + slide, layer.bounds.min.y + 74.0f}},
                               {0.92f, 0.20f, 0.24f, 0.9f},
                               0.0f);
        draw::draw_rect_filled(context,
                               {{layer.bounds.min.x - 30.0f - slide, layer.bounds.min.y + 94.0f},
                                {layer.bounds.max.x + 48.0f - slide, layer.bounds.min.y + 142.0f}},
                               {0.12f, 0.62f, 1.0f, 0.9f},
                               0.0f);
        draw::draw_circle_filled(
            context,
            {layer.bounds.max.x - 22.0f + (slide * 0.2f), layer.bounds.max.y - 16.0f},
            54.0f,
            {0.98f, 0.78f, 0.20f, 0.88f},
            32);
        draw::pop_layer(context);
        draw::draw_rect(
            context, layer.bounds, {0.92f, 0.95f, 0.98f, 0.9f}, 2.0f, layer.clip_radius);
    }

    auto draw_blend_swatch(draw::Context context,
                           draw::Rect rect,
                           draw::LayerBlendMode blend_mode,
                           draw::Color color,
                           float time_seconds,
                           float phase) -> void {
        draw::draw_rect_filled(context, rect, {0.02f, 0.04f, 0.055f, 1.0f}, 7.0f);
        draw::draw_rect_filled(context, inset(rect, 12.0f), {0.14f, 0.50f, 0.95f, 1.0f}, 5.0f);
        draw::draw_circle_filled(context,
                                 {rect.min.x + 56.0f, rect.min.y + 72.0f},
                                 38.0f,
                                 {0.98f, 0.78f, 0.18f, 1.0f},
                                 28);

        draw::LayerDesc layer = {};
        layer.bounds = rect;
        layer.blend_mode = blend_mode;
        draw::push_layer(context, layer);
        float const drift = wave(time_seconds, 0.30f, phase) * 7.0f;
        draw::draw_rect_filled(
            context, offset(inset(rect, 30.0f), {16.0f + drift, 14.0f}), color, 8.0f);
        draw::pop_layer(context);
    }

    auto draw_blend_modes(draw::Context context, draw::Rect tile, float time_seconds) -> void {
        draw_tile(context, tile);
        draw::Rect const area = inset(tile, 28.0f);
        draw::Rect const normal = {area.min, {area.min.x + 92.0f, area.min.y + 86.0f}};
        draw::Rect const additive = offset(normal, {110.0f, 0.0f});
        draw::Rect const multiply = offset(normal, {0.0f, 104.0f});
        draw::Rect const screen = offset(normal, {110.0f, 104.0f});
        draw_blend_swatch(context,
                          normal,
                          draw::LayerBlendMode::NORMAL,
                          {0.94f, 0.20f, 0.48f, 0.78f},
                          time_seconds,
                          0.0f);
        draw_blend_swatch(context,
                          additive,
                          draw::LayerBlendMode::ADDITIVE,
                          {0.94f, 0.20f, 0.48f, 0.78f},
                          time_seconds,
                          1.1f);
        draw_blend_swatch(context,
                          multiply,
                          draw::LayerBlendMode::MULTIPLY,
                          {0.94f, 0.20f, 0.48f, 0.78f},
                          time_seconds,
                          2.2f);
        draw_blend_swatch(context,
                          screen,
                          draw::LayerBlendMode::SCREEN,
                          {0.94f, 0.20f, 0.48f, 0.78f},
                          time_seconds,
                          3.3f);
    }

    auto build_draw_commands(draw::Context context, float time_seconds) -> void {
        draw::begin_frame(context);
        draw_alpha_overlap(context, tile_rect(0u, 0u), time_seconds);
        draw_group_opacity(context, tile_rect(1u, 0u), time_seconds);
        draw_rounded_border(context, tile_rect(2u, 0u), time_seconds);
        draw_box_shadow(context, tile_rect(3u, 0u), time_seconds);
        draw_blur(context, tile_rect(0u, 1u), time_seconds);
        draw_drop_shadow(context, tile_rect(1u, 1u), time_seconds);
        draw_clipped_layer(context, tile_rect(2u, 1u), time_seconds);
        draw_blend_modes(context, tile_rect(3u, 1u), time_seconds);
        draw::end_frame(context);
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
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
                                          L"gui_framework rendering effects testbed",
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

    render::Context render_context = {};
    render::ContextDesc context_desc = {};
    context_desc.backend = render::Backend::D3D11;
#if BASE_DEBUG
    context_desc.enable_debug_layer = true;
#endif

    render::Result result = render::create_context(app_arena, context_desc, render_context);
    if (render::result_failed(result)) {
        log_render_result("render::create_context", result);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    render::Window render_window = {};
    render::WindowDesc window_desc = {};
    window_desc.native_window = app_state.hwnd;
    window_desc.size = {INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT};
    window_desc.buffer_count = 2u;
    window_desc.present_mode = render::PresentMode::VSYNC;

    result = render::create_window(app_arena, render_context, window_desc, render_window);
    if (render::result_failed(result)) {
        log_render_result("render::create_window", result);
        render::destroy_context(render_context);
        DestroyWindow(app_state.hwnd);
        return 1;
    }

    draw::Context draw_context = {};
    draw::ContextDesc draw_desc = {};
    draw::create_context(app_arena, draw_desc, draw_context);

    draw::Renderer draw_renderer = {};
    draw::RendererDesc const renderer_desc = {};
    result = draw::create_renderer(app_arena, render_context, renderer_desc, draw_renderer);
    if (render::result_failed(result)) {
        log_render_result("draw::create_renderer", result);
        draw::destroy_context(draw_context);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
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
            result = render::resize_window(render_context, render_window, app_state.pending_size);
            if (render::result_failed(result)) {
                log_render_result("render::resize_window", result);
                app_state.running = false;
                continue;
            }
            app_state.resize_pending = false;
        }

        render::begin_frame(render_context);
        uint64_t const elapsed_ticks = GetTickCount64() - start_ticks;
        float const time_seconds = static_cast<float>(elapsed_ticks) * 0.006f;
        build_draw_commands(draw_context, time_seconds);

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.035f, 0.045f, 0.055f, 1.0f};

        result =
            draw::render_commands_to_window(draw_renderer, render_context, pass_desc, draw_context);
        if (render::result_failed(result)) {
            log_render_result("draw::render_commands_to_window", result);
            break;
        }

        result = render::present_window(render_context, render_window);
        if (result == render::Result::OCCLUDED) {
            Sleep(16u);
            continue;
        }
        if (render::result_failed(result)) {
            log_render_result("render::present_window", result);
            break;
        }
    }

    draw::destroy_renderer(render_context, draw_renderer);
    draw::destroy_context(draw_context);
    render::destroy_window(render_window);
    render::destroy_context(render_context);

    if (app_state.hwnd != nullptr && IsWindow(app_state.hwnd)) {
        DestroyWindow(app_state.hwnd);
    }

    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    global_app_state = nullptr;
    return 0;
}
