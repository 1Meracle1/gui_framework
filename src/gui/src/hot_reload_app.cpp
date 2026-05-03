#include <base/config.h>
#include <base/fmt.h>
#include <gui/hot_reload_app.h>

namespace gui {

    [[nodiscard]] auto hot_reload_app_api_valid(
        HotReloadAppApi const* api, size_t storage_size, size_t storage_alignment
    ) -> bool {
        return api != nullptr &&
               hot_reload_api_valid(&api->hot_reload, storage_size, storage_alignment) &&
               api->render_frame != nullptr;
    }

    auto
    init_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc, Arena& arena)
        -> void {
#if GUI_HOT_RELOAD_AVAILABLE
        init_hot_reload_module(&module->hot_reload, desc, arena);
#else
        module->storage = arena.allocate_bytes(desc.storage_size, desc.storage_alignment);
#endif
    }

    [[nodiscard]] auto load_hot_reload_app_module(
        HotReloadAppModule* module, HotReloadDesc const& desc, HotReloadAppApi const* direct_api
    ) -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        BASE_UNUSED(direct_api);
        return load_hot_reload_module(&module->hot_reload, desc);
#else
        module->api = direct_api;
        if (!hot_reload_app_api_valid(module->api, desc.storage_size, desc.storage_alignment)) {
            fmt::eprintf("%s: direct module API is incompatible\n", desc.label);
            return false;
        }
        module->runtime_valid = module->api->hot_reload.create(module->storage, desc.user_data);
        return module->runtime_valid;
#endif
    }

    [[nodiscard]] auto
    update_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc) -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        return update_hot_reload_module(&module->hot_reload, desc);
#else
        BASE_UNUSED(module);
        BASE_UNUSED(desc);
        return false;
#endif
    }

    auto destroy_hot_reload_app_module(HotReloadAppModule* module, HotReloadDesc const& desc)
        -> void {
#if GUI_HOT_RELOAD_AVAILABLE
        destroy_hot_reload_module(&module->hot_reload, desc);
#else
        if (module->runtime_valid) {
            module->api->hot_reload.destroy(module->storage, desc.user_data);
            module->runtime_valid = false;
        }
#endif
    }

    [[nodiscard]] auto hot_reload_app_module_api(HotReloadAppModule const& module)
        -> HotReloadAppApi const* {
#if GUI_HOT_RELOAD_AVAILABLE
        return static_cast<HotReloadAppApi const*>(hot_reload_module_api(module.hot_reload));
#else
        return module.api;
#endif
    }

    [[nodiscard]] auto hot_reload_app_module_storage(HotReloadAppModule const& module) -> void* {
#if GUI_HOT_RELOAD_AVAILABLE
        return hot_reload_module_storage(module.hot_reload);
#else
        return module.storage;
#endif
    }

    [[nodiscard]] auto hot_reload_app_module_status(HotReloadAppModule const& module)
        -> HotReloadStatus {
#if GUI_HOT_RELOAD_AVAILABLE
        return hot_reload_status(module.hot_reload);
#else
        BASE_UNUSED(module);
        return {};
#endif
    }

    [[nodiscard]] auto hot_reload_app_module_status_visible(HotReloadAppModule const& module)
        -> bool {
#if GUI_HOT_RELOAD_AVAILABLE
        return hot_reload_status_visible(module.hot_reload);
#else
        BASE_UNUSED(module);
        return false;
#endif
    }

} // namespace gui
