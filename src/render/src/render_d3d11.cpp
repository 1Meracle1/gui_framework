#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "render_d3d11.h"

#include <d3d11.h>
#include <dxgi.h>
#include <new>
#include <windows.h>

namespace gui::render::d3d11 {
    namespace {

        struct D3D11Context {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* device_context = nullptr;
            IDXGIFactory* factory = nullptr;
            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
        };

        struct D3D11Window {
            IDXGISwapChain* swap_chain = nullptr;
            ID3D11RenderTargetView* render_target_view = nullptr;
            SizeU32 size = {};
            PresentMode present_mode = PresentMode::VSYNC;
        };

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
            }
        }

        [[nodiscard]] auto context_from_handle(Context context) -> D3D11Context* {
            return static_cast<D3D11Context*>(context.handle);
        }

        [[nodiscard]] auto window_from_handle(Window window) -> D3D11Window* {
            return static_cast<D3D11Window*>(window.handle);
        }

        [[nodiscard]] auto handle_from_context(D3D11Context* context) -> Context {
            return Context{context};
        }

        [[nodiscard]] auto handle_from_window(D3D11Window* window) -> Window {
            return Window{window};
        }

        [[nodiscard]] auto feature_level_count(D3D_FEATURE_LEVEL const* begin,
                                               D3D_FEATURE_LEVEL const* end) -> UINT {
            return static_cast<UINT>(end - begin);
        }

        [[nodiscard]] auto try_create_device(D3D_DRIVER_TYPE driver_type,
                                             UINT creation_flags,
                                             D3D11Context* out_context) -> HRESULT {
            D3D_FEATURE_LEVEL levels_with_11_1[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };
            D3D_FEATURE_LEVEL levels_without_11_1[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0,
            };

            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* device_context = nullptr;
            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;

            HRESULT hr = D3D11CreateDevice(
                nullptr,
                driver_type,
                nullptr,
                creation_flags,
                levels_with_11_1,
                feature_level_count(levels_with_11_1,
                                    levels_with_11_1 +
                                        (sizeof(levels_with_11_1) / sizeof(levels_with_11_1[0u]))),
                D3D11_SDK_VERSION,
                &device,
                &feature_level,
                &device_context);

            if (hr == E_INVALIDARG) {
                release_com(device);
                release_com(device_context);
                hr = D3D11CreateDevice(
                    nullptr,
                    driver_type,
                    nullptr,
                    creation_flags,
                    levels_without_11_1,
                    feature_level_count(levels_without_11_1,
                                        levels_without_11_1 + (sizeof(levels_without_11_1) /
                                                               sizeof(levels_without_11_1[0u]))),
                    D3D11_SDK_VERSION,
                    &device,
                    &feature_level,
                    &device_context);
            }

            if (FAILED(hr)) {
                release_com(device);
                release_com(device_context);
                return hr;
            }

            out_context->device = device;
            out_context->device_context = device_context;
            out_context->feature_level = feature_level;
            return hr;
        }

        [[nodiscard]] auto init_factory(D3D11Context* context) -> bool {
            IDXGIDevice* dxgi_device = nullptr;
            IDXGIAdapter* adapter = nullptr;

            HRESULT hr = context->device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
            if (SUCCEEDED(hr)) {
                hr = dxgi_device->GetAdapter(&adapter);
            }
            if (SUCCEEDED(hr)) {
                hr = adapter->GetParent(IID_PPV_ARGS(&context->factory));
            }

            release_com(adapter);
            release_com(dxgi_device);
            return SUCCEEDED(hr) && context->factory != nullptr;
        }

        auto destroy_context_impl(D3D11Context* context) -> void {
            if (context == nullptr) {
                return;
            }

            if (context->device_context != nullptr) {
                context->device_context->ClearState();
            }

            release_com(context->factory);
            release_com(context->device_context);
            release_com(context->device);
        }

        [[nodiscard]] auto create_render_target(D3D11Context* context, D3D11Window* window)
            -> Result {
            ID3D11Texture2D* back_buffer = nullptr;
            HRESULT hr = window->swap_chain->GetBuffer(0u, IID_PPV_ARGS(&back_buffer));
            if (FAILED(hr)) {
                return Result::RENDER_TARGET_CREATION_FAILED;
            }

            hr = context->device->CreateRenderTargetView(
                back_buffer, nullptr, &window->render_target_view);
            release_com(back_buffer);

            if (FAILED(hr) || window->render_target_view == nullptr) {
                return Result::RENDER_TARGET_CREATION_FAILED;
            }

            return Result::OK;
        }

        auto release_render_target(D3D11Window* window) -> void {
            if (window != nullptr) {
                release_com(window->render_target_view);
            }
        }

        auto destroy_window_impl(D3D11Window* window) -> void {
            if (window == nullptr) {
                return;
            }

            release_render_target(window);
            release_com(window->swap_chain);
            window->size = {};
            window->present_mode = PresentMode::VSYNC;
        }

    } // namespace

    auto create_context(ContextDesc const& desc, Context* out_context) -> Result {
        D3D11Context* context = new (std::nothrow) D3D11Context();
        if (context == nullptr) {
            return Result::OUT_OF_MEMORY;
        }

        UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (desc.enable_debug_layer) {
            creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        HRESULT hr = try_create_device(D3D_DRIVER_TYPE_HARDWARE, creation_flags, context);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && desc.enable_debug_layer) {
            creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = try_create_device(D3D_DRIVER_TYPE_HARDWARE, creation_flags, context);
        }

        if (FAILED(hr)) {
            hr = try_create_device(D3D_DRIVER_TYPE_WARP, creation_flags, context);
            if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING &&
                (creation_flags & D3D11_CREATE_DEVICE_DEBUG) != 0u) {
                creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                hr = try_create_device(D3D_DRIVER_TYPE_WARP, creation_flags, context);
            }
        }

        if (FAILED(hr)) {
            destroy_context_impl(context);
            delete context;
            return Result::DEVICE_CREATION_FAILED;
        }

        if (!init_factory(context)) {
            destroy_context_impl(context);
            delete context;
            return Result::FACTORY_CREATION_FAILED;
        }

        *out_context = handle_from_context(context);
        return Result::OK;
    }

    auto destroy_context(Context* context) -> void {
        if (context == nullptr) {
            return;
        }

        D3D11Context* impl = context_from_handle(*context);
        destroy_context_impl(impl);
        delete impl;
        context->handle = nullptr;
    }

    auto create_window(Context context, WindowDesc const& desc, Window* out_window) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        if (context_impl == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        D3D11Window* window = new (std::nothrow) D3D11Window();
        if (window == nullptr) {
            return Result::OUT_OF_MEMORY;
        }

        DXGI_SWAP_CHAIN_DESC swap_desc = {};
        swap_desc.BufferCount = desc.buffer_count;
        swap_desc.BufferDesc.Width = desc.size.width;
        swap_desc.BufferDesc.Height = desc.size.height;
        swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferDesc.RefreshRate.Numerator = 60u;
        swap_desc.BufferDesc.RefreshRate.Denominator = 1u;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.OutputWindow = static_cast<HWND>(desc.native_window);
        swap_desc.SampleDesc.Count = 1u;
        swap_desc.SampleDesc.Quality = 0u;
        swap_desc.Windowed = TRUE;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = context_impl->factory->CreateSwapChain(
            context_impl->device, &swap_desc, &window->swap_chain);
        if (FAILED(hr) || window->swap_chain == nullptr) {
            delete window;
            return Result::WINDOW_CREATION_FAILED;
        }

        context_impl->factory->MakeWindowAssociation(swap_desc.OutputWindow, DXGI_MWA_NO_ALT_ENTER);

        Result const render_target_result = create_render_target(context_impl, window);
        if (result_failed(render_target_result)) {
            destroy_window_impl(window);
            delete window;
            return render_target_result;
        }

        window->size = desc.size;
        window->present_mode = desc.present_mode;
        *out_window = handle_from_window(window);
        return Result::OK;
    }

    auto destroy_window(Window* window) -> void {
        if (window == nullptr) {
            return;
        }

        D3D11Window* impl = window_from_handle(*window);
        destroy_window_impl(impl);
        delete impl;
        window->handle = nullptr;
    }

    auto resize_window(Context context, Window window, SizeU32 size) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Window* window_impl = window_from_handle(window);
        if (context_impl == nullptr || window_impl == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        context_impl->device_context->OMSetRenderTargets(0u, nullptr, nullptr);
        context_impl->device_context->Flush();
        release_render_target(window_impl);

        HRESULT const hr = window_impl->swap_chain->ResizeBuffers(
            0u, size.width, size.height, DXGI_FORMAT_UNKNOWN, 0u);
        if (FAILED(hr)) {
            return Result::RESIZE_FAILED;
        }

        Result const render_target_result = create_render_target(context_impl, window_impl);
        if (result_failed(render_target_result)) {
            return render_target_result;
        }

        window_impl->size = size;
        return Result::OK;
    }

    auto begin_frame(Context context) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        return context_impl != nullptr ? Result::OK : Result::INVALID_ARGUMENT;
    }

    auto clear_window(Context context, Window window, Color color) -> Result {
        D3D11Context* context_impl = context_from_handle(context);
        D3D11Window* window_impl = window_from_handle(window);
        if (context_impl == nullptr || window_impl == nullptr ||
            window_impl->render_target_view == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        float const clear_color[] = {color.r, color.g, color.b, color.a};
        context_impl->device_context->OMSetRenderTargets(
            1u, &window_impl->render_target_view, nullptr);
        context_impl->device_context->ClearRenderTargetView(window_impl->render_target_view,
                                                            clear_color);
        return Result::OK;
    }

    auto present_window(Window window) -> Result {
        D3D11Window* window_impl = window_from_handle(window);
        if (window_impl == nullptr || window_impl->swap_chain == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        UINT const sync_interval = window_impl->present_mode == PresentMode::VSYNC ? 1u : 0u;
        HRESULT const hr = window_impl->swap_chain->Present(sync_interval, 0u);
        if (hr == DXGI_STATUS_OCCLUDED) {
            return Result::OCCLUDED;
        }
        if (FAILED(hr)) {
            return Result::PRESENT_FAILED;
        }

        return Result::OK;
    }

    auto window_size(Window window) -> SizeU32 {
        D3D11Window const* const window_impl = window_from_handle(window);
        return window_impl != nullptr ? window_impl->size : SizeU32{};
    }

    auto native_device(Context context) -> void* {
        D3D11Context const* const context_impl = context_from_handle(context);
        return context_impl != nullptr ? context_impl->device : nullptr;
    }

    auto native_device_context(Context context) -> void* {
        D3D11Context const* const context_impl = context_from_handle(context);
        return context_impl != nullptr ? context_impl->device_context : nullptr;
    }

    auto native_swap_chain(Window window) -> void* {
        D3D11Window const* const window_impl = window_from_handle(window);
        return window_impl != nullptr ? window_impl->swap_chain : nullptr;
    }

    auto native_render_target_view(Window window) -> void* {
        D3D11Window const* const window_impl = window_from_handle(window);
        return window_impl != nullptr ? window_impl->render_target_view : nullptr;
    }

} // namespace gui::render::d3d11
