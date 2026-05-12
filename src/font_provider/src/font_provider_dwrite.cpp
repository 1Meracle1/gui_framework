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
        constexpr DWRITE_RENDERING_MODE SHARP_RENDERING_MODE = DWRITE_RENDERING_MODE_GDI_CLASSIC;
        constexpr DWRITE_RENDERING_MODE SMOOTH_RENDERING_MODE =
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        constexpr DWRITE_GRID_FIT_MODE TEXT_GRID_FIT_MODE = DWRITE_GRID_FIT_MODE_ENABLED;
        constexpr DWRITE_TEXT_ANTIALIAS_MODE TEXT_ANTIALIAS_MODE =
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        constexpr DWRITE_TEXTURE_TYPE TEXT_ALPHA_BOUNDS_TYPE = DWRITE_TEXTURE_ALIASED_1x1;
        constexpr DWRITE_TEXTURE_TYPE TEXT_LCD_BOUNDS_TYPE = DWRITE_TEXTURE_CLEARTYPE_3x1;
        constexpr LONG GLYPH_RASTER_PADDING = 1;
        constexpr float TEXT_PADDING = 2.0f;

        struct StaticFontData;
        struct StaticFontFileLoader;
        struct FontImpl;
        struct TextAnalysisSource;
        struct TextAnalysisSink;

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

        struct TextAnalysisSource final : IDWriteTextAnalysisSource {
            wchar_t const* text = nullptr;
            UINT32 text_len = 0u;

            auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
            auto STDMETHODCALLTYPE AddRef() -> ULONG override;
            auto STDMETHODCALLTYPE Release() -> ULONG override;
            auto STDMETHODCALLTYPE
            GetTextAtPosition(UINT32 text_position, WCHAR const** text_string, UINT32* text_length)
                -> HRESULT override;
            auto STDMETHODCALLTYPE GetTextBeforePosition(
                UINT32 text_position, WCHAR const** text_string, UINT32* text_length
            ) -> HRESULT override;
            auto STDMETHODCALLTYPE GetParagraphReadingDirection()
                -> DWRITE_READING_DIRECTION override;
            auto STDMETHODCALLTYPE
            GetLocaleName(UINT32 text_position, UINT32* text_length, WCHAR const** locale_name)
                -> HRESULT override;
            auto STDMETHODCALLTYPE GetNumberSubstitution(
                UINT32 text_position,
                UINT32* text_length,
                IDWriteNumberSubstitution** number_substitution
            ) -> HRESULT override;
        };

        struct TextAnalysisSink final : IDWriteTextAnalysisSink {
            DWRITE_SCRIPT_ANALYSIS* scripts = nullptr;
            uint8_t* bidi_levels = nullptr;
            UINT32 text_len = 0u;

            auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) -> HRESULT override;
            auto STDMETHODCALLTYPE AddRef() -> ULONG override;
            auto STDMETHODCALLTYPE Release() -> ULONG override;
            auto STDMETHODCALLTYPE SetScriptAnalysis(
                UINT32 text_position,
                UINT32 text_length,
                DWRITE_SCRIPT_ANALYSIS const* script_analysis
            ) -> HRESULT override;
            auto STDMETHODCALLTYPE SetLineBreakpoints(
                UINT32 text_position,
                UINT32 text_length,
                DWRITE_LINE_BREAKPOINT const* line_breakpoints
            ) -> HRESULT override;
            auto STDMETHODCALLTYPE SetBidiLevel(
                UINT32 text_position, UINT32 text_length, UINT8 explicit_level, UINT8 resolved_level
            ) -> HRESULT override;
            auto STDMETHODCALLTYPE SetNumberSubstitution(
                UINT32 text_position,
                UINT32 text_length,
                IDWriteNumberSubstitution* number_substitution
            ) -> HRESULT override;
        };

        struct ContextImpl {
            Arena* arena = nullptr;
            IDWriteFactory* factory = nullptr;
            IDWriteFactory2* factory2 = nullptr;
            IDWriteTextAnalyzer* text_analyzer = nullptr;
            IDWriteFontFallback* system_fallback = nullptr;
            StaticFontFileLoader* static_loader = nullptr;
            IDWriteGdiInterop* gdi_interop = nullptr;
            IDWriteRenderingParams* sharp_rendering_params = nullptr;
            IDWriteRenderingParams* natural_rendering_params = nullptr;
            IDWriteRenderingParams* smooth_rendering_params = nullptr;
            IDWriteBitmapRenderTarget* bitmap_target = nullptr;
            IDWriteBitmapRenderTarget1* bitmap_target1 = nullptr;
            FontImpl* first_fallback_font = nullptr;
        };

        struct FontImpl {
            FontImpl* next_fallback = nullptr;
            ContextImpl* context = nullptr;
            IDWriteFontFile* font_file = nullptr;
            IDWriteFontFace* font_face = nullptr;
            StaticFontData* static_data = nullptr;
            bool fallback = false;
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

        auto STDMETHODCALLTYPE TextAnalysisSource::QueryInterface(REFIID riid, void** object)
            -> HRESULT {
            return query_com_interface(
                riid,
                object,
                static_cast<IDWriteTextAnalysisSource*>(this),
                __uuidof(IDWriteTextAnalysisSource)
            );
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::AddRef() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::Release() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::GetTextAtPosition(
            UINT32 text_position, WCHAR const** text_string, UINT32* text_length
        ) -> HRESULT {
            ASSERT(text_string != nullptr);
            ASSERT(text_length != nullptr);

            if (text_position >= text_len) {
                *text_string = nullptr;
                *text_length = 0u;
                return S_OK;
            }

            *text_string = text + text_position;
            *text_length = text_len - text_position;
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::GetTextBeforePosition(
            UINT32 text_position, WCHAR const** text_string, UINT32* text_length
        ) -> HRESULT {
            ASSERT(text_string != nullptr);
            ASSERT(text_length != nullptr);

            if (text_position == 0u || text_position > text_len) {
                *text_string = nullptr;
                *text_length = 0u;
                return S_OK;
            }

            *text_string = text;
            *text_length = text_position;
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::GetParagraphReadingDirection()
            -> DWRITE_READING_DIRECTION {
            return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::GetLocaleName(
            UINT32 text_position, UINT32* text_length, WCHAR const** locale_name
        ) -> HRESULT {
            ASSERT(text_length != nullptr);
            ASSERT(locale_name != nullptr);

            static constexpr WCHAR LOCALE_NAME[] = L"en-us";
            *locale_name = LOCALE_NAME;
            *text_length = text_position < text_len ? text_len - text_position : 0u;
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSource::GetNumberSubstitution(
            UINT32 text_position,
            UINT32* text_length,
            IDWriteNumberSubstitution** number_substitution
        ) -> HRESULT {
            ASSERT(text_length != nullptr);
            ASSERT(number_substitution != nullptr);

            *text_length = text_position < text_len ? text_len - text_position : 0u;
            *number_substitution = nullptr;
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::QueryInterface(REFIID riid, void** object)
            -> HRESULT {
            return query_com_interface(
                riid,
                object,
                static_cast<IDWriteTextAnalysisSink*>(this),
                __uuidof(IDWriteTextAnalysisSink)
            );
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::AddRef() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::Release() -> ULONG {
            return 1u;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::SetScriptAnalysis(
            UINT32 text_position, UINT32 text_length, DWRITE_SCRIPT_ANALYSIS const* script_analysis
        ) -> HRESULT {
            ASSERT(script_analysis != nullptr);
            ASSERT(text_position <= text_len);
            ASSERT(text_length <= text_len - text_position);

            for (UINT32 index = 0u; index < text_length; ++index) {
                scripts[text_position + index] = *script_analysis;
            }
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::SetLineBreakpoints(
            UINT32 text_position, UINT32 text_length, DWRITE_LINE_BREAKPOINT const* line_breakpoints
        ) -> HRESULT {
            BASE_UNUSED(text_position);
            BASE_UNUSED(text_length);
            BASE_UNUSED(line_breakpoints);
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::SetBidiLevel(
            UINT32 text_position, UINT32 text_length, UINT8 explicit_level, UINT8 resolved_level
        ) -> HRESULT {
            ASSERT(text_position <= text_len);
            ASSERT(text_length <= text_len - text_position);
            BASE_UNUSED(explicit_level);

            for (UINT32 index = 0u; index < text_length; ++index) {
                bidi_levels[text_position + index] = resolved_level;
            }
            return S_OK;
        }

        auto STDMETHODCALLTYPE TextAnalysisSink::SetNumberSubstitution(
            UINT32 text_position, UINT32 text_length, IDWriteNumberSubstitution* number_substitution
        ) -> HRESULT {
            BASE_UNUSED(text_position);
            BASE_UNUSED(text_length);
            BASE_UNUSED(number_substitution);
            return S_OK;
        }

        auto unregister_static_font_loader(ContextImpl* context) -> void {
            if (context->factory != nullptr && context->static_loader != nullptr) {
                context->factory->UnregisterFontFileLoader(context->static_loader);
            }
        }

        [[nodiscard]] auto create_rendering_params(
            ContextImpl* context,
            FLOAT gamma,
            FLOAT enhanced_contrast,
            DWRITE_RENDERING_MODE rendering_mode,
            IDWriteRenderingParams*& out_params
        ) -> HRESULT {
            IDWriteRenderingParams2* params = nullptr;
            HRESULT const hr = context->factory2->CreateCustomRenderingParams(
                gamma,
                enhanced_contrast,
                enhanced_contrast,
                0.0f,
                DWRITE_PIXEL_GEOMETRY_FLAT,
                rendering_mode,
                TEXT_GRID_FIT_MODE,
                &params
            );
            out_params = params;
            return hr;
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

            FLOAT const gamma = base_params->GetGamma();
            FLOAT const enhanced_contrast = base_params->GetEnhancedContrast();
            release_com(base_params);

            hr = create_rendering_params(
                context,
                gamma,
                enhanced_contrast,
                SHARP_RENDERING_MODE,
                context->sharp_rendering_params
            );
            if (FAILED(hr)) {
                return hr;
            }

            hr = create_rendering_params(
                context,
                gamma,
                enhanced_contrast,
                DWRITE_RENDERING_MODE_NATURAL,
                context->natural_rendering_params
            );
            if (FAILED(hr)) {
                return hr;
            }

            hr = create_rendering_params(
                context,
                gamma,
                enhanced_contrast,
                SMOOTH_RENDERING_MODE,
                context->smooth_rendering_params
            );
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

        auto destroy_fallback_fonts(ContextImpl* context) -> void {
            for (FontImpl* font = context->first_fallback_font; font != nullptr;) {
                FontImpl* const next = font->next_fallback;
                destroy_font_impl(font);
                font->next_fallback = nullptr;
                font->fallback = false;
                font = next;
            }
            context->first_fallback_font = nullptr;
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
            return size;
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

        [[nodiscard]] auto raster_policy_smooth(RasterPolicy raster_policy) -> bool {
            return raster_policy == RasterPolicy::SMOOTH_HINTED ||
                   raster_policy == RasterPolicy::LCD_SMOOTH_HINTED;
        }

        [[nodiscard]] auto raster_policy_lcd(RasterPolicy raster_policy) -> bool {
            return raster_policy == RasterPolicy::LCD_SHARP_HINTED ||
                   raster_policy == RasterPolicy::LCD_SMOOTH_HINTED;
        }

        struct GaspRange {
            int version = 0;
            uint16_t flags = 0u;
        };

        struct ScalerMode {
            DWRITE_RENDERING_MODE rendering_mode = SHARP_RENDERING_MODE;
            DWRITE_MEASURING_MODE measuring_mode = TEXT_MEASURING_MODE;
            DWRITE_GRID_FIT_MODE grid_fit_mode = TEXT_GRID_FIT_MODE;
        };

        [[nodiscard]] auto be_u16(uint8_t const* data) -> uint16_t {
            return static_cast<uint16_t>(
                (static_cast<uint16_t>(data[0u]) << 8u) | static_cast<uint16_t>(data[1u])
            );
        }

        [[nodiscard]] auto be_u32(uint8_t const* data) -> uint32_t {
            return (static_cast<uint32_t>(data[0u]) << 24u) |
                   (static_cast<uint32_t>(data[1u]) << 16u) |
                   (static_cast<uint32_t>(data[2u]) << 8u) | static_cast<uint32_t>(data[3u]);
        }

        [[nodiscard]] auto get_font_table(
            IDWriteFontFace* face,
            UINT32 tag,
            uint8_t const*& out_data,
            UINT32& out_size,
            void*& out_context
        ) -> bool {
            BOOL exists = FALSE;
            void const* data = nullptr;
            HRESULT const hr = face->TryGetFontTable(tag, &data, &out_size, &out_context, &exists);
            if (FAILED(hr) || exists == FALSE || data == nullptr) {
                return false;
            }
            out_data = static_cast<uint8_t const*>(data);
            return true;
        }

        [[nodiscard]] auto font_has_bytecode_hints(IDWriteFontFace* face) -> bool {
            uint8_t const* data = nullptr;
            UINT32 size = 0u;
            void* context = nullptr;
            bool hinted = false;
            if (get_font_table(
                    face, DWRITE_MAKE_OPENTYPE_TAG('m', 'a', 'x', 'p'), data, size, context
                )) {
                hinted = size >= 28u && be_u32(data) == 0x00010000u && be_u16(data + 26u) != 0u;
                face->ReleaseFontTable(context);
            }
            return hinted;
        }

        [[nodiscard]] auto get_gasp_range(IDWriteFontFace* face, int ppem, GaspRange& out_range)
            -> bool {
            uint8_t const* data = nullptr;
            UINT32 size = 0u;
            void* context = nullptr;
            bool found = false;
            if (get_font_table(
                    face, DWRITE_MAKE_OPENTYPE_TAG('g', 'a', 's', 'p'), data, size, context
                )) {
                if (size >= 4u) {
                    uint16_t const version = be_u16(data);
                    uint16_t const range_count = be_u16(data + 2u);
                    if ((version == 0u || version == 1u) && range_count <= 1024u &&
                        size >= 4u + static_cast<UINT32>(range_count) * 4u) {
                        int min_ppem = -1;
                        for (uint16_t index = 0u; index < range_count; ++index) {
                            uint8_t const* range = data + 4u + static_cast<size_t>(index) * 4u;
                            int const max_ppem = static_cast<int>(be_u16(range));
                            if (min_ppem < ppem && ppem <= max_ppem) {
                                out_range.version = static_cast<int>(version);
                                out_range.flags = be_u16(range + 2u);
                                found = true;
                                break;
                            }
                            min_ppem = max_ppem;
                        }
                    }
                }
                face->ReleaseFontTable(context);
            }
            return found;
        }

        [[nodiscard]] auto scaler_mode(FontImpl* font, float size, RasterPolicy raster_policy)
            -> ScalerMode {
            ASSERT(font != nullptr);
            ASSERT(font->font_face != nullptr);

            ScalerMode mode = {};
            if (raster_policy_smooth(raster_policy)) {
                mode.rendering_mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
                mode.measuring_mode = DWRITE_MEASURING_MODE_NATURAL;
                return mode;
            }

            int const ppem = std::max(1, static_cast<int>(std::round(dwrite_font_size(size))));
            GaspRange range = {};
            if (get_gasp_range(font->font_face, ppem, range) && range.version >= 1) {
                constexpr uint16_t GASP_SYMMETRIC_SMOOTHING = 0x0008u;
                mode.rendering_mode = (range.flags & GASP_SYMMETRIC_SMOOTHING) != 0u
                                          ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC
                                          : DWRITE_RENDERING_MODE_NATURAL;
                mode.measuring_mode = DWRITE_MEASURING_MODE_NATURAL;
            } else if (dwrite_font_size(size) > 20.0f ||
                       !font_has_bytecode_hints(font->font_face)) {
                mode.rendering_mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
                mode.measuring_mode = DWRITE_MEASURING_MODE_NATURAL;
            } else {
                mode.rendering_mode = DWRITE_RENDERING_MODE_NATURAL;
                mode.measuring_mode = DWRITE_MEASURING_MODE_NATURAL;
            }
            return mode;
        }

        [[nodiscard]] auto
        rendering_params(ContextImpl* context, DWRITE_RENDERING_MODE rendering_mode)
            -> IDWriteRenderingParams* {
            if (rendering_mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC) {
                return context->smooth_rendering_params;
            }
            if (rendering_mode == DWRITE_RENDERING_MODE_NATURAL) {
                return context->natural_rendering_params;
            }
            return context->sharp_rendering_params;
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

        [[nodiscard]] auto max_coverage(uint8_t r, uint8_t g, uint8_t b) -> uint8_t {
            return std::max(std::max(r, g), b);
        }

        struct WideCodepointSpan {
            uint32_t codepoint = 0u;
            UINT32 wide_start = 0u;
            UINT32 wide_len = 0u;
            uint32_t utf8_start = 0u;
            uint32_t utf8_end = 0u;
        };

        struct WideText {
            wchar_t* text = nullptr;
            UINT32 len = 0u;
            uint32_t* utf8_starts = nullptr;
            uint32_t* utf8_ends = nullptr;
            WideCodepointSpan* codepoints = nullptr;
            size_t codepoint_count = 0u;
        };

        [[nodiscard]] auto uint32_from_size(size_t value, uint32_t& out_value) -> bool {
            if (value > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            out_value = static_cast<uint32_t>(value);
            return true;
        }

        [[nodiscard]] auto build_wide_text(StrRef text, Arena& arena, WideText& out_text) -> bool {
            out_text = {};
            if (text.empty()) {
                return true;
            }

            wchar_t* const wide = arena_alloc<wchar_t>(arena, text.size() + 1u);
            uint32_t* const utf8_starts = arena_alloc<uint32_t>(arena, text.size() + 1u);
            uint32_t* const utf8_ends = arena_alloc<uint32_t>(arena, text.size() + 1u);
            WideCodepointSpan* const codepoints =
                arena_alloc<WideCodepointSpan>(arena, text.size());

            UINT32 wide_len = 0u;
            size_t codepoint_count = 0u;
            size_t offset = 0u;
            while (offset < text.size()) {
                base::Utf8DecodeResult decoded = base::utf8_decode(text, offset);
                if (!decoded.ok) {
                    decoded = {'?', 1u, true};
                }

                uint32_t utf8_start = 0u;
                uint32_t utf8_end = 0u;
                if (!uint32_from_size(offset, utf8_start) ||
                    !uint32_from_size(offset + decoded.size, utf8_end)) {
                    return false;
                }

                WideCodepointSpan& span = codepoints[codepoint_count];
                span = {};
                span.codepoint = decoded.codepoint;
                span.wide_start = wide_len;
                span.utf8_start = utf8_start;
                span.utf8_end = utf8_end;

                if (decoded.codepoint <= 0xffffu) {
                    wide[wide_len] = static_cast<wchar_t>(decoded.codepoint);
                    utf8_starts[wide_len] = utf8_start;
                    utf8_ends[wide_len] = utf8_end;
                    wide_len += 1u;
                    span.wide_len = 1u;
                } else {
                    uint32_t const value = decoded.codepoint - 0x10000u;
                    wide[wide_len] = static_cast<wchar_t>(0xd800u + (value >> 10u));
                    utf8_starts[wide_len] = utf8_start;
                    utf8_ends[wide_len] = utf8_end;
                    wide_len += 1u;
                    wide[wide_len] = static_cast<wchar_t>(0xdc00u + (value & 0x3ffu));
                    utf8_starts[wide_len] = utf8_start;
                    utf8_ends[wide_len] = utf8_end;
                    wide_len += 1u;
                    span.wide_len = 2u;
                }

                codepoint_count += 1u;
                offset += decoded.size;
            }

            wide[wide_len] = L'\0';
            utf8_starts[wide_len] = static_cast<uint32_t>(text.size());
            utf8_ends[wide_len] = static_cast<uint32_t>(text.size());
            out_text.text = wide;
            out_text.len = wide_len;
            out_text.utf8_starts = utf8_starts;
            out_text.utf8_ends = utf8_ends;
            out_text.codepoints = codepoints;
            out_text.codepoint_count = codepoint_count;
            return true;
        }

        [[nodiscard]] auto font_face_glyph(IDWriteFontFace* font_face, uint32_t codepoint)
            -> uint16_t {
            UINT32 const dwrite_codepoint = codepoint;
            UINT16 glyph_index = 0u;
            HRESULT const hr = font_face->GetGlyphIndices(&dwrite_codepoint, 1u, &glyph_index);
            ASSERT(SUCCEEDED(hr));
            BASE_UNUSED(hr);
            return glyph_index;
        }

        [[nodiscard]] auto add_fallback_font(ContextImpl* context, IDWriteFontFace* font_face)
            -> FontImpl* {
            ASSERT(context != nullptr);
            ASSERT(context->arena != nullptr);
            ASSERT(font_face != nullptr);

            FontImpl* const font = arena_new<FontImpl>(*context->arena);
            font->context = context;
            font->font_face = font_face;
            font->fallback = true;
            font->next_fallback = context->first_fallback_font;
            context->first_fallback_font = font;
            return font;
        }

        [[nodiscard]] auto fallback_font_for_span(
            FontImpl* primary,
            TextAnalysisSource& source,
            WideCodepointSpan const& span,
            float& out_scale
        ) -> FontImpl* {
            ASSERT(primary != nullptr);
            ASSERT(primary->context != nullptr);

            out_scale = 1.0f;
            ContextImpl* const context = primary->context;
            if (context->system_fallback == nullptr) {
                return primary;
            }

            UINT32 mapped_len = 0u;
            FLOAT mapped_scale = 1.0f;
            IDWriteFont* mapped_font = nullptr;
            HRESULT hr = context->system_fallback->MapCharacters(
                &source,
                span.wide_start,
                span.wide_len,
                nullptr,
                nullptr,
                DWRITE_FONT_WEIGHT_REGULAR,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                &mapped_len,
                &mapped_font,
                &mapped_scale
            );
            if (FAILED(hr) || mapped_font == nullptr || mapped_len == 0u) {
                release_com(mapped_font);
                return primary;
            }

            IDWriteFontFace* font_face = nullptr;
            hr = mapped_font->CreateFontFace(&font_face);
            release_com(mapped_font);
            if (FAILED(hr) || font_face == nullptr) {
                release_com(font_face);
                return primary;
            }

            out_scale = mapped_scale;
            return add_fallback_font(context, font_face);
        }

        auto assign_fonts(
            FontImpl* primary,
            TextAnalysisSource& source,
            WideText const& wide,
            FontImpl** fonts,
            float* scales
        ) -> void {
            for (UINT32 index = 0u; index < wide.len; ++index) {
                fonts[index] = primary;
                scales[index] = 1.0f;
            }

            for (size_t index = 0u; index < wide.codepoint_count; ++index) {
                WideCodepointSpan const& span = wide.codepoints[index];
                if (font_face_glyph(primary->font_face, span.codepoint) != 0u) {
                    continue;
                }

                float scale = 1.0f;
                FontImpl* const fallback = fallback_font_for_span(primary, source, span, scale);
                for (UINT32 unit = 0u; unit < span.wide_len; ++unit) {
                    fonts[span.wide_start + unit] = fallback;
                    scales[span.wide_start + unit] = scale;
                }
            }
        }

        [[nodiscard]] auto script_equal(DWRITE_SCRIPT_ANALYSIS lhs, DWRITE_SCRIPT_ANALYSIS rhs)
            -> bool {
            return lhs.script == rhs.script && lhs.shapes == rhs.shapes;
        }

        [[nodiscard]] auto glyph_source_range(
            UINT16 const* clusters,
            UINT32 text_start,
            UINT32 text_len,
            UINT32 glyph_index,
            UINT32& out_start,
            UINT32& out_end
        ) -> void {
            out_start = std::numeric_limits<UINT32>::max();
            out_end = text_start;
            for (UINT32 index = 0u; index < text_len; ++index) {
                if (clusters[index] == glyph_index) {
                    out_start = std::min(out_start, text_start + index);
                    out_end = std::max(out_end, text_start + index + 1u);
                }
            }
            if (out_start != std::numeric_limits<UINT32>::max()) {
                return;
            }

            for (UINT32 previous = glyph_index; previous > 0u; --previous) {
                UINT32 start = 0u;
                UINT32 end = 0u;
                glyph_source_range(clusters, text_start, text_len, previous - 1u, start, end);
                if (end > start) {
                    out_start = start;
                    out_end = end;
                    return;
                }
            }

            out_start = text_start;
            out_end = text_start;
        }

        [[nodiscard]] auto shape_run(
            Arena& arena,
            FontImpl* font,
            float size,
            WideText const& wide,
            UINT32 text_start,
            UINT32 text_len,
            DWRITE_SCRIPT_ANALYSIS script,
            uint8_t bidi_level,
            ShapedGlyph* glyphs,
            size_t glyph_capacity,
            size_t& glyph_count,
            ShapedRun* runs,
            size_t& run_count,
            float& pen_x
        ) -> bool {
            ASSERT(font != nullptr);
            ASSERT(font->context != nullptr);
            ASSERT(font->context->text_analyzer != nullptr);

            if (text_len == 0u || text_len > (std::numeric_limits<UINT32>::max() - 16u) / 3u) {
                return false;
            }

            UINT32 const max_glyph_count = text_len * 3u + 16u;
            UINT16* const clusters = arena_alloc<UINT16>(arena, text_len);
            DWRITE_SHAPING_TEXT_PROPERTIES* const text_props =
                arena_alloc<DWRITE_SHAPING_TEXT_PROPERTIES>(arena, text_len);
            UINT16* const glyph_indices = arena_alloc<UINT16>(arena, max_glyph_count);
            DWRITE_SHAPING_GLYPH_PROPERTIES* const glyph_props =
                arena_alloc<DWRITE_SHAPING_GLYPH_PROPERTIES>(arena, max_glyph_count);

            UINT32 actual_glyph_count = 0u;
            BOOL const right_to_left = (bidi_level & 1u) != 0u ? TRUE : FALSE;
            static constexpr WCHAR LOCALE_NAME[] = L"en-us";
            HRESULT hr = font->context->text_analyzer->GetGlyphs(
                wide.text + text_start,
                text_len,
                font->font_face,
                FALSE,
                right_to_left,
                &script,
                LOCALE_NAME,
                nullptr,
                nullptr,
                nullptr,
                0u,
                max_glyph_count,
                clusters,
                text_props,
                glyph_indices,
                glyph_props,
                &actual_glyph_count
            );
            if (FAILED(hr) || actual_glyph_count == 0u ||
                static_cast<size_t>(actual_glyph_count) > glyph_capacity - glyph_count) {
                return false;
            }

            FLOAT* const advances = arena_alloc<FLOAT>(arena, actual_glyph_count);
            DWRITE_GLYPH_OFFSET* const offsets =
                arena_alloc<DWRITE_GLYPH_OFFSET>(arena, actual_glyph_count);
            hr = font->context->text_analyzer->GetGlyphPlacements(
                wide.text + text_start,
                clusters,
                text_props,
                text_len,
                glyph_indices,
                glyph_props,
                actual_glyph_count,
                font->font_face,
                dwrite_font_size(size),
                FALSE,
                right_to_left,
                &script,
                LOCALE_NAME,
                nullptr,
                nullptr,
                0u,
                advances,
                offsets
            );
            if (FAILED(hr)) {
                return false;
            }

            ASSERT(run_count <= static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
            uint16_t const run_index = static_cast<uint16_t>(run_count);
            ShapedRun& run = runs[run_count];
            run = {};
            run.font = font_handle(font);
            run.first_glyph = glyph_count;
            run.glyph_count = actual_glyph_count;
            run.utf8_start = wide.utf8_starts[text_start];
            run.utf8_end = wide.utf8_ends[text_start + text_len - 1u];
            run.script = script.script;
            run.bidi_level = bidi_level;
            run.right_to_left = right_to_left != FALSE;

            for (UINT32 index = 0u; index < actual_glyph_count; ++index) {
                UINT32 utf16_start = 0u;
                UINT32 utf16_end = 0u;
                glyph_source_range(clusters, text_start, text_len, index, utf16_start, utf16_end);
                if (utf16_end <= utf16_start) {
                    utf16_start = text_start;
                    utf16_end = text_start + 1u;
                }

                ShapedGlyph& glyph = glyphs[glyph_count];
                glyph = {};
                glyph.font = font_handle(font);
                glyph.glyph_index = glyph_indices[index];
                glyph.run_index = run_index;
                glyph.size = size;
                glyph.x = pen_x;
                glyph.advance = advances[index];
                glyph.offset_x = offsets[index].advanceOffset;
                glyph.offset_y = offsets[index].ascenderOffset;
                glyph.cluster = wide.utf8_starts[utf16_start];
                glyph.utf8_start = wide.utf8_starts[utf16_start];
                glyph.utf8_end = wide.utf8_ends[utf16_end - 1u];
                pen_x += advances[index];
                glyph_count += 1u;
            }

            run_count += 1u;
            return true;
        }

        auto shape_text(FontImpl* impl, float size, StrRef text, Arena& arena, ShapedText& out_text)
            -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);
            ASSERT(impl->context != nullptr);
            ASSERT(impl->context->text_analyzer != nullptr);

            out_text = {};
            WideText wide = {};
            if (text.empty() || !build_wide_text(text, arena, wide) || wide.len == 0u) {
                return;
            }

            TextAnalysisSource source = {};
            source.text = wide.text;
            source.text_len = wide.len;

            DWRITE_SCRIPT_ANALYSIS* const scripts =
                arena_alloc<DWRITE_SCRIPT_ANALYSIS>(arena, wide.len);
            uint8_t* const levels = arena_alloc<uint8_t>(arena, wide.len);
            std::memset(scripts, 0, sizeof(DWRITE_SCRIPT_ANALYSIS) * wide.len);
            std::memset(levels, 0, wide.len);

            TextAnalysisSink sink = {};
            sink.scripts = scripts;
            sink.bidi_levels = levels;
            sink.text_len = wide.len;
            BASE_UNUSED(impl->context->text_analyzer->AnalyzeScript(&source, 0u, wide.len, &sink));
            BASE_UNUSED(impl->context->text_analyzer->AnalyzeBidi(&source, 0u, wide.len, &sink));

            FontImpl** const fonts = arena_alloc<FontImpl*>(arena, wide.len);
            float* const scales = arena_alloc<float>(arena, wide.len);
            assign_fonts(impl, source, wide, fonts, scales);

            size_t const glyph_capacity = text.size() * 3u + 16u;
            ShapedGlyph* const glyphs = arena_alloc<ShapedGlyph>(arena, glyph_capacity);
            ShapedRun* const runs = arena_alloc<ShapedRun>(arena, wide.codepoint_count + 1u);
            float pen_x = 0.0f;
            size_t glyph_count = 0u;
            size_t run_count = 0u;

            UINT32 start = 0u;
            while (start < wide.len) {
                UINT32 end = start + 1u;
                while (end < wide.len && fonts[end] == fonts[start] &&
                       scales[end] == scales[start] && levels[end] == levels[start] &&
                       script_equal(scripts[end], scripts[start])) {
                    end += 1u;
                }

                bool const shaped = shape_run(
                    arena,
                    fonts[start],
                    size * scales[start],
                    wide,
                    start,
                    end - start,
                    scripts[start],
                    levels[start],
                    glyphs,
                    glyph_capacity,
                    glyph_count,
                    runs,
                    run_count,
                    pen_x
                );
                if (!shaped) {
                    out_text = {};
                    return;
                }
                start = end;
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
            out_text.runs = runs;
            out_text.run_count = run_count;
            out_text.advance = pen_x;
            out_text.origin_x = TEXT_PADDING;
            out_text.origin_y = TEXT_PADDING;
            out_text.baseline_y = TEXT_PADDING + ascent;
            out_text.height = static_cast<float>(bitmap_height);
            out_text.size = {bitmap_width, bitmap_height};
        }

        auto raster_glyph_lcd(
            FontImpl* impl,
            float size,
            uint16_t glyph_index,
            RasterPolicy raster_policy,
            uint8_t phase_x,
            uint8_t phase_y,
            Arena& arena,
            GlyphRaster& out_raster
        ) -> void {
            FLOAT const advance = 0.0f;
            DWRITE_GLYPH_RUN glyph_run = {};
            glyph_run.fontFace = impl->font_face;
            glyph_run.fontEmSize = dwrite_font_size(size);
            glyph_run.glyphCount = 1u;
            glyph_run.glyphIndices = &glyph_index;
            glyph_run.glyphAdvances = &advance;

            IDWriteGlyphRunAnalysis* analysis = nullptr;
            ScalerMode const mode = scaler_mode(impl, size, raster_policy);
            HRESULT hr = impl->context->factory2->CreateGlyphRunAnalysis(
                &glyph_run,
                nullptr,
                mode.rendering_mode,
                mode.measuring_mode,
                mode.grid_fit_mode,
                DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE,
                glyph_phase(phase_x),
                glyph_phase(phase_y),
                &analysis
            );
            ASSERT(SUCCEEDED(hr));
            ASSERT(analysis != nullptr);

            RECT bounds = {};
            hr = analysis->GetAlphaTextureBounds(TEXT_LCD_BOUNDS_TYPE, &bounds);
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

            size_t lcd_size = 0u;
            ASSERT(checked_pixel_size(padded_width, padded_height, 3u, lcd_size));
            ASSERT(lcd_size <= static_cast<size_t>(std::numeric_limits<UINT32>::max()));
            uint8_t* const lcd_pixels = arena_alloc<uint8_t>(arena, lcd_size);
            hr = analysis->CreateAlphaTexture(
                TEXT_LCD_BOUNDS_TYPE, &bounds, lcd_pixels, static_cast<UINT32>(lcd_size)
            );
            ASSERT(SUCCEEDED(hr));

            size_t rgba_size = 0u;
            ASSERT(checked_pixel_size(padded_width, padded_height, 4u, rgba_size));
            uint8_t* const pixels = arena_alloc<uint8_t>(arena, rgba_size);
            for (uint32_t y = 0u; y < padded_height; ++y) {
                uint8_t const* const src =
                    lcd_pixels + (static_cast<size_t>(y) * padded_width * 3u);
                uint8_t* const dst = pixels + (static_cast<size_t>(y) * padded_width * 4u);
                for (uint32_t x = 0u; x < padded_width; ++x) {
                    uint8_t const r = src[x * 3u + 0u];
                    uint8_t const g = src[x * 3u + 1u];
                    uint8_t const b = src[x * 3u + 2u];
                    dst[x * 4u + 0u] = r;
                    dst[x * 4u + 1u] = g;
                    dst[x * 4u + 2u] = b;
                    dst[x * 4u + 3u] = max_coverage(r, g, b);
                }
            }

            out_raster = {};
            out_raster.size = {padded_width, padded_height};
            out_raster.stride = padded_width * 4u;
            out_raster.pixels = pixels;
            out_raster.format = RasterFormat::LCD_RGB;
            out_raster.offset_x = static_cast<float>(bounds.left);
            out_raster.offset_y = static_cast<float>(bounds.top);
            release_com(analysis);
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

            if (raster_policy_lcd(raster_policy)) {
                raster_glyph_lcd(
                    impl, size, glyph_index, raster_policy, phase_x, phase_y, arena, out_raster
                );
                return;
            }

            FLOAT const advance = 0.0f;
            DWRITE_GLYPH_RUN glyph_run = {};
            glyph_run.fontFace = impl->font_face;
            glyph_run.fontEmSize = dwrite_font_size(size);
            glyph_run.glyphCount = 1u;
            glyph_run.glyphIndices = &glyph_index;
            glyph_run.glyphAdvances = &advance;

            ScalerMode const mode = scaler_mode(impl, size, raster_policy);
            IDWriteRenderingParams* const params =
                rendering_params(impl->context, mode.rendering_mode);
            ASSERT(params != nullptr);
            IDWriteGlyphRunAnalysis* analysis = nullptr;
            HRESULT hr = impl->context->factory2->CreateGlyphRunAnalysis(
                &glyph_run,
                nullptr,
                mode.rendering_mode,
                mode.measuring_mode,
                mode.grid_fit_mode,
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
                mode.measuring_mode,
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
            hr = context->factory->CreateTextAnalyzer(&context->text_analyzer);
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory2->GetSystemFontFallback(&context->system_fallback);
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
            release_com(context->natural_rendering_params);
            release_com(context->sharp_rendering_params);
            release_com(context->gdi_interop);
            release_com(context->system_fallback);
            release_com(context->text_analyzer);
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
        destroy_fallback_fonts(impl);
        release_com(impl->bitmap_target1);
        release_com(impl->bitmap_target);
        release_com(impl->smooth_rendering_params);
        release_com(impl->natural_rendering_params);
        release_com(impl->sharp_rendering_params);
        release_com(impl->gdi_interop);
        release_com(impl->system_fallback);
        release_com(impl->text_analyzer);
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

        raster_glyph(impl, size, glyph_index, DEFAULT_RASTER_POLICY, 0u, 0u, arena, out_raster);
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
            font, size, glyph_index, DEFAULT_RASTER_POLICY, phase_x, phase_y, arena, out_raster
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
