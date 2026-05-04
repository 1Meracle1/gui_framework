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
#include <dwrite_2.h>
#include <limits>
#include <windows.h>

namespace gui::font_provider::platform {
    namespace {

        constexpr StrRef DEFAULT_FONT_FAMILY = "Segoe UI";
        constexpr DWRITE_MEASURING_MODE TEXT_MEASURING_MODE = DWRITE_MEASURING_MODE_GDI_NATURAL;
        constexpr DWRITE_RENDERING_MODE TEXT_RENDERING_MODE =
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        constexpr DWRITE_GRID_FIT_MODE TEXT_GRID_FIT_MODE = DWRITE_GRID_FIT_MODE_ENABLED;
        constexpr DWRITE_TEXT_ANTIALIAS_MODE TEXT_ANTIALIAS_MODE =
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        constexpr float TEXT_PADDING = 2.0f;
        struct ContextImpl {
            IDWriteFactory* factory = nullptr;
            IDWriteFactory2* factory2 = nullptr;
            IDWriteTextAnalyzer* analyzer = nullptr;
        };

        struct FontImpl {
            ContextImpl* context = nullptr;
            IDWriteFontFile* font_file = nullptr;
            IDWriteFontFace* font_face = nullptr;
        };

        struct ScriptRun {
            uint32_t start = 0u;
            uint32_t length = 0u;
            DWRITE_SCRIPT_ANALYSIS script = {};
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
            ScriptRun* runs = nullptr;
            uint32_t run_capacity = 0u;
            uint32_t run_count = 0u;
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
                if (run_count >= run_capacity) {
                    return E_FAIL;
                }

                runs[run_count] = {text_position, text_length, *script_analysis};
                run_count += 1u;
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
                BASE_UNUSED(text_position);
                BASE_UNUSED(text_length);
                BASE_UNUSED(explicit_level);
                BASE_UNUSED(resolved_level);
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

        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float;

        [[nodiscard]] auto glyph_buffer_capacity(uint32_t text_length) -> uint32_t {
            ASSERT(text_length <= (std::numeric_limits<uint32_t>::max() - 16u) / 3u);
            return (text_length * 3u) + 16u;
        }

        [[nodiscard]] auto
        glyph_cluster(uint16_t const* cluster_map, uint32_t text_length, uint32_t glyph_index)
            -> uint32_t {
            for (uint32_t index = 0u; index < text_length; ++index) {
                if (cluster_map[index] == glyph_index) {
                    return index;
                }
            }

            return 0u;
        }

        auto fallback_script_run(ScriptRun* runs, uint32_t text_length, uint32_t& run_count)
            -> void {
            if (run_count == 0u) {
                runs[0u] = {0u, text_length, {}};
                run_count = 1u;
            }
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

        [[nodiscard]] auto checked_alpha_size(uint32_t width, uint32_t height, size_t& out_size)
            -> bool {
            if (height != 0u &&
                static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height) {
                return false;
            }

            out_size = static_cast<size_t>(width) * static_cast<size_t>(height);
            return true;
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
            TextAnalysisSource source = {};
            source.text = wide_text;
            source.text_length = text_length;

            TextAnalysisSink sink = {};
            sink.runs = script_runs;
            sink.run_capacity = text_length;

            HRESULT hr = impl->context->analyzer->AnalyzeScript(&source, 0u, text_length, &sink);
            ASSERT(SUCCEEDED(hr));
            fallback_script_run(script_runs, text_length, sink.run_count);

            uint32_t const max_glyphs = glyph_buffer_capacity(text_length);
            ShapedGlyph* const shaped_glyphs = arena_alloc<ShapedGlyph>(arena, max_glyphs);
            float pen_x = 0.0f;
            uint32_t total_glyph_count = 0u;

            for (uint32_t run_index = 0u; run_index < sink.run_count; ++run_index) {
                ScriptRun const& run = script_runs[run_index];
                ASSERT(run.start <= text_length);
                ASSERT(run.length <= text_length - run.start);
                if (run.length == 0u) {
                    continue;
                }

                uint32_t glyph_capacity = glyph_buffer_capacity(run.length);
                uint16_t* cluster_map = nullptr;
                DWRITE_SHAPING_TEXT_PROPERTIES* text_props = nullptr;
                uint16_t* glyph_indices = nullptr;
                DWRITE_SHAPING_GLYPH_PROPERTIES* glyph_props = nullptr;
                uint32_t glyph_count = 0u;

                for (;;) {
                    cluster_map = arena_alloc<uint16_t>(*temp.arena(), run.length);
                    text_props =
                        arena_alloc<DWRITE_SHAPING_TEXT_PROPERTIES>(*temp.arena(), run.length);
                    glyph_indices = arena_alloc<uint16_t>(*temp.arena(), glyph_capacity);
                    glyph_props =
                        arena_alloc<DWRITE_SHAPING_GLYPH_PROPERTIES>(*temp.arena(), glyph_capacity);

                    hr = impl->context->analyzer->GetGlyphs(
                        wide_text + run.start,
                        run.length,
                        impl->font_face,
                        FALSE,
                        FALSE,
                        &run.script,
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
                    wide_text + run.start,
                    cluster_map,
                    text_props,
                    run.length,
                    glyph_indices,
                    glyph_props,
                    glyph_count,
                    impl->font_face,
                    size,
                    FALSE,
                    FALSE,
                    &run.script,
                    L"en-us",
                    nullptr,
                    nullptr,
                    0u,
                    advances,
                    offsets
                );
                ASSERT(SUCCEEDED(hr));

                for (uint32_t glyph_index = 0u; glyph_index < glyph_count; ++glyph_index) {
                    ShapedGlyph& glyph = shaped_glyphs[total_glyph_count + glyph_index];
                    glyph.glyph_index = glyph_indices[glyph_index];
                    glyph.cluster = run.start + glyph_cluster(cluster_map, run.length, glyph_index);
                    glyph.x = pen_x;
                    glyph.advance = advances[glyph_index];
                    glyph.offset_x = offsets[glyph_index].advanceOffset;
                    glyph.offset_y = offsets[glyph_index].ascenderOffset;
                    pen_x += advances[glyph_index];
                }

                total_glyph_count += glyph_count;
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

            out_text = {};
            out_text.glyphs = shaped_glyphs;
            out_text.glyph_count = total_glyph_count;
            out_text.advance = pen_x;
            out_text.origin_x = TEXT_PADDING;
            out_text.origin_y = TEXT_PADDING;
            out_text.baseline_y = TEXT_PADDING + ascent;
            out_text.height = static_cast<float>(bitmap_height);
            out_text.size = {bitmap_width, bitmap_height};
        }

        auto raster_glyph(
            FontImpl* impl, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster
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
                0.0f,
                0.0f,
                &analysis
            );
            ASSERT(SUCCEEDED(hr));
            ASSERT(analysis != nullptr);

            RECT bounds = {};
            hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds);
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

            size_t alpha_size = 0u;
            ASSERT(checked_alpha_size(width, height, alpha_size));
            uint8_t* const pixels = arena_alloc<uint8_t>(arena, alpha_size);
            hr = analysis->CreateAlphaTexture(
                DWRITE_TEXTURE_ALIASED_1x1, &bounds, pixels, static_cast<UINT32>(alpha_size)
            );
            ASSERT(SUCCEEDED(hr));

            out_raster = {};
            out_raster.size = {width, height};
            out_raster.stride = width;
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

            int32_t const dst_x = static_cast<int32_t>(
                std::floor(shaped.origin_x + glyph.x + glyph.offset_x + glyph_raster.offset_x)
            );
            int32_t const dst_y = static_cast<int32_t>(
                std::floor(shaped.baseline_y - glyph.offset_y + glyph_raster.offset_y)
            );

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
            hr = context->factory->CreateTextAnalyzer(&context->analyzer);
        }
        if (FAILED(hr)) {
            release_com(context->analyzer);
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
        release_com(impl->analyzer);
        release_com(impl->factory2);
        release_com(impl->factory);
        context.handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

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

        raster_glyph(impl, size, glyph_index, arena, out_raster);
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
        ASSERT(checked_alpha_size(shaped.size.width, shaped.size.height, alpha_byte_count));
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
            raster_glyph(impl, size, shaped.glyphs[index].glyph_index, *temp.arena(), glyph_raster);
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

} // namespace gui::font_provider::platform
