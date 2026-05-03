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
#include "trace.h"

#include <algorithm>
#include <dwmapi.h>
#include <render/render.h>
#include <ui_api_testbed_hot_reload_manifest.h>
#include <windows.h>
#endif
#include <gui/gui.h>
#include <gui/hot_reload_overlay.h>

#ifndef UI_API_TESTBED_SOURCE_DIR
#define UI_API_TESTBED_SOURCE_DIR "."
#endif

#ifndef UI_API_TESTBED_BINARY_DIR
#define UI_API_TESTBED_BINARY_DIR "."
#endif

#ifndef UI_API_TESTBED_BUILD_CONFIG
#define UI_API_TESTBED_BUILD_CONFIG "Debug"
#endif

namespace ui_api_testbed {

#if defined(_WIN32)
    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_ui_api_testbed";
    constexpr DWORD DWM_ATTR_USE_IMMERSIVE_DARK_MODE = 20u;
    constexpr DWORD DWM_ATTR_BORDER_COLOR = 34u;
    constexpr DWORD DWM_ATTR_CAPTION_COLOR = 35u;
    constexpr DWORD DWM_ATTR_TEXT_COLOR = 36u;
    constexpr COLORREF WINDOW_HEADER_BACKGROUND = RGB(5, 9, 15);
    constexpr COLORREF WINDOW_HEADER_TEXT = RGB(233, 233, 233);
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
        HICON app_icon = nullptr;
        HICON app_icon_small = nullptr;
    };

    AppState* global_app_state = nullptr;

    auto request_redraw(AppState* state) -> void {
        if (state != nullptr) {
            state->redraw_pending = true;
        }
    }

    [[nodiscard]] auto app_icon_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a) -> uint32_t {
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
    }

    [[nodiscard]] auto icon_inside_round_rect(
        int32_t x,
        int32_t y,
        int32_t min_x,
        int32_t min_y,
        int32_t max_x,
        int32_t max_y,
        int32_t radius
    ) -> bool {
        if (x < min_x || y < min_y || x >= max_x || y >= max_y) {
            return false;
        }

        int32_t corner_x = x < min_x + radius ? min_x + radius : max_x - radius - 1;
        int32_t corner_y = y < min_y + radius ? min_y + radius : max_y - radius - 1;
        if (x >= min_x + radius && x < max_x - radius) {
            corner_x = x;
        }
        if (y >= min_y + radius && y < max_y - radius) {
            corner_y = y;
        }

        int32_t const dx = x - corner_x;
        int32_t const dy = y - corner_y;
        return dx * dx + dy * dy <= radius * radius;
    }

    [[nodiscard]] auto
    icon_inside_circle(int32_t x, int32_t y, int32_t center_x, int32_t center_y, int32_t radius)
        -> bool {
        int32_t const dx = x - center_x;
        int32_t const dy = y - center_y;
        return dx * dx + dy * dy <= radius * radius;
    }

    auto draw_app_icon(uint32_t* pixels, int32_t size) -> void {
        for (int32_t y = 0; y < size; ++y) {
            for (int32_t x = 0; x < size; ++x) {
                int32_t const sx = (x * 32) / size;
                int32_t const sy = (y * 32) / size;
                uint32_t color = 0u;

                if (icon_inside_round_rect(sx, sy, 1, 1, 31, 31, 7)) {
                    if (icon_inside_round_rect(sx, sy, 2, 2, 30, 30, 6)) {
                        color = app_icon_color(
                            6u + static_cast<uint32_t>((sx * 9) / 31),
                            13u + static_cast<uint32_t>((sy * 14) / 31),
                            22u + static_cast<uint32_t>(((sx + sy) * 16) / 62),
                            255u
                        );
                    } else {
                        color = app_icon_color(46u, 60u, 78u, 255u);
                    }

                    if (icon_inside_round_rect(sx, sy, 6, 8, 15, 25, 3)) {
                        color = app_icon_color(65u, 142u, 255u, 255u);
                    }
                    if (sy >= 5 && sy < 27 && sx >= 12 + sy / 4 && sx <= 20 + sy / 4) {
                        color = app_icon_color(86u, 218u, 224u, 255u);
                    }
                    if (icon_inside_round_rect(sx, sy, 8, 10, 14, 15, 2)) {
                        color = app_icon_color(232u, 246u, 255u, 255u);
                    }
                    if (icon_inside_circle(sx, sy, 22, 12, 4)) {
                        color = app_icon_color(255u, 92u, 160u, 255u);
                    }
                    if (icon_inside_circle(sx, sy, 25, 15, 3)) {
                        color = app_icon_color(255u, 190u, 92u, 255u);
                    }
                }

                pixels[static_cast<size_t>(y * size + x)] = color;
            }
        }
    }

    [[nodiscard]] auto create_app_icon(int32_t size) -> HICON {
        BITMAPV5HEADER bitmap_header = {};
        bitmap_header.bV5Size = static_cast<DWORD>(sizeof(bitmap_header));
        bitmap_header.bV5Width = size;
        bitmap_header.bV5Height = -size;
        bitmap_header.bV5Planes = 1u;
        bitmap_header.bV5BitCount = 32u;
        bitmap_header.bV5Compression = BI_BITFIELDS;
        bitmap_header.bV5RedMask = 0x00ff0000u;
        bitmap_header.bV5GreenMask = 0x0000ff00u;
        bitmap_header.bV5BlueMask = 0x000000ffu;
        bitmap_header.bV5AlphaMask = 0xff000000u;

        void* bits = nullptr;
        HBITMAP const color_bitmap = CreateDIBSection(
            nullptr,
            reinterpret_cast<BITMAPINFO*>(&bitmap_header),
            DIB_RGB_COLORS,
            &bits,
            nullptr,
            0u
        );
        if (color_bitmap == nullptr || bits == nullptr) {
            if (color_bitmap != nullptr) {
                DeleteObject(color_bitmap);
            }
            return nullptr;
        }

        draw_app_icon(static_cast<uint32_t*>(bits), size);

        uint8_t mask_bits[128] = {};
        HBITMAP const mask_bitmap = CreateBitmap(size, size, 1u, 1u, mask_bits);
        if (mask_bitmap == nullptr) {
            DeleteObject(color_bitmap);
            return nullptr;
        }

        ICONINFO icon_info = {};
        icon_info.fIcon = TRUE;
        icon_info.hbmMask = mask_bitmap;
        icon_info.hbmColor = color_bitmap;
        HICON const icon = CreateIconIndirect(&icon_info);
        DeleteObject(mask_bitmap);
        DeleteObject(color_bitmap);
        return icon;
    }

    auto destroy_app_icons(AppState* app_state) -> void {
        if (app_state->app_icon != nullptr) {
            DestroyIcon(app_state->app_icon);
            app_state->app_icon = nullptr;
        }
        if (app_state->app_icon_small != nullptr) {
            DestroyIcon(app_state->app_icon_small);
            app_state->app_icon_small = nullptr;
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

    [[nodiscard]] auto hot_reload_desc(ModuleRuntimeContext* context) -> gui::HotReloadDesc {
        return {
            .label = "ui_api_testbed",
            .source_dir = UI_API_TESTBED_SOURCE_DIR,
            .binary_dir = UI_API_TESTBED_BINARY_DIR,
            .build_config = UI_API_TESTBED_BUILD_CONFIG,
            .build_target = "ui_api_testbed_module",
            .api_export_name = "ui_api_testbed_get_module_api",
            .module_file_name = MODULE_FILE_NAME,
            .module_copy_prefix = "ui_api_testbed_module",
            .watched_files = HOT_RELOAD_WATCH_FILES,
            .storage_size = MODULE_STORAGE_SIZE,
            .storage_alignment = MODULE_STORAGE_ALIGNMENT,
            .user_data = context,
        };
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
                global_app_state->input.key_mods = current_key_mods();
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

    auto apply_window_header_theme(HWND hwnd) -> void {
        BOOL const dark_mode = TRUE;
        COLORREF const border_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const caption_color = WINDOW_HEADER_BACKGROUND;
        COLORREF const text_color = WINDOW_HEADER_TEXT;

        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd,
            DWM_ATTR_USE_IMMERSIVE_DARK_MODE,
            &dark_mode,
            static_cast<DWORD>(sizeof(dark_mode))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_BORDER_COLOR, &border_color, static_cast<DWORD>(sizeof(border_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_CAPTION_COLOR, &caption_color, static_cast<DWORD>(sizeof(caption_color))
        ));
        BASE_UNUSED(DwmSetWindowAttribute(
            hwnd, DWM_ATTR_TEXT_COLOR, &text_color, static_cast<DWORD>(sizeof(text_color))
        ));
    }

    [[nodiscard]] auto create_testbed_window(AppState* app_state) -> bool {
        HINSTANCE const instance = GetModuleHandleW(nullptr);
        app_state->app_icon = create_app_icon(32);
        app_state->app_icon_small = create_app_icon(16);

        WNDCLASSEXW window_class = {};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hIcon = app_state->app_icon;
        window_class.hIconSm = app_state->app_icon_small;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&window_class) == 0u) {
            fmt::eprintf("RegisterClassExW failed: %lu\n", GetLastError());
            destroy_app_icons(app_state);
            return false;
        }

        DWORD const style = WS_OVERLAPPEDWINDOW;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRect(&rect, style, FALSE)) {
            fmt::eprintf("AdjustWindowRect failed: %lu\n", GetLastError());
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            destroy_app_icons(app_state);
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
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            destroy_app_icons(app_state);
            return false;
        }

        apply_window_header_theme(hwnd);
        BASE_UNUSED(
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_state->app_icon))
        );
        BASE_UNUSED(SendMessageW(
            hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_state->app_icon_small)
        ));
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
        destroy_app_icons(app_state);
    }

#if BASE_DEBUG
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
        gui::HotReloadOverlay hot_reload_overlay = {};
        gui::HotReloadOverlayState hot_reload_overlay_state = {};
        bool const hot_reload_overlay_ready = gui::create_hot_reload_overlay(
            app_arena, render_context, app_state.hwnd, &hot_reload_overlay
        );
#endif

        ModuleRuntimeContext module_context = {
            .render_context = render_context,
            .native_window = app_state.hwnd,
        };
        gui::HotReloadDesc module_desc = hot_reload_desc(&module_context);
        gui::HotReloadAppModule module = {};
        gui::init_hot_reload_app_module(&module, module_desc, app_arena);
#if BASE_DEBUG
        bool const module_loaded = gui::load_hot_reload_app_module(&module, module_desc, nullptr);
#else
        bool const module_loaded =
            gui::load_hot_reload_app_module(&module, module_desc, ui_api_testbed_module_api());
#endif
        if (!module_loaded) {
            gui::destroy_hot_reload_app_module(&module, module_desc);
#if BASE_DEBUG
            if (hot_reload_overlay_ready) {
                gui::destroy_hot_reload_overlay(render_context, &hot_reload_overlay);
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

                if (gui::update_hot_reload_app_module(&module, module_desc)) {
                    app_state.last_frame = {};
                    app_state.mouse_hit_id = {};
                    app_state.redraw_pending = true;
                }
#if BASE_DEBUG
                gui::HotReloadStatus reload_overlay = {};
                bool hot_reload_overlay_visible = false;
                if (hot_reload_overlay_ready) {
                    reload_overlay = gui::hot_reload_app_module_status(module);
                    bool const module_overlay_visible =
                        gui::hot_reload_app_module_status_visible(module);
                    if (gui::update_hot_reload_overlay_state(
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
                app_state.input.key_mods = current_key_mods();
                gui::InputState module_input = app_state.input;
#if BASE_DEBUG
                if (hot_reload_overlay_ready) {
                    gui::build_hot_reload_overlay_commands(
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
                    frame_result = gui::hot_reload_app_module_api(module)->render_frame(
                        gui::hot_reload_app_module_storage(module),
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
                    result = gui::render_hot_reload_overlay(
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
        gui::destroy_hot_reload_app_module(&module, module_desc);
#if BASE_DEBUG
        if (hot_reload_overlay_ready) {
            gui::destroy_hot_reload_overlay(render_context, &hot_reload_overlay);
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
