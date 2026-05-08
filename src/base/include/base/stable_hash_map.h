#pragma once

#include <algorithm>
#include <base/assert.h>
#include <base/hash_map.h>
#include <base/memory.h>
#include <base/xar.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace stable_hash_map_detail {
    inline constexpr size_t MAP_LOAD_FACTOR = 75u;
    inline constexpr size_t MAP_MIN_LOG2_CAPACITY = 3u;
    inline constexpr size_t ENTRY_CHUNK_SHIFT = 4u;
    inline constexpr uintptr_t TOMBSTONE_MASK = uintptr_t{1u} << (sizeof(uintptr_t) * 8u - 1u);
    inline constexpr uintptr_t HASH_MASK = TOMBSTONE_MASK - 1u;
    inline constexpr uintptr_t HASH_SEED = 0xcbf29ce484222325ull;
    inline constexpr size_t NPOS = std::numeric_limits<size_t>::max();

    using MapHash = uintptr_t;

    struct Bucket {
        MapHash hash = 0u;
        size_t entry_index = 0u;
    };

    template <typename Key, typename Value> struct StoredEntry {
        Key key = {};
        Value value = {};
        bool occupied = false;
    };

    [[nodiscard]] constexpr auto map_hash_is_empty(MapHash hash) -> bool {
        return hash == 0u;
    }

    [[nodiscard]] constexpr auto map_hash_is_deleted(MapHash hash) -> bool {
        return hash != 0u && (hash & TOMBSTONE_MASK) != 0u;
    }

    [[nodiscard]] constexpr auto map_hash_is_valid(MapHash hash) -> bool {
        return hash != 0u && (hash & TOMBSTONE_MASK) == 0u;
    }

    [[nodiscard]] constexpr auto sanitize_hash(uintptr_t hash) -> MapHash {
        MapHash const sanitized = static_cast<MapHash>(hash) & HASH_MASK;
        return sanitized != 0u ? sanitized : 1u;
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
} // namespace stable_hash_map_detail

template <
    typename Key,
    typename Value,
    typename Hasher = HashMapDefaultHasher<Key>,
    typename KeyEqual = HashMapDefaultEqual<Key>>
class StableHashMap final {
    using Bucket = stable_hash_map_detail::Bucket;
    using StoredEntry = stable_hash_map_detail::StoredEntry<Key, Value>;

  public:
    struct Entry {
        Key* key = nullptr;
        Value* value = nullptr;
    };

    struct ConstEntry {
        Key const* key = nullptr;
        Value const* value = nullptr;
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
            StoredEntry* const entry = m_map->entry_at(m_index);
            return Entry{&entry->key, &entry->value};
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
        friend class StableHashMap;

        Iterator(StableHashMap* map, size_t index) : m_map(map), m_index(index) {
            skip_invalid();
        }

        auto skip_invalid() -> void {
            if (m_map == nullptr) {
                return;
            }
            while (m_index < m_map->entry_count() && !m_map->entry_at(m_index)->occupied) {
                m_index += 1u;
            }
        }

      private:
        StableHashMap* m_map = nullptr;
        size_t m_index = 0u;
    };

    class ConstIterator {
      public:
        ConstIterator() = default;

        [[nodiscard]] auto operator*() const -> ConstEntry {
            StoredEntry const* const entry = m_map->entry_at(m_index);
            return ConstEntry{&entry->key, &entry->value};
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
        friend class StableHashMap;

        ConstIterator(StableHashMap const* map, size_t index) : m_map(map), m_index(index) {
            skip_invalid();
        }

        auto skip_invalid() -> void {
            if (m_map == nullptr) {
                return;
            }
            while (m_index < m_map->entry_count() && !m_map->entry_at(m_index)->occupied) {
                m_index += 1u;
            }
        }

      private:
        StableHashMap const* m_map = nullptr;
        size_t m_index = 0u;
    };

    StableHashMap() = default;
    explicit StableHashMap(MemoryResource* resource) : m_resource(resource) {}
    StableHashMap(Hasher hasher, KeyEqual equal, MemoryResource* resource)
        : m_hasher(std::move(hasher)), m_equal(std::move(equal)), m_resource(resource) {}

    [[nodiscard]] auto init(size_t capacity, MemoryResource* resource) -> bool {
        m_entries = {};
        m_buckets = nullptr;
        m_bucket_capacity = 0u;
        m_len = 0u;
        m_used_buckets = 0u;
        DEBUG_ASSERT(resource != nullptr);
        if (resource == nullptr) {
            return false;
        }
        m_resource = resource;

        if (!m_entries.init(m_resource)) {
            return false;
        }
        if (capacity == 0u) {
            return true;
        }
        return reserve(capacity);
    }

    [[nodiscard]] auto copy_from(StableHashMap const& other, MemoryResource* resource) -> bool {
        if (this == &other) {
            return true;
        }

        m_entries = {};
        m_buckets = nullptr;
        m_bucket_capacity = 0u;
        m_len = 0u;
        m_used_buckets = 0u;
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
        if (m_buckets != nullptr) {
            std::memset(m_buckets, 0, m_bucket_capacity * sizeof(Bucket));
        }
        m_entries.clear();
        m_len = 0u;
        m_used_buckets = 0u;
    }

    auto reset() -> void {
        clear();
    }

    [[nodiscard]] auto reserve(size_t new_capacity) -> bool {
        if (m_bucket_capacity >= new_capacity) {
            return true;
        }
        if (new_capacity == 0u) {
            return true;
        }

        size_t constexpr SIZE_BITS = sizeof(size_t) * 8u;
        size_t const log2_new_capacity = stable_hash_map_detail::ceil_log2(new_capacity);
        size_t const log2_capacity =
            std::max(log2_new_capacity, stable_hash_map_detail::MAP_MIN_LOG2_CAPACITY);
        if (log2_capacity >= SIZE_BITS) {
            return false;
        }
        return rehash(size_t{1u} << log2_capacity);
    }

    [[nodiscard]] auto set(Key const& key, Value const& value) -> Value* {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        if (find_bucket_index(key, &bucket_index)) {
            StoredEntry* const entry = stored_entry_from_bucket(bucket_index);
            copy_value(&entry->value, &value);
            return &entry->value;
        }

        InsertResult const result = insert_new(key, value);
        return result.value;
    }

    [[nodiscard]] auto get_or_insert(Key const& key, Value const& value = {}) -> InsertResult {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        if (find_bucket_index(key, &bucket_index)) {
            StoredEntry* const entry = stored_entry_from_bucket(bucket_index);
            return InsertResult{&entry->key, &entry->value, false};
        }

        return insert_new(key, value);
    }

    [[nodiscard]] auto erase(Key const& key, Key* old_key = nullptr, Value* old_value = nullptr)
        -> bool {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        if (!find_bucket_index(key, &bucket_index)) {
            return false;
        }

        StoredEntry* const entry = stored_entry_from_bucket(bucket_index);
        if (old_key != nullptr) {
            copy_key(old_key, &entry->key);
        }
        if (old_value != nullptr) {
            copy_value(old_value, &entry->value);
        }

        entry->occupied = false;
        m_buckets[bucket_index].hash |= stable_hash_map_detail::TOMBSTONE_MASK;
        m_len -= 1u;
        return true;
    }

    [[nodiscard]] auto get(Key const& key) -> Value* {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        return find_bucket_index(key, &bucket_index)
                   ? &stored_entry_from_bucket(bucket_index)->value
                   : nullptr;
    }

    [[nodiscard]] auto get(Key const& key) const -> Value const* {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        return find_bucket_index(key, &bucket_index)
                   ? &stored_entry_from_bucket(bucket_index)->value
                   : nullptr;
    }

    [[nodiscard]] auto find_entry(Key const& key) -> Entry {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        if (!find_bucket_index(key, &bucket_index)) {
            return {};
        }
        StoredEntry* const entry = stored_entry_from_bucket(bucket_index);
        return Entry{&entry->key, &entry->value};
    }

    [[nodiscard]] auto find_entry(Key const& key) const -> ConstEntry {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        if (!find_bucket_index(key, &bucket_index)) {
            return {};
        }
        StoredEntry const* const entry = stored_entry_from_bucket(bucket_index);
        return ConstEntry{&entry->key, &entry->value};
    }

    [[nodiscard]] auto contains(Key const& key) const -> bool {
        size_t bucket_index = stable_hash_map_detail::NPOS;
        return find_bucket_index(key, &bucket_index);
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto len() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto capacity() const -> size_t {
        return m_bucket_capacity;
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
        return Iterator(this, entry_count());
    }

    [[nodiscard]] auto begin() const -> ConstIterator {
        return ConstIterator(this, 0u);
    }

    [[nodiscard]] auto end() const -> ConstIterator {
        return ConstIterator(this, entry_count());
    }

    [[nodiscard]] auto cbegin() const -> ConstIterator {
        return begin();
    }

    [[nodiscard]] auto cend() const -> ConstIterator {
        return end();
    }

  private:
    static_assert(!std::is_const_v<Key>, "StableHashMap key type must not be const");
    static_assert(!std::is_const_v<Value>, "StableHashMap value type must not be const");
    static_assert(
        std::is_trivially_copyable_v<Key>, "StableHashMap stores keys with Odin-style byte copies"
    );
    static_assert(
        std::is_trivially_copyable_v<Value>,
        "StableHashMap stores values with Odin-style byte copies"
    );
    static_assert(
        std::is_trivially_copyable_v<Hasher>, "StableHashMap hasher type must be trivially copyable"
    );
    static_assert(
        std::is_trivially_copyable_v<KeyEqual>,
        "StableHashMap equality type must be trivially copyable"
    );

    [[nodiscard]] auto entry_count() const -> size_t {
        return m_entries.len();
    }

    [[nodiscard]] auto entry_at(size_t index) -> StoredEntry* {
        return m_entries.get_ptr(index);
    }

    [[nodiscard]] auto entry_at(size_t index) const -> StoredEntry const* {
        return m_entries.get_ptr(index);
    }

    [[nodiscard]] auto stored_entry_from_bucket(size_t bucket_index) -> StoredEntry* {
        return entry_at(m_buckets[bucket_index].entry_index);
    }

    [[nodiscard]] auto stored_entry_from_bucket(size_t bucket_index) const -> StoredEntry const* {
        return entry_at(m_buckets[bucket_index].entry_index);
    }

    [[nodiscard]] auto resize_threshold() const -> size_t {
        return (m_bucket_capacity * stable_hash_map_detail::MAP_LOAD_FACTOR) / 100u;
    }

    [[nodiscard]] auto hash_key(Key const& key) const -> stable_hash_map_detail::MapHash {
        return stable_hash_map_detail::sanitize_hash(
            m_hasher(key, stable_hash_map_detail::HASH_SEED)
        );
    }

    [[nodiscard]] static auto insert_bucket_index(
        Bucket const* buckets, size_t bucket_capacity, stable_hash_map_detail::MapHash hash
    ) -> size_t {
        DEBUG_ASSERT(bucket_capacity > 0u);

        size_t const mask = bucket_capacity - 1u;
        size_t slot = static_cast<size_t>(hash) & mask;
        size_t first_deleted = stable_hash_map_detail::NPOS;

        for (size_t probe = 0u; probe < bucket_capacity; ++probe) {
            stable_hash_map_detail::MapHash const bucket_hash = buckets[slot].hash;
            if (stable_hash_map_detail::map_hash_is_empty(bucket_hash)) {
                return first_deleted != stable_hash_map_detail::NPOS ? first_deleted : slot;
            }
            if (stable_hash_map_detail::map_hash_is_deleted(bucket_hash) &&
                first_deleted == stable_hash_map_detail::NPOS) {
                first_deleted = slot;
            }
            slot = (slot + 1u) & mask;
        }

        return first_deleted;
    }

    [[nodiscard]] auto find_bucket_index(Key const& key, size_t* out_index) const -> bool {
        if (m_len == 0u || m_buckets == nullptr) {
            return false;
        }

        stable_hash_map_detail::MapHash const hash = hash_key(key);
        size_t const mask = m_bucket_capacity - 1u;
        size_t slot = static_cast<size_t>(hash) & mask;

        for (size_t probe = 0u; probe < m_bucket_capacity; ++probe) {
            stable_hash_map_detail::MapHash const bucket_hash = m_buckets[slot].hash;
            if (stable_hash_map_detail::map_hash_is_empty(bucket_hash)) {
                return false;
            }

            if (bucket_hash == hash) {
                StoredEntry const* const entry = stored_entry_from_bucket(slot);
                if (entry->occupied && m_equal(key, entry->key)) {
                    *out_index = slot;
                    return true;
                }
            }

            slot = (slot + 1u) & mask;
        }

        return false;
    }

    [[nodiscard]] auto insert_new(Key const& key, Value const& value) -> InsertResult {
        if (!check_grow()) {
            return {};
        }

        StoredEntry stored = {};
        copy_key(&stored.key, &key);
        copy_value(&stored.value, &value);
        stored.occupied = true;

        size_t const entry_index = m_entries.len();
        StoredEntry* const entry = m_entries.push_back_and_get_ptr(stored);
        if (entry == nullptr) {
            return {};
        }

        stable_hash_map_detail::MapHash const hash = hash_key(key);
        size_t const bucket_index = insert_bucket_index(m_buckets, m_bucket_capacity, hash);
        if (bucket_index == stable_hash_map_detail::NPOS) {
            BASE_UNUSED(m_entries.pop());
            return {};
        }

        bool const used_empty =
            stable_hash_map_detail::map_hash_is_empty(m_buckets[bucket_index].hash);
        m_buckets[bucket_index].hash = hash;
        m_buckets[bucket_index].entry_index = entry_index;
        m_len += 1u;
        if (used_empty) {
            m_used_buckets += 1u;
        }

        return InsertResult{&entry->key, &entry->value, true};
    }

    [[nodiscard]] auto check_grow() -> bool {
        if (m_bucket_capacity == 0u) {
            return grow();
        }
        if (m_used_buckets < resize_threshold()) {
            return true;
        }
        if (m_len < m_used_buckets) {
            return rehash(m_bucket_capacity);
        }
        return grow();
    }

    [[nodiscard]] auto grow() -> bool {
        size_t constexpr SIZE_BITS = sizeof(size_t) * 8u;
        size_t const current_log2_capacity =
            m_bucket_capacity == 0u ? 0u : stable_hash_map_detail::ceil_log2(m_bucket_capacity);
        size_t const next_log2_capacity =
            std::max(current_log2_capacity + 1u, stable_hash_map_detail::MAP_MIN_LOG2_CAPACITY);
        if (next_log2_capacity >= SIZE_BITS) {
            return false;
        }
        return rehash(size_t{1u} << next_log2_capacity);
    }

    [[nodiscard]] auto rehash(size_t new_capacity) -> bool {
        DEBUG_ASSERT(new_capacity > 0u);

        Bucket* new_buckets = nullptr;
        if (!allocate_buckets(new_capacity, &new_buckets)) {
            return false;
        }

        size_t new_used_buckets = 0u;
        for (size_t index = 0u; index < m_entries.len(); ++index) {
            StoredEntry const* const entry = m_entries.get_ptr(index);
            if (!entry->occupied) {
                continue;
            }

            stable_hash_map_detail::MapHash const hash = hash_key(entry->key);
            size_t const bucket_index = insert_bucket_index(new_buckets, new_capacity, hash);
            DEBUG_ASSERT(bucket_index != stable_hash_map_detail::NPOS);

            new_buckets[bucket_index].hash = hash;
            new_buckets[bucket_index].entry_index = index;
            new_used_buckets += 1u;
        }

        m_buckets = new_buckets;
        m_bucket_capacity = new_capacity;
        m_used_buckets = new_used_buckets;
        return true;
    }

    [[nodiscard]] auto allocate_buckets(size_t bucket_capacity, Bucket** out_buckets) -> bool {
        size_t allocation_size = 0u;
        if (stable_hash_map_detail::mul_overflows(
                sizeof(Bucket), bucket_capacity, allocation_size
            )) {
            return false;
        }

        DEBUG_ASSERT(m_resource != nullptr);
        if (m_resource == nullptr) {
            return false;
        }
        void* const memory = m_resource->allocate(allocation_size, alignof(Bucket));
        if (memory == nullptr) {
            return false;
        }

        Bucket* const buckets = static_cast<Bucket*>(memory);
        std::memset(buckets, 0, allocation_size);
        *out_buckets = buckets;
        return true;
    }

    auto copy_key(Key* target, Key const* source) -> void {
        std::memcpy(target, source, sizeof(Key));
    }

    auto copy_value(Value* target, Value const* source) -> void {
        std::memcpy(target, source, sizeof(Value));
    }

    XarArray<StoredEntry, stable_hash_map_detail::ENTRY_CHUNK_SHIFT> m_entries;
    Bucket* m_buckets = nullptr;
    size_t m_bucket_capacity = 0u;
    size_t m_len = 0u;
    size_t m_used_buckets = 0u;
    MemoryResource* m_resource = nullptr;
    Hasher m_hasher = {};
    KeyEqual m_equal = {};
};
