#pragma once

#include <base/str_ref.h>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace base {
    namespace detail {

        struct PrintSpec {
            StrRef text;
            char conversion = '\0';
            int width = -1;
            int precision = -1;
            bool left_adjust = false;
            bool uses_dynamic_width = false;
            bool uses_dynamic_precision = false;
        };

        [[nodiscard]] auto add_print_count(int* written, size_t amount) noexcept -> bool;
        [[nodiscard]] auto
        copy_print_spec(PrintSpec const& spec, char* target, size_t target_size) noexcept -> bool;
        [[nodiscard]] auto parse_print_spec(StrRef format,
                                            size_t percent_index,
                                            PrintSpec* out_spec) noexcept -> size_t;
        [[nodiscard]] auto print_text(std::FILE* stream, StrRef text, int* written) noexcept
            -> bool;
        [[nodiscard]] auto
        print_repeated(std::FILE* stream, char value, size_t count, int* written) noexcept -> bool;
        [[nodiscard]] auto print_str_ref_arg(std::FILE* stream,
                                             PrintSpec const& spec,
                                             StrRef value,
                                             int* written) noexcept -> bool;
        [[nodiscard]] auto
        print_format_without_args(std::FILE* stream, StrRef format, int* written) noexcept -> bool;

        template <typename Arg>
        [[nodiscard]] auto print_standard_arg(std::FILE* stream,
                                              PrintSpec const& spec,
                                              Arg const& arg,
                                              int* written) noexcept -> bool {
            if (spec.uses_dynamic_width || spec.uses_dynamic_precision) {
                return false;
            }

            char spec_buffer[96] = {};
            if (!copy_print_spec(spec, spec_buffer, sizeof(spec_buffer))) {
                return false;
            }

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
            int const result = std::fprintf(stream, spec_buffer, arg);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            if (result < 0) {
                return false;
            }

            return add_print_count(written, static_cast<size_t>(result));
        }

        [[nodiscard]] inline auto
        print_arg(std::FILE* stream, PrintSpec const& spec, StrRef value, int* written) noexcept
            -> bool {
            return print_str_ref_arg(stream, spec, value, written);
        }

        [[nodiscard]] inline auto print_arg(std::FILE* stream,
                                            PrintSpec const& spec,
                                            std::string_view value,
                                            int* written) noexcept -> bool {
            return print_str_ref_arg(stream, spec, StrRef(value), written);
        }

        [[nodiscard]] inline auto print_arg(std::FILE* stream,
                                            PrintSpec const& spec,
                                            std::string const& value,
                                            int* written) noexcept -> bool {
            return print_str_ref_arg(stream, spec, StrRef(value), written);
        }

        [[nodiscard]] inline auto print_arg(std::FILE* stream,
                                            PrintSpec const& spec,
                                            char const* value,
                                            int* written) noexcept -> bool {
            if (spec.conversion == 's') {
                return print_str_ref_arg(
                    stream, spec, value != nullptr ? StrRef(value) : StrRef("(null)"), written);
            }

            if (spec.conversion == 'p') {
                return print_standard_arg(stream, spec, static_cast<void const*>(value), written);
            }

            return print_standard_arg(stream, spec, value, written);
        }

        [[nodiscard]] inline auto
        print_arg(std::FILE* stream, PrintSpec const& spec, char* value, int* written) noexcept
            -> bool {
            return print_arg(stream, spec, static_cast<char const*>(value), written);
        }

        template <size_t N>
        [[nodiscard]] auto print_arg(std::FILE* stream,
                                     PrintSpec const& spec,
                                     char const (&value)[N],
                                     int* written) noexcept -> bool {
            if (spec.conversion == 's') {
                return print_str_ref_arg(stream, spec, StrRef(value), written);
            }

            return print_standard_arg(stream, spec, value, written);
        }

        template <size_t N>
        [[nodiscard]] auto print_arg(std::FILE* stream,
                                     PrintSpec const& spec,
                                     char8_t const (&value)[N],
                                     int* written) noexcept -> bool {
            if (spec.conversion == 's') {
                return print_str_ref_arg(stream, spec, StrRef(value), written);
            }

            return print_standard_arg(stream, spec, value, written);
        }

        [[nodiscard]] inline auto
        print_arg(std::FILE* stream, PrintSpec const& spec, std::nullptr_t, int* written) noexcept
            -> bool {
            if (spec.conversion == 's') {
                return print_str_ref_arg(stream, spec, StrRef("(null)"), written);
            }

            return print_standard_arg(stream, spec, static_cast<void const*>(nullptr), written);
        }

        template <typename Pointer>
        [[nodiscard]] auto print_pointer_arg(std::FILE* stream,
                                             PrintSpec const& spec,
                                             Pointer value,
                                             int* written) noexcept -> bool {
            if constexpr (std::is_object_v<std::remove_pointer_t<Pointer>>) {
                if (spec.conversion == 'p') {
                    return print_standard_arg(
                        stream, spec, static_cast<void const*>(value), written);
                }
            }

            return print_standard_arg(stream, spec, value, written);
        }

        template <typename Arg>
        [[nodiscard]] auto
        print_arg(std::FILE* stream, PrintSpec const& spec, Arg const& arg, int* written) noexcept
            -> bool {
            if constexpr (std::is_pointer_v<Arg>) {
                return print_pointer_arg(stream, spec, arg, written);
            } else {
                return print_standard_arg(stream, spec, arg, written);
            }
        }

        template <typename... Args>
        [[nodiscard]] auto print_format_args(std::FILE* stream,
                                             StrRef format,
                                             int* written,
                                             Args const&... args) noexcept -> bool;

        template <typename Arg, typename... Rest>
        [[nodiscard]] auto print_format_args(std::FILE* stream,
                                             StrRef format,
                                             int* written,
                                             Arg const& arg,
                                             Rest const&... rest) noexcept -> bool {
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
                size_t const next_offset = parse_print_spec(format, offset, &spec);
                if (next_offset == StrRef::NPOS) {
                    ++offset;
                    continue;
                }

                if (!print_text(stream, format.prefix(offset), written)) {
                    return false;
                }

                if (!print_arg(stream, spec, arg, written)) {
                    return false;
                }

                return print_format_args(stream, format.drop_prefix(next_offset), written, rest...);
            }

            return true;
        }

        template <>
        [[nodiscard]] inline auto
        print_format_args<>(std::FILE* stream, StrRef format, int* written) noexcept -> bool {
            return print_format_without_args(stream, format, written);
        }

    } // namespace detail

    auto print(std::FILE* stream, StrRef text) noexcept -> int;
    auto print(StrRef text) noexcept -> int;
    auto eprint(StrRef text) noexcept -> int;

    template <typename... Args>
    auto fprintf(std::FILE* stream, StrRef format, Args const&... args) noexcept -> int {
        int written = 0;
        if (!detail::print_format_args(stream, format, &written, args...)) {
            return -1;
        }

        return written;
    }

    template <typename... Args> auto printf(StrRef format, Args const&... args) noexcept -> int {
        return base::fprintf(stdout, format, args...);
    }

    template <typename... Args> auto eprintf(StrRef format, Args const&... args) noexcept -> int {
        return base::fprintf(stderr, format, args...);
    }

} // namespace base
