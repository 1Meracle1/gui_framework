#pragma once

#include <base/memory.h>
#include <cstdint>

namespace gui::render {

    enum class Backend : uint8_t {
        D3D11,
    };

    enum class Result : int8_t {
        OK = 0,
        OCCLUDED = 1,

        UNSUPPORTED_PLATFORM = -1,
        UNSUPPORTED_BACKEND = -2,
        INVALID_ARGUMENT = -3,
        OUT_OF_MEMORY = -4,
        DEVICE_CREATION_FAILED = -5,
        FACTORY_CREATION_FAILED = -6,
        WINDOW_CREATION_FAILED = -7,
        RENDER_TARGET_CREATION_FAILED = -8,
        RESIZE_FAILED = -9,
        PRESENT_FAILED = -10,
    };

    enum class PresentMode : uint8_t {
        IMMEDIATE,
        VSYNC,
    };

    struct SizeU32 {
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct Color {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    struct ContextDesc {
        Backend backend = Backend::D3D11;
        bool enable_debug_layer = false;
    };

    struct WindowDesc {
        // HWND on Windows. Kept opaque so this header does not include windows.h.
        void* native_window = nullptr;
        SizeU32 size = {};
        uint32_t buffer_count = 2u;
        PresentMode present_mode = PresentMode::VSYNC;
    };

    struct Context {
        void* handle = nullptr;
    };

    struct Window {
        void* handle = nullptr;
    };

    [[nodiscard]] auto result_succeeded(Result result) -> bool;
    [[nodiscard]] auto result_failed(Result result) -> bool;
    [[nodiscard]] auto result_name(Result result) -> char const*;
    [[nodiscard]] auto backend_name(Backend backend) -> char const*;
    [[nodiscard]] auto context_valid(Context context) -> bool;
    [[nodiscard]] auto window_valid(Window window) -> bool;

    [[nodiscard]] auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context)
        -> Result;
    auto destroy_context(Context* context) -> void;

    [[nodiscard]] auto
    create_window(Arena& arena, Context context, WindowDesc const& desc, Window* out_window)
        -> Result;
    auto destroy_window(Window* window) -> void;

    [[nodiscard]] auto resize_window(Context context, Window window, SizeU32 size) -> Result;
    [[nodiscard]] auto begin_frame(Context context) -> Result;
    [[nodiscard]] auto clear_window(Context context, Window window, Color color) -> Result;
    [[nodiscard]] auto present_window(Window window) -> Result;

    [[nodiscard]] auto window_size(Window window) -> SizeU32;

    [[nodiscard]] auto native_device(Context context) -> void*;
    [[nodiscard]] auto native_device_context(Context context) -> void*;
    [[nodiscard]] auto native_swap_chain(Window window) -> void*;
    [[nodiscard]] auto native_render_target_view(Window window) -> void*;

} // namespace gui::render
