#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.h"

#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#if defined(_WIN32)
#include "module_loader.h"
#include "trace.h"

#include <algorithm>
#include <base/string_buffer.h>
#include <cstring>
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>
#endif
#include <gui/gui.h>

namespace ui_api_testbed {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_ui_api_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 800u;
    constexpr size_t MAX_KEY_EVENTS_PER_FRAME = 32u;

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool redraw_pending = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::Frame last_frame = {};
        gui::Id mouse_hit_id = {};
        gui::InputState input = {};
        gui::KeyEvent key_events[MAX_KEY_EVENTS_PER_FRAME] = {};
        gui::Vec2 left_double_click_pos = {};
        uint64_t left_double_click_ticks = 0u;
        bool left_double_click_pending = false;
    };

    AppState* global_app_state = nullptr;

    auto request_redraw(AppState* state) -> void {
        if (state != nullptr) {
            state->redraw_pending = true;
        }
    }

    [[nodiscard]] auto frame_ready(gui::Frame const& frame) -> bool {
        return frame.box_info_count() != 0u;
    }

    [[nodiscard]] auto frame_hit_id(gui::Frame const& frame, gui::Vec2 pos) -> gui::Id {
        gui::BoxInfo const* const box = frame.hit_test(pos);
        return box != nullptr ? box->id : gui::Id{};
    }

    [[nodiscard]] auto focused_box_exists(gui::Frame const& frame) -> bool {
        return frame.focused_box() != nullptr;
    }

    [[nodiscard]] auto focused_text_box_exists(gui::Frame const& frame) -> bool {
        gui::BoxInfo const* const box = frame.focused_box();
        return box != nullptr && (box->kind == gui::BoxKind::INPUT_TEXT ||
                                  box->kind == gui::BoxKind::INPUT_TEXT_MULTILINE);
    }

    [[nodiscard]] auto shortcut_key(gui::Key key, gui::KeyMods mods) -> bool {
        if ((mods & gui::KEY_MOD_CTRL) == 0u) {
            return false;
        }
        return key == gui::Key::A || key == gui::Key::C || key == gui::Key::V ||
               key == gui::Key::X || key == gui::Key::Z;
    }

    [[nodiscard]] auto key_event_needs_frame(AppState const& state, gui::Key key, gui::KeyMods mods)
        -> bool {
        return state.redraw_pending || !frame_ready(state.last_frame) || key == gui::Key::TAB ||
               shortcut_key(key, mods) || focused_box_exists(state.last_frame);
    }

    [[nodiscard]] auto text_event_needs_frame(AppState const& state) -> bool {
        return state.redraw_pending || !frame_ready(state.last_frame) ||
               focused_text_box_exists(state.last_frame);
    }

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto lparam_x(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>(value & 0xffff));
    }

    [[nodiscard]] auto lparam_y(LPARAM value) -> float {
        return static_cast<float>(static_cast<int16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto left_triple_click(AppState const& state, gui::Vec2 pos) -> bool {
        float const x_radius = static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)) * 0.5f;
        float const y_radius = static_cast<float>(GetSystemMetrics(SM_CYDOUBLECLK)) * 0.5f;
        return state.left_double_click_pending &&
               GetTickCount64() - state.left_double_click_ticks <=
                   static_cast<uint64_t>(GetDoubleClickTime()) &&
               pos.x >= state.left_double_click_pos.x - x_radius &&
               pos.x <= state.left_double_click_pos.x + x_radius &&
               pos.y >= state.left_double_click_pos.y - y_radius &&
               pos.y <= state.left_double_click_pos.y + y_radius;
    }

    auto log_host_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    [[nodiscard]] auto key_from_virtual_key(WPARAM value) -> gui::Key {
        switch (value) {
        case VK_TAB:
            return gui::Key::TAB;
        case VK_RETURN:
            return gui::Key::ENTER;
        case VK_ESCAPE:
            return gui::Key::ESCAPE;
        case VK_SPACE:
            return gui::Key::SPACE;
        case VK_LEFT:
            return gui::Key::LEFT;
        case VK_RIGHT:
            return gui::Key::RIGHT;
        case VK_UP:
            return gui::Key::UP;
        case VK_DOWN:
            return gui::Key::DOWN;
        case VK_HOME:
            return gui::Key::HOME;
        case VK_END:
            return gui::Key::END;
        case VK_BACK:
            return gui::Key::BACKSPACE;
        case VK_DELETE:
            return gui::Key::DELETE_KEY;
        case 'A':
            return gui::Key::A;
        case 'C':
            return gui::Key::C;
        case 'V':
            return gui::Key::V;
        case 'X':
            return gui::Key::X;
        case 'Z':
            return gui::Key::Z;
        default:
            return gui::Key::UNKNOWN;
        }
    }

    [[nodiscard]] auto current_key_mods() -> gui::KeyMods {
        gui::KeyMods mods = gui::KEY_MOD_NONE;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SHIFT;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_CTRL;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_ALT;
        }
        if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
            mods |= gui::KEY_MOD_SUPER;
        }
        return mods;
    }

    auto push_key_event(AppState* state, gui::Key key, gui::KeyEventKind kind, gui::KeyMods mods)
        -> void {
        if (state == nullptr || key == gui::Key::UNKNOWN) {
            return;
        }
        if (state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
#if BASE_DEBUG
            fmt::eprintf("dropped key event\n");
#endif
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {.key = key, .kind = kind, .mods = mods};
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;

#if BASE_DEBUG
        if (kind == gui::KeyEventKind::PRESS || kind == gui::KeyEventKind::REPEAT) {
            fmt::printf(
                "key %s: %u mods=0x%02x\n",
                kind == gui::KeyEventKind::REPEAT ? "repeat" : "press",
                static_cast<unsigned>(key),
                static_cast<unsigned>(state->key_events[index].mods)
            );
        }
#endif
    }

    auto push_text_event(AppState* state, uint32_t codepoint, gui::KeyMods mods) -> void {
        if (state == nullptr || state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {
            .kind = gui::KeyEventKind::TEXT, .mods = mods, .codepoint = codepoint
        };
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    [[nodiscard]] auto key_down_kind(LPARAM lparam) -> gui::KeyEventKind {
        return (lparam & (1ll << 30)) != 0 ? gui::KeyEventKind::REPEAT : gui::KeyEventKind::PRESS;
    }

    auto window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
        switch (message) {
        case WM_SIZE:
            if (global_app_state != nullptr && wparam != SIZE_MINIMIZED) {
                render::SizeU32 const size = {loword_u32(lparam), hiword_u32(lparam)};
                if (size.width != 0u && size.height != 0u) {
                    global_app_state->pending_size = size;
                    global_app_state->resize_pending = true;
                    request_redraw(global_app_state);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                gui::Id const hit_id = frame_hit_id(global_app_state->last_frame, pos);
                bool const needs_frame = global_app_state->redraw_pending ||
                                         !frame_ready(global_app_state->last_frame) ||
                                         global_app_state->input.mouse_down[0u] ||
                                         hit_id.value != global_app_state->mouse_hit_id.value;
                global_app_state->input.mouse_pos = pos;
                global_app_state->mouse_hit_id = hit_id;
                if (needs_frame) {
                    request_redraw(global_app_state);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                global_app_state->input.mouse_down[0u] = true;
                global_app_state->input.mouse_pos = pos;
                global_app_state->input.mouse_triple_clicked[0u] =
                    left_triple_click(*global_app_state, pos);
                global_app_state->left_double_click_pending = false;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[0u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
                request_redraw(global_app_state);
            }
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (global_app_state != nullptr) {
                gui::Vec2 const pos = {lparam_x(lparam), lparam_y(lparam)};
                global_app_state->input.mouse_down[0u] = true;
                global_app_state->input.mouse_double_clicked[0u] = true;
                global_app_state->input.mouse_pos = pos;
                global_app_state->left_double_click_pos = pos;
                global_app_state->left_double_click_ticks = GetTickCount64();
                global_app_state->left_double_click_pending = true;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                POINT point = {
                    static_cast<LONG>(lparam_x(lparam)),
                    static_cast<LONG>(lparam_y(lparam)),
                };
                BASE_UNUSED(ScreenToClient(hwnd, &point));
                global_app_state->input.mouse_pos = {
                    static_cast<float>(point.x), static_cast<float>(point.y)
                };
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 36.0f;
                request_redraw(global_app_state);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            gui::Key const key = key_from_virtual_key(wparam);
            if (key != gui::Key::UNKNOWN) {
                gui::KeyMods const mods = current_key_mods();
                if (global_app_state != nullptr &&
                    key_event_needs_frame(*global_app_state, key, mods)) {
                    push_key_event(global_app_state, key, key_down_kind(lparam), mods);
                    request_redraw(global_app_state);
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        case WM_CHAR:
            if (global_app_state != nullptr) {
                if (text_event_needs_frame(*global_app_state)) {
                    push_text_event(
                        global_app_state, static_cast<uint32_t>(wparam), current_key_mods()
                    );
                    request_redraw(global_app_state);
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
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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

        HWND const hwnd = CreateWindowExW(
            0u,
            WINDOW_CLASS_NAME,
            L"gui_framework UI API testbed",
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            instance,
            nullptr
        );
        if (hwnd == nullptr) {
            fmt::eprintf("CreateWindowExW failed: %lu\n", GetLastError());
            return false;
        }

        app_state->hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    auto destroy_testbed_window(AppState* app_state) -> void {
        if (app_state->hwnd != nullptr && IsWindow(app_state->hwnd)) {
            DestroyWindow(app_state->hwnd);
        }
        app_state->hwnd = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    }

#if BASE_DEBUG
    struct HotReloadOverlayRenderer {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        gui::Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::TextSelection log_selection = {};
        StringBuffer log_text = {};
        HWND hwnd = nullptr;
    };

    struct HotReloadOverlayState {
        ReloadPhase observed_phase = ReloadPhase::IDLE;
        size_t observed_text_size = 0u;
        size_t observed_line_count = 0u;
        bool observed_truncated = false;
        bool visible = false;
        bool hidden_failure = false;
        bool follow_tail = true;
        bool capture_input = false;
        bool mouse_capture = false;
        bool last_mouse_down = false;
    };

    struct HotReloadOverlayMetrics {
        draw::Rect panel = {};
        draw::Rect log = {};
        draw::Rect close = {};
        bool valid = false;
    };

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    auto set_hot_reload_clipboard_text(void* user_data, StrRef text) -> void {
        auto* const overlay = static_cast<HotReloadOverlayRenderer*>(user_data);
        if (overlay == nullptr || overlay->hwnd == nullptr || !OpenClipboard(overlay->hwnd)) {
            return;
        }

        int const wide_count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0
        );
        if (wide_count <= 0) {
            CloseClipboard();
            return;
        }

        HGLOBAL const memory =
            GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * (static_cast<size_t>(wide_count) + 1u));
        if (memory == nullptr) {
            CloseClipboard();
            return;
        }

        auto* const wide_text = static_cast<wchar_t*>(GlobalLock(memory));
        if (wide_text == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return;
        }
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide_text,
            wide_count
        );
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
        CloseClipboard();
    }

    auto destroy_hot_reload_overlay_renderer(
        render::Context render_context, HotReloadOverlayRenderer* overlay
    ) -> void {
        if (overlay == nullptr) {
            return;
        }

        if (draw::renderer_valid(overlay->draw_renderer)) {
            draw::destroy_renderer(render_context, overlay->draw_renderer);
        }
        if (draw::context_valid(overlay->draw_context)) {
            draw::destroy_context(overlay->draw_context);
        }
        if (gui::context_valid(overlay->ui_context)) {
            gui::destroy_context(overlay->ui_context);
        }
        if (font_cache::cache_valid(overlay->cache)) {
            font_cache::destroy_cache(overlay->cache);
        }
        if (font_provider::context_valid(overlay->provider)) {
            font_provider::destroy_context(overlay->provider);
        }
        overlay->log_text.destroy();
        overlay->font = {};
        overlay->hwnd = nullptr;
    }

    [[nodiscard]] auto create_hot_reload_overlay_renderer(
        Arena& arena, render::Context render_context, HWND hwnd, HotReloadOverlayRenderer* overlay
    ) -> bool {
        overlay->hwnd = hwnd;
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, overlay->draw_renderer);
        if (render::result_failed(render_result)) {
            log_host_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, overlay->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            destroy_hot_reload_overlay_renderer(render_context, overlay);
            return false;
        }

        font_cache::create_cache(arena, overlay->provider, {}, overlay->cache);
        font_cache::open_system_font(overlay->cache, "Consolas", overlay->font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = overlay->cache;
        draw::create_context(arena, draw_desc, overlay->draw_context);

        gui::ThemeDesc theme = gui::default_theme();
        theme.tokens.accent = gui::rgba(78, 140, 255, 255);
        theme.root = {
            .foreground = gui::rgba(196, 200, 208, 235),
            .font = overlay->font,
            .font_size = 12.0f,
        };
        gui::create_context(
            arena,
            {
                .theme = &theme,
                .set_clipboard_text = set_hot_reload_clipboard_text,
                .clipboard_user_data = overlay,
            },
            overlay->ui_context
        );
        BASE_UNUSED(overlay->log_text.init(RELOAD_LOG_CAPACITY + RELOAD_LOG_LINE_COUNT));
        return true;
    }

    [[nodiscard]] auto reload_phase_title(ReloadPhase phase) -> StrRef {
        switch (phase) {
        case ReloadPhase::COMPILING:
            return "Hot reload: compiling";
        case ReloadPhase::RELOADING:
            return "Hot reload: reloading DLL";
        case ReloadPhase::FAILED:
            return "Hot reload: failed";
        case ReloadPhase::COMPLETE:
            return "Hot reload: complete";
        case ReloadPhase::IDLE:
            return "Hot reload";
        }
        return "Hot reload";
    }

    [[nodiscard]] auto rect_contains(draw::Rect rect, gui::Vec2 pos) -> bool {
        return pos.x >= rect.min.x && pos.x <= rect.max.x && pos.y >= rect.min.y &&
               pos.y <= rect.max.y;
    }

    [[nodiscard]] auto hot_reload_overlay_active(ReloadPhase phase) -> bool {
        return phase == ReloadPhase::COMPILING || phase == ReloadPhase::RELOADING;
    }

    [[nodiscard]] auto hot_reload_overlay_metrics(render::SizeU32 window_size)
        -> HotReloadOverlayMetrics {
        float const window_width = static_cast<float>(window_size.width);
        float const window_height = static_cast<float>(window_size.height);
        if (window_width < 80.0f || window_height < 80.0f) {
            return {};
        }

        float const panel_width = std::min(860.0f, std::max(160.0f, window_width - 32.0f));
        float const panel_height = std::min(380.0f, std::max(96.0f, window_height - 32.0f));
        draw::Rect const panel = {{16.0f, 16.0f}, {16.0f + panel_width, 16.0f + panel_height}};
        draw::Rect const log = {
            {panel.min.x + 14.0f, panel.min.y + 40.0f}, {panel.max.x - 14.0f, panel.max.y - 12.0f}
        };
        return {
            .panel = panel,
            .log = log,
            .close =
                {{panel.max.x - 36.0f, panel.min.y + 10.0f},
                 {panel.max.x - 12.0f, panel.min.y + 34.0f}},
            .valid = true,
        };
    }

    [[nodiscard]] auto update_hot_reload_overlay_state(
        HotReloadOverlayState* state,
        ReloadOverlay overlay,
        bool module_visible,
        render::SizeU32 window_size,
        gui::InputState* input
    ) -> bool {
        if (overlay.phase != ReloadPhase::FAILED) {
            state->hidden_failure = false;
        }
        if (hot_reload_overlay_active(overlay.phase) && state->observed_phase != overlay.phase) {
            state->follow_tail = true;
        }

        HotReloadOverlayMetrics const metrics = hot_reload_overlay_metrics(window_size);
        bool visible =
            module_visible && !(overlay.phase == ReloadPhase::FAILED && state->hidden_failure);
        bool changed = visible != state->visible || overlay.phase != state->observed_phase ||
                       overlay.text_size != state->observed_text_size ||
                       overlay.line_count != state->observed_line_count ||
                       overlay.truncated != state->observed_truncated;

        if (visible && metrics.valid) {
            bool const panel_hovered = rect_contains(metrics.panel, input->mouse_pos);
            bool const mouse_pressed = input->mouse_down[0u] && !state->last_mouse_down;
            if (mouse_pressed && panel_hovered) {
                state->mouse_capture = true;
            }
            if (!input->mouse_down[0u]) {
                state->mouse_capture = false;
            }
            state->capture_input = panel_hovered || state->mouse_capture;

            if (overlay.phase == ReloadPhase::FAILED && mouse_pressed &&
                rect_contains(metrics.close, input->mouse_pos)) {
                state->hidden_failure = true;
                visible = false;
                changed = true;
                state->capture_input = true;
                input->mouse_down[0u] = false;
                input->mouse_double_clicked[0u] = false;
                input->mouse_triple_clicked[0u] = false;
            } else if (input->scroll_delta_y > 0.0f && panel_hovered) {
                state->follow_tail = false;
            }
        } else {
            state->capture_input = false;
            state->mouse_capture = false;
        }

        state->last_mouse_down = input->mouse_down[0u];
        state->visible = visible;
        state->observed_phase = overlay.phase;
        state->observed_text_size = overlay.text_size;
        state->observed_line_count = overlay.line_count;
        state->observed_truncated = overlay.truncated;
        return changed;
    }

    auto update_hot_reload_log_text(HotReloadOverlayRenderer* renderer, ReloadOverlay overlay)
        -> void {
        renderer->log_text.reset();
        if (overlay.truncated) {
            BASE_UNUSED(renderer->log_text.write_string("output truncated\n"));
        }
        for (size_t index = 0u; index < overlay.line_count; ++index) {
            ReloadLogLine const& line = overlay.lines[index];
            BASE_UNUSED(renderer->log_text.write_bytes(overlay.text + line.offset, line.size));
            BASE_UNUSED(renderer->log_text.write_byte('\n'));
        }
    }

    auto build_hot_reload_overlay_commands(
        HotReloadOverlayRenderer* renderer,
        render::SizeU32 window_size,
        ReloadOverlay overlay,
        HotReloadOverlayState* state,
        gui::InputState const& input,
        float delta_time
    ) -> void {
        draw::begin_frame(renderer->draw_context);

        HotReloadOverlayMetrics const metrics = hot_reload_overlay_metrics(window_size);
        if (!metrics.valid || !state->visible) {
            draw::end_frame(renderer->draw_context);
            return;
        }
        update_hot_reload_log_text(renderer, overlay);

        draw::BoxStyle panel_style = {};
        panel_style.fill_color = {0.025f, 0.027f, 0.032f, 0.90f};
        panel_style.border_color = {1.0f, 1.0f, 1.0f, 0.16f};
        panel_style.border_thickness = 1.0f;
        panel_style.radius = 8.0f;
        panel_style.shadow = {
            .offset = {0.0f, 10.0f},
            .blur_radius = 24.0f,
            .color = {0.0f, 0.0f, 0.0f, 0.30f},
        };
        draw::draw_rect_styled(renderer->draw_context, metrics.panel, panel_style);

        draw::Color title_color = {0.94f, 0.96f, 1.0f, 0.96f};
        if (overlay.phase == ReloadPhase::FAILED) {
            title_color = {1.0f, 0.34f, 0.34f, 0.98f};
        }
        draw::draw_text(
            renderer->draw_context,
            {metrics.panel.min.x + 14.0f, metrics.panel.min.y + 12.0f},
            {.font = renderer->font, .size = 14.0f, .color = title_color},
            reload_phase_title(overlay.phase),
            nullptr
        );

        if (overlay.phase == ReloadPhase::FAILED) {
            draw::BoxStyle close_style = {};
            close_style.fill_color = {0.35f, 0.08f, 0.08f, 0.72f};
            close_style.border_color = {1.0f, 0.42f, 0.42f, 0.38f};
            close_style.border_thickness = 1.0f;
            close_style.radius = 5.0f;
            draw::draw_rect_styled(renderer->draw_context, metrics.close, close_style);
            draw::Color const close_color = {1.0f, 0.78f, 0.78f, 0.92f};
            draw::draw_line(
                renderer->draw_context,
                {metrics.close.min.x + 7.0f, metrics.close.min.y + 7.0f},
                {metrics.close.max.x - 7.0f, metrics.close.max.y - 7.0f},
                close_color,
                1.6f
            );
            draw::draw_line(
                renderer->draw_context,
                {metrics.close.max.x - 7.0f, metrics.close.min.y + 7.0f},
                {metrics.close.min.x + 7.0f, metrics.close.max.y - 7.0f},
                close_color,
                1.6f
            );
        }

        gui::Id const log_id = gui::id("hot_reload_log");
        gui::Frame ui = gui::begin_frame(
            renderer->ui_context,
            {
                .size =
                    {
                        static_cast<float>(window_size.width),
                        static_cast<float>(window_size.height),
                    },
                .delta_time = delta_time,
                .input = input,
            }
        );
        if (state->follow_tail) {
            ui.scroll_to_end(log_id);
        }
        ui.selectable_label(
            log_id,
            renderer->log_text.str(),
            &renderer->log_selection,
            {
                .layout =
                    {
                        .width = gui::px(metrics.log.max.x - metrics.log.min.x),
                        .height = gui::px(metrics.log.max.y - metrics.log.min.y),
                        .margin = gui::insets(metrics.log.min.y, 0.0f, 0.0f, metrics.log.min.x),
                        .word_wrap = true,
                    },
                .style =
                    {
                        .role = gui::StyleRole::TEXT,
                        .foreground = gui::rgba(196, 200, 208, 235),
                        .font = renderer->font,
                        .font_size = 12.0f,
                    },
                .debug_name = "hot_reload_log",
            }
        );
        gui::end_frame(ui);
        gui::render_frame(ui, renderer->draw_context);
        gui::ScrollState const scroll = ui.scroll_state(log_id);
        if (scroll.valid) {
            state->follow_tail = scroll.y >= scroll.max_y - 1.0f;
        }

        draw::end_frame(renderer->draw_context);
    }

    [[nodiscard]] auto render_hot_reload_overlay(
        HotReloadOverlayRenderer* overlay,
        render::Context render_context,
        render::Window render_window
    ) -> render::Result {
        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.load_op = render::LoadOp::LOAD;
        return draw::render_commands_to_window(
            overlay->draw_renderer, render_context, pass_desc, overlay->draw_context
        );
    }

    [[nodiscard]] auto trace_idle_wait_ms(
        TraceOptions const& options,
        ManualTrace const& trace,
        bool trace_start_done,
        uint64_t trace_warmup_start
    ) -> DWORD {
        uint64_t wait_ms = INFINITE;
        if (!trace_start_done && options.path != nullptr) {
            uint64_t const elapsed_ms = GetTickCount64() - trace_warmup_start;
            wait_ms = elapsed_ms < options.warmup_ms ? options.warmup_ms - elapsed_ms : 0u;
        } else if (options.duration_ms != 0u && trace_active(&trace)) {
            double const elapsed_ms = trace_elapsed_ms(trace);
            wait_ms = elapsed_ms < static_cast<double>(options.duration_ms)
                          ? options.duration_ms - static_cast<uint64_t>(elapsed_ms)
                          : 0u;
        }
        return wait_ms >= INFINITE ? INFINITE - 1u : static_cast<DWORD>(wait_ms);
    }
#endif

    auto run_windowed(TraceOptions trace_options) -> int {
        AppState app_state = {};
        global_app_state = &app_state;

        if (!create_testbed_window(&app_state)) {
            global_app_state = nullptr;
            return 1;
        }

        Arena app_arena = {};
        app_arena.init();

        render::Context render_context = {};
        render::ContextDesc context_desc = {};
        context_desc.backend = render::Backend::D3D11;
#if BASE_DEBUG
        context_desc.enable_debug_layer = trace_options.enable_debug_layer;
#endif

        render::Result result = render::create_context(app_arena, context_desc, render_context);
        if (render::result_failed(result)) {
            log_host_render_result("render::create_context", result);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
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
            log_host_render_result("render::create_window", result);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

#if BASE_DEBUG
        HotReloadOverlayRenderer hot_reload_overlay = {};
        HotReloadOverlayState hot_reload_overlay_state = {};
        bool const hot_reload_overlay_ready = create_hot_reload_overlay_renderer(
            app_arena, render_context, app_state.hwnd, &hot_reload_overlay
        );
#endif

        TestbedModule module = {};
        init_testbed_module_storage(&module, app_arena);
        if (!load_testbed_module(&module, render_context, app_state.hwnd)) {
            destroy_testbed_module(&module, render_context);
#if BASE_DEBUG
            if (hot_reload_overlay_ready) {
                destroy_hot_reload_overlay_renderer(render_context, &hot_reload_overlay);
            }
#endif
            render::destroy_window(render_window);
            render::destroy_context(render_context);
            destroy_testbed_window(&app_state);
            global_app_state = nullptr;
            return 1;
        }

        uint64_t previous_ticks = GetTickCount64();
        uint64_t const trace_warmup_start = previous_ticks;
        bool trace_start_done = false;
        ManualTrace trace = {};
        while (app_state.running) {
            if (!trace_start_done && trace_options.path != nullptr &&
                GetTickCount64() - trace_warmup_start >= trace_options.warmup_ms) {
                trace_start_done = true;
#if BASE_DEBUG
                BASE_UNUSED(trace_start(
                    &trace,
                    trace_options.path,
                    render::window_size(render_window),
                    trace_options.enable_debug_layer
                ));
#endif
            }
#if BASE_DEBUG
            if (trace_options.duration_ms != 0u && trace_active(&trace) &&
                trace_elapsed_ms(trace) >= static_cast<double>(trace_options.duration_ms)) {
                app_state.running = false;
                break;
            }
#endif

            {
                TRACE_SCOPE(&trace, "frame");
                {
                    TRACE_SCOPE(&trace, "pump_messages");
                    MSG message = {};
                    while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
                        if (message.message == WM_QUIT) {
                            app_state.running = false;
                            break;
                        }
                        TranslateMessage(&message);
#if BASE_DEBUG
                        if (trace_input_message(message.message)) {
                            TRACE_SCOPE(&trace, "input_handling");
                            DispatchMessageW(&message);
                        } else
#endif
                        {
                            DispatchMessageW(&message);
                        }
                    }
                }

                if (!app_state.running) {
                    break;
                }

                if (app_state.resize_pending) {
                    TRACE_SCOPE(&trace, "resize");
                    result = render::resize_window(
                        render_context, render_window, app_state.pending_size
                    );
                    if (render::result_failed(result)) {
                        log_host_render_result("render::resize_window", result);
                        break;
                    }
                    app_state.resize_pending = false;
                    app_state.redraw_pending = true;
                }

                if (update_testbed_module(&module, render_context, app_state.hwnd)) {
                    app_state.last_frame = {};
                    app_state.mouse_hit_id = {};
                    app_state.redraw_pending = true;
                }
#if BASE_DEBUG
                ReloadOverlay reload_overlay = {};
                bool hot_reload_overlay_visible = false;
                if (hot_reload_overlay_ready) {
                    reload_overlay = testbed_module_reload_overlay(module);
                    bool const module_overlay_visible =
                        testbed_module_reload_overlay_visible(module);
                    if (update_hot_reload_overlay_state(
                            &hot_reload_overlay_state,
                            reload_overlay,
                            module_overlay_visible,
                            render::window_size(render_window),
                            &app_state.input
                        )) {
                        app_state.redraw_pending = true;
                    }
                    hot_reload_overlay_visible = hot_reload_overlay_state.visible;
                }
#endif

                if (!app_state.redraw_pending) {
                    TRACE_SCOPE(&trace, "idle_wait");
#if BASE_DEBUG
                    DWORD const wait_ms = std::min(
                        trace_idle_wait_ms(
                            trace_options, trace, trace_start_done, trace_warmup_start
                        ),
                        static_cast<DWORD>(HOT_RELOAD_POLL_MS)
                    );
#else
                    DWORD const wait_ms = INFINITE;
#endif
                    BASE_UNUSED(MsgWaitForMultipleObjectsEx(
                        0u, nullptr, wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE
                    ));
                    continue;
                }

                uint64_t const ticks = GetTickCount64();
                float const delta_time = static_cast<float>(ticks - previous_ticks) * 0.001f;
                previous_ticks = ticks;
                app_state.redraw_pending = false;
                gui::InputState module_input = app_state.input;
#if BASE_DEBUG
                if (hot_reload_overlay_ready) {
                    build_hot_reload_overlay_commands(
                        &hot_reload_overlay,
                        render::window_size(render_window),
                        reload_overlay,
                        &hot_reload_overlay_state,
                        app_state.input,
                        delta_time
                    );
                    if (hot_reload_overlay_state.capture_input) {
                        module_input.scroll_delta_y = 0.0f;
                        module_input.mouse_down[0u] = false;
                        module_input.mouse_double_clicked[0u] = false;
                        module_input.mouse_triple_clicked[0u] = false;
                        module_input.key_event_count = 0u;
                    }
                }
#endif
                FrameResult frame_result = {};
                {
                    TRACE_SCOPE(&trace, "module_render_frame");
                    frame_result = testbed_module_api(module)->render_frame(
                        testbed_module_storage(module),
                        render_context,
                        render_window,
                        render::window_size(render_window),
                        module_input,
                        delta_time
                    );
                }
#if BASE_DEBUG
                trace_draw_command_counts(&trace, frame_result.draw_counts);
#endif
                app_state.last_frame = frame_result.frame;
                app_state.mouse_hit_id = frame_result.mouse_hit_id;
                if (render::result_failed(frame_result.render_result)) {
                    log_host_render_result(
                        "draw::render_commands_to_window", frame_result.render_result
                    );
                    break;
                }

#if BASE_DEBUG
                if (hot_reload_overlay_ready && hot_reload_overlay_visible) {
                    result = render_hot_reload_overlay(
                        &hot_reload_overlay, render_context, render_window
                    );
                    if (render::result_failed(result)) {
                        log_host_render_result("render_hot_reload_overlay", result);
                        break;
                    }
                }
#endif

                {
                    TRACE_SCOPE(&trace, "present");
                    result = render::present_window(render_context, render_window);
                }
                app_state.redraw_pending = frame_result.redraw_pending;
                app_state.input.scroll_delta_y = 0.0f;
                app_state.input.mouse_double_clicked[0u] = false;
                app_state.input.mouse_triple_clicked[0u] = false;
                app_state.input.key_events = app_state.key_events;
                app_state.input.key_event_count = 0u;
                if (result == render::Result::OCCLUDED) {
                    TRACE_SCOPE(&trace, "idle_wait");
                    Sleep(16u);
                } else if (render::result_failed(result)) {
                    log_host_render_result("render::present_window", result);
                    break;
                }
            }
        }

#if BASE_DEBUG
        trace_finish(
            &trace,
            trace_options.path,
            render::window_size(render_window),
            trace_options.enable_debug_layer
        );
#else
        BASE_UNUSED(trace_options);
#endif
        destroy_testbed_module(&module, render_context);
#if BASE_DEBUG
        if (hot_reload_overlay_ready) {
            destroy_hot_reload_overlay_renderer(render_context, &hot_reload_overlay);
        }
#endif
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        destroy_testbed_window(&app_state);
        global_app_state = nullptr;
        return 0;
    }
#endif

} // namespace ui_api_testbed

auto main(int argc, char** argv) -> int {
    base::install_crash_handlers();

#if defined(_WIN32)
    ui_api_testbed::TraceOptions trace_options = {};
    if (!ui_api_testbed::parse_trace_options(argc, argv, &trace_options)) {
        return 2;
    }
#if !BASE_DEBUG
    if (ui_api_testbed::trace_requested(trace_options)) {
        fmt::printf("ui_api_testbed trace: debug-only tracing is disabled in this build\n");
        trace_options = {};
    }
#endif
    int const result = ui_api_testbed::run_windowed(trace_options);
#else
    BASE_UNUSED(argc);
    BASE_UNUSED(argv);
    int const result = ui_api_testbed::run_console_fallback();
#endif
    shutdown_thread_temp_arenas();
    return result;
}
