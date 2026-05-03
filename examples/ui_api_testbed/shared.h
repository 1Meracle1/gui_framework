#pragma once

#include <base/config.h>
#include <cstddef>
#include <cstdint>
#include <gui/hot_reload_app.h>

namespace ui_api_testbed {

#if defined(_WIN32) && BASE_DEBUG
    inline constexpr bool HOT_RELOAD_ENABLED = true;
#else
    inline constexpr bool HOT_RELOAD_ENABLED = false;
#endif

    inline constexpr size_t MODULE_STORAGE_SIZE = 64u * 1024u;
    inline constexpr size_t MODULE_STORAGE_ALIGNMENT = 64u;
    inline constexpr StrRef MODULE_FILE_NAME = "ui_api_testbed_module.dll";
    inline constexpr uint32_t HOT_RELOAD_POLL_MS = 250u;

    using ModuleRuntimeContext = gui::HotReloadRuntimeContext;
    using DrawCommandCounts = gui::HotReloadDrawCommandCounts;
    using FrameResult = gui::HotReloadFrameResult;
    using ModuleRenderFrameFn = gui::HotReloadRenderFrameFn;
    using ModuleApi = gui::HotReloadAppApi;

} // namespace ui_api_testbed
