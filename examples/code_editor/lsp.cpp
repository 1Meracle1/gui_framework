#include "lsp.h"

#include <algorithm>
#include <base/assert.h>
#include <base/string_buffer.h>
#include <base/unicode.h>
#include <charconv>
#include <cmath>
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
        BASE_UNUSED(buffer.init(text.size() + 1024u, arena.resource()));
        BASE_UNUSED(buffer.write_string(text));
        for (size_t index = 0u; index < count; ++index) {
            LspTextEdit const& edit = sorted[index];
            size_t const start = lsp_offset_from_position(buffer.str(), edit.range.start);
            size_t const end = lsp_offset_from_position(buffer.str(), edit.range.end);
            if (end < start || end > buffer.size()) {
                continue;
            }

            size_t const new_size = buffer.size() - (end - start) + edit.new_text.size();
            StringBuffer next = {};
            BASE_UNUSED(next.init(new_size + 1u, arena.resource()));
            BASE_UNUSED(next.write_bytes(buffer.data(), start));
            BASE_UNUSED(next.write_string(edit.new_text));
            BASE_UNUSED(next.write_bytes(buffer.data() + end, buffer.size() - end));
            buffer = std::move(next);
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
        BASE_UNUSED(buffer.write_byte('%'));
        BASE_UNUSED(buffer.write_byte(HEX[byte >> 4u]));
        BASE_UNUSED(buffer.write_byte(HEX[byte & 0x0fu]));
    }

    [[nodiscard]] auto lsp_path_to_file_uri(Arena& arena, StrRef path) -> StrRef {
        StringBuffer buffer = {};
        BASE_UNUSED(buffer.init(path.size() * 3u + 16u, arena.resource()));
        BASE_UNUSED(buffer.write_string("file:///"));
        for (char ch : path) {
            char const out = ch == '\\' ? '/' : ch;
            if (uri_unreserved(out)) {
                BASE_UNUSED(buffer.write_byte(out));
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
        }

        StringBuffer buffer = {};
        BASE_UNUSED(buffer.init(text.size() + 1u, arena.resource()));
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
            BASE_UNUSED(buffer.write_byte(ch));
        }
        return arena_copy_cstr(arena, buffer.str());
    }

    auto lsp_json_write_escaped_string(StringBuffer& buffer, StrRef text) -> void {
        BASE_UNUSED(buffer.write_byte('"'));
        for (char ch : text) {
            switch (ch) {
            case '\\':
                BASE_UNUSED(buffer.write_string("\\\\"));
                break;
            case '"':
                BASE_UNUSED(buffer.write_string("\\\""));
                break;
            case '\n':
                BASE_UNUSED(buffer.write_string("\\n"));
                break;
            case '\r':
                BASE_UNUSED(buffer.write_string("\\r"));
                break;
            case '\t':
                BASE_UNUSED(buffer.write_string("\\t"));
                break;
            default:
                if (static_cast<uint8_t>(ch) < 0x20u) {
                    BASE_UNUSED(buffer.write_string("\\u00"));
                    char constexpr HEX[] = "0123456789abcdef";
                    BASE_UNUSED(buffer.write_byte(HEX[static_cast<uint8_t>(ch) >> 4u]));
                    BASE_UNUSED(buffer.write_byte(HEX[static_cast<uint8_t>(ch) & 0x0fu]));
                } else {
                    BASE_UNUSED(buffer.write_byte(ch));
                }
                break;
            }
        }
        BASE_UNUSED(buffer.write_byte('"'));
    }

    auto lsp_write_json_rpc_message(StringBuffer& buffer, StrRef json) -> bool {
        char header[64] = {};
        auto const result =
            std::to_chars(header, header + sizeof(header), static_cast<uint64_t>(json.size()));
        if (result.ec != std::errc{}) {
            return false;
        }
        BASE_UNUSED(buffer.write_string("Content-Length: "));
        BASE_UNUSED(buffer.write_bytes(header, static_cast<size_t>(result.ptr - header)));
        BASE_UNUSED(buffer.write_string("\r\n\r\n"));
        BASE_UNUSED(buffer.write_string(json));
        return true;
    }

    struct JsonParser {
        Arena* arena = nullptr;
        StrRef text = {};
        size_t index = 0u;
    };

    auto skip_ws(JsonParser& parser) -> void {
        while (parser.index < parser.text.size()) {
            char const ch = parser.text[parser.index];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            parser.index += 1u;
        }
    }

    [[nodiscard]] auto consume(JsonParser& parser, char ch) -> bool {
        skip_ws(parser);
        if (parser.index >= parser.text.size() || parser.text[parser.index] != ch) {
            return false;
        }
        parser.index += 1u;
        return true;
    }

    [[nodiscard]] auto consume_literal(JsonParser& parser, StrRef literal) -> bool {
        skip_ws(parser);
        if (!parser.text.substr(parser.index).starts_with(literal)) {
            return false;
        }
        parser.index += literal.size();
        return true;
    }

    [[nodiscard]] auto hex4(StrRef text, uint32_t& out) -> bool {
        if (text.size() < 4u) {
            return false;
        }
        uint32_t value = 0u;
        for (size_t index = 0u; index < 4u; ++index) {
            int32_t const digit = hex_value(text[index]);
            if (digit < 0) {
                return false;
            }
            value = (value << 4u) | static_cast<uint32_t>(digit);
        }
        out = value;
        return true;
    }

    auto write_utf8(StringBuffer& buffer, uint32_t codepoint) -> void {
        if (codepoint <= 0x7fu) {
            BASE_UNUSED(buffer.write_byte(static_cast<char>(codepoint)));
        } else if (codepoint <= 0x7ffu) {
            BASE_UNUSED(buffer.write_byte(static_cast<char>(0xc0u | (codepoint >> 6u))));
            BASE_UNUSED(buffer.write_byte(static_cast<char>(0x80u | (codepoint & 0x3fu))));
        } else {
            BASE_UNUSED(buffer.write_byte(static_cast<char>(0xe0u | (codepoint >> 12u))));
            BASE_UNUSED(buffer.write_byte(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu))));
            BASE_UNUSED(buffer.write_byte(static_cast<char>(0x80u | (codepoint & 0x3fu))));
        }
    }

    [[nodiscard]] auto parse_string(JsonParser& parser, StrRef& out) -> bool {
        skip_ws(parser);
        if (parser.index >= parser.text.size() || parser.text[parser.index] != '"') {
            return false;
        }
        parser.index += 1u;
        size_t const start = parser.index;
        bool escaped = false;
        while (parser.index < parser.text.size()) {
            char const ch = parser.text[parser.index];
            if (ch == '"') {
                StrRef const raw = parser.text.substr(start, parser.index - start);
                parser.index += 1u;
                if (!escaped) {
                    out = raw;
                    return true;
                }

                StringBuffer buffer = {};
                BASE_UNUSED(buffer.init(raw.size() + 1u, parser.arena->resource()));
                for (size_t index = 0u; index < raw.size(); ++index) {
                    char const c = raw[index];
                    if (c != '\\' || index + 1u >= raw.size()) {
                        BASE_UNUSED(buffer.write_byte(c));
                        continue;
                    }
                    char const e = raw[++index];
                    switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        BASE_UNUSED(buffer.write_byte(e));
                        break;
                    case 'b':
                        BASE_UNUSED(buffer.write_byte('\b'));
                        break;
                    case 'f':
                        BASE_UNUSED(buffer.write_byte('\f'));
                        break;
                    case 'n':
                        BASE_UNUSED(buffer.write_byte('\n'));
                        break;
                    case 'r':
                        BASE_UNUSED(buffer.write_byte('\r'));
                        break;
                    case 't':
                        BASE_UNUSED(buffer.write_byte('\t'));
                        break;
                    case 'u': {
                        uint32_t codepoint = 0u;
                        if (index + 4u <= raw.size() &&
                            hex4(raw.substr(index + 1u, 4u), codepoint)) {
                            write_utf8(buffer, codepoint);
                            index += 4u;
                        }
                    } break;
                    default:
                        BASE_UNUSED(buffer.write_byte(e));
                        break;
                    }
                }
                out = arena_copy_str(*parser.arena, buffer.str());
                return true;
            }
            if (ch == '\\') {
                escaped = true;
                parser.index += 1u;
                if (parser.index < parser.text.size()) {
                    parser.index += 1u;
                }
            } else {
                parser.index += 1u;
            }
        }
        return false;
    }

    [[nodiscard]] auto parse_value(JsonParser& parser, LspJsonValue const*& out) -> bool;

    [[nodiscard]] auto make_value(JsonParser& parser) -> LspJsonValue* {
        auto* const value = arena_new<LspJsonValue>(*parser.arena);
        *value = {};
        return value;
    }

    [[nodiscard]] auto parse_array(JsonParser& parser, LspJsonValue& value) -> bool {
        if (!consume(parser, '[')) {
            return false;
        }

        LspJsonValue const** values = nullptr;
        size_t count = 0u;
        size_t capacity = 0u;
        skip_ws(parser);
        if (parser.index < parser.text.size() && parser.text[parser.index] == ']') {
            parser.index += 1u;
            value.kind = LspJsonKind::ARRAY;
            return true;
        }

        for (;;) {
            LspJsonValue const* element = nullptr;
            if (!parse_value(parser, element)) {
                return false;
            }
            if (count == capacity) {
                size_t const new_capacity = capacity == 0u ? 8u : capacity * 2u;
                LspJsonValue const** next =
                    arena_alloc<LspJsonValue const*>(*parser.arena, new_capacity);
                if (values != nullptr) {
                    std::memcpy(next, values, sizeof(values[0u]) * count);
                }
                values = next;
                capacity = new_capacity;
            }
            values[count++] = element;

            skip_ws(parser);
            if (consume(parser, ']')) {
                value.kind = LspJsonKind::ARRAY;
                value.array = Slice<LspJsonValue const*>(values, count);
                return true;
            }
            if (!consume(parser, ',')) {
                return false;
            }
        }
    }

    [[nodiscard]] auto parse_object(JsonParser& parser, LspJsonValue& value) -> bool {
        if (!consume(parser, '{')) {
            return false;
        }

        LspJsonMember* members = nullptr;
        size_t count = 0u;
        size_t capacity = 0u;
        skip_ws(parser);
        if (parser.index < parser.text.size() && parser.text[parser.index] == '}') {
            parser.index += 1u;
            value.kind = LspJsonKind::OBJECT;
            return true;
        }

        for (;;) {
            StrRef key = {};
            LspJsonValue const* member_value = nullptr;
            if (!parse_string(parser, key) || !consume(parser, ':') ||
                !parse_value(parser, member_value)) {
                return false;
            }
            if (count == capacity) {
                size_t const new_capacity = capacity == 0u ? 8u : capacity * 2u;
                LspJsonMember* next = arena_alloc<LspJsonMember>(*parser.arena, new_capacity);
                if (members != nullptr) {
                    std::memcpy(next, members, sizeof(members[0u]) * count);
                }
                members = next;
                capacity = new_capacity;
            }
            members[count++] = {.key = key, .value = member_value};

            skip_ws(parser);
            if (consume(parser, '}')) {
                value.kind = LspJsonKind::OBJECT;
                value.object = Slice<LspJsonMember>(members, count);
                return true;
            }
            if (!consume(parser, ',')) {
                return false;
            }
        }
    }

    [[nodiscard]] auto parse_number(JsonParser& parser, LspJsonValue& value) -> bool {
        skip_ws(parser);
        size_t const start = parser.index;
        if (parser.index < parser.text.size() && parser.text[parser.index] == '-') {
            parser.index += 1u;
        }
        while (parser.index < parser.text.size() && parser.text[parser.index] >= '0' &&
               parser.text[parser.index] <= '9') {
            parser.index += 1u;
        }
        if (parser.index < parser.text.size() && parser.text[parser.index] == '.') {
            parser.index += 1u;
            while (parser.index < parser.text.size() && parser.text[parser.index] >= '0' &&
                   parser.text[parser.index] <= '9') {
                parser.index += 1u;
            }
        }
        if (parser.index == start) {
            return false;
        }

        StrRef const text = parser.text.substr(start, parser.index - start);
        char temp[64] = {};
        size_t const size = text.copy_to(temp, sizeof(temp) - 1u);
        if (size != text.size()) {
            return false;
        }
        value.kind = LspJsonKind::NUMBER;
        value.number = std::strtod(temp, nullptr);
        return true;
    }

    [[nodiscard]] auto parse_value(JsonParser& parser, LspJsonValue const*& out) -> bool {
        skip_ws(parser);
        if (parser.index >= parser.text.size()) {
            return false;
        }

        LspJsonValue* const value = make_value(parser);
        char const ch = parser.text[parser.index];
        if (ch == '{') {
            if (!parse_object(parser, *value)) {
                return false;
            }
        } else if (ch == '[') {
            if (!parse_array(parser, *value)) {
                return false;
            }
        } else if (ch == '"') {
            value->kind = LspJsonKind::STRING;
            if (!parse_string(parser, value->string)) {
                return false;
            }
        } else if (ch == 't') {
            if (!consume_literal(parser, "true")) {
                return false;
            }
            value->kind = LspJsonKind::BOOL;
            value->bool_value = true;
        } else if (ch == 'f') {
            if (!consume_literal(parser, "false")) {
                return false;
            }
            value->kind = LspJsonKind::BOOL;
        } else if (ch == 'n') {
            if (!consume_literal(parser, "null")) {
                return false;
            }
            value->kind = LspJsonKind::NULL_VALUE;
        } else if (!parse_number(parser, *value)) {
            return false;
        }

        out = value;
        return true;
    }

    [[nodiscard]] auto lsp_json_parse(Arena& arena, StrRef text, LspJsonValue const*& out_value)
        -> bool {
        JsonParser parser = {.arena = &arena, .text = text};
        if (!parse_value(parser, out_value)) {
            return false;
        }
        skip_ws(parser);
        return parser.index == text.size();
    }

    [[nodiscard]] auto lsp_json_object_get(LspJsonValue const* value, StrRef key)
        -> LspJsonValue const* {
        if (value == nullptr || value->kind != LspJsonKind::OBJECT) {
            return nullptr;
        }
        for (LspJsonMember const& member : value->object) {
            if (member.key == key) {
                return member.value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto lsp_json_string(LspJsonValue const* value, StrRef& out) -> bool {
        if (value == nullptr || value->kind != LspJsonKind::STRING) {
            return false;
        }
        out = value->string;
        return true;
    }

    [[nodiscard]] auto lsp_json_int(LspJsonValue const* value, int32_t& out) -> bool {
        if (value == nullptr || value->kind != LspJsonKind::NUMBER ||
            value->number < static_cast<double>(INT32_MIN) ||
            value->number > static_cast<double>(INT32_MAX)) {
            return false;
        }
        out = static_cast<int32_t>(value->number);
        return true;
    }

    [[nodiscard]] auto lsp_json_size(LspJsonValue const* value, size_t& out) -> bool {
        if (value == nullptr || value->kind != LspJsonKind::NUMBER || value->number < 0.0) {
            return false;
        }
        out = static_cast<size_t>(value->number);
        return true;
    }

    [[nodiscard]] auto lsp_json_bool(LspJsonValue const* value, bool& out) -> bool {
        if (value == nullptr || value->kind != LspJsonKind::BOOL) {
            return false;
        }
        out = value->bool_value;
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
        BASE_UNUSED(framer.bytes.resize(remaining));
        return true;
    }

} // namespace code_editor
