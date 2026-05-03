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
#include <dwmapi.h>
#include <gui/gui.h>
#include <gui/hot_reload_overlay.h>
#include <render/render.h>
#include <repository_ui_testbed_hot_reload_manifest.h>
#include <windows.h>

#ifndef REPOSITORY_UI_TESTBED_SOURCE_DIR
#define REPOSITORY_UI_TESTBED_SOURCE_DIR "."
#endif

#ifndef REPOSITORY_UI_TESTBED_BINARY_DIR
#define REPOSITORY_UI_TESTBED_BINARY_DIR "."
#endif

#ifndef REPOSITORY_UI_TESTBED_BUILD_CONFIG
#define REPOSITORY_UI_TESTBED_BUILD_CONFIG "Debug"
#endif

namespace repository_ui_testbed {

    namespace render = gui::render;

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_repository_ui_testbed";
    constexpr DWORD DWM_ATTR_USE_IMMERSIVE_DARK_MODE = 20u;
    constexpr DWORD DWM_ATTR_BORDER_COLOR = 34u;
    constexpr DWORD DWM_ATTR_CAPTION_COLOR = 35u;
    constexpr DWORD DWM_ATTR_TEXT_COLOR = 36u;
    constexpr COLORREF WINDOW_HEADER_BACKGROUND = RGB(0, 0, 0);
    constexpr COLORREF WINDOW_HEADER_TEXT = RGB(233, 233, 233);

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool redraw_pending = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::Frame last_frame = {};
        gui::Id mouse_hit_id = {};
        gui::InputState input = {};
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

    [[nodiscard]] auto loword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_u32(LPARAM value) -> uint32_t {
        return static_cast<uint32_t>(static_cast<uint16_t>((value >> 16) & 0xffff));
    }

    [[nodiscard]] auto loword_i32(LPARAM value) -> int32_t {
        return static_cast<int32_t>(static_cast<int16_t>(value & 0xffff));
    }

    [[nodiscard]] auto hiword_i32(LPARAM value) -> int32_t {
        return static_cast<int32_t>(static_cast<int16_t>((value >> 16) & 0xffff));
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    [[nodiscard]] auto hot_reload_desc(ModuleRuntimeContext* context) -> gui::HotReloadDesc {
        return {
            .label = "repository_ui_testbed",
            .source_dir = REPOSITORY_UI_TESTBED_SOURCE_DIR,
            .binary_dir = REPOSITORY_UI_TESTBED_BINARY_DIR,
            .build_config = REPOSITORY_UI_TESTBED_BUILD_CONFIG,
            .build_target = "repository_ui_testbed_module",
            .api_export_name = "repository_ui_testbed_get_module_api",
            .module_file_name = MODULE_FILE_NAME,
            .module_copy_prefix = "repository_ui_testbed_module",
            .watched_files = HOT_RELOAD_WATCH_FILES,
            .storage_size = MODULE_STORAGE_SIZE,
            .storage_alignment = MODULE_STORAGE_ALIGNMENT,
            .user_data = context,
        };
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
                gui::Vec2 const pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
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
        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                POINT point = {
                    static_cast<LONG>(loword_i32(lparam)),
                    static_cast<LONG>(hiword_i32(lparam)),
                };
                BASE_UNUSED(ScreenToClient(hwnd, &point));
                global_app_state->input.mouse_pos = {
                    static_cast<float>(point.x),
                    static_cast<float>(point.y),
                };
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 72.0f;
                global_app_state->input.key_mods = current_key_mods();
                request_redraw(global_app_state);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
                global_app_state->input.mouse_down[0u] = true;
                request_redraw(global_app_state);
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;
        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {
                    static_cast<float>(loword_i32(lparam)),
                    static_cast<float>(hiword_i32(lparam)),
                };
                global_app_state->input.mouse_down[0u] = false;
                request_redraw(global_app_state);
            }
            if (GetCapture() == hwnd) {
                ReleaseCapture();
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
        DWORD const ex_style = WS_EX_DLGMODALFRAME;
        RECT rect = {};
        rect.right = static_cast<LONG>(INITIAL_WINDOW_WIDTH);
        rect.bottom = static_cast<LONG>(INITIAL_WINDOW_HEIGHT);
        if (!AdjustWindowRectEx(&rect, style, FALSE, ex_style)) {
            fmt::eprintf("AdjustWindowRectEx failed: %lu\n", GetLastError());
            return false;
        }

        HWND const hwnd = CreateWindowExW(
            ex_style,
            WINDOW_CLASS_NAME,
            L"gui_framework repository UI testbed",
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

        apply_window_header_theme(hwnd);
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

    auto run_windowed() -> int {
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
        context_desc.enable_debug_layer = true;
#endif

        render::Result result = render::create_context(app_arena, context_desc, render_context);
        if (render::result_failed(result)) {
            log_render_result("render::create_context", result);
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
            log_render_result("render::create_window", result);
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
        bool const module_loaded = gui::load_hot_reload_app_module(
            &module, module_desc, repository_ui_testbed_module_api()
        );
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
                    render::resize_window(render_context, render_window, app_state.pending_size);
                if (render::result_failed(result)) {
                    log_render_result("render::resize_window", result);
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
#if BASE_DEBUG
                DWORD const wait_ms = HOT_RELOAD_POLL_MS;
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
                }
            }
#endif

            FrameResult const frame_result = gui::hot_reload_app_module_api(module)->render_frame(
                gui::hot_reload_app_module_storage(module),
                render_context,
                render_window,
                render::window_size(render_window),
                module_input,
                delta_time
            );
            app_state.last_frame = frame_result.frame;
            app_state.mouse_hit_id = frame_result.mouse_hit_id;
            if (render::result_failed(frame_result.render_result)) {
                log_render_result("draw::render_commands_to_window", frame_result.render_result);
                break;
            }

#if BASE_DEBUG
            if (hot_reload_overlay_ready && hot_reload_overlay_visible) {
                result = gui::render_hot_reload_overlay(
                    &hot_reload_overlay, render_context, render_window
                );
                if (render::result_failed(result)) {
                    log_render_result("render_hot_reload_overlay", result);
                    break;
                }
            }
#endif

            result = render::present_window(render_context, render_window);
            app_state.redraw_pending = frame_result.redraw_pending;
            app_state.input.scroll_delta_y = 0.0f;
            if (result == render::Result::OCCLUDED) {
                Sleep(16u);
            } else if (render::result_failed(result)) {
                log_render_result("render::present_window", result);
                break;
            }
        }

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

} // namespace repository_ui_testbed

auto main() -> int {
    base::install_crash_handlers();
    int const result = repository_ui_testbed::run_windowed();
    shutdown_thread_temp_arenas();
    return result;
}
