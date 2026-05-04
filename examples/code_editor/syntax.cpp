#include "syntax.h"

#include <algorithm>
#include <base/assert.h>

namespace code_editor {

    [[nodiscard]] auto plain_text_next_token(StrRef line, size_t index) -> SyntaxToken {
        return {.kind = SyntaxTokenKind::TEXT, .start = index, .end = line.size()};
    }

    [[nodiscard]] auto syntax_next_token(SyntaxTokenizer tokenizer, StrRef line, size_t index)
        -> SyntaxToken {
        DEBUG_ASSERT(index < line.size());
        SyntaxNextTokenFn const next_token =
            tokenizer.next_token != nullptr ? tokenizer.next_token : plain_text_next_token;
        SyntaxToken token = next_token(line, index);
        DEBUG_ASSERT(token.start == index && token.end > token.start && token.end <= line.size());
        token.start = index;
        token.end = std::clamp(token.end, index + 1u, line.size());
        return token;
    }

    [[nodiscard]] auto cpp_file_name(StrRef file_name) -> bool {
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

    [[nodiscard]] auto syntax_tokenizer_for_file_name(StrRef file_name) -> SyntaxTokenizer {
        return cpp_file_name(file_name) ? cpp_syntax_tokenizer()
                                        : SyntaxTokenizer{.next_token = plain_text_next_token};
    }

} // namespace code_editor
