#include <algorithm>
#include <base/assert.h>
#include <base/string_buffer.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
    inline constexpr size_t MIN_GROWN_STRING_BUFFER_CAPACITY = 64u;

    [[nodiscard]] auto add_overflows(size_t lhs, size_t rhs, size_t* out) -> bool {
        if (lhs > std::numeric_limits<size_t>::max() - rhs) {
            return true;
        }
        *out = lhs + rhs;
        return false;
    }

    [[nodiscard]] auto grown_capacity(size_t current_capacity, size_t needed_size) -> size_t {
        size_t capacity = std::max(current_capacity, MIN_GROWN_STRING_BUFFER_CAPACITY);
        while (capacity < needed_size) {
            if (capacity > std::numeric_limits<size_t>::max() / 2u) {
                return needed_size;
            }

            capacity *= 2u;
        }
        return capacity;
    }

    [[nodiscard]] auto pointer_in_range(
        void const* pointer, void const* begin, size_t size, size_t width, size_t* out_offset
    ) -> bool {
        if (pointer == nullptr || begin == nullptr || width > size) {
            return false;
        }

        uintptr_t const address = reinterpret_cast<uintptr_t>(pointer);
        uintptr_t const first = reinterpret_cast<uintptr_t>(begin);
        if (address < first || address - first > size - width) {
            return false;
        }

        *out_offset = static_cast<size_t>(address - first);
        return true;
    }

} // namespace

auto StringBuffer::init(size_t capacity, MemoryResource* resource) -> bool {
    *this = {};

    DEBUG_ASSERT(resource != nullptr);
    if (resource == nullptr) {
        return false;
    }
    m_resource = resource;

    if (capacity == 0u) {
        return true;
    }

    size_t allocation_size = 0u;
    if (add_overflows(capacity, 1u, &allocation_size)) {
        *this = {};
        return false;
    }

    m_data = static_cast<char*>(m_resource->allocate(allocation_size, alignof(char)));
    if (m_data == nullptr) {
        *this = {};
        return false;
    }
    m_capacity = capacity;
    write_terminator();
    return true;
}

auto StringBuffer::copy_from(StringBuffer const& other, MemoryResource* resource) -> bool {
    if (this == &other) {
        return true;
    }
    if (!init(other.m_size, resource)) {
        return false;
    }
    size_t const copied = write_bytes(other.data(), other.m_size);
    DEBUG_ASSERT(copied == other.m_size);
    return copied == other.m_size;
}

auto StringBuffer::init_with_backing(char* backing, size_t capacity) -> void {
    DEBUG_ASSERT(backing != nullptr);
    DEBUG_ASSERT(capacity > 0u);

    *this = {};

    m_data = backing;
    m_capacity = capacity;
    write_terminator();
}

auto StringBuffer::reset() -> void {
    m_size = 0u;
    write_terminator();
}

auto StringBuffer::reserve(size_t capacity) -> void {
    if (capacity <= m_capacity) {
        return;
    }
    if (fixed_capacity()) {
        return;
    }

    size_t allocation_size = 0u;
    bool const overflowed = add_overflows(capacity, 1u, &allocation_size);
    if (overflowed) {
        return;
    }

    DEBUG_ASSERT(m_resource != nullptr);
    if (m_resource == nullptr) {
        return;
    }

    char* const new_data = static_cast<char*>(m_resource->allocate(allocation_size, alignof(char)));
    if (new_data == nullptr) {
        return;
    }

    if (m_data != nullptr && m_size != 0u) {
        std::memcpy(new_data, m_data, m_size);
    }

    m_data = new_data;
    m_capacity = capacity;
    write_terminator();
}

auto StringBuffer::resize(size_t size, char fill) -> bool {
    if (size > m_capacity) {
        if (fixed_capacity()) {
            return false;
        }

        reserve(grown_capacity(m_capacity, size));
    }

    if (size > m_capacity) {
        return false;
    }

    if (size > m_size) {
        std::memset(m_data + m_size, static_cast<int>(fill), size - m_size);
    }

    m_size = size;
    write_terminator();
    return true;
}

auto StringBuffer::write_byte(char value) -> size_t {
    return write_bytes(&value, 1u);
}

auto StringBuffer::write_bytes(void const* data, size_t size) -> size_t {
    if (size == 0u || data == nullptr) {
        return 0u;
    }

    size_t internal_source_offset = 0u;
    bool const internal_source =
        pointer_in_range(data, m_data, m_size, size, &internal_source_offset);

    size_t const write_size = prepare_write(size);
    if (write_size == 0u) {
        return 0u;
    }

    char const* source = static_cast<char const*>(data);
    if (internal_source) {
        source = m_data + internal_source_offset;
    }

    std::memmove(m_data + m_size, source, write_size);
    m_size += write_size;
    write_terminator();
    return write_size;
}

auto StringBuffer::write_string(StrRef text) -> size_t {
    return write_bytes(text.data(), text.size());
}

auto StringBuffer::write_fill(char value, size_t count) -> size_t {
    if (count == 0u) {
        return 0u;
    }

    size_t const write_size = prepare_write(count);
    if (write_size == 0u) {
        return 0u;
    }

    std::memset(m_data + m_size, static_cast<int>(static_cast<unsigned char>(value)), write_size);
    m_size += write_size;
    write_terminator();
    return write_size;
}

auto StringBuffer::pop_byte() -> char {
    DEBUG_ASSERT(m_size > 0u);
    m_size -= 1u;
    char const value = m_data[m_size];
    write_terminator();
    return value;
}

auto StringBuffer::c_str() -> char const* {
    if (m_data == nullptr) {
        return "";
    }

    if (!has_terminator_space()) {
        return nullptr;
    }

    m_data[m_size] = '\0';
    return m_data;
}

auto StringBuffer::str() const -> StrRef {
    return StrRef(data(), m_size);
}

StringBuffer::operator StrRef() const {
    return str();
}

auto StringBuffer::data() -> char* {
    return m_data;
}

auto StringBuffer::data() const -> char const* {
    return m_data != nullptr ? m_data : "";
}

auto StringBuffer::size() const -> size_t {
    return m_size;
}

auto StringBuffer::capacity() const -> size_t {
    return m_capacity;
}

auto StringBuffer::space() const -> size_t {
    return m_capacity - m_size;
}

auto StringBuffer::empty() const -> bool {
    return m_size == 0u;
}

auto StringBuffer::fixed_capacity() const -> bool {
    return m_data != nullptr && m_resource == nullptr;
}

auto StringBuffer::has_terminator_space() const -> bool {
    if (m_data == nullptr) {
        return false;
    }

    if (fixed_capacity()) {
        return m_size < m_capacity;
    }

    return m_size <= m_capacity;
}

auto StringBuffer::prepare_write(size_t size) -> size_t {
    size_t needed_size = 0u;
    if (add_overflows(m_size, size, &needed_size)) {
        return 0u;
    }

    if (needed_size <= m_capacity) {
        return size;
    }

    if (fixed_capacity()) {
        DEBUG_ASSERT(m_size <= m_capacity);
        return m_capacity - m_size;
    }

    reserve(grown_capacity(m_capacity, needed_size));
    return needed_size <= m_capacity ? size : 0u;
}

auto StringBuffer::write_terminator() -> void {
    if (has_terminator_space()) {
        m_data[m_size] = '\0';
    }
}
