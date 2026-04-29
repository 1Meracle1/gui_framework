#pragma once

#include <array>
#include <base/assert.h>
#include <base/memory.h>
#include <bit>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory_resource>
#include <span>
#include <type_traits>
#include <utility>

inline constexpr size_t XAR_NPOS = std::numeric_limits<size_t>::max();

namespace xar_detail {
    inline constexpr size_t PLATFORM_BITS = sizeof(size_t) * 8u;
    inline constexpr size_t MAX_SHIFT = PLATFORM_BITS / 2u;

    [[nodiscard]] constexpr auto is_valid_shift(size_t shift) -> bool {
        return shift > 0u && shift <= MAX_SHIFT;
    }

    template <size_t SHIFT> inline constexpr size_t CHUNK_COUNT = PLATFORM_BITS - SHIFT;

    template <size_t SHIFT> inline constexpr size_t FIRST_CHUNK_CAPACITY = size_t{1u} << SHIFT;

    template <size_t SHIFT>
    inline constexpr size_t MAX_CAPACITY = size_t{1u} << (SHIFT + CHUNK_COUNT<SHIFT> - 1u);

    [[nodiscard]] inline auto default_memory_resource() -> MemoryResource* {
        return std::pmr::get_default_resource();
    }

    [[nodiscard]] constexpr auto mul_overflows(size_t lhs, size_t rhs, size_t& out) -> bool {
        if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs) {
            return true;
        }
        out = lhs * rhs;
        return false;
    }

    template <size_t SHIFT>
    [[nodiscard]] constexpr auto chunk_capacity(size_t chunk_index) -> size_t {
        if (chunk_index == 0u) {
            return FIRST_CHUNK_CAPACITY<SHIFT>;
        }
        return size_t{1u} << (SHIFT + chunk_index - 1u);
    }

    template <size_t SHIFT>
    [[nodiscard]] constexpr auto capacity_after_chunk(size_t chunk_index) -> size_t {
        if (chunk_index == 0u) {
            return FIRST_CHUNK_CAPACITY<SHIFT>;
        }
        return size_t{1u} << (SHIFT + chunk_index);
    }

    struct Meta {
        size_t chunk_index = 0u;
        size_t element_index = 0u;
        size_t chunk_capacity = 0u;
    };

    template <size_t SHIFT> [[nodiscard]] constexpr auto meta_get(size_t index) -> Meta {
        Meta meta = {};
        meta.element_index = index;
        meta.chunk_capacity = FIRST_CHUNK_CAPACITY<SHIFT>;

        size_t const index_shift = index >> SHIFT;
        if (index_shift > 0u) {
            size_t const chunk_msb = std::bit_width(index_shift) - 1u;
            meta.chunk_index = chunk_msb + 1u;
            meta.chunk_capacity = size_t{1u} << (chunk_msb + SHIFT);
            meta.element_index -= meta.chunk_capacity;
        }

        return meta;
    }

    template <typename T> auto copy_value(T* target, T const* source) -> void {
        std::memcpy(target, source, sizeof(T));
    }

} // namespace xar_detail

inline constexpr size_t XAR_MAX_SHIFT = xar_detail::MAX_SHIFT;

template <typename T> struct XarResult {
    T value = {};
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T> struct XarPtrResult {
    T* ptr = nullptr;
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

struct XarIndexResult {
    size_t index = XAR_NPOS;
    bool found = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return found;
    }
};

template <typename T> struct XarPushResult {
    T* ptr = nullptr;
    size_t index = XAR_NPOS;
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T, size_t SHIFT> class XarArray final {
  public:
    static_assert(!std::is_const_v<T>, "XarArray value type must not be const");
    static_assert(std::is_trivially_copyable_v<T>,
                  "XarArray stores values with Odin-style byte copies");
    static_assert(xar_detail::is_valid_shift(SHIFT),
                  "XarArray SHIFT must be in range 1..XAR_MAX_SHIFT");

    using Value = T;

    static constexpr size_t CHUNK_COUNT = xar_detail::CHUNK_COUNT<SHIFT>;
    static constexpr size_t FIRST_CHUNK_CAPACITY = xar_detail::FIRST_CHUNK_CAPACITY<SHIFT>;
    static constexpr size_t MAX_CAPACITY = xar_detail::MAX_CAPACITY<SHIFT>;

    class Iterator {
      public:
        Iterator() = default;

        [[nodiscard]] auto operator*() const -> T& {
            return *m_array->get_ptr_unsafe(m_index);
        }

        [[nodiscard]] auto operator->() const -> T* {
            return m_array->get_ptr_unsafe(m_index);
        }

        auto operator++() -> Iterator& {
            m_index += 1u;
            return *this;
        }

        auto operator++(int) -> Iterator {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] friend auto operator==(Iterator const& lhs, Iterator const& rhs) -> bool {
            return lhs.m_array == rhs.m_array && lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs, Iterator const& rhs) -> bool {
            return !(lhs == rhs);
        }

      private:
        friend class XarArray;

        Iterator(XarArray* array, size_t index) : m_array(array), m_index(index) {}

      private:
        XarArray* m_array = nullptr;
        size_t m_index = 0u;
    };

    class ConstIterator {
      public:
        ConstIterator() = default;

        [[nodiscard]] auto operator*() const -> T const& {
            return *m_array->get_ptr_unsafe(m_index);
        }

        [[nodiscard]] auto operator->() const -> T const* {
            return m_array->get_ptr_unsafe(m_index);
        }

        auto operator++() -> ConstIterator& {
            m_index += 1u;
            return *this;
        }

        auto operator++(int) -> ConstIterator {
            ConstIterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] friend auto operator==(ConstIterator const& lhs, ConstIterator const& rhs)
            -> bool {
            return lhs.m_array == rhs.m_array && lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend auto operator!=(ConstIterator const& lhs, ConstIterator const& rhs)
            -> bool {
            return !(lhs == rhs);
        }

      private:
        friend class XarArray;

        ConstIterator(XarArray const* array, size_t index) : m_array(array), m_index(index) {}

      private:
        XarArray const* m_array = nullptr;
        size_t m_index = 0u;
    };

    explicit XarArray(MemoryResource* resource = xar_detail::default_memory_resource())
        : m_resource(resource) {
        if (m_resource == nullptr) {
            m_resource = xar_detail::default_memory_resource();
        }
    }

    ~XarArray() {
        destroy();
    }

    XarArray(XarArray&& other) {
        move_from(other);
    }

    XarArray(XarArray const&) = delete;

    auto operator=(XarArray&& other) -> XarArray& {
        if (this != &other) {
            destroy();
            move_from(other);
        }
        return *this;
    }

    auto operator=(XarArray const&) -> XarArray& = delete;

    [[nodiscard]] auto init(MemoryResource* resource = nullptr) -> bool {
        destroy();
        m_resource = resource != nullptr ? resource : xar_detail::default_memory_resource();
        return true;
    }

    auto destroy() -> void {
        if (m_resource != nullptr) {
            for (size_t reverse_index = CHUNK_COUNT; reverse_index > 0u; --reverse_index) {
                size_t const index = reverse_index - 1u;
                if (m_chunks[index] != nullptr) {
                    deallocate_chunk(index);
                }
            }
        }

        m_chunks = {};
        m_len = 0u;
        m_resource = nullptr;
    }

    auto clear() -> void {
        m_len = 0u;
    }

    auto reset() -> void {
        clear();
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto len() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto capacity() const -> size_t {
        for (size_t reverse_index = CHUNK_COUNT; reverse_index > 0u; --reverse_index) {
            size_t const index = reverse_index - 1u;
            if (m_chunks[index] != nullptr) {
                return xar_detail::capacity_after_chunk<SHIFT>(index);
            }
        }
        return 0u;
    }

    [[nodiscard]] auto empty() const -> bool {
        return m_len == 0u;
    }

    [[nodiscard]] auto resource() -> MemoryResource* {
        return m_resource;
    }

    [[nodiscard]] auto get(size_t index) const -> T {
        DEBUG_ASSERT(index < m_len);
        T value = {};
        xar_detail::copy_value(&value, get_ptr_unsafe(index));
        return value;
    }

    [[nodiscard]] auto get_ptr(size_t index) -> T* {
        DEBUG_ASSERT(index < m_len);
        return get_ptr_unsafe(index);
    }

    [[nodiscard]] auto get_ptr(size_t index) const -> T const* {
        DEBUG_ASSERT(index < m_len);
        return get_ptr_unsafe(index);
    }

    [[nodiscard]] auto get_ptr_safe(size_t index) -> XarPtrResult<T> {
        if (index >= m_len) {
            return {};
        }
        return XarPtrResult<T>{get_ptr_unsafe(index), true};
    }

    [[nodiscard]] auto get_ptr_safe(size_t index) const -> XarPtrResult<T const> {
        if (index >= m_len) {
            return {};
        }
        return XarPtrResult<T const>{get_ptr_unsafe(index), true};
    }

    [[nodiscard]] auto get_safe(size_t index) const -> XarResult<T> {
        if (index >= m_len) {
            return {};
        }
        return XarResult<T>{get(index), true};
    }

    auto set(size_t index, T const& value) -> void {
        DEBUG_ASSERT(index < m_len);
        xar_detail::copy_value(get_ptr_unsafe(index), &value);
    }

    [[nodiscard]] auto push_back(T const& value) -> bool {
        return push_back_and_get_ptr(value) != nullptr;
    }

    [[nodiscard]] auto append(T const& value) -> bool {
        return push_back(value);
    }

    [[nodiscard]] auto push_back_elems(std::span<T const> values) -> size_t {
        size_t added = 0u;
        for (T const& value : values) {
            if (!push_back(value)) {
                return added;
            }
            added += 1u;
        }
        return added;
    }

    [[nodiscard]] auto push_back_elems(std::initializer_list<T> values) -> size_t {
        return push_back_elems(std::span<T const>(values.begin(), values.size()));
    }

    template <size_t N> [[nodiscard]] auto push_back_elems(T const (&values)[N]) -> size_t {
        return push_back_elems(std::span<T const>(values, N));
    }

    [[nodiscard]] auto append(std::span<T const> values) -> size_t {
        return push_back_elems(values);
    }

    [[nodiscard]] auto append(std::initializer_list<T> values) -> size_t {
        return push_back_elems(values);
    }

    template <size_t N> [[nodiscard]] auto append(T const (&values)[N]) -> size_t {
        return push_back_elems(values);
    }

    [[nodiscard]] auto push_back_and_get_ptr(T const& value) -> T* {
        if (m_len >= MAX_CAPACITY) {
            return nullptr;
        }

        xar_detail::Meta const meta = xar_detail::meta_get<SHIFT>(m_len);
        DEBUG_ASSERT(meta.chunk_index < CHUNK_COUNT);

        if (m_chunks[meta.chunk_index] == nullptr && !allocate_chunk(meta.chunk_index)) {
            return nullptr;
        }

        T* const ptr = m_chunks[meta.chunk_index] + meta.element_index;
        xar_detail::copy_value(ptr, &value);
        m_len += 1u;
        return ptr;
    }

    [[nodiscard]] auto pop() -> T {
        DEBUG_ASSERT(m_len > 0u);
        size_t const index = m_len - 1u;
        T const value = get(index);
        m_len -= 1u;
        return value;
    }

    [[nodiscard]] auto pop_safe() -> XarResult<T> {
        if (m_len == 0u) {
            return {};
        }
        return XarResult<T>{pop(), true};
    }

    auto unordered_remove(size_t index) -> void {
        DEBUG_ASSERT(index < m_len);

        size_t const last_index = m_len - 1u;
        if (index != last_index) {
            T const end_value = get(last_index);
            set(index, end_value);
        }
        m_len -= 1u;
    }

    [[nodiscard]] auto linear_search(T const& value) const -> XarIndexResult {
        for (size_t index = 0u; index < m_len; ++index) {
            if (get(index) == value) {
                return XarIndexResult{index, true};
            }
        }
        return {};
    }

    [[nodiscard]] auto begin() -> Iterator {
        return Iterator(this, 0u);
    }

    [[nodiscard]] auto end() -> Iterator {
        return Iterator(this, m_len);
    }

    [[nodiscard]] auto begin() const -> ConstIterator {
        return ConstIterator(this, 0u);
    }

    [[nodiscard]] auto end() const -> ConstIterator {
        return ConstIterator(this, m_len);
    }

    [[nodiscard]] auto cbegin() const -> ConstIterator {
        return begin();
    }

    [[nodiscard]] auto cend() const -> ConstIterator {
        return end();
    }

  private:
    [[nodiscard]] auto allocate_chunk(size_t chunk_index) -> bool {
        size_t const chunk_capacity = xar_detail::chunk_capacity<SHIFT>(chunk_index);
        size_t allocation_size = 0u;
        if (xar_detail::mul_overflows(sizeof(T), chunk_capacity, allocation_size)) {
            return false;
        }
        DEBUG_ASSERT(m_resource != nullptr);
        void* const memory = m_resource->allocate(allocation_size, alignof(T));
        if (memory == nullptr) {
            return false;
        }

        m_chunks[chunk_index] = static_cast<T*>(memory);
        return true;
    }

    auto deallocate_chunk(size_t chunk_index) -> void {
        T* const chunk = m_chunks[chunk_index];
        if (chunk == nullptr) {
            return;
        }

        size_t const chunk_capacity = xar_detail::chunk_capacity<SHIFT>(chunk_index);
        size_t allocation_size = 0u;
        BASE_UNUSED(xar_detail::mul_overflows(sizeof(T), chunk_capacity, allocation_size));
        m_resource->deallocate(chunk, allocation_size, alignof(T));
        m_chunks[chunk_index] = nullptr;
    }

    [[nodiscard]] auto get_ptr_unsafe(size_t index) -> T* {
        xar_detail::Meta const meta = xar_detail::meta_get<SHIFT>(index);
        DEBUG_ASSERT(meta.chunk_index < CHUNK_COUNT);
        DEBUG_ASSERT(m_chunks[meta.chunk_index] != nullptr);
        return m_chunks[meta.chunk_index] + meta.element_index;
    }

    [[nodiscard]] auto get_ptr_unsafe(size_t index) const -> T const* {
        xar_detail::Meta const meta = xar_detail::meta_get<SHIFT>(index);
        DEBUG_ASSERT(meta.chunk_index < CHUNK_COUNT);
        DEBUG_ASSERT(m_chunks[meta.chunk_index] != nullptr);
        return m_chunks[meta.chunk_index] + meta.element_index;
    }

    auto move_from(XarArray& other) -> void {
        m_chunks = other.m_chunks;
        m_len = other.m_len;
        m_resource = other.m_resource;

        other.m_chunks = {};
        other.m_len = 0u;
        other.m_resource = nullptr;
    }

  private:
    std::array<T*, CHUNK_COUNT> m_chunks = {};
    size_t m_len = 0u;
    MemoryResource* m_resource = nullptr;
};

template <typename T, size_t SHIFT> class XarFreelistArray final {
  public:
    static_assert(sizeof(T) >= sizeof(T*),
                  "XarFreelistArray value type must fit a free-list pointer");

    using Value = T;
    using Array = XarArray<T, SHIFT>;

    class Iterator {
      public:
        Iterator() = default;

        [[nodiscard]] auto operator*() const -> T& {
            return *m_array->get_ptr(m_index);
        }

        [[nodiscard]] auto operator->() const -> T* {
            return m_array->get_ptr(m_index);
        }

        auto operator++() -> Iterator& {
            m_index += 1u;
            skip_freed();
            return *this;
        }

        auto operator++(int) -> Iterator {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[nodiscard]] auto index() const -> size_t {
            return m_index;
        }

        [[nodiscard]] friend auto operator==(Iterator const& lhs, Iterator const& rhs) -> bool {
            return lhs.m_array == rhs.m_array && lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs, Iterator const& rhs) -> bool {
            return !(lhs == rhs);
        }

      private:
        friend class XarFreelistArray;

        Iterator(XarFreelistArray* array, size_t index) : m_array(array), m_index(index) {
            skip_freed();
        }

        auto skip_freed() -> void {
            if (m_array == nullptr) {
                return;
            }
            while (m_index < m_array->len() && m_array->is_freed(m_index)) {
                m_index += 1u;
            }
        }

      private:
        XarFreelistArray* m_array = nullptr;
        size_t m_index = 0u;
    };

    XarFreelistArray() = default;
    explicit XarFreelistArray(MemoryResource* resource) : m_array(resource) {}

    XarFreelistArray(XarFreelistArray&& other) {
        move_from(other);
    }

    XarFreelistArray(XarFreelistArray const&) = delete;

    auto operator=(XarFreelistArray&& other) -> XarFreelistArray& {
        if (this != &other) {
            destroy();
            move_from(other);
        }
        return *this;
    }

    auto operator=(XarFreelistArray const&) -> XarFreelistArray& = delete;

    [[nodiscard]] auto init(MemoryResource* resource = nullptr) -> bool {
        m_freelist = nullptr;
        return m_array.init(resource);
    }

    auto destroy() -> void {
        m_array.destroy();
        m_freelist = nullptr;
    }

    auto clear() -> void {
        m_array.clear();
        m_freelist = nullptr;
    }

    auto reset() -> void {
        clear();
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_array.size();
    }

    [[nodiscard]] auto len() const -> size_t {
        return m_array.len();
    }

    [[nodiscard]] auto capacity() const -> size_t {
        return m_array.capacity();
    }

    [[nodiscard]] auto empty() const -> bool {
        return m_array.empty();
    }

    [[nodiscard]] auto resource() -> MemoryResource* {
        return m_array.resource();
    }

    [[nodiscard]] auto get(size_t index) const -> T {
        DEBUG_ASSERT(!is_freed(index));
        return m_array.get(index);
    }

    [[nodiscard]] auto get_ptr(size_t index) -> T* {
        DEBUG_ASSERT(!is_freed(index));
        return m_array.get_ptr(index);
    }

    [[nodiscard]] auto get_ptr(size_t index) const -> T const* {
        DEBUG_ASSERT(!is_freed(index));
        return m_array.get_ptr(index);
    }

    auto set(size_t index, T const& value) -> void {
        DEBUG_ASSERT(!is_freed(index));
        m_array.set(index, value);
    }

    [[nodiscard]] auto push(T const& value) -> T* {
        return push_with_index(value).ptr;
    }

    [[nodiscard]] auto push_with_index(T const& value) -> XarPushResult<T> {
        if (m_freelist != nullptr) {
            T* const slot = m_freelist;
            XarIndexResult const found = linear_search(slot);
            DEBUG_ASSERT(found);

            m_freelist = read_next_free(slot);
            xar_detail::copy_value(slot, &value);
            return XarPushResult<T>{slot, found.index, found.found};
        }

        size_t const index = m_array.len();
        T* const ptr = m_array.push_back_and_get_ptr(value);
        return XarPushResult<T>{ptr, ptr != nullptr ? index : XAR_NPOS, ptr != nullptr};
    }

    [[nodiscard]] auto pop(size_t index) -> T {
        T* const item = m_array.get_ptr(index);
        DEBUG_ASSERT(!is_freed(index));

        T result = {};
        xar_detail::copy_value(&result, item);
        write_next_free(item, m_freelist);
        m_freelist = item;
        return result;
    }

    auto release(size_t index) -> void {
        T* const item = m_array.get_ptr(index);
        DEBUG_ASSERT(!is_freed(index));
        write_next_free(item, m_freelist);
        m_freelist = item;
    }

    [[nodiscard]] auto is_freed(size_t index) const -> bool {
        T const* const ptr = m_array.get_ptr(index);
        T const* current = m_freelist;
        while (current != nullptr) {
            if (current == ptr) {
                return true;
            }
            current = read_next_free(current);
        }
        return false;
    }

    [[nodiscard]] auto linear_search(T const* ptr) const -> XarIndexResult {
        for (size_t index = 0u; index < m_array.len(); ++index) {
            if (m_array.get_ptr(index) == ptr) {
                return XarIndexResult{index, true};
            }
        }
        return {};
    }

    [[nodiscard]] auto begin() -> Iterator {
        return Iterator(this, 0u);
    }

    [[nodiscard]] auto end() -> Iterator {
        return Iterator(this, len());
    }

  private:
    static auto write_next_free(T* slot, T* next) -> void {
        std::memcpy(slot, &next, sizeof(next));
    }

    [[nodiscard]] static auto read_next_free(T const* slot) -> T* {
        T* next = nullptr;
        std::memcpy(&next, slot, sizeof(next));
        return next;
    }

    auto move_from(XarFreelistArray& other) -> void {
        m_array = std::move(other.m_array);
        m_freelist = other.m_freelist;
        other.m_freelist = nullptr;
    }

  private:
    Array m_array;
    T* m_freelist = nullptr;
};
