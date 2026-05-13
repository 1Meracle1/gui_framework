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

    [[nodiscard]] auto syntax_pair_for_char(char ch, SyntaxPair& out_pair) -> bool {
        switch (ch) {
        case '(':
            out_pair = {.open = '(', .close = ')', .direction = 1};
            return true;
        case ')':
            out_pair = {.open = '(', .close = ')', .direction = -1};
            return true;
        case '{':
            out_pair = {.open = '{', .close = '}', .direction = 1};
            return true;
        case '}':
            out_pair = {.open = '{', .close = '}', .direction = -1};
            return true;
        case '[':
            out_pair = {.open = '[', .close = ']', .direction = 1};
            return true;
        case ']':
            out_pair = {.open = '[', .close = ']', .direction = -1};
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] auto syntax_pair_for_token(
        SyntaxTokenizer tokenizer, SyntaxTokenKind kind, char ch, SyntaxPair& out_pair
    ) -> bool {
        return tokenizer.match_pairs && kind == SyntaxTokenKind::PUNCTUATION &&
               syntax_pair_for_char(ch, out_pair);
    }

    [[nodiscard]] auto
    syntax_pair_at(SyntaxTokenizer tokenizer, StrRef line, size_t index, SyntaxPair& out_pair)
        -> bool {
        if (!tokenizer.match_pairs || index >= line.size()) {
            return false;
        }

        size_t token_index = 0u;
        while (token_index < line.size()) {
            SyntaxToken const token = syntax_next_token(tokenizer, line, token_index);
            if (index < token.end) {
                return syntax_pair_for_token(tokenizer, token.kind, line[index], out_pair);
            }
            token_index = token.end;
        }
        return false;
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

    [[nodiscard]] auto json_file_name(StrRef file_name) -> bool {
        return file_name.ends_with_ignore_ascii_case(".json");
    }

    [[nodiscard]] auto abap_file_name(StrRef file_name) -> bool {
        return file_name.ends_with_ignore_ascii_case(".abap");
    }

    [[nodiscard]] auto syntax_tokenizer_for_file_name(StrRef file_name) -> SyntaxTokenizer {
        if (abap_file_name(file_name)) {
            return abap_syntax_tokenizer();
        }
        if (cpp_file_name(file_name)) {
            return cpp_syntax_tokenizer();
        }
        if (json_file_name(file_name)) {
            return json_syntax_tokenizer();
        }
        return {.next_token = plain_text_next_token};
    }

} // namespace code_editor
