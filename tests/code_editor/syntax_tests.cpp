#include "syntax.h"

#include <test/test.h>

namespace {

    [[nodiscard]] auto expect_token(
        test::Context* context,
        code_editor::SyntaxTokenizer tokenizer,
        StrRef line,
        size_t index,
        code_editor::SyntaxTokenKind kind,
        StrRef text
    ) -> size_t {
        code_editor::SyntaxToken const token =
            code_editor::syntax_next_token(tokenizer, line, index);
        TEST_EXPECT(context, token.kind == kind);
        TEST_EXPECT(context, line.substr(token.start, token.end - token.start) == text);
        return token.end;
    }

    TEST_CASE(cpp_tokenizer_classifies_common_tokens) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::cpp_syntax_tokenizer();
        StrRef const line = "auto count = 12u; // ok";
        size_t index = 0u;
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "auto"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, "count"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, "="
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::NUMBER, "12u"
        );
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, ";"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::COMMENT, "// ok"
        );
        TEST_EXPECT(context, index == line.size());
    }

    TEST_CASE(cpp_tokenizer_classifies_types_and_strings) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::cpp_syntax_tokenizer();
        StrRef const line = "int32_t value = \"x\"; char ch = '\\n';";
        size_t index = 0u;
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::TYPE, "int32_t"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, "value"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, "="
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::STRING, "\"x\""
        );
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, ";"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::TYPE, "char"
        );
        while (index < line.size() && line[index] != '\'') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::STRING, "'\\n'"
        );
        TEST_EXPECT(context, index < line.size());
    }

    TEST_CASE(cpp_tokenizer_classifies_preprocessor) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::cpp_syntax_tokenizer();
        StrRef const line = "  #include <x>";
        size_t index = 0u;
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, "  ");
        index = expect_token(
            context,
            tokenizer,
            line,
            index,
            code_editor::SyntaxTokenKind::PREPROCESSOR,
            "#include <x>"
        );
        TEST_EXPECT(context, index == line.size());
    }

    TEST_CASE(tokenizer_selection_uses_file_extension) {
        StrRef const cpp_line = "auto value = 1;";
        code_editor::SyntaxTokenizer const cpp_tokenizer =
            code_editor::syntax_tokenizer_for_file_name("view.hpp");
        code_editor::SyntaxToken const cpp_token =
            code_editor::syntax_next_token(cpp_tokenizer, cpp_line, 0u);
        TEST_EXPECT(context, cpp_token.kind == code_editor::SyntaxTokenKind::KEYWORD);

        code_editor::SyntaxTokenizer const text_tokenizer =
            code_editor::syntax_tokenizer_for_file_name("notes.txt");
        code_editor::SyntaxToken const text_token =
            code_editor::syntax_next_token(text_tokenizer, cpp_line, 0u);
        TEST_EXPECT(context, text_token.kind == code_editor::SyntaxTokenKind::TEXT);
        TEST_EXPECT(context, text_token.end == cpp_line.size());
    }

    TEST_CASE(json_tokenizer_classifies_common_tokens) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::json_syntax_tokenizer();
        StrRef const line = "\"enabled\": true, \"count\": -12.5e+2";
        size_t index = 0u;
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::STRING, "\"enabled\""
        );
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, ":"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "true"
        );
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::PUNCTUATION, ","
        );
        while (index < line.size() && line[index] != '-') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::NUMBER, "-12.5e+2"
        );
        TEST_EXPECT(context, index == line.size());
    }

    TEST_CASE(tokenizer_selection_uses_json_extension) {
        code_editor::SyntaxTokenizer const tokenizer =
            code_editor::syntax_tokenizer_for_file_name("settings.json");
        code_editor::SyntaxToken const token =
            code_editor::syntax_next_token(tokenizer, "null", 0u);
        TEST_EXPECT(context, token.kind == code_editor::SyntaxTokenKind::KEYWORD);
    }

} // namespace

TEST_MAIN()
