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
        constexpr DWRITE_RENDERING_MODE TEXT_RENDERING_MODE = DWRITE_RENDERING_MODE_GDI_NATURAL;
        constexpr DWRITE_GRID_FIT_MODE TEXT_GRID_FIT_MODE = DWRITE_GRID_FIT_MODE_ENABLED;
        constexpr DWRITE_TEXT_ANTIALIAS_MODE TEXT_ANTIALIAS_MODE =
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        constexpr DWRITE_TEXT_ANTIALIAS_MODE TEXT_LCD_ANTIALIAS_MODE =
            DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
        constexpr DWRITE_TEXTURE_TYPE TEXT_BOUNDS_TYPE = DWRITE_TEXTURE_ALIASED_1x1;
        constexpr DWRITE_TEXTURE_TYPE TEXT_LCD_TEXTURE_TYPE = DWRITE_TEXTURE_CLEARTYPE_3x1;
        constexpr float TEXT_LCD_MAX_SIZE = 20.0f;
        constexpr LONG GLYPH_RASTER_PADDING = 1;
        constexpr float TEXT_PADDING = 2.0f;

        struct FontImpl;

        struct ContextImpl {
            Arena* arena = nullptr;
            IDWriteFactory* factory = nullptr;
            IDWriteFactory2* factory2 = nullptr;
            IDWriteGdiInterop* gdi_interop = nullptr;
            IDWriteFontFallback* font_fallback = nullptr;
            IDWriteTextAnalyzer* analyzer = nullptr;
            IDWriteRenderingParams* rendering_params = nullptr;
            IDWriteBitmapRenderTarget* bitmap_target = nullptr;
            IDWriteBitmapRenderTarget1* bitmap_target1 = nullptr;
            FontImpl* first_fallback_font = nullptr;
        };

        struct FontFaceIdentity {
            IUnknown* loader_identity = nullptr;
            uint8_t* key = nullptr;
            uint32_t key_size = 0u;
            uint32_t face_index = 0u;
            DWRITE_FONT_SIMULATIONS simulations = DWRITE_FONT_SIMULATIONS_NONE;
            bool valid = false;
        };

        struct FontImpl {
            FontImpl* next_fallback = nullptr;
            ContextImpl* context = nullptr;
            IDWriteFontCollection* font_collection = nullptr;
            IDWriteFont* dwrite_font = nullptr;
            FontFaceIdentity face_identity = {};
            IDWriteFontFile* font_file = nullptr;
            IDWriteFontFace* font_face = nullptr;
            wchar_t* family_name = nullptr;
        };

        struct ScriptRun {
            uint32_t start = 0u;
            uint32_t length = 0u;
            DWRITE_SCRIPT_ANALYSIS script = {};
        };

        struct BidiRun {
            uint32_t start = 0u;
            uint32_t length = 0u;
            uint8_t level = 0u;
        };

        struct FontRun {
            uint32_t start = 0u;
            uint32_t length = 0u;
            FontImpl* font = nullptr;
            float size = 0.0f;
        };

        struct ShapeRun {
            uint32_t start = 0u;
            uint32_t length = 0u;
            DWRITE_SCRIPT_ANALYSIS script = {};
            uint8_t bidi_level = 0u;
            FontImpl* font = nullptr;
            float size = 0.0f;
        };

        struct TextAnalysisSource final : IDWriteTextAnalysisSource {
            wchar_t const* text = nullptr;
            uint32_t text_length = 0u;
            ULONG ref_count = 1u;

            auto STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) -> HRESULT override {
                if (object == nullptr) {
                    return E_POINTER;
                }

                if (IsEqualGUID(iid, __uuidof(IUnknown)) ||
                    IsEqualGUID(iid, __uuidof(IDWriteTextAnalysisSource))) {
                    *object = this;
                    AddRef();
                    return S_OK;
                }

                *object = nullptr;
                return E_NOINTERFACE;
            }

            auto STDMETHODCALLTYPE AddRef() -> ULONG override {
                ref_count += 1u;
                return ref_count;
            }

            auto STDMETHODCALLTYPE Release() -> ULONG override {
                ref_count -= 1u;
                return ref_count;
            }

            auto STDMETHODCALLTYPE
            GetTextAtPosition(UINT32 text_position, WCHAR const** out_text, UINT32* out_length)
                -> HRESULT override {
                if (out_text == nullptr || out_length == nullptr) {
                    return E_POINTER;
                }

                if (text_position >= text_length) {
                    *out_text = nullptr;
                    *out_length = 0u;
                    return S_OK;
                }

                *out_text = text + text_position;
                *out_length = text_length - text_position;
                return S_OK;
            }

            auto STDMETHODCALLTYPE
            GetTextBeforePosition(UINT32 text_position, WCHAR const** out_text, UINT32* out_length)
                -> HRESULT override {
                if (out_text == nullptr || out_length == nullptr) {
                    return E_POINTER;
                }

                if (text_position == 0u || text_position > text_length) {
                    *out_text = nullptr;
                    *out_length = 0u;
                    return S_OK;
                }

                *out_text = text;
                *out_length = text_position;
                return S_OK;
            }

            auto STDMETHODCALLTYPE GetParagraphReadingDirection()
                -> DWRITE_READING_DIRECTION override {
                return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
            }

            auto STDMETHODCALLTYPE
            GetLocaleName(UINT32 text_position, UINT32* out_length, WCHAR const** out_locale)
                -> HRESULT override {
                if (out_length == nullptr || out_locale == nullptr) {
                    return E_POINTER;
                }

                static wchar_t const LOCALE_NAME[] = L"en-us";
                *out_locale = LOCALE_NAME;
                *out_length = text_position < text_length ? text_length - text_position : 0u;
                return S_OK;
            }

            auto STDMETHODCALLTYPE GetNumberSubstitution(
                UINT32 text_position,
                UINT32* out_length,
                IDWriteNumberSubstitution** out_substitution
            ) -> HRESULT override {
                if (out_length == nullptr || out_substitution == nullptr) {
                    return E_POINTER;
                }

                *out_substitution = nullptr;
                *out_length = text_position < text_length ? text_length - text_position : 0u;
                return S_OK;
            }
        };

        struct TextAnalysisSink final : IDWriteTextAnalysisSink {
            ScriptRun* script_runs = nullptr;
            uint32_t script_run_capacity = 0u;
            uint32_t script_run_count = 0u;
            BidiRun* bidi_runs = nullptr;
            uint32_t bidi_run_capacity = 0u;
            uint32_t bidi_run_count = 0u;
            ULONG ref_count = 1u;

            auto STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) -> HRESULT override {
                if (object == nullptr) {
                    return E_POINTER;
                }

                if (IsEqualGUID(iid, __uuidof(IUnknown)) ||
                    IsEqualGUID(iid, __uuidof(IDWriteTextAnalysisSink))) {
                    *object = this;
                    AddRef();
                    return S_OK;
                }

                *object = nullptr;
                return E_NOINTERFACE;
            }

            auto STDMETHODCALLTYPE AddRef() -> ULONG override {
                ref_count += 1u;
                return ref_count;
            }

            auto STDMETHODCALLTYPE Release() -> ULONG override {
                ref_count -= 1u;
                return ref_count;
            }

            auto STDMETHODCALLTYPE SetScriptAnalysis(
                UINT32 text_position,
                UINT32 text_length,
                DWRITE_SCRIPT_ANALYSIS const* script_analysis
            ) -> HRESULT override {
                if (script_analysis == nullptr) {
                    return E_POINTER;
                }
                if (script_run_count >= script_run_capacity) {
                    return E_FAIL;
                }

                script_runs[script_run_count] = {text_position, text_length, *script_analysis};
                script_run_count += 1u;
                return S_OK;
            }

            auto STDMETHODCALLTYPE SetLineBreakpoints(
                UINT32 text_position,
                UINT32 text_length,
                DWRITE_LINE_BREAKPOINT const* line_breakpoints
            ) -> HRESULT override {
                BASE_UNUSED(text_position);
                BASE_UNUSED(text_length);
                BASE_UNUSED(line_breakpoints);
                return S_OK;
            }

            auto STDMETHODCALLTYPE SetBidiLevel(
                UINT32 text_position, UINT32 text_length, UINT8 explicit_level, UINT8 resolved_level
            ) -> HRESULT override {
                BASE_UNUSED(explicit_level);
                if (bidi_run_count >= bidi_run_capacity) {
                    return E_FAIL;
                }

                bidi_runs[bidi_run_count] = {text_position, text_length, resolved_level};
                bidi_run_count += 1u;
                return S_OK;
            }

            auto STDMETHODCALLTYPE SetNumberSubstitution(
                UINT32 text_position, UINT32 text_length, IDWriteNumberSubstitution* substitution
            ) -> HRESULT override {
                BASE_UNUSED(text_position);
                BASE_UNUSED(text_length);
                BASE_UNUSED(substitution);
                return S_OK;
            }
        };

        template <typename T> auto release_com(T*& value) -> void {
            if (value != nullptr) {
                value->Release();
                value = nullptr;
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

            IDWriteRenderingParams2* params = nullptr;
            hr = context->factory2->CreateCustomRenderingParams(
                1.0f,
                enhanced_contrast,
                enhanced_contrast,
                0.0f,
                DWRITE_PIXEL_GEOMETRY_FLAT,
                TEXT_RENDERING_MODE,
                TEXT_GRID_FIT_MODE,
                &params
            );
            context->rendering_params = params;
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

        [[nodiscard]] auto copy_wide(Arena& arena, wchar_t const* text, int text_len) -> wchar_t* {
            ASSERT(text != nullptr);
            ASSERT(text_len >= 0);

            wchar_t* const result = arena_alloc<wchar_t>(arena, static_cast<size_t>(text_len) + 1u);
            std::memcpy(result, text, sizeof(wchar_t) * static_cast<size_t>(text_len));
            result[text_len] = L'\0';
            return result;
        }

        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float;

        [[nodiscard]] auto font_handle(FontImpl* font) -> Font {
            return {font, Backend::DWRITE};
        }

        [[nodiscard]] auto run_end(uint32_t start, uint32_t length) -> uint32_t {
            return start + length;
        }

        [[nodiscard]] auto glyph_buffer_capacity(uint32_t text_length) -> uint32_t {
            ASSERT(text_length <= (std::numeric_limits<uint32_t>::max() - 16u) / 3u);
            return (text_length * 3u) + 16u;
        }

        [[nodiscard]] auto is_high_surrogate(wchar_t value) -> bool {
            return value >= 0xd800 && value <= 0xdbff;
        }

        [[nodiscard]] auto is_low_surrogate(wchar_t value) -> bool {
            return value >= 0xdc00 && value <= 0xdfff;
        }

        auto utf16_codepoint_at(
            wchar_t const* text,
            uint32_t text_length,
            uint32_t text_position,
            uint32_t& out_codepoint,
            uint32_t& out_length
        ) -> void {
            ASSERT(text != nullptr);
            ASSERT(text_position < text_length);

            wchar_t const first = text[text_position];
            if (is_high_surrogate(first) && text_position + 1u < text_length &&
                is_low_surrogate(text[text_position + 1u])) {
                uint32_t const high = static_cast<uint32_t>(first) - 0xd800u;
                uint32_t const low = static_cast<uint32_t>(text[text_position + 1u]) - 0xdc00u;
                out_codepoint = 0x10000u + ((high << 10u) | low);
                out_length = 2u;
                return;
            }

            out_codepoint = static_cast<uint32_t>(first);
            out_length = 1u;
        }

        auto utf16_codepoint_before(
            wchar_t const* text,
            uint32_t text_position,
            uint32_t& out_start,
            uint32_t& out_codepoint,
            uint32_t& out_length
        ) -> void {
            ASSERT(text != nullptr);
            ASSERT(text_position != 0u);

            out_start = text_position - 1u;
            if (out_start != 0u && is_low_surrogate(text[out_start]) &&
                is_high_surrogate(text[out_start - 1u])) {
                out_start -= 1u;
            }
            wchar_t const first = text[out_start];
            if (is_high_surrogate(first) && out_start + 1u < text_position &&
                is_low_surrogate(text[out_start + 1u])) {
                uint32_t const high = static_cast<uint32_t>(first) - 0xd800u;
                uint32_t const low = static_cast<uint32_t>(text[out_start + 1u]) - 0xdc00u;
                out_codepoint = 0x10000u + ((high << 10u) | low);
                out_length = 2u;
                return;
            }

            out_codepoint = static_cast<uint32_t>(first);
            out_length = 1u;
        }

        [[nodiscard]] auto is_combining_codepoint(uint32_t codepoint) -> bool {
            return (codepoint >= 0x0300u && codepoint <= 0x036fu) ||
                   (codepoint >= 0x0591u && codepoint <= 0x05bdu) || codepoint == 0x05bfu ||
                   (codepoint >= 0x05c1u && codepoint <= 0x05c2u) ||
                   (codepoint >= 0x05c4u && codepoint <= 0x05c5u) || codepoint == 0x05c7u ||
                   (codepoint >= 0x0610u && codepoint <= 0x061au) ||
                   (codepoint >= 0x064bu && codepoint <= 0x065fu) || codepoint == 0x0670u ||
                   (codepoint >= 0x06d6u && codepoint <= 0x06edu) ||
                   (codepoint >= 0x1ab0u && codepoint <= 0x1affu) ||
                   (codepoint >= 0x1dc0u && codepoint <= 0x1dffu) ||
                   (codepoint >= 0x20d0u && codepoint <= 0x20ffu) ||
                   (codepoint >= 0xfe20u && codepoint <= 0xfe2fu);
        }

        [[nodiscard]] auto is_variation_selector(uint32_t codepoint) -> bool {
            return (codepoint >= 0xfe00u && codepoint <= 0xfe0fu) ||
                   (codepoint >= 0xe0100u && codepoint <= 0xe01efu);
        }

        [[nodiscard]] auto is_cluster_extender(uint32_t codepoint) -> bool {
            return is_combining_codepoint(codepoint) || is_variation_selector(codepoint) ||
                   (codepoint >= 0x1f3fbu && codepoint <= 0x1f3ffu);
        }

        [[nodiscard]] auto
        grapheme_cluster_start(wchar_t const* text, uint32_t text_length, uint32_t text_position)
            -> uint32_t {
            uint32_t start = text_position;
            for (;;) {
                uint32_t codepoint = 0u;
                uint32_t codepoint_length = 0u;
                utf16_codepoint_at(text, text_length, start, codepoint, codepoint_length);
                BASE_UNUSED(codepoint_length);
                if (start == 0u) {
                    return start;
                }

                uint32_t previous_start = 0u;
                uint32_t previous_codepoint = 0u;
                uint32_t previous_length = 0u;
                utf16_codepoint_before(
                    text, start, previous_start, previous_codepoint, previous_length
                );
                BASE_UNUSED(previous_length);
                if (!is_cluster_extender(codepoint) && codepoint != 0x200du &&
                    previous_codepoint != 0x200du) {
                    return start;
                }
                start = previous_start;
            }
        }

        [[nodiscard]] auto
        grapheme_cluster_end(wchar_t const* text, uint32_t text_length, uint32_t text_position)
            -> uint32_t {
            uint32_t position = text_position;
            while (position < text_length) {
                uint32_t codepoint = 0u;
                uint32_t codepoint_length = 0u;
                utf16_codepoint_at(text, text_length, position, codepoint, codepoint_length);
                BASE_UNUSED(codepoint);
                position += codepoint_length;

                for (;;) {
                    if (position >= text_length) {
                        return position;
                    }

                    uint32_t next_codepoint = 0u;
                    uint32_t next_length = 0u;
                    utf16_codepoint_at(text, text_length, position, next_codepoint, next_length);
                    if (!is_cluster_extender(next_codepoint)) {
                        break;
                    }
                    position += next_length;
                }

                if (position >= text_length) {
                    return position;
                }

                uint32_t next_codepoint = 0u;
                uint32_t next_length = 0u;
                utf16_codepoint_at(text, text_length, position, next_codepoint, next_length);
                if (next_codepoint != 0x200du) {
                    return position;
                }
                position += next_length;
            }

            return position;
        }

        auto build_glyph_clusters(
            uint16_t const* cluster_map,
            uint32_t text_length,
            uint32_t glyph_count,
            bool right_to_left,
            uint32_t* logical_clusters,
            uint32_t* glyph_clusters
        ) -> void {
            constexpr uint32_t INVALID_CLUSTER = UINT32_MAX;
            for (uint32_t index = 0u; index < glyph_count; ++index) {
                logical_clusters[index] = INVALID_CLUSTER;
                glyph_clusters[index] = INVALID_CLUSTER;
            }
            for (uint32_t index = 0u; index < text_length; ++index) {
                uint32_t const glyph_index = cluster_map[index];
                if (glyph_index < glyph_count) {
                    logical_clusters[glyph_index] = std::min(logical_clusters[glyph_index], index);
                }
            }

            uint32_t start = 0u;
            while (start < glyph_count) {
                while (start < glyph_count && logical_clusters[start] == INVALID_CLUSTER) {
                    start += 1u;
                }
                if (start >= glyph_count) {
                    break;
                }

                uint32_t end = start + 1u;
                while (end < glyph_count && logical_clusters[end] == INVALID_CLUSTER) {
                    end += 1u;
                }

                uint32_t const out_start = right_to_left ? glyph_count - end : start;
                uint32_t const out_end = right_to_left ? glyph_count - start : end;
                for (uint32_t index = out_start; index < out_end; ++index) {
                    glyph_clusters[index] = logical_clusters[start];
                }
                start = end;
            }

            uint32_t cluster = 0u;
            for (uint32_t index = 0u; index < glyph_count; ++index) {
                if (glyph_clusters[index] == INVALID_CLUSTER) {
                    glyph_clusters[index] = cluster;
                } else {
                    cluster = glyph_clusters[index];
                }
            }
        }

        [[nodiscard]] auto font_supports_codepoint(FontImpl* font, uint32_t codepoint) -> bool {
            ASSERT(font != nullptr);
            ASSERT(font->font_face != nullptr);

            UINT32 const dwrite_codepoint = codepoint;
            UINT16 glyph_index = 0u;
            HRESULT const hr =
                font->font_face->GetGlyphIndices(&dwrite_codepoint, 1u, &glyph_index);
            ASSERT(SUCCEEDED(hr));
            BASE_UNUSED(hr);
            return glyph_index != 0u;
        }

        auto fallback_script_run(ScriptRun* runs, uint32_t text_length, uint32_t& run_count)
            -> void {
            if (run_count == 0u) {
                runs[0u] = {0u, text_length, {}};
                run_count = 1u;
            }
        }

        auto fallback_bidi_run(BidiRun* runs, uint32_t text_length, uint32_t& run_count) -> void {
            if (run_count == 0u) {
                runs[0u] = {0u, text_length, 0u};
                run_count = 1u;
            }
        }

        [[nodiscard]] auto is_rtl(uint8_t bidi_level) -> BOOL {
            return (bidi_level & 1u) != 0u ? TRUE : FALSE;
        }

        auto append_shape_run(
            ShapeRun* runs,
            uint32_t& run_count,
            uint32_t start,
            uint32_t length,
            DWRITE_SCRIPT_ANALYSIS script,
            uint8_t bidi_level,
            FontImpl* font,
            float size
        ) -> void {
            if (length == 0u) {
                return;
            }

            runs[run_count] = {start, length, script, bidi_level, font, size};
            run_count += 1u;
        }

        auto build_shape_runs(
            ScriptRun const* script_runs,
            uint32_t script_run_count,
            FontRun const* font_runs,
            uint32_t font_run_count,
            BidiRun const* bidi_runs,
            uint32_t bidi_run_count,
            ShapeRun* shape_runs,
            uint32_t& shape_run_count
        ) -> void {
            shape_run_count = 0u;
            uint32_t script_index = 0u;
            uint32_t font_index = 0u;
            uint32_t bidi_index = 0u;

            while (script_index < script_run_count && font_index < font_run_count &&
                   bidi_index < bidi_run_count) {
                ScriptRun const& script_run = script_runs[script_index];
                FontRun const& font_run = font_runs[font_index];
                BidiRun const& bidi_run = bidi_runs[bidi_index];
                uint32_t const script_end = run_end(script_run.start, script_run.length);
                uint32_t const font_end = run_end(font_run.start, font_run.length);
                uint32_t const bidi_end = run_end(bidi_run.start, bidi_run.length);
                uint32_t const run_start =
                    std::max(std::max(script_run.start, font_run.start), bidi_run.start);
                uint32_t const run_end_pos = std::min(std::min(script_end, font_end), bidi_end);

                if (run_start < run_end_pos) {
                    append_shape_run(
                        shape_runs,
                        shape_run_count,
                        run_start,
                        run_end_pos - run_start,
                        script_run.script,
                        bidi_run.level,
                        font_run.font,
                        font_run.size
                    );
                }

                script_index += script_end == run_end_pos ? 1u : 0u;
                font_index += font_end == run_end_pos ? 1u : 0u;
                bidi_index += bidi_end == run_end_pos ? 1u : 0u;
            }
        }

        auto reorder_shape_runs(ShapeRun* runs, uint32_t run_count) -> void {
            if (run_count == 0u) {
                return;
            }

            uint8_t max_level = 0u;
            uint8_t min_odd_level = UINT8_MAX;
            for (uint32_t index = 0u; index < run_count; ++index) {
                max_level = std::max(max_level, runs[index].bidi_level);
                if ((runs[index].bidi_level & 1u) != 0u) {
                    min_odd_level = std::min(min_odd_level, runs[index].bidi_level);
                }
            }
            if (min_odd_level == UINT8_MAX) {
                return;
            }

            uint8_t level = max_level;
            for (;;) {
                uint32_t index = 0u;
                while (index < run_count) {
                    while (index < run_count && runs[index].bidi_level < level) {
                        index += 1u;
                    }
                    uint32_t const start = index;
                    while (index < run_count && runs[index].bidi_level >= level) {
                        index += 1u;
                    }
                    std::reverse(runs + start, runs + index);
                }
                if (level == min_odd_level) {
                    break;
                }
                level -= 1u;
            }
        }

        auto release_face_identity(FontFaceIdentity& identity) -> void {
            release_com(identity.loader_identity);
            identity.key = nullptr;
            identity.key_size = 0u;
            identity.face_index = 0u;
            identity.simulations = DWRITE_FONT_SIMULATIONS_NONE;
            identity.valid = false;
        }

        auto init_face_identity(Arena& arena, IDWriteFontFace* face, FontFaceIdentity& out_identity)
            -> void {
            ASSERT(face != nullptr);

            UINT32 file_count = 0u;
            HRESULT hr = face->GetFiles(&file_count, nullptr);
            ASSERT(SUCCEEDED(hr));
            ASSERT(file_count != 0u);

            ArenaTemp temp = begin_thread_temp_arena();
            IDWriteFontFile** const files =
                arena_alloc<IDWriteFontFile*>(*temp.arena(), file_count);
            hr = face->GetFiles(&file_count, files);
            ASSERT(SUCCEEDED(hr));

            IDWriteFontFileLoader* loader = nullptr;
            hr = files[0u]->GetLoader(&loader);
            ASSERT(SUCCEEDED(hr));
            ASSERT(loader != nullptr);

            IUnknown* loader_identity = nullptr;
            hr = loader->QueryInterface(
                __uuidof(IUnknown), reinterpret_cast<void**>(&loader_identity)
            );
            ASSERT(SUCCEEDED(hr));
            ASSERT(loader_identity != nullptr);

            void const* key = nullptr;
            UINT32 key_size = 0u;
            hr = files[0u]->GetReferenceKey(&key, &key_size);
            ASSERT(SUCCEEDED(hr));
            ASSERT(key != nullptr);
            ASSERT(key_size != 0u);

            uint8_t* const key_copy = arena_alloc<uint8_t>(arena, key_size);
            std::memcpy(key_copy, key, key_size);

            release_face_identity(out_identity);
            out_identity.loader_identity = loader_identity;
            out_identity.key = key_copy;
            out_identity.key_size = key_size;
            out_identity.face_index = face->GetIndex();
            out_identity.simulations = face->GetSimulations();
            out_identity.valid = true;

            release_com(loader);
            for (UINT32 index = 0u; index < file_count; ++index) {
                release_com(files[index]);
            }
        }

        [[nodiscard]] auto
        face_identity_equal(FontFaceIdentity const& lhs, FontFaceIdentity const& rhs) -> bool {
            return lhs.valid && rhs.valid && lhs.loader_identity == rhs.loader_identity &&
                   lhs.face_index == rhs.face_index && lhs.simulations == rhs.simulations &&
                   lhs.key_size == rhs.key_size && std::memcmp(lhs.key, rhs.key, lhs.key_size) == 0;
        }

        auto init_font_identity_from_dwrite_font(
            Arena& arena, IDWriteFont* font, FontFaceIdentity& out_identity
        ) -> void {
            ASSERT(font != nullptr);

            IDWriteFontFace* face = nullptr;
            HRESULT const hr = font->CreateFontFace(&face);
            ASSERT(SUCCEEDED(hr));
            ASSERT(face != nullptr);
            BASE_UNUSED(hr);

            init_face_identity(arena, face, out_identity);
            release_com(face);
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
            release_face_identity(font->face_identity);
            release_com(font->dwrite_font);
            release_com(font->font_collection);
            font->family_name = nullptr;
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
            if (SUCCEEDED(hr) && font->font_face != nullptr) {
                font->font_collection = collection;
                font->font_collection->AddRef();
                font->dwrite_font = dwrite_font;
                font->dwrite_font->AddRef();
                init_face_identity(arena, font->font_face, font->face_identity);
                font->family_name = copy_wide(arena, wide_family, wide_family_len);
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
            if (SUCCEEDED(hr) && font->font_face != nullptr) {
                init_face_identity(arena, font->font_face, font->face_identity);
            }

            BASE_UNUSED(file_type);
            ASSERT(SUCCEEDED(hr));
            ASSERT(supported != FALSE);
            ASSERT(face_count != 0u);
            ASSERT(font->font_face != nullptr);

            out_font.handle = font;
        }

        [[nodiscard]] auto fallback_font_from_dwrite_font(ContextImpl* context, IDWriteFont* source)
            -> FontImpl* {
            ASSERT(context != nullptr);
            ASSERT(context->arena != nullptr);
            ASSERT(source != nullptr);

            ArenaTemp temp = begin_thread_temp_arena();
            FontFaceIdentity source_identity = {};
            init_font_identity_from_dwrite_font(*temp.arena(), source, source_identity);
            for (FontImpl* font = context->first_fallback_font; font != nullptr;
                 font = font->next_fallback) {
                if (face_identity_equal(font->face_identity, source_identity)) {
                    release_face_identity(source_identity);
                    return font;
                }
            }

            FontImpl* const font = create_font_impl(*context->arena, context);
            font->dwrite_font = source;
            font->dwrite_font->AddRef();
            HRESULT const hr = source->CreateFontFace(&font->font_face);
            ASSERT(SUCCEEDED(hr));
            ASSERT(font->font_face != nullptr);
            BASE_UNUSED(hr);
            init_face_identity(*context->arena, font->font_face, font->face_identity);
            release_face_identity(source_identity);

            font->next_fallback = context->first_fallback_font;
            context->first_fallback_font = font;
            return font;
        }

        auto append_font_run(
            FontRun* runs,
            uint32_t& run_count,
            uint32_t start,
            uint32_t length,
            FontImpl* font,
            float size
        ) -> void {
            if (length == 0u) {
                return;
            }
            if (run_count != 0u) {
                FontRun& previous = runs[run_count - 1u];
                if (previous.font == font && previous.size == size &&
                    run_end(previous.start, previous.length) == start) {
                    previous.length += length;
                    return;
                }
            }

            runs[run_count] = {start, length, font, size};
            run_count += 1u;
        }

        [[nodiscard]] auto mapped_font_is_base(FontImpl* impl, IDWriteFont* mapped_font) -> bool {
            ASSERT(impl != nullptr);
            ASSERT(mapped_font != nullptr);

            ArenaTemp temp = begin_thread_temp_arena();
            FontFaceIdentity mapped_identity = {};
            init_font_identity_from_dwrite_font(*temp.arena(), mapped_font, mapped_identity);
            bool const result = face_identity_equal(impl->face_identity, mapped_identity);
            release_face_identity(mapped_identity);
            return result;
        }

        auto append_system_font_runs(
            FontImpl* impl,
            TextAnalysisSource* source,
            uint32_t text_length,
            float size,
            FontRun* runs,
            uint32_t& run_count
        ) -> void {
            uint32_t text_position = 0u;
            while (text_position < text_length) {
                UINT32 mapped_length = 0u;
                IDWriteFont* mapped_font = nullptr;
                FLOAT scale = 1.0f;
                HRESULT const hr = impl->context->font_fallback->MapCharacters(
                    source,
                    text_position,
                    text_length - text_position,
                    impl->font_collection,
                    impl->family_name,
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    &mapped_length,
                    &mapped_font,
                    &scale
                );
                ASSERT(SUCCEEDED(hr));
                ASSERT(mapped_length != 0u);
                BASE_UNUSED(hr);

                FontImpl* run_font = impl;
                if (mapped_font != nullptr) {
                    run_font = mapped_font_is_base(impl, mapped_font)
                                   ? impl
                                   : fallback_font_from_dwrite_font(impl->context, mapped_font);
                }

                append_font_run(
                    runs, run_count, text_position, mapped_length, run_font, size * scale
                );
                release_com(mapped_font);
                text_position += mapped_length;
            }
        }

        auto append_file_font_runs(
            FontImpl* impl,
            TextAnalysisSource* source,
            wchar_t const* wide_text,
            uint32_t text_length,
            float size,
            FontRun* runs,
            uint32_t& run_count
        ) -> void {
            uint32_t base_start = 0u;
            uint32_t text_position = 0u;
            while (text_position < text_length) {
                uint32_t codepoint = 0u;
                uint32_t codepoint_length = 0u;
                utf16_codepoint_at(
                    wide_text, text_length, text_position, codepoint, codepoint_length
                );
                if (font_supports_codepoint(impl, codepoint)) {
                    text_position += codepoint_length;
                    continue;
                }

                uint32_t const fallback_start =
                    grapheme_cluster_start(wide_text, text_length, text_position);
                uint32_t const fallback_end =
                    grapheme_cluster_end(wide_text, text_length, fallback_start);
                append_font_run(
                    runs, run_count, base_start, fallback_start - base_start, impl, size
                );

                UINT32 mapped_length = 0u;
                IDWriteFont* mapped_font = nullptr;
                FLOAT scale = 1.0f;
                HRESULT const hr = impl->context->font_fallback->MapCharacters(
                    source,
                    fallback_start,
                    fallback_end - fallback_start,
                    nullptr,
                    nullptr,
                    DWRITE_FONT_WEIGHT_REGULAR,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    &mapped_length,
                    &mapped_font,
                    &scale
                );
                ASSERT(SUCCEEDED(hr));
                ASSERT(mapped_length != 0u);
                BASE_UNUSED(hr);

                FontImpl* run_font =
                    mapped_font != nullptr
                        ? fallback_font_from_dwrite_font(impl->context, mapped_font)
                        : impl;
                append_font_run(
                    runs,
                    run_count,
                    fallback_start,
                    std::max(mapped_length, fallback_end - fallback_start),
                    run_font,
                    size * scale
                );
                release_com(mapped_font);
                text_position = fallback_end;
                base_start = text_position;
            }

            append_font_run(runs, run_count, base_start, text_length - base_start, impl, size);
        }

        auto build_font_runs(
            FontImpl* impl,
            TextAnalysisSource* source,
            StrRef text,
            wchar_t const* wide_text,
            uint32_t text_length,
            float size,
            FontRun* runs,
            uint32_t& run_count
        ) -> void {
            ASSERT(impl != nullptr);
            ASSERT(source != nullptr);
            ASSERT(runs != nullptr);

            run_count = 0u;
            if (text.is_ascii() || impl->context->font_fallback == nullptr) {
                append_font_run(runs, run_count, 0u, text_length, impl, size);
                return;
            }

            if (impl->font_collection != nullptr && impl->family_name != nullptr) {
                append_system_font_runs(impl, source, text_length, size, runs, run_count);
                return;
            }

            append_file_font_runs(impl, source, wide_text, text_length, size, runs, run_count);
        }

        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float {
            ASSERT(metrics.designUnitsPerEm != 0u);
            return size / static_cast<float>(metrics.designUnitsPerEm);
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

        auto lcd_texture_to_rgba(
            uint8_t const* texture, uint32_t width, uint32_t height, uint8_t* pixels
        ) -> void {
            for (uint32_t y = 0u; y < height; ++y) {
                uint8_t const* src = texture + (static_cast<size_t>(y) * width * 3u);
                uint8_t* dst = pixels + (static_cast<size_t>(y) * width * 4u);
                for (uint32_t x = 0u; x < width; ++x) {
                    uint8_t const r = src[0u];
                    uint8_t const g = src[1u];
                    uint8_t const b = src[2u];
                    dst[0u] = r;
                    dst[1u] = g;
                    dst[2u] = b;
                    dst[3u] = std::max(r, std::max(g, b));
                    src += 3u;
                    dst += 4u;
                }
            }
        }

        auto shape_text(FontImpl* impl, float size, StrRef text, Arena& arena, ShapedText& out_text)
            -> void {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);
            ASSERT(impl->context != nullptr);
            ASSERT(impl->context->analyzer != nullptr);

            ArenaTemp temp = begin_thread_temp_arena(1u);
            wchar_t* wide_text = nullptr;
            int wide_len = 0;
            ASSERT(utf8_to_wide(text, *temp.arena(), wide_text, wide_len));
            ASSERT(wide_len >= 0);

            uint32_t const text_length = static_cast<uint32_t>(wide_len);
            if (text_length == 0u) {
                out_text = {};
                return;
            }

            ScriptRun* const script_runs = arena_alloc<ScriptRun>(*temp.arena(), text_length);
            BidiRun* const bidi_runs = arena_alloc<BidiRun>(*temp.arena(), text_length);
            TextAnalysisSource source = {};
            source.text = wide_text;
            source.text_length = text_length;

            TextAnalysisSink sink = {};
            sink.script_runs = script_runs;
            sink.script_run_capacity = text_length;
            sink.bidi_runs = bidi_runs;
            sink.bidi_run_capacity = text_length;

            HRESULT hr = impl->context->analyzer->AnalyzeScript(&source, 0u, text_length, &sink);
            ASSERT(SUCCEEDED(hr));
            fallback_script_run(script_runs, text_length, sink.script_run_count);
            if (text.is_ascii()) {
                fallback_bidi_run(bidi_runs, text_length, sink.bidi_run_count);
            } else {
                hr = impl->context->analyzer->AnalyzeBidi(&source, 0u, text_length, &sink);
                ASSERT(SUCCEEDED(hr));
                fallback_bidi_run(bidi_runs, text_length, sink.bidi_run_count);
            }

            FontRun* const font_runs = arena_alloc<FontRun>(*temp.arena(), text_length);
            uint32_t font_run_count = 0u;
            uint32_t const max_glyphs = glyph_buffer_capacity(text_length);
            build_font_runs(
                impl, &source, text, wide_text, text_length, size, font_runs, font_run_count
            );

            ShapeRun* const shape_runs = arena_alloc<ShapeRun>(*temp.arena(), max_glyphs);
            uint32_t shape_run_count = 0u;
            build_shape_runs(
                script_runs,
                sink.script_run_count,
                font_runs,
                font_run_count,
                bidi_runs,
                sink.bidi_run_count,
                shape_runs,
                shape_run_count
            );
            reorder_shape_runs(shape_runs, shape_run_count);

            ShapedGlyph* const shaped_glyphs = arena_alloc<ShapedGlyph>(arena, max_glyphs);
            float pen_x = 0.0f;
            uint32_t total_glyph_count = 0u;
            float max_ascent = 0.0f;
            float max_descent = 0.0f;

            for (uint32_t run_index = 0u; run_index < shape_run_count; ++run_index) {
                ShapeRun const& shape_run = shape_runs[run_index];
                uint32_t const run_start = shape_run.start;
                uint32_t const run_length = shape_run.length;
                uint32_t glyph_capacity = glyph_buffer_capacity(run_length);
                uint16_t* cluster_map = nullptr;
                DWRITE_SHAPING_TEXT_PROPERTIES* text_props = nullptr;
                uint16_t* glyph_indices = nullptr;
                DWRITE_SHAPING_GLYPH_PROPERTIES* glyph_props = nullptr;
                uint32_t glyph_count = 0u;

                for (;;) {
                    cluster_map = arena_alloc<uint16_t>(*temp.arena(), run_length);
                    text_props =
                        arena_alloc<DWRITE_SHAPING_TEXT_PROPERTIES>(*temp.arena(), run_length);
                    glyph_indices = arena_alloc<uint16_t>(*temp.arena(), glyph_capacity);
                    glyph_props =
                        arena_alloc<DWRITE_SHAPING_GLYPH_PROPERTIES>(*temp.arena(), glyph_capacity);

                    hr = impl->context->analyzer->GetGlyphs(
                        wide_text + run_start,
                        run_length,
                        shape_run.font->font_face,
                        FALSE,
                        is_rtl(shape_run.bidi_level),
                        &shape_run.script,
                        L"en-us",
                        nullptr,
                        nullptr,
                        nullptr,
                        0u,
                        glyph_capacity,
                        cluster_map,
                        text_props,
                        glyph_indices,
                        glyph_props,
                        &glyph_count
                    );
                    if (hr != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) {
                        break;
                    }

                    ASSERT(glyph_capacity <= std::numeric_limits<uint32_t>::max() / 2u);
                    glyph_capacity *= 2u;
                }
                ASSERT(SUCCEEDED(hr));
                ASSERT(total_glyph_count <= max_glyphs);
                ASSERT(glyph_count <= max_glyphs - total_glyph_count);

                FLOAT* const advances = arena_alloc<FLOAT>(*temp.arena(), glyph_count);
                DWRITE_GLYPH_OFFSET* const offsets =
                    arena_alloc<DWRITE_GLYPH_OFFSET>(*temp.arena(), glyph_count);
                hr = impl->context->analyzer->GetGlyphPlacements(
                    wide_text + run_start,
                    cluster_map,
                    text_props,
                    run_length,
                    glyph_indices,
                    glyph_props,
                    glyph_count,
                    shape_run.font->font_face,
                    shape_run.size,
                    FALSE,
                    is_rtl(shape_run.bidi_level),
                    &shape_run.script,
                    L"en-us",
                    nullptr,
                    nullptr,
                    0u,
                    advances,
                    offsets
                );
                ASSERT(SUCCEEDED(hr));

                uint32_t* const logical_clusters =
                    arena_alloc<uint32_t>(*temp.arena(), glyph_count);
                uint32_t* const glyph_clusters = arena_alloc<uint32_t>(*temp.arena(), glyph_count);
                build_glyph_clusters(
                    cluster_map,
                    run_length,
                    glyph_count,
                    is_rtl(shape_run.bidi_level) != FALSE,
                    logical_clusters,
                    glyph_clusters
                );
                for (uint32_t glyph_index = 0u; glyph_index < glyph_count; ++glyph_index) {
                    ShapedGlyph& glyph = shaped_glyphs[total_glyph_count + glyph_index];
                    glyph.font = font_handle(shape_run.font);
                    glyph.glyph_index = glyph_indices[glyph_index];
                    glyph.cluster = run_start + glyph_clusters[glyph_index];
                    glyph.size = shape_run.size;
                    glyph.x = pen_x;
                    glyph.advance = advances[glyph_index];
                    glyph.offset_x = offsets[glyph_index].advanceOffset;
                    glyph.offset_y = offsets[glyph_index].ascenderOffset;
                    pen_x += advances[glyph_index];
                }

                total_glyph_count += glyph_count;

                DWRITE_FONT_METRICS font_metrics = {};
                shape_run.font->font_face->GetMetrics(&font_metrics);
                float const metric_scale = metrics_scale(font_metrics, shape_run.size);
                max_ascent =
                    std::max(max_ascent, static_cast<float>(font_metrics.ascent) * metric_scale);
                max_descent =
                    std::max(max_descent, static_cast<float>(font_metrics.descent) * metric_scale);
            }

            uint32_t bitmap_width = 0u;
            uint32_t bitmap_height = 0u;
            ASSERT(ceil_u32(std::max(1.0f, pen_x + (TEXT_PADDING * 2.0f)), bitmap_width));
            ASSERT(ceil_u32(
                std::max(1.0f, max_ascent + max_descent + (TEXT_PADDING * 2.0f)), bitmap_height
            ));

            out_text = {};
            out_text.glyphs = shaped_glyphs;
            out_text.glyph_count = total_glyph_count;
            out_text.advance = pen_x;
            out_text.origin_x = TEXT_PADDING;
            out_text.origin_y = TEXT_PADDING;
            out_text.baseline_y = TEXT_PADDING + max_ascent;
            out_text.height = static_cast<float>(bitmap_height);
            out_text.size = {bitmap_width, bitmap_height};
        }

        auto raster_glyph(
            FontImpl* impl,
            float size,
            uint16_t glyph_index,
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
            glyph_run.fontEmSize = size;
            glyph_run.glyphCount = 1u;
            glyph_run.glyphIndices = &glyph_index;
            glyph_run.glyphAdvances = &advance;

            IDWriteGlyphRunAnalysis* analysis = nullptr;
            HRESULT hr = impl->context->factory2->CreateGlyphRunAnalysis(
                &glyph_run,
                nullptr,
                TEXT_RENDERING_MODE,
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
            hr = analysis->GetAlphaTextureBounds(TEXT_BOUNDS_TYPE, &bounds);
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
                impl->context->rendering_params,
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

        [[nodiscard]] auto raster_glyph_lcd(
            FontImpl* impl,
            float size,
            uint16_t glyph_index,
            uint8_t phase_x,
            uint8_t phase_y,
            Arena& arena,
            GlyphRaster& out_raster
        ) -> bool {
            ASSERT(impl != nullptr);
            ASSERT(impl->font_face != nullptr);
            ASSERT(impl->context != nullptr);
            ASSERT(impl->context->factory2 != nullptr);

            FLOAT const advance = 0.0f;
            DWRITE_GLYPH_RUN glyph_run = {};
            glyph_run.fontFace = impl->font_face;
            glyph_run.fontEmSize = size;
            glyph_run.glyphCount = 1u;
            glyph_run.glyphIndices = &glyph_index;
            glyph_run.glyphAdvances = &advance;

            IDWriteGlyphRunAnalysis* analysis = nullptr;
            HRESULT hr = impl->context->factory2->CreateGlyphRunAnalysis(
                &glyph_run,
                nullptr,
                TEXT_RENDERING_MODE,
                TEXT_MEASURING_MODE,
                TEXT_GRID_FIT_MODE,
                TEXT_LCD_ANTIALIAS_MODE,
                glyph_phase(phase_x),
                glyph_phase(phase_y),
                &analysis
            );
            if (FAILED(hr) || analysis == nullptr) {
                return false;
            }

            RECT bounds = {};
            hr = analysis->GetAlphaTextureBounds(TEXT_LCD_TEXTURE_TYPE, &bounds);
            if (FAILED(hr)) {
                release_com(analysis);
                return false;
            }

            uint32_t const width =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.right - bounds.left));
            uint32_t const height =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.bottom - bounds.top));
            if (width == 0u || height == 0u) {
                out_raster = {};
                release_com(analysis);
                return true;
            }

            inflate_bounds(bounds);
            uint32_t const padded_width =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.right - bounds.left));
            uint32_t const padded_height =
                static_cast<uint32_t>(std::max<LONG>(0, bounds.bottom - bounds.top));
            size_t lcd_size = 0u;
            size_t rgba_size = 0u;
            ASSERT(checked_pixel_size(padded_width, padded_height, 3u, lcd_size));
            ASSERT(checked_pixel_size(padded_width, padded_height, 4u, rgba_size));

            uint8_t* const lcd_pixels = arena_alloc<uint8_t>(arena, lcd_size);
            uint8_t* const rgba_pixels = arena_alloc<uint8_t>(arena, rgba_size);
            hr = analysis->CreateAlphaTexture(
                TEXT_LCD_TEXTURE_TYPE, &bounds, lcd_pixels, static_cast<UINT32>(lcd_size)
            );
            if (FAILED(hr)) {
                release_com(analysis);
                return false;
            }

            lcd_texture_to_rgba(lcd_pixels, padded_width, padded_height, rgba_pixels);

            out_raster = {};
            out_raster.size = {padded_width, padded_height};
            out_raster.stride = padded_width * 4u;
            out_raster.pixels = rgba_pixels;
            out_raster.format = RasterFormat::LCD_RGB;
            out_raster.offset_x = static_cast<float>(bounds.left);
            out_raster.offset_y = static_cast<float>(bounds.top);
            release_com(analysis);
            return true;
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

        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_ISOLATED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&context->factory)
        );
        if (SUCCEEDED(hr)) {
            hr = context->factory->QueryInterface(
                __uuidof(IDWriteFactory2), reinterpret_cast<void**>(&context->factory2)
            );
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory->GetGdiInterop(&context->gdi_interop);
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory2->GetSystemFontFallback(&context->font_fallback);
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory->CreateTextAnalyzer(&context->analyzer);
        }
        if (SUCCEEDED(hr)) {
            hr = create_text_rendering_params(context);
        }
        if (FAILED(hr)) {
            release_com(context->bitmap_target1);
            release_com(context->bitmap_target);
            release_com(context->rendering_params);
            release_com(context->analyzer);
            release_com(context->font_fallback);
            release_com(context->gdi_interop);
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
        for (FontImpl* font = impl->first_fallback_font; font != nullptr;) {
            FontImpl* const next_font = font->next_fallback;
            destroy_font_impl(font);
            font = next_font;
        }
        impl->first_fallback_font = nullptr;
        release_com(impl->bitmap_target1);
        release_com(impl->bitmap_target);
        release_com(impl->rendering_params);
        release_com(impl->analyzer);
        release_com(impl->font_fallback);
        release_com(impl->gdi_interop);
        release_com(impl->factory2);
        release_com(impl->factory);
        impl->arena = nullptr;
        context.handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (!desc.data.empty()) {
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

        if (size > TEXT_LCD_MAX_SIZE ||
            !raster_glyph_lcd(impl, size, glyph_index, 0u, 0u, arena, out_raster)) {
            raster_glyph(impl, size, glyph_index, 0u, 0u, arena, out_raster);
        }
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
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->font_face != nullptr);

        if (size > TEXT_LCD_MAX_SIZE ||
            !raster_glyph_lcd(impl, size, glyph_index, phase_x, phase_y, arena, out_raster)) {
            raster_glyph(impl, size, glyph_index, phase_x, phase_y, arena, out_raster);
        }
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
