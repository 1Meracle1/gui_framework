#pragma once

#include <atomic>
#include <base/assert.h>
#include <base/memory.h>
#include <cstddef>
#include <cstring>
#include <type_traits>

template <typename T> class SpscQueue final {
  public:
    static_assert(!std::is_const_v<T>, "SpscQueue value type must not be const");
    static_assert(
        std::is_trivially_copyable_v<T>, "SpscQueue stores values with Odin-style byte copies"
    );

    SpscQueue() = default;

    [[nodiscard]] auto init(size_t capacity, MemoryResource* resource) -> bool {
        *this = {};
        DEBUG_ASSERT(resource != nullptr);
        if (capacity == 0u || resource == nullptr) {
            return false;
        }
        m_capacity = capacity + 1u;
        m_data = static_cast<T*>(resource->allocate(sizeof(T) * m_capacity, alignof(T)));
        return m_data != nullptr;
    }

    auto clear() -> void {
        store(m_read, 0u, std::memory_order_relaxed);
        store(m_write, 0u, std::memory_order_relaxed);
    }

    [[nodiscard]] auto push(T const& value) -> bool {
        size_t const write = load(m_write, std::memory_order_relaxed);
        size_t const next = increment(write);
        if (next == load(m_read, std::memory_order_acquire)) {
            return false;
        }
        std::memcpy(m_data + write, &value, sizeof(T));
        store(m_write, next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto pop(T& out) -> bool {
        size_t const read = load(m_read, std::memory_order_relaxed);
        if (read == load(m_write, std::memory_order_acquire)) {
            return false;
        }
        std::memcpy(&out, m_data + read, sizeof(T));
        store(m_read, increment(read), std::memory_order_release);
        return true;
    }

    [[nodiscard]] auto empty() const -> bool {
        return load(m_read, std::memory_order_acquire) == load(m_write, std::memory_order_acquire);
    }

    [[nodiscard]] auto full() const -> bool {
        size_t const write = load(m_write, std::memory_order_acquire);
        return increment(write) == load(m_read, std::memory_order_acquire);
    }

    [[nodiscard]] auto capacity() const -> size_t {
        return m_capacity == 0u ? 0u : m_capacity - 1u;
    }

  private:
    [[nodiscard]] static auto load(size_t const& value, std::memory_order order) -> size_t {
        return std::atomic_ref<size_t>(const_cast<size_t&>(value)).load(order);
    }

    static auto store(size_t& target, size_t value, std::memory_order order) -> void {
        std::atomic_ref<size_t>(target).store(value, order);
    }

    [[nodiscard]] auto increment(size_t index) const -> size_t {
        index += 1u;
        return index == m_capacity ? 0u : index;
    }

  private:
    T* m_data = nullptr;
    size_t m_capacity = 0u;
    size_t m_read = 0u;
    uint8_t m_read_padding[64u - sizeof(size_t)] = {};
    size_t m_write = 0u;
    uint8_t m_write_padding[64u - sizeof(size_t)] = {};
};

static_assert(std::is_trivially_copyable_v<SpscQueue<int>>);
