#pragma once

#include "assert.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>

template <typename Char, size_t N>
[[nodiscard]] constexpr auto array_size_without_terminator(Char const (&text)[N]) noexcept
    -> size_t {
    if constexpr (N == 0u) {
        return 0u;
    } else {
        return text[N - 1u] == Char{} ? N - 1u : N;
    }
}

constexpr auto cstr_len_constexpr(char const* s) noexcept -> size_t {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

inline auto cstr_len(char const* s) noexcept -> size_t {
    if (std::is_constant_evaluated()) {
        return cstr_len_constexpr(s);
    } else {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_strlen(s);
#else
        return std::strlen(s);
#endif
    }
}

[[nodiscard]] constexpr auto pointer_range_size(char const* begin, char const* end) noexcept
    -> size_t {
    ASSERT(begin != nullptr && end != nullptr && begin <= end);
    return static_cast<size_t>(end - begin);
}

[[nodiscard]] constexpr auto to_ascii_lower(char value) noexcept -> char {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] constexpr auto is_ascii_whitespace(char value) noexcept -> bool {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' ||
           value == '\v';
}

[[nodiscard]] constexpr auto is_ascii_digit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr auto is_ascii_alpha(char value) noexcept -> bool {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] constexpr auto is_ascii_alphanumeric(char value) noexcept -> bool {
    return is_ascii_alpha(value) || is_ascii_digit(value);
}

class StrRef {
  public:
    class SplitIterator;
    class SplitRange;
    class LineIterator;
    class LinesRange;
    class SplitAsciiWhitespaceIterator;
    class SplitAsciiWhitespaceRange;

    struct SplitOnce;

    static constexpr size_t NPOS = static_cast<size_t>(-1);

    constexpr StrRef() noexcept = default;
    constexpr StrRef(std::nullptr_t) noexcept = delete;

    template <size_t N>
    constexpr StrRef(char const (&text)[N]) noexcept
        : m_data(text), m_size(array_size_without_terminator(text)) {}

    template <size_t N>
    constexpr StrRef(char8_t const (&text)[N]) noexcept
        : m_data(reinterpret_cast<char const*>(text)), m_size(array_size_without_terminator(text)) {
    }

    constexpr StrRef(char const* text) noexcept
        : m_data(text != nullptr ? text : ""), m_size(cstr_len_constexpr(text)) {}

    constexpr StrRef(char* text) noexcept : StrRef(static_cast<char const*>(text)) {}

    constexpr StrRef(char const* data, size_t size) noexcept
        : m_data(data != nullptr ? data : ""), m_size(data != nullptr ? size : 0u) {}

    constexpr StrRef(char* data, size_t size) noexcept
        : StrRef(static_cast<char const*>(data), size) {}

    constexpr StrRef(char const* begin, char const* end) noexcept
        : StrRef(begin, pointer_range_size(begin, end)) {}

    constexpr StrRef(char* begin, char* end) noexcept
        : StrRef(static_cast<char const*>(begin), static_cast<char const*>(end)) {}

    StrRef(std::byte const* data, size_t size) noexcept
        : StrRef(reinterpret_cast<char const*>(data), size) {}

    StrRef(std::byte* data, size_t size) noexcept
        : StrRef(static_cast<std::byte const*>(data), size) {}

    StrRef(uint8_t* data, size_t size) noexcept : StrRef(static_cast<uint8_t const*>(data), size) {}

    StrRef(uint8_t const* data, size_t size) noexcept
        : StrRef(reinterpret_cast<char const*>(data), size) {}

    StrRef(char8_t* data, size_t size) noexcept : StrRef(static_cast<char8_t const*>(data), size) {}

    StrRef(char8_t const* data, size_t size) noexcept
        : StrRef(reinterpret_cast<char const*>(data), size) {}

    constexpr StrRef(std::string_view view) noexcept : StrRef(view.data(), view.size()) {}

    StrRef(std::string const& text) noexcept : StrRef(text.data(), text.size()) {}

    template <size_t Extent>
    constexpr StrRef(std::span<char const, Extent> bytes) noexcept
        : StrRef(bytes.data(), bytes.size()) {}

    template <size_t Extent>
    constexpr StrRef(std::span<char, Extent> bytes) noexcept : StrRef(bytes.data(), bytes.size()) {}

    template <size_t Extent>
    StrRef(std::span<uint8_t const, Extent> bytes) noexcept : StrRef(bytes.data(), bytes.size()) {}

    template <size_t Extent>
    StrRef(std::span<uint8_t, Extent> bytes) noexcept : StrRef(bytes.data(), bytes.size()) {}

    [[nodiscard]] static constexpr auto from_parts(char const* data, size_t size) noexcept
        -> StrRef {
        return StrRef(data, size);
    }

    [[nodiscard]] static auto from_bytes(void const* data, size_t size) noexcept -> StrRef {
        return StrRef(static_cast<char const*>(data), size);
    }

    [[nodiscard]] constexpr auto data() const noexcept -> char const* {
        return m_data;
    }

    [[nodiscard]] constexpr auto size() const noexcept -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto length() const noexcept -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto byte_size() const noexcept -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return m_size == 0u;
    }

    [[nodiscard]] constexpr auto begin() const noexcept -> char const* {
        return m_data;
    }

    [[nodiscard]] constexpr auto end() const noexcept -> char const* {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr auto cbegin() const noexcept -> char const* {
        return begin();
    }

    [[nodiscard]] constexpr auto cend() const noexcept -> char const* {
        return end();
    }

    [[nodiscard]] constexpr auto operator[](size_t index) const noexcept -> char {
        return m_data[index];
    }

    [[nodiscard]] constexpr auto front() const noexcept -> char {
        return m_data[0];
    }

    [[nodiscard]] constexpr auto back() const noexcept -> char {
        return m_data[m_size - 1u];
    }

    [[nodiscard]] constexpr auto front_or(char fallback) const noexcept -> char {
        return empty() ? fallback : front();
    }

    [[nodiscard]] constexpr auto back_or(char fallback) const noexcept -> char {
        return empty() ? fallback : back();
    }

    [[nodiscard]] constexpr auto to_string_view() const noexcept -> std::string_view {
        return std::string_view(m_data, m_size);
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return to_string_view();
    }

    [[nodiscard]] auto bytes() const noexcept -> std::span<uint8_t const> {
        return std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(m_data), m_size);
    }

    [[nodiscard]] auto to_string() const -> std::string {
        return std::string(m_data, m_size);
    }

    [[nodiscard]] auto copy_to(char* target, size_t target_size) const noexcept -> size_t {
        if (target == nullptr || target_size == 0u) {
            return 0u;
        }
        size_t const copied_size = std::min(m_size, target_size);
        std::memcpy(target, m_data, copied_size);
        return copied_size;
    }

    [[nodiscard]] constexpr auto substr(size_t offset, size_t count = NPOS) const noexcept
        -> StrRef {
        size_t const clamped_offset = std::min(offset, m_size);
        size_t const remaining_size = m_size - clamped_offset;
        size_t const clamped_count = count < remaining_size ? count : remaining_size;
        return StrRef(m_data + clamped_offset, clamped_count);
    }

    [[nodiscard]] constexpr auto slice(size_t offset, size_t count = NPOS) const noexcept
        -> StrRef {
        return substr(offset, count);
    }

    [[nodiscard]] constexpr auto prefix(size_t count) const noexcept -> StrRef {
        return substr(0u, count);
    }

    [[nodiscard]] constexpr auto suffix(size_t count) const noexcept -> StrRef {
        size_t const clamped_count = std::min(count, m_size);
        return substr(m_size - clamped_count, clamped_count);
    }

    [[nodiscard]] constexpr auto drop_prefix(size_t count) const noexcept -> StrRef {
        size_t const clamped_count = std::min(count, m_size);
        return substr(clamped_count);
    }

    [[nodiscard]] constexpr auto drop_suffix(size_t count) const noexcept -> StrRef {
        size_t const clamped_count = std::min(count, m_size);
        return substr(0u, m_size - clamped_count);
    }

    constexpr auto remove_prefix(size_t count) noexcept -> void {
        *this = drop_prefix(count);
    }

    constexpr auto remove_suffix(size_t count) noexcept -> void {
        *this = drop_suffix(count);
    }

    [[nodiscard]] constexpr auto compare(StrRef other) const noexcept -> int {
        size_t const shared_size = std::min(m_size, other.m_size);
        for (size_t index = 0u; index < shared_size; ++index) {
            auto const lhs = m_data[index];
            auto const rhs = other.m_data[index];
            if (lhs < rhs) {
                return -1;
            }
            if (lhs > rhs) {
                return 1;
            }
        }
        if (m_size < other.m_size) {
            return -1;
        }
        if (m_size > other.m_size) {
            return 1;
        }
        return 0;
    }

    [[nodiscard]] constexpr auto equals(StrRef other) const noexcept -> bool {
        return compare(other) == 0;
    }

    [[nodiscard]] constexpr auto equals_ignore_ascii_case(StrRef other) const noexcept -> bool {
        if (m_size != other.m_size) {
            return false;
        }
        for (size_t index = 0u; index < m_size; ++index) {
            if (to_ascii_lower(m_data[index]) != to_ascii_lower(other.m_data[index])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto find(char needle, size_t offset = 0u) const noexcept -> size_t {
        if (offset >= m_size) {
            return NPOS;
        }
        for (size_t index = offset; index < m_size; ++index) {
            if (m_data[index] == needle) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto find(StrRef needle, size_t offset = 0u) const noexcept -> size_t {
        if (needle.empty()) {
            return std::min(offset, m_size);
        }
        if (offset >= m_size || needle.m_size > m_size - offset) {
            return NPOS;
        }
        size_t const last_index = m_size - needle.m_size;
        for (size_t index = offset; index <= last_index; ++index) {
            if (matches_at(index, needle)) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto rfind(char needle, size_t offset = NPOS) const noexcept -> size_t {
        if (empty()) {
            return NPOS;
        }
        size_t index = offset < m_size ? offset + 1u : m_size;
        while (index > 0u) {
            --index;
            if (m_data[index] == needle) {
                return index;
            }
        }

        return NPOS;
    }

    [[nodiscard]] constexpr auto rfind(StrRef needle, size_t offset = NPOS) const noexcept
        -> size_t {
        if (needle.empty()) {
            return std::min(offset, m_size);
        }
        if (needle.m_size > m_size) {
            return NPOS;
        }

        size_t const max_start = m_size - needle.m_size;
        size_t index = offset < max_start ? offset + 1u : max_start + 1u;
        while (index > 0u) {
            --index;
            if (matches_at(index, needle)) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto find_first_of(StrRef needles, size_t offset = 0u) const noexcept
        -> size_t {
        if (needles.empty() || offset >= m_size) {
            return NPOS;
        }
        for (size_t index = offset; index < m_size; ++index) {
            if (needles.contains(m_data[index])) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto find_first_not_of(StrRef needles,
                                                   size_t offset = 0u) const noexcept -> size_t {
        if (offset >= m_size) {
            return NPOS;
        }
        for (size_t index = offset; index < m_size; ++index) {
            if (!needles.contains(m_data[index])) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto find_last_of(StrRef needles, size_t offset = NPOS) const noexcept
        -> size_t {
        if (needles.empty() || empty()) {
            return NPOS;
        }
        size_t index = offset < m_size ? offset + 1u : m_size;
        while (index > 0u) {
            --index;

            if (needles.contains(m_data[index])) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto find_last_not_of(StrRef needles,
                                                  size_t offset = NPOS) const noexcept -> size_t {
        if (empty()) {
            return NPOS;
        }
        size_t index = offset < m_size ? offset + 1u : m_size;
        while (index > 0u) {
            --index;

            if (!needles.contains(m_data[index])) {
                return index;
            }
        }
        return NPOS;
    }

    [[nodiscard]] constexpr auto contains(char needle) const noexcept -> bool {
        return find(needle) != NPOS;
    }

    [[nodiscard]] constexpr auto contains(StrRef needle) const noexcept -> bool {
        return find(needle) != NPOS;
    }

    [[nodiscard]] constexpr auto starts_with(char prefix) const noexcept -> bool {
        return !empty() && front() == prefix;
    }

    [[nodiscard]] constexpr auto starts_with(StrRef prefix) const noexcept -> bool {
        return matches_at(0u, prefix);
    }

    [[nodiscard]] constexpr auto ends_with(char suffix) const noexcept -> bool {
        return !empty() && back() == suffix;
    }

    [[nodiscard]] constexpr auto ends_with(StrRef suffix) const noexcept -> bool {
        return suffix.m_size <= m_size && matches_at(m_size - suffix.m_size, suffix);
    }

    [[nodiscard]] constexpr auto starts_with_ignore_ascii_case(StrRef prefix) const noexcept
        -> bool {
        return prefix.m_size <= m_size &&
               prefix.equals_ignore_ascii_case(substr(0u, prefix.m_size));
    }

    [[nodiscard]] constexpr auto ends_with_ignore_ascii_case(StrRef suffix) const noexcept -> bool {
        return suffix.m_size <= m_size &&
               suffix.equals_ignore_ascii_case(this->suffix(suffix.m_size));
    }

    [[nodiscard]] constexpr auto trim_start() const noexcept -> StrRef {
        size_t index = 0u;
        while (index < m_size && is_ascii_whitespace(m_data[index])) {
            ++index;
        }
        return substr(index);
    }

    [[nodiscard]] constexpr auto trim_end() const noexcept -> StrRef {
        size_t index = m_size;
        while (index > 0u && is_ascii_whitespace(m_data[index - 1u])) {
            --index;
        }
        return substr(0u, index);
    }

    [[nodiscard]] constexpr auto trim() const noexcept -> StrRef {
        return trim_start().trim_end();
    }

    [[nodiscard]] constexpr auto trim_start_matches(char needle) const noexcept -> StrRef {
        size_t index = 0u;
        while (index < m_size && m_data[index] == needle) {
            ++index;
        }
        return substr(index);
    }

    [[nodiscard]] constexpr auto trim_end_matches(char needle) const noexcept -> StrRef {
        size_t index = m_size;
        while (index > 0u && m_data[index - 1u] == needle) {
            --index;
        }
        return substr(0u, index);
    }

    [[nodiscard]] constexpr auto trim_matches(char needle) const noexcept -> StrRef {
        return trim_start_matches(needle).trim_end_matches(needle);
    }

    [[nodiscard]] constexpr auto strip_prefix(StrRef prefix) const noexcept -> StrRef {
        return starts_with(prefix) ? drop_prefix(prefix.m_size) : StrRef();
    }

    [[nodiscard]] constexpr auto strip_suffix(StrRef suffix) const noexcept -> StrRef {
        return ends_with(suffix) ? drop_suffix(suffix.m_size) : StrRef();
    }

    [[nodiscard]] constexpr auto strip_prefix(StrRef prefix, StrRef* out) const noexcept -> bool {
        if (!starts_with(prefix)) {
            return false;
        }
        if (out != nullptr) {
            *out = drop_prefix(prefix.m_size);
        }
        return true;
    }

    [[nodiscard]] constexpr auto strip_suffix(StrRef suffix, StrRef* out) const noexcept -> bool {
        if (!ends_with(suffix)) {
            return false;
        }
        if (out != nullptr) {
            *out = drop_suffix(suffix.m_size);
        }
        return true;
    }

    constexpr auto consume_prefix(StrRef prefix) noexcept -> bool {
        if (!starts_with(prefix)) {
            return false;
        }
        remove_prefix(prefix.m_size);
        return true;
    }

    constexpr auto consume_suffix(StrRef suffix) noexcept -> bool {
        if (!ends_with(suffix)) {
            return false;
        }
        remove_suffix(suffix.m_size);
        return true;
    }

    [[nodiscard]] constexpr auto split_once(char delimiter) const noexcept -> SplitOnce;
    [[nodiscard]] constexpr auto split_once(StrRef delimiter) const noexcept -> SplitOnce;
    [[nodiscard]] constexpr auto rsplit_once(char delimiter) const noexcept -> SplitOnce;
    [[nodiscard]] constexpr auto rsplit_once(StrRef delimiter) const noexcept -> SplitOnce;

    [[nodiscard]] constexpr auto is_ascii() const noexcept -> bool {
        for (size_t index = 0u; index < m_size; ++index) {
            if ((static_cast<uint8_t>(m_data[index]) & 0x80u) != 0u) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] constexpr auto is_ascii_whitespace_only() const noexcept -> bool {
        for (size_t index = 0u; index < m_size; ++index) {
            if (!is_ascii_whitespace(m_data[index])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto is_ascii_alphanumeric_only() const noexcept -> bool {
        for (size_t index = 0u; index < m_size; ++index) {
            if (!is_ascii_alphanumeric(m_data[index])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto count(char needle) const noexcept -> size_t {
        size_t match_count = 0u;
        for (size_t index = 0u; index < m_size; ++index) {
            if (m_data[index] == needle) {
                ++match_count;
            }
        }
        return match_count;
    }

    [[nodiscard]] constexpr auto count(StrRef needle) const noexcept -> size_t {
        if (needle.empty()) {
            return 0u;
        }

        size_t match_count = 0u;
        size_t offset = 0u;

        while (offset < m_size) {
            size_t const index = find(needle, offset);

            if (index == NPOS) {
                break;
            }

            ++match_count;
            offset = index + needle.m_size;
        }

        return match_count;
    }

    [[nodiscard]] constexpr auto hash64() const noexcept -> uint64_t {
        uint64_t hash = 14695981039346656037ull;

        for (size_t index = 0u; index < m_size; ++index) {
            hash ^= static_cast<uint64_t>(m_data[index]);
            hash *= 1099511628211ull;
        }

        return hash;
    }

    [[nodiscard]] constexpr auto split(StrRef delimiter) const noexcept -> SplitRange;
    [[nodiscard]] constexpr auto lines() const noexcept -> LinesRange;
    [[nodiscard]] constexpr auto split_ascii_whitespace() const noexcept
        -> SplitAsciiWhitespaceRange;

    [[nodiscard]] friend constexpr auto operator==(StrRef lhs, StrRef rhs) noexcept -> bool {
        return lhs.equals(rhs);
    }

    [[nodiscard]] friend constexpr auto operator<=>(StrRef lhs, StrRef rhs) noexcept
        -> std::strong_ordering {
        int const result = lhs.compare(rhs);
        if (result < 0) {
            return std::strong_ordering::less;
        }
        if (result > 0) {
            return std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

  private:
    [[nodiscard]] constexpr auto matches_at(size_t offset, StrRef needle) const noexcept -> bool {
        if (needle.m_size > m_size || offset > m_size - needle.m_size) {
            return false;
        }

        for (size_t index = 0u; index < needle.m_size; ++index) {
            if (m_data[offset + index] != needle.m_data[index]) {
                return false;
            }
        }

        return true;
    }

  private:
    char const* m_data = "";
    size_t m_size = 0u;
};

struct StrRef::SplitOnce {
    StrRef before;
    StrRef after;
    bool found;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return found;
    }
};

class StrRef::SplitIterator {
  public:
    constexpr SplitIterator() noexcept = default;

    constexpr SplitIterator(StrRef text, StrRef delimiter) noexcept
        : m_next(text), m_delimiter(delimiter) {
        prepare_next();
    }

    [[nodiscard]] constexpr auto operator*() const noexcept -> StrRef {
        return m_current;
    }

    constexpr auto operator++() noexcept -> SplitIterator& {
        if (m_at_end) {
            return *this;
        }

        if (m_last) {
            m_current = StrRef();
            m_at_end = true;
            return *this;
        }

        prepare_next();
        return *this;
    }

    constexpr auto operator++(int) noexcept -> SplitIterator {
        SplitIterator copy = *this;
        ++(*this);
        return copy;
    }

    [[nodiscard]] friend constexpr auto operator==(SplitIterator const& lhs,
                                                   SplitIterator const& rhs) noexcept -> bool {
        if (lhs.m_at_end || rhs.m_at_end) {
            return lhs.m_at_end == rhs.m_at_end;
        }

        return lhs.m_current.data() == rhs.m_current.data() &&
               lhs.m_current.size() == rhs.m_current.size() &&
               lhs.m_next.data() == rhs.m_next.data() && lhs.m_next.size() == rhs.m_next.size();
    }

    [[nodiscard]] friend constexpr auto operator!=(SplitIterator const& lhs,
                                                   SplitIterator const& rhs) noexcept -> bool {
        return !(lhs == rhs);
    }

  private:
    StrRef m_current;
    StrRef m_next;
    StrRef m_delimiter;
    bool m_at_end = true;
    bool m_last = true;

    constexpr auto prepare_next() noexcept -> void {
        m_at_end = false;

        if (m_delimiter.empty()) {
            m_current = m_next;
            m_next = StrRef();
            m_last = true;
            return;
        }

        size_t const delimiter_index = m_next.find(m_delimiter);

        if (delimiter_index == NPOS) {
            m_current = m_next;
            m_next = StrRef();
            m_last = true;
            return;
        }

        m_current = m_next.prefix(delimiter_index);
        m_next = m_next.substr(delimiter_index + m_delimiter.size());
        m_last = false;
    }
};

class StrRef::SplitRange {
  public:
    constexpr SplitRange(StrRef text, StrRef delimiter) noexcept
        : m_text(text), m_delimiter(delimiter) {}

    [[nodiscard]] constexpr auto begin() const noexcept -> SplitIterator {
        return SplitIterator(m_text, m_delimiter);
    }

    [[nodiscard]] constexpr auto end() const noexcept -> SplitIterator {
        return SplitIterator();
    }

  private:
    StrRef m_text;
    StrRef m_delimiter;
};

class StrRef::LineIterator {
  public:
    constexpr LineIterator() noexcept = default;

    constexpr explicit LineIterator(StrRef text) noexcept : m_next(text) {
        prepare_next();
    }

    [[nodiscard]] constexpr auto operator*() const noexcept -> StrRef {
        return m_current;
    }

    constexpr auto operator++() noexcept -> LineIterator& {
        prepare_next();
        return *this;
    }

    constexpr auto operator++(int) noexcept -> LineIterator {
        LineIterator copy = *this;
        ++(*this);
        return copy;
    }

    [[nodiscard]] friend constexpr auto operator==(LineIterator const& lhs,
                                                   LineIterator const& rhs) noexcept -> bool {
        if (lhs.m_at_end || rhs.m_at_end) {
            return lhs.m_at_end == rhs.m_at_end;
        }

        return lhs.m_current.data() == rhs.m_current.data() &&
               lhs.m_current.size() == rhs.m_current.size() &&
               lhs.m_next.data() == rhs.m_next.data() && lhs.m_next.size() == rhs.m_next.size();
    }

    [[nodiscard]] friend constexpr auto operator!=(LineIterator const& lhs,
                                                   LineIterator const& rhs) noexcept -> bool {
        return !(lhs == rhs);
    }

  private:
    StrRef m_current;
    StrRef m_next;
    bool m_at_end = true;

    constexpr auto prepare_next() noexcept -> void {
        if (m_next.empty()) {
            m_current = StrRef();
            m_at_end = true;
            return;
        }

        m_at_end = false;
        size_t const newline_index = m_next.find('\n');

        if (newline_index == NPOS) {
            m_current = m_next;
            m_next = StrRef();
            return;
        }

        m_current = m_next.prefix(newline_index);

        if (m_current.ends_with('\r')) {
            m_current.remove_suffix(1u);
        }

        m_next = m_next.substr(newline_index + 1u);
    }
};

class StrRef::LinesRange {
  public:
    constexpr explicit LinesRange(StrRef text) noexcept : m_text(text) {}

    [[nodiscard]] constexpr auto begin() const noexcept -> LineIterator {
        return LineIterator(m_text);
    }

    [[nodiscard]] constexpr auto end() const noexcept -> LineIterator {
        return LineIterator();
    }

  private:
    StrRef m_text;
};

class StrRef::SplitAsciiWhitespaceIterator {
  public:
    constexpr SplitAsciiWhitespaceIterator() noexcept = default;

    constexpr explicit SplitAsciiWhitespaceIterator(StrRef text) noexcept : m_next(text) {
        prepare_next();
    }

    [[nodiscard]] constexpr auto operator*() const noexcept -> StrRef {
        return m_current;
    }

    constexpr auto operator++() noexcept -> SplitAsciiWhitespaceIterator& {
        prepare_next();
        return *this;
    }

    constexpr auto operator++(int) noexcept -> SplitAsciiWhitespaceIterator {
        SplitAsciiWhitespaceIterator copy = *this;
        ++(*this);
        return copy;
    }

    [[nodiscard]] friend constexpr auto operator==(SplitAsciiWhitespaceIterator const& lhs,
                                                   SplitAsciiWhitespaceIterator const& rhs) noexcept
        -> bool {
        if (lhs.m_at_end || rhs.m_at_end) {
            return lhs.m_at_end == rhs.m_at_end;
        }

        return lhs.m_current.data() == rhs.m_current.data() &&
               lhs.m_current.size() == rhs.m_current.size() &&
               lhs.m_next.data() == rhs.m_next.data() && lhs.m_next.size() == rhs.m_next.size();
    }

    [[nodiscard]] friend constexpr auto operator!=(SplitAsciiWhitespaceIterator const& lhs,
                                                   SplitAsciiWhitespaceIterator const& rhs) noexcept
        -> bool {
        return !(lhs == rhs);
    }

  private:
    StrRef m_current;
    StrRef m_next;
    bool m_at_end = true;

    constexpr auto prepare_next() noexcept -> void {
        m_next = m_next.trim_start();

        if (m_next.empty()) {
            m_current = StrRef();
            m_at_end = true;
            return;
        }

        m_at_end = false;
        size_t index = 0u;

        while (index < m_next.size() && !is_ascii_whitespace(m_next[index])) {
            ++index;
        }

        m_current = m_next.prefix(index);
        m_next = m_next.substr(index);
    }
};

class StrRef::SplitAsciiWhitespaceRange {
  public:
    constexpr explicit SplitAsciiWhitespaceRange(StrRef text) noexcept : m_text(text) {}

    [[nodiscard]] constexpr auto begin() const noexcept -> SplitAsciiWhitespaceIterator {
        return SplitAsciiWhitespaceIterator(m_text);
    }

    [[nodiscard]] constexpr auto end() const noexcept -> SplitAsciiWhitespaceIterator {
        return SplitAsciiWhitespaceIterator();
    }

  private:
    StrRef m_text;
};

[[nodiscard]] constexpr auto StrRef::split(StrRef delimiter) const noexcept -> StrRef::SplitRange {
    return SplitRange(*this, delimiter);
}

[[nodiscard]] constexpr auto StrRef::split_once(char delimiter) const noexcept
    -> StrRef::SplitOnce {
    size_t const index = find(delimiter);

    if (index == NPOS) {
        return SplitOnce{StrRef(), StrRef(), false};
    }

    return SplitOnce{prefix(index), substr(index + 1u), true};
}

[[nodiscard]] constexpr auto StrRef::split_once(StrRef delimiter) const noexcept
    -> StrRef::SplitOnce {
    size_t const index = find(delimiter);

    if (index == NPOS) {
        return SplitOnce{StrRef(), StrRef(), false};
    }

    return SplitOnce{prefix(index), substr(index + delimiter.size()), true};
}

[[nodiscard]] constexpr auto StrRef::rsplit_once(char delimiter) const noexcept
    -> StrRef::SplitOnce {
    size_t const index = rfind(delimiter);

    if (index == NPOS) {
        return SplitOnce{StrRef(), StrRef(), false};
    }

    return SplitOnce{prefix(index), substr(index + 1u), true};
}

[[nodiscard]] constexpr auto StrRef::rsplit_once(StrRef delimiter) const noexcept
    -> StrRef::SplitOnce {
    size_t const index = rfind(delimiter);

    if (index == NPOS) {
        return SplitOnce{StrRef(), StrRef(), false};
    }

    return SplitOnce{prefix(index), substr(index + delimiter.size()), true};
}

[[nodiscard]] constexpr auto StrRef::lines() const noexcept -> StrRef::LinesRange {
    return LinesRange(*this);
}

[[nodiscard]] constexpr auto StrRef::split_ascii_whitespace() const noexcept
    -> StrRef::SplitAsciiWhitespaceRange {
    return SplitAsciiWhitespaceRange(*this);
}

namespace std {
    template <> struct hash<StrRef> {
        [[nodiscard]] auto operator()(StrRef value) const noexcept -> size_t {
            return static_cast<size_t>(value.hash64());
        }
    };
} // namespace std
