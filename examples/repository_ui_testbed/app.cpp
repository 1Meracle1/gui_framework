#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "app.h"

#include "repo_data.h"
#include "ui_icons.h"
#include "ui_repository.h"
#include "ui_theme.h"

#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <draw/draw_renderer.h>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>
#include <render/render.h>
#include <repository_ui_testbed_embedded_codicons.h>
#include <windows.h>

namespace repository_ui_testbed {

#if defined(_WIN32)
    namespace draw = gui::draw;
    namespace font_cache = gui::font_cache;
    namespace font_provider = gui::font_provider;
    namespace render = gui::render;

    struct Runtime {
        font_provider::Context provider = {};
        font_cache::Cache cache = {};
        font_cache::Font font = {};
        font_cache::Font icon_font = {};
        draw::Context draw_context = {};
        draw::Renderer draw_renderer = {};
        gui::Context ui_context = {};
        StrRef icon_font_path = {};
        RepoTree tree = {};
        RepoDetails details = {};
        RepositorySection selected_section = RepositorySection::CODE;
        size_t selected_tab = 0u;
    };

    auto log_app_render_result(char const* operation, render::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, render::result_name(result));
    }

    auto log_app_font_result(char const* operation, font_provider::Result result) -> void {
        fmt::eprintf("%s failed: %s\n", operation, font_provider::result_name(result));
    }

    [[nodiscard]] auto write_embedded_icon_font(Arena& arena, Runtime* runtime) -> bool {
        ArenaTemp temp = begin_thread_temp_arena();
        char* const temp_dir = arena_alloc<char>(*temp.arena(), MAX_PATH);
        DWORD const temp_dir_capacity = static_cast<DWORD>(MAX_PATH);
        DWORD const temp_dir_size = GetTempPathA(temp_dir_capacity, temp_dir);
        if (temp_dir_size == 0u || temp_dir_size >= temp_dir_capacity) {
            return false;
        }

        char* const font_path = arena_alloc<char>(*temp.arena(), MAX_PATH);
        if (GetTempFileNameA(temp_dir, "gfi", 0u, font_path) == 0u) {
            return false;
        }

        HANDLE const file = CreateFileA(
            font_path, GENERIC_WRITE, 0u, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr
        );
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileA(font_path);
            return false;
        }

        DWORD const font_size = static_cast<DWORD>(repository_ui_testbed_assets::codicon_ttf_size);
        DWORD bytes_written = 0u;
        BOOL const write_ok = WriteFile(
            file, repository_ui_testbed_assets::codicon_ttf, font_size, &bytes_written, nullptr
        );
        CloseHandle(file);

        if (write_ok == FALSE || bytes_written != font_size) {
            DeleteFileA(font_path);
            return false;
        }

        runtime->icon_font_path = arena_copy_cstr(arena, font_path);
        return true;
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
        if (!runtime->icon_font_path.empty()) {
            DeleteFileA(runtime->icon_font_path.data());
            runtime->icon_font_path = {};
        }
        if (font_provider::context_valid(runtime->provider)) {
            font_provider::destroy_context(runtime->provider);
        }
        runtime->font = {};
        runtime->icon_font = {};
    }

    [[nodiscard]] auto
    create_runtime(Arena& arena, render::Context render_context, Runtime* runtime) -> bool {
        render::Result render_result =
            draw::create_renderer(arena, render_context, {}, runtime->draw_renderer);
        if (render::result_failed(render_result)) {
            log_app_render_result("draw::create_renderer", render_result);
            return false;
        }

        font_provider::Result font_result =
            font_provider::create_context(arena, {}, runtime->provider);
        if (font_provider::result_failed(font_result)) {
            log_app_font_result("font_provider::create_context", font_result);
            return false;
        }

        font_cache::create_cache(arena, runtime->provider, {}, runtime->cache);
        font_cache::open_system_font(runtime->cache, "Segoe UI", runtime->font);
        if (!write_embedded_icon_font(arena, runtime)) {
            fmt::eprintf("failed to write embedded Codicons font\n");
            return false;
        }
        font_cache::open_font_file(runtime->cache, runtime->icon_font_path, runtime->icon_font);

        draw::ContextDesc draw_desc = {};
        draw_desc.font_cache = runtime->cache;
        draw_desc.initial_command_capacity = 4096u;
        draw::create_context(arena, draw_desc, runtime->draw_context);

        RepositorySpec const spec = {};
        gui::ThemeDesc const theme = repo_theme(runtime->font, spec);
        gui::create_context(
            arena, {.initial_box_capacity = 2048u, .theme = &theme}, runtime->ui_context
        );
        load_repo_tree(arena, runtime->tree);
        load_repo_details(arena, runtime->tree, runtime->details);
        return true;
    }

    struct ModuleRuntime {
        Arena arena = {};
        Runtime runtime = {};
    };

    [[nodiscard]] auto runtime_state_hash(Runtime const& runtime) -> uint64_t {
        uint64_t hash = repo_tree_open_hash(runtime.tree);
        hash ^= static_cast<uint64_t>(runtime.selected_section) + 1u;
        hash *= 1099511628211ull;
        hash ^= runtime.selected_tab + 1u;
        hash *= 1099511628211ull;
        return hash;
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
        if (!create_runtime(module->arena, context->render_context, &module->runtime)) {
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
        Runtime& runtime = module->runtime;
        uint64_t const state_hash_before = runtime_state_hash(runtime);

        render::begin_frame(render_context);

        FrameResult frame_result = {};
        frame_result.frame = gui::begin_frame(
            runtime.ui_context,
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
        draw_repository_ui(
            frame_result.frame,
            runtime.icon_font,
            runtime.selected_section,
            runtime.selected_tab,
            runtime.tree,
            runtime.details
        );
        gui::end_frame(frame_result.frame);

        gui::BoxInfo const* const hit_box = frame_result.frame.hit_test(input.mouse_pos);
        frame_result.mouse_hit_id = hit_box != nullptr ? hit_box->id : gui::Id{};

        draw::begin_frame(runtime.draw_context);
        gui::render_frame(frame_result.frame, runtime.draw_context);
        draw_repository_icons(
            frame_result.frame,
            runtime.draw_context,
            runtime.selected_section,
            runtime.selected_tab,
            runtime.tree
        );
        draw::end_frame(runtime.draw_context);

        render::WindowRenderPassDesc pass_desc = {};
        pass_desc.window = render_window;
        pass_desc.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

        frame_result.render_result = draw::render_commands_to_window(
            runtime.draw_renderer, render_context, pass_desc, runtime.draw_context
        );
        frame_result.draw_counts = draw_command_counts(runtime.draw_context);
        frame_result.redraw_pending = frame_result.frame.redraw_requested() ||
                                      runtime_state_hash(runtime) != state_hash_before;
        reset_thread_temp_arenas();
        return frame_result;
    }

    [[nodiscard]] auto repository_ui_testbed_module_api() -> ModuleApi const* {
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
#endif

} // namespace repository_ui_testbed

#if defined(_WIN32) && defined(REPOSITORY_UI_TESTBED_MODULE)
GUI_HOT_RELOAD_EXPORT auto repository_ui_testbed_get_module_api()
    -> repository_ui_testbed::ModuleApi const* {
    return repository_ui_testbed::repository_ui_testbed_module_api();
}
#endif
