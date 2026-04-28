#pragma once

#include <base/assert.h>
#include <render/render.h>

namespace gui::render {

    struct ContextHeader {
        Backend backend = Backend::D3D11;
    };

    struct WindowHeader {
        Backend backend = Backend::D3D11;
    };

    [[nodiscard]] inline auto context_backend(Context context) -> Backend {
        ASSERT(context.handle != nullptr);
        return static_cast<ContextHeader const*>(context.handle)->backend;
    }

    [[nodiscard]] inline auto window_backend(Window window) -> Backend {
        ASSERT(window.handle != nullptr);
        return static_cast<WindowHeader const*>(window.handle)->backend;
    }

} // namespace gui::render
