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

    struct BufferHeader {
        Backend backend = Backend::D3D11;
    };

    struct TextureHeader {
        Backend backend = Backend::D3D11;
    };

    struct SamplerHeader {
        Backend backend = Backend::D3D11;
    };

    struct ShaderHeader {
        Backend backend = Backend::D3D11;
    };

    struct PipelineHeader {
        Backend backend = Backend::D3D11;
    };

    struct BindGroupHeader {
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

    [[nodiscard]] inline auto buffer_backend(Buffer buffer) -> Backend {
        ASSERT(buffer.handle != nullptr);
        return static_cast<BufferHeader const*>(buffer.handle)->backend;
    }

    [[nodiscard]] inline auto texture_backend(Texture texture) -> Backend {
        ASSERT(texture.handle != nullptr);
        return static_cast<TextureHeader const*>(texture.handle)->backend;
    }

    [[nodiscard]] inline auto sampler_backend(Sampler sampler) -> Backend {
        ASSERT(sampler.handle != nullptr);
        return static_cast<SamplerHeader const*>(sampler.handle)->backend;
    }

    [[nodiscard]] inline auto shader_backend(Shader shader) -> Backend {
        ASSERT(shader.handle != nullptr);
        return static_cast<ShaderHeader const*>(shader.handle)->backend;
    }

    [[nodiscard]] inline auto pipeline_backend(Pipeline pipeline) -> Backend {
        ASSERT(pipeline.handle != nullptr);
        return static_cast<PipelineHeader const*>(pipeline.handle)->backend;
    }

    [[nodiscard]] inline auto bind_group_backend(BindGroup bind_group) -> Backend {
        ASSERT(bind_group.handle != nullptr);
        return static_cast<BindGroupHeader const*>(bind_group.handle)->backend;
    }

} // namespace gui::render
