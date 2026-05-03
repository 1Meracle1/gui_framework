#pragma once

#include <base/config.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && BASE_DEBUG
#define GUI_HOT_RELOAD_AVAILABLE 1
#else
#define GUI_HOT_RELOAD_AVAILABLE 0
#endif

#if defined(_WIN32)
#define GUI_HOT_RELOAD_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define GUI_HOT_RELOAD_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define GUI_HOT_RELOAD_EXPORT extern "C"
#endif

namespace gui {

    inline constexpr bool HOT_RELOAD_AVAILABLE = GUI_HOT_RELOAD_AVAILABLE != 0;
    inline constexpr uint32_t HOT_RELOAD_API_VERSION = 1u;
    inline constexpr size_t HOT_RELOAD_LOG_CAPACITY = 64u * 1024u;
    inline constexpr size_t HOT_RELOAD_LOG_LINE_COUNT = 1024u;
    inline constexpr uint32_t HOT_RELOAD_STATUS_HOLD_MS = 1000u;

    enum class HotReloadPhase : uint8_t {
        IDLE,
        COMPILING,
        RELOADING,
        COMPLETE,
        FAILED,
    };

    struct HotReloadLogLine {
        uint32_t offset = 0u;
        uint32_t size = 0u;
        bool is_stderr = false;
    };

    struct HotReloadStatus {
        HotReloadPhase phase = HotReloadPhase::IDLE;
        char const* text = nullptr;
        size_t text_size = 0u;
        HotReloadLogLine const* lines = nullptr;
        size_t line_count = 0u;
        bool truncated = false;
    };

    using HotReloadCreateFn = auto (*)(void* storage, void* user_data) -> bool;
    using HotReloadDestroyFn = auto (*)(void* storage, void* user_data) -> void;

    struct HotReloadApi {
        uint32_t version = 0u;
        size_t runtime_size = 0u;
        size_t runtime_alignment = 1u;
        HotReloadCreateFn create = nullptr;
        HotReloadDestroyFn destroy = nullptr;
    };

    struct HotReloadDesc {
        StrRef label = "hot_reload";
        StrRef source_dir = ".";
        StrRef binary_dir = ".";
        StrRef build_config = "Debug";
        StrRef build_target = {};
        StrRef api_export_name = {};
        StrRef module_file_name = {};
        StrRef module_copy_prefix = "hot_module";
        Slice<StrRef const> watched_files = {};
        size_t storage_size = 0u;
        size_t storage_alignment = 1u;
        void* user_data = nullptr;
    };

    struct HotReloadModule {
        void* handle = nullptr;
    };

    [[nodiscard]] auto
    hot_reload_api_valid(HotReloadApi const* api, size_t storage_size, size_t storage_alignment)
        -> bool;
    auto init_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc, Arena& arena)
        -> void;
    [[nodiscard]] auto load_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc)
        -> bool;
    [[nodiscard]] auto update_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc)
        -> bool;
    auto destroy_hot_reload_module(HotReloadModule* module, HotReloadDesc const& desc) -> void;
    [[nodiscard]] auto hot_reload_module_api(HotReloadModule const& module) -> void const*;
    [[nodiscard]] auto hot_reload_module_storage(HotReloadModule const& module) -> void*;
    [[nodiscard]] auto hot_reload_status(HotReloadModule const& module) -> HotReloadStatus;
    [[nodiscard]] auto hot_reload_status_visible(HotReloadModule const& module) -> bool;

} // namespace gui
