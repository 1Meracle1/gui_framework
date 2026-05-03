#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.h"

#include "ui_backdrop.h"
#include "ui_root.h"
#include "ui_theme.h"

#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <ui_api_testbed_embedded_texture.h>
#include <windows.h>
#endif
#include <gui/gui.h>

namespace ui_api_testbed {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;
#endif

#if defined(_WIN32)
#define TRACE_SCOPE(trace, name) BASE_UNUSED(trace)
#define trace_draw_command_counts(trace, context) BASE_UNUSED(trace)
    struct UiRuntime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        TestbedTextures textures = {};
        gui::Context ui_context = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        TestbedState state = {};
        HWND hwnd = nullptr;
        StringBuffer clipboard_text = {};
        DWORD clipboard_sequence = 0u;
        bool clipboard_valid = false;
    };

    [[nodiscard]] auto hash_bytes(uint64_t hash, void const* data, size_t size) -> uint64_t {
        uint8_t const* bytes = static_cast<uint8_t const*>(data);
        for (size_t index = 0u; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] auto testbed_state_hash(TestbedState const& state) -> uint64_t {
        uint64_t hash = 1469598103934665603ull;
        hash = hash_bytes(hash, &state, sizeof(state));
        StrRef const multiline_text = state.multiline_text_buffer.str();
        return hash_bytes(hash, multiline_text.data(), multiline_text.size());
    }

    auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto texture_sample_size(render::SizeU32 size) -> gui::Vec2 {
        return {static_cast<float>(size.width), static_cast<float>(size.height)};
    }

    [[nodiscard]] auto testbed_asset_path(char* path, size_t capacity, wchar_t const* file_name)
        -> bool {
        wchar_t wide_path[MAX_PATH] = {};
        DWORD const length = GetModuleFileNameW(nullptr, wide_path, MAX_PATH);
        if (length == 0u || length >= MAX_PATH || capacity == 0u ||
            capacity > static_cast<size_t>(0x7fffffffu)) {
            return false;
        }

        size_t dir_length = static_cast<size_t>(length);
        while (dir_length != 0u && wide_path[dir_length - 1u] != L'\\' &&
               wide_path[dir_length - 1u] != L'/') {
            --dir_length;
        }

        size_t const file_length = static_cast<size_t>(lstrlenW(file_name));
        if (dir_length + file_length >= MAX_PATH) {
            return false;
        }

        for (size_t index = 0u; index <= file_length; ++index) {
            wide_path[dir_length + index] = file_name[index];
        }

        int const written = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_path,
            -1,
            path,
            static_cast<int>(capacity),
            nullptr,
            nullptr
        );
        return written > 0;
    }

    auto destroy_texture_sample(render::Context context, TextureSample& sample) -> void {
        if (render::texture_valid(sample.texture)) {
            render::destroy_texture(context, sample.texture);
        }
        sample.size = {};
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        UiRuntime* const runtime = static_cast<UiRuntime*>(user_data);
        HWND const hwnd = runtime->hwnd;
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

    auto get_windows_clipboard_text(void* user_data, Arena& arena) -> StrRef {
        UiRuntime* const runtime = static_cast<UiRuntime*>(user_data);
        DWORD const sequence = GetClipboardSequenceNumber();
        if (runtime->clipboard_valid && runtime->clipboard_sequence == sequence) {
            StrRef const text = runtime->clipboard_text.str();
            if (text.empty()) {
                return {};
            }
            char* const copy = arena_alloc<char>(arena, text.size());
            return {copy, text.copy_to(copy, text.size())};
        }

        HWND const hwnd = runtime->hwnd;
        if (!OpenClipboard(hwnd)) {
            fmt::eprintf("OpenClipboard failed: %lu\n", GetLastError());
            return {};
        }

        HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) {
            CloseClipboard();
            return {};
        }

        wchar_t const* const wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
        if (wide_text == nullptr) {
            CloseClipboard();
            fmt::eprintf("GlobalLock failed: %lu\n", GetLastError());
            return {};
        }

        int const wide_count = lstrlenW(wide_text);
        if (wide_count == 0) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        int const byte_count = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_text, wide_count, nullptr, 0, nullptr, nullptr
        );
        if (byte_count <= 0) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        char* const text = arena_alloc<char>(arena, static_cast<size_t>(byte_count));
        WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide_text, wide_count, text, byte_count, nullptr, nullptr
        );
        GlobalUnlock(handle);
        CloseClipboard();
        runtime->clipboard_text.reset();
        BASE_UNUSED(runtime->clipboard_text.write_bytes(text, static_cast<size_t>(byte_count)));
        runtime->clipboard_sequence = sequence;
        runtime->clipboard_valid = true;
        return {text, static_cast<size_t>(byte_count)};
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
        destroy_texture_sample(render_context, runtime->textures.disk);
        destroy_texture_sample(render_context, runtime->textures.embedded);
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
    }

    [[nodiscard]] auto
    create_ui_runtime(Arena& arena, render::Context render_context, HWND hwnd, UiRuntime* runtime)
        -> bool {
        runtime->hwnd = hwnd;
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
        char disk_texture_path[MAX_PATH * 4] = {};
        if (!testbed_asset_path(
                disk_texture_path, sizeof(disk_texture_path), L"ui_api_testbed_texture.png"
            )) {
            fmt::eprintf("ui_api_testbed_texture.png path is too long\n");
            return false;
        }
        render_result = render::load_image_texture_from_file(
            render_context, disk_texture_path, runtime->textures.disk.texture
        );
        if (render::result_failed(render_result)) {
            log_render_result("render::load_image_texture_from_file", render_result);
            return false;
        }
        runtime->textures.disk.size =
            texture_sample_size(render::texture_size(runtime->textures.disk.texture));

        render_result = render::load_image_texture_from_memory(
            render_context,
            ui_api_testbed_assets::texture_png,
            ui_api_testbed_assets::texture_png_size,
            runtime->textures.embedded.texture
        );
        if (render::result_failed(render_result)) {
            log_render_result("render::load_image_texture_from_memory", render_result);
            return false;
        }
        runtime->textures.embedded.size =
            texture_sample_size(render::texture_size(runtime->textures.embedded.texture));

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        LiquidGlassSpec const style = liquid_glass_spec(runtime->state.theme);
        gui::ThemeDesc theme = gui::default_theme();
        configure_liquid_glass_theme(theme, runtime->font, style);
        gui::create_context(
            arena,
            {
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .clipboard_user_data = runtime,
            },
            runtime->ui_context
        );
        BASE_UNUSED(runtime->state.multiline_text_buffer.write_string(
            "Editable multiline textEditable multiline textEditable multiline textEditable "
            "multiline textEditable multiline textEditable multiline textEditable multiline "
            "textEditable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        UiRuntime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        void* trace
    ) -> gui::Frame {
        TRACE_SCOPE(trace, "ui_build");
        LiquidGlassSpec style = {};
        {
            TRACE_SCOPE(trace, "theme_setup");
            style = liquid_glass_spec(runtime->state.theme);
            gui::ThemeDesc theme = gui::default_theme();
            configure_liquid_glass_theme(theme, runtime->font, style);
            gui::set_theme(runtime->ui_context, theme);
        }

        gui::Frame ui = {};
        {
            TRACE_SCOPE(trace, "begin_ui_frame");
            ui = gui::begin_frame(
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
        }
        {
            TRACE_SCOPE(trace, "draw_ui");
            draw_ui(ui, runtime->state, style, runtime->textures, input, delta_time);
        }
        {
            TRACE_SCOPE(trace, "end_ui_frame");
            gui::end_frame(ui);
        }

        {
            TRACE_SCOPE(trace, "draw_command_recording");
            {
                TRACE_SCOPE(trace, "draw_begin_frame");
                draw::begin_frame(runtime->draw_context);
            }
            {
                TRACE_SCOPE(trace, "draw_backdrop");
                draw_liquid_glass_backdrop(
                    runtime->draw_context, window_size, runtime->state.theme
                );
            }
            {
                TRACE_SCOPE(trace, "gui_render_frame");
                gui::render_frame(ui, runtime->draw_context);
            }
            {
                TRACE_SCOPE(trace, "draw_end_frame");
                draw::end_frame(runtime->draw_context);
            }
        }
#if BASE_DEBUG
        trace_draw_command_counts(trace, runtime->draw_context);
#endif
        return ui;
    }

#undef trace_draw_command_counts
#undef TRACE_SCOPE

    struct ModuleRuntime {
        Arena arena = {};
        UiRuntime runtime = {};
    };

    [[nodiscard]] auto draw_command_counts(draw::Context context) -> DrawCommandCounts {
        return {
            .command_count = draw::command_count(context),
            .primitive_count = draw::primitive_command_count(context),
            .batch_count = draw::primitive_batch_count(context),
            .styled_rect_count = draw::styled_rect_command_count(context),
            .text_count = draw::text_command_count(context),
            .layer_count = draw::layer_command_count(context),
        };
    }

    [[nodiscard]] auto module_create(void* storage, void* user_data) -> bool {
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = new (storage) ModuleRuntime{};
        module->arena.init();
        if (!create_ui_runtime(
                module->arena,
                context->render_context,
                static_cast<HWND>(context->native_window),
                &module->runtime
            )) {
            destroy_ui_runtime(context->render_context, &module->runtime);
            module->~ModuleRuntime();
            return false;
        }
        return true;
    }

    auto module_destroy(void* storage, void* user_data) -> void {
        if (storage == nullptr) {
            return;
        }
        auto const* const context = static_cast<ModuleRuntimeContext const*>(user_data);
        auto* const module = static_cast<ModuleRuntime*>(storage);
        destroy_ui_runtime(context->render_context, &module->runtime);
        module->~ModuleRuntime();
    }

    [[nodiscard]] auto module_render_frame(
        void* storage,
        render::Context render_context,
        render::Window render_window,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> FrameResult {
        auto* const module = static_cast<ModuleRuntime*>(storage);
        uint64_t const state_hash_before = testbed_state_hash(module->runtime.state);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame =
            build_ui_commands(&module->runtime, window_size, input, delta_time, nullptr);
        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = liquid_glass_clear_color(module->runtime.state.theme);

        frame_result.render_result = draw::render_commands_to_window(
            module->runtime.draw_renderer, render_context, pass_desc, module->runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(module->runtime.draw_context);
        reset_thread_temp_arenas();
        frame_result.redraw_pending =
            frame_result.frame.redraw_requested() || module->runtime.state.selected_tab == 1u ||
            testbed_state_hash(module->runtime.state) != state_hash_before;
        return frame_result;
    }

    [[nodiscard]] auto ui_api_testbed_module_api() -> ModuleApi const* {
        static ModuleApi const api = {
            .hot_reload =
                {
                    .version = gui::HOT_RELOAD_API_VERSION,
                    .runtime_size = sizeof(ModuleRuntime),
                    .runtime_alignment = alignof(ModuleRuntime),
                    .create = module_create,
                    .destroy = module_destroy,
                },
            .render_frame = module_render_frame,
        };
        return &api;
    }
#else
    auto run_console_fallback() -> int {
        Arena arena = {};
        arena.init();

        TestbedState state = {};
        LiquidGlassSpec const style = liquid_glass_spec(state.theme);
        gui::ThemeDesc theme = gui::default_theme();
        configure_liquid_glass_theme(theme, {}, style);

        gui::Context ui_context = {};
        gui::create_context(arena, {.theme = &theme}, ui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        BASE_UNUSED(state.multiline_text_buffer.write_string(
            "Editable multiline text\nPress Enter for a new line\nTab inserts four spaces"
        ));
        int texture_storage = 0;
        TestbedTextures textures = {};
        textures.disk = {{&texture_storage}, {2.0f, 2.0f}};
        textures.embedded = textures.disk;
        gui::Frame ui =
            gui::begin_frame(ui_context, {.size = {640.0f, 400.0f}, .delta_time = 1.0f / 60.0f});
        draw_ui(ui, state, style, textures, gui::InputState{}, 1.0f / 60.0f);
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

} // namespace ui_api_testbed

#if defined(_WIN32) && defined(UI_API_TESTBED_MODULE)
GUI_HOT_RELOAD_EXPORT auto ui_api_testbed_get_module_api() -> ui_api_testbed::ModuleApi const* {
    return ui_api_testbed::ui_api_testbed_module_api();
}
#endif
