#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "font_provider_platform.h"

#include <algorithm>
#include <base/config.h>
#include <base/memory.h>
#include <base/unicode.h>
#include <cmath>
#include <cstring>
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <limits>
#include <windows.h>

namespace gui::font_provider::platform::dwrite {
    namespace {

        constexpr StrRef DEFAULT_FONT_FAMILY = "Segoe UI";
        constexpr DWRITE_MEASURING_MODE TEXT_MEASURING_MODE = DWRITE_MEASURING_MODE_GDI_NATURAL;
        constexpr DWRITE_MEASURING_MODE TEXT_BITMAP_MEASURING_MODE = DWRITE_MEASURING_MODE_NATURAL;
        constexpr DWRITE_RENDERING_MODE SHARP_RENDERING_MODE = DWRITE_RENDERING_MODE_GDI_NATURAL;
        constexpr DWRITE_RENDERING_MODE SMOOTH_RENDERING_MODE =
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        constexpr DWRITE_GRID_FIT_MODE TEXT_GRID_FIT_MODE = DWRITE_GRID_FIT_MODE_ENABLED;
        constexpr DWRITE_TEXT_ANTIALIAS_MODE TEXT_ANTIALIAS_MODE =
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        constexpr DWRITE_TEXTURE_TYPE TEXT_ALPHA_BOUNDS_TYPE = DWRITE_TEXTURE_ALIASED_1x1;
        constexpr LONG GLYPH_RASTER_PADDING = 1;
        constexpr float TEXT_PADDING = 2.0f;
        constexpr float POINTS_TO_DIPS = 96.0f / 72.0f;

        struct StaticFontData;
        struct StaticFontFileLoader;

        struct StaticFontFileStream final : IDWriteFontFileStream {
            StaticFontData* font_data = nullptr;

            auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
            auto STDMETHODCALLTYPE AddRef() -> ULONG override;
            auto STDMETHODCALLTYPE Release() -> ULONG override;
            auto STDMETHODCALLTYPE ReadFileFragment(
                void const** fragment_start,
                UINT64 file_offset,
                UINT64 fragment_size,
                void** fragment_context
            ) -> HRESULT override;
            auto STDMETHODCALLTYPE ReleaseFileFragment(void* fragment_context) -> void override;
            auto STDMETHODCALLTYPE GetFileSize(UINT64* file_size) -> HRESULT override;
            auto STDMETHODCALLTYPE GetLastWriteTime(UINT64* last_write_time) -> HRESULT override;
        };

        struct StaticFontData {
            uint8_t const* data = nullptr;
            UINT64 size = 0u;
            StaticFontFileStream stream = {};
        };

        struct StaticFontFileLoader final : IDWriteFontFileLoader {
            auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
            auto STDMETHODCALLTYPE AddRef() -> ULONG override;
            auto STDMETHODCALLTYPE Release() -> ULONG override;
            auto STDMETHODCALLTYPE CreateStreamFromKey(
                void const* font_file_ref_key,
                UINT32 font_file_ref_key_size,
                IDWriteFontFileStream** font_file_stream
            ) -> HRESULT override;
        };

        struct ContextImpl {
            Arena* arena = nullptr;
            IDWriteFactory* factory = nullptr;
            IDWriteFactory2* factory2 = nullptr;
            StaticFontFileLoader* static_loader = nullptr;
            IDWriteGdiInterop* gdi_interop = nullptr;
            IDWriteRenderingParams* sharp_rendering_params = nullptr;
            IDWriteRenderingParams* smooth_rendering_params = nullptr;
            IDWriteBitmapRenderTarget* bitmap_target = nullptr;
            IDWriteBitmapRenderTarget1* bitmap_target1 = nullptr;
        };

        struct FontImpl {
            ContextImpl* context = nullptr;
            IDWriteFontFile* font_file = nullptr;
            IDWriteFontFace* font_face = nullptr;
            StaticFontData* static_data = nullptr;
        };

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
            }
        }

        [[nodiscard]] auto
        query_com_interface(REFIID riid, void** object, void* self, IID const& interface_id)
            -> HRESULT {
            ASSERT(object != nullptr);

            if (IsEqualGUID(riid, __uuidof(IUnknown)) || IsEqualGUID(riid, interface_id)) {
                *object = self;
                return S_OK;
            }

            *object = nullptr;
            return E_NOINTERFACE;
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::QueryInterface(REFIID riid, void** object)
            -> HRESULT {
            return query_com_interface(
                riid,
                object,
                static_cast<IDWriteFontFileStream*>(this),
                __uuidof(IDWriteFontFileStream)
            );
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::AddRef() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::Release() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::ReadFileFragment(
            void const** fragment_start,
            UINT64 file_offset,
            UINT64 fragment_size,
            void** fragment_context
        ) -> HRESULT {
            ASSERT(font_data != nullptr);
            ASSERT(fragment_start != nullptr);
            ASSERT(fragment_context != nullptr);

            if (file_offset > font_data->size || fragment_size > font_data->size - file_offset) {
                return E_FAIL;
            }

            *fragment_start = font_data->data + static_cast<size_t>(file_offset);
            *fragment_context = nullptr;
            return S_OK;
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::ReleaseFileFragment(void* fragment_context)
            -> void {
            BASE_UNUSED(fragment_context);
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::GetFileSize(UINT64* file_size) -> HRESULT {
            ASSERT(font_data != nullptr);
            ASSERT(file_size != nullptr);

            *file_size = font_data->size;
            return S_OK;
        }

        auto STDMETHODCALLTYPE StaticFontFileStream::GetLastWriteTime(UINT64* last_write_time)
            -> HRESULT {
            ASSERT(last_write_time != nullptr);

            *last_write_time = 0u;
            return S_OK;
        }

        auto STDMETHODCALLTYPE StaticFontFileLoader::QueryInterface(REFIID riid, void** object)
            -> HRESULT {
            return query_com_interface(
                riid,
                object,
                static_cast<IDWriteFontFileLoader*>(this),
                __uuidof(IDWriteFontFileLoader)
            );
        }

        auto STDMETHODCALLTYPE StaticFontFileLoader::AddRef() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE StaticFontFileLoader::Release() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE StaticFontFileLoader::CreateStreamFromKey(
            void const* font_file_ref_key,
            UINT32 font_file_ref_key_size,
            IDWriteFontFileStream** font_file_stream
        ) -> HRESULT {
            ASSERT(font_file_ref_key != nullptr);
            ASSERT(font_file_ref_key_size == sizeof(StaticFontData*));
            ASSERT(font_file_stream != nullptr);

            BASE_UNUSED(font_file_ref_key_size);

            StaticFontData* const font_data =
                *static_cast<StaticFontData* const*>(font_file_ref_key);
            ASSERT(font_data != nullptr);

            *font_file_stream = &font_data->stream;
            return S_OK;
        }

        auto unregister_static_font_loader(ContextImpl* context) -> void {
            if (context->factory != nullptr && context->static_loader != nullptr) {
                context->factory->UnregisterFontFileLoader(context->static_loader);
            }
        }

        [[nodiscard]] auto create_text_rendering_params(ContextImpl* context) -> HRESULT {
            ASSERT(context != nullptr);
            ASSERT(context->factory != nullptr);
            ASSERT(context->factory2 != nullptr);

            IDWriteRenderingParams* base_params = nullptr;
            HRESULT hr = context->factory->CreateRenderingParams(&base_params);
            if (FAILED(hr)) {
                return hr;
            }

            FLOAT const enhanced_contrast = base_params->GetEnhancedContrast();
            release_com(base_params);

            IDWriteRenderingParams2* sharp_params = nullptr;
            hr = context->factory2->CreateCustomRenderingParams(
                1.0f,
                enhanced_contrast,
                enhanced_contrast,
                0.0f,
                DWRITE_PIXEL_GEOMETRY_FLAT,
                SHARP_RENDERING_MODE,
                TEXT_GRID_FIT_MODE,
                &sharp_params
            );
            context->sharp_rendering_params = sharp_params;
            if (FAILED(hr)) {
                return hr;
            }

            IDWriteRenderingParams2* smooth_params = nullptr;
            hr = context->factory2->CreateCustomRenderingParams(
                1.0f,
                0.0f,
                0.0f,
                0.0f,
                DWRITE_PIXEL_GEOMETRY_FLAT,
                SMOOTH_RENDERING_MODE,
                TEXT_GRID_FIT_MODE,
                &smooth_params
            );
            context->smooth_rendering_params = smooth_params;
            return hr;
        }

        [[nodiscard]] auto context_from_handle(Context context) -> ContextImpl* {
            return static_cast<ContextImpl*>(context.handle);
        }

        [[nodiscard]] auto font_from_handle(Font font) -> FontImpl* {
            return static_cast<FontImpl*>(font.handle);
        }

        [[nodiscard]] auto text_size_to_int(StrRef text, int& out_size) -> bool {
            if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return false;
            }

            out_size = static_cast<int>(text.size());
            return true;
        }

        [[nodiscard]] auto utf8_to_wide(StrRef text, Arena& arena, wchar_t*& out_text, int& out_len)
            -> bool {
            int input_size = 0;
            if (!text_size_to_int(text, input_size)) {
                return false;
            }

            if (text.empty()) {
                wchar_t* const wide_text = arena_alloc<wchar_t>(arena, 1u);
                wide_text[0] = L'\0';
                out_text = wide_text;
                out_len = 0;
                return true;
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
            out_len = wide_len;
            return true;
        }

        [[nodiscard]] auto dwrite_font_size(float size) -> float;
        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float;

        [[nodiscard]] auto font_handle(FontImpl* font) -> Font {
            return {font, Backend::DWRITE};
        }

        [[nodiscard]] auto create_font_impl(Arena& arena, ContextImpl* context) -> FontImpl* {
            ASSERT(context != nullptr);

            FontImpl* const font = arena_new<FontImpl>(arena);
            font->context = context;
            return font;
        }

        auto destroy_font_impl(FontImpl* font) -> void {
            release_com(font->font_face);
            release_com(font->font_file);
            font->static_data = nullptr;
            font->context = nullptr;
        }

        auto
        open_system_font(Arena& arena, ContextImpl* context, StrRef family_name, Font& out_font)
            -> void {
            FontImpl* const font = create_font_impl(arena, context);

            ArenaTemp temp = begin_thread_temp_arena();
            wchar_t* wide_family = nullptr;
            int wide_family_len = 0;
            StrRef const selected_family = family_name.empty() ? DEFAULT_FONT_FAMILY : family_name;
            ASSERT(utf8_to_wide(selected_family, *temp.arena(), wide_family, wide_family_len));
            BASE_UNUSED(wide_family_len);

            IDWriteFontCollection* collection = nullptr;
            IDWriteFontFamily* family = nullptr;
            IDWriteFont* dwrite_font = nullptr;
            UINT32 family_index = 0u;
            BOOL family_exists = FALSE;

            HRESULT hr = context->factory->GetSystemFontCollection(&collection, FALSE);
            if (SUCCEEDED(hr)) {
                hr = collection->FindFamilyName(wide_family, &family_index, &family_exists);
            }
            if (SUCCEEDED(hr) && family_exists != FALSE) {
                hr = collection->GetFontFamily(family_index, &family);
            }
            if (SUCCEEDED(hr) && family != nullptr) {
                hr = family->GetFirstMatchingFont(
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STRETCH_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    &dwrite_font
                );
            }
            if (SUCCEEDED(hr) && dwrite_font != nullptr) {
                hr = dwrite_font->CreateFontFace(&font->font_face);
            }
            release_com(dwrite_font);
            release_com(family);
            release_com(collection);

            ASSERT(SUCCEEDED(hr));
            ASSERT(family_exists != FALSE);
            ASSERT(font->font_face != nullptr);

            out_font.handle = font;
        }

        auto open_file_font(Arena& arena, ContextImpl* context, StrRef file_path, Font& out_font)
            -> void {
            ASSERT(!file_path.empty());

            FontImpl* const font = create_font_impl(arena, context);

            ArenaTemp temp = begin_thread_temp_arena();
            wchar_t* wide_path = nullptr;
            int wide_path_len = 0;
            ASSERT(utf8_to_wide(file_path, *temp.arena(), wide_path, wide_path_len));
            BASE_UNUSED(wide_path_len);

            HRESULT hr =
                context->factory->CreateFontFileReference(wide_path, nullptr, &font->font_file);
            BOOL supported = FALSE;
            DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
            DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
            UINT32 face_count = 0u;
            if (SUCCEEDED(hr)) {
                hr = font->font_file->Analyze(&supported, &file_type, &face_type, &face_count);
            }
            if (SUCCEEDED(hr) && supported != FALSE && face_count != 0u) {
                IDWriteFontFile* font_files[] = {font->font_file};
                hr = context->factory->CreateFontFace(
                    face_type, 1u, font_files, 0u, DWRITE_FONT_SIMULATIONS_NONE, &font->font_face
                );
            }
            BASE_UNUSED(file_type);
            ASSERT(SUCCEEDED(hr));
            ASSERT(supported != FALSE);
            ASSERT(face_count != 0u);
            ASSERT(font->font_face != nullptr);

            out_font.handle = font;
        }

        auto open_data_font(
            Arena& arena, ContextImpl* context, Slice<uint8_t const> data, Font& out_font
        ) -> void {
            ASSERT(!data.empty());
            ASSERT(context->static_loader != nullptr);

            uint8_t* const font_bytes = arena_alloc<uint8_t>(arena, data.size());
            std::memcpy(font_bytes, data.data(), data.size());

            StaticFontData* const static_data = arena_new<StaticFontData>(arena);
            static_data->data = font_bytes;
            static_data->size = static_cast<UINT64>(data.size());
            static_data->stream.font_data = static_data;

            FontImpl* const font = create_font_impl(arena, context);
            font->static_data = static_data;

            StaticFontData* const key = static_data;
            HRESULT hr = context->factory->CreateCustomFontFileReference(
                &key, sizeof(key), context->static_loader, &font->font_file
            );
            BOOL supported = FALSE;
            DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
            DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
            UINT32 face_count = 0u;
            if (SUCCEEDED(hr)) {
                hr = font->font_file->Analyze(&supported, &file_type, &face_type, &face_count);
            }
            if (SUCCEEDED(hr) && supported != FALSE && face_count != 0u) {
                IDWriteFontFile* font_files[] = {font->font_file};
                hr = context->factory->CreateFontFace(
                    face_type, 1u, font_files, 0u, DWRITE_FONT_SIMULATIONS_NONE, &font->font_face
                );
            }
            BASE_UNUSED(file_type);
            ASSERT(SUCCEEDED(hr));
            ASSERT(supported != FALSE);
            ASSERT(face_count != 0u);
            ASSERT(font->font_face != nullptr);

            out_font.handle = font;
        }

        [[nodiscard]] auto dwrite_font_size(float size) -> float {
            return size * POINTS_TO_DIPS;
        }

        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float {
            ASSERT(metrics.designUnitsPerEm != 0u);
            return dwrite_font_size(size) / static_cast<float>(metrics.designUnitsPerEm);
        }

        [[nodiscard]] auto ceil_u32(float value, uint32_t& out_value) -> bool {
            if (!(value >= 0.0f) ||
                value > static_cast<float>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }

            out_value = static_cast<uint32_t>(std::ceil(value));
            return true;
        }

        [[nodiscard]] auto checked_pixel_size(
            uint32_t width, uint32_t height, uint32_t bytes_per_pixel, size_t& out_size
        ) -> bool {
            ASSERT(bytes_per_pixel != 0u);
            if (height != 0u &&
                static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height) {
                return false;
            }

            size_t const pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
            if (pixels > std::numeric_limits<size_t>::max() / bytes_per_pixel) {
                return false;
            }

            out_size = pixels * bytes_per_pixel;
            return true;
        }

        [[nodiscard]] auto glyph_phase(uint8_t phase) -> float {
            return static_cast<float>(phase % GLYPH_RASTER_PHASE_COUNT) /
                   static_cast<float>(GLYPH_RASTER_PHASE_COUNT);
        }

        [[nodiscard]] auto rendering_mode(RasterPolicy raster_policy) -> DWRITE_RENDERING_MODE {
            return raster_policy == RasterPolicy::SMOOTH_HINTED ? SMOOTH_RENDERING_MODE
                                                                : SHARP_RENDERING_MODE;
        }

        [[nodiscard]] auto rendering_params(ContextImpl* context, RasterPolicy raster_policy)
            -> IDWriteRenderingParams* {
            return raster_policy == RasterPolicy::SMOOTH_HINTED ? context->smooth_rendering_params
                                                                : context->sharp_rendering_params;
        }

        auto inflate_bounds(RECT& bounds) -> void {
            bounds.left -= GLYPH_RASTER_PADDING;
            bounds.top -= GLYPH_RASTER_PADDING;
            bounds.right += GLYPH_RASTER_PADDING;
            bounds.bottom += GLYPH_RASTER_PADDING;
        }

        [[nodiscard]] auto
        ensure_bitmap_target(ContextImpl* context, uint32_t width, uint32_t height) -> HRESULT {
            ASSERT(context != nullptr);
            ASSERT(context->gdi_interop != nullptr);

            if (context->bitmap_target == nullptr) {
                HRESULT hr = context->gdi_interop->CreateBitmapRenderTarget(
                    nullptr, width, height, &context->bitmap_target
                );
                if (FAILED(hr)) {
                    return hr;
                }
                hr = context->bitmap_target->QueryInterface(
                    __uuidof(IDWriteBitmapRenderTarget1),
                    reinterpret_cast<void**>(&context->bitmap_target1)
                );
                if (FAILED(hr)) {
                    release_com(context->bitmap_target);
                    return hr;
                }
            } else {
                SIZE size = {};
                HRESULT hr = context->bitmap_target->GetSize(&size);
                if (FAILED(hr)) {
                    return hr;
                }
                if (static_cast<uint32_t>(size.cx) != width ||
                    static_cast<uint32_t>(size.cy) != height) {
                    HRESULT const resize_hr = context->bitmap_target->Resize(width, height);
                    if (FAILED(resize_hr)) {
                        return resize_hr;
                    }
                }
            }

            HRESULT const pixels_hr = context->bitmap_target->SetPixelsPerDip(1.0f);
            if (FAILED(pixels_hr)) {
                return pixels_hr;
            }

            return context->bitmap_target1->SetTextAntialiasMode(TEXT_ANTIALIAS_MODE);
        }

        [[nodiscard]] auto bitmap_target_bits(
            IDWriteBitmapRenderTarget* target,
            uint8_t*& out_bits,
            uint32_t& out_stride,
            uint32_t& out_bytes_per_pixel,
            bool& out_top_down
        ) -> bool {
            HDC const hdc = target->GetMemoryDC();
            HGDIOBJ const bitmap = GetCurrentObject(hdc, OBJ_BITMAP);
            DIBSECTION section = {};
            if (GetObjectW(bitmap, sizeof(section), &section) != sizeof(section)) {
                return false;
            }

            out_bits = static_cast<uint8_t*>(section.dsBm.bmBits);
            out_stride = static_cast<uint32_t>(std::abs(section.dsBm.bmWidthBytes));
            out_bytes_per_pixel = static_cast<uint32_t>(section.dsBm.bmBitsPixel) / 8u;
            out_top_down = section.dsBmih.biHeight > 0;
            return out_bits != nullptr && out_stride != 0u &&
                   (out_bytes_per_pixel == 3u || out_bytes_per_pixel == 4u);
        }

        auto clear_bitmap_target(IDWriteBitmapRenderTarget* target) -> void {
            uint8_t* bits = nullptr;
            uint32_t stride = 0u;
            uint32_t bytes_per_pixel = 0u;
            bool top_down = true;
            ASSERT(bitmap_target_bits(target, bits, stride, bytes_per_pixel, top_down));
            BASE_UNUSED(bytes_per_pixel);
            BASE_UNUSED(top_down);
            SIZE size = {};
            HRESULT const hr = target->GetSize(&size);
            ASSERT(SUCCEEDED(hr));
            std::memset(bits, 0, static_cast<size_t>(stride) * static_cast<size_t>(size.cy));
            BASE_UNUSED(hr);
        }

        auto bitmap_target_to_alpha(
            IDWriteBitmapRenderTarget* target, uint32_t width, uint32_t height, uint8_t* pixels
        ) -> void {
            uint8_t* bits = nullptr;
            uint32_t stride = 0u;
            uint32_t bytes_per_pixel = 0u;
            bool top_down = true;
            ASSERT(bitmap_target_bits(target, bits, stride, bytes_per_pixel, top_down));

            for (uint32_t y = 0u; y < height; ++y) {
                uint32_t const source_y = top_down ? y : height - y - 1u;
                uint8_t const* const src = bits + (static_cast<size_t>(source_y) * stride);
                uint8_t* const dst = pixels + (static_cast<size_t>(y) * width);
                for (uint32_t x = 0u; x < width; ++x) {
                    uint8_t const* const pixel = src + (static_cast<size_t>(x) * bytes_per_pixel);
                    DEBUG_ASSERT(pixel[0u] == pixel[1u] && pixel[1u] == pixel[2u]);
                    dst[x] = pixel[1u];
                }
            }
        }

        [[nodiscard]] auto codepoint_glyph(FontImpl* impl, uint32_t codepoint) -> uint16_t {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);

            UINT32 const dwrite_codepoint = codepoint;
            UINT16 glyph_index = 0u;
            HRESULT const hr =
                impl->font_face->GetGlyphIndices(&dwrite_codepoint, 1u, &glyph_index);
            ASSERT(SUCCEEDED(hr));
            BASE_UNUSED(hr);
            return glyph_index;
        }

        [[nodiscard]] auto glyph_advance(FontImpl* impl, float size, uint16_t glyph_index)
            -> float {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);

            DWRITE_FONT_METRICS font_metrics = {};
            impl->font_face->GetMetrics(&font_metrics);

            DWRITE_GLYPH_METRICS glyph_metrics = {};
            HRESULT const hr = impl->font_face->GetGdiCompatibleGlyphMetrics(
                dwrite_font_size(size), 1.0f, nullptr, TRUE, &glyph_index, 1u, &glyph_metrics, FALSE
            );
            ASSERT(SUCCEEDED(hr));
            BASE_UNUSED(hr);
            return static_cast<float>(glyph_metrics.advanceWidth) *
                   metrics_scale(font_metrics, size);
        }

        auto shape_text(FontImpl* impl, float size, StrRef text, Arena& arena, ShapedText& out_text)
            -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);

            out_text = {};
            if (text.empty()) {
                return;
            }

            ShapedGlyph* const glyphs = arena_alloc<ShapedGlyph>(arena, text.size());
            float pen_x = 0.0f;
            size_t glyph_count = 0u;

            size_t offset = 0u;
            while (offset < text.size()) {
                base::Utf8DecodeResult decoded = base::utf8_decode(text, offset);
                if (!decoded.ok) {
                    decoded = {'?', 1u, true};
                }

                uint16_t const glyph_index = codepoint_glyph(impl, decoded.codepoint);
                float const advance = glyph_advance(impl, size, glyph_index);
                ShapedGlyph& glyph = glyphs[glyph_count];
                glyph = {};
                glyph.font = font_handle(impl);
                glyph.glyph_index = glyph_index;
                glyph.size = size;
                glyph.x = pen_x;
                glyph.advance = advance;
                pen_x += advance;
                glyph_count += 1u;
                offset += decoded.size;
            }

            DWRITE_FONT_METRICS font_metrics = {};
            impl->font_face->GetMetrics(&font_metrics);
            float const scale = metrics_scale(font_metrics, size);
            float const ascent = static_cast<float>(font_metrics.ascent) * scale;
            float const descent = static_cast<float>(font_metrics.descent) * scale;

            uint32_t bitmap_width = 0u;
            uint32_t bitmap_height = 0u;
            ASSERT(ceil_u32(std::max(1.0f, pen_x + (TEXT_PADDING * 2.0f)), bitmap_width));
            ASSERT(
                ceil_u32(std::max(1.0f, ascent + descent + (TEXT_PADDING * 2.0f)), bitmap_height)
            );

            out_text.glyphs = glyphs;
            out_text.glyph_count = glyph_count;
            out_text.advance = pen_x;
            out_text.origin_x = TEXT_PADDING;
            out_text.origin_y = TEXT_PADDING;
            out_text.baseline_y = TEXT_PADDING + ascent;
            out_text.height = static_cast<float>(bitmap_height);
            out_text.size = {bitmap_width, bitmap_height};
        }

        auto raster_glyph(
            FontImpl* impl,
            float size,
            uint16_t glyph_index,
            RasterPolicy raster_policy,
            uint8_t phase_x,
            uint8_t phase_y,
            Arena& arena,
            GlyphRaster& out_raster
        ) -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);
            ASSERT(impl->context != nullptr);
            ASSERT(impl->context->factory2 != nullptr);

            FLOAT const advance = 0.0f;
            DWRITE_GLYPH_RUN glyph_run = {};
            glyph_run.fontFace = impl->font_face;
            glyph_run.fontEmSize = dwrite_font_size(size);
            glyph_run.glyphCount = 1u;
            glyph_run.glyphIndices = &glyph_index;
            glyph_run.glyphAdvances = &advance;

            IDWriteRenderingParams* const params = rendering_params(impl->context, raster_policy);
            ASSERT(params != nullptr);
            IDWriteGlyphRunAnalysis* analysis = nullptr;
            HRESULT hr = impl->context->factory2->CreateGlyphRunAnalysis(
                &glyph_run,
                nullptr,
                rendering_mode(raster_policy),
                TEXT_MEASURING_MODE,
                TEXT_GRID_FIT_MODE,
                TEXT_ANTIALIAS_MODE,
                glyph_phase(phase_x),
                glyph_phase(phase_y),
                &analysis
            );
            ASSERT(SUCCEEDED(hr));
            ASSERT(analysis != nullptr);

            RECT bounds = {};
            hr = analysis->GetAlphaTextureBounds(TEXT_ALPHA_BOUNDS_TYPE, &bounds);
            ASSERT(SUCCEEDED(hr));

            uint32_t const width =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.right - bounds.left));
            uint32_t const height =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.bottom - bounds.top));
            if (width == 0u || height == 0u) {
                out_raster = {};
                release_com(analysis);
                return;
            }

            inflate_bounds(bounds);
            uint32_t const padded_width =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.right - bounds.left));
            uint32_t const padded_height =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.bottom - bounds.top));
            size_t alpha_size = 0u;
            ASSERT(checked_pixel_size(padded_width, padded_height, 1u, alpha_size));
            uint8_t* const pixels = arena_alloc<uint8_t>(arena, alpha_size);

            hr = ensure_bitmap_target(impl->context, padded_width, padded_height);
            ASSERT(SUCCEEDED(hr));
            clear_bitmap_target(impl->context->bitmap_target);
            hr = impl->context->bitmap_target->DrawGlyphRun(
                glyph_phase(phase_x) - static_cast<float>(bounds.left),
                glyph_phase(phase_y) - static_cast<float>(bounds.top),
                TEXT_BITMAP_MEASURING_MODE,
                &glyph_run,
                params,
                RGB(255, 255, 255),
                nullptr
            );
            ASSERT(SUCCEEDED(hr));
            bitmap_target_to_alpha(
                impl->context->bitmap_target, padded_width, padded_height, pixels
            );

            out_raster = {};
            out_raster.size = {padded_width, padded_height};
            out_raster.stride = padded_width;
            out_raster.pixels = pixels;
            out_raster.format = RasterFormat::ALPHA;
            out_raster.offset_x = static_cast<float>(bounds.left);
            out_raster.offset_y = static_cast<float>(bounds.top);
            release_com(analysis);
        }

        auto composite_glyph(
            RasterResult const& raster,
            ShapedText const& shaped,
            ShapedGlyph const& glyph,
            GlyphRaster const& glyph_raster
        ) -> void {
            if (glyph_raster.pixels == nullptr) {
                return;
            }

            int32_t const dst_x =
                static_cast<int32_t>(std::round(shaped.origin_x + glyph.x + glyph.offset_x)) +
                static_cast<int32_t>(glyph_raster.offset_x);
            int32_t const dst_y =
                static_cast<int32_t>(std::round(shaped.baseline_y - glyph.offset_y)) +
                static_cast<int32_t>(glyph_raster.offset_y);

            for (uint32_t y = 0u; y < glyph_raster.size.height; ++y) {
                int32_t const out_y = dst_y + static_cast<int32_t>(y);
                if (out_y < 0 || out_y >= static_cast<int32_t>(raster.size.height)) {
                    continue;
                }

                uint8_t const* const src =
                    glyph_raster.pixels + (static_cast<size_t>(y) * glyph_raster.stride);
                uint8_t* const dst = raster.pixels + (static_cast<size_t>(out_y) * raster.stride);
                for (uint32_t x = 0u; x < glyph_raster.size.width; ++x) {
                    int32_t const out_x = dst_x + static_cast<int32_t>(x);
                    if (out_x >= 0 && out_x < static_cast<int32_t>(raster.size.width)) {
                        uint8_t& pixel = dst[static_cast<size_t>(out_x)];
                        pixel = std::max(pixel, src[x]);
                    }
                }
            }
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        BASE_UNUSED(desc);

        ArenaMarker const marker = arena.marker();
        ContextImpl* const context = arena_new<ContextImpl>(arena);
        context->arena = &arena;
        context->static_loader = arena_new<StaticFontFileLoader>(arena);

        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_ISOLATED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&context->factory)
        );
        if (SUCCEEDED(hr)) {
            hr = context->factory->RegisterFontFileLoader(context->static_loader);
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory->QueryInterface(
                __uuidof(IDWriteFactory2), reinterpret_cast<void**>(&context->factory2)
            );
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory->GetGdiInterop(&context->gdi_interop);
        }
        if (SUCCEEDED(hr)) {
            hr = create_text_rendering_params(context);
        }
        if (FAILED(hr)) {
            release_com(context->bitmap_target1);
            release_com(context->bitmap_target);
            release_com(context->smooth_rendering_params);
            release_com(context->sharp_rendering_params);
            release_com(context->gdi_interop);
            unregister_static_font_loader(context);
            release_com(context->factory2);
            release_com(context->factory);
            arena.reset_to(marker);
            return Result::BACKEND_FAILURE;
        }

        out_context.handle = context;
        return Result::OK;
    }

    auto destroy_context(Context& context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        release_com(impl->bitmap_target1);
        release_com(impl->bitmap_target);
        release_com(impl->smooth_rendering_params);
        release_com(impl->sharp_rendering_params);
        release_com(impl->gdi_interop);
        unregister_static_font_loader(impl);
        release_com(impl->factory2);
        release_com(impl->factory);
        impl->static_loader = nullptr;
        impl->arena = nullptr;
        context.handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (!desc.data.empty()) {
            open_data_font(arena, impl, desc.data, out_font);
            return;
        }
        if (!desc.file_path.empty()) {
            open_file_font(arena, impl, desc.file_path, out_font);
            return;
        }

        open_system_font(arena, impl, desc.family_name, out_font);
    }

    auto close_font(Font& font) -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        destroy_font_impl(impl);
        font.handle = nullptr;
    }

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        DWRITE_FONT_METRICS dwrite_metrics = {};
        impl->font_face->GetMetrics(&dwrite_metrics);

        float const scale = metrics_scale(dwrite_metrics, size);
        out_metrics = {};
        out_metrics.line_gap = static_cast<float>(dwrite_metrics.lineGap) * scale;
        out_metrics.ascent = static_cast<float>(dwrite_metrics.ascent) * scale;
        out_metrics.descent = static_cast<float>(dwrite_metrics.descent) * scale;
        out_metrics.capital_height = static_cast<float>(dwrite_metrics.capHeight) * scale;
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        ArenaTemp temp = begin_thread_temp_arena();
        ShapedText shaped = {};
        shape_text(impl, size, text, *temp.arena(), shaped);
        return shaped.advance;
    }

    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        shape_text(impl, size, text, arena, out_text);
    }

    auto
    raster_glyph(Font font, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        raster_glyph(
            impl, size, glyph_index, RasterPolicy::SHARP_HINTED, 0u, 0u, arena, out_raster
        );
    }

    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        RasterPolicy raster_policy,
        uint8_t phase_x,
        uint8_t phase_y,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        raster_glyph(impl, size, glyph_index, raster_policy, phase_x, phase_y, arena, out_raster);
    }

    auto raster_glyph(
        Font font,
        float size,
        uint16_t glyph_index,
        uint8_t phase_x,
        uint8_t phase_y,
        Arena& arena,
        GlyphRaster& out_raster
    ) -> void {
        gui::font_provider::platform::dwrite::raster_glyph(
            font, size, glyph_index, RasterPolicy::SHARP_HINTED, phase_x, phase_y, arena, out_raster
        );
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);
        ASSERT(impl->context != nullptr);

        ArenaTemp temp = begin_thread_temp_arena();
        ShapedText shaped = {};
        shape_text(impl, size, text, *temp.arena(), shaped);

        size_t alpha_byte_count = 0u;
        ASSERT(checked_pixel_size(shaped.size.width, shaped.size.height, 1u, alpha_byte_count));
        uint8_t* const out_pixels = arena_alloc<uint8_t>(arena, alpha_byte_count);
        std::memset(out_pixels, 0, alpha_byte_count);

        out_raster = {};
        out_raster.size = shaped.size;
        out_raster.stride = shaped.size.width;
        out_raster.pixels = out_pixels;
        out_raster.format = RasterFormat::ALPHA;
        out_raster.advance = shaped.advance;
        out_raster.offset_y = shaped.origin_y;
        out_raster.height = shaped.height;

        for (size_t index = 0u; index < shaped.glyph_count; ++index) {
            GlyphRaster glyph_raster = {};
            auto* const glyph_font = font_from_handle(shaped.glyphs[index].font);
            raster_glyph(
                glyph_font,
                shaped.glyphs[index].size,
                shaped.glyphs[index].glyph_index,
                RasterPolicy::SHARP_HINTED,
                0u,
                0u,
                *temp.arena(),
                glyph_raster
            );
            composite_glyph(out_raster, shaped, shaped.glyphs[index], glyph_raster);
        }
    }

    auto native_factory(Context context) -> void* {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        return impl->factory;
    }

    auto native_font_face(Font font) -> void* {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        return impl->font_face;
    }

} // namespace gui::font_provider::platform::dwrite
