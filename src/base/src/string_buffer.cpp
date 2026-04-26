#include <algorithm>
#include <base/string_buffer.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory_resource>

namespace {

    inline constexpr size_t MIN_GROWN_STRING_BUFFER_CAPACITY = 64u;

    [[nodiscard]] auto add_overflows(size_t lhs, size_t rhs, size_t* out) noexcept -> bool {
        if (lhs > std::numeric_limits<size_t>::max() - rhs) {
            return true;
        }

        *out = lhs + rhs;
        return false;
    }

    [[nodiscard]] auto grown_capacity(size_t current_capacity, size_t needed_size) noexcept
        -> size_t {
        size_t capacity = std::max(current_capacity, MIN_GROWN_STRING_BUFFER_CAPACITY);

        while (capacity < needed_size) {
            if (capacity > std::numeric_limits<size_t>::max() / 2u) {
                return needed_size;
            }

            capacity *= 2u;
        }

        return capacity;
    }

    [[nodiscard]] auto default_memory_resource() noexcept -> MemoryResource* {
        return std::pmr::get_default_resource();
    }

    [[nodiscard]] auto pointer_in_range(void const* pointer,
                                        void const* begin,
                                        size_t size,
                                        size_t width,
                                        size_t* out_offset) noexcept -> bool {
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

StringBuffer::StringBuffer(StringBuffer&& other) noexcept {
    move_from(other);
}

auto StringBuffer::operator=(StringBuffer&& other) noexcept -> StringBuffer& {
    if (this != &other) {
        destroy();
        move_from(other);
    }

    return *this;
}

auto StringBuffer::init(size_t capacity, MemoryResource* resource) noexcept -> bool {
    if (m_initialized) {
        return false;
    }

    m_resource = resource != nullptr ? resource : default_memory_resource();
    m_initialized = true;
    m_fixed_capacity = false;

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
    m_allocation_size = allocation_size;
    write_terminator();
    return true;
}

auto StringBuffer::init_with_backing(char* backing, size_t capacity) noexcept -> bool {
    if (m_initialized || (backing == nullptr && capacity != 0u)) {
        return false;
    }

    m_data = backing;
    m_size = 0u;
    m_capacity = capacity;
    m_allocation_size = capacity;
    m_resource = nullptr;
    m_initialized = true;
    m_fixed_capacity = true;
    write_terminator();
    return true;
}

auto StringBuffer::destroy() noexcept -> void {
    if (m_data != nullptr && !m_fixed_capacity && m_resource != nullptr) {
        m_resource->deallocate(m_data, m_allocation_size, alignof(char));
    }

    m_data = nullptr;
    m_size = 0u;
    m_capacity = 0u;
    m_allocation_size = 0u;
    m_resource = nullptr;
    m_initialized = false;
    m_fixed_capacity = false;
}

auto StringBuffer::reset() noexcept -> void {
    m_size = 0u;
    write_terminator();
}

auto StringBuffer::reserve(size_t capacity) noexcept -> bool {
    if (capacity <= m_capacity) {
        return true;
    }

    if (m_fixed_capacity || !ensure_dynamic_resource()) {
        return false;
    }

    size_t allocation_size = 0u;
    if (add_overflows(capacity, 1u, &allocation_size)) {
        return false;
    }

    char* const new_data = static_cast<char*>(m_resource->allocate(allocation_size, alignof(char)));

    if (m_data != nullptr && m_size != 0u) {
        std::memcpy(new_data, m_data, m_size);
    }

    if (m_data != nullptr) {
        m_resource->deallocate(m_data, m_allocation_size, alignof(char));
    }

    m_data = new_data;
    m_capacity = capacity;
    m_allocation_size = allocation_size;
    write_terminator();
    return true;
}

auto StringBuffer::resize(size_t size, char fill) noexcept -> bool {
    if (size <= m_size) {
        m_size = size;
        write_terminator();
        return true;
    }

    if (!grow_to_fit(size)) {
        return false;
    }

    std::memset(m_data + m_size, static_cast<int>(static_cast<unsigned char>(fill)), size - m_size);
    m_size = size;
    write_terminator();
    return true;
}

auto StringBuffer::truncate(size_t size) noexcept -> void {
    m_size = std::min(size, m_size);
    write_terminator();
}

auto StringBuffer::write_byte(char value) noexcept -> size_t {
    size_t needed_size = 0u;
    if (add_overflows(m_size, 1u, &needed_size) || !grow_to_fit(needed_size)) {
        return 0u;
    }

    m_data[m_size] = value;
    m_size = needed_size;
    write_terminator();
    return 1u;
}

auto StringBuffer::write_bytes(void const* data, size_t size) noexcept -> size_t {
    if (data == nullptr || size == 0u) {
        return 0u;
    }

    size_t needed_size = 0u;
    if (add_overflows(m_size, size, &needed_size)) {
        return 0u;
    }

    size_t internal_source_offset = 0u;
    bool const internal_source =
        pointer_in_range(data, m_data, m_size, size, &internal_source_offset);

    size_t write_size = size;
    if (needed_size > m_capacity) {
        if (m_fixed_capacity) {
            write_size = m_capacity - m_size;
            needed_size = m_capacity;
        } else if (!grow_to_fit(needed_size)) {
            return 0u;
        }
    }

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

auto StringBuffer::write_string(StrRef text) noexcept -> size_t {
    return write_bytes(text.data(), text.size());
}

auto StringBuffer::write_fill(char value, size_t count) noexcept -> size_t {
    if (count == 0u) {
        return 0u;
    }

    size_t needed_size = 0u;
    if (add_overflows(m_size, count, &needed_size)) {
        return 0u;
    }

    size_t write_size = count;
    if (needed_size > m_capacity) {
        if (m_fixed_capacity) {
            write_size = m_capacity - m_size;
            needed_size = m_capacity;
        } else if (!grow_to_fit(needed_size)) {
            return 0u;
        }
    }

    if (write_size == 0u) {
        return 0u;
    }

    std::memset(m_data + m_size, static_cast<int>(static_cast<unsigned char>(value)), write_size);
    m_size += write_size;
    write_terminator();
    return write_size;
}

auto StringBuffer::append(char value) noexcept -> bool {
    return write_byte(value) == 1u;
}

auto StringBuffer::append(StrRef text) noexcept -> bool {
    return write_string(text) == text.size();
}

auto StringBuffer::pop_byte() noexcept -> char {
    if (m_size == 0u) {
        return '\0';
    }

    m_size -= 1u;
    char const value = m_data[m_size];
    write_terminator();
    return value;
}

auto StringBuffer::c_str() noexcept -> char const* {
    if (m_data == nullptr) {
        return "";
    }

    if (m_allocation_size <= m_size) {
        return nullptr;
    }

    m_data[m_size] = '\0';
    return m_data;
}

auto StringBuffer::str() const noexcept -> StrRef {
    return StrRef(data(), m_size);
}

auto StringBuffer::view() const noexcept -> StrRef {
    return str();
}

StringBuffer::operator StrRef() const noexcept {
    return str();
}

auto StringBuffer::data() noexcept -> char* {
    return m_data;
}

auto StringBuffer::data() const noexcept -> char const* {
    return m_data != nullptr ? m_data : "";
}

auto StringBuffer::size() const noexcept -> size_t {
    return m_size;
}

auto StringBuffer::capacity() const noexcept -> size_t {
    return m_capacity;
}

auto StringBuffer::space() const noexcept -> size_t {
    return m_capacity - m_size;
}

auto StringBuffer::empty() const noexcept -> bool {
    return m_size == 0u;
}

auto StringBuffer::initialized() const noexcept -> bool {
    return m_initialized;
}

auto StringBuffer::fixed_capacity() const noexcept -> bool {
    return m_fixed_capacity;
}

auto StringBuffer::resource() const noexcept -> MemoryResource* {
    return m_resource;
}

auto StringBuffer::ensure_dynamic_resource() noexcept -> bool {
    if (!m_initialized) {
        m_resource = default_memory_resource();
        m_initialized = true;
        m_fixed_capacity = false;
        return true;
    }

    return !m_fixed_capacity && m_resource != nullptr;
}

auto StringBuffer::grow_to_fit(size_t needed_size) noexcept -> bool {
    if (needed_size <= m_capacity) {
        return true;
    }

    if (m_fixed_capacity) {
        return false;
    }

    return reserve(grown_capacity(m_capacity, needed_size));
}

auto StringBuffer::write_terminator() noexcept -> void {
    if (m_data != nullptr && m_allocation_size > m_size) {
        m_data[m_size] = '\0';
    }
}

auto StringBuffer::move_from(StringBuffer& other) noexcept -> void {
    m_data = other.m_data;
    m_size = other.m_size;
    m_capacity = other.m_capacity;
    m_allocation_size = other.m_allocation_size;
    m_resource = other.m_resource;
    m_initialized = other.m_initialized;
    m_fixed_capacity = other.m_fixed_capacity;

    other.m_data = nullptr;
    other.m_size = 0u;
    other.m_capacity = 0u;
    other.m_allocation_size = 0u;
    other.m_resource = nullptr;
    other.m_initialized = false;
    other.m_fixed_capacity = false;
}
