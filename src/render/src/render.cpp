#include <base/config.h>
#include <render/render.h>

#if BASE_PLATFORM_WINDOWS
#include "render_d3d11.h"
#endif

namespace gui::render {

    auto result_succeeded(Result result) -> bool {
        return static_cast<int32_t>(result) >= 0;
    }

    auto result_failed(Result result) -> bool {
        return !result_succeeded(result);
    }

    auto result_name(Result result) -> char const* {
        switch (result) {
        case Result::OK:
            return "ok";
        case Result::OCCLUDED:
            return "occluded";
        case Result::UNSUPPORTED_PLATFORM:
            return "unsupported platform";
        case Result::UNSUPPORTED_BACKEND:
            return "unsupported backend";
        case Result::INVALID_ARGUMENT:
            return "invalid argument";
        case Result::OUT_OF_MEMORY:
            return "out of memory";
        case Result::DEVICE_CREATION_FAILED:
            return "device creation failed";
        case Result::FACTORY_CREATION_FAILED:
            return "factory creation failed";
        case Result::WINDOW_CREATION_FAILED:
            return "window creation failed";
        case Result::RENDER_TARGET_CREATION_FAILED:
            return "render target creation failed";
        case Result::RESIZE_FAILED:
            return "resize failed";
        case Result::PRESENT_FAILED:
            return "present failed";
        }

        return "unknown";
    }

    auto backend_name(Backend backend) -> char const* {
        switch (backend) {
        case Backend::D3D11:
            return "d3d11";
        }

        return "unknown";
    }

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto window_valid(Window window) -> bool {
        return window.handle != nullptr;
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context) -> Result {
        if (out_context == nullptr || out_context->handle != nullptr) {
            return Result::INVALID_ARGUMENT;
        }
        if (desc.backend != Backend::D3D11) {
            return Result::UNSUPPORTED_BACKEND;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_context(arena, desc, out_context);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(desc);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_context(Context* context) -> void {
        if (context == nullptr || context->handle == nullptr) {
            return;
        }

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_context(context);
#else
        context->handle = nullptr;
#endif
    }

    auto create_window(Arena& arena, Context context, WindowDesc const& desc, Window* out_window)
        -> Result {
        if (!context_valid(context) || out_window == nullptr || out_window->handle != nullptr ||
            desc.native_window == nullptr || desc.size.width == 0u || desc.size.height == 0u ||
            desc.buffer_count == 0u) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::create_window(arena, context, desc, out_window);
#else
        BASE_UNUSED(arena);
        BASE_UNUSED(context);
        BASE_UNUSED(desc);
        BASE_UNUSED(out_window);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto destroy_window(Window* window) -> void {
        if (window == nullptr || window->handle == nullptr) {
            return;
        }

#if BASE_PLATFORM_WINDOWS
        d3d11::destroy_window(window);
#else
        window->handle = nullptr;
#endif
    }

    auto resize_window(Context context, Window window, SizeU32 size) -> Result {
        if (!context_valid(context) || !window_valid(window) || size.width == 0u ||
            size.height == 0u) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::resize_window(context, window, size);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(window);
        BASE_UNUSED(size);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto begin_frame(Context context) -> Result {
        if (!context_valid(context)) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::begin_frame(context);
#else
        BASE_UNUSED(context);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto clear_window(Context context, Window window, Color color) -> Result {
        if (!context_valid(context) || !window_valid(window)) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::clear_window(context, window, color);
#else
        BASE_UNUSED(context);
        BASE_UNUSED(window);
        BASE_UNUSED(color);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto present_window(Window window) -> Result {
        if (!window_valid(window)) {
            return Result::INVALID_ARGUMENT;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::present_window(window);
#else
        BASE_UNUSED(window);
        return Result::UNSUPPORTED_PLATFORM;
#endif
    }

    auto window_size(Window window) -> SizeU32 {
        if (!window_valid(window)) {
            return {};
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::window_size(window);
#else
        BASE_UNUSED(window);
        return {};
#endif
    }

    auto native_device(Context context) -> void* {
        if (!context_valid(context)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_device(context);
#else
        BASE_UNUSED(context);
        return nullptr;
#endif
    }

    auto native_device_context(Context context) -> void* {
        if (!context_valid(context)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_device_context(context);
#else
        BASE_UNUSED(context);
        return nullptr;
#endif
    }

    auto native_swap_chain(Window window) -> void* {
        if (!window_valid(window)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_swap_chain(window);
#else
        BASE_UNUSED(window);
        return nullptr;
#endif
    }

    auto native_render_target_view(Window window) -> void* {
        if (!window_valid(window)) {
            return nullptr;
        }

#if BASE_PLATFORM_WINDOWS
        return d3d11::native_render_target_view(window);
#else
        BASE_UNUSED(window);
        return nullptr;
#endif
    }

} // namespace gui::render
