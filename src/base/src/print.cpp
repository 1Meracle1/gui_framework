#include <base/print.h>
#include <climits>
#include <cstdio>
#include <cstring>

namespace {

    [[nodiscard]] auto is_print_flag(char value) noexcept -> bool {
        return value == '-' || value == '+' || value == ' ' || value == '#' || value == '0';
    }

    [[nodiscard]] auto is_print_digit(char value) noexcept -> bool {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] auto is_print_conversion(char value) noexcept -> bool {
        switch (value) {
        case 'd':
        case 'i':
        case 'u':
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

} // namespace

namespace base::detail {

    [[nodiscard]] auto add_print_count(int* written, size_t amount) noexcept -> bool {
        if (written == nullptr || amount > static_cast<size_t>(INT_MAX - *written)) {
            return false;
        }

        *written += static_cast<int>(amount);
        return true;
    }

    [[nodiscard]] auto
    copy_print_spec(PrintSpec const& spec, char* target, size_t target_size) noexcept -> bool {
        if (target == nullptr || target_size == 0u || spec.text.size() >= target_size) {
            return false;
        }

        for (char value : spec.text) {
            if (value == '\0') {
                return false;
            }
        }

        size_t const copied_size = spec.text.copy_to(target, target_size - 1u);
        target[copied_size] = '\0';
        return copied_size == spec.text.size();
    }

    [[nodiscard]] auto
    parse_print_spec(StrRef format, size_t percent_index, PrintSpec* out_spec) noexcept -> size_t {
        if (out_spec == nullptr || percent_index >= format.size() || format[percent_index] != '%') {
            return StrRef::NPOS;
        }

        size_t index = percent_index + 1u;
        if (index >= format.size()) {
            return StrRef::NPOS;
        }

        PrintSpec spec = {};

        while (index < format.size() && is_print_flag(format[index])) {
            spec.left_adjust = spec.left_adjust || format[index] == '-';
            ++index;
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

    [[nodiscard]] auto print_text(std::FILE* stream, StrRef text, int* written) noexcept -> bool {
        if (stream == nullptr) {
            return false;
        }

        if (text.empty()) {
            return true;
        }

        size_t const write_count = std::fwrite(text.data(), 1u, text.size(), stream);
        if (write_count != text.size()) {
            return false;
        }

        return add_print_count(written, write_count);
    }

    [[nodiscard]] auto
    print_repeated(std::FILE* stream, char value, size_t count, int* written) noexcept -> bool {
        if (stream == nullptr) {
            return false;
        }

        char buffer[64] = {};
        std::memset(buffer, value, sizeof(buffer));

        while (count != 0u) {
            size_t const chunk_size = count < sizeof(buffer) ? count : sizeof(buffer);
            size_t const write_count = std::fwrite(buffer, 1u, chunk_size, stream);
            if (write_count != chunk_size || !add_print_count(written, write_count)) {
                return false;
            }

            count -= chunk_size;
        }

        return true;
    }

    [[nodiscard]] auto
    print_str_ref_arg(std::FILE* stream, PrintSpec const& spec, StrRef value, int* written) noexcept
        -> bool {
        if (spec.conversion != 's' || spec.uses_dynamic_width || spec.uses_dynamic_precision) {
            return false;
        }

        size_t text_size = value.size();
        if (spec.precision >= 0 && static_cast<size_t>(spec.precision) < text_size) {
            text_size = static_cast<size_t>(spec.precision);
        }

        size_t const width = spec.width > 0 ? static_cast<size_t>(spec.width) : 0u;
        size_t const padding_size = width > text_size ? width - text_size : 0u;

        if (!spec.left_adjust && !print_repeated(stream, ' ', padding_size, written)) {
            return false;
        }

        if (!print_text(stream, value.prefix(text_size), written)) {
            return false;
        }

        if (spec.left_adjust && !print_repeated(stream, ' ', padding_size, written)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] auto
    print_format_without_args(std::FILE* stream, StrRef format, int* written) noexcept -> bool {
        size_t offset = 0u;

        while (offset < format.size()) {
            if (format[offset] != '%') {
                ++offset;
                continue;
            }

            if (offset + 1u < format.size() && format[offset + 1u] == '%') {
                if (!print_text(stream, format.prefix(offset), written)) {
                    return false;
                }
                if (!print_text(stream, StrRef("%"), written)) {
                    return false;
                }

                format = format.drop_prefix(offset + 2u);
                offset = 0u;
                continue;
            }

            PrintSpec spec = {};
            if (parse_print_spec(format, offset, &spec) != StrRef::NPOS) {
                return false;
            }

            ++offset;
        }

        return print_text(stream, format, written);
    }

} // namespace base::detail

namespace base {

    auto print(std::FILE* stream, StrRef text) noexcept -> int {
        int written = 0;
        if (!detail::print_text(stream, text, &written)) {
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

} // namespace base
