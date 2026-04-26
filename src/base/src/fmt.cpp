#include <base/fmt.h>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

    [[nodiscard]] auto is_print_flag(char value) noexcept -> bool {
        return value == '-' || value == '+' || value == ' ' || value == '#' || value == '0';
    }

    [[nodiscard]] auto is_print_digit(char value) noexcept -> bool {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] auto is_print_conversion(char value) noexcept -> bool {
        switch (value) {
        case 'v':
        case 't':
        case 'd':
        case 'i':
        case 'u':
        case 'b':
        case 'o':
        case 'x':
        case 'X':
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
        case 'c':
        case 's':
        case 'p':
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] auto is_decimal_conversion(char conversion) noexcept -> bool {
        return conversion == 'd' || conversion == 'i';
    }

    [[nodiscard]] auto is_unsigned_integer_conversion(char conversion) noexcept -> bool {
        return conversion == 'u' || conversion == 'b' || conversion == 'o' || conversion == 'x' ||
               conversion == 'X';
    }

    [[nodiscard]] auto is_floating_conversion(char conversion) noexcept -> bool {
        return conversion == 'f' || conversion == 'F' || conversion == 'e' || conversion == 'E' ||
               conversion == 'g' || conversion == 'G' || conversion == 'a' || conversion == 'A';
    }

    [[nodiscard]] auto ascii_upper(char value) noexcept -> char {
        if (value >= 'a' && value <= 'z') {
            return static_cast<char>(value - 'a' + 'A');
        }

        return value;
    }

    auto uppercase_ascii(char* text, size_t size) noexcept -> void {
        if (text == nullptr) {
            return;
        }

        for (size_t index = 0u; index < size; ++index) {
            text[index] = ascii_upper(text[index]);
        }
    }

    [[nodiscard]] auto parse_decimal_field(StrRef format, size_t* index) noexcept -> int {
        int value = 0;

        while (*index < format.size() && is_print_digit(format[*index])) {
            int const digit = format[*index] - '0';
            if (value <= (INT_MAX - digit) / 10) {
                value = value * 10 + digit;
            } else {
                value = INT_MAX;
            }

            *index += 1u;
        }

        return value;
    }

    auto skip_length_modifier(StrRef format, size_t* index) noexcept -> void {
        if (*index >= format.size()) {
            return;
        }

        char const first = format[*index];
        if ((first == 'h' || first == 'l') && *index + 1u < format.size() &&
            format[*index + 1u] == first) {
            *index += 2u;
            return;
        }

        if (first == 'h' || first == 'l' || first == 'j' || first == 'z' || first == 't' ||
            first == 'L') {
            *index += 1u;
        }
    }

    [[nodiscard]] auto print_padded_text(io::Writer writer,
                                         fmt::detail::FormatSpec const& spec,
                                         StrRef text,
                                         int* written) noexcept -> bool {
        size_t const width = spec.width > 0 ? static_cast<size_t>(spec.width) : 0u;
        size_t const padding_size = width > text.size() ? width - text.size() : 0u;

        if (!spec.left_adjust && !fmt::detail::print_repeated(writer, ' ', padding_size, written)) {
            return false;
        }

        if (!fmt::detail::print_text(writer, text, written)) {
            return false;
        }

        if (spec.left_adjust && !fmt::detail::print_repeated(writer, ' ', padding_size, written)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] auto make_unsigned_digits(uint64_t value,
                                            uint32_t base,
                                            bool uppercase,
                                            char* buffer,
                                            size_t buffer_size) noexcept -> StrRef {
        if (buffer == nullptr || buffer_size == 0u || base < 2u || base > 16u) {
            return {};
        }

        char const* const digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        size_t index = buffer_size;

        do {
            uint32_t const digit = static_cast<uint32_t>(value % base);
            value /= base;
            --index;
            buffer[index] = digits[digit];
        } while (value != 0u && index != 0u);

        return StrRef(buffer + index, buffer_size - index);
    }

    [[nodiscard]] auto print_prefixed_digits(io::Writer writer,
                                             fmt::detail::FormatSpec const& spec,
                                             StrRef prefix,
                                             StrRef digits,
                                             int* written) noexcept -> bool {
        size_t precision_zero_count = 0u;
        if (spec.precision > 0 && static_cast<size_t>(spec.precision) > digits.size()) {
            precision_zero_count = static_cast<size_t>(spec.precision) - digits.size();
        }

        size_t const body_size = prefix.size() + precision_zero_count + digits.size();
        size_t const width = spec.width > 0 ? static_cast<size_t>(spec.width) : 0u;
        bool const use_zero_padding =
            spec.zero_pad && !spec.left_adjust && spec.precision < 0 && width > body_size;
        size_t const width_zero_count = use_zero_padding ? width - body_size : 0u;
        size_t const padded_body_size = body_size + width_zero_count;
        size_t const space_count = width > padded_body_size ? width - padded_body_size : 0u;

        if (!spec.left_adjust && !use_zero_padding &&
            !fmt::detail::print_repeated(writer, ' ', space_count, written)) {
            return false;
        }

        if (!fmt::detail::print_text(writer, prefix, written)) {
            return false;
        }

        if (!fmt::detail::print_repeated(writer, '0', width_zero_count, written)) {
            return false;
        }

        if (!fmt::detail::print_repeated(writer, '0', precision_zero_count, written)) {
            return false;
        }

        if (!fmt::detail::print_text(writer, digits, written)) {
            return false;
        }

        if (spec.left_adjust && !fmt::detail::print_repeated(writer, ' ', space_count, written)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] auto print_prefixed_text(io::Writer writer,
                                           fmt::detail::FormatSpec const& spec,
                                           StrRef prefix,
                                           StrRef text,
                                           bool allow_zero_padding,
                                           int* written) noexcept -> bool {
        size_t const body_size = prefix.size() + text.size();
        size_t const width = spec.width > 0 ? static_cast<size_t>(spec.width) : 0u;
        bool const use_zero_padding =
            allow_zero_padding && spec.zero_pad && !spec.left_adjust && width > body_size;
        size_t const zero_count = use_zero_padding ? width - body_size : 0u;
        size_t const padded_body_size = body_size + zero_count;
        size_t const space_count = width > padded_body_size ? width - padded_body_size : 0u;

        if (!spec.left_adjust && !use_zero_padding &&
            !fmt::detail::print_repeated(writer, ' ', space_count, written)) {
            return false;
        }

        if (!fmt::detail::print_text(writer, prefix, written)) {
            return false;
        }

        if (!fmt::detail::print_repeated(writer, '0', zero_count, written)) {
            return false;
        }

        if (!fmt::detail::print_text(writer, text, written)) {
            return false;
        }

        if (spec.left_adjust && !fmt::detail::print_repeated(writer, ' ', space_count, written)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] auto print_integer_value(io::Writer writer,
                                           fmt::detail::FormatSpec const& spec,
                                           uint64_t magnitude,
                                           bool negative,
                                           bool signed_conversion,
                                           int* written) noexcept -> bool {
        char conversion = spec.conversion;
        if (conversion == 'v') {
            conversion = signed_conversion ? 'd' : 'u';
        }

        uint32_t base = 10u;
        bool uppercase = false;
        switch (conversion) {
        case 'd':
        case 'i':
        case 'u':
            base = 10u;
            break;
        case 'b':
            base = 2u;
            break;
        case 'o':
            base = 8u;
            break;
        case 'x':
        case 'X':
            base = 16u;
            uppercase = conversion == 'X';
            break;
        default:
            return false;
        }

        char digit_buffer[64] = {};
        bool const suppress_digits = spec.precision == 0 && magnitude == 0u;
        StrRef const digits =
            suppress_digits ? StrRef()
                            : make_unsigned_digits(
                                  magnitude, base, uppercase, digit_buffer, sizeof(digit_buffer));

        char prefix_buffer[3] = {};
        size_t prefix_size = 0u;

        if (signed_conversion && is_decimal_conversion(conversion)) {
            if (negative) {
                prefix_buffer[prefix_size] = '-';
                ++prefix_size;
            } else if (spec.show_sign) {
                prefix_buffer[prefix_size] = '+';
                ++prefix_size;
            } else if (spec.space_sign) {
                prefix_buffer[prefix_size] = ' ';
                ++prefix_size;
            }
        }

        if (spec.alternate_form && is_unsigned_integer_conversion(conversion)) {
            if (conversion == 'b' && magnitude != 0u) {
                prefix_buffer[prefix_size] = '0';
                prefix_buffer[prefix_size + 1u] = uppercase ? 'B' : 'b';
                prefix_size += 2u;
            } else if (conversion == 'o') {
                bool const needs_octal_prefix =
                    suppress_digits ||
                    (magnitude != 0u &&
                     (spec.precision < 0 || static_cast<size_t>(spec.precision) <= digits.size()));
                if (needs_octal_prefix) {
                    prefix_buffer[prefix_size] = '0';
                    ++prefix_size;
                }
            } else if ((conversion == 'x' || conversion == 'X') && magnitude != 0u) {
                prefix_buffer[prefix_size] = '0';
                prefix_buffer[prefix_size + 1u] = uppercase ? 'X' : 'x';
                prefix_size += 2u;
            }
        }

        return print_prefixed_digits(
            writer, spec, StrRef(prefix_buffer, prefix_size), digits, written);
    }

    [[nodiscard]] auto to_chars_float(long double value,
                                      char conversion,
                                      int precision,
                                      char* buffer,
                                      size_t buffer_size,
                                      size_t* out_size) noexcept -> bool {
        if (buffer == nullptr || out_size == nullptr || buffer_size == 0u) {
            return false;
        }

        char const normalized = (conversion >= 'A' && conversion <= 'Z')
                                    ? static_cast<char>(conversion - 'A' + 'a')
                                    : conversion;
        std::chars_format format = std::chars_format::general;
        int resolved_precision = precision;
        bool use_precision = true;

        switch (normalized) {
        case 'f':
            format = std::chars_format::fixed;
            if (resolved_precision < 0) {
                resolved_precision = 6;
            }
            break;
        case 'e':
            format = std::chars_format::scientific;
            if (resolved_precision < 0) {
                resolved_precision = 6;
            }
            break;
        case 'g':
            format = std::chars_format::general;
            if (resolved_precision < 0) {
                resolved_precision = 6;
            } else if (resolved_precision == 0) {
                resolved_precision = 1;
            }
            break;
        case 'a':
            format = std::chars_format::hex;
            use_precision = resolved_precision >= 0;
            break;
        default:
            return false;
        }

        double const converted_value = static_cast<double>(value);
        std::to_chars_result result = {};
        if (use_precision) {
            result = std::to_chars(
                buffer, buffer + buffer_size, converted_value, format, resolved_precision);
        } else {
            result = std::to_chars(buffer, buffer + buffer_size, converted_value, format);
        }

        if (result.ec != std::errc()) {
            return false;
        }

        *out_size = static_cast<size_t>(result.ptr - buffer);
        return true;
    }

    [[nodiscard]] auto
    ensure_float_decimal_point(char* buffer, size_t* size, size_t capacity) noexcept -> bool {
        if (buffer == nullptr || size == nullptr) {
            return false;
        }

        bool has_digit = false;
        bool has_point = false;
        size_t insert_index = *size;

        for (size_t index = 0u; index < *size; ++index) {
            char const value = buffer[index];
            if (value >= '0' && value <= '9') {
                has_digit = true;
            } else if (value == '.') {
                has_point = true;
            } else if (value == 'e' || value == 'E' || value == 'p' || value == 'P') {
                insert_index = index;
                break;
            }
        }

        if (!has_digit || has_point) {
            return true;
        }

        if (*size >= capacity) {
            return false;
        }

        std::memmove(buffer + insert_index + 1u, buffer + insert_index, *size - insert_index);
        buffer[insert_index] = '.';
        *size += 1u;
        return true;
    }

} // namespace

namespace fmt::detail {

    [[nodiscard]] auto add_print_count(int* written, size_t amount) noexcept -> bool {
        if (written == nullptr || amount > static_cast<size_t>(INT_MAX - *written)) {
            return false;
        }

        *written += static_cast<int>(amount);
        return true;
    }

    [[nodiscard]] auto
    parse_print_spec(StrRef format, size_t percent_index, FormatSpec* out_spec) noexcept -> size_t {
        if (out_spec == nullptr || percent_index >= format.size() || format[percent_index] != '%') {
            return StrRef::NPOS;
        }

        size_t index = percent_index + 1u;
        if (index >= format.size()) {
            return StrRef::NPOS;
        }

        FormatSpec spec = {};

        while (index < format.size() && is_print_flag(format[index])) {
            switch (format[index]) {
            case '-':
                spec.left_adjust = true;
                spec.zero_pad = false;
                break;
            case '+':
                spec.show_sign = true;
                break;
            case ' ':
                spec.space_sign = true;
                break;
            case '#':
                spec.alternate_form = true;
                break;
            case '0':
                spec.zero_pad = !spec.left_adjust;
                break;
            default:
                break;
            }

            ++index;
        }

        if (spec.show_sign) {
            spec.space_sign = false;
        }

        if (index < format.size() && format[index] == '*') {
            spec.uses_dynamic_width = true;
            ++index;
        } else {
            size_t const width_start = index;
            spec.width = parse_decimal_field(format, &index);
            if (index == width_start) {
                spec.width = -1;
            }
        }

        if (index < format.size() && format[index] == '.') {
            ++index;

            if (index < format.size() && format[index] == '*') {
                spec.uses_dynamic_precision = true;
                ++index;
            } else {
                spec.precision = parse_decimal_field(format, &index);
            }
        }

        skip_length_modifier(format, &index);

        if (index >= format.size() || !is_print_conversion(format[index])) {
            return StrRef::NPOS;
        }

        spec.conversion = format[index];
        spec.text = format.substr(percent_index, index - percent_index + 1u);
        *out_spec = spec;
        return index + 1u;
    }

    [[nodiscard]] auto print_text(io::Writer writer, StrRef text, int* written) noexcept -> bool {
        if (text.empty()) {
            return true;
        }

        size_t write_count = 0u;
        if (io::write_full(writer, text, &write_count) != io::Error::NONE ||
            write_count != text.size()) {
            return false;
        }

        return add_print_count(written, text.size());
    }

    [[nodiscard]] auto
    print_repeated(io::Writer writer, char value, size_t count, int* written) noexcept -> bool {
        if (count == 0u) {
            return true;
        }

        size_t write_count = 0u;
        if (io::write_fill(writer, value, count, &write_count) != io::Error::NONE ||
            write_count != count) {
            return false;
        }

        return add_print_count(written, count);
    }

    [[nodiscard]] auto
    print_string_arg(io::Writer writer, FormatSpec const& spec, StrRef value, int* written) noexcept
        -> bool {
        if (spec.conversion != 's' && spec.conversion != 'v') {
            return false;
        }

        size_t text_size = value.size();
        if (spec.precision >= 0 && static_cast<size_t>(spec.precision) < text_size) {
            text_size = static_cast<size_t>(spec.precision);
        }

        return print_padded_text(writer, spec, value.prefix(text_size), written);
    }

    [[nodiscard]] auto
    print_char_arg(io::Writer writer, FormatSpec const& spec, char value, int* written) noexcept
        -> bool {
        if (spec.conversion != 'c' && spec.conversion != 'v') {
            return false;
        }

        return print_padded_text(writer, spec, StrRef(&value, 1u), written);
    }

    [[nodiscard]] auto
    print_bool_arg(io::Writer writer, FormatSpec const& spec, bool value, int* written) noexcept
        -> bool {
        if (spec.conversion != 't' && spec.conversion != 'v') {
            return false;
        }

        return print_padded_text(writer, spec, value ? StrRef("true") : StrRef("false"), written);
    }

    [[nodiscard]] auto print_signed_integer_arg(io::Writer writer,
                                                FormatSpec const& spec,
                                                int64_t value,
                                                int* written) noexcept -> bool {
        if (spec.conversion != 'v' && !is_decimal_conversion(spec.conversion)) {
            return false;
        }

        bool const negative = value < 0;
        uint64_t const magnitude =
            negative ? 0u - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);

        return print_integer_value(writer, spec, magnitude, negative, true, written);
    }

    [[nodiscard]] auto print_unsigned_integer_arg(io::Writer writer,
                                                  FormatSpec const& spec,
                                                  uint64_t value,
                                                  int* written) noexcept -> bool {
        if (spec.conversion != 'v' && !is_decimal_conversion(spec.conversion) &&
            !is_unsigned_integer_conversion(spec.conversion)) {
            return false;
        }

        bool const signed_conversion = is_decimal_conversion(spec.conversion);
        return print_integer_value(writer, spec, value, false, signed_conversion, written);
    }

    [[nodiscard]] auto print_pointer_value_arg(io::Writer writer,
                                               FormatSpec const& spec,
                                               void const* value,
                                               int* written) noexcept -> bool {
        if (spec.conversion != 'p' && spec.conversion != 'v') {
            return false;
        }

        FormatSpec pointer_spec = spec;
        if (pointer_spec.precision == 0) {
            pointer_spec.precision = 1;
        }

        char digit_buffer[sizeof(uintptr_t) * 2u] = {};
        StrRef const digits = make_unsigned_digits(
            reinterpret_cast<uintptr_t>(value), 16u, false, digit_buffer, sizeof(digit_buffer));
        return print_prefixed_digits(writer, pointer_spec, StrRef("0x"), digits, written);
    }

    [[nodiscard]] auto print_floating_arg(io::Writer writer,
                                          FormatSpec const& spec,
                                          long double value,
                                          int* written) noexcept -> bool {
        char conversion = spec.conversion == 'v' ? 'g' : spec.conversion;
        if (!is_floating_conversion(conversion)) {
            return false;
        }

        char text_buffer[512] = {};
        size_t text_size = 0u;
        if (!to_chars_float(
                value, conversion, spec.precision, text_buffer, sizeof(text_buffer), &text_size)) {
            return false;
        }

        bool const uppercase =
            conversion == 'F' || conversion == 'E' || conversion == 'G' || conversion == 'A';
        bool negative = false;
        if (text_size != 0u && text_buffer[0] == '-') {
            negative = true;
            std::memmove(text_buffer, text_buffer + 1u, text_size - 1u);
            --text_size;
        }

        if (spec.alternate_form &&
            !ensure_float_decimal_point(text_buffer, &text_size, sizeof(text_buffer))) {
            return false;
        }

        if (uppercase) {
            uppercase_ascii(text_buffer, text_size);
        }

        char prefix_buffer[4] = {};
        size_t prefix_size = 0u;

        if (negative) {
            prefix_buffer[prefix_size] = '-';
            ++prefix_size;
        } else if (spec.show_sign) {
            prefix_buffer[prefix_size] = '+';
            ++prefix_size;
        } else if (spec.space_sign) {
            prefix_buffer[prefix_size] = ' ';
            ++prefix_size;
        }

        if (conversion == 'a' || conversion == 'A') {
            prefix_buffer[prefix_size] = '0';
            prefix_buffer[prefix_size + 1u] = uppercase ? 'X' : 'x';
            prefix_size += 2u;
        }

        return print_prefixed_text(writer,
                                   spec,
                                   StrRef(prefix_buffer, prefix_size),
                                   StrRef(text_buffer, text_size),
                                   true,
                                   written);
    }

    [[nodiscard]] auto
    print_format_without_args(io::Writer writer, StrRef format, int* written) noexcept -> bool {
        size_t offset = 0u;

        while (offset < format.size()) {
            if (format[offset] != '%') {
                ++offset;
                continue;
            }

            if (offset + 1u < format.size() && format[offset + 1u] == '%') {
                if (!print_text(writer, format.prefix(offset), written)) {
                    return false;
                }
                if (!print_text(writer, StrRef("%"), written)) {
                    return false;
                }

                format = format.drop_prefix(offset + 2u);
                offset = 0u;
                continue;
            }

            FormatSpec spec = {};
            if (parse_print_spec(format, offset, &spec) != StrRef::NPOS) {
                return false;
            }

            ++offset;
        }

        return print_text(writer, format, written);
    }

} // namespace fmt::detail

namespace fmt {

    auto print(std::FILE* stream, StrRef text) noexcept -> int {
        io::Writer const writer = io::file_writer(stream);

        int written = 0;
        if (!detail::print_text(writer, text, &written)) {
            return -1;
        }

        return written;
    }

    auto print(StrRef text) noexcept -> int {
        return print(stdout, text);
    }

    auto eprint(StrRef text) noexcept -> int {
        return print(stderr, text);
    }

} // namespace fmt
