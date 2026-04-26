#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <cstddef>

class StringBuffer final {
  public:
    StringBuffer() noexcept = default;
    ~StringBuffer();

    StringBuffer(StringBuffer&& other) noexcept;
    StringBuffer(StringBuffer const&) = delete;
    auto operator=(StringBuffer&& other) noexcept -> StringBuffer&;
    auto operator=(StringBuffer const&) -> StringBuffer& = delete;

    [[nodiscard]] auto init(size_t capacity = 0u, MemoryResource* resource = nullptr) noexcept
        -> bool;
    [[nodiscard]] auto init_with_backing(char* backing, size_t capacity) noexcept -> bool;
    auto destroy() noexcept -> void;

    auto reset() noexcept -> void;
    [[nodiscard]] auto reserve(size_t capacity) noexcept -> bool;
    [[nodiscard]] auto resize(size_t size, char fill = '\0') noexcept -> bool;
    auto truncate(size_t size) noexcept -> void;

    [[nodiscard]] auto write_byte(char value) noexcept -> size_t;
    [[nodiscard]] auto write_bytes(void const* data, size_t size) noexcept -> size_t;
    [[nodiscard]] auto write_string(StrRef text) noexcept -> size_t;
    [[nodiscard]] auto write_fill(char value, size_t count) noexcept -> size_t;

    [[nodiscard]] auto append(char value) noexcept -> bool;
    [[nodiscard]] auto append(StrRef text) noexcept -> bool;

    [[nodiscard]] auto pop_byte() noexcept -> char;
    [[nodiscard]] auto c_str() noexcept -> char const*;

    [[nodiscard]] auto str() const noexcept -> StrRef;
    [[nodiscard]] auto view() const noexcept -> StrRef;
    [[nodiscard]] explicit operator StrRef() const noexcept;

    [[nodiscard]] auto data() noexcept -> char*;
    [[nodiscard]] auto data() const noexcept -> char const*;
    [[nodiscard]] auto size() const noexcept -> size_t;
    [[nodiscard]] auto capacity() const noexcept -> size_t;
    [[nodiscard]] auto space() const noexcept -> size_t;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto initialized() const noexcept -> bool;
    [[nodiscard]] auto fixed_capacity() const noexcept -> bool;
    [[nodiscard]] auto resource() const noexcept -> MemoryResource*;

  private:
    [[nodiscard]] auto ensure_dynamic_resource() noexcept -> bool;
    [[nodiscard]] auto grow_to_fit(size_t needed_size) noexcept -> bool;
    auto write_terminator() noexcept -> void;
    auto move_from(StringBuffer& other) noexcept -> void;

  private:
    char* m_data = nullptr;
    size_t m_size = 0u;
    size_t m_capacity = 0u;
    size_t m_allocation_size = 0u;
    MemoryResource* m_resource = nullptr;
    bool m_initialized = false;
    bool m_fixed_capacity = false;
};
