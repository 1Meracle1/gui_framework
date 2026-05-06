#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "font_provider_win32_backends.h"

#include <algorithm>
#include <base/unicode.h>
#include <cmath>
#include <cstring>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits>
#include <windows.h>

namespace gui::font_provider::platform::freetype {
    namespace {

        constexpr StrRef DEFAULT_FONT_FAMILY = "Segoe UI";
        constexpr float TEXT_PADDING = 2.0f;
        constexpr float POINTS_TO_PIXELS = 96.0f / 72.0f;

        struct ContextImpl {
            FT_Library library = nullptr;
        };

        struct FontImpl {
            ContextImpl* context = nullptr;
            FT_Face face = nullptr;
        };

        struct FontFileMap {
            StrRef family = {};
            StrRef file_name = {};
        };

        constexpr FontFileMap FONT_FILES[] = {
            {"Segoe UI", "segoeui.ttf"},
            {"Segoe MDL2 Assets", "segmdl2.ttf"},
            {"Segoe Fluent Icons", "SegoeIcons.ttf"},
            {"Cascadia Mono", "CascadiaMono.ttf"},
            {"Cascadia Code", "CascadiaCode.ttf"},
            {"Consolas", "consola.ttf"},
            {"Courier New", "cour.ttf"},
            {"Arial", "arial.ttf"},
        };

        [[nodiscard]] auto context_from_handle(Context context) -> ContextImpl* {
            return static_cast<ContextImpl*>(context.handle);
        }

        [[nodiscard]] auto font_from_handle(Font font) -> FontImpl* {
            return static_cast<FontImpl*>(font.handle);
        }

        [[nodiscard]] auto font_handle(FontImpl* font) -> Font {
            return {font, Backend::FREETYPE};
        }

        [[nodiscard]] auto
        append_path_part(char* buffer, size_t capacity, size_t& size, StrRef text) -> bool {
            if (text.size() >= capacity || size > capacity - text.size() - 1u) {
                return false;
            }
            if (!text.empty()) {
                std::memcpy(buffer + size, text.data(), text.size());
                size += text.size();
            }
            buffer[size] = '\0';
            return true;
        }

        [[nodiscard]] auto windows_font_path(Arena& arena, StrRef file_name) -> StrRef {
            char windows_dir[MAX_PATH] = {};
            UINT const dir_size = GetWindowsDirectoryA(windows_dir, static_cast<UINT>(MAX_PATH));
            if (dir_size == 0u || dir_size >= static_cast<UINT>(MAX_PATH)) {
                return {};
            }

            char path[MAX_PATH * 2u] = {};
            size_t size = 0u;
            if (!append_path_part(path, sizeof(path), size, StrRef(windows_dir, dir_size)) ||
                !append_path_part(path, sizeof(path), size, "\\Fonts\\") ||
                !append_path_part(path, sizeof(path), size, file_name)) {
                return {};
            }
            return arena_copy_cstr(arena, StrRef(path, size));
        }

        [[nodiscard]] auto system_font_path(Arena& arena, StrRef family_name) -> StrRef {
            StrRef const family = family_name.empty() ? DEFAULT_FONT_FAMILY : family_name;
            for (FontFileMap const& map : FONT_FILES) {
                if (family.equals_ignore_ascii_case(map.family)) {
                    return windows_font_path(arena, map.file_name);
                }
            }
            return {};
        }

        [[nodiscard]] auto ft_size(float size) -> FT_UInt {
            double value = std::round(static_cast<double>(size * POINTS_TO_PIXELS));
            value =
                std::clamp(value, 1.0, static_cast<double>(std::numeric_limits<FT_UInt>::max()));
            return static_cast<FT_UInt>(value);
        }

        [[nodiscard]] auto ft_set_size(FontImpl* font, float size) -> bool {
            ASSERT(font != nullptr);
            ASSERT(font->face != nullptr);
            return FT_Set_Pixel_Sizes(font->face, 0u, ft_size(size)) == 0;
        }

        auto ft_set_phase(FT_Face face, uint8_t phase_x, uint8_t phase_y) -> void {
            FT_Matrix matrix = {};
            matrix.xx = 1l << 16;
            matrix.yy = 1l << 16;

            FT_Vector delta = {};
            delta.x = (static_cast<FT_Pos>(phase_x % GLYPH_RASTER_PHASE_COUNT) * 64l) /
                      static_cast<FT_Pos>(GLYPH_RASTER_PHASE_COUNT);
            delta.y = (static_cast<FT_Pos>(phase_y % GLYPH_RASTER_PHASE_COUNT) * 64l) /
                      static_cast<FT_Pos>(GLYPH_RASTER_PHASE_COUNT);
            FT_Set_Transform(face, &matrix, &delta);
        }

        [[nodiscard]] auto ft_load_flags(RasterPolicy raster_policy) -> FT_Int32 {
            return raster_policy == RasterPolicy::SMOOTH_HINTED
                       ? FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT
                       : FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL;
        }

        [[nodiscard]] auto ft_render_mode(RasterPolicy raster_policy) -> FT_Render_Mode {
            return raster_policy == RasterPolicy::SMOOTH_HINTED ? FT_RENDER_MODE_LIGHT
                                                                : FT_RENDER_MODE_NORMAL;
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

        [[nodiscard]] auto glyph_index_u16(FT_UInt glyph_index) -> uint16_t {
            if (glyph_index > static_cast<FT_UInt>(std::numeric_limits<uint16_t>::max())) {
                return 0u;
            }
            return static_cast<uint16_t>(glyph_index);
        }

        [[nodiscard]] auto codepoint_glyph(FT_Face face, uint32_t codepoint) -> FT_UInt {
            FT_UInt glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint));
            if (glyph_index == 0u && codepoint != '?') {
                glyph_index = FT_Get_Char_Index(face, static_cast<FT_ULong>('?'));
            }
            return glyph_index;
        }

        [[nodiscard]] auto load_glyph_advance(FontImpl* font, FT_UInt glyph_index) -> float {
            if (FT_Load_Glyph(font->face, glyph_index, FT_LOAD_DEFAULT) != 0) {
                return 0.0f;
            }
            return static_cast<float>(font->face->glyph->advance.x) / 64.0f;
        }

        auto read_metrics(FontImpl* font, float size, Metrics& out_metrics) -> void {
            out_metrics = {};
            if (!ft_set_size(font, size)) {
                return;
            }

            FT_Size_Metrics const& metrics = font->face->size->metrics;
            out_metrics.ascent = static_cast<float>(metrics.ascender) / 64.0f;
            out_metrics.descent = static_cast<float>(-metrics.descender) / 64.0f;
            out_metrics.line_gap = std::max(
                0.0f,
                static_cast<float>(metrics.height) / 64.0f - out_metrics.ascent -
                    out_metrics.descent
            );
            out_metrics.capital_height = out_metrics.ascent;
        }

        auto shape_text(FontImpl* font, float size, StrRef text, Arena& arena, ShapedText& out_text)
            -> void {
            ASSERT(font != nullptr);
            ASSERT(font->face != nullptr);

            out_text = {};
            if (text.empty() || !ft_set_size(font, size)) {
                return;
            }

            ft_set_phase(font->face, 0u, 0u);
            ShapedGlyph* const glyphs = arena_alloc<ShapedGlyph>(arena, text.size());
            float pen_x = 0.0f;
            size_t glyph_count = 0u;
            FT_UInt previous_glyph = 0u;

            size_t offset = 0u;
            while (offset < text.size()) {
                base::Utf8DecodeResult decoded = base::utf8_decode(text, offset);
                if (!decoded.ok) {
                    decoded = {'?', 1u, true};
                }

                FT_UInt const glyph_index = codepoint_glyph(font->face, decoded.codepoint);
                if (FT_HAS_KERNING(font->face) && previous_glyph != 0u && glyph_index != 0u) {
                    FT_Vector kerning = {};
                    if (FT_Get_Kerning(
                            font->face, previous_glyph, glyph_index, FT_KERNING_DEFAULT, &kerning
                        ) == 0) {
                        pen_x += static_cast<float>(kerning.x) / 64.0f;
                    }
                }

                float const advance = load_glyph_advance(font, glyph_index);
                ShapedGlyph& glyph = glyphs[glyph_count];
                glyph = {};
                glyph.font = font_handle(font);
                glyph.glyph_index = glyph_index_u16(glyph_index);
                glyph.size = size;
                glyph.x = pen_x;
                glyph.advance = advance;
                pen_x += advance;
                previous_glyph = glyph_index;
                glyph_count += 1u;
                offset += decoded.size;
            }

            Metrics metrics = {};
            read_metrics(font, size, metrics);
            uint32_t bitmap_width = 0u;
            uint32_t bitmap_height = 0u;
            ASSERT(ceil_u32(std::max(1.0f, pen_x + (TEXT_PADDING * 2.0f)), bitmap_width));
            ASSERT(ceil_u32(
                std::max(1.0f, metrics.ascent + metrics.descent + (TEXT_PADDING * 2.0f)),
                bitmap_height
            ));

            out_text.glyphs = glyphs;
            out_text.glyph_count = glyph_count;
            out_text.advance = pen_x;
            out_text.origin_x = TEXT_PADDING;
            out_text.origin_y = TEXT_PADDING;
            out_text.baseline_y = TEXT_PADDING + metrics.ascent;
            out_text.height = static_cast<float>(bitmap_height);
            out_text.size = {bitmap_width, bitmap_height};
        }

        auto copy_bitmap(FT_Bitmap const& bitmap, Arena& arena, GlyphRaster& out_raster) -> void {
            uint32_t const width = bitmap.width;
            uint32_t const height = bitmap.rows;
            if (width == 0u || height == 0u || bitmap.buffer == nullptr) {
                return;
            }

            size_t byte_count = 0u;
            ASSERT(checked_pixel_size(width, height, 1u, byte_count));
            uint8_t* const pixels = arena_alloc<uint8_t>(arena, byte_count);
            std::memset(pixels, 0, byte_count);

            int const pitch = bitmap.pitch;
            size_t const abs_pitch = static_cast<size_t>(pitch < 0 ? -pitch : pitch);
            for (uint32_t y = 0u; y < height; ++y) {
                uint32_t const source_y = pitch >= 0 ? y : height - y - 1u;
                uint8_t const* const src =
                    bitmap.buffer + (static_cast<size_t>(source_y) * abs_pitch);
                uint8_t* const dst = pixels + (static_cast<size_t>(y) * width);
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                    for (uint32_t x = 0u; x < width; ++x) {
                        dst[x] = (src[x / 8u] & (0x80u >> (x % 8u))) != 0u ? 255u : 0u;
                    }
                } else if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
                    for (uint32_t x = 0u; x < width; ++x) {
                        dst[x] = src[x * 4u + 3u];
                    }
                } else {
                    std::memcpy(dst, src, width);
                }
            }

            out_raster.size = {width, height};
            out_raster.stride = width;
            out_raster.pixels = pixels;
            out_raster.format = RasterFormat::ALPHA;
        }

        auto raster_glyph(
            FontImpl* font,
            float size,
            uint16_t glyph_index,
            RasterPolicy raster_policy,
            uint8_t phase_x,
            uint8_t phase_y,
            Arena& arena,
            GlyphRaster& out_raster
        ) -> void {
            ASSERT(font != nullptr);
            ASSERT(font->face != nullptr);

            out_raster = {};
            if (!ft_set_size(font, size)) {
                return;
            }

            ft_set_phase(font->face, phase_x, phase_y);
            if (FT_Load_Glyph(
                    font->face, static_cast<FT_UInt>(glyph_index), ft_load_flags(raster_policy)
                ) != 0 ||
                FT_Render_Glyph(font->face->glyph, ft_render_mode(raster_policy)) != 0) {
                return;
            }

            FT_GlyphSlot const slot = font->face->glyph;
            copy_bitmap(slot->bitmap, arena, out_raster);
            out_raster.offset_x = static_cast<float>(slot->bitmap_left);
            out_raster.offset_y = -static_cast<float>(slot->bitmap_top);
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

        auto
        raster_text(FontImpl* font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
            -> void {
            ArenaTemp temp = begin_thread_temp_arena();
            ShapedText shaped = {};
            shape_text(font, size, text, *temp.arena(), shaped);
            if (shaped.glyph_count == 0u) {
                return;
            }

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
                raster_glyph(
                    font,
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

        auto open_font_path(Arena& arena, ContextImpl* context, StrRef path, Font& out_font)
            -> void {
            if (path.empty()) {
                return;
            }

            StrRef const cpath = arena_copy_cstr(arena, path);
            FontImpl* const font = arena_new<FontImpl>(arena);
            font->context = context;
            if (FT_New_Face(context->library, cpath.data(), 0l, &font->face) != 0) {
                font->context = nullptr;
                return;
            }
            out_font = font_handle(font);
        }

        auto open_font_data(
            Arena& arena, ContextImpl* context, Slice<uint8_t const> data, Font& out_font
        ) -> void {
            if (data.empty() ||
                data.size() > static_cast<size_t>(std::numeric_limits<FT_Long>::max())) {
                return;
            }

            uint8_t* const font_data = arena_alloc<uint8_t>(arena, data.size());
            std::memcpy(font_data, data.data(), data.size());

            FontImpl* const font = arena_new<FontImpl>(arena);
            font->context = context;
            if (FT_New_Memory_Face(
                    context->library, font_data, static_cast<FT_Long>(data.size()), 0l, &font->face
                ) != 0) {
                font->context = nullptr;
                return;
            }
            out_font = font_handle(font);
        }

    } // namespace

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> Result {
        BASE_UNUSED(desc);

        ArenaMarker const marker = arena.marker();
        ContextImpl* const context = arena_new<ContextImpl>(arena);
        if (FT_Init_FreeType(&context->library) != 0) {
            arena.reset_to(marker);
            return Result::BACKEND_FAILURE;
        }

        out_context = {context, Backend::FREETYPE};
        return Result::OK;
    }

    auto destroy_context(Context& context) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        if (impl->library != nullptr) {
            FT_Done_FreeType(impl->library);
        }
        context.handle = nullptr;
    }

    auto open_font(Arena& arena, Context context, FontDesc const& desc, Font& out_font) -> void {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);

        if (!desc.data.empty()) {
            open_font_data(arena, impl, desc.data, out_font);
            return;
        }
        if (!desc.file_path.empty()) {
            open_font_path(arena, impl, desc.file_path, out_font);
            return;
        }

        ArenaTemp temp = begin_thread_temp_arena();
        open_font_path(arena, impl, system_font_path(*temp.arena(), desc.family_name), out_font);
    }

    auto close_font(Font& font) -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        if (impl->face != nullptr) {
            FT_Done_Face(impl->face);
        }
        font.handle = nullptr;
    }

    auto metrics_from_font(Font font, float size, Metrics& out_metrics) -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        read_metrics(impl, size, out_metrics);
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ArenaTemp temp = begin_thread_temp_arena();
        ShapedText shaped = {};
        shape_text(impl, size, text, *temp.arena(), shaped);
        return shaped.advance;
    }

    auto shape_text(Font font, float size, StrRef text, Arena& arena, ShapedText& out_text)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        shape_text(impl, size, text, arena, out_text);
    }

    auto
    raster_glyph(Font font, float size, uint16_t glyph_index, Arena& arena, GlyphRaster& out_raster)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
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
        gui::font_provider::platform::freetype::raster_glyph(
            font, size, glyph_index, RasterPolicy::SHARP_HINTED, phase_x, phase_y, arena, out_raster
        );
    }

    auto raster_text(Font font, float size, StrRef text, Arena& arena, RasterResult& out_raster)
        -> void {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        out_raster = {};
        if (!text.empty()) {
            raster_text(impl, size, text, arena, out_raster);
        }
    }

    auto native_factory(Context context) -> void* {
        ContextImpl* const impl = context_from_handle(context);
        ASSERT(impl != nullptr);
        return impl->library;
    }

    auto native_font_face(Font font) -> void* {
        FontImpl* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        return impl->face;
    }

} // namespace gui::font_provider::platform::freetype
