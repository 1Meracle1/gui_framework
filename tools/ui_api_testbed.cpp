#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <base/config.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <windows.h>
#endif
#include <gui/gui.h>

namespace {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;
#endif

    struct TestbedState {
        bool enabled = true;
        bool preview = true;
        bool read_only_value = false;
        bool reveal_asset_list = true;
        bool reveal_log_scroll = true;
        float scale = 1.25f;
        size_t selected_index = 12u;
        char name[64] = "Editable text";
        gui::TextSelection title_selection = {};
        gui::TextSelection body_selection = {};
        gui::Signal header_signal = {};
        gui::Signal selected_row_signal = {};
    };

    auto row_id(size_t index) -> gui::Id {
        return gui::id(0xA1100000ull + static_cast<uint64_t>(index));
    }

    constexpr char BODY_TEXT[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n"
        "Integer posuere erat a ante venenatis dapibus posuere velit aliquet.\n\n"
        "Donec ullamcorper nulla non metus auctor fringilla.\n"
        "Cras mattis consectetur purus sit amet fermentum.\n\n"
        "Maecenas sed diam eget risus varius blandit sit amet non magna.\n"
        "Vestibulum id ligula porta felis euismod semper.";

    auto draw_scroll_lines(gui::Frame& ui, StrRef prefix, size_t count) -> void {
        for (size_t index = 0u; index < count; ++index) {
            ui.label(
                fmt::tprintf("%s %02zu", prefix, index),
                {.layout = {.width = gui::fill(), .height = gui::px(22.0f)}}
            );
        }
    }

    auto draw_ui(gui::Frame& ui, TestbedState& state) -> void {
        gui::Id const list_id = gui::id("asset_list");
        gui::Id const notes_id = gui::id("notes_scroll");
        gui::Id const body_text_id = gui::id("body_text_scroll");
        gui::Id const log_id = gui::id("log_scroll");

        if (state.reveal_asset_list) {
            ui.scroll_to_index(list_id, state.selected_index, gui::ScrollReveal::CENTER);
            state.reveal_asset_list = false;
        }
        ui.set_scroll_y(notes_id, 18.0f);
        if (state.reveal_log_scroll) {
            ui.scroll_to_end(log_id);
            state.reveal_log_scroll = false;
        }
        state.selected_row_signal = {};

        if (auto root = ui.column(
                gui::id("ui_api_testbed_root"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(8.0f),
                            .gap = 8.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .role = gui::StyleRole::CANVAS,
                            .background = gui::rgb(22, 24, 28),
                            .foreground = gui::rgb(235, 238, 242),
                        },
                    .debug_name = "ui_api_testbed_root",
                }
            )) {

            if (auto header = ui.row(
                    gui::id("header_bar"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(34.0f),
                                .padding = gui::insets(6.0f, 8.0f),
                                .gap = 8.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        .style =
                            {
                                .role = gui::StyleRole::PANEL,
                                .background = gui::rgb(34, 38, 44),
                                .border = gui::rgba(255, 255, 255, 28),
                                .border_thickness = 1.0f,
                                .radius = 4.0f,
                            },
                        .debug_name = "clickable_header_bar",
                    }
                )) {
                state.header_signal = header.signal();

                ui.label(
                    "V2 UI API Testbed", {.layout = {.width = gui::text(), .height = gui::fill()}}
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                if (ui.button(
                          gui::id("reset_scale"),
                          "Reset",
                          {
                              .layout =
                                  {
                                      .width = gui::px(72.0f),
                                      .height = gui::px(26.0f),
                                      .padding = gui::insets(2.0f, 6.0f),
                                  },
                              .style = {.role = gui::StyleRole::DANGER, .radius = 3.0f},
                              .debug_name = "reset_scale_button",
                          }
                    )
                        .activated) {
                    state.scale = 1.0f;
                }
            }

            if (auto body = ui.row(
                    gui::id("body"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::fill(),
                                .gap = 8.0f,
                                .align_y = gui::Align::STRETCH,
                            },
                        .debug_name = "body_row",
                    }
                )) {

                if (auto sidebar = ui.column(
                        gui::id("sidebar"),
                        {
                            .layout =
                                {
                                    .width = gui::px(220.0f),
                                    .height = gui::fill(),
                                    .padding = gui::insets(8.0f),
                                    .gap = 6.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .style =
                                {
                                    .role = gui::StyleRole::PANEL,
                                    .background = gui::rgb(30, 34, 40),
                                    .radius = 4.0f,
                                },
                            .debug_name = "sidebar",
                        }
                    )) {
                    ui.selectable_label(
                        "Virtualized Assets",
                        &state.title_selection,
                        {.layout = {.width = gui::text(), .height = gui::px(24.0f)}}
                    );

                    auto rows = ui.list_fixed(
                        list_id,
                        {
                            .item_count = 48u,
                            .item_height = 26.0f,
                            .box = {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(226.0f),
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(24, 27, 32),
                                    },
                                .debug_name = "asset_list",
                            },
                        }
                    );
                    for (size_t index = rows.first; index < rows.end; ++index) {
                        bool const selected = index == state.selected_index;
                        auto row = rows.row(
                            row_id(index),
                            {
                                .layout =
                                    {
                                        .padding = gui::insets(0.0f, 6.0f),
                                        .gap = 6.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = selected ? gui::StyleRole::ACCENT
                                                         : gui::StyleRole::CONTROL,
                                        .background = selected ? gui::rgb(58, 108, 220)
                                                               : gui::rgba(255, 255, 255, 10),
                                        .radius = 3.0f,
                                    },
                                .debug_name = "asset_row",
                            }
                        );
                        gui::Signal const signal = row.signal();
                        if (signal.activated) {
                            state.selected_index = index;
                        }
                        if (index == state.selected_index) {
                            state.selected_row_signal = signal;
                        }
                        ui.label(
                            fmt::tprintf("Asset %02zu", index),
                            {.layout = {.width = gui::fill(), .height = gui::fill()}}
                        );
                    }

                    if (auto notes = ui.scroll_panel(
                            notes_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(112.0f),
                                        .padding = gui::insets(8.0f),
                                        .gap = 4.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(25, 28, 34),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "notes_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Note line", 8u);
                    }
                }

                if (auto main_panel = ui.column(
                        gui::id("main_panel"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .gap = 8.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .debug_name = "main_panel",
                        }
                    )) {

                    if (auto controls = ui.row(
                            gui::id("controls"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::children(),
                                        .padding = gui::insets(6.0f, 8.0f),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(30, 34, 40),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "controls",
                            }
                        )) {
                        ui.checkbox(
                            gui::id("enabled_checkbox"),
                            "Enabled",
                            &state.enabled,
                            {.layout = {.width = gui::px(116.0f), .height = gui::px(30.0f)}}
                        );
                        ui.toggle(
                            gui::id("preview_toggle"),
                            "Preview",
                            &state.preview,
                            {.layout = {.width = gui::px(126.0f), .height = gui::px(30.0f)}}
                        );
                        if (auto scale = ui.row(
                                gui::id("scale_control"),
                                {
                                    .layout =
                                        {
                                            .width = gui::px(188.0f),
                                            .height = gui::px(30.0f),
                                            .gap = 6.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .debug_name = "scale_control",
                                }
                            )) {
                            ui.label(
                                "Scale",
                                {.layout = {.width = gui::px(44.0f), .height = gui::fill()}}
                            );
                            ui.slider_float(
                                gui::id("scale_slider"),
                                " ",
                                &state.scale,
                                {
                                    .box =
                                        {
                                            .layout =
                                                {
                                                    .width = gui::fill(),
                                                    .height = gui::px(30.0f),
                                                },
                                            .style = {.foreground = gui::rgba(255, 255, 255, 0)},
                                        },
                                    .min = 0.5f,
                                    .max = 2.0f,
                                    .step = 0.05f,
                                }
                            );
                        }
                        ui.checkbox(
                            gui::id("read_only_checkbox"),
                            "Read-only",
                            &state.read_only_value,
                            {
                                .layout =
                                    {
                                        .width = gui::px(126.0f),
                                        .height = gui::px(30.0f),
                                    },
                                .flags = gui::BOX_FLAG_READ_ONLY,
                            }
                        );
                        ui.input_text(
                            gui::id("name_input"),
                            "Name",
                            state.name,
                            sizeof(state.name),
                            {
                                .layout =
                                    {
                                        .width = gui::px(160.0f),
                                        .height = gui::px(30.0f),
                                        .padding = gui::insets(5.0f, 8.0f),
                                    },
                                .debug_name = "name_input",
                            }
                        );
                        ui.button(
                            gui::id("disabled_button"),
                            "Disabled",
                            {
                                .layout =
                                    {
                                        .width = gui::px(96.0f),
                                        .height = gui::px(30.0f),
                                        .padding = gui::insets(3.0f, 6.0f),
                                    },
                                .flags = gui::BOX_FLAG_DISABLED,
                            }
                        );
                    }

                    if (auto body_text = ui.scroll_panel(
                            body_text_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(148.0f),
                                        .padding = gui::insets(8.0f),
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(30, 34, 40),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "body_text_scroll",
                            }
                        )) {
                        ui.selectable_label(
                            gui::id("body_text"),
                            BODY_TEXT,
                            &state.body_selection,
                            {
                                .layout = {.width = gui::fill(), .height = gui::text()},
                                .style = {.foreground = gui::rgb(210, 218, 230)},
                                .debug_name = "body_text",
                            }
                        );
                    }

                    if (auto preview = ui.overlay(
                            gui::id("preview_overlay"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .padding = gui::insets(10.0f),
                                        .align_x = gui::Align::START,
                                        .align_y = gui::Align::START,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(26, 29, 34),
                                        .border = gui::rgba(255, 255, 255, 24),
                                        .border_thickness = 1.0f,
                                        .radius = 4.0f,
                                    },
                                .debug_name = "preview_overlay",
                            }
                        )) {
                        if (auto top = ui.row(
                                gui::id("preview_top"),
                                {
                                    .layout =
                                        {
                                            .width = gui::fill(),
                                            .height = gui::px(28.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .debug_name = "preview_top",
                                }
                            )) {
                            ui.label(
                                "Preview fills the overlay",
                                {
                                    .layout = {.width = gui::text(), .height = gui::fill()},
                                    .style = {.foreground = gui::rgb(175, 188, 204)},
                                }
                            );
                            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                            if (auto badge = ui.row(
                                    gui::id("overlay_badge"),
                                    {
                                        .layout =
                                            {
                                                .width = gui::children(),
                                                .height = gui::px(28.0f),
                                                .padding = gui::insets(3.0f, 8.0f),
                                                .align_y = gui::Align::CENTER,
                                            },
                                        .style =
                                            {
                                                .role = gui::StyleRole::ACCENT,
                                                .radius = 3.0f,
                                            },
                                        .debug_name = "overlay_badge",
                                    }
                                )) {
                                ui.label(
                                    "Overlay",
                                    {.layout = {.width = gui::text(), .height = gui::fill()}}
                                );
                            }
                        }
                    }

                    if (auto log = ui.scroll_panel(
                            log_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(112.0f),
                                        .padding = gui::insets(8.0f),
                                        .gap = 4.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(30, 34, 40),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "log_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Log entry", 8u);
                    }
                }
            }
        }
    }

#if defined(_WIN32)
    constexpr wchar_t WINDOW_CLASS_NAME[] = L"gui_framework_ui_api_testbed";
    constexpr uint32_t INITIAL_WINDOW_WIDTH = 1280u;
    constexpr uint32_t INITIAL_WINDOW_HEIGHT = 800u;
    constexpr size_t MAX_KEY_EVENTS_PER_FRAME = 32u;

    struct UiRuntime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        gui::Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        TestbedState state = {};
    };

    struct AppState {
        HWND hwnd = nullptr;
        bool running = true;
        bool resize_pending = false;
        render::SizeU32 pending_size = {};
        gui::InputState input = {};
        gui::KeyEvent key_events[MAX_KEY_EVENTS_PER_FRAME] = {};
        gui::Vec2 left_double_click_pos = {};
        uint64_t left_double_click_ticks = 0u;
        bool left_double_click_pending = false;
    };

    AppState* global_app_state = nullptr;

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

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
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
        case 'C':
            return gui::Key::C;
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

    auto push_key_event(AppState* state, gui::Key key, gui::KeyEventKind kind) -> void {
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
        state->key_events[index] = {.key = key, .kind = kind, .mods = current_key_mods()};
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

    auto push_text_event(AppState* state, uint32_t codepoint) -> void {
        if (state == nullptr || state->input.key_event_count >= MAX_KEY_EVENTS_PER_FRAME) {
            return;
        }

        size_t const index = state->input.key_event_count;
        state->key_events[index] = {.kind = gui::KeyEventKind::TEXT,
                                    .mods = current_key_mods(),
                                    .codepoint = codepoint};
        state->input.key_events = state->key_events;
        state->input.key_event_count += 1u;
    }

    [[nodiscard]] auto key_down_kind(LPARAM lparam) -> gui::KeyEventKind {
        return (lparam & (1ll << 30)) != 0 ? gui::KeyEventKind::REPEAT : gui::KeyEventKind::PRESS;
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (!OpenClipboard(hwnd)) {
            fmt::eprintf("OpenClipboard failed: %lu\n", GetLastError());
            return;
        }

        int const wide_count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0
        );
        if (wide_count <= 0) {
            CloseClipboard();
            fmt::eprintf("MultiByteToWideChar failed: %lu\n", GetLastError());
            return;
        }

        HGLOBAL const memory =
            GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * (static_cast<size_t>(wide_count) + 1u));
        if (memory == nullptr) {
            CloseClipboard();
            fmt::eprintf("GlobalAlloc failed: %lu\n", GetLastError());
            return;
        }

        wchar_t* const wide_text = static_cast<wchar_t*>(GlobalLock(memory));
        if (wide_text == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            fmt::eprintf("GlobalLock failed: %lu\n", GetLastError());
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
            fmt::eprintf("SetClipboardData failed: %lu\n", GetLastError());
        }
#if BASE_DEBUG
        else {
            fmt::printf("copied %zu byte(s) to clipboard\n", text.size());
        }
#endif
        CloseClipboard();
    }

    auto destroy_ui_runtime(render::Context render_context, UiRuntime* runtime) -> void {
        if (runtime == nullptr) {
            return;
        }

        if (draw::renderer_valid(runtime->draw_renderer)) {
            draw::destroy_renderer(render_context, runtime->draw_renderer);
        }
        if (draw::context_valid(runtime->draw_context)) {
            draw::destroy_context(runtime->draw_context);
        }
        if (gui::context_valid(runtime->ui_context)) {
            gui::destroy_context(runtime->ui_context);
        }
        if (font_cache::cache_valid(runtime->cache)) {
            font_cache::destroy_cache(runtime->cache);
        }
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
    }

    [[nodiscard]] auto
    create_ui_runtime(Arena& arena, render::Context render_context, HWND hwnd, UiRuntime* runtime)
        -> bool {
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, runtime->draw_renderer);
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = runtime->font;
        theme.root.font_size = 13.0f;
        gui::theme_role(theme, gui::StyleRole::ACCENT).normal.background = gui::rgb(58, 108, 220);
        gui::theme_role(theme, gui::StyleRole::DANGER).normal.background = gui::rgb(150, 54, 58);
        gui::create_context(
            arena,
            {
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .clipboard_user_data = hwnd,
            },
            runtime->ui_context
        );
        return true;
    }

    auto build_ui_commands(
        UiRuntime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> void {
        gui::Frame ui = gui::begin_frame(
            runtime->ui_context,
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
        draw_ui(ui, runtime->state);
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        gui::render_frame(ui, runtime->draw_context);
        draw::end_frame(runtime->draw_context);
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

        case WM_MOUSEMOVE:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
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
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_LBUTTONUP:
            if (global_app_state != nullptr) {
                global_app_state->input.mouse_down[0u] = false;
                global_app_state->input.mouse_pos = {lparam_x(lparam), lparam_y(lparam)};
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
            }
            SetCapture(hwnd);
            SetFocus(hwnd);
            return 0;

        case WM_MOUSEWHEEL:
            if (global_app_state != nullptr) {
                global_app_state->input.scroll_delta_y +=
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                    static_cast<float>(WHEEL_DELTA) * 36.0f;
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            gui::Key const key = key_from_virtual_key(wparam);
            if (key != gui::Key::UNKNOWN) {
                push_key_event(global_app_state, key, key_down_kind(lparam));
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }

        case WM_CHAR:
            if (global_app_state != nullptr) {
                push_text_event(global_app_state, static_cast<uint32_t>(wparam));
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

        UiRuntime runtime = {};
        if (!create_ui_runtime(app_arena, render_context, app_state.hwnd, &runtime)) {
            destroy_ui_runtime(render_context, &runtime);
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
            }

            uint64_t const ticks = GetTickCount64();
            float const delta_time = static_cast<float>(ticks - previous_ticks) * 0.001f;
            previous_ticks = ticks;

            render::begin_frame(render_context);
            build_ui_commands(
                &runtime, render::window_size(render_window), app_state.input, delta_time
            );

            render::WindowRenderPassDesc pass_desc = {};
            pass_desc.window = render_window;
            pass_desc.clear_color = {0.025f, 0.030f, 0.038f, 1.0f};

            result = draw::render_commands_to_window(
                runtime.draw_renderer, render_context, pass_desc, runtime.draw_context
            );
            if (render::result_failed(result)) {
                log_render_result("draw::render_commands_to_window", result);
                break;
            }

            result = render::present_window(render_context, render_window);
            app_state.input.scroll_delta_y = 0.0f;
            app_state.input.mouse_double_clicked[0u] = false;
            app_state.input.mouse_triple_clicked[0u] = false;
            app_state.input.key_events = app_state.key_events;
            app_state.input.key_event_count = 0u;
            if (result == render::Result::OCCLUDED) {
                Sleep(16u);
                continue;
            }
            if (render::result_failed(result)) {
                log_render_result("render::present_window", result);
                break;
            }
        }

        destroy_ui_runtime(render_context, &runtime);
        render::destroy_window(render_window);
        render::destroy_context(render_context);
        destroy_testbed_window(&app_state);
        global_app_state = nullptr;
        return 0;
    }
#else
    auto run_console_fallback() -> int {
        Arena arena = {};
        arena.init();

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font_size = 13.0f;
        gui::theme_role(theme, gui::StyleRole::ACCENT).normal.background = gui::rgb(58, 108, 220);
        gui::theme_role(theme, gui::StyleRole::DANGER).normal.background = gui::rgb(150, 54, 58);

        gui::Context ui_context = {};
        gui::create_context(arena, {.theme = &theme}, ui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        TestbedState state = {};
        gui::Frame ui =
            gui::begin_frame(ui_context, {.size = {640.0f, 400.0f}, .delta_time = 1.0f / 60.0f});
        draw_ui(ui, state);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        gui::draw::end_frame(draw_context);

        fmt::printf(
            "ui_api_testbed: windowed D3D11 path is Windows-only; commands=%zu styled_rects=%zu "
            "text=%zu\n",
            gui::draw::command_count(draw_context),
            gui::draw::styled_rect_command_count(draw_context),
            gui::draw::text_command_count(draw_context)
        );

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(ui_context);
        return 0;
    }
#endif

} // namespace

auto main() -> int {
    base::install_crash_handlers();

#if defined(_WIN32)
    return run_windowed();
#else
    return run_console_fallback();
#endif
}
