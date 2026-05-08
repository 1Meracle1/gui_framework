#pragma once

#include <base/assert.h>
#include <base/config.h>
#include <base/memory.h>
#include <base/str_ref.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace hash_map_detail {
    inline constexpr size_t MAP_LOAD_FACTOR = 75u;
    inline constexpr size_t MAP_MIN_LOG2_CAPACITY = 3u;
    inline constexpr size_t MAP_CACHE_LINE_LOG2 = 6u;
    inline constexpr size_t MAP_CACHE_LINE_SIZE = size_t{1u} << MAP_CACHE_LINE_LOG2;
    inline constexpr uintptr_t DATA_TAG_MASK = MAP_CACHE_LINE_SIZE - 1u;
    inline constexpr uintptr_t TOMBSTONE_MASK = uintptr_t{1u} << (sizeof(uintptr_t) * 8u - 1u);
    inline constexpr uintptr_t HASH_MASK = TOMBSTONE_MASK - 1u;
    inline constexpr uint64_t INITIAL_HASH_SEED = 0xcbf29ce484222325ull;
    inline constexpr uint64_t FNV64_PRIME = 0x100000001b3ull;

    using MapHash = uintptr_t;

    static_assert(MAP_LOAD_FACTOR < 100u);
    static_assert(MAP_CACHE_LINE_SIZE >= 64u);

    template <typename T>
    inline constexpr size_t CELL_ELEMENTS =
        sizeof(T) < MAP_CACHE_LINE_SIZE ? MAP_CACHE_LINE_SIZE / sizeof(T) : 1u;

    template <typename T>
    inline constexpr size_t CELL_SIZE =
        sizeof(T) < MAP_CACHE_LINE_SIZE
            ? MAP_CACHE_LINE_SIZE
            : ((sizeof(T) + MAP_CACHE_LINE_SIZE - 1u) & ~(MAP_CACHE_LINE_SIZE - 1u));

    template <typename T>
    inline constexpr bool HAS_ODIN_CELL_ALIGNMENT = alignof(T) <= MAP_CACHE_LINE_SIZE;

    [[nodiscard]] constexpr auto is_power_of_two(size_t value) -> bool {
        return value != 0u && (value & (value - 1u)) == 0u;
    }

    [[nodiscard]] inline auto add_overflows(size_t lhs, size_t rhs, size_t& out) -> bool {
        if (lhs > std::numeric_limits<size_t>::max() - rhs) {
            return true;
        }
        out = lhs + rhs;
        return false;
    }

    [[nodiscard]] inline auto mul_overflows(size_t lhs, size_t rhs, size_t& out) -> bool {
        if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs) {
            return true;
        }
        out = lhs * rhs;
        return false;
    }

    [[nodiscard]] inline auto ceil_log2(size_t value) -> size_t {
        DEBUG_ASSERT(value > 0u);

        size_t log2 = 0u;
        size_t capacity = 1u;
        while (capacity < value) {
            DEBUG_ASSERT(capacity <= std::numeric_limits<size_t>::max() / 2u);
            capacity <<= 1u;
            log2 += 1u;
        }

        return log2;
    }

    [[nodiscard]] inline auto splitmix(uintptr_t data) -> uintptr_t {
        if constexpr (sizeof(uintptr_t) == sizeof(uint64_t)) {
            uint64_t mix = static_cast<uint64_t>(data) + 0x9e3779b97f4a7c15ull;
            mix = (mix ^ (mix >> 30u)) * 0xbf58476d1ce4e5b9ull;
            mix = (mix ^ (mix >> 27u)) * 0x94d049bb133111ebull;
            return static_cast<uintptr_t>(mix ^ (mix >> 31u));
        } else {
            uint32_t mix = static_cast<uint32_t>(data) + 0x9e3779b9u;
            mix = (mix ^ (mix >> 16u)) * 0x21f0aaadu;
            mix = (mix ^ (mix >> 15u)) * 0x735a2d97u;
            return static_cast<uintptr_t>(mix ^ (mix >> 15u));
        }
    }

    [[nodiscard]] inline auto sanitize_hash(uintptr_t hash) -> MapHash {
        MapHash result = static_cast<MapHash>(hash) & HASH_MASK;
        result |= static_cast<MapHash>(result == 0u);
        return result;
    }

    [[nodiscard]] inline auto hash_bytes(void const* data, size_t size, uintptr_t seed)
        -> uintptr_t {
        uint64_t hash = static_cast<uint64_t>(seed) + INITIAL_HASH_SEED;
        auto const* bytes = static_cast<uint8_t const*>(data);

        for (size_t index = 0u; index < size; ++index) {
            hash = (hash ^ static_cast<uint64_t>(bytes[index])) * FNV64_PRIME;
        }

        return sanitize_hash(static_cast<uintptr_t>(hash));
    }

    [[nodiscard]] inline auto hash_cstring(char const* text, uintptr_t seed) -> uintptr_t {
        uint64_t hash = static_cast<uint64_t>(seed) + INITIAL_HASH_SEED;

        if (text != nullptr) {
            while (*text != '\0') {
                hash = (hash ^ static_cast<uint64_t>(static_cast<uint8_t>(*text))) * FNV64_PRIME;
                ++text;
            }
        }

        return sanitize_hash(static_cast<uintptr_t>(hash));
    }

    [[nodiscard]] inline auto cstring_equal(char const* lhs, char const* rhs) -> bool {
        if (lhs == rhs) {
            return true;
        }

        if (lhs == nullptr || rhs == nullptr) {
            return false;
        }

        while (*lhs != '\0' && *rhs != '\0') {
            if (*lhs != *rhs) {
                return false;
            }
            ++lhs;
            ++rhs;
        }

        return *lhs == *rhs;
    }

    template <typename T>
    inline constexpr bool IS_CHAR_POINTER =
        std::is_pointer_v<T> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>;

    template <typename T>
    [[nodiscard]] auto cell_storage_size(size_t capacity, size_t* out) -> bool {
        size_t cells = 0u;
        if (add_overflows(capacity, CELL_ELEMENTS<T> - 1u, cells)) {
            return false;
        }

        cells /= CELL_ELEMENTS<T>;
        return !mul_overflows(cells, CELL_SIZE<T>, *out);
    }

    template <typename T> [[nodiscard]] auto cell_index(std::byte* base, size_t index) -> T* {
        if constexpr (CELL_ELEMENTS<T> == 1u) {
            return reinterpret_cast<T*>(base + (index * CELL_SIZE<T>));
        } else if constexpr (is_power_of_two(CELL_ELEMENTS<T>)) {
            size_t constexpr SHIFT = CELL_ELEMENTS<T> == 2u    ? 1u
                                     : CELL_ELEMENTS<T> == 4u  ? 2u
                                     : CELL_ELEMENTS<T> == 8u  ? 3u
                                     : CELL_ELEMENTS<T> == 16u ? 4u
                                     : CELL_ELEMENTS<T> == 32u ? 5u
                                                               : 6u;
            size_t const cell = index >> SHIFT;
            size_t const element = index & (CELL_ELEMENTS<T> - 1u);
            return reinterpret_cast<T*>(base + (cell * CELL_SIZE<T>)+(element * sizeof(T)));
        } else {
            size_t const cell = index / CELL_ELEMENTS<T>;
            size_t const element = index % CELL_ELEMENTS<T>;
            return reinterpret_cast<T*>(base + (cell * CELL_SIZE<T>)+(element * sizeof(T)));
        }
    }

    template <typename T>
    [[nodiscard]] auto cell_index(std::byte const* base, size_t index) -> T const* {
        return cell_index<T>(const_cast<std::byte*>(base), index);
    }

    [[nodiscard]] inline auto map_hash_is_empty(MapHash hash) -> bool {
        return hash == 0u;
    }

    [[nodiscard]] inline auto map_hash_is_deleted(MapHash hash) -> bool {
        return (hash & TOMBSTONE_MASK) != 0u;
    }

    [[nodiscard]] inline auto map_hash_is_valid(MapHash hash) -> bool {
        return hash != 0u && (hash & TOMBSTONE_MASK) == 0u;
    }

} // namespace hash_map_detail

template <typename T> struct HashMapDefaultHasher {
    [[nodiscard]] auto operator()(T const& key, uintptr_t seed) const -> uintptr_t {
        using Key = std::remove_cv_t<T>;

        if constexpr (std::is_same_v<Key, StrRef>) {
            return hash_map_detail::hash_bytes(key.data(), key.size(), seed);
        } else if constexpr (hash_map_detail::IS_CHAR_POINTER<Key>) {
            return hash_map_detail::hash_cstring(key, seed);
        } else if constexpr (std::is_floating_point_v<Key>) {
            if (key == Key{}) {
                uint8_t zero[sizeof(Key)] = {};
                return hash_map_detail::hash_bytes(zero, sizeof(zero), seed);
            }

            return hash_map_detail::hash_bytes(&key, sizeof(key), seed);
        } else {
            static_assert(
                std::is_trivially_copyable_v<Key>,
                "HashMapDefaultHasher requires a trivially copyable key type"
            );
            return hash_map_detail::hash_bytes(&key, sizeof(key), seed);
        }
    }
};

template <typename T> struct HashMapDefaultEqual {
    [[nodiscard]] auto operator()(T const& lhs, T const& rhs) const -> bool {
        using Key = std::remove_cv_t<T>;

        if constexpr (hash_map_detail::IS_CHAR_POINTER<Key>) {
            return hash_map_detail::cstring_equal(lhs, rhs);
        } else {
            return lhs == rhs;
        }
    }
};

template <
    typename Key,
    typename Value,
    typename Hasher = HashMapDefaultHasher<Key>,
    typename KeyEqual = HashMapDefaultEqual<Key>>
class HashMap final {
  public:
    struct Entry {
        Key* key = nullptr;
        Value* value = nullptr;
    };

    struct ConstEntry {
        Key const* key;
        Value const* value;
    };

    struct InsertResult {
        Key* key = nullptr;
        Value* value = nullptr;
        bool inserted = false;
    };

    struct Pair {
        Key key = {};
        Value value = {};
    };

    class Iterator {
      public:
        Iterator() = default;

        [[nodiscard]] auto operator*() const -> Entry {
            return Entry{m_map->key_at(m_index), m_map->value_at(m_index)};
        }

        auto operator++() -> Iterator& {
            m_index += 1u;
            skip_invalid();
            return *this;
        }

        auto operator++(int) -> Iterator {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] friend auto operator==(Iterator const& lhs, Iterator const& rhs) -> bool {
            return lhs.m_map == rhs.m_map && lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs, Iterator const& rhs) -> bool {
            return !(lhs == rhs);
        }

      private:
        friend class HashMap;

        Iterator(HashMap* map, size_t index) : m_map(map), m_index(index) {
            skip_invalid();
        }

        auto skip_invalid() -> void {
            if (m_map == nullptr) {
                return;
            }

            size_t const capacity = m_map->capacity();
            while (m_index < capacity &&
                   !hash_map_detail::map_hash_is_valid(m_map->hash_at(m_index))) {
                m_index += 1u;
            }
        }

      private:
        HashMap* m_map = nullptr;
        size_t m_index = 0u;
    };

    class ConstIterator {
      public:
        ConstIterator() = default;

        [[nodiscard]] auto operator*() const -> ConstEntry {
            return ConstEntry{m_map->key_at(m_index), m_map->value_at(m_index)};
        }

        auto operator++() -> ConstIterator& {
            m_index += 1u;
            skip_invalid();
            return *this;
        }

        auto operator++(int) -> ConstIterator {
            ConstIterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] friend auto operator==(ConstIterator const& lhs, ConstIterator const& rhs)
            -> bool {
            return lhs.m_map == rhs.m_map && lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend auto operator!=(ConstIterator const& lhs, ConstIterator const& rhs)
            -> bool {
            return !(lhs == rhs);
        }

      private:
        friend class HashMap;

        ConstIterator(HashMap const* map, size_t index) : m_map(map), m_index(index) {
            skip_invalid();
        }

        auto skip_invalid() -> void {
            if (m_map == nullptr) {
                return;
            }

            size_t const capacity = m_map->capacity();
            while (m_index < capacity &&
                   !hash_map_detail::map_hash_is_valid(m_map->hash_at(m_index))) {
                m_index += 1u;
            }
        }

      private:
        HashMap const* m_map = nullptr;
        size_t m_index = 0u;
    };

    HashMap() = default;
    explicit HashMap(MemoryResource* resource) : m_resource(resource) {}
    HashMap(Hasher hasher, KeyEqual equal, MemoryResource* resource)
        : m_hasher(std::move(hasher)), m_equal(std::move(equal)), m_resource(resource) {}

    [[nodiscard]] auto init(size_t capacity, MemoryResource* resource) -> bool {
        m_data = 0u;
        m_len = 0u;
        DEBUG_ASSERT(resource != nullptr);
        if (resource == nullptr) {
            return false;
        }
        m_resource = resource;

        if (capacity == 0u) {
            return true;
        }

        return reserve(capacity);
    }

    [[nodiscard]] auto copy_from(HashMap const& other, MemoryResource* resource) -> bool {
        if (this == &other) {
            return true;
        }

        m_data = 0u;
        m_len = 0u;
        m_resource = nullptr;
        m_hasher = other.m_hasher;
        m_equal = other.m_equal;

        if (!init(other.m_len, resource)) {
            return false;
        }
        for (ConstEntry entry : other) {
            if (set(*entry.key, *entry.value) == nullptr) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto init(std::initializer_list<Pair> pairs, MemoryResource* resource) -> bool {
        if (!init(pairs.size(), resource)) {
            return false;
        }
        for (Pair const& pair : pairs) {
            if (set(pair.key, pair.value) == nullptr) {
                return false;
            }
        }
        return true;
    }

    auto clear() -> void {
        if (m_data == 0u) {
            return;
        }
        std::memset(hashes(), 0, capacity() * sizeof(hash_map_detail::MapHash));
        m_len = 0u;
    }

    auto reset() -> void {
        clear();
    }

    [[nodiscard]] auto reserve(size_t new_capacity) -> bool {
        size_t const old_capacity = capacity();
        if (old_capacity >= new_capacity) {
            return true;
        }

        if (new_capacity == 0u) {
            return true;
        }

        size_t const log2_new_capacity = hash_map_detail::ceil_log2(new_capacity);
        size_t const log2_min_capacity =
            std::max(log2_new_capacity, hash_map_detail::MAP_MIN_LOG2_CAPACITY);

        uintptr_t resized_data = 0u;
        if (!allocate_data(log2_min_capacity, resized_data)) {
            return false;
        }

        if (m_data != 0u) {
            HashMap resized(m_hasher, m_equal, m_resource);
            resized.m_data = resized_data;

            size_t remaining = m_len;

            for (size_t index = 0u; index < old_capacity && remaining != 0u; ++index) {
                hash_map_detail::MapHash const hash = hash_at(index);
                if (!hash_map_detail::map_hash_is_valid(hash)) {
                    continue;
                }

                Key const* old_key = key_at(index);
                Value const* old_value = value_at(index);
                hash_map_detail::MapHash const new_hash = resized.hash_key(*old_key);
                BASE_UNUSED(resized.insert_hash_with_key(new_hash, old_key, old_value));
                remaining -= 1u;
            }

            m_data = resized.m_data;
        } else {
            m_data = resized_data;
        }

        return true;
    }

    [[nodiscard]] auto set(Key const& key, Value const& value) -> Value* {
        size_t found_index = 0u;
        if (find_index(key, &found_index)) {
            copy_value(value_at(found_index), &value);
            return value_at(found_index);
        }

        if (!check_grow()) {
            return nullptr;
        }

        hash_map_detail::MapHash const hash = hash_key(key);
        Entry const inserted = insert_hash_with_key(hash, &key, &value);
        if (inserted.value != nullptr) {
            m_len += 1u;
        }

        return inserted.value;
    }

    [[nodiscard]] auto get_or_insert(Key const& key, Value const& value = {}) -> InsertResult {
        size_t found_index = 0u;
        if (find_index(key, &found_index)) {
            return InsertResult{key_at(found_index), value_at(found_index), false};
        }

        if (!check_grow()) {
            return InsertResult{};
        }

        hash_map_detail::MapHash const hash = hash_key(key);
        Entry const inserted = insert_hash_with_key(hash, &key, &value);
        if (inserted.value != nullptr) {
            m_len += 1u;
        }

        return InsertResult{inserted.key, inserted.value, inserted.value != nullptr};
    }

    [[nodiscard]] auto erase(Key const& key, Key* old_key = nullptr, Value* old_value = nullptr)
        -> bool {
        size_t index = 0u;
        if (!find_index(key, &index)) {
            return false;
        }

        if (old_key != nullptr) {
            copy_key(old_key, key_at(index));
        }
        if (old_value != nullptr) {
            copy_value(old_value, value_at(index));
        }

        hash_map_detail::MapHash* hash_values = hashes();
        hash_values[index] |= hash_map_detail::TOMBSTONE_MASK;
        m_len -= 1u;

        size_t const mask = capacity() - 1u;
        size_t const next_index = (index + 1u) & mask;
        hash_map_detail::MapHash const next_hash = hash_values[next_index];
        if (hash_map_detail::map_hash_is_empty(next_hash) ||
            probe_distance(next_hash, next_index) == 0u) {
            hash_values[index] = 0u;
        } else {
            hash_values[index] |= hash_map_detail::TOMBSTONE_MASK;
        }

        return true;
    }

    [[nodiscard]] auto get(Key const& key) -> Value* {
        size_t index = 0u;
        return find_index(key, &index) ? value_at(index) : nullptr;
    }

    [[nodiscard]] auto get(Key const& key) const -> Value const* {
        size_t index = 0u;
        return find_index(key, &index) ? value_at(index) : nullptr;
    }

    [[nodiscard]] auto find_entry(Key const& key) -> Entry {
        size_t index = 0u;
        if (!find_index(key, &index)) {
            return Entry{};
        }
        return Entry{key_at(index), value_at(index)};
    }

    [[nodiscard]] auto find_entry(Key const& key) const -> ConstEntry {
        size_t index = 0u;
        if (!find_index(key, &index)) {
            return ConstEntry{nullptr, nullptr};
        }
        return ConstEntry{key_at(index), value_at(index)};
    }

    [[nodiscard]] auto contains(Key const& key) const -> bool {
        size_t index = 0u;
        return find_index(key, &index);
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto len() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto capacity() const -> size_t {
        return m_data == 0u ? 0u : size_t{1u} << log2_cap();
    }

    [[nodiscard]] auto empty() const -> bool {
        return m_len == 0u;
    }

    [[nodiscard]] auto resource() -> MemoryResource* {
        return m_resource;
    }

    [[nodiscard]] auto begin() -> Iterator {
        return Iterator(this, 0u);
    }

    [[nodiscard]] auto end() -> Iterator {
        return Iterator(this, capacity());
    }

    [[nodiscard]] auto begin() const -> ConstIterator {
        return ConstIterator(this, 0u);
    }

    [[nodiscard]] auto end() const -> ConstIterator {
        return ConstIterator(this, capacity());
    }

    [[nodiscard]] auto cbegin() const -> ConstIterator {
        return begin();
    }

    [[nodiscard]] auto cend() const -> ConstIterator {
        return end();
    }

  private:
    static_assert(!std::is_const_v<Key>, "HashMap key type must not be const");
    static_assert(!std::is_const_v<Value>, "HashMap value type must not be const");
    static_assert(
        std::is_trivially_copyable_v<Key>, "HashMap stores keys with Odin-style byte copies"
    );
    static_assert(
        std::is_trivially_copyable_v<Value>, "HashMap stores values with Odin-style byte copies"
    );
    static_assert(
        hash_map_detail::HAS_ODIN_CELL_ALIGNMENT<Key>,
        "HashMap key alignment must fit within a map cache line"
    );
    static_assert(
        hash_map_detail::HAS_ODIN_CELL_ALIGNMENT<Value>,
        "HashMap value alignment must fit within a map cache line"
    );
    static_assert(
        std::is_trivially_copyable_v<Hasher>, "HashMap hasher type must be trivially copyable"
    );
    static_assert(
        std::is_trivially_copyable_v<KeyEqual>, "HashMap equality type must be trivially copyable"
    );

    [[nodiscard]] auto log2_cap() const -> size_t {
        return static_cast<size_t>(m_data & hash_map_detail::DATA_TAG_MASK);
    }

    [[nodiscard]] auto data_address() const -> uintptr_t {
        return m_data & ~hash_map_detail::DATA_TAG_MASK;
    }

    [[nodiscard]] auto raw_data() -> std::byte* {
        return std::bit_cast<std::byte*>(data_address());
    }

    [[nodiscard]] auto raw_data() const -> std::byte const* {
        return std::bit_cast<std::byte const*>(data_address());
    }

    [[nodiscard]] auto hash_seed() const -> uintptr_t {
        return hash_map_detail::splitmix(data_address());
    }

    [[nodiscard]] auto resize_threshold() const -> size_t {
        return ((size_t{1u} << log2_cap()) * hash_map_detail::MAP_LOAD_FACTOR) / 100u;
    }

    [[nodiscard]] auto section_offsets(size_t map_capacity, size_t& values, size_t& hashes) const
        -> bool {
        size_t key_size = 0u;
        size_t value_size = 0u;
        if (!hash_map_detail::cell_storage_size<Key>(map_capacity, &key_size) ||
            !hash_map_detail::cell_storage_size<Value>(map_capacity, &value_size)) {
            return false;
        }
        values = key_size;
        return !hash_map_detail::add_overflows(key_size, value_size, hashes);
    }

    [[nodiscard]] auto total_allocation_size(size_t map_capacity, size_t& out) const -> bool {
        size_t key_size = 0u;
        size_t value_size = 0u;
        size_t hash_size = 0u;
        size_t key_value_size = 0u;

        if (!hash_map_detail::cell_storage_size<Key>(map_capacity, &key_size) ||
            !hash_map_detail::cell_storage_size<Value>(map_capacity, &value_size) ||
            !hash_map_detail::cell_storage_size<hash_map_detail::MapHash>(
                map_capacity, &hash_size
            ) ||
            hash_map_detail::add_overflows(key_size, value_size, key_value_size)) {
            return false;
        }

        return !hash_map_detail::add_overflows(key_value_size, hash_size, out);
    }

    [[nodiscard]] auto values_base() -> std::byte* {
        size_t values_offset = 0u;
        size_t hashes_offset = 0u;
        BASE_UNUSED(section_offsets(capacity(), values_offset, hashes_offset));
        return raw_data() + values_offset;
    }

    [[nodiscard]] auto values_base() const -> std::byte const* {
        size_t values_offset = 0u;
        size_t hashes_offset = 0u;
        BASE_UNUSED(section_offsets(capacity(), values_offset, hashes_offset));
        return raw_data() + values_offset;
    }

    [[nodiscard]] auto hashes_base() -> std::byte* {
        size_t values_offset = 0u;
        size_t hashes_offset = 0u;
        BASE_UNUSED(section_offsets(capacity(), values_offset, hashes_offset));
        return raw_data() + hashes_offset;
    }

    [[nodiscard]] auto hashes_base() const -> std::byte const* {
        size_t values_offset = 0u;
        size_t hashes_offset = 0u;
        BASE_UNUSED(section_offsets(capacity(), values_offset, hashes_offset));
        return raw_data() + hashes_offset;
    }

    [[nodiscard]] auto key_at(size_t index) -> Key* {
        return hash_map_detail::cell_index<Key>(raw_data(), index);
    }

    [[nodiscard]] auto key_at(size_t index) const -> Key const* {
        return hash_map_detail::cell_index<Key>(raw_data(), index);
    }

    [[nodiscard]] auto value_at(size_t index) -> Value* {
        return hash_map_detail::cell_index<Value>(values_base(), index);
    }

    [[nodiscard]] auto value_at(size_t index) const -> Value const* {
        return hash_map_detail::cell_index<Value>(values_base(), index);
    }

    [[nodiscard]] auto hashes() -> hash_map_detail::MapHash* {
        return reinterpret_cast<hash_map_detail::MapHash*>(hashes_base());
    }

    [[nodiscard]] auto hashes() const -> hash_map_detail::MapHash const* {
        return reinterpret_cast<hash_map_detail::MapHash const*>(hashes_base());
    }

    [[nodiscard]] auto hash_at(size_t index) const -> hash_map_detail::MapHash {
        return hashes()[index];
    }

    [[nodiscard]] auto hash_key(Key const& key) const -> hash_map_detail::MapHash {
        return hash_map_detail::sanitize_hash(m_hasher(key, hash_seed()));
    }

    [[nodiscard]] auto desired_position(hash_map_detail::MapHash hash) const -> size_t {
        return static_cast<size_t>(hash) & (capacity() - 1u);
    }

    [[nodiscard]] auto probe_distance(hash_map_detail::MapHash hash, size_t slot) const -> size_t {
        return (slot - static_cast<size_t>(hash)) & (capacity() - 1u);
    }

    auto copy_key(Key* target, Key const* source) -> void {
        std::memcpy(target, source, sizeof(Key));
    }

    auto copy_value(Value* target, Value const* source) -> void {
        std::memcpy(target, source, sizeof(Value));
    }

    auto copy_key_bytes(void* target, void const* source) -> void {
        std::memcpy(target, source, sizeof(Key));
    }

    auto copy_value_bytes(void* target, void const* source) -> void {
        std::memcpy(target, source, sizeof(Value));
    }

    [[nodiscard]] auto allocate_data(size_t log2_capacity, uintptr_t& out_data) -> bool {
        size_t constexpr SIZE_BITS = sizeof(size_t) * 8u;
        DEBUG_ASSERT(log2_capacity < SIZE_BITS);

        size_t const map_capacity = size_t{1u} << log2_capacity;
        size_t allocation_size = 0u;
        if (!total_allocation_size(map_capacity, allocation_size)) {
            return false;
        }

        DEBUG_ASSERT(m_resource != nullptr);
        if (m_resource == nullptr) {
            return false;
        }

        void* const data =
            m_resource->allocate(allocation_size, hash_map_detail::MAP_CACHE_LINE_SIZE);
        uintptr_t const data_address = reinterpret_cast<uintptr_t>(data);

        if (data == nullptr || (data_address & hash_map_detail::DATA_TAG_MASK) != 0u) {
            if (data != nullptr) {
                m_resource->deallocate(data, allocation_size, hash_map_detail::MAP_CACHE_LINE_SIZE);
            }
            DEBUG_ASSERT_MSG(false, "HashMap allocation was not cache-line aligned");
            return false;
        }

        out_data = data_address | log2_capacity;

        uintptr_t const old_data = m_data;
        m_data = out_data;
        std::memset(hashes(), 0, map_capacity * sizeof(hash_map_detail::MapHash));
        m_data = old_data;
        return true;
    }

    [[nodiscard]] auto check_grow() -> bool {
        if (m_data == 0u) {
            return grow();
        }
        if (m_len >= resize_threshold()) {
            return grow();
        }
        return true;
    }

    [[nodiscard]] auto grow() -> bool {
        size_t const current_log2_capacity = m_data == 0u ? 0u : log2_cap();
        size_t const next_log2_capacity =
            std::max(current_log2_capacity + 1u, hash_map_detail::MAP_MIN_LOG2_CAPACITY);
        size_t constexpr SIZE_BITS = sizeof(size_t) * 8u;
        if (next_log2_capacity >= SIZE_BITS) {
            return false;
        }
        return reserve(size_t{1u} << next_log2_capacity);
    }

    [[nodiscard]] auto find_index(Key const& key, size_t* out_index) const -> bool {
        if (m_len == 0u || m_data == 0u) {
            return false;
        }

        hash_map_detail::MapHash const hash = hash_key(key);
        size_t position = desired_position(hash);
        size_t distance = 0u;
        size_t const mask = capacity() - 1u;
        hash_map_detail::MapHash const* hash_values = hashes();

        for (;;) {
            hash_map_detail::MapHash const element_hash = hash_values[position];
            if (hash_map_detail::map_hash_is_empty(element_hash)) {
                return false;
            }

            if (distance > probe_distance(element_hash, position)) {
                return false;
            }

            if (element_hash == hash && m_equal(key, *key_at(position))) {
                *out_index = position;
                return true;
            }

            position = (position + 1u) & mask;
            distance += 1u;
        }
    }

    [[nodiscard]] auto insert_hash_with_key(
        hash_map_detail::MapHash hash, Key const* inserted_key, Value const* inserted_value
    ) -> Entry {
        hash_map_detail::MapHash moving_hash = hash;
        size_t position = desired_position(hash);
        size_t distance = 0u;
        size_t const mask = capacity() - 1u;

        hash_map_detail::MapHash* hash_values = hashes();
        alignas(Key) std::byte key_storage[sizeof(Key)] = {};
        alignas(Value) std::byte value_storage[sizeof(Value)] = {};
        alignas(Key) std::byte temp_key_storage[sizeof(Key)] = {};
        alignas(Value) std::byte temp_value_storage[sizeof(Value)] = {};
        copy_key_bytes(key_storage, inserted_key);
        copy_value_bytes(value_storage, inserted_value);

        Entry result = {nullptr, nullptr};

        for (;;) {
            DEBUG_ASSERT(distance <= mask);

            hash_map_detail::MapHash const element_hash = hash_values[position];

            if (hash_map_detail::map_hash_is_empty(element_hash)) {
                Key* key_destination = key_at(position);
                Value* value_destination = value_at(position);
                copy_key_bytes(key_destination, key_storage);
                copy_value_bytes(value_destination, value_storage);
                hash_values[position] = moving_hash;

                if (result.value == nullptr) {
                    result = Entry{key_destination, value_destination};
                }
                return result;
            }

            if (hash_map_detail::map_hash_is_deleted(element_hash)) {
                break;
            }

            size_t const element_probe_distance = probe_distance(element_hash, position);
            if (distance > element_probe_distance) {
                Key* key_position = key_at(position);
                Value* value_position = value_at(position);

                if (result.value == nullptr) {
                    result = Entry{key_position, value_position};
                }

                copy_key_bytes(temp_key_storage, key_storage);
                copy_key_bytes(key_storage, key_position);
                copy_key_bytes(key_position, temp_key_storage);

                copy_value_bytes(temp_value_storage, value_storage);
                copy_value_bytes(value_storage, value_position);
                copy_value_bytes(value_position, temp_value_storage);

                hash_map_detail::MapHash const temp_hash = moving_hash;
                moving_hash = hash_values[position];
                hash_values[position] = temp_hash;

                distance = element_probe_distance;
            }

            position = (position + 1u) & mask;
            distance += 1u;
        }

        hash_values[position] = 0u;
        size_t look_ahead = 1u;

        for (;;) {
            size_t look_ahead_position = (position + look_ahead) & mask;
            hash_map_detail::MapHash element_hash = hash_values[look_ahead_position];

            if (hash_map_detail::map_hash_is_deleted(element_hash)) {
                look_ahead += 1u;
                hash_values[look_ahead_position] = 0u;
                continue;
            }

            Key* key_destination = key_at(position);
            Value* value_destination = value_at(position);

            if (hash_map_detail::map_hash_is_empty(element_hash)) {
                copy_key_bytes(key_destination, key_storage);
                copy_value_bytes(value_destination, value_storage);
                hash_values[position] = moving_hash;

                if (result.value == nullptr) {
                    result = Entry{key_destination, value_destination};
                }
                return result;
            }

            Key* key_source = key_at(look_ahead_position);
            Value* value_source = value_at(look_ahead_position);
            size_t probe = probe_distance(element_hash, look_ahead_position);

            if (probe < look_ahead) {
                if (result.value == nullptr) {
                    result = Entry{key_destination, value_destination};
                }

                copy_key_bytes(key_destination, key_storage);
                copy_value_bytes(value_destination, value_storage);
                hash_values[position] = moving_hash;

                position = (look_ahead_position - probe) & mask;
                look_ahead -= probe;

                while (probe != 0u) {
                    key_destination = key_at(position);
                    value_destination = value_at(position);

                    copy_key_bytes(key_destination, key_source);
                    copy_value_bytes(value_destination, value_source);
                    hash_values[position] = element_hash;
                    hash_values[look_ahead_position] = 0u;

                    position = (position + 1u) & mask;
                    look_ahead_position = (look_ahead_position + 1u) & mask;
                    look_ahead = (look_ahead_position - position) & mask;
                    element_hash = hash_values[look_ahead_position];
                    if (hash_map_detail::map_hash_is_empty(element_hash)) {
                        return result;
                    }

                    probe = probe_distance(element_hash, look_ahead_position);
                    if (probe == 0u) {
                        return result;
                    }

                    if (probe < look_ahead) {
                        position = (look_ahead_position - probe) & mask;
                    }

                    key_source = key_at(look_ahead_position);
                    value_source = value_at(look_ahead_position);
                }

                return result;
            }

            if (distance < probe - look_ahead) {
                copy_key_bytes(key_destination, key_source);
                copy_value_bytes(value_destination, value_source);
                hash_values[position] = element_hash;
                hash_values[look_ahead_position] = 0u;
            } else {
                if (result.value == nullptr) {
                    result = Entry{key_destination, value_destination};
                }

                copy_key_bytes(key_destination, key_storage);
                copy_value_bytes(value_destination, value_storage);
                hash_values[position] = moving_hash;

                copy_key_bytes(key_storage, key_source);
                copy_value_bytes(value_storage, value_source);
                moving_hash = hash_values[look_ahead_position];
                hash_values[look_ahead_position] = 0u;
                distance = probe - look_ahead;
            }

            position = (position + 1u) & mask;
            distance += 1u;
        }
    }

    uintptr_t m_data = 0u;
    size_t m_len = 0u;
    MemoryResource* m_resource = nullptr;
    Hasher m_hasher = {};
    KeyEqual m_equal = {};
};
