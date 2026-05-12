#include "syntax.h"

#include <base/unicode.h>

namespace code_editor {

    [[nodiscard]] auto json_next_token(StrRef line, size_t index) -> SyntaxToken {
        char const ch = line[index];
        uint8_t const byte = static_cast<uint8_t>(ch);
        size_t end = index + 1u;
        SyntaxTokenKind kind = SyntaxTokenKind::TEXT;
        if (byte >= 0x80u) {
            size_t const size = base::utf8_codepoint_size(line, index);
            end = index + (size != 0u ? size : 1u);
        } else if (ch == ' ' || ch == '\t') {
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
                ++end;
            }
        } else if (ch == '"') {
            while (end < line.size()) {
                if (line[end] == '\\' && end + 1u < line.size()) {
                    end += 2u;
                } else if (line[end] == '"') {
                    end += 1u;
                    break;
                } else {
                    end += 1u;
                }
            }
            kind = SyntaxTokenKind::STRING;
        } else if (is_ascii_digit(ch) || ch == '-') {
            while (end < line.size() && (is_ascii_alphanumeric(line[end]) || line[end] == '.' ||
                                         line[end] == '+' || line[end] == '-')) {
                ++end;
            }
            kind = SyntaxTokenKind::NUMBER;
        } else if (is_ascii_alpha(ch)) {
            while (end < line.size() && is_ascii_alpha(line[end])) {
                ++end;
            }
            StrRef const token(line.data() + index, end - index);
            if (token == "true" || token == "false" || token == "null") {
                kind = SyntaxTokenKind::KEYWORD;
            }
        } else {
            kind = SyntaxTokenKind::PUNCTUATION;
        }
        return {.kind = kind, .start = index, .end = end};
    }

    [[nodiscard]] auto json_syntax_tokenizer() -> SyntaxTokenizer {
        return {.next_token = json_next_token, .match_pairs = true};
    }

} // namespace code_editor
