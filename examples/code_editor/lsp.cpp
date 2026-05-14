#include "lsp.h"

#include <algorithm>
#include <base/assert.h>
#include <base/string_buffer.h>
#include <base/unicode.h>
#include <charconv>
#include <cstring>

namespace code_editor {

    [[nodiscard]] auto lsp_cpp_file_name(StrRef file_name) -> bool {
        return file_name.ends_with_ignore_ascii_case(".c") ||
               file_name.ends_with_ignore_ascii_case(".cc") ||
               file_name.ends_with_ignore_ascii_case(".cpp") ||
               file_name.ends_with_ignore_ascii_case(".cxx") ||
               file_name.ends_with_ignore_ascii_case(".h") ||
               file_name.ends_with_ignore_ascii_case(".hh") ||
               file_name.ends_with_ignore_ascii_case(".hpp") ||
               file_name.ends_with_ignore_ascii_case(".hxx") ||
               file_name.ends_with_ignore_ascii_case(".inl") ||
               file_name.ends_with_ignore_ascii_case(".ipp");
    }

    [[nodiscard]] auto lsp_server_matches_file(LspServerConfig const& server, StrRef file_name)
        -> bool {
        if (!server.enabled || file_name.empty()) {
            return false;
        }
        bool const abap_dependency = file_name.starts_with_ignore_ascii_case("abapls-cache:");
        for (StrRef const extension : server.extensions) {
            if (extension.empty()) {
                continue;
            }
            if (abap_dependency && (extension.equals_ignore_ascii_case(".abap") ||
                                    extension.equals_ignore_ascii_case("abap"))) {
                return true;
            }
            if (extension.front() == '.' && file_name.ends_with_ignore_ascii_case(extension)) {
                return true;
            }
            if (extension.front() != '.' && file_name.size() > extension.size() &&
                file_name[file_name.size() - extension.size() - 1u] == '.' &&
                file_name.ends_with_ignore_ascii_case(extension)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto lsp_server_for_file(Slice<LspServerConfig const> servers, StrRef file_name)
        -> LspServerConfig const* {
        for (LspServerConfig const& server : servers) {
            if (lsp_server_matches_file(server, file_name)) {
                return &server;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto lsp_position_less(LspPosition lhs, LspPosition rhs) -> bool {
        return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.column < rhs.column);
    }

    [[nodiscard]] auto lsp_range_valid(LspRange range) -> bool {
        return lsp_position_less(range.start, range.end) ||
               (range.start.line == range.end.line && range.start.column == range.end.column);
    }

    [[nodiscard]] auto line_text_at(StrRef text, size_t line_index) -> StrRef {
        size_t line = 0u;
        size_t start = 0u;
        for (size_t index = 0u; index < text.size(); ++index) {
            if (line == line_index) {
                start = index;
                break;
            }
            if (text[index] == '\n') {
                line += 1u;
            }
        }
        if (line != line_index) {
            return {};
        }

        size_t end = start;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') {
            end += 1u;
        }
        return text.substr(start, end - start);
    }

    [[nodiscard]] auto lsp_offset_from_position(StrRef text, LspPosition position) -> size_t {
        size_t line = 0u;
        size_t line_start = 0u;
        for (size_t index = 0u; index < text.size() && line < position.line; ++index) {
            if (text[index] == '\n') {
                line += 1u;
                line_start = index + 1u;
            }
        }
        if (line != position.line) {
            return text.size();
        }

        size_t line_end = line_start;
        while (line_end < text.size() && text[line_end] != '\n' && text[line_end] != '\r') {
            line_end += 1u;
        }
        return std::min(line_start + position.column, line_end);
    }

    [[nodiscard]] auto utf8_advance(StrRef text, size_t offset) -> size_t {
        if (offset >= text.size()) {
            return offset;
        }
        if (base::utf8_codepoint_valid(text, offset)) {
            return std::min(text.size(), offset + base::utf8_codepoint_size(text, offset));
        }
        return offset + 1u;
    }

    [[nodiscard]] auto utf16_units(StrRef text, size_t offset) -> size_t {
        if (!base::utf8_codepoint_valid(text, offset)) {
            return 1u;
        }
        base::Utf8DecodeResult const decoded = base::utf8_decode(text, offset);
        return decoded.codepoint > 0xffffu ? 2u : 1u;
    }

    [[nodiscard]] auto lsp_byte_column_to_utf16(StrRef line, size_t byte_column) -> size_t {
        size_t units = 0u;
        size_t offset = 0u;
        size_t const end = std::min(byte_column, line.size());
        while (offset < end) {
            units += utf16_units(line, offset);
            offset = utf8_advance(line, offset);
        }
        return units;
    }

    [[nodiscard]] auto lsp_utf16_column_to_byte(StrRef line, size_t utf16_column) -> size_t {
        size_t units = 0u;
        size_t offset = 0u;
        while (offset < line.size()) {
            size_t const next_units = units + utf16_units(line, offset);
            if (next_units > utf16_column) {
                return offset;
            }
            units = next_units;
            offset = utf8_advance(line, offset);
            if (units == utf16_column) {
                return offset;
            }
        }
        return line.size();
    }

    [[nodiscard]] auto lsp_position_byte_to_utf16(StrRef text, LspPosition position)
        -> LspPosition {
        return {
            position.line,
            lsp_byte_column_to_utf16(line_text_at(text, position.line), position.column)
        };
    }

    [[nodiscard]] auto lsp_position_utf16_to_byte(StrRef text, LspPosition position)
        -> LspPosition {
        return {
            position.line,
            lsp_utf16_column_to_byte(line_text_at(text, position.line), position.column)
        };
    }

    [[nodiscard]] auto lsp_range_utf16_to_byte(StrRef text, LspRange range) -> LspRange {
        return {
            .start = lsp_position_utf16_to_byte(text, range.start),
            .end = lsp_position_utf16_to_byte(text, range.end),
        };
    }

    struct SnippetBuilder {
        StringBuffer text = {};
        LspPosition position = {};
        LspRange selection = {};
        LspRange final_selection = {};
        size_t selection_tabstop = static_cast<size_t>(-1);
        bool has_selection = false;
        bool has_final = false;
    };

    auto snippet_write_byte(SnippetBuilder& builder, char ch) -> void {
        builder.text.write_byte(ch);
        if (ch == '\n') {
            builder.position.line += 1u;
            builder.position.column = 0u;
        } else {
            builder.position.column += 1u;
        }
    }

    [[nodiscard]] auto snippet_parse_number(StrRef text, size_t& index, size_t& out) -> bool {
        size_t const start = index;
        out = 0u;
        while (index < text.size() && is_ascii_digit(text[index])) {
            out = out * 10u + static_cast<size_t>(text[index] - '0');
            index += 1u;
        }
        return index != start;
    }

    [[nodiscard]] auto snippet_identifier_char(char ch) -> bool {
        return is_ascii_alphanumeric(ch) || ch == '_';
    }

    auto snippet_skip_identifier(StrRef text, size_t& index) -> void {
        while (index < text.size() && snippet_identifier_char(text[index])) {
            index += 1u;
        }
    }

    auto snippet_record_tabstop(
        SnippetBuilder& builder, size_t tabstop, LspPosition start, LspPosition end
    ) -> void {
        if (tabstop == 0u) {
            if (!builder.has_final) {
                builder.final_selection = {start, end};
                builder.has_final = true;
            }
            return;
        }
        if (!builder.has_selection || tabstop < builder.selection_tabstop) {
            builder.selection = {start, end};
            builder.selection_tabstop = tabstop;
            builder.has_selection = true;
        }
    }

    auto snippet_skip_braced(StrRef text, size_t& index) -> void {
        size_t depth = 1u;
        while (index < text.size() && depth != 0u) {
            if (text[index] == '\\' && index + 1u < text.size()) {
                index += 2u;
            } else if (text[index] == '$' && index + 1u < text.size() && text[index + 1u] == '{') {
                depth += 1u;
                index += 2u;
            } else if (text[index] == '}') {
                depth -= 1u;
                index += 1u;
            } else {
                index += 1u;
            }
        }
    }

    auto snippet_parse_text(SnippetBuilder& builder, StrRef text, size_t& index, char terminator)
        -> void;

    auto snippet_parse_choice(SnippetBuilder& builder, StrRef text, size_t& index) -> void {
        bool write = true;
        while (index < text.size()) {
            char const ch = text[index];
            if (ch == '|' && index + 1u < text.size() && text[index + 1u] == '}') {
                index += 2u;
                return;
            }
            if (ch == '\\' && index + 1u < text.size()) {
                char const escaped = text[index + 1u];
                if (escaped == ',' || escaped == '|' || escaped == '\\') {
                    if (write) {
                        snippet_write_byte(builder, escaped);
                    }
                    index += 2u;
                    continue;
                }
            }
            if (ch == ',') {
                write = false;
            } else if (write) {
                snippet_write_byte(builder, ch);
            }
            index += 1u;
        }
    }

    auto snippet_parse_braced(SnippetBuilder& builder, StrRef text, size_t& index) -> void {
        size_t tabstop = 0u;
        if (snippet_parse_number(text, index, tabstop)) {
            LspPosition const start = builder.position;
            if (index < text.size() && text[index] == '}') {
                index += 1u;
                snippet_record_tabstop(builder, tabstop, start, start);
            } else if (index < text.size() && text[index] == ':') {
                index += 1u;
                snippet_parse_text(builder, text, index, '}');
                LspPosition const end = builder.position;
                if (index < text.size() && text[index] == '}') {
                    index += 1u;
                }
                snippet_record_tabstop(builder, tabstop, start, end);
            } else if (index < text.size() && text[index] == '|') {
                index += 1u;
                snippet_parse_choice(builder, text, index);
                snippet_record_tabstop(builder, tabstop, start, builder.position);
            } else {
                snippet_skip_braced(text, index);
                snippet_record_tabstop(builder, tabstop, start, start);
            }
            return;
        }

        if (index < text.size() && snippet_identifier_char(text[index])) {
            snippet_skip_identifier(text, index);
            if (index < text.size() && text[index] == ':') {
                index += 1u;
                snippet_parse_text(builder, text, index, '}');
                if (index < text.size() && text[index] == '}') {
                    index += 1u;
                }
            } else {
                snippet_skip_braced(text, index);
            }
            return;
        }

        snippet_skip_braced(text, index);
    }

    [[nodiscard]] auto snippet_parse_dollar(SnippetBuilder& builder, StrRef text, size_t& index)
        -> bool {
        if (index + 1u >= text.size()) {
            return false;
        }

        size_t next = index + 1u;
        size_t tabstop = 0u;
        if (snippet_parse_number(text, next, tabstop)) {
            LspPosition const position = builder.position;
            snippet_record_tabstop(builder, tabstop, position, position);
            index = next;
            return true;
        }

        if (text[next] == '{') {
            index = next + 1u;
            snippet_parse_braced(builder, text, index);
            return true;
        }

        if (snippet_identifier_char(text[next])) {
            index = next;
            snippet_skip_identifier(text, index);
            return true;
        }

        return false;
    }

    auto snippet_parse_text(SnippetBuilder& builder, StrRef text, size_t& index, char terminator)
        -> void {
        while (index < text.size()) {
            char const ch = text[index];
            if (terminator != '\0' && ch == terminator) {
                return;
            }
            if (ch == '\\' && index + 1u < text.size()) {
                char const escaped = text[index + 1u];
                if (escaped == '$' || escaped == '}' || escaped == '\\') {
                    snippet_write_byte(builder, escaped);
                    index += 2u;
                    continue;
                }
            }
            if (ch == '$' && snippet_parse_dollar(builder, text, index)) {
                continue;
            }
            snippet_write_byte(builder, ch);
            index += 1u;
        }
    }

    [[nodiscard]] auto lsp_expand_snippet(Arena& arena, StrRef snippet) -> LspSnippetExpansion {
        SnippetBuilder builder = {};
        builder.text.init(snippet.size() + 1u, arena.resource());
        size_t index = 0u;
        snippet_parse_text(builder, snippet, index, '\0');

        LspRange selection = {builder.position, builder.position};
        bool has_selection = false;
        if (builder.has_selection) {
            selection = builder.selection;
            has_selection =
                !(selection.start.line == selection.end.line &&
                  selection.start.column == selection.end.column);
        } else if (builder.has_final) {
            selection = builder.final_selection;
        }
        return {
            .text = arena_copy_str(arena, builder.text.str()),
            .selection = selection,
            .has_selection = has_selection,
        };
    }

    [[nodiscard]] auto edit_path_matches(LspTextEdit const& edit, StrRef path) -> bool {
        return edit.path.empty() || edit.path == path;
    }

    [[nodiscard]] auto
    lsp_apply_text_edits(Arena& arena, StrRef text, Slice<LspTextEdit const> edits, StrRef path)
        -> StrRef {
        if (edits.empty()) {
            return text;
        }

        LspTextEdit* sorted = arena_alloc<LspTextEdit>(arena, edits.size());
        size_t count = 0u;
        for (LspTextEdit const& edit : edits) {
            if (edit_path_matches(edit, path) && lsp_range_valid(edit.range)) {
                sorted[count] = edit;
                count += 1u;
            }
        }
        if (count == 0u) {
            return text;
        }

        std::sort(sorted, sorted + count, [](LspTextEdit const& a, LspTextEdit const& b) {
            if (a.range.start.line != b.range.start.line) {
                return a.range.start.line > b.range.start.line;
            }
            return a.range.start.column > b.range.start.column;
        });

        StringBuffer buffer = {};
        buffer.init(text.size() + 1024u, arena.resource());
        buffer.write_string(text);
        for (size_t index = 0u; index < count; ++index) {
            LspTextEdit const& edit = sorted[index];
            size_t const start = lsp_offset_from_position(buffer.str(), edit.range.start);
            size_t const end = lsp_offset_from_position(buffer.str(), edit.range.end);
            if (end < start || end > buffer.size()) {
                continue;
            }

            size_t const new_size = buffer.size() - (end - start) + edit.new_text.size();
            StringBuffer next = {};
            next.init(new_size + 1u, arena.resource());
            next.write_bytes(buffer.data(), start);
            next.write_string(edit.new_text);
            next.write_bytes(buffer.data() + end, buffer.size() - end);
            buffer = next;
            next = {};
        }

        return arena_copy_str(arena, buffer.str());
    }

    [[nodiscard]] auto uri_unreserved(char ch) -> bool {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/' || ch == ':';
    }

    [[nodiscard]] auto hex_value(char ch) -> int32_t {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    }

    auto write_uri_byte(StringBuffer& buffer, uint8_t byte) -> void {
        char constexpr HEX[] = "0123456789ABCDEF";
        buffer.write_byte('%');
        buffer.write_byte(HEX[byte >> 4u]);
        buffer.write_byte(HEX[byte & 0x0fu]);
    }

    [[nodiscard]] auto lsp_path_to_file_uri(Arena& arena, StrRef path) -> StrRef {
        if (path.starts_with_ignore_ascii_case("abapls-cache:")) {
            return arena_copy_cstr(arena, path);
        }
        StringBuffer buffer = {};
        buffer.init(path.size() * 3u + 16u, arena.resource());
        buffer.write_string("file:///");
        for (char ch : path) {
            char const out = ch == '\\' ? '/' : ch;
            if (uri_unreserved(out)) {
                buffer.write_byte(out);
            } else {
                write_uri_byte(buffer, static_cast<uint8_t>(out));
            }
        }
        return arena_copy_cstr(arena, buffer.str());
    }

    [[nodiscard]] auto lsp_file_uri_to_path(Arena& arena, StrRef uri) -> StrRef {
        StrRef text = uri;
        if (text.starts_with_ignore_ascii_case("file:///")) {
            text.remove_prefix(8u);
        } else if (text.starts_with_ignore_ascii_case("file://")) {
            text.remove_prefix(7u);
        } else {
            return arena_copy_cstr(arena, uri);
        }

        StringBuffer buffer = {};
        buffer.init(text.size() + 1u, arena.resource());
        for (size_t index = 0u; index < text.size(); ++index) {
            char ch = text[index];
            if (ch == '%' && index + 2u < text.size()) {
                int32_t const hi = hex_value(text[index + 1u]);
                int32_t const lo = hex_value(text[index + 2u]);
                if (hi >= 0 && lo >= 0) {
                    ch = static_cast<char>((hi << 4) | lo);
                    index += 2u;
                }
            } else if (ch == '/') {
                ch = '\\';
            }
            buffer.write_byte(ch);
        }
        return arena_copy_cstr(arena, buffer.str());
    }

    auto lsp_json_write_escaped_string(StringBuffer& buffer, StrRef text) -> void {
        buffer.write_byte('"');
        for (char ch : text) {
            switch (ch) {
            case '\\':
                buffer.write_string("\\\\");
                break;
            case '"':
                buffer.write_string("\\\"");
                break;
            case '\n':
                buffer.write_string("\\n");
                break;
            case '\r':
                buffer.write_string("\\r");
                break;
            case '\t':
                buffer.write_string("\\t");
                break;
            default:
                if (static_cast<uint8_t>(ch) < 0x20u) {
                    buffer.write_string("\\u00");
                    char constexpr HEX[] = "0123456789abcdef";
                    buffer.write_byte(HEX[static_cast<uint8_t>(ch) >> 4u]);
                    buffer.write_byte(HEX[static_cast<uint8_t>(ch) & 0x0fu]);
                } else {
                    buffer.write_byte(ch);
                }
                break;
            }
        }
        buffer.write_byte('"');
    }

    auto lsp_write_json_rpc_message(StringBuffer& buffer, StrRef json) -> bool {
        char header[64] = {};
        auto const result =
            std::to_chars(header, header + sizeof(header), static_cast<uint64_t>(json.size()));
        if (result.ec != std::errc{}) {
            return false;
        }
        buffer.write_string("Content-Length: ");
        buffer.write_bytes(header, static_cast<size_t>(result.ptr - header));
        buffer.write_string("\r\n\r\n");
        buffer.write_string(json);
        return true;
    }

    [[nodiscard]] auto lsp_framer_init(LspFramer& framer, MemoryResource* resource) -> bool {
        return framer.bytes.init(4096u, resource);
    }

    auto lsp_framer_reset(LspFramer& framer) -> void {
        framer.bytes.clear();
    }

    [[nodiscard]] auto lsp_framer_append(LspFramer& framer, StrRef bytes) -> bool {
        return framer.bytes.append(Slice<char const>(bytes.data(), bytes.size())) == bytes.size();
    }

    [[nodiscard]] auto header_end(Slice<char const> bytes) -> size_t {
        for (size_t index = 0u; index + 3u < bytes.size(); ++index) {
            if (bytes[index] == '\r' && bytes[index + 1u] == '\n' && bytes[index + 2u] == '\r' &&
                bytes[index + 3u] == '\n') {
                return index;
            }
        }
        return StrRef::NPOS;
    }

    [[nodiscard]] auto content_length(Slice<char const> bytes, size_t end, size_t& out) -> bool {
        StrRef const key = "Content-Length:";
        size_t line = 0u;
        while (line < end) {
            size_t next = line;
            while (next < end && bytes[next] != '\r' && bytes[next] != '\n') {
                next += 1u;
            }
            StrRef const text(bytes.data() + line, next - line);
            if (text.starts_with_ignore_ascii_case(key)) {
                StrRef value = text.substr(key.size()).trim();
                uint64_t length = 0u;
                auto const result =
                    std::from_chars(value.data(), value.data() + value.size(), length);
                if (result.ec != std::errc{}) {
                    return false;
                }
                out = static_cast<size_t>(length);
                return true;
            }
            line = next + 1u;
            if (line < end && bytes[line - 1u] == '\r' && bytes[line] == '\n') {
                line += 1u;
            }
        }
        return false;
    }

    [[nodiscard]] auto lsp_framer_next_message(LspFramer& framer, Arena& arena, StrRef& out_message)
        -> bool {
        Slice<char const> const bytes = framer.bytes.slice();
        size_t const end = header_end(bytes);
        if (end == StrRef::NPOS) {
            return false;
        }
        size_t length = 0u;
        if (!content_length(bytes, end, length)) {
            framer.bytes.clear();
            return false;
        }
        size_t const payload = end + 4u;
        if (bytes.size() < payload + length) {
            return false;
        }

        out_message = arena_copy_str(arena, StrRef(bytes.data() + payload, length));
        size_t const remaining = bytes.size() - payload - length;
        if (remaining != 0u) {
            std::memmove(framer.bytes.data(), framer.bytes.data() + payload + length, remaining);
        }
        framer.bytes.resize(remaining);
        return true;
    }

} // namespace code_editor
