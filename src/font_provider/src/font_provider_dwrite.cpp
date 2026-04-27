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
#include <limits>
#include <windows.h>

namespace gui::font_provider::platform {
    namespace {

        constexpr StrRef DEFAULT_FONT_FAMILY = "Segoe UI";
        constexpr float POINTS_TO_DIPS = 96.0f / 72.0f;

        struct ContextImpl {
            IDWriteFactory* factory = nullptr;
            IDWriteGdiInterop* gdi_interop = nullptr;
            IDWriteRenderingParams* base_rendering_params = nullptr;
            IDWriteRenderingParams* rendering_params = nullptr;
        };

        struct FontImpl {
            ContextImpl* context = nullptr;
            IDWriteFontFile* font_file = nullptr;
            IDWriteFontFace* font_face = nullptr;
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

        [[nodiscard]] auto text_size_to_int(StrRef text, int* out_size) -> bool {
            if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
                return false;
            }

            *out_size = static_cast<int>(text.size());
            return true;
        }

        [[nodiscard]] auto utf8_to_wide(StrRef text, Arena& arena, wchar_t** out_text, int* out_len)
            -> bool {
            ASSERT(out_text != nullptr);
            ASSERT(out_len != nullptr);

            int input_size = 0;
            if (!text_size_to_int(text, &input_size)) {
                return false;
            }

            if (text.empty()) {
                wchar_t* const wide_text = arena_alloc<wchar_t>(arena, 1u);
                wide_text[0] = L'\0';
                *out_text = wide_text;
                *out_len = 0;
                return true;
            }

            int const wide_len = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
            if (wide_len <= 0) {
                return false;
            }

            wchar_t* const wide_text =
                arena_alloc<wchar_t>(arena, static_cast<size_t>(wide_len) + 1u);
            int const converted = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, wide_text, wide_len);
            if (converted != wide_len) {
                return false;
            }

            wide_text[wide_len] = L'\0';
            *out_text = wide_text;
            *out_len = wide_len;
            return true;
        }

        struct DecodeResult {
            uint32_t codepoint = 0u;
            size_t advance = 0u;
            bool ok = false;
        };

        [[nodiscard]] auto is_utf8_trailing_byte(uint8_t byte) -> bool {
            return (byte & 0xc0u) == 0x80u;
        }

        [[nodiscard]] auto decode_utf8_codepoint(uint8_t const* data, size_t size) -> DecodeResult {
            if (data == nullptr || size == 0u) {
                return {};
            }

            uint8_t const b0 = data[0];
            if (b0 < 0x80u) {
                return DecodeResult{b0, 1u, true};
            }

            if ((b0 & 0xe0u) == 0xc0u) {
                if (size < 2u || !is_utf8_trailing_byte(data[1])) {
                    return {};
                }

                uint32_t const codepoint = (static_cast<uint32_t>(b0 & 0x1fu) << 6u) |
                                           static_cast<uint32_t>(data[1] & 0x3fu);
                return codepoint >= 0x80u ? DecodeResult{codepoint, 2u, true} : DecodeResult{};
            }

            if ((b0 & 0xf0u) == 0xe0u) {
                if (size < 3u || !is_utf8_trailing_byte(data[1]) ||
                    !is_utf8_trailing_byte(data[2])) {
                    return {};
                }

                uint32_t const codepoint = (static_cast<uint32_t>(b0 & 0x0fu) << 12u) |
                                           (static_cast<uint32_t>(data[1] & 0x3fu) << 6u) |
                                           static_cast<uint32_t>(data[2] & 0x3fu);
                if (codepoint < 0x800u || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
                    return {};
                }

                return DecodeResult{codepoint, 3u, true};
            }

            if ((b0 & 0xf8u) == 0xf0u) {
                if (size < 4u || !is_utf8_trailing_byte(data[1]) ||
                    !is_utf8_trailing_byte(data[2]) || !is_utf8_trailing_byte(data[3])) {
                    return {};
                }

                uint32_t const codepoint = (static_cast<uint32_t>(b0 & 0x07u) << 18u) |
                                           (static_cast<uint32_t>(data[1] & 0x3fu) << 12u) |
                                           (static_cast<uint32_t>(data[2] & 0x3fu) << 6u) |
                                           static_cast<uint32_t>(data[3] & 0x3fu);
                if (codepoint < 0x10000u || codepoint > 0x10ffffu) {
                    return {};
                }

                return DecodeResult{codepoint, 4u, true};
            }

            return {};
        }

        [[nodiscard]] auto
        utf8_to_codepoints(StrRef text, Arena& arena, uint32_t** out_codepoints, uint32_t* out_len)
            -> bool {
            ASSERT(out_codepoints != nullptr);
            ASSERT(out_len != nullptr);
            if (text.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }

            if (text.empty()) {
                *out_codepoints = nullptr;
                *out_len = 0u;
                return true;
            }

            uint32_t* const codepoints = arena_alloc<uint32_t>(arena, text.size());
            auto const* data = reinterpret_cast<uint8_t const*>(text.data());
            size_t offset = 0u;
            uint32_t count = 0u;

            while (offset < text.size()) {
                DecodeResult const decoded =
                    decode_utf8_codepoint(data + offset, text.size() - offset);
                if (!decoded.ok || decoded.advance == 0u) {
                    return false;
                }

                codepoints[count] = decoded.codepoint;
                count += 1u;
                offset += decoded.advance;
            }

            *out_codepoints = codepoints;
            *out_len = count;
            return true;
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

        [[nodiscard]] auto
        open_system_font(Arena& arena, ContextImpl* context, StrRef family_name, Font* out_font)
            -> Result {
            ArenaMarker const marker = arena.marker();
            FontImpl* const font = create_font_impl(arena, context);

            ArenaTemp temp = begin_thread_temp_arena();
            wchar_t* wide_family = nullptr;
            int wide_family_len = 0;
            StrRef const selected_family = family_name.empty() ? DEFAULT_FONT_FAMILY : family_name;
            if (!utf8_to_wide(selected_family, *temp.arena(), &wide_family, &wide_family_len)) {
                arena.reset_to(marker);
                return Result::TEXT_CONVERSION_FAILED;
            }
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
                hr = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_REGULAR,
                                                  DWRITE_FONT_STRETCH_NORMAL,
                                                  DWRITE_FONT_STYLE_NORMAL,
                                                  &dwrite_font);
            }
            if (SUCCEEDED(hr) && dwrite_font != nullptr) {
                hr = dwrite_font->CreateFontFace(&font->font_face);
            }

            release_com(dwrite_font);
            release_com(family);
            release_com(collection);

            if (FAILED(hr) || family_exists == FALSE || font->font_face == nullptr) {
                destroy_font_impl(font);
                arena.reset_to(marker);
                return family_exists == FALSE ? Result::FONT_NOT_FOUND : Result::BACKEND_FAILURE;
            }

            out_font->handle = font;
            return Result::OK;
        }

        [[nodiscard]] auto
        open_file_font(Arena& arena, ContextImpl* context, StrRef file_path, Font* out_font)
            -> Result {
            ASSERT(!file_path.empty());

            ArenaMarker const marker = arena.marker();
            FontImpl* const font = create_font_impl(arena, context);

            ArenaTemp temp = begin_thread_temp_arena();
            wchar_t* wide_path = nullptr;
            int wide_path_len = 0;
            if (!utf8_to_wide(file_path, *temp.arena(), &wide_path, &wide_path_len)) {
                arena.reset_to(marker);
                return Result::TEXT_CONVERSION_FAILED;
            }
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
                    face_type, 1u, font_files, 0u, DWRITE_FONT_SIMULATIONS_NONE, &font->font_face);
            }

            BASE_UNUSED(file_type);
            if (FAILED(hr) || supported == FALSE || face_count == 0u ||
                font->font_face == nullptr) {
                destroy_font_impl(font);
                arena.reset_to(marker);
                return Result::FONT_NOT_FOUND;
            }

            out_font->handle = font;
            return Result::OK;
        }

        [[nodiscard]] auto metrics_scale(DWRITE_FONT_METRICS const& metrics, float size) -> float {
            ASSERT(metrics.designUnitsPerEm != 0u);
            return POINTS_TO_DIPS * size / static_cast<float>(metrics.designUnitsPerEm);
        }

        [[nodiscard]] auto ceil_u32(float value, uint32_t* out_value) -> bool {
            if (!(value >= 0.0f) ||
                value > static_cast<float>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }

            *out_value = static_cast<uint32_t>(std::ceil(value));
            return true;
        }

        [[nodiscard]] auto checked_bitmap_size(uint32_t width, uint32_t height, size_t* out_size)
            -> bool {
            if (width > std::numeric_limits<uint32_t>::max() / 4u) {
                return false;
            }

            size_t const row_size = static_cast<size_t>(width) * 4u;
            if (height != 0u && row_size > std::numeric_limits<size_t>::max() / height) {
                return false;
            }

            *out_size = row_size * static_cast<size_t>(height);
            return true;
        }

        auto clear_render_target(HDC dc, uint32_t width, uint32_t height) -> void {
            HGDIOBJ const old_pen = SelectObject(dc, GetStockObject(DC_PEN));
            HGDIOBJ const old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCPenColor(dc, RGB(0, 0, 0));
            SetDCBrushColor(dc, RGB(0, 0, 0));
            Rectangle(dc, 0, 0, static_cast<int>(width), static_cast<int>(height));
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context* out_context) -> Result {
        BASE_UNUSED(desc);

        ArenaMarker const marker = arena.marker();
        ContextImpl* const context = arena_new<ContextImpl>(arena);

        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,
                                         __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(&context->factory));
        if (SUCCEEDED(hr)) {
            hr = context->factory->GetGdiInterop(&context->gdi_interop);
        }
        if (SUCCEEDED(hr)) {
            hr = context->factory->CreateRenderingParams(&context->base_rendering_params);
        }
        if (SUCCEEDED(hr)) {
            FLOAT const gamma = context->base_rendering_params->GetGamma();
            FLOAT const enhanced_contrast = context->base_rendering_params->GetEnhancedContrast();
            FLOAT const clear_type_level = context->base_rendering_params->GetClearTypeLevel();
            hr = context->factory->CreateCustomRenderingParams(gamma,
                                                               enhanced_contrast,
                                                               clear_type_level,
                                                               DWRITE_PIXEL_GEOMETRY_FLAT,
                                                               DWRITE_RENDERING_MODE_DEFAULT,
                                                               &context->rendering_params);
        }

        if (FAILED(hr)) {
            release_com(context->rendering_params);
            release_com(context->base_rendering_params);
            release_com(context->gdi_interop);
            release_com(context->factory);
            arena.reset_to(marker);
            return Result::BACKEND_FAILURE;
        }

        out_context->handle = context;
        return Result::OK;
    }

    auto destroy_context(Context* context) -> void {
        ASSERT(context != nullptr);
        ContextImpl* const impl = context_from_handle(*context);
        ASSERT(impl != nullptr);
        release_com(impl->rendering_params);
        release_com(impl->base_rendering_params);
        release_com(impl->gdi_interop);
        release_com(impl->factory);
        context->handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font* out_font) -> Result {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (!desc.file_path.empty()) {
            return open_file_font(arena, impl, desc.file_path, out_font);
        }

        return open_system_font(arena, impl, desc.family_name, out_font);
    }

    auto close_font(Font* font) -> void {
        ASSERT(font != nullptr);
        FontImpl* const impl = font_from_handle(*font);
        ASSERT(impl != nullptr);
        destroy_font_impl(impl);
        font->handle = nullptr;
    }

    auto metrics_from_font(Font font, float size, Metrics* out_metrics) -> Result {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(out_metrics != nullptr);
        ASSERT(impl->font_face != nullptr);

        DWRITE_FONT_METRICS dwrite_metrics = {};
        impl->font_face->GetMetrics(&dwrite_metrics);

        float const scale = metrics_scale(dwrite_metrics, size);
        *out_metrics = {};
        out_metrics->line_gap = static_cast<float>(dwrite_metrics.lineGap) * scale;
        out_metrics->ascent = static_cast<float>(dwrite_metrics.ascent) * scale;
        out_metrics->descent = static_cast<float>(dwrite_metrics.descent) * scale;
        out_metrics->capital_height = static_cast<float>(dwrite_metrics.capHeight) * scale;
        return Result::OK;
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult* out_raster)
        -> Result {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(out_raster != nullptr);
        ASSERT(impl->font_face != nullptr);
        ASSERT(impl->context != nullptr);
        ASSERT(impl->context->gdi_interop != nullptr);

        ArenaTemp temp = begin_thread_temp_arena();
        uint32_t* codepoints = nullptr;
        uint32_t glyph_count = 0u;
        if (!utf8_to_codepoints(text, *temp.arena(), &codepoints, &glyph_count)) {
            return Result::TEXT_CONVERSION_FAILED;
        }

        ASSERT(glyph_count != 0u);

        uint16_t* const glyph_indices = arena_alloc<uint16_t>(*temp.arena(), glyph_count);
        HRESULT hr = impl->font_face->GetGlyphIndices(codepoints, glyph_count, glyph_indices);
        if (FAILED(hr)) {
            return Result::RASTERIZATION_FAILED;
        }

        auto* const glyph_metrics = arena_alloc<DWRITE_GLYPH_METRICS>(*temp.arena(), glyph_count);
        hr = impl->font_face->GetGdiCompatibleGlyphMetrics(
            size, 1.0f, nullptr, TRUE, glyph_indices, glyph_count, glyph_metrics, FALSE);
        if (FAILED(hr)) {
            return Result::RASTERIZATION_FAILED;
        }

        DWRITE_FONT_METRICS font_metrics = {};
        impl->font_face->GetMetrics(&font_metrics);
        float const scale = metrics_scale(font_metrics, size);
        float advance = 0.0f;
        for (uint32_t index = 0u; index < glyph_count; ++index) {
            advance += static_cast<float>(glyph_metrics[index].advanceWidth) * scale;
        }

        uint32_t bitmap_width = 0u;
        uint32_t bitmap_height = 0u;
        if (!ceil_u32(std::max(1.0f, advance + 4.0f), &bitmap_width) ||
            !ceil_u32(std::max(1.0f,
                               (static_cast<float>(font_metrics.ascent) +
                                static_cast<float>(font_metrics.descent)) *
                                       scale +
                                   4.0f),
                      &bitmap_height)) {
            return Result::RASTERIZATION_FAILED;
        }

        IDWriteBitmapRenderTarget* render_target = nullptr;
        hr = impl->context->gdi_interop->CreateBitmapRenderTarget(
            nullptr, bitmap_width, bitmap_height, &render_target);
        if (FAILED(hr) || render_target == nullptr) {
            release_com(render_target);
            return Result::RASTERIZATION_FAILED;
        }

        render_target->SetPixelsPerDip(1.0f);
        HDC const dc = render_target->GetMemoryDC();
        clear_render_target(dc, bitmap_width, bitmap_height);

        float const descent = static_cast<float>(font_metrics.descent) * scale;
        float const baseline_y = static_cast<float>(bitmap_height) - 2.0f - descent;

        DWRITE_GLYPH_RUN glyph_run = {};
        glyph_run.fontFace = impl->font_face;
        glyph_run.fontEmSize = size * POINTS_TO_DIPS;
        glyph_run.glyphCount = glyph_count;
        glyph_run.glyphIndices = glyph_indices;

        RECT bounding_box = {};
        hr = render_target->DrawGlyphRun(1.0f,
                                         baseline_y,
                                         DWRITE_MEASURING_MODE_NATURAL,
                                         &glyph_run,
                                         impl->context->rendering_params,
                                         RGB(255, 255, 255),
                                         &bounding_box);
        if (FAILED(hr)) {
            release_com(render_target);
            return Result::RASTERIZATION_FAILED;
        }

        DIBSECTION dib = {};
        HBITMAP const bitmap = static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP));
        if (bitmap == nullptr || GetObjectW(bitmap, sizeof(dib), &dib) == 0) {
            release_com(render_target);
            return Result::RASTERIZATION_FAILED;
        }

        size_t bitmap_byte_count = 0u;
        if (!checked_bitmap_size(bitmap_width, bitmap_height, &bitmap_byte_count)) {
            release_com(render_target);
            return Result::RASTERIZATION_FAILED;
        }

        uint8_t* const out_pixels = arena_alloc<uint8_t>(arena, bitmap_byte_count);
        auto const* in_data = static_cast<uint8_t const*>(dib.dsBm.bmBits);
        uint8_t const* in_line = in_data;
        uint8_t* out_line = out_pixels;
        uint32_t const out_pitch = bitmap_width * 4u;

        for (uint32_t y = 0u; y < bitmap_height; ++y) {
            uint8_t const* in_pixel = in_line;
            uint8_t* out_pixel = out_line;
            for (uint32_t x = 0u; x < bitmap_width; ++x) {
                out_pixel[0] = 255u;
                out_pixel[1] = 255u;
                out_pixel[2] = 255u;
                out_pixel[3] = in_pixel[0];
                in_pixel += 4;
                out_pixel += 4;
            }

            in_line += static_cast<size_t>(dib.dsBm.bmWidthBytes);
            out_line += out_pitch;
        }

        *out_raster = {};
        out_raster->size = {bitmap_width, bitmap_height};
        out_raster->stride = out_pitch;
        out_raster->rgba_pixels = out_pixels;
        out_raster->advance = advance;
        out_raster->height = static_cast<float>(bounding_box.bottom - bounding_box.top);

        release_com(render_target);
        return Result::OK;
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
