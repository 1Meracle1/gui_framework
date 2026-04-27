#pragma once

#include <algorithm>
#include <base/assert.h>
#include <cstddef>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

template <typename T> class Slice final {
  public:
    using Value = std::remove_cv_t<T>;

    static constexpr size_t NPOS = std::numeric_limits<size_t>::max();

    constexpr Slice() = default;
    constexpr Slice(T* data, size_t size) : m_data(data), m_size(data != nullptr ? size : 0u) {}

    template <size_t N> constexpr Slice(T (&items)[N]) : Slice(items, N) {}

    template <typename U>
        requires(std::is_convertible_v<U*, T*>)
    constexpr Slice(Slice<U> other) : Slice(other.data(), other.size()) {}

    [[nodiscard]] constexpr auto data() const -> T* {
        return m_data;
    }

    [[nodiscard]] constexpr auto size() const -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto len() const -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto empty() const -> bool {
        return m_size == 0u;
    }

    [[nodiscard]] constexpr auto begin() const -> T* {
        return m_data;
    }

    [[nodiscard]] constexpr auto end() const -> T* {
        return m_data != nullptr ? m_data + m_size : nullptr;
    }

    [[nodiscard]] constexpr auto operator[](size_t index) const -> T& {
        ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] constexpr auto front() const -> T& {
        ASSERT(m_size > 0u);
        return m_data[0u];
    }

    [[nodiscard]] constexpr auto back() const -> T& {
        ASSERT(m_size > 0u);
        return m_data[m_size - 1u];
    }

    [[nodiscard]] constexpr auto slice(size_t offset, size_t count = NPOS) const -> Slice {
        size_t const clamped_offset = std::min(offset, m_size);
        size_t const remaining_size = m_size - clamped_offset;
        size_t const clamped_count = std::min(count, remaining_size);
        T* const data = m_data != nullptr ? m_data + clamped_offset : nullptr;
        return Slice(data, clamped_count);
    }

    [[nodiscard]] constexpr auto prefix(size_t count) const -> Slice {
        return slice(0u, count);
    }

    [[nodiscard]] constexpr auto suffix(size_t count) const -> Slice {
        size_t const clamped_count = std::min(count, m_size);
        return slice(m_size - clamped_count, clamped_count);
    }

    [[nodiscard]] constexpr auto drop_prefix(size_t count) const -> Slice {
        size_t const clamped_count = std::min(count, m_size);
        return slice(clamped_count);
    }

    [[nodiscard]] constexpr auto drop_suffix(size_t count) const -> Slice {
        size_t const clamped_count = std::min(count, m_size);
        return slice(0u, m_size - clamped_count);
    }

    constexpr auto remove_prefix(size_t count) -> void {
        *this = drop_prefix(count);
    }

    constexpr auto remove_suffix(size_t count) -> void {
        *this = drop_suffix(count);
    }

  private:
    T* m_data = nullptr;
    size_t m_size = 0u;
};

template <typename T> [[nodiscard]] constexpr auto slice(T* data, size_t size) -> Slice<T> {
    return Slice<T>(data, size);
}

template <typename T> [[nodiscard]] constexpr auto slice(T* begin, T* end) -> Slice<T> {
    ASSERT(begin != nullptr && end != nullptr && begin <= end);
    return Slice<T>(begin, static_cast<size_t>(end - begin));
}

template <typename T, size_t N> [[nodiscard]] constexpr auto slice(T (&items)[N]) -> Slice<T> {
    return Slice<T>(items, N);
}

namespace slice_detail {
    template <typename Container> using DataPtr = decltype(std::data(std::declval<Container&>()));
} // namespace slice_detail

template <typename Container>
    requires(std::is_pointer_v<slice_detail::DataPtr<Container>>)
[[nodiscard]] constexpr auto slice(Container& container)
    -> Slice<std::remove_pointer_t<slice_detail::DataPtr<Container>>> {
    return {std::data(container), static_cast<size_t>(std::size(container))};
}
