#include <algorithm>
#include <base/memory.h>
#include <base/slice.h>
#include <base/vec.h>
#include <cmath>
#include <cstring>
#include <font_cache/font_cache.h>
#include <limits>

namespace gui::font_cache {
    namespace {

        constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
        constexpr uint64_t FNV64_PRIME = 1099511628211ull;

        struct CacheImpl;

        struct CacheFont {
            CacheFont* next = nullptr;
            CacheImpl* cache = nullptr;
            font_provider::Font provider_font = {};
        };

        struct CacheEntry {
            CacheEntry* next = nullptr;
            CacheFont* font = nullptr;
            uint32_t size_bits = 0u;
            uint64_t text_hash = 0u;
            StrRef text = {};
            TextRun run = {};
        };

        struct AdvanceEntry {
            AdvanceEntry* next = nullptr;
            CacheFont* font = nullptr;
            uint32_t size_bits = 0u;
            uint64_t text_hash = 0u;
            StrRef text = {};
            float advance = 0.0f;
        };

        struct GlyphStrikeKey {
            font_provider::Font font = {};
            font_provider::Backend backend = font_provider::Backend::DEFAULT;
            uint32_t size_bits = 0u;
            uint16_t glyph_index = 0u;
            font_provider::RasterPolicy raster_policy = font_provider::DEFAULT_RASTER_POLICY;
            uint8_t phase_x = 0u;
            uint8_t phase_y = 0u;
            font_provider::PixelGeometry pixel_geometry =
                font_provider::PixelGeometry::RGB_HORIZONTAL;
            font_provider::TargetColorFormat color_format =
                font_provider::TargetColorFormat::RGBA8_UNORM;
            uint32_t text_gamma_bits = 0u;
            uint32_t text_contrast_bits = 0u;
        };

        struct GlyphEntry {
            GlyphEntry* next = nullptr;
            GlyphStrikeKey key = {};
            font_provider::GlyphRaster raster = {};
        };

        struct CacheImpl {
            Arena persistent_arena = {};
            Arena cache_arena = {};
            font_provider::Context provider = {};
            CacheFont* first_font = nullptr;
            CacheEntry** slots = nullptr;
            AdvanceEntry** advance_slots = nullptr;
            GlyphEntry** glyph_slots = nullptr;
            size_t slot_count = 0u;
        };

        [[nodiscard]] auto cache_from_handle(Cache cache) -> CacheImpl* {
            return static_cast<CacheImpl*>(cache.handle);
        }

        [[nodiscard]] auto font_from_handle(Font font) -> CacheFont* {
            return static_cast<CacheFont*>(font.handle);
        }

        [[nodiscard]] auto hash_bytes(uint64_t seed, void const* data, size_t size) -> uint64_t {
            uint64_t result = seed;
            auto const* bytes = static_cast<uint8_t const*>(data);

            for (size_t index = 0u; index < size; ++index) {
                result ^= static_cast<uint64_t>(bytes[index]);
                result *= FNV64_PRIME;
            }

            return result;
        }

        [[nodiscard]] auto hash_size(uint64_t seed, size_t value) -> uint64_t {
            return hash_bytes(seed, &value, sizeof(value));
        }

        [[nodiscard]] auto float_bits(float value) -> uint32_t {
            uint32_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        [[nodiscard]] auto text_run_hash(CacheFont const* font, uint32_t size_bits, StrRef text)
            -> uint64_t {
            uint64_t result = FNV64_OFFSET;
            result = hash_size(result, reinterpret_cast<size_t>(font));
            result = hash_bytes(result, &size_bits, sizeof(size_bits));
            result = hash_bytes(result, text.data(), text.size());
            return result;
        }

        [[nodiscard]] auto raster_policy_lcd(font_provider::RasterPolicy raster_policy) -> bool {
            return raster_policy == font_provider::RasterPolicy::LCD_SHARP_HINTED ||
                   raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED;
        }

        [[nodiscard]] auto grayscale_raster_policy(font_provider::RasterPolicy raster_policy)
            -> font_provider::RasterPolicy {
            return raster_policy == font_provider::RasterPolicy::LCD_SMOOTH_HINTED
                       ? font_provider::RasterPolicy::SMOOTH_HINTED
                       : font_provider::RasterPolicy::SHARP_HINTED;
        }

        [[nodiscard]] auto surface_allows_lcd_text(font_provider::SurfaceProps props) -> bool {
            return props.pixel_geometry != font_provider::PixelGeometry::UNKNOWN &&
                   props.color_format == font_provider::TargetColorFormat::RGBA8_UNORM;
        }

        [[nodiscard]] auto canonical_text_gamma(float gamma) -> float {
            return gamma > 0.0f ? gamma : 1.0f;
        }

        [[nodiscard]] auto canonical_surface_props(font_provider::SurfaceProps props)
            -> font_provider::SurfaceProps {
            props.text_gamma = canonical_text_gamma(props.text_gamma);
            props.text_contrast = std::clamp(props.text_contrast, 0.0f, 1.0f);
            return props;
        }

        [[nodiscard]] auto glyph_strike_key(
            font_provider::Font font,
            uint32_t size_bits,
            uint16_t glyph_index,
            font_provider::GlyphRasterDesc desc
        ) -> GlyphStrikeKey {
            desc.surface_props = canonical_surface_props(desc.surface_props);
            if (raster_policy_lcd(desc.raster_policy) &&
                !surface_allows_lcd_text(desc.surface_props)) {
                desc.raster_policy = grayscale_raster_policy(desc.raster_policy);
            }

            GlyphStrikeKey key = {};
            key.font = font;
            key.backend = font.backend;
            key.size_bits = size_bits;
            key.glyph_index = glyph_index;
            key.raster_policy = desc.raster_policy;
            key.phase_x = desc.phase_x % font_provider::GLYPH_RASTER_PHASE_COUNT;
            key.phase_y = desc.phase_y % font_provider::GLYPH_RASTER_PHASE_COUNT;
            key.pixel_geometry = desc.surface_props.pixel_geometry;
            key.color_format = desc.surface_props.color_format;
            key.text_gamma_bits = float_bits(desc.surface_props.text_gamma);
            key.text_contrast_bits = float_bits(desc.surface_props.text_contrast);
            return key;
        }

        [[nodiscard]] auto glyph_hash(GlyphStrikeKey const& key) -> uint64_t {
            uint64_t result = FNV64_OFFSET;
            result = hash_size(result, reinterpret_cast<size_t>(key.font.handle));
            result = hash_bytes(result, &key.backend, sizeof(key.backend));
            result = hash_bytes(result, &key.size_bits, sizeof(key.size_bits));
            result = hash_bytes(result, &key.glyph_index, sizeof(key.glyph_index));
            result = hash_bytes(result, &key.raster_policy, sizeof(key.raster_policy));
            result = hash_bytes(result, &key.phase_x, sizeof(key.phase_x));
            result = hash_bytes(result, &key.phase_y, sizeof(key.phase_y));
            result = hash_bytes(result, &key.pixel_geometry, sizeof(key.pixel_geometry));
            result = hash_bytes(result, &key.color_format, sizeof(key.color_format));
            result = hash_bytes(result, &key.text_gamma_bits, sizeof(key.text_gamma_bits));
            result = hash_bytes(result, &key.text_contrast_bits, sizeof(key.text_contrast_bits));
            return result;
        }

        [[nodiscard]] auto
        glyph_strike_key_equal(GlyphStrikeKey const& lhs, GlyphStrikeKey const& rhs) -> bool {
            return lhs.font.handle == rhs.font.handle && lhs.backend == rhs.backend &&
                   lhs.size_bits == rhs.size_bits && lhs.glyph_index == rhs.glyph_index &&
                   lhs.raster_policy == rhs.raster_policy && lhs.phase_x == rhs.phase_x &&
                   lhs.phase_y == rhs.phase_y && lhs.pixel_geometry == rhs.pixel_geometry &&
                   lhs.color_format == rhs.color_format &&
                   lhs.text_gamma_bits == rhs.text_gamma_bits &&
                   lhs.text_contrast_bits == rhs.text_contrast_bits;
        }

        [[nodiscard]] auto pixel_byte_count(font_provider::SizeU32 size, uint32_t stride)
            -> size_t {
            if (size.height == 0u || stride == 0u) {
                return 0u;
            }

            size_t const height = static_cast<size_t>(size.height);
            ASSERT(static_cast<size_t>(stride) <= std::numeric_limits<size_t>::max() / height);

            return static_cast<size_t>(stride) * height;
        }

        auto copy_text(Arena& arena, StrRef text, StrRef& out_text) -> void {
            if (text.empty()) {
                out_text = {};
                return;
            }

            char* const data = arena_alloc<char>(arena, text.size());
            std::memcpy(data, text.data(), text.size());
            out_text = StrRef(data, text.size());
        }

        [[nodiscard]] auto key_text_gamma(GlyphStrikeKey const& key) -> float {
            float gamma = 1.0f;
            std::memcpy(&gamma, &key.text_gamma_bits, sizeof(gamma));
            return gamma;
        }

        [[nodiscard]] auto key_text_contrast(GlyphStrikeKey const& key) -> float {
            float contrast = 0.0f;
            std::memcpy(&contrast, &key.text_contrast_bits, sizeof(contrast));
            return contrast;
        }

        [[nodiscard]] auto apply_contrast(float alpha, float contrast) -> float {
            return alpha + ((1.0f - alpha) * contrast * alpha);
        }

        [[nodiscard]] auto mask_coverage(GlyphStrikeKey const& key, uint8_t coverage) -> uint8_t {
            if (coverage == 0u || coverage == 255u) {
                return coverage;
            }

            float alpha = static_cast<float>(coverage) / 255.0f;
            float const contrast = key_text_contrast(key);
            alpha = apply_contrast(alpha, contrast);

            float const gamma = key_text_gamma(key);
            if (gamma != 1.0f) {
                alpha = std::pow(alpha, 1.0f / gamma);
            }

            return static_cast<uint8_t>(std::clamp(alpha * 255.0f + 0.5f, 0.0f, 255.0f));
        }

        auto copy_glyph_raster(
            Arena& arena,
            GlyphStrikeKey const& key,
            font_provider::GlyphRaster const& raster,
            font_provider::GlyphRaster& out
        ) -> void {
            out = raster;
            size_t const bytes = pixel_byte_count(raster.size, raster.stride);
            if (bytes == 0u) {
                out.pixels = nullptr;
                return;
            }

            uint8_t* const data = arena_alloc<uint8_t>(arena, bytes);
            std::memset(data, 0, bytes);
            if (raster.format == font_provider::RasterFormat::LCD_RGB) {
                for (uint32_t y = 0u; y < raster.size.height; ++y) {
                    uint8_t const* const src =
                        raster.pixels + (static_cast<size_t>(y) * raster.stride);
                    uint8_t* const dst = data + (static_cast<size_t>(y) * raster.stride);
                    for (uint32_t x = 0u; x < raster.size.width; ++x) {
                        uint8_t r = mask_coverage(key, src[x * 4u + 0u]);
                        uint8_t const g = mask_coverage(key, src[x * 4u + 1u]);
                        uint8_t b = mask_coverage(key, src[x * 4u + 2u]);
                        if (key.pixel_geometry == font_provider::PixelGeometry::BGR_HORIZONTAL) {
                            std::swap(r, b);
                        }
                        dst[x * 4u + 0u] = r;
                        dst[x * 4u + 1u] = g;
                        dst[x * 4u + 2u] = b;
                        dst[x * 4u + 3u] = std::max(std::max(r, g), b);
                    }
                }
            } else {
                for (uint32_t y = 0u; y < raster.size.height; ++y) {
                    uint8_t const* const src =
                        raster.pixels + (static_cast<size_t>(y) * raster.stride);
                    uint8_t* const dst = data + (static_cast<size_t>(y) * raster.stride);
                    for (uint32_t x = 0u; x < raster.size.width; ++x) {
                        dst[x] = mask_coverage(key, src[x]);
                    }
                }
            }
            out.pixels = data;
        }

        [[nodiscard]] auto cached_glyph_raster(
            CacheImpl* cache,
            font_provider::Font font,
            uint32_t size_bits,
            float size,
            uint16_t glyph_index,
            font_provider::GlyphRasterDesc desc
        ) -> font_provider::GlyphRaster {
            GlyphStrikeKey const key = glyph_strike_key(font, size_bits, glyph_index, desc);
            uint64_t const hash = glyph_hash(key);
            size_t const slot_index = static_cast<size_t>(hash % cache->slot_count);

            for (GlyphEntry* entry = cache->glyph_slots[slot_index]; entry != nullptr;
                 entry = entry->next) {
                if (glyph_strike_key_equal(entry->key, key)) {
                    return entry->raster;
                }
            }

            ArenaTemp temp = begin_thread_temp_arena();
            font_provider::GlyphRaster raster = {};
            font_provider::raster_glyph(
                font,
                size,
                glyph_index,
                key.raster_policy,
                key.phase_x,
                key.phase_y,
                *temp.arena(),
                raster
            );

            GlyphEntry* const entry = arena_new<GlyphEntry>(cache->cache_arena);
            entry->key = key;
            copy_glyph_raster(cache->cache_arena, key, raster, entry->raster);
            entry->next = cache->glyph_slots[slot_index];
            cache->glyph_slots[slot_index] = entry;
            return entry->raster;
        }

        auto close_fonts(CacheImpl* impl) -> void {
            for (CacheFont* font = impl->first_font; font != nullptr; font = font->next) {
                font_provider::close_font(font->provider_font);
            }
            impl->first_font = nullptr;
        }

        auto
        open_provider_font(CacheImpl* impl, font_provider::FontDesc const& desc, Font& out_font)
            -> void {
            font_provider::Font provider_font = {};
            font_provider::open_font(impl->persistent_arena, impl->provider, desc, provider_font);
            if (!font_provider::font_valid(provider_font)) {
                out_font = {};
                return;
            }

            CacheFont* const font = arena_new<CacheFont>(impl->persistent_arena);
            font->cache = impl;
            font->provider_font = provider_font;
            font->next = impl->first_font;
            impl->first_font = font;
            out_font.handle = font;
        }

    } // namespace

    auto cache_valid(Cache cache) -> bool {
        return cache.handle != nullptr;
    }

    auto font_valid(Font font) -> bool {
        return font.handle != nullptr;
    }

    auto create_cache(
        Arena& arena, font_provider::Context provider, CacheDesc const& desc, Cache& out_cache
    ) -> void {
        ASSERT(font_provider::context_valid(provider));
        ASSERT(out_cache.handle == nullptr);
        ASSERT(desc.cache_slot_count != 0u);
        ASSERT(desc.arena_reserve_size != 0u);
        ASSERT(desc.arena_commit_size != 0u);

        CacheImpl* const impl = arena_new<CacheImpl>(arena);

        ArenaOptions const persistent_options = {1024u * 1024u, DEFAULT_ARENA_COMMIT_SIZE};
        ArenaOptions const cache_options = {desc.arena_reserve_size, desc.arena_commit_size};
        impl->persistent_arena.init(persistent_options);
        impl->cache_arena.init(cache_options);

        impl->slots = arena_alloc<CacheEntry*>(impl->persistent_arena, desc.cache_slot_count);
        impl->advance_slots =
            arena_alloc<AdvanceEntry*>(impl->persistent_arena, desc.cache_slot_count);
        impl->glyph_slots = arena_alloc<GlyphEntry*>(impl->persistent_arena, desc.cache_slot_count);
        size_t const slot_bytes = sizeof(CacheEntry*) * desc.cache_slot_count;
        std::memset(impl->slots, 0, slot_bytes);
        std::memset(impl->advance_slots, 0, sizeof(AdvanceEntry*) * desc.cache_slot_count);
        std::memset(impl->glyph_slots, 0, sizeof(GlyphEntry*) * desc.cache_slot_count);
        impl->slot_count = desc.cache_slot_count;
        impl->provider = provider;
        out_cache.handle = impl;
    }

    auto destroy_cache(Cache& cache) -> void {
        ASSERT(cache.handle != nullptr);

        CacheImpl* const impl = cache_from_handle(cache);
        close_fonts(impl);
        impl->slots = nullptr;
        impl->advance_slots = nullptr;
        impl->glyph_slots = nullptr;
        impl->slot_count = 0u;
        impl->cache_arena.destroy();
        impl->persistent_arena.destroy();
        cache.handle = nullptr;
    }

    auto clear_cache(Cache cache) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        if (impl == nullptr || impl->slots == nullptr) {
            return;
        }

        size_t const slot_bytes = sizeof(CacheEntry*) * impl->slot_count;
        std::memset(impl->slots, 0, slot_bytes);
        std::memset(impl->advance_slots, 0, sizeof(AdvanceEntry*) * impl->slot_count);
        std::memset(impl->glyph_slots, 0, sizeof(GlyphEntry*) * impl->slot_count);
        impl->cache_arena.reset();
    }

    auto open_system_font(Cache cache, StrRef family_name, Font& out_font) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        ASSERT(impl != nullptr);
        ASSERT(out_font.handle == nullptr);

        open_provider_font(impl, font_provider::FontDesc{family_name, {}}, out_font);
    }

    auto open_font_file(Cache cache, StrRef file_path, Font& out_font) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        ASSERT(impl != nullptr);
        ASSERT(!file_path.empty());
        ASSERT(out_font.handle == nullptr);

        open_provider_font(impl, font_provider::FontDesc{{}, file_path}, out_font);
    }

    auto open_font_data(Cache cache, Slice<uint8_t const> data, Font& out_font) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        ASSERT(impl != nullptr);
        ASSERT(!data.empty());
        ASSERT(out_font.handle == nullptr);

        open_provider_font(impl, font_provider::FontDesc{{}, {}, data}, out_font);
    }

    auto metrics_from_font(Font font, float size, font_provider::Metrics& out_metrics) -> void {
        CacheFont* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);

        font_provider::metrics_from_font(impl->provider_font, size, out_metrics);
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        CacheFont* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);

        if (text.empty()) {
            return 0.0f;
        }

        CacheImpl* const cache = impl->cache;
        if (cache == nullptr || cache->advance_slots == nullptr) {
            return font_provider::text_advance(impl->provider_font, size, text);
        }

        uint32_t const size_bits = float_bits(size);
        uint64_t const hash = text_run_hash(impl, size_bits, text);
        size_t const slot_index = static_cast<size_t>(hash % cache->slot_count);

        for (AdvanceEntry* entry = cache->advance_slots[slot_index]; entry != nullptr;
             entry = entry->next) {
            if (entry->font == impl && entry->size_bits == size_bits && entry->text_hash == hash &&
                entry->text == text) {
                return entry->advance;
            }
        }

        float const advance = font_provider::text_advance(impl->provider_font, size, text);
        AdvanceEntry* const entry = arena_new<AdvanceEntry>(cache->cache_arena);
        entry->font = impl;
        entry->size_bits = size_bits;
        entry->text_hash = hash;
        copy_text(cache->cache_arena, text, entry->text);
        entry->advance = advance;
        entry->next = cache->advance_slots[slot_index];
        cache->advance_slots[slot_index] = entry;
        return advance;
    }

    auto text_run(Cache cache, Font font, float size, StrRef text, TextRun& out_run) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        CacheFont* const font_impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(font_impl != nullptr);
        ASSERT(size > 0.0f);

        if (text.empty()) {
            out_run = {};
            return;
        }

        uint32_t const size_bits = float_bits(size);
        uint64_t const hash = text_run_hash(font_impl, size_bits, text);
        size_t const slot_index = static_cast<size_t>(hash % impl->slot_count);

        for (CacheEntry* entry = impl->slots[slot_index]; entry != nullptr; entry = entry->next) {
            if (entry->font == font_impl && entry->size_bits == size_bits &&
                entry->text_hash == hash && entry->text == text) {
                out_run = entry->run;
                return;
            }
        }

        ArenaTemp temp = begin_thread_temp_arena();
        font_provider::ShapedText shaped = {};
        font_provider::shape_text(font_impl->provider_font, size, text, *temp.arena(), shaped);

        CacheEntry* const entry = arena_new<CacheEntry>(impl->cache_arena);
        entry->font = font_impl;
        entry->size_bits = size_bits;
        entry->text_hash = hash;
        copy_text(impl->cache_arena, text, entry->text);

        entry->run.size = shaped.size;
        entry->run.advance = shaped.advance;
        entry->run.origin_x = shaped.origin_x;
        entry->run.origin_y = shaped.origin_y;
        entry->run.baseline_y = shaped.baseline_y;
        entry->run.offset_y = shaped.origin_y;
        entry->run.height = shaped.height;
        entry->run.glyph_count = shaped.glyph_count;
        entry->run.run_count = shaped.run_count;

        if (shaped.glyph_count != 0u) {
            TextGlyph* const glyphs = arena_alloc<TextGlyph>(impl->cache_arena, shaped.glyph_count);
            for (size_t index = 0u; index < shaped.glyph_count; ++index) {
                font_provider::ShapedGlyph const& shaped_glyph = shaped.glyphs[index];
                TextGlyph& glyph = glyphs[index];
                glyph.font = shaped_glyph.font;
                glyph.glyph_index = shaped_glyph.glyph_index;
                glyph.run_index = shaped_glyph.run_index;
                glyph.size = shaped_glyph.size;
                glyph.x = shaped_glyph.x;
                glyph.advance = shaped_glyph.advance;
                glyph.offset_x = shaped_glyph.offset_x;
                glyph.offset_y = shaped_glyph.offset_y;
                glyph.cluster = shaped_glyph.cluster;
                glyph.utf8_start = shaped_glyph.utf8_start;
                glyph.utf8_end = shaped_glyph.utf8_end;
            }
            entry->run.glyphs = glyphs;
        }
        if (shaped.run_count != 0u) {
            TextBlobRun* const runs = arena_alloc<TextBlobRun>(impl->cache_arena, shaped.run_count);
            for (size_t index = 0u; index < shaped.run_count; ++index) {
                font_provider::ShapedRun const& shaped_run = shaped.runs[index];
                TextBlobRun& run = runs[index];
                run.font = shaped_run.font;
                run.first_glyph = shaped_run.first_glyph;
                run.glyph_count = shaped_run.glyph_count;
                run.utf8_start = shaped_run.utf8_start;
                run.utf8_end = shaped_run.utf8_end;
                run.script = shaped_run.script;
                run.bidi_level = shaped_run.bidi_level;
                run.right_to_left = shaped_run.right_to_left;
            }
            entry->run.runs = runs;
        }

        entry->next = impl->slots[slot_index];
        impl->slots[slot_index] = entry;

        out_run = entry->run;
    }

    auto glyph_raster(Font font, TextGlyph const& glyph, font_provider::GlyphRasterDesc const& desc)
        -> font_provider::GlyphRaster {
        CacheFont* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(impl->cache != nullptr);
        ASSERT(font_provider::font_valid(glyph.font));
        ASSERT(glyph.size > 0.0f);

        return cached_glyph_raster(
            impl->cache, glyph.font, float_bits(glyph.size), glyph.size, glyph.glyph_index, desc
        );
    }

    auto glyph_raster(
        Font font,
        TextGlyph const& glyph,
        font_provider::RasterPolicy raster_policy,
        uint8_t phase_x,
        uint8_t phase_y
    ) -> font_provider::GlyphRaster {
        font_provider::GlyphRasterDesc desc = {};
        desc.raster_policy = raster_policy;
        desc.phase_x = phase_x;
        desc.phase_y = phase_y;
        return glyph_raster(font, glyph, desc);
    }

    auto glyph_raster(Font font, TextGlyph const& glyph, uint8_t phase_x, uint8_t phase_y)
        -> font_provider::GlyphRaster {
        return glyph_raster(font, glyph, font_provider::DEFAULT_RASTER_POLICY, phase_x, phase_y);
    }

} // namespace gui::font_cache
