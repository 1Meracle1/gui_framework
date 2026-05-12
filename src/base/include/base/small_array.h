#pragma once

#include <algorithm>
#include <array>
#include <base/assert.h>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <type_traits>

template <typename T> struct SmallArrayResult {
    T value = {};
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T> struct SmallArrayPtrResult {
    T* ptr = nullptr;
    bool ok = false;

    [[nodiscard]] constexpr explicit operator bool() const {
        return ok;
    }
};

template <typename T, size_t N> struct SmallArray {
    static_assert(!std::is_const_v<T>, "SmallArray value type must not be const");

    using Value = T;

    static constexpr size_t CAPACITY = N;

    std::array<T, N> data = {};
    size_t len = 0u;

    [[nodiscard]] static constexpr auto static_capacity() -> size_t {
        return CAPACITY;
    }

    [[nodiscard]] constexpr auto size() const -> size_t {
        return len;
    }

    [[nodiscard]] constexpr auto length() const -> size_t {
        return len;
    }

    [[nodiscard]] constexpr auto capacity() const -> size_t {
        return CAPACITY;
    }

    [[nodiscard]] constexpr auto space() const -> size_t {
        return CAPACITY - len;
    }

    [[nodiscard]] constexpr auto empty() const -> bool {
        return len == 0u;
    }

    [[nodiscard]] constexpr auto full() const -> bool {
        return len == CAPACITY;
    }

    [[nodiscard]] constexpr auto slice() -> std::span<T> {
        return std::span<T>(data.data(), len);
    }

    [[nodiscard]] constexpr auto slice() const -> std::span<T const> {
        return std::span<T const>(data.data(), len);
    }

    [[nodiscard]] constexpr auto begin() -> T* {
        return data.data();
    }

    [[nodiscard]] constexpr auto begin() const -> T const* {
        return data.data();
    }

    [[nodiscard]] constexpr auto end() -> T* {
        return data.data() + len;
    }

    [[nodiscard]] constexpr auto end() const -> T const* {
        return data.data() + len;
    }

    [[nodiscard]] constexpr auto get(size_t index) const -> T {
        DEBUG_ASSERT(index < CAPACITY);
        return data[index];
    }

    [[nodiscard]] constexpr auto operator[](size_t index) -> T& {
        DEBUG_ASSERT(index < CAPACITY);
        return data[index];
    }

    [[nodiscard]] constexpr auto operator[](size_t index) const -> T const& {
        DEBUG_ASSERT(index < CAPACITY);
        return data[index];
    }

    [[nodiscard]] constexpr auto get_ptr(size_t index) -> T* {
        DEBUG_ASSERT(index < CAPACITY);
        return data.data() + index;
    }

    [[nodiscard]] constexpr auto get_ptr(size_t index) const -> T const* {
        DEBUG_ASSERT(index < CAPACITY);
        return data.data() + index;
    }

    [[nodiscard]] constexpr auto get_safe(size_t index) const -> SmallArrayResult<T> {
        if (index >= len) {
            return {};
        }
        return SmallArrayResult<T>{data[index], true};
    }

    [[nodiscard]] constexpr auto get_ptr_safe(size_t index) -> SmallArrayPtrResult<T> {
        if (index >= len) {
            return {};
        }
        return SmallArrayPtrResult<T>{data.data() + index, true};
    }

    [[nodiscard]] constexpr auto get_ptr_safe(size_t index) const -> SmallArrayPtrResult<T const> {
        if (index >= len) {
            return {};
        }
        return SmallArrayPtrResult<T const>{data.data() + index, true};
    }

    constexpr auto set(size_t index, T const& item) -> void {
        DEBUG_ASSERT(index < CAPACITY);
        data[index] = item;
    }

    constexpr auto resize(size_t length) -> void {
        size_t const old_len = len;
        len = std::min(length, CAPACITY);
        for (size_t index = old_len; index < len; ++index) {
            data[index] = T{};
        }
    }

    constexpr auto non_zero_resize(size_t length) -> void {
        len = std::min(length, CAPACITY);
    }

    constexpr auto push_back(T const& item) -> bool {
        if (len >= CAPACITY) {
            return false;
        }

        data[len] = item;
        len += 1u;
        return true;
    }

    constexpr auto push_front(T const& item) -> bool {
        return inject_at(item, 0u);
    }

    [[nodiscard]] constexpr auto pop_back() -> T {
        DEBUG_ASSERT(len > 0u);
        T item = data[len - 1u];
        len -= 1u;
        return item;
    }

    [[nodiscard]] constexpr auto pop_front() -> T {
        DEBUG_ASSERT(len > 0u);
        T item = data[0u];
        for (size_t index = 1u; index < len; ++index) {
            data[index - 1u] = data[index];
        }
        len -= 1u;
        return item;
    }

    [[nodiscard]] constexpr auto pop_back_safe() -> SmallArrayResult<T> {
        if (len == 0u) {
            return {};
        }
        return SmallArrayResult<T>{pop_back(), true};
    }

    [[nodiscard]] constexpr auto pop_front_safe() -> SmallArrayResult<T> {
        if (len == 0u) {
            return {};
        }
        return SmallArrayResult<T>{pop_front(), true};
    }

    constexpr auto consume(size_t count) -> void {
        DEBUG_ASSERT(count <= len);
        len -= count;
    }

    constexpr auto ordered_remove(size_t index) -> void {
        DEBUG_ASSERT(index < len);
        for (size_t next = index + 1u; next < len; ++next) {
            data[next - 1u] = data[next];
        }
        len -= 1u;
    }

    constexpr auto unordered_remove(size_t index) -> void {
        DEBUG_ASSERT(index < len);
        size_t const last = len - 1u;
        if (index != last) {
            data[index] = data[last];
        }
        len -= 1u;
    }

    constexpr auto clear() -> void {
        resize(0u);
    }

    constexpr auto push_back_elems(std::span<T const> items) -> bool {
        if (items.size() > space()) {
            return false;
        }

        for (T const& item : items) {
            data[len] = item;
            len += 1u;
        }
        return true;
    }

    constexpr auto push_back_elems(std::initializer_list<T> items) -> bool {
        return push_back_elems(std::span<T const>(items.begin(), items.size()));
    }

    template <size_t M> constexpr auto push_back_elems(T const (&items)[M]) -> bool {
        return push_back_elems(std::span<T const>(items, M));
    }

    constexpr auto inject_at(T const& item, size_t index) -> bool {
        if (len >= CAPACITY || index > len) {
            return false;
        }

        for (size_t position = len; position > index; --position) {
            data[position] = data[position - 1u];
        }
        data[index] = item;
        len += 1u;
        return true;
    }

    constexpr auto append_elem(T const& item) -> bool {
        return push_back(item);
    }

    constexpr auto append_elems(std::span<T const> items) -> bool {
        return push_back_elems(items);
    }

    constexpr auto append_elems(std::initializer_list<T> items) -> bool {
        return push_back_elems(items);
    }

    template <size_t M> constexpr auto append_elems(T const (&items)[M]) -> bool {
        return push_back_elems(items);
    }

    constexpr auto append(T const& item) -> bool {
        return push_back(item);
    }

    constexpr auto append(std::span<T const> items) -> bool {
        return push_back_elems(items);
    }

    constexpr auto append(std::initializer_list<T> items) -> bool {
        return push_back_elems(items);
    }

    template <size_t M> constexpr auto append(T const (&items)[M]) -> bool {
        return push_back_elems(items);
    }
};
