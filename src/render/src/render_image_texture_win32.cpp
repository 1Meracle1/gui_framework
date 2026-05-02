#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef NOGDI
#define NOGDI
#endif

#include <base/assert.h>
#include <base/memory.h>
#include <cstdint>
#include <limits>
#include <objbase.h>
#include <render/render.h>
#include <wincodec.h>
#include <windows.h>

namespace gui::render {
    namespace {

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
            }
        }

        class ComApartment final {
          public:
            auto init() -> bool {
                HRESULT const result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (result == RPC_E_CHANGED_MODE) {
                    return true;
                }
                if (FAILED(result)) {
                    return false;
                }
                m_initialized = true;
                return true;
            }

            ~ComApartment() {
                if (m_initialized) {
                    CoUninitialize();
                }
            }

            ComApartment() = default;
            ComApartment(ComApartment&&) = delete;
            ComApartment(ComApartment const&) = delete;
            auto operator=(ComApartment&&) -> ComApartment& = delete;
            auto operator=(ComApartment const&) -> ComApartment& = delete;

          private:
            bool m_initialized = false;
        };

        [[nodiscard]] auto text_size_to_int(StrRef text, int& out_size) -> bool {
            if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return false;
            }

            out_size = static_cast<int>(text.size());
            return true;
        }

        [[nodiscard]] auto utf8_to_wide(StrRef text, Arena& arena, wchar_t*& out_text) -> bool {
            int input_size = 0;
            if (!text_size_to_int(text, input_size) || input_size == 0) {
                return false;
            }

            int const wide_len = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0
            );
            if (wide_len <= 0) {
                return false;
            }

            wchar_t* const wide_text =
                arena_alloc<wchar_t>(arena, static_cast<size_t>(wide_len) + 1u);
            int const converted = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, wide_text, wide_len
            );
            if (converted != wide_len) {
                return false;
            }

            wide_text[wide_len] = L'\0';
            out_text = wide_text;
            return true;
        }

        [[nodiscard]] auto create_texture_from_wic_frame(
            Context context,
            IWICImagingFactory* factory,
            IWICBitmapFrameDecode* frame,
            Texture& out_texture
        ) -> Result {
            UINT width = 0u;
            UINT height = 0u;
            HRESULT result = frame->GetSize(&width, &height);
            if (FAILED(result) || width == 0u || height == 0u) {
                return Result::IMAGE_LOAD_FAILED;
            }

            uint64_t const bytes_per_row64 = static_cast<uint64_t>(width) * 4u;
            uint64_t const byte_size64 = bytes_per_row64 * static_cast<uint64_t>(height);
            if (bytes_per_row64 > std::numeric_limits<uint32_t>::max() ||
                byte_size64 > std::numeric_limits<uint32_t>::max()) {
                return Result::IMAGE_LOAD_FAILED;
            }

            IWICFormatConverter* converter = nullptr;
            result = factory->CreateFormatConverter(&converter);
            if (FAILED(result) || converter == nullptr) {
                return Result::IMAGE_LOAD_FAILED;
            }

            result = converter->Initialize(
                frame,
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom
            );
            if (FAILED(result)) {
                release_com(converter);
                return Result::IMAGE_LOAD_FAILED;
            }

            ArenaTemp temp = begin_thread_temp_arena();
            uint32_t const bytes_per_row = static_cast<uint32_t>(bytes_per_row64);
            uint32_t const byte_size = static_cast<uint32_t>(byte_size64);
            uint8_t* const pixels = arena_alloc<uint8_t>(*temp.arena(), byte_size);
            result = converter->CopyPixels(nullptr, bytes_per_row, byte_size, pixels);
            release_com(converter);
            if (FAILED(result)) {
                return Result::IMAGE_LOAD_FAILED;
            }

            TextureDesc desc = {};
            desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            desc.bytes_per_row = bytes_per_row;
            desc.rgba_pixels = pixels;
            return create_texture(context, desc, out_texture);
        }

    } // namespace

    auto load_image_texture_from_file(Context context, StrRef path, Texture& out_texture)
        -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_texture.handle == nullptr);

        ArenaTemp temp = begin_thread_temp_arena();
        wchar_t* wide_path = nullptr;
        if (!utf8_to_wide(path, *temp.arena(), wide_path)) {
            return Result::IMAGE_LOAD_FAILED;
        }

        ComApartment com;
        if (!com.init()) {
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICImagingFactory* factory = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)
        );
        if (FAILED(result) || factory == nullptr) {
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICBitmapDecoder* decoder = nullptr;
        result = factory->CreateDecoderFromFilename(
            wide_path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
        );
        if (FAILED(result) || decoder == nullptr) {
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        result = decoder->GetFrame(0u, &frame);
        if (FAILED(result) || frame == nullptr) {
            release_com(decoder);
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        Result const loaded = create_texture_from_wic_frame(context, factory, frame, out_texture);
        release_com(frame);
        release_com(decoder);
        release_com(factory);
        return loaded;
    }

    auto load_image_texture_from_memory(
        Context context, void const* bytes, size_t byte_count, Texture& out_texture
    ) -> Result {
        ASSERT(context_valid(context));
        ASSERT(out_texture.handle == nullptr);

        if (bytes == nullptr || byte_count == 0u ||
            byte_count > static_cast<size_t>(std::numeric_limits<DWORD>::max())) {
            return Result::IMAGE_LOAD_FAILED;
        }

        ComApartment com;
        if (!com.init()) {
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICImagingFactory* factory = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)
        );
        if (FAILED(result) || factory == nullptr) {
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICStream* stream = nullptr;
        result = factory->CreateStream(&stream);
        if (FAILED(result) || stream == nullptr) {
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        result = stream->InitializeFromMemory(
            const_cast<BYTE*>(static_cast<BYTE const*>(bytes)), static_cast<DWORD>(byte_count)
        );
        if (FAILED(result)) {
            release_com(stream);
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICBitmapDecoder* decoder = nullptr;
        result = factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder
        );
        if (FAILED(result) || decoder == nullptr) {
            release_com(stream);
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        result = decoder->GetFrame(0u, &frame);
        if (FAILED(result) || frame == nullptr) {
            release_com(decoder);
            release_com(stream);
            release_com(factory);
            return Result::IMAGE_LOAD_FAILED;
        }

        Result const loaded = create_texture_from_wic_frame(context, factory, frame, out_texture);
        release_com(frame);
        release_com(decoder);
        release_com(stream);
        release_com(factory);
        return loaded;
    }

} // namespace gui::render
