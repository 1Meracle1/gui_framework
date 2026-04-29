#pragma once

#include <base/assert.h>
#include <base/io.h>
#include <base/str_ref.h>
#include <base/string_buffer.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace fmt {
    namespace detail {

        struct FormatSpec {
            StrRef text;
            char conversion = '\0';
            int width = -1;
            int precision = -1;
            int dynamic_width = 0;
            int dynamic_precision = 0;
            bool left_adjust = false;
            bool show_sign = false;
            bool space_sign = false;
            bool alternate_form = false;
            bool zero_pad = false;
            bool uses_dynamic_width = false;
            bool uses_dynamic_precision = false;
        };

        [[nodiscard]] auto add_print_count(int* written, size_t amount) -> bool;
        [[nodiscard]] auto
        parse_print_spec(StrRef format, size_t percent_index, FormatSpec* out_spec) -> size_t;
        [[nodiscard]] auto print_text(io::Writer writer, StrRef text, int* written) -> bool;
        [[nodiscard]] auto print_repeated(io::Writer writer, char value, size_t count, int* written)
            -> bool;
        [[nodiscard]] auto
        print_string_arg(io::Writer writer, FormatSpec const& spec, StrRef value, int* written)
            -> bool;
        [[nodiscard]] auto
        print_char_arg(io::Writer writer, FormatSpec const& spec, char value, int* written) -> bool;
        [[nodiscard]] auto
        print_bool_arg(io::Writer writer, FormatSpec const& spec, bool value, int* written) -> bool;
        [[nodiscard]] auto print_signed_integer_arg(io::Writer writer,
                                                    FormatSpec const& spec,
                                                    int64_t value,
                                                    int* written) -> bool;
        [[nodiscard]] auto print_unsigned_integer_arg(io::Writer writer,
                                                      FormatSpec const& spec,
                                                      uint64_t value,
                                                      int* written) -> bool;
        [[nodiscard]] auto print_pointer_value_arg(io::Writer writer,
                                                   FormatSpec const& spec,
                                                   void const* value,
                                                   int* written) -> bool;
        [[nodiscard]] auto print_floating_arg(io::Writer writer,
                                              FormatSpec const& spec,
                                              long double value,
                                              int* written) -> bool;
        [[nodiscard]] auto print_format_without_args(io::Writer writer, StrRef format, int* written)
            -> bool;

        template <typename Arg>
        [[nodiscard]] auto print_dynamic_int_arg(Arg const& arg, int* out_value) -> bool {
            if (out_value == nullptr) {
                return false;
            }

            using Value = std::remove_cvref_t<Arg>;
            if constexpr (std::is_integral_v<Value> && !std::is_same_v<Value, bool>) {
                if constexpr (std::is_signed_v<Value>) {
                    long long const value = static_cast<long long>(arg);
                    if (value < static_cast<long long>(INT_MIN) ||
                        value > static_cast<long long>(INT_MAX)) {
                        return false;
                    }

                    *out_value = static_cast<int>(value);
                } else {
                    unsigned long long const value = static_cast<unsigned long long>(arg);
                    if (value > static_cast<unsigned long long>(INT_MAX)) {
                        return false;
                    }

                    *out_value = static_cast<int>(value);
                }

                return true;
            } else {
                return false;
            }
        }

        template <typename Arg>
        [[nodiscard]] auto resolve_dynamic_width(FormatSpec* spec, Arg const& arg) -> bool {
            int width = 0;
            if (spec == nullptr || !print_dynamic_int_arg(arg, &width) || width == INT_MIN) {
                return false;
            }

            spec->dynamic_width = width;
            if (width < 0) {
                spec->width = -width;
                spec->left_adjust = true;
                spec->zero_pad = false;
            } else {
                spec->width = width;
            }

            return true;
        }

        template <typename Arg>
        [[nodiscard]] auto resolve_dynamic_precision(FormatSpec* spec, Arg const& arg) -> bool {
            int precision = 0;
            if (spec == nullptr || !print_dynamic_int_arg(arg, &precision)) {
                return false;
            }

            spec->dynamic_precision = precision;
            spec->precision = precision >= 0 ? precision : -1;
            return true;
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, StrRef value, int* written) -> bool {
            return print_string_arg(writer, spec, value, written);
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, std::string_view value, int* written)
            -> bool {
            return print_string_arg(writer, spec, StrRef(value), written);
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, std::string const& value, int* written)
            -> bool {
            return print_string_arg(writer, spec, StrRef(value), written);
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, char const* value, int* written)
            -> bool {
            if (spec.conversion == 's' || spec.conversion == 'v') {
                return print_string_arg(
                    writer, spec, value != nullptr ? StrRef(value) : StrRef("(null)"), written);
            }

            if (spec.conversion == 'p') {
                return print_pointer_value_arg(
                    writer, spec, static_cast<void const*>(value), written);
            }

            return false;
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, char* value, int* written) -> bool {
            return print_arg(writer, spec, static_cast<char const*>(value), written);
        }

        template <size_t N>
        [[nodiscard]] auto
        print_arg(io::Writer writer, FormatSpec const& spec, char const (&value)[N], int* written)
            -> bool {
            if (spec.conversion == 's' || spec.conversion == 'v') {
                return print_string_arg(writer, spec, StrRef(value), written);
            }

            if (spec.conversion == 'p') {
                return print_pointer_value_arg(
                    writer, spec, static_cast<void const*>(value), written);
            }

            return false;
        }

        template <size_t N>
        [[nodiscard]] auto print_arg(io::Writer writer,
                                     FormatSpec const& spec,
                                     char8_t const (&value)[N],
                                     int* written) -> bool {
            if (spec.conversion == 's' || spec.conversion == 'v') {
                return print_string_arg(writer, spec, StrRef(value), written);
            }

            return false;
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, std::nullptr_t, int* written) -> bool {
            if (spec.conversion == 's' || spec.conversion == 'v') {
                return print_string_arg(writer, spec, StrRef("(null)"), written);
            }

            if (spec.conversion == 'p') {
                return print_pointer_value_arg(writer, spec, nullptr, written);
            }

            return false;
        }

        [[nodiscard]] inline auto
        print_arg(io::Writer writer, FormatSpec const& spec, bool value, int* written) -> bool {
            if (spec.conversion == 't' || spec.conversion == 'v') {
                return print_bool_arg(writer, spec, value, written);
            }

            return print_unsigned_integer_arg(writer, spec, value ? 1u : 0u, written);
        }

        template <typename Arg>
        [[nodiscard]] auto
        print_integral_arg(io::Writer writer, FormatSpec const& spec, Arg const& arg, int* written)
            -> bool {
            using Value = std::remove_cvref_t<Arg>;

            if (spec.conversion == 'c') {
                return print_char_arg(writer, spec, static_cast<char>(arg), written);
            }

            if constexpr (std::is_signed_v<Value>) {
                if (spec.conversion == 'd' || spec.conversion == 'i' || spec.conversion == 'v') {
                    return print_signed_integer_arg(
                        writer, spec, static_cast<int64_t>(arg), written);
                }
            }

            using Unsigned = std::make_unsigned_t<Value>;
            return print_unsigned_integer_arg(
                writer, spec, static_cast<uint64_t>(static_cast<Unsigned>(arg)), written);
        }

        template <typename Pointer>
        [[nodiscard]] auto
        print_pointer_arg(io::Writer writer, FormatSpec const& spec, Pointer value, int* written)
            -> bool {
            if constexpr (std::is_object_v<std::remove_pointer_t<Pointer>>) {
                if (spec.conversion == 'p' || spec.conversion == 'v') {
                    return print_pointer_value_arg(
                        writer, spec, static_cast<void const*>(value), written);
                }
            }

            return false;
        }

        template <typename Arg>
        [[nodiscard]] auto
        print_arg(io::Writer writer, FormatSpec const& spec, Arg const& arg, int* written) -> bool {
            using Value = std::remove_cvref_t<Arg>;

            if constexpr (std::is_pointer_v<Value>) {
                return print_pointer_arg(writer, spec, arg, written);
            } else if constexpr (std::is_integral_v<Value>) {
                return print_integral_arg(writer, spec, arg, written);
            } else if constexpr (std::is_floating_point_v<Value>) {
                return print_floating_arg(writer, spec, static_cast<long double>(arg), written);
            } else {
                return false;
            }
        }

        [[nodiscard]] inline auto print_format_args(io::Writer writer, StrRef format, int* written)
            -> bool {
            return print_format_without_args(writer, format, written);
        }

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_args(
            io::Writer writer, StrRef format, int* written, Arg const& arg, Rest const&... rest)
            -> bool;

        [[nodiscard]] inline auto
        print_format_arg_after_fields(io::Writer, StrRef, int*, FormatSpec const&) -> bool {
            return false;
        }

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_arg_after_fields(io::Writer writer,
                                                         StrRef remaining_format,
                                                         int* written,
                                                         FormatSpec const& spec,
                                                         Arg const& arg,
                                                         Rest const&... rest) -> bool {
            if (!print_arg(writer, spec, arg, written)) {
                return false;
            }

            return print_format_args(writer, remaining_format, written, rest...);
        }

        [[nodiscard]] inline auto
        print_format_arg_after_precision(io::Writer, StrRef, int*, FormatSpec const&) -> bool {
            return false;
        }

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_arg_after_precision(io::Writer writer,
                                                            StrRef remaining_format,
                                                            int* written,
                                                            FormatSpec spec,
                                                            Arg const& arg,
                                                            Rest const&... rest) -> bool {
            if (spec.uses_dynamic_precision) {
                if (!resolve_dynamic_precision(&spec, arg)) {
                    return false;
                }

                return print_format_arg_after_fields(
                    writer, remaining_format, written, spec, rest...);
            }

            return print_format_arg_after_fields(
                writer, remaining_format, written, spec, arg, rest...);
        }

        [[nodiscard]] inline auto
        print_format_arg_after_width(io::Writer, StrRef, int*, FormatSpec const&) -> bool {
            return false;
        }

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_arg_after_width(io::Writer writer,
                                                        StrRef remaining_format,
                                                        int* written,
                                                        FormatSpec spec,
                                                        Arg const& arg,
                                                        Rest const&... rest) -> bool {
            if (spec.uses_dynamic_width) {
                if (!resolve_dynamic_width(&spec, arg)) {
                    return false;
                }

                return print_format_arg_after_precision(
                    writer, remaining_format, written, spec, rest...);
            }

            return print_format_arg_after_precision(
                writer, remaining_format, written, spec, arg, rest...);
        }

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_args(
            io::Writer writer, StrRef format, int* written, Arg const& arg, Rest const&... rest)
            -> bool {
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
                size_t const next_offset = parse_print_spec(format, offset, &spec);
                if (next_offset == StrRef::NPOS) {
                    ++offset;
                    continue;
                }

                if (!print_text(writer, format.prefix(offset), written)) {
                    return false;
                }

                return print_format_arg_after_width(
                    writer, format.drop_prefix(next_offset), written, spec, arg, rest...);
            }

            return print_text(writer, format, written);
        }

    } // namespace detail

    auto print(std::FILE* stream, StrRef text) -> int;
    auto print(StrRef text) -> int;
    auto eprint(StrRef text) -> int;

    template <typename... Args>
    auto wprintf(io::Writer writer, StrRef format_text, Args const&... args) -> int {
        int written = 0;
        if (!detail::print_format_args(writer, format_text, &written, args...)) {
            return -1;
        }

        return written;
    }

    template <typename... Args>
    auto format(StringBuffer* buffer, StrRef format_text, Args const&... args) -> int {
        return fmt::wprintf(io::string_buffer_writer(buffer), format_text, args...);
    }

    template <typename... Args>
    [[nodiscard]] auto aprintf(MemoryResource* resource, StrRef format_text, Args const&... args)
        -> StringBuffer {
        StringBuffer buffer;
        if (!buffer.init(0u, resource)) {
            return buffer;
        }

        if (fmt::format(&buffer, format_text, args...) < 0) {
            buffer.reset();
        }

        return buffer;
    }

    template <typename... Args>
    [[nodiscard]] auto
    bprintf(char* backing, size_t capacity, StrRef format_text, Args const&... args) -> StrRef {
        DEBUG_ASSERT(backing != nullptr);
        DEBUG_ASSERT(capacity > 0u);
        if (backing == nullptr || capacity == 0u) {
            return {};
        }

        StringBuffer buffer;
        buffer.init_with_backing(backing, capacity);

        int const ignored_result = fmt::format(&buffer, format_text, args...);
        (void)ignored_result;
        return buffer.str();
    }

    template <typename... Args>
    [[nodiscard]] auto tprintf(StrRef format_text, Args const&... args) -> StrRef {
        StringBuffer buffer = fmt::aprintf(thread_temp_resource(), format_text, args...);
        return buffer.str();
    }

    template <typename... Args>
    auto fprintf(std::FILE* stream, StrRef format_text, Args const&... args) -> int {
        return fmt::wprintf(io::file_writer(stream), format_text, args...);
    }

    template <typename... Args> auto printf(StrRef format_text, Args const&... args) -> int {
        return fmt::fprintf(stdout, format_text, args...);
    }

    template <typename... Args> auto eprintf(StrRef format_text, Args const&... args) -> int {
        return fmt::fprintf(stderr, format_text, args...);
    }

} // namespace fmt
