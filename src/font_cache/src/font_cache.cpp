#include <base/memory.h>
#include <base/slice.h>
#include <base/vec.h>
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

        auto close_fonts(CacheImpl* impl) -> void {
            for (CacheFont* font = impl->first_font; font != nullptr; font = font->next) {
                font_provider::close_font(font->provider_font);
            }
            impl->first_font = nullptr;
        }

        auto open_provider_font(CacheImpl* impl,
                                font_provider::FontDesc const& desc,
                                Font& out_font) -> void {
            font_provider::Font provider_font = {};
            font_provider::open_font(impl->persistent_arena, impl->provider, desc, provider_font);

            CacheFont* const font = arena_new<CacheFont>(impl->persistent_arena);
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

    auto create_cache(Arena& arena,
                      font_provider::Context provider,
                      CacheDesc const& desc,
                      Cache& out_cache) -> void {
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
        size_t const slot_bytes = sizeof(CacheEntry*) * desc.cache_slot_count;
        std::memset(impl->slots, 0, slot_bytes);
        impl->slot_count = desc.cache_slot_count;
        impl->provider = provider;
        out_cache.handle = impl;
    }

    auto destroy_cache(Cache& cache) -> void {
        ASSERT(cache.handle != nullptr);

        CacheImpl* const impl = cache_from_handle(cache);
        close_fonts(impl);
        impl->slots = nullptr;
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

    auto metrics_from_font(Font font, float size, font_provider::Metrics& out_metrics) -> void {
        CacheFont* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);

        font_provider::metrics_from_font(impl->provider_font, size, out_metrics);
    }

    auto text_advance(Font font, float size, StrRef text) -> float {
        CacheFont* const impl = font_from_handle(font);
        ASSERT(impl != nullptr);

        return font_provider::text_advance(impl->provider_font, size, text);
    }

    auto text_run(Cache cache, Font font, float size, StrRef text, TextRun& out_run) -> void {
        CacheImpl* const impl = cache_from_handle(cache);
        CacheFont* const font_impl = font_from_handle(font);
        ASSERT(impl != nullptr);
        ASSERT(font_impl != nullptr);
        ASSERT(size > 0.0f);

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
        font_provider::RasterResult raster = {};
        font_provider::raster_text(font_impl->provider_font, size, text, *temp.arena(), raster);

        size_t const bytes = pixel_byte_count(raster.size, raster.stride);

        CacheEntry* const entry = arena_new<CacheEntry>(impl->cache_arena);
        entry->font = font_impl;
        entry->size_bits = size_bits;
        entry->text_hash = hash;
        copy_text(impl->cache_arena, text, entry->text);

        entry->run.size = raster.size;
        entry->run.stride = raster.stride;

        if (bytes) {
            auto data = arena_alloc<uint8_t>(impl->cache_arena, bytes);
            std::memcpy(data, raster.rgba_pixels, bytes);
            entry->run.rgba_pixels = data;
        }

        entry->run.advance = raster.advance;
        entry->run.height = raster.height;
        entry->next = impl->slots[slot_index];
        impl->slots[slot_index] = entry;

        out_run = entry->run;
    }

} // namespace gui::font_cache
