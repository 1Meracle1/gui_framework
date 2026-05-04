#include <base/memory.h>
#include <base/unicode.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <limits>
#include <windows.h>

namespace {

    [[nodiscard]] auto text_size_to_int(StrRef text, int& out_size) -> bool {
        if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }

        out_size = static_cast<int>(text.size());
        return true;
    }

} // namespace

namespace base {

    [[nodiscard]] auto utf8_to_wide(StrRef text, Arena& arena, wchar_t*& out_text, int& out_len)
        -> bool {
        int input_size = 0;
        if (!text_size_to_int(text, input_size)) {
            return false;
        }

        if (input_size == 0) {
            wchar_t* const wide_text = arena_alloc<wchar_t>(arena, 1u);
            wide_text[0] = L'\0';
            out_text = wide_text;
            out_len = 0;
            return true;
        }

        int const wide_len =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
        if (wide_len <= 0) {
            return false;
        }

        wchar_t* const wide_text = arena_alloc<wchar_t>(arena, static_cast<size_t>(wide_len) + 1u);
        int const converted = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, wide_text, wide_len
        );
        if (converted != wide_len) {
            return false;
        }

        wide_text[wide_len] = L'\0';
        out_text = wide_text;
        out_len = wide_len;
        return true;
    }

    [[nodiscard]] auto
    wide_to_utf8(wchar_t const* text, int text_len, Arena& arena, StrRef& out_text) -> bool {
        if (text == nullptr || text_len < 0) {
            return false;
        }

        if (text_len == 0) {
            out_text = {};
            return true;
        }

        int const byte_count = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text, text_len, nullptr, 0, nullptr, nullptr
        );
        if (byte_count <= 0) {
            return false;
        }

        char* const utf8_text = arena_alloc<char>(arena, static_cast<size_t>(byte_count) + 1u);
        int const converted = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text, text_len, utf8_text, byte_count, nullptr, nullptr
        );
        if (converted != byte_count) {
            return false;
        }

        utf8_text[byte_count] = '\0';
        out_text = StrRef(utf8_text, static_cast<size_t>(byte_count));
        return true;
    }

} // namespace base
#endif
