#pragma once

#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>

namespace code_editor {

    enum class SyntaxTokenKind : uint8_t {
        TEXT,
        KEYWORD,
        TYPE,
        STRING,
        NUMBER,
        COMMENT,
        PREPROCESSOR,
        PUNCTUATION,
    };

    struct SyntaxToken {
        SyntaxTokenKind kind = SyntaxTokenKind::TEXT;
        size_t start = 0u;
        size_t end = 0u;
    };

    using SyntaxNextTokenFn = auto (*)(StrRef line, size_t index) -> SyntaxToken;

    struct SyntaxTokenizer {
        SyntaxNextTokenFn next_token = nullptr;
    };

    [[nodiscard]] auto syntax_next_token(SyntaxTokenizer tokenizer, StrRef line, size_t index)
        -> SyntaxToken;
    [[nodiscard]] auto syntax_tokenizer_for_file_name(StrRef file_name) -> SyntaxTokenizer;
    [[nodiscard]] auto cpp_syntax_tokenizer() -> SyntaxTokenizer;

} // namespace code_editor
