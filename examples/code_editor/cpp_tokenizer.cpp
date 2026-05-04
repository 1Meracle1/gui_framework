#include "shared.h"
#include "syntax.h"

#include <algorithm>
#include <base/unicode.h>

namespace code_editor {

    [[nodiscard]] auto is_identifier_start(char ch) -> bool {
        return is_ascii_alpha(ch) || ch == '_';
    }

    [[nodiscard]] auto is_identifier_char(char ch) -> bool {
        return is_ascii_alphanumeric(ch) || ch == '_';
    }

    [[nodiscard]] auto is_cpp_keyword(StrRef token) -> bool {
        constexpr StrRef KEYWORDS[] = {
            "alignas",
            "auto",
            "bool",
            "break",
            "case",
            "class",
            "const",
            "constexpr",
            "continue",
            "default",
            "delete",
            "do",
            "else",
            "enum",
            "false",
            "for",
            "if",
            "inline",
            "namespace",
            "new",
            "noexcept",
            "nullptr",
            "private",
            "public",
            "return",
            "sizeof",
            "static",
            "struct",
            "switch",
            "template",
            "true",
            "using",
            "void",
            "while",
            "static_cast",
            "bit_cast",
            "reinterpret_cast",
            "typename",
            "alignof",
            "static_assert",
            "final",
        };
        for (StrRef keyword : KEYWORDS) {
            if (token == keyword) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_cpp_type_keyword(StrRef token) -> bool {
        constexpr StrRef TYPES[] = {
            "char",
            "double",
            "float",
            "int",
            "int32_t",
            "size_t",
            "uint8_t",
            "uint32_t",
            "uint64_t",
            "uintptr_t",
            "wchar_t",
        };
        for (StrRef type : TYPES) {
            if (token == type) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto cpp_preprocessor_start(StrRef line) -> size_t {
        size_t index = 0u;
        while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) {
            ++index;
        }
        return index < line.size() && line[index] == '#' ? index : StrRef::NPOS;
    }

    [[nodiscard]] auto cpp_next_token(StrRef line, size_t index) -> SyntaxToken {
        size_t const preprocessor = cpp_preprocessor_start(line);
        if (preprocessor != StrRef::NPOS && index >= preprocessor) {
            return {.kind = SyntaxTokenKind::PREPROCESSOR, .start = index, .end = line.size()};
        }

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
        } else if (ch == '/' && index + 1u < line.size() && line[index + 1u] == '/') {
            end = line.size();
            kind = SyntaxTokenKind::COMMENT;
        } else if (ch == '/' && index + 1u < line.size() && line[index + 1u] == '*') {
            end = index + 2u;
            while (end + 1u < line.size() && !(line[end] == '*' && line[end + 1u] == '/')) {
                ++end;
            }
            end = std::min(line.size(), end + 2u);
            kind = SyntaxTokenKind::COMMENT;
        } else if (ch == '"' || ch == '\'') {
            char const quote = ch;
            while (end < line.size()) {
                if (line[end] == '\\' && end + 1u < line.size()) {
                    end += 2u;
                } else if (line[end] == quote) {
                    end += 1u;
                    break;
                } else {
                    end += 1u;
                }
            }
            kind = SyntaxTokenKind::STRING;
        } else if (is_ascii_digit(ch)) {
            while (end < line.size() &&
                   (is_ascii_alphanumeric(line[end]) || line[end] == '.' || line[end] == '\'')) {
                ++end;
            }
            kind = SyntaxTokenKind::NUMBER;
        } else if (is_identifier_start(ch)) {
            while (end < line.size() && is_identifier_char(line[end])) {
                ++end;
            }
            StrRef const token(line.data() + index, end - index);
            if (is_cpp_keyword(token)) {
                kind = SyntaxTokenKind::KEYWORD;
            } else if (is_cpp_type_keyword(token)) {
                kind = SyntaxTokenKind::TYPE;
            }
        } else {
            kind = SyntaxTokenKind::PUNCTUATION;
        }
        return {.kind = kind, .start = index, .end = end};
    }

    [[nodiscard]] auto cpp_syntax_tokenizer() -> SyntaxTokenizer {
        return {.next_token = cpp_next_token};
    }

} // namespace code_editor
