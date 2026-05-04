#pragma once

#include <algorithm>
#include <base/config.h>
#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <base/unicode.h>
#include <cstddef>
#include <cstdint>
#include <gui/hot_reload_app.h>

namespace code_editor {

#if defined(_WIN32) && BASE_DEBUG
    inline constexpr bool HOT_RELOAD_ENABLED = true;
#else
    inline constexpr bool HOT_RELOAD_ENABLED = false;
#endif

    inline constexpr uint32_t INITIAL_WINDOW_WIDTH = 1320u;
    inline constexpr uint32_t INITIAL_WINDOW_HEIGHT = 820u;
    inline constexpr size_t MAX_KEY_EVENTS_PER_FRAME = 64u;
    inline constexpr size_t MODULE_STORAGE_SIZE = 128u * 1024u;
    inline constexpr size_t MODULE_STORAGE_ALIGNMENT = 64u;
    inline constexpr StrRef MODULE_FILE_NAME = "code_editor_module.dll";
    inline constexpr uint32_t HOT_RELOAD_POLL_MS = 250u;

    [[nodiscard]] inline auto hex_digit(uint8_t value) -> char {
        return static_cast<char>(value < 10u ? '0' + value : 'a' + value - 10u);
    }

    inline auto append_hex_byte(char*& out, uint8_t value) -> void {
        *out++ = hex_digit(value >> 4u);
        *out++ = hex_digit(value & 0x0fu);
    }

    inline auto append_hex_u32(char*& out, uint32_t value) -> void {
        for (int32_t shift = 28; shift >= 0; shift -= 4) {
            *out++ = hex_digit(static_cast<uint8_t>((value >> shift) & 0x0fu));
        }
    }

    [[nodiscard]] inline auto editor_text_supported(StrRef text) -> bool {
        size_t offset = 0u;
        while (offset < text.size()) {
            uint8_t const byte = static_cast<uint8_t>(text[offset]);
            if (byte < 0x80u) {
                if ((byte >= 0x20u && byte < 0x7fu) || byte == '\n' || byte == '\r' ||
                    byte == '\t') {
                    offset += 1u;
                    continue;
                }
                return false;
            }

            if (!base::utf8_codepoint_valid(text, offset)) {
                return false;
            }
            offset += base::utf8_codepoint_size(text, offset);
        }
        return true;
    }

    [[nodiscard]] inline auto editor_display_text(Arena& arena, StrRef text) -> StrRef {
        if (text.starts_with("\xef\xbb\xbf")) {
            text.remove_prefix(3u);
        }
        if (editor_text_supported(text)) {
            return text;
        }

        size_t const line_count = (text.size() + 15u) / 16u;
        char* const display = arena_alloc<char>(arena, line_count * 58u + 1u);
        char* out = display;
        for (size_t offset = 0u; offset < text.size(); offset += 16u) {
            append_hex_u32(out, static_cast<uint32_t>(offset));
            *out++ = ' ';
            *out++ = ' ';
            size_t const end = std::min(offset + 16u, text.size());
            for (size_t index = offset; index < end; ++index) {
                append_hex_byte(out, static_cast<uint8_t>(text[index]));
                if (index + 1u < end) {
                    *out++ = ' ';
                }
            }
            *out++ = '\n';
        }
        *out = '\0';
        return StrRef(display, static_cast<size_t>(out - display));
    }

    struct FileTreeEntry {
        StrRef name = {};
        StrRef path = {};
        StrRef relative_path = {};
        size_t depth = 0u;
        bool is_directory = false;
        bool open = false;
    };

    struct ModuleRuntimeContext {
        gui::render::Context render_context = {};
        void* native_window = nullptr;
        StrRef initial_text = {};
        StrRef initial_file_name = {};
        StrRef initial_file_path = {};
        StrRef tree_root_name = {};
        StrRef save_root_path = {};
        Slice<FileTreeEntry> tree_files = {};
        bool initial_sidebar_visible = false;
    };

    using DrawCommandCounts = gui::HotReloadDrawCommandCounts;
    using FrameResult = gui::HotReloadFrameResult;
    using ModuleApi = gui::HotReloadAppApi;

} // namespace code_editor
