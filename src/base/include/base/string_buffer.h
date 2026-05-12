#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <cstddef>

class StringBuffer final {
  public:
    StringBuffer() = default;

    auto init(size_t capacity, MemoryResource* resource) -> bool;
    auto copy_from(StringBuffer const& other, MemoryResource* resource) -> bool;
    auto init_with_backing(char* backing, size_t capacity) -> void;

    auto reset() -> void;
    auto reserve(size_t capacity) -> void;
    auto resize(size_t size, char fill = '\0') -> bool;

    auto write_byte(char value) -> size_t;
    auto write_bytes(void const* data, size_t size) -> size_t;
    auto write_string(StrRef text) -> size_t;
    auto write_fill(char value, size_t count) -> size_t;

    [[nodiscard]] auto pop_byte() -> char;
    [[nodiscard]] auto c_str() -> char const*;

    [[nodiscard]] auto str() const -> StrRef;
    [[nodiscard]] explicit operator StrRef() const;

    [[nodiscard]] auto data() -> char*;
    [[nodiscard]] auto data() const -> char const*;
    [[nodiscard]] auto size() const -> size_t;
    [[nodiscard]] auto capacity() const -> size_t;
    [[nodiscard]] auto space() const -> size_t;
    [[nodiscard]] auto empty() const -> bool;

  private:
    [[nodiscard]] auto fixed_capacity() const -> bool;
    [[nodiscard]] auto has_terminator_space() const -> bool;
    [[nodiscard]] auto prepare_write(size_t size) -> size_t;
    auto write_terminator() -> void;

  private:
    char* m_data = nullptr;
    size_t m_size = 0u;
    size_t m_capacity = 0u;
    MemoryResource* m_resource = nullptr;
};
