#include <algorithm>
#include <base/assert.h>
#include <base/string_buffer.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory_resource>

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

    [[nodiscard]] auto default_memory_resource() -> MemoryResource* {
        return std::pmr::get_default_resource();
    }

    [[nodiscard]] auto dynamic_allocation_size(size_t capacity) -> size_t {
        size_t allocation_size = 0u;
        bool const overflowed = add_overflows(capacity, 1u, &allocation_size);
        ASSERT(!overflowed);
        return allocation_size;
    }

    [[nodiscard]] auto pointer_in_range(void const* pointer,
                                        void const* begin,
                                        size_t size,
                                        size_t width,
                                        size_t* out_offset) -> bool {
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

StringBuffer::~StringBuffer() {
    destroy();
}

StringBuffer::StringBuffer(StringBuffer&& other) {
    move_from(other);
}

auto StringBuffer::operator=(StringBuffer&& other) -> StringBuffer& {
    if (this != &other) {
        destroy();
        move_from(other);
    }

    return *this;
}

auto StringBuffer::init(size_t capacity, MemoryResource* resource) -> bool {
    destroy();

    m_resource = resource != nullptr ? resource : default_memory_resource();

    if (capacity == 0u) {
        return true;
    }

    size_t allocation_size = 0u;
    if (add_overflows(capacity, 1u, &allocation_size)) {
        destroy();
        return false;
    }

    m_data = static_cast<char*>(m_resource->allocate(allocation_size, alignof(char)));
    m_capacity = capacity;
    write_terminator();
    return true;
}

auto StringBuffer::init_with_backing(char* backing, size_t capacity) -> void {
    ASSERT(backing != nullptr);
    ASSERT(capacity > 0u);

    destroy();

    m_data = backing;
    m_capacity = capacity;
    write_terminator();
}

auto StringBuffer::destroy() -> void {
    if (m_data != nullptr && m_resource != nullptr) {
        m_resource->deallocate(m_data, dynamic_allocation_size(m_capacity), alignof(char));
    }

    m_data = nullptr;
    m_size = 0u;
    m_capacity = 0u;
    m_resource = nullptr;
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

    if (m_resource == nullptr) {
        m_resource = default_memory_resource();
    }

    char* const new_data = static_cast<char*>(m_resource->allocate(allocation_size, alignof(char)));

    if (m_data != nullptr && m_size != 0u) {
        std::memcpy(new_data, m_data, m_size);
    }

    if (m_data != nullptr) {
        m_resource->deallocate(m_data, dynamic_allocation_size(m_capacity), alignof(char));
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
    ASSERT(m_size > 0u);
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
        ASSERT(m_size <= m_capacity);
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

auto StringBuffer::move_from(StringBuffer& other) -> void {
    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_capacity;
    m_resource = other.m_resource;

    other.m_data = nullptr;
    other.m_size = 0u;
    other.m_capacity = 0u;
    other.m_resource = nullptr;
}
