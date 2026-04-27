#include <base/memory.h>
#include <cstring>
#include <font_cache/font_cache.h>
#include <limits>

namespace gui::font_cache {
    namespace {

        constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
        constexpr uint64_t FNV64_PRIME = 1099511628211ull;

        struct CacheFont {
            CacheFont* next = nullptr;
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

        struct CacheImpl {
            Arena persistent_arena = {};
            Arena cache_arena = {};
            font_provider::Context provider = {};
            CacheFont* first_font = nullptr;
            CacheEntry** slots = nullptr;
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

        [[nodiscard]] auto
        pixel_byte_count(font_provider::SizeU32 size, uint32_t stride, size_t* out) -> bool {
            if (size.height == 0u || stride == 0u) {
                *out = 0u;
                return true;
            }

            size_t const height = static_cast<size_t>(size.height);
            if (static_cast<size_t>(stride) > std::numeric_limits<size_t>::max() / height) {
                return false;
            }

            *out = static_cast<size_t>(stride) * height;
            return true;
        }

        auto copy_text(Arena& arena, StrRef text, StrRef* out_text) -> void {
            if (text.empty()) {
                *out_text = {};
                return;
            }

            char* const data = arena_alloc<char>(arena, text.size());
            std::memcpy(data, text.data(), text.size());
            *out_text = StrRef(data, text.size());
        }

        auto copy_pixels(Arena& arena,
                         uint8_t const* pixels,
                         size_t byte_count,
                         uint8_t const** out_pixels) -> void {
            if (byte_count == 0u) {
                *out_pixels = nullptr;
                return;
            }

            uint8_t* const data = arena_alloc<uint8_t>(arena, byte_count);
            std::memcpy(data, pixels, byte_count);
            *out_pixels = data;
        }

        auto close_fonts(CacheImpl* impl) -> void {
            for (CacheFont* font = impl->first_font; font != nullptr; font = font->next) {
                font_provider::close_font(&font->provider_font);
            }
            impl->first_font = nullptr;
        }

        [[nodiscard]] auto open_provider_font(CacheImpl* impl,
                                              font_provider::FontDesc const& desc,
                                              Font* out_font) -> Result {
            font_provider::Font provider_font = {};
            Result const open_result = font_provider::open_font(
                impl->persistent_arena, impl->provider, desc, &provider_font);
            if (font_provider::result_failed(open_result)) {
                return open_result;
            }

            CacheFont* const font = arena_new<CacheFont>(impl->persistent_arena);
            font->provider_font = provider_font;
            font->next = impl->first_font;
            impl->first_font = font;
            out_font->handle = font;
            return Result::OK;
        }

    } // namespace

    auto cache_valid(Cache cache) -> bool {
        return cache.handle != nullptr;
    }

    auto font_valid(Font font) -> bool {
        return font.handle != nullptr;
    }

    auto create_cache(Arena& arena,
                      font_provider::Context provider,
                      CacheDesc const& desc,
                      Cache* out_cache) -> Result {
        if (!font_provider::context_valid(provider) || out_cache == nullptr ||
            out_cache->handle != nullptr || desc.cache_slot_count == 0u ||
            desc.arena_reserve_size == 0u || desc.arena_commit_size == 0u) {
            return Result::INVALID_ARGUMENT;
        }

        CacheImpl* const impl = arena_new<CacheImpl>(arena);

        ArenaOptions const persistent_options = {1024u * 1024u, DEFAULT_ARENA_COMMIT_SIZE};
        ArenaOptions const cache_options = {desc.arena_reserve_size, desc.arena_commit_size};
        impl->persistent_arena.init(persistent_options);
        impl->cache_arena.init(cache_options);

        impl->slots = arena_alloc<CacheEntry*>(impl->persistent_arena, desc.cache_slot_count);
        size_t const slot_bytes = sizeof(CacheEntry*) * desc.cache_slot_count;
        std::memset(impl->slots, 0, slot_bytes);
        impl->slot_count = desc.cache_slot_count;
        impl->provider = provider;
        out_cache->handle = impl;
        return Result::OK;
    }

    auto destroy_cache(Cache* cache) -> void {
        if (cache == nullptr || cache->handle == nullptr) {
            return;
        }

        CacheImpl* const impl = cache_from_handle(*cache);
        close_fonts(impl);
        impl->slots = nullptr;
        impl->slot_count = 0u;
        impl->cache_arena.destroy();
        impl->persistent_arena.destroy();
        cache->handle = nullptr;
    }

    auto clear_cache(Cache cache) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        if (impl == nullptr || impl->slots == nullptr) {
            return;
        }

        size_t const slot_bytes = sizeof(CacheEntry*) * impl->slot_count;
        std::memset(impl->slots, 0, slot_bytes);
        impl->cache_arena.reset();
    }

    auto open_system_font(Cache cache, StrRef family_name, Font* out_font) -> Result {
        CacheImpl* const impl = cache_from_handle(cache);
        if (impl == nullptr || out_font == nullptr || out_font->handle != nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        return open_provider_font(impl, font_provider::FontDesc{family_name, {}}, out_font);
    }

    auto open_font_file(Cache cache, StrRef file_path, Font* out_font) -> Result {
        CacheImpl* const impl = cache_from_handle(cache);
        if (impl == nullptr || file_path.empty() || out_font == nullptr ||
            out_font->handle != nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        return open_provider_font(impl, font_provider::FontDesc{{}, file_path}, out_font);
    }

    auto metrics_from_font(Font font, float size, font_provider::Metrics* out_metrics) -> Result {
        CacheFont* const impl = font_from_handle(font);
        if (impl == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        return font_provider::metrics_from_font(impl->provider_font, size, out_metrics);
    }

    auto text_run(Cache cache, Font font, float size, StrRef text, TextRun* out_run) -> Result {
        CacheImpl* const impl = cache_from_handle(cache);
        CacheFont* const font_impl = font_from_handle(font);
        if (impl == nullptr || font_impl == nullptr || size <= 0.0f || out_run == nullptr) {
            return Result::INVALID_ARGUMENT;
        }

        uint32_t const size_bits = float_bits(size);
        uint64_t const hash = text_run_hash(font_impl, size_bits, text);
        size_t const slot_index = static_cast<size_t>(hash % impl->slot_count);

        for (CacheEntry* entry = impl->slots[slot_index]; entry != nullptr; entry = entry->next) {
            if (entry->font == font_impl && entry->size_bits == size_bits &&
                entry->text_hash == hash && entry->text == text) {
                *out_run = entry->run;
                return Result::OK;
            }
        }

        ArenaTemp temp = begin_thread_temp_arena();
        font_provider::RasterResult raster = {};
        Result const raster_result = font_provider::raster_text(
            font_impl->provider_font, size, text, *temp.arena(), &raster);
        if (font_provider::result_failed(raster_result)) {
            return raster_result;
        }

        size_t bytes = 0u;
        if (!pixel_byte_count(raster.size, raster.stride, &bytes)) {
            return Result::OUT_OF_MEMORY;
        }

        CacheEntry* const entry = arena_new<CacheEntry>(impl->cache_arena);
        entry->font = font_impl;
        entry->size_bits = size_bits;
        entry->text_hash = hash;
        copy_text(impl->cache_arena, text, &entry->text);

        entry->run.size = raster.size;
        entry->run.stride = raster.stride;
        copy_pixels(impl->cache_arena, raster.rgba_pixels, bytes, &entry->run.rgba_pixels);

        entry->run.advance = raster.advance;
        entry->run.height = raster.height;
        entry->next = impl->slots[slot_index];
        impl->slots[slot_index] = entry;

        *out_run = entry->run;
        return Result::OK;
    }

} // namespace gui::font_cache
