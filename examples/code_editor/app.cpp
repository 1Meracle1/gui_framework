#include "app.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "editor_model.h"
#include "editor_render.h"
#include "editor_theme.h"

#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <base/unicode.h>
#include <cstring>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <new>
#include <render/render.h>
#endif

namespace code_editor {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    struct Runtime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font ui_font = {};
        font_cache::Font editor_font = {};
        font_cache::Font icon_font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::Context ui_context = {};
        EditorState editor = {};
        void* native_window = nullptr;
        float char_width = 8.0f;
    };

    static auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    static auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    auto set_windows_clipboard_text(void* user_data, StrRef text) -> void {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* wide_source = nullptr;
        int wide_count = 0;
        if (!base::utf8_to_wide(text, *temp.arena(), wide_source, wide_count)) {
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
        std::memcpy(wide_text, wide_source, sizeof(wchar_t) * static_cast<size_t>(wide_count));
        wide_text[wide_count] = L'\0';
        GlobalUnlock(memory);

        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
        }
        CloseClipboard();
    }

    [[nodiscard]] auto get_windows_clipboard_text(void* user_data, Arena& arena) -> StrRef {
        HWND const hwnd = static_cast<HWND>(user_data);
        if (hwnd == nullptr || !OpenClipboard(hwnd)) {
            return {};
        }

        HANDLE const handle = GetClipboardData(CF_UNICODETEXT);
        if (handle == nullptr) {
            CloseClipboard();
            return {};
        }

        auto const* const wide_text = static_cast<wchar_t const*>(GlobalLock(handle));
        if (wide_text == nullptr) {
            CloseClipboard();
            return {};
        }

        int const wide_count = lstrlenW(wide_text);
        StrRef text = {};
        if (!base::wide_to_utf8(wide_text, wide_count, arena, text)) {
            GlobalUnlock(handle);
            CloseClipboard();
            return {};
        }

        GlobalUnlock(handle);
        CloseClipboard();
        return text;
    }

    auto destroy_runtime(render::Context render_context, Runtime* runtime) -> void {
        if (runtime == nullptr) {
            return;
        }
        if (gui::context_valid(runtime->ui_context)) {
            gui::destroy_context(runtime->ui_context);
        }
        if (draw::context_valid(runtime->draw_context)) {
            draw::destroy_context(runtime->draw_context);
        }
        if (draw::renderer_valid(runtime->draw_renderer)) {
            draw::destroy_renderer(render_context, runtime->draw_renderer);
        }
        if (font_cache::cache_valid(runtime->cache)) {
            font_cache::destroy_cache(runtime->cache);
        }
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->ui_font = {};
        runtime->editor_font = {};
        runtime->icon_font = {};
    }

    [[nodiscard]] auto
    create_runtime(Arena& arena, ModuleRuntimeContext const& context, Runtime* runtime) -> bool {
        render::Result render_result =
            draw::create_renderer(arena, context.render_context, {}, runtime->draw_renderer);
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
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->ui_font);
        font_cache::open_system_font(runtime->cache, "Segoe MDL2 Assets", runtime->icon_font);
        if (!font_cache::font_valid(runtime->icon_font)) {
            font_cache::open_system_font(runtime->cache, "Segoe Fluent Icons", runtime->icon_font);
        }
        if (!font_cache::font_valid(runtime->icon_font)) {
            runtime->icon_font = runtime->ui_font;
        }
        font_cache::open_system_font(runtime->cache, "Cascadia Mono", runtime->editor_font);
        if (!font_cache::font_valid(runtime->editor_font)) {
            font_cache::open_system_font(runtime->cache, "Consolas", runtime->editor_font);
        }
        if (!font_cache::font_valid(runtime->editor_font)) {
            runtime->editor_font = runtime->ui_font;
        }
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        Palette const palette = {};
        gui::ThemeDesc const theme = code_editor_theme(
            runtime->ui_font, palette, editor_scaled_font_size(runtime->editor, EDITOR_UI_FONT_SIZE)
        );
        gui::create_context(
            arena, {.initial_box_capacity = 1024u, .theme = &theme}, runtime->ui_context
        );
        runtime->native_window = context.native_window;
        init_editor(arena, runtime->editor, context.initial_text);
        if (!context.initial_file_name.empty()) {
            runtime->editor.current_file_name = context.initial_file_name;
        }
        runtime->editor.current_file_path = context.initial_file_path;
        runtime->editor.tree_root_name = context.tree_root_name;
        runtime->editor.tree_files = context.tree_files;
        runtime->editor.sidebar_visible = context.initial_sidebar_visible;
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        Runtime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time
    ) -> gui::Frame {
        process_editor_input(
            runtime->editor,
            input,
            {
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .user_data = runtime->native_window,
            }
        );
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        Palette const palette = {};
        gui::ThemeDesc const theme = code_editor_theme(
            runtime->ui_font, palette, editor_scaled_font_size(runtime->editor, EDITOR_UI_FONT_SIZE)
        );
        gui::set_theme(runtime->ui_context, theme);

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
        draw_editor_ui(
            ui,
            runtime->editor,
            runtime->ui_font,
            runtime->icon_font,
            palette,
            static_cast<float>(window_size.width),
            input
        );
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        gui::render_frame(ui, runtime->draw_context);
        if (!runtime->editor.file_search_open) {
            draw_editor_surface(
                runtime->draw_context,
                runtime->editor_font,
                runtime->editor,
                runtime->char_width,
                ui,
                input,
                palette
            );
        }
        draw::end_frame(runtime->draw_context);
        return ui;
    }

    struct ModuleRuntime {
        Arena arena = {};
        Runtime runtime = {};
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
        if (!create_runtime(module->arena, *context, &module->runtime)) {
            destroy_runtime(context->render_context, &module->runtime);
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
        destroy_runtime(context->render_context, &module->runtime);
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
        uint64_t const state_hash_before = editor_state_hash(module->runtime.editor);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame = build_ui_commands(&module->runtime, window_size, input, delta_time);
        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.05f, 0.07f, 0.09f, 1.0f};

        frame_result.render_result = draw::render_commands_to_window(
            module->runtime.draw_renderer, render_context, pass_desc, module->runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(module->runtime.draw_context);
        frame_result.redraw_pending =
            frame_result.frame.redraw_requested() ||
            editor_state_hash(module->runtime.editor) != state_hash_before;
        reset_thread_temp_arenas();
        return frame_result;
    }

    [[nodiscard]] auto code_editor_module_api() -> ModuleApi const* {
        static ModuleApi const API = {
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
        return &API;
    }
#else
    auto run_console_fallback() -> int {
        fmt::printf("code_editor: windowed editor example is Windows-only\n");
        return 0;
    }
#endif

} // namespace code_editor

#if defined(_WIN32) && defined(CODE_EDITOR_MODULE)
GUI_HOT_RELOAD_EXPORT auto code_editor_get_module_api() -> code_editor::ModuleApi const* {
    return code_editor::code_editor_module_api();
}
#endif
