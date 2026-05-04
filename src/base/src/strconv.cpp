#include <base/strconv.h>
#include <charconv>
#include <system_error>

namespace {

    [[nodiscard]] auto parse_ok(StrRef text, std::from_chars_result result) -> bool {
        return !text.empty() && result.ec == std::errc() && result.ptr == text.end();
    }

    [[nodiscard]] auto write_ok(char const* begin, std::to_chars_result result) -> StrRef {
        return result.ec == std::errc() ? StrRef(begin, static_cast<size_t>(result.ptr - begin))
                                        : StrRef();
    }

} // namespace

namespace base {

    [[nodiscard]] auto string_to_i64(StrRef text, int64_t& out_value) -> bool {
        int64_t value = 0;
        std::from_chars_result const result = std::from_chars(text.begin(), text.end(), value);
        if (!parse_ok(text, result)) {
            return false;
        }

        out_value = value;
        return true;
    }

    [[nodiscard]] auto string_to_u64(StrRef text, uint64_t& out_value) -> bool {
        uint64_t value = 0u;
        std::from_chars_result const result = std::from_chars(text.begin(), text.end(), value);
        if (!parse_ok(text, result)) {
            return false;
        }

        out_value = value;
        return true;
    }

    [[nodiscard]] auto string_to_f32(StrRef text, float& out_value) -> bool {
        float value = 0.0f;
        std::from_chars_result const result = std::from_chars(text.begin(), text.end(), value);
        if (!parse_ok(text, result)) {
            return false;
        }

        out_value = value;
        return true;
    }

    [[nodiscard]] auto string_to_f64(StrRef text, double& out_value) -> bool {
        double value = 0.0;
        std::from_chars_result const result = std::from_chars(text.begin(), text.end(), value);
        if (!parse_ok(text, result)) {
            return false;
        }

        out_value = value;
        return true;
    }

    [[nodiscard]] auto i64_to_string(char* buffer, size_t capacity, int64_t value) -> StrRef {
        if (buffer == nullptr || capacity == 0u) {
            return {};
        }

        return write_ok(buffer, std::to_chars(buffer, buffer + capacity, value));
    }

    [[nodiscard]] auto u64_to_string(char* buffer, size_t capacity, uint64_t value) -> StrRef {
        if (buffer == nullptr || capacity == 0u) {
            return {};
        }

        return write_ok(buffer, std::to_chars(buffer, buffer + capacity, value));
    }

    [[nodiscard]] auto f32_to_string(char* buffer, size_t capacity, float value) -> StrRef {
        if (buffer == nullptr || capacity == 0u) {
            return {};
        }

        return write_ok(buffer, std::to_chars(buffer, buffer + capacity, value));
    }

    [[nodiscard]] auto f64_to_string(char* buffer, size_t capacity, double value) -> StrRef {
        if (buffer == nullptr || capacity == 0u) {
            return {};
        }

        return write_ok(buffer, std::to_chars(buffer, buffer + capacity, value));
    }

} // namespace base
