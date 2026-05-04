#pragma once

#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

class Arena;

namespace base {

    struct Utf8DecodeResult {
        uint32_t codepoint = 0u;
        size_t size = 0u;
        bool ok = false;
    };

    [[nodiscard]] constexpr auto unicode_scalar_valid(uint32_t codepoint) -> bool {
        return codepoint <= 0x10ffffu && (codepoint < 0xd800u || codepoint > 0xdfffu);
    }

    [[nodiscard]] constexpr auto utf8_trailing_byte(uint8_t byte) -> bool {
        return (byte & 0xc0u) == 0x80u;
    }

    [[nodiscard]] constexpr auto utf8_codepoint_size(uint8_t byte) -> size_t {
        if (byte < 0x80u) {
            return 1u;
        }
        if (byte >= 0xc2u && byte <= 0xdfu) {
            return 2u;
        }
        if (byte >= 0xe0u && byte <= 0xefu) {
            return 3u;
        }
        if (byte >= 0xf0u && byte <= 0xf4u) {
            return 4u;
        }
        return 0u;
    }

    [[nodiscard]] constexpr auto utf8_codepoint_size(StrRef text, size_t offset) -> size_t {
        return offset < text.size() ? utf8_codepoint_size(static_cast<uint8_t>(text[offset])) : 0u;
    }

    [[nodiscard]] constexpr auto utf8_decode(StrRef text, size_t offset) -> Utf8DecodeResult {
        if (offset >= text.size()) {
            return {};
        }

        uint8_t const b0 = static_cast<uint8_t>(text[offset]);
        size_t const size = utf8_codepoint_size(b0);
        if (size == 0u || offset + size > text.size()) {
            return {};
        }

        for (size_t index = 1u; index < size; ++index) {
            if (!utf8_trailing_byte(static_cast<uint8_t>(text[offset + index]))) {
                return {};
            }
        }

        if (size == 1u) {
            return {b0, 1u, true};
        }

        uint8_t const b1 = static_cast<uint8_t>(text[offset + 1u]);
        if ((b0 == 0xe0u && b1 < 0xa0u) || (b0 == 0xedu && b1 >= 0xa0u) ||
            (b0 == 0xf0u && b1 < 0x90u) || (b0 == 0xf4u && b1 >= 0x90u)) {
            return {};
        }

        uint32_t codepoint = 0u;
        if (size == 2u) {
            codepoint =
                (static_cast<uint32_t>(b0 & 0x1fu) << 6u) | static_cast<uint32_t>(b1 & 0x3fu);
        } else if (size == 3u) {
            uint8_t const b2 = static_cast<uint8_t>(text[offset + 2u]);
            codepoint = (static_cast<uint32_t>(b0 & 0x0fu) << 12u) |
                        (static_cast<uint32_t>(b1 & 0x3fu) << 6u) |
                        static_cast<uint32_t>(b2 & 0x3fu);
        } else {
            uint8_t const b2 = static_cast<uint8_t>(text[offset + 2u]);
            uint8_t const b3 = static_cast<uint8_t>(text[offset + 3u]);
            codepoint = (static_cast<uint32_t>(b0 & 0x07u) << 18u) |
                        (static_cast<uint32_t>(b1 & 0x3fu) << 12u) |
                        (static_cast<uint32_t>(b2 & 0x3fu) << 6u) |
                        static_cast<uint32_t>(b3 & 0x3fu);
        }

        return unicode_scalar_valid(codepoint) ? Utf8DecodeResult{codepoint, size, true}
                                               : Utf8DecodeResult{};
    }

    [[nodiscard]] constexpr auto utf8_codepoint_valid(StrRef text, size_t offset) -> bool {
        return utf8_decode(text, offset).ok;
    }

    [[nodiscard]] inline auto utf8_from_codepoint(uint32_t codepoint, char out[4]) -> size_t {
        if (out == nullptr || !unicode_scalar_valid(codepoint)) {
            return 0u;
        }
        if (codepoint <= 0x7fu) {
            out[0] = static_cast<char>(codepoint);
            return 1u;
        }
        if (codepoint <= 0x7ffu) {
            out[0] = static_cast<char>(0xc0u | (codepoint >> 6u));
            out[1] = static_cast<char>(0x80u | (codepoint & 0x3fu));
            return 2u;
        }
        if (codepoint <= 0xffffu) {
            out[0] = static_cast<char>(0xe0u | (codepoint >> 12u));
            out[1] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
            out[2] = static_cast<char>(0x80u | (codepoint & 0x3fu));
            return 3u;
        }
        out[0] = static_cast<char>(0xf0u | (codepoint >> 18u));
        out[1] = static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu));
        out[2] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
        out[3] = static_cast<char>(0x80u | (codepoint & 0x3fu));
        return 4u;
    }

#if defined(_WIN32)
    [[nodiscard]] auto utf8_to_wide(StrRef text, Arena& arena, wchar_t*& out_text, int& out_len)
        -> bool;
    [[nodiscard]] auto
    wide_to_utf8(wchar_t const* text, int text_len, Arena& arena, StrRef& out_text) -> bool;
#endif

} // namespace base
