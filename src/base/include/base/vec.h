#pragma once

#include <algorithm>
#include <base/assert.h>
#include <base/memory.h>
#include <base/slice.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace vec_detail {
    inline constexpr size_t MIN_GROWN_CAPACITY = 8u;

    [[nodiscard]] inline auto default_memory_resource() -> MemoryResource* {
        return std::pmr::get_default_resource();
    }

    [[nodiscard]] constexpr auto add_overflows(size_t lhs, size_t rhs, size_t& out) -> bool {
        if (lhs > std::numeric_limits<size_t>::max() - rhs) {
            return true;
        }
        out = lhs + rhs;
        return false;
    }

    [[nodiscard]] constexpr auto mul_overflows(size_t lhs, size_t rhs, size_t& out) -> bool {
        if (lhs != 0u && rhs > std::numeric_limits<size_t>::max() / lhs) {
            return true;
        }
        out = lhs * rhs;
        return false;
    }

    [[nodiscard]] constexpr auto grown_capacity(size_t capacity, size_t item_count) -> size_t {
        size_t doubled = 0u;
        if (mul_overflows(capacity, 2u, doubled)) {
            return std::numeric_limits<size_t>::max();
        }

        size_t grown = 0u;
        if (add_overflows(doubled, std::max(MIN_GROWN_CAPACITY, item_count), grown)) {
            return std::numeric_limits<size_t>::max();
        }
        return grown;
    }

    template <typename T> auto copy_value(T* target, T const* source) -> void {
        std::memcpy(target, source, sizeof(T));
    }
} // namespace vec_detail

template <typename T> struct VecResult {
    T value = {};
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T> struct VecPtrResult {
    T* ptr = nullptr;
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T> class Vec final {
  public:
    static_assert(!std::is_const_v<T>, "Vec value type must not be const");
    static_assert(std::is_trivially_copyable_v<T>, "Vec stores values with Odin-style byte copies");

    using Value = T;

    Vec() = default;
    explicit Vec(MemoryResource* resource) : m_resource(resource) {}

    ~Vec() {
        destroy();
    }

    Vec(Vec&& other) {
        move_from(other);
    }

    Vec(Vec const&) = delete;

    auto operator=(Vec&& other) -> Vec& {
        if (this != &other) {
            destroy();
            move_from(other);
        }
        return *this;
    }

    auto operator=(Vec const&) -> Vec& = delete;

    [[nodiscard]] auto init(size_t capacity = 0u, MemoryResource* resource = nullptr) -> bool {
        destroy();
        m_resource = resource != nullptr ? resource : vec_detail::default_memory_resource();
        return reserve(capacity);
    }

    auto destroy() -> void {
        if (m_data != nullptr && m_resource != nullptr) {
            m_resource->deallocate(m_data, allocation_size(m_cap), alignof(T));
        }

        m_data = nullptr;
        m_len = 0u;
        m_cap = 0u;
        m_resource = nullptr;
    }

    auto clear() -> void {
        m_len = 0u;
    }

    auto reset() -> void {
        clear();
    }

    [[nodiscard]] auto reserve(size_t capacity) -> bool {
        if (m_resource == nullptr) {
            m_resource = vec_detail::default_memory_resource();
        }
        if (capacity <= m_cap) {
            return true;
        }
        return reallocate(capacity);
    }

    [[nodiscard]] auto shrink(size_t capacity) -> bool {
        if (capacity >= m_cap) {
            return false;
        }
        return reallocate(capacity);
    }

    [[nodiscard]] auto resize(size_t size, T const& fill = {}) -> bool {
        if (!reserve(size)) {
            return false;
        }

        for (size_t index = m_len; index < size; ++index) {
            vec_detail::copy_value(m_data + index, &fill);
        }
        m_len = size;
        return true;
    }

    [[nodiscard]] auto non_zero_resize(size_t size) -> bool {
        if (!reserve(size)) {
            return false;
        }
        m_len = size;
        return true;
    }

    [[nodiscard]] auto push_back(T const& value) -> bool {
        return push_back_and_get_ptr(value) != nullptr;
    }

    [[nodiscard]] auto append(T const& value) -> bool {
        return push_back(value);
    }

    [[nodiscard]] auto push_back_and_get_ptr(T const& value) -> T* {
        if (!ensure_space(1u)) {
            return nullptr;
        }

        T* const ptr = m_data + m_len;
        vec_detail::copy_value(ptr, &value);
        m_len += 1u;
        return ptr;
    }

    [[nodiscard]] auto append_nothing() -> T* {
        if (!ensure_space(1u)) {
            return nullptr;
        }

        T* const ptr = m_data + m_len;
        std::memset(ptr, 0, sizeof(T));
        m_len += 1u;
        return ptr;
    }

    [[nodiscard]] auto push_back_elems(Slice<T const> values) -> size_t {
        return append(values);
    }

    [[nodiscard]] auto push_back_elems(std::initializer_list<T> values) -> size_t {
        return append(values);
    }

    template <size_t N> [[nodiscard]] auto push_back_elems(T const (&values)[N]) -> size_t {
        return append(Slice<T const>(values, N));
    }

    [[nodiscard]] auto append(Slice<T const> values) -> size_t {
        if (values.empty()) {
            return 0u;
        }

        size_t source_offset = 0u;
        bool const internal_source = pointer_in_data(values.data(), values.size(), &source_offset);
        if (!ensure_space(values.size())) {
            return 0u;
        }

        T const* source = internal_source ? m_data + source_offset : values.data();
        std::memmove(m_data + m_len, source, sizeof(T) * values.size());
        m_len += values.size();
        return values.size();
    }

    [[nodiscard]] auto append(std::initializer_list<T> values) -> size_t {
        return append(Slice<T const>(values.begin(), values.size()));
    }

    template <size_t N> [[nodiscard]] auto append(T const (&values)[N]) -> size_t {
        return append(Slice<T const>(values, N));
    }

    [[nodiscard]] auto pop() -> T {
        DEBUG_ASSERT(m_len > 0u);
        m_len -= 1u;
        T value = {};
        vec_detail::copy_value(&value, m_data + m_len);
        return value;
    }

    [[nodiscard]] auto pop_safe() -> VecResult<T> {
        if (m_len == 0u) {
            return {};
        }
        return VecResult<T>{pop(), true};
    }

    auto ordered_remove(size_t index) -> void {
        DEBUG_ASSERT(index < m_len);
        size_t const trailing_count = m_len - index - 1u;
        if (trailing_count != 0u) {
            std::memmove(m_data + index, m_data + index + 1u, trailing_count * sizeof(T));
        }
        m_len -= 1u;
    }

    auto unordered_remove(size_t index) -> void {
        DEBUG_ASSERT(index < m_len);
        size_t const last = m_len - 1u;
        if (index != last) {
            vec_detail::copy_value(m_data + index, m_data + last);
        }
        m_len -= 1u;
    }

    [[nodiscard]] auto get(size_t index) const -> T {
        DEBUG_ASSERT(index < m_len);
        T value = {};
        vec_detail::copy_value(&value, m_data + index);
        return value;
    }

    [[nodiscard]] auto get_safe(size_t index) const -> VecResult<T> {
        if (index >= m_len) {
            return {};
        }
        return VecResult<T>{get(index), true};
    }

    [[nodiscard]] auto get_ptr(size_t index) -> T* {
        DEBUG_ASSERT(index < m_len);
        return m_data + index;
    }

    [[nodiscard]] auto get_ptr(size_t index) const -> T const* {
        DEBUG_ASSERT(index < m_len);
        return m_data + index;
    }

    [[nodiscard]] auto get_ptr_safe(size_t index) -> VecPtrResult<T> {
        if (index >= m_len) {
            return {};
        }
        return VecPtrResult<T>{m_data + index, true};
    }

    [[nodiscard]] auto get_ptr_safe(size_t index) const -> VecPtrResult<T const> {
        if (index >= m_len) {
            return {};
        }
        return VecPtrResult<T const>{m_data + index, true};
    }

    auto set(size_t index, T const& value) -> void {
        DEBUG_ASSERT(index < m_len);
        vec_detail::copy_value(m_data + index, &value);
    }

    [[nodiscard]] auto slice() -> Slice<T> {
        return Slice<T>(m_data, m_len);
    }

    [[nodiscard]] auto slice() const -> Slice<T const> {
        return Slice<T const>(m_data, m_len);
    }

    [[nodiscard]] auto data() -> T* {
        return m_data;
    }

    [[nodiscard]] auto data() const -> T const* {
        return m_data;
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto len() const -> size_t {
        return m_len;
    }

    [[nodiscard]] auto capacity() const -> size_t {
        return m_cap;
    }

    [[nodiscard]] auto space() const -> size_t {
        return m_cap - m_len;
    }

    [[nodiscard]] auto empty() const -> bool {
        return m_len == 0u;
    }

    [[nodiscard]] auto resource() -> MemoryResource* {
        return m_resource;
    }

    [[nodiscard]] auto begin() -> T* {
        return m_data;
    }

    [[nodiscard]] auto begin() const -> T const* {
        return m_data;
    }

    [[nodiscard]] auto end() -> T* {
        return m_data != nullptr ? m_data + m_len : nullptr;
    }

    [[nodiscard]] auto end() const -> T const* {
        return m_data != nullptr ? m_data + m_len : nullptr;
    }

    [[nodiscard]] auto operator[](size_t index) -> T& {
        DEBUG_ASSERT(index < m_len);
        return m_data[index];
    }

    [[nodiscard]] auto operator[](size_t index) const -> T const& {
        DEBUG_ASSERT(index < m_len);
        return m_data[index];
    }

  private:
    [[nodiscard]] static auto allocation_size(size_t capacity) -> size_t {
        size_t size = 0u;
        BASE_UNUSED(vec_detail::mul_overflows(sizeof(T), capacity, size));
        return size;
    }

    [[nodiscard]] auto ensure_space(size_t item_count) -> bool {
        size_t needed = 0u;
        if (vec_detail::add_overflows(m_len, item_count, needed)) {
            return false;
        }
        if (needed <= m_cap) {
            return true;
        }
        return reserve(std::max(needed, vec_detail::grown_capacity(m_cap, item_count)));
    }

    [[nodiscard]] auto reallocate(size_t capacity) -> bool {
        size_t new_size = 0u;
        if (vec_detail::mul_overflows(sizeof(T), capacity, new_size)) {
            return false;
        }

        T* new_data = nullptr;
        if (capacity != 0u) {
            DEBUG_ASSERT(m_resource != nullptr);
            new_data = static_cast<T*>(m_resource->allocate(new_size, alignof(T)));
            if (new_data == nullptr) {
                return false;
            }
        }

        size_t const new_len = std::min(m_len, capacity);
        if (m_data != nullptr && new_len != 0u) {
            std::memcpy(new_data, m_data, sizeof(T) * new_len);
        }
        if (m_data != nullptr && m_resource != nullptr) {
            m_resource->deallocate(m_data, allocation_size(m_cap), alignof(T));
        }

        m_data = new_data;
        m_len = new_len;
        m_cap = capacity;
        return true;
    }

    [[nodiscard]] auto pointer_in_data(T const* ptr, size_t count, size_t* out_offset) const
        -> bool {
        if (ptr == nullptr || m_data == nullptr || count > m_len) {
            return false;
        }

        uintptr_t const address = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t const first = reinterpret_cast<uintptr_t>(m_data);
        uintptr_t const byte_size = sizeof(T) * m_len;
        uintptr_t const source_size = sizeof(T) * count;
        if (address < first || address - first > byte_size - source_size) {
            return false;
        }

        uintptr_t const offset = address - first;
        if ((offset % sizeof(T)) != 0u) {
            return false;
        }

        *out_offset = static_cast<size_t>(offset / sizeof(T));
        return true;
    }

    auto move_from(Vec& other) -> void {
        m_data = other.m_data;
        m_len = other.m_len;
        m_cap = other.m_cap;
        m_resource = other.m_resource;

        other.m_data = nullptr;
        other.m_len = 0u;
        other.m_cap = 0u;
        other.m_resource = nullptr;
    }

  private:
    T* m_data = nullptr;
    size_t m_len = 0u;
    size_t m_cap = 0u;
    MemoryResource* m_resource = nullptr;
};
