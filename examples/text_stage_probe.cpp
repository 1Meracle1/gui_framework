#include <algorithm>
#include <base/config.h>
#include <base/fmt.h>
#include <base/memory.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <font_cache/font_cache.h>
#include <font_provider/font_provider.h>

namespace {

    constexpr uint32_t CANVAS_WIDTH = 620u;
    constexpr uint32_t CANVAS_HEIGHT = 21u;
    constexpr uint32_t TEXT_ATLAS_PADDING = 1u;
    constexpr float FONT_SIZE = 12.0f;
    constexpr StrRef SAMPLE_TEXT = "#include \"editor_render.h\"";

    struct Color8 {
        uint8_t r = 0u;
        uint8_t g = 0u;
        uint8_t b = 0u;
    };

    struct Image {
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint8_t* pixels = nullptr;
    };

    struct QuantizedOrigin {
        float origin = 0.0f;
        uint8_t phase = 0u;
    };

    struct ProbeContext {
        Arena arena = {};
        gui::font_provider::Context provider = {};
        gui::font_cache::Cache cache = {};
        gui::font_cache::Font font = {};
        float char_width = 0.0f;
    };

    struct StageImages {
        Image raw_mask = {};
        Image font_cache_mask = {};
        Image atlas_upload_mask = {};
        Image cpu_composite = {};
    };

    struct Policy {
        char const* name = nullptr;
        gui::font_provider::RasterPolicy raster_policy =
            gui::font_provider::RasterPolicy::SHARP_HINTED;
    };

    [[nodiscard]] auto pixel(Image image, uint32_t x, uint32_t y) -> uint8_t* {
        return image.pixels + ((static_cast<size_t>(y) * image.width + x) * 4u);
    }

    auto init_image(Arena& arena, Image& image, uint32_t width, uint32_t height, Color8 color)
        -> void {
        image.width = width;
        image.height = height;
        image.pixels = arena_alloc<uint8_t>(arena, static_cast<size_t>(width) * height * 4u);
        for (uint32_t y = 0u; y < height; ++y) {
            for (uint32_t x = 0u; x < width; ++x) {
                uint8_t* const dst = pixel(image, x, y);
                dst[0u] = color.r;
                dst[1u] = color.g;
                dst[2u] = color.b;
                dst[3u] = 255u;
            }
        }
    }

    auto write_u16(std::FILE* file, uint16_t value) -> void {
        uint8_t bytes[] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8u) & 0xffu),
        };
        std::fwrite(bytes, 1u, sizeof(bytes), file);
    }

    auto write_u32(std::FILE* file, uint32_t value) -> void {
        uint8_t bytes[] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8u) & 0xffu),
            static_cast<uint8_t>((value >> 16u) & 0xffu),
            static_cast<uint8_t>((value >> 24u) & 0xffu),
        };
        std::fwrite(bytes, 1u, sizeof(bytes), file);
    }

    auto write_i32(std::FILE* file, int32_t value) -> void {
        write_u32(file, static_cast<uint32_t>(value));
    }

    [[nodiscard]] auto write_bmp(StrRef path, Image image) -> bool {
        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, path.data(), "wb") != 0) {
            return false;
        }
#else
        file = std::fopen(path.data(), "wb");
#endif
        if (file == nullptr) {
            return false;
        }

        uint32_t const image_size = image.width * image.height * 4u;
        uint32_t const file_size = 14u + 40u + image_size;
        write_u16(file, 0x4d42u);
        write_u32(file, file_size);
        write_u16(file, 0u);
        write_u16(file, 0u);
        write_u32(file, 54u);
        write_u32(file, 40u);
        write_i32(file, static_cast<int32_t>(image.width));
        write_i32(file, -static_cast<int32_t>(image.height));
        write_u16(file, 1u);
        write_u16(file, 32u);
        write_u32(file, 0u);
        write_u32(file, image_size);
        write_i32(file, 2835);
        write_i32(file, 2835);
        write_u32(file, 0u);
        write_u32(file, 0u);

        uint8_t bgra[4] = {};
        for (uint32_t y = 0u; y < image.height; ++y) {
            for (uint32_t x = 0u; x < image.width; ++x) {
                uint8_t const* const src = pixel(image, x, y);
                bgra[0u] = src[2u];
                bgra[1u] = src[1u];
                bgra[2u] = src[0u];
                bgra[3u] = src[3u];
                std::fwrite(bgra, 1u, sizeof(bgra), file);
            }
        }

        std::fclose(file);
        return true;
    }

    [[nodiscard]] auto make_path(char* buffer, size_t capacity, StrRef dir, StrRef name) -> StrRef {
        if (fmt::snprintf(buffer, capacity, "%s\\%s", dir, name) < 0) {
            return {};
        }
        return StrRef(buffer);
    }

    [[nodiscard]] auto glyph_phase_value(uint8_t phase) -> float {
        return static_cast<float>(phase % gui::font_provider::GLYPH_RASTER_PHASE_COUNT) /
               static_cast<float>(gui::font_provider::GLYPH_RASTER_PHASE_COUNT);
    }

    [[nodiscard]] auto quantize_origin(float value) -> QuantizedOrigin {
        float const scaled =
            std::floor(value * gui::font_provider::GLYPH_RASTER_PHASE_COUNT + 0.5f);
        int32_t const quantized = static_cast<int32_t>(scaled);
        uint8_t const phase = static_cast<uint8_t>(
            ((quantized % gui::font_provider::GLYPH_RASTER_PHASE_COUNT) +
             gui::font_provider::GLYPH_RASTER_PHASE_COUNT) %
            gui::font_provider::GLYPH_RASTER_PHASE_COUNT
        );
        return {scaled / gui::font_provider::GLYPH_RASTER_PHASE_COUNT, phase};
    }

    [[nodiscard]] auto origin_base(QuantizedOrigin origin) -> float {
        return origin.origin - glyph_phase_value(origin.phase);
    }

    [[nodiscard]] auto raster_lcd(gui::font_provider::GlyphRaster const& raster) -> bool {
        return raster.format == gui::font_provider::RasterFormat::LCD_RGB;
    }

    auto plot_mask(Image image, int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) -> void {
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(image.width) ||
            y >= static_cast<int32_t>(image.height)) {
            return;
        }
        uint8_t* const dst = pixel(image, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        dst[0u] = std::max(dst[0u], r);
        dst[1u] = std::max(dst[1u], g);
        dst[2u] = std::max(dst[2u], b);
        dst[3u] = 255u;
    }

    [[nodiscard]] auto blend_channel(uint8_t dst, uint8_t src, uint8_t alpha) -> uint8_t {
        uint32_t const inv_alpha = 255u - alpha;
        uint32_t const value =
            (static_cast<uint32_t>(dst) * inv_alpha) + (static_cast<uint32_t>(src) * alpha) + 127u;
        return static_cast<uint8_t>(value / 255u);
    }

    auto composite_pixel(
        Image image, int32_t x, int32_t y, Color8 color, uint8_t r, uint8_t g, uint8_t b
    ) -> void {
        if (x < 0 || y < 0 || x >= static_cast<int32_t>(image.width) ||
            y >= static_cast<int32_t>(image.height)) {
            return;
        }
        uint8_t* const dst = pixel(image, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        dst[0u] = blend_channel(dst[0u], color.r, r);
        dst[1u] = blend_channel(dst[1u], color.g, g);
        dst[2u] = blend_channel(dst[2u], color.b, b);
    }

    auto raster_pixel(
        gui::font_provider::GlyphRaster const& raster,
        uint32_t x,
        uint32_t y,
        uint8_t& out_r,
        uint8_t& out_g,
        uint8_t& out_b
    ) -> void {
        uint8_t const* const src = raster.pixels + (static_cast<size_t>(y) * raster.stride);
        if (raster_lcd(raster)) {
            out_r = src[x * 4u + 0u];
            out_g = src[x * 4u + 1u];
            out_b = src[x * 4u + 2u];
        } else {
            out_r = src[x];
            out_g = src[x];
            out_b = src[x];
        }
    }

    auto fill_atlas_pixels(
        gui::font_provider::GlyphRaster const& raster, uint8_t* upload_pixels, uint32_t upload_width
    ) -> void {
        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const src = raster.pixels + (static_cast<size_t>(y) * raster.stride);
            uint8_t* const dst = upload_pixels +
                                 (static_cast<size_t>(y + TEXT_ATLAS_PADDING) * upload_width * 4u) +
                                 (TEXT_ATLAS_PADDING * 4u);
            if (raster_lcd(raster)) {
                for (uint32_t x = 0u; x < raster.size.width; ++x) {
                    uint8_t const r = src[x * 4u + 0u];
                    uint8_t const g = src[x * 4u + 1u];
                    uint8_t const b = src[x * 4u + 2u];
                    dst[x * 4u + 0u] = r;
                    dst[x * 4u + 1u] = g;
                    dst[x * 4u + 2u] = b;
                    dst[x * 4u + 3u] = std::max(std::max(r, g), b);
                }
            } else {
                for (uint32_t x = 0u; x < raster.size.width; ++x) {
                    uint8_t const coverage = src[x];
                    dst[x * 4u + 0u] = coverage;
                    dst[x * 4u + 1u] = coverage;
                    dst[x * 4u + 2u] = coverage;
                    dst[x * 4u + 3u] = coverage;
                }
            }
        }
    }

    auto plot_raster_mask(
        Image image, gui::font_provider::GlyphRaster const& raster, int32_t x0, int32_t y0
    ) -> void {
        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t r = 0u;
                uint8_t g = 0u;
                uint8_t b = 0u;
                raster_pixel(raster, x, y, r, g, b);
                plot_mask(
                    image, x0 + static_cast<int32_t>(x), y0 + static_cast<int32_t>(y), r, g, b
                );
            }
        }
    }

    auto plot_atlas_upload_mask(
        Arena& arena,
        Image image,
        gui::font_provider::GlyphRaster const& raster,
        int32_t x0,
        int32_t y0
    ) -> void {
        uint32_t const upload_width = raster.size.width + TEXT_ATLAS_PADDING * 2u;
        uint32_t const upload_height = raster.size.height + TEXT_ATLAS_PADDING * 2u;
        size_t const upload_size =
            static_cast<size_t>(upload_width) * static_cast<size_t>(upload_height) * 4u;
        uint8_t* const upload_pixels = arena_alloc<uint8_t>(arena, upload_size);
        std::memset(upload_pixels, 0, upload_size);
        fill_atlas_pixels(raster, upload_pixels, upload_width);
        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            uint8_t const* const src =
                upload_pixels + (static_cast<size_t>(y + TEXT_ATLAS_PADDING) * upload_width * 4u) +
                (TEXT_ATLAS_PADDING * 4u);
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                plot_mask(
                    image,
                    x0 + static_cast<int32_t>(x),
                    y0 + static_cast<int32_t>(y),
                    src[x * 4u + 0u],
                    src[x * 4u + 1u],
                    src[x * 4u + 2u]
                );
            }
        }
    }

    auto composite_raster(
        Image image,
        gui::font_provider::GlyphRaster const& raster,
        int32_t x0,
        int32_t y0,
        Color8 color
    ) -> void {
        for (uint32_t y = 0u; y < raster.size.height; ++y) {
            for (uint32_t x = 0u; x < raster.size.width; ++x) {
                uint8_t r = 0u;
                uint8_t g = 0u;
                uint8_t b = 0u;
                raster_pixel(raster, x, y, r, g, b);
                composite_pixel(
                    image,
                    x0 + static_cast<int32_t>(x),
                    y0 + static_cast<int32_t>(y),
                    color,
                    r,
                    g,
                    b
                );
            }
        }
    }

    auto draw_glyph(
        ProbeContext& context,
        StageImages& images,
        gui::font_cache::TextRun const& run,
        gui::font_cache::TextGlyph const& glyph,
        float command_x,
        Policy policy,
        Color8 color
    ) -> void {
        QuantizedOrigin const x_origin = quantize_origin(command_x + glyph.x + glyph.offset_x);
        QuantizedOrigin const y_origin = quantize_origin(run.baseline_y - glyph.offset_y);

        gui::font_provider::GlyphRasterDesc desc = {};
        desc.raster_policy = policy.raster_policy;
        desc.phase_x = x_origin.phase;
        desc.phase_y = y_origin.phase;

        gui::font_provider::GlyphRaster const cached =
            gui::font_cache::glyph_raster(context.font, glyph, desc);
        if (cached.pixels == nullptr || cached.size.width == 0u || cached.size.height == 0u) {
            return;
        }

        int32_t const dst_x =
            static_cast<int32_t>(std::round(origin_base(x_origin) + cached.offset_x));
        int32_t const dst_y =
            static_cast<int32_t>(std::round(origin_base(y_origin) + cached.offset_y));

        ArenaTemp temp = begin_thread_temp_arena();
        gui::font_provider::GlyphRaster raw = {};
        gui::font_provider::raster_glyph(
            glyph.font,
            glyph.size,
            glyph.glyph_index,
            policy.raster_policy,
            x_origin.phase,
            y_origin.phase,
            *temp.arena(),
            raw
        );

        plot_raster_mask(images.raw_mask, raw, dst_x, dst_y);
        plot_raster_mask(images.font_cache_mask, cached, dst_x, dst_y);
        plot_atlas_upload_mask(context.arena, images.atlas_upload_mask, cached, dst_x, dst_y);
        composite_raster(images.cpu_composite, cached, dst_x, dst_y, color);
    }

    auto draw_token(
        ProbeContext& context,
        StageImages& images,
        StrRef line,
        size_t start,
        size_t end,
        Policy policy
    ) -> void {
        StrRef const text = line.slice(start, end - start);
        gui::font_cache::TextRun run = {};
        gui::font_cache::text_run(context.cache, context.font, FONT_SIZE, text, run);

        float const command_x = std::round(context.char_width * static_cast<float>(start));
        Color8 const color = {255u, 116u, 132u};
        for (size_t index = 0u; index < run.glyph_count; ++index) {
            draw_glyph(context, images, run, run.glyphs[index], command_x, policy, color);
        }
    }

    auto render_policy(ProbeContext& context, Policy policy, StageImages& images) -> void {
        Color8 const black = {};
        Color8 const background = {18u, 23u, 30u};
        init_image(context.arena, images.raw_mask, CANVAS_WIDTH, CANVAS_HEIGHT, black);
        init_image(context.arena, images.font_cache_mask, CANVAS_WIDTH, CANVAS_HEIGHT, black);
        init_image(context.arena, images.atlas_upload_mask, CANVAS_WIDTH, CANVAS_HEIGHT, black);
        init_image(context.arena, images.cpu_composite, CANVAS_WIDTH, CANVAS_HEIGHT, background);

        draw_token(context, images, SAMPLE_TEXT, 0u, SAMPLE_TEXT.size(), policy);
    }

    [[nodiscard]] auto init_probe(StrRef font_path, ProbeContext& context) -> bool {
        context.arena.init();

        gui::font_provider::ContextDesc provider_desc = {};
        provider_desc.backend = gui::font_provider::Backend::DWRITE;
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(context.arena, provider_desc, context.provider);
        if (gui::font_provider::result_failed(provider_result)) {
            fmt::eprintf(
                "font provider failed: %s\n", gui::font_provider::result_name(provider_result)
            );
            return false;
        }

        gui::font_cache::create_cache(context.arena, context.provider, {}, context.cache);
        gui::font_cache::open_font_file(context.cache, font_path, context.font);
        if (!gui::font_cache::font_valid(context.font)) {
            fmt::eprintf("failed to open font: %s\n", font_path);
            return false;
        }

        context.char_width = gui::font_cache::text_advance(context.font, FONT_SIZE, "M");
        return context.char_width > 0.0f;
    }

    auto destroy_probe(ProbeContext& context) -> void {
        if (gui::font_cache::cache_valid(context.cache)) {
            gui::font_cache::destroy_cache(context.cache);
        }
        if (gui::font_provider::context_valid(context.provider)) {
            gui::font_provider::destroy_context(context.provider);
        }
        context.arena.destroy();
    }

    auto write_stage_images(StrRef artifact_dir, Policy policy, StageImages const& images) -> bool {
        char path[4096] = {};
        bool ok = true;
        ok = write_bmp(
                 make_path(
                     path, sizeof(path), artifact_dir, fmt::tprintf("%s_raw_mask.bmp", policy.name)
                 ),
                 images.raw_mask
             ) &&
             ok;
        ok = write_bmp(
                 make_path(
                     path,
                     sizeof(path),
                     artifact_dir,
                     fmt::tprintf("%s_font_cache_mask.bmp", policy.name)
                 ),
                 images.font_cache_mask
             ) &&
             ok;
        ok = write_bmp(
                 make_path(
                     path,
                     sizeof(path),
                     artifact_dir,
                     fmt::tprintf("%s_atlas_upload_mask.bmp", policy.name)
                 ),
                 images.atlas_upload_mask
             ) &&
             ok;
        ok = write_bmp(
                 make_path(
                     path,
                     sizeof(path),
                     artifact_dir,
                     fmt::tprintf("%s_cpu_composite.bmp", policy.name)
                 ),
                 images.cpu_composite
             ) &&
             ok;
        return ok;
    }

    auto write_json_string(std::FILE* file, StrRef text) -> void {
        std::fputc('"', file);
        for (size_t index = 0u; index < text.size(); ++index) {
            char const c = text.data()[index];
            if (c == '"' || c == '\\') {
                std::fputc('\\', file);
                std::fputc(c, file);
            } else if (c == '\n') {
                fmt::fprintf(file, "\\n");
            } else if (c == '\r') {
                fmt::fprintf(file, "\\r");
            } else if (c == '\t') {
                fmt::fprintf(file, "\\t");
            } else {
                std::fputc(c, file);
            }
        }
        std::fputc('"', file);
    }

    auto write_summary(StrRef artifact_dir, StrRef font_path) -> bool {
        char path[4096] = {};
        StrRef const summary_path =
            make_path(path, sizeof(path), artifact_dir, "stage_probe_summary.json");
        std::FILE* file = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&file, summary_path.data(), "wb") != 0) {
            return false;
        }
#else
        file = std::fopen(summary_path.data(), "wb");
#endif
        if (file == nullptr) {
            return false;
        }
        fmt::fprintf(file, "{\n  \"font_path\": ");
        write_json_string(file, font_path);
        fmt::fprintf(
            file, ",\n  \"font_size\": %.3f,\n  \"line\": ", static_cast<double>(FONT_SIZE)
        );
        write_json_string(file, SAMPLE_TEXT);
        fmt::fprintf(
            file,
            ",\n"
            "  \"canvas\": {\"width\": %u, \"height\": %u},\n"
            "  \"policies\": [\"sharp\", \"lcd_sharp\"],\n"
            "  \"notes\": \"raw and font-cache masks use identical glyph placement; atlas masks "
            "are rebuilt from the upload pixels used by text_atlas.\"\n"
            "}\n",
            CANVAS_WIDTH,
            CANVAS_HEIGHT
        );
        std::fclose(file);
        return true;
    }

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 3) {
        fmt::eprintf("usage: text_stage_probe <artifact_dir> <font_path>\n");
        return 2;
    }

    StrRef const artifact_dir = argv[1];
    StrRef const font_path = argv[2];

    ProbeContext context = {};
    if (!init_probe(font_path, context)) {
        destroy_probe(context);
        return 1;
    }

    Policy const policies[] = {
        {"sharp", gui::font_provider::RasterPolicy::SHARP_HINTED},
        {"lcd_sharp", gui::font_provider::RasterPolicy::LCD_SHARP_HINTED},
    };

    bool ok = true;
    for (Policy const policy : policies) {
        StageImages images = {};
        render_policy(context, policy, images);
        ok = write_stage_images(artifact_dir, policy, images) && ok;
    }
    ok = write_summary(artifact_dir, font_path) && ok;

    destroy_probe(context);
    return ok ? 0 : 1;
}
