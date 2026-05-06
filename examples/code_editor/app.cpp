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
#include <base/slice.h>
#include <base/unicode.h>
#include <cstring>
#if defined(_WIN32)
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#endif

namespace code_editor {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    constexpr int SOURCE_CODE_PRO_FONT_ID = 102;
    constexpr int WINDOWS_RCDATA_ID = 10;
    constexpr float FILE_SEARCH_BACKDROP_BLUR_RADIUS = 14.0f;
    constexpr float FILE_SEARCH_BACKDROP_DIM_ALPHA = 0.12f;

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
        StrRef const* shared_tree_root_name = nullptr;
        Slice<FileTreeEntry>* shared_tree_files = nullptr;
        uint64_t const* shared_file_change_generation = nullptr;
        LspBridge const* lsp_bridge = nullptr;
        LspSendEditorRequestFn lsp_send_request = nullptr;
        void* lsp_user_data = nullptr;
        bool* app_close_requested = nullptr;
        bool* app_close_confirmed = nullptr;
        uint64_t file_change_generation = 0u;
        float char_width = 8.0f;
    };

    static auto log_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    static auto log_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto selected_font_backend() -> font_provider::Backend {
        char backend[32] = {};
        DWORD const size = GetEnvironmentVariableA(
            "CODE_EDITOR_FONT_BACKEND", backend, static_cast<DWORD>(sizeof(backend))
        );
        if (size != 0u && size < sizeof(backend) &&
            StrRef(backend, static_cast<size_t>(size)).equals_ignore_ascii_case("freetype")) {
            return font_provider::Backend::FREETYPE;
        }
#if defined(_WIN32)
        return font_provider::Backend::DWRITE;
#else
        return font_provider::Backend::FREETYPE;
#endif
    }

    [[nodiscard]] auto embedded_source_code_pro_font() -> Slice<uint8_t const> {
        HMODULE const module = GetModuleHandleW(nullptr);
        HRSRC const resource = FindResourceW(
            module, MAKEINTRESOURCEW(SOURCE_CODE_PRO_FONT_ID), MAKEINTRESOURCEW(WINDOWS_RCDATA_ID)
        );
        if (resource == nullptr) {
            return {};
        }
        HGLOBAL const loaded = LoadResource(module, resource);
        void const* const data = loaded != nullptr ? LockResource(loaded) : nullptr;
        DWORD const size = SizeofResource(module, resource);
        if (data == nullptr || size == 0u) {
            return {};
        }
        return {static_cast<uint8_t const*>(data), static_cast<size_t>(size)};
    }

    [[nodiscard]] static auto sync_shared_file_tree(Runtime& runtime) -> bool {
        if (runtime.shared_tree_root_name != nullptr) {
            runtime.editor.tree_root_name = *runtime.shared_tree_root_name;
        }
        if (runtime.shared_tree_files != nullptr) {
            runtime.editor.tree_files = *runtime.shared_tree_files;
        }
        if (runtime.shared_file_change_generation == nullptr) {
            return true;
        }
        uint64_t const generation = *runtime.shared_file_change_generation;
        if (generation == runtime.file_change_generation) {
            return false;
        }
        runtime.file_change_generation = generation;
        return true;
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
        draw::RendererDesc renderer_desc = {};
        renderer_desc.text_atlas_slot_count = 4096u;
        render::Result render_result = draw::create_renderer(
            arena, context.render_context, renderer_desc, runtime->draw_renderer
        );
        if (render::result_failed(render_result)) {
            log_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::ContextDesc font_desc = {};
        font_desc.backend = selected_font_backend();
        font_provider::Result font_result =
            font_provider::create_context(arena, font_desc, runtime->provider);
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
        Slice<uint8_t const> const source_code_pro = embedded_source_code_pro_font();
        if (!source_code_pro.empty()) {
            font_cache::open_font_data(runtime->cache, source_code_pro, runtime->editor_font);
        }
        ASSERT(font_cache::font_valid(runtime->editor_font));
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        Palette const palette = {};
        gui::ThemeDesc const theme =
            code_editor_theme(runtime->ui_font, palette, runtime->editor.font_size);
        gui::create_context(
            arena,
            {
                .initial_box_capacity = 1024u,
                .theme = &theme,
                .set_clipboard_text = set_windows_clipboard_text,
                .get_clipboard_text = get_windows_clipboard_text,
                .clipboard_user_data = context.native_window,
            },
            runtime->ui_context
        );
        runtime->native_window = context.native_window;
        runtime->shared_tree_root_name = context.shared_tree_root_name;
        runtime->shared_tree_files = context.shared_tree_files;
        runtime->shared_file_change_generation = context.shared_file_change_generation;
        runtime->lsp_bridge = context.lsp_bridge;
        runtime->lsp_send_request = context.lsp_send_request;
        runtime->lsp_user_data = context.lsp_user_data;
        runtime->app_close_requested = context.app_close_requested;
        runtime->app_close_confirmed = context.app_close_confirmed;
        init_editor(arena, runtime->editor, context.initial_text);
        runtime->editor.lsp_bridge = runtime->lsp_bridge;
        runtime->editor.lsp_send_request = runtime->lsp_send_request;
        runtime->editor.lsp_user_data = runtime->lsp_user_data;
        if (!context.initial_file_name.empty()) {
            runtime->editor.current_file_name = context.initial_file_name;
        }
        runtime->editor.current_file_path = context.initial_file_path;
        runtime->editor.tree_root_name = context.tree_root_name;
        runtime->editor.save_root_path = context.save_root_path;
        runtime->editor.tree_files = context.tree_files;
        runtime->editor.set_flag(EditorFlag::SIDEBAR_VISIBLE, context.initial_sidebar_visible);
        return true;
    }

    [[nodiscard]] auto build_ui_commands(
        Runtime* runtime,
        render::SizeU32 window_size,
        gui::InputState const& input,
        float delta_time,
        bool files_changed
    ) -> gui::Frame {
        if (files_changed) {
            update_open_file_changes(runtime->editor);
        }
        if (runtime->app_close_requested != nullptr && *runtime->app_close_requested) {
            runtime->editor.set_flag(EditorFlag::CLOSE_APP_REQUESTED, true);
            *runtime->app_close_requested = false;
        }
        bool const popup_open = editor_focused_pane_kind(runtime->editor) == EditorPaneKind::CODE &&
                                (runtime->editor.flag(EditorFlag::EXTERNAL_CHANGE_PENDING) ||
                                 runtime->editor.flag(EditorFlag::FILE_DELETED_ON_DISK) ||
                                 runtime->editor.close_intent != EditorCloseIntent::NONE);
        if (!popup_open) {
            update_editor_lsp_document(runtime->editor);
            process_editor_input(
                runtime->editor,
                input,
                {
                    .set_clipboard_text = set_windows_clipboard_text,
                    .get_clipboard_text = get_windows_clipboard_text,
                    .user_data = runtime->native_window,
                }
            );
            update_editor_lsp_document(runtime->editor);
        }
        runtime->char_width = std::max(
            1.0f, font_cache::text_advance(runtime->editor_font, runtime->editor.font_size, "M")
        );
        Palette const palette = {};
        gui::ThemeDesc const theme =
            code_editor_theme(runtime->ui_font, palette, runtime->editor.font_size);
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
            runtime->editor_font,
            runtime->icon_font,
            palette,
            static_cast<float>(window_size.width),
            static_cast<float>(window_size.height),
            runtime->char_width,
            input
        );
        gui::end_frame(ui);

        draw::begin_frame(runtime->draw_context);
        bool const search_open = runtime->editor.flag(EditorFlag::FILE_SEARCH_OPEN) ||
                                 runtime->editor.flag(EditorFlag::BUFFER_SEARCH_OPEN);
        if (search_open) {
            draw::LayerDesc backdrop = {};
            backdrop.bounds = {
                {0.0f, 0.0f},
                {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}
            };
            backdrop.filter_kind = draw::FilterKind::BLUR;
            backdrop.filter_radius = FILE_SEARCH_BACKDROP_BLUR_RADIUS;
            draw::push_layer(runtime->draw_context, backdrop);
        }
        gui::render_frame_base(ui, runtime->draw_context);
        if (!runtime->editor.flag(EditorFlag::SAVE_PATH_OPEN)) {
            draw_editor_surface(
                runtime->draw_context,
                runtime->editor_font,
                runtime->editor,
                runtime->char_width,
                ui,
                search_open || runtime->editor.lsp_popup == EditorLspPopupKind::RENAME
                    ? gui::InputState{}
                    : input,
                palette,
                !search_open
            );
        }
        if (search_open) {
            draw::pop_layer(runtime->draw_context);
            draw::draw_rect_filled(
                runtime->draw_context,
                {{0.0f, 0.0f},
                 {static_cast<float>(window_size.width), static_cast<float>(window_size.height)}},
                {0.0f, 0.0f, 0.0f, FILE_SEARCH_BACKDROP_DIM_ALPHA},
                0.0f
            );
        }
        gui::render_frame_floating(ui, runtime->draw_context);
        draw::end_frame(runtime->draw_context);
        return ui;
    }

    struct ModuleRuntime {
        Arena arena = {};
        Runtime runtime = {};
    };

    auto request_window_close(Runtime& runtime) -> void {
        if (!runtime.editor.flag(EditorFlag::CLOSE_APP_CONFIRMED)) {
            return;
        }
        runtime.editor.set_flag(EditorFlag::CLOSE_APP_CONFIRMED, false);
        if (runtime.app_close_confirmed != nullptr) {
            *runtime.app_close_confirmed = true;
        }
        if (runtime.native_window != nullptr) {
            PostMessageW(static_cast<HWND>(runtime.native_window), WM_CLOSE, 0u, 0l);
        }
    }

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
        bool const files_changed = sync_shared_file_tree(module->runtime);
        uint64_t const state_hash_before = editor_state_hash(module->runtime.editor);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame =
            build_ui_commands(&module->runtime, window_size, input, delta_time, files_changed);
        request_window_close(module->runtime);
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
