#include <base/assert.h>
#include <base/memory.h>
#include <base/string_buffer.h>
#include <cstdlib>
#include <cstring>
#include <encoding/json.h>
#include <new>

namespace {

    using encoding::JsonContainerRangeFn;
    using encoding::JsonKind;
    using encoding::JsonMember;
    using encoding::JsonParseError;
    using encoding::JsonValue;

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
            buffer.write_byte(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffu) {
            buffer.write_byte(static_cast<char>(0xc0u | (codepoint >> 6u)));
            buffer.write_byte(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            buffer.write_byte(static_cast<char>(0xe0u | (codepoint >> 12u)));
            buffer.write_byte(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            buffer.write_byte(static_cast<char>(0x80u | (codepoint & 0x3fu)));
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
                buffer.init(raw.size() + 1u, parser.arena->resource());
                for (size_t index = 0u; index < raw.size(); ++index) {
                    char const c = raw[index];
                    if (c != '\\' || index + 1u >= raw.size()) {
                        buffer.write_byte(c);
                        continue;
                    }
                    char const e = raw[++index];
                    switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        buffer.write_byte(e);
                        break;
                    case 'b':
                        buffer.write_byte('\b');
                        break;
                    case 'f':
                        buffer.write_byte('\f');
                        break;
                    case 'n':
                        buffer.write_byte('\n');
                        break;
                    case 'r':
                        buffer.write_byte('\r');
                        break;
                    case 't':
                        buffer.write_byte('\t');
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
                        buffer.write_byte(e);
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

    [[nodiscard]] auto parse_value(JsonParser& parser, JsonValue const*& out) -> bool;

    [[nodiscard]] auto make_value(JsonParser& parser) -> JsonValue* {
        return arena_new<JsonValue>(*parser.arena);
    }

    [[nodiscard]] auto parse_array(JsonParser& parser, JsonValue& value) -> bool {
        if (!consume(parser, '[')) {
            return false;
        }

        JsonValue const** values = nullptr;
        size_t count = 0u;
        size_t capacity = 0u;
        skip_ws(parser);
        if (parser.index < parser.text.size() && parser.text[parser.index] == ']') {
            parser.index += 1u;
            value.kind = JsonKind::ARRAY;
            new (&value.array) Slice<JsonValue const*>();
            return true;
        }

        for (;;) {
            JsonValue const* element = nullptr;
            if (!parse_value(parser, element)) {
                return false;
            }
            if (count == capacity) {
                size_t const new_capacity = capacity == 0u ? 8u : capacity * 2u;
                JsonValue const** next = arena_alloc<JsonValue const*>(*parser.arena, new_capacity);
                if (values != nullptr) {
                    std::memcpy(next, values, sizeof(values[0u]) * count);
                }
                values = next;
                capacity = new_capacity;
            }
            values[count++] = element;

            skip_ws(parser);
            if (consume(parser, ']')) {
                value.kind = JsonKind::ARRAY;
                new (&value.array) Slice<JsonValue const*>(values, count);
                return true;
            }
            if (!consume(parser, ',')) {
                return false;
            }
        }
    }

    [[nodiscard]] auto parse_object(JsonParser& parser, JsonValue& value) -> bool {
        if (!consume(parser, '{')) {
            return false;
        }

        JsonMember* members = nullptr;
        size_t count = 0u;
        size_t capacity = 0u;
        skip_ws(parser);
        if (parser.index < parser.text.size() && parser.text[parser.index] == '}') {
            parser.index += 1u;
            value.kind = JsonKind::OBJECT;
            new (&value.object) Slice<JsonMember>();
            return true;
        }

        for (;;) {
            StrRef key = {};
            JsonValue const* member_value = nullptr;
            if (!parse_string(parser, key) || !consume(parser, ':') ||
                !parse_value(parser, member_value)) {
                return false;
            }
            if (count == capacity) {
                size_t const new_capacity = capacity == 0u ? 8u : capacity * 2u;
                JsonMember* next = arena_alloc<JsonMember>(*parser.arena, new_capacity);
                if (members != nullptr) {
                    std::memcpy(next, members, sizeof(members[0u]) * count);
                }
                members = next;
                capacity = new_capacity;
            }
            members[count++] = {.key = key, .value = member_value};

            skip_ws(parser);
            if (consume(parser, '}')) {
                value.kind = JsonKind::OBJECT;
                new (&value.object) Slice<JsonMember>(members, count);
                return true;
            }
            if (!consume(parser, ',')) {
                return false;
            }
        }
    }

    [[nodiscard]] auto parse_number(JsonParser& parser, JsonValue& value) -> bool {
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
        value.kind = JsonKind::NUMBER;
        value.number = std::strtod(temp, nullptr);
        return true;
    }

    [[nodiscard]] auto parse_value(JsonParser& parser, JsonValue const*& out) -> bool {
        skip_ws(parser);
        if (parser.index >= parser.text.size()) {
            return false;
        }

        JsonValue* const value = make_value(parser);
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
            StrRef string = {};
            if (!parse_string(parser, string)) {
                return false;
            }
            value->kind = JsonKind::STRING;
            new (&value->string) StrRef(string);
        } else if (ch == 't') {
            if (!consume_literal(parser, "true")) {
                return false;
            }
            value->kind = JsonKind::BOOL;
            value->bool_value = true;
        } else if (ch == 'f') {
            if (!consume_literal(parser, "false")) {
                return false;
            }
            value->kind = JsonKind::BOOL;
        } else if (ch == 'n') {
            if (!consume_literal(parser, "null")) {
                return false;
            }
            value->kind = JsonKind::NULL_VALUE;
        } else if (!parse_number(parser, *value)) {
            return false;
        }

        out = value;
        return true;
    }

    [[nodiscard]] auto is_digit(char ch) -> bool {
        return ch >= '0' && ch <= '9';
    }

    struct JsonValidator {
        StrRef text = {};
        size_t index = 0u;
        size_t line = 0u;
        size_t column = 0u;
        JsonParseError error = {};
        JsonContainerRangeFn container_range_fn = nullptr;
        void* user_data = nullptr;
        bool failed = false;
    };

    [[nodiscard]] auto validate_at_end(JsonValidator const& parser) -> bool {
        return parser.index >= parser.text.size();
    }

    [[nodiscard]] auto validate_peek(JsonValidator const& parser) -> char {
        return validate_at_end(parser) ? '\0' : parser.text[parser.index];
    }

    auto validate_advance(JsonValidator& parser) -> void {
        if (validate_at_end(parser)) {
            return;
        }
        char const ch = parser.text[parser.index++];
        if (ch == '\n') {
            parser.line += 1u;
            parser.column = 0u;
        } else {
            parser.column += 1u;
        }
    }

    [[nodiscard]] auto validate_fail(JsonValidator& parser, StrRef message) -> bool {
        if (!parser.failed) {
            parser.error = {parser.line, parser.column, message};
            parser.failed = true;
        }
        return false;
    }

    auto validate_skip_ws(JsonValidator& parser) -> void {
        while (!validate_at_end(parser)) {
            char const ch = validate_peek(parser);
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            validate_advance(parser);
        }
    }

    [[nodiscard]] auto validate_hex_digit(char ch) -> bool {
        return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    }

    [[nodiscard]] auto validate_string(JsonValidator& parser) -> bool {
        if (validate_peek(parser) != '"') {
            return validate_fail(parser, "Expected string");
        }
        validate_advance(parser);
        while (!validate_at_end(parser)) {
            char const ch = validate_peek(parser);
            if (ch == '"') {
                validate_advance(parser);
                return true;
            }
            if (static_cast<uint8_t>(ch) < 0x20u) {
                return validate_fail(parser, "Invalid control character in string");
            }
            if (ch != '\\') {
                validate_advance(parser);
                continue;
            }

            validate_advance(parser);
            if (validate_at_end(parser)) {
                return validate_fail(parser, "Unfinished escape sequence");
            }
            char const escape = validate_peek(parser);
            switch (escape) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                validate_advance(parser);
                break;
            case 'u':
                validate_advance(parser);
                for (size_t digit = 0u; digit < 4u; ++digit) {
                    if (validate_at_end(parser) || !validate_hex_digit(validate_peek(parser))) {
                        return validate_fail(parser, "Invalid unicode escape");
                    }
                    validate_advance(parser);
                }
                break;
            default:
                return validate_fail(parser, "Invalid escape sequence");
            }
        }
        return validate_fail(parser, "Unterminated string");
    }

    [[nodiscard]] auto validate_digits(JsonValidator& parser) -> bool {
        if (validate_at_end(parser) || !is_digit(validate_peek(parser))) {
            return false;
        }
        while (!validate_at_end(parser) && is_digit(validate_peek(parser))) {
            validate_advance(parser);
        }
        return true;
    }

    [[nodiscard]] auto validate_number(JsonValidator& parser) -> bool {
        if (validate_peek(parser) == '-') {
            validate_advance(parser);
        }
        if (validate_at_end(parser)) {
            return validate_fail(parser, "Expected digit");
        }
        if (validate_peek(parser) == '0') {
            validate_advance(parser);
            if (!validate_at_end(parser) && is_digit(validate_peek(parser))) {
                return validate_fail(parser, "Leading zero in number");
            }
        } else if (!validate_digits(parser)) {
            return validate_fail(parser, "Expected digit");
        }

        if (validate_peek(parser) == '.') {
            validate_advance(parser);
            if (!validate_digits(parser)) {
                return validate_fail(parser, "Expected digit after decimal point");
            }
        }
        if (validate_peek(parser) == 'e' || validate_peek(parser) == 'E') {
            validate_advance(parser);
            if (validate_peek(parser) == '+' || validate_peek(parser) == '-') {
                validate_advance(parser);
            }
            if (!validate_digits(parser)) {
                return validate_fail(parser, "Expected exponent digit");
            }
        }
        return true;
    }

    [[nodiscard]] auto validate_literal(JsonValidator& parser, StrRef literal) -> bool {
        for (char const ch : literal) {
            if (validate_at_end(parser) || validate_peek(parser) != ch) {
                return validate_fail(parser, "Expected JSON literal");
            }
            validate_advance(parser);
        }
        return true;
    }

    [[nodiscard]] auto validate_value(JsonValidator& parser) -> bool;

    auto validate_add_container_range(JsonValidator& parser, size_t start_line, size_t end_line)
        -> void {
        if (parser.container_range_fn != nullptr && end_line > start_line) {
            parser.container_range_fn(parser.user_data, start_line, end_line);
        }
    }

    [[nodiscard]] auto validate_array(JsonValidator& parser) -> bool {
        size_t const start_line = parser.line;
        validate_advance(parser);
        validate_skip_ws(parser);
        if (validate_peek(parser) == ']') {
            size_t const end_line = parser.line;
            validate_advance(parser);
            validate_add_container_range(parser, start_line, end_line);
            return true;
        }
        for (;;) {
            if (!validate_value(parser)) {
                return false;
            }
            validate_skip_ws(parser);
            if (validate_peek(parser) == ']') {
                size_t const end_line = parser.line;
                validate_advance(parser);
                validate_add_container_range(parser, start_line, end_line);
                return true;
            }
            if (validate_peek(parser) != ',') {
                return validate_fail(parser, "Expected ',' or ']'");
            }
            validate_advance(parser);
            validate_skip_ws(parser);
        }
    }

    [[nodiscard]] auto validate_object(JsonValidator& parser) -> bool {
        size_t const start_line = parser.line;
        validate_advance(parser);
        validate_skip_ws(parser);
        if (validate_peek(parser) == '}') {
            size_t const end_line = parser.line;
            validate_advance(parser);
            validate_add_container_range(parser, start_line, end_line);
            return true;
        }
        for (;;) {
            if (!validate_string(parser)) {
                return false;
            }
            validate_skip_ws(parser);
            if (validate_peek(parser) != ':') {
                return validate_fail(parser, "Expected ':'");
            }
            validate_advance(parser);
            if (!validate_value(parser)) {
                return false;
            }
            validate_skip_ws(parser);
            if (validate_peek(parser) == '}') {
                size_t const end_line = parser.line;
                validate_advance(parser);
                validate_add_container_range(parser, start_line, end_line);
                return true;
            }
            if (validate_peek(parser) != ',') {
                return validate_fail(parser, "Expected ',' or '}'");
            }
            validate_advance(parser);
            validate_skip_ws(parser);
        }
    }

    [[nodiscard]] auto validate_value(JsonValidator& parser) -> bool {
        validate_skip_ws(parser);
        switch (validate_peek(parser)) {
        case '{':
            return validate_object(parser);
        case '[':
            return validate_array(parser);
        case '"':
            return validate_string(parser);
        case 't':
            return validate_literal(parser, "true");
        case 'f':
            return validate_literal(parser, "false");
        case 'n':
            return validate_literal(parser, "null");
        default:
            if (validate_peek(parser) == '-' || is_digit(validate_peek(parser))) {
                return validate_number(parser);
            }
            return validate_fail(parser, "Expected JSON value");
        }
    }

} // namespace

namespace encoding {

    [[nodiscard]] auto json_parse(Arena& arena, StrRef text, JsonValue const*& out_value) -> bool {
        JsonParser parser = {.arena = &arena, .text = text};
        if (!parse_value(parser, out_value)) {
            return false;
        }
        skip_ws(parser);
        return parser.index == text.size();
    }

    [[nodiscard]] auto json_validate(
        StrRef text,
        JsonParseError* out_error,
        JsonContainerRangeFn container_range_fn,
        void* user_data
    ) -> bool {
        JsonValidator parser = {
            .text = text,
            .container_range_fn = container_range_fn,
            .user_data = user_data,
        };
        bool valid = validate_value(parser);
        if (valid) {
            validate_skip_ws(parser);
            if (!validate_at_end(parser)) {
                valid = validate_fail(parser, "Unexpected text after JSON value");
            }
        }
        if (!valid && out_error != nullptr) {
            *out_error = parser.error;
        }
        return valid;
    }

    [[nodiscard]] auto json_object_get(JsonValue const* value, StrRef key) -> JsonValue const* {
        if (value == nullptr || value->kind != JsonKind::OBJECT) {
            return nullptr;
        }
        for (JsonMember const& member : value->object) {
            if (member.key == key) {
                return member.value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto json_string(JsonValue const* value, StrRef& out) -> bool {
        if (value == nullptr || value->kind != JsonKind::STRING) {
            return false;
        }
        out = value->string;
        return true;
    }

    [[nodiscard]] auto json_int(JsonValue const* value, int32_t& out) -> bool {
        if (value == nullptr || value->kind != JsonKind::NUMBER ||
            value->number < static_cast<double>(INT32_MIN) ||
            value->number > static_cast<double>(INT32_MAX)) {
            return false;
        }
        out = static_cast<int32_t>(value->number);
        return true;
    }

    [[nodiscard]] auto json_size(JsonValue const* value, size_t& out) -> bool {
        if (value == nullptr || value->kind != JsonKind::NUMBER || value->number < 0.0) {
            return false;
        }
        out = static_cast<size_t>(value->number);
        return true;
    }

    [[nodiscard]] auto json_bool(JsonValue const* value, bool& out) -> bool {
        if (value == nullptr || value->kind != JsonKind::BOOL) {
            return false;
        }
        out = value->bool_value;
        return true;
    }

} // namespace encoding
