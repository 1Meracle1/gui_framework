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

    auto expect_line_token(
        test::Context* context,
        code_editor::SyntaxTokenizer tokenizer,
        StrRef line,
        code_editor::SyntaxTokenKind kind,
        StrRef text
    ) -> void {
        bool found = false;
        size_t index = 0u;
        while (index < line.size()) {
            code_editor::SyntaxToken const token =
                code_editor::syntax_next_token(tokenizer, line, index);
            if (line.substr(token.start, token.end - token.start) == text) {
                TEST_EXPECT(context, token.kind == kind);
                found = true;
                break;
            }
            index = token.end;
        }
        TEST_EXPECT(context, found);
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

    TEST_CASE(abap_tokenizer_classifies_keywords_types_strings_and_comments) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::abap_syntax_tokenizer();
        StrRef const line = "class-data value TYPE string VALUE 'can''t'. \" note";
        size_t index = 0u;
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "class-data"
        );
        while (index < line.size() && line[index] != 'T') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "TYPE"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::TYPE, "string"
        );
        index =
            expect_token(context, tokenizer, line, index, code_editor::SyntaxTokenKind::TEXT, " ");
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "VALUE"
        );
        while (index < line.size() && line[index] != '\'') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::STRING, "'can''t'"
        );
        while (index < line.size() && line[index] != '"') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::COMMENT, "\" note"
        );
        TEST_EXPECT(context, index == line.size());
    }

    TEST_CASE(abap_tokenizer_keeps_hyphenated_statement_keywords) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::abap_syntax_tokenizer();
        StrRef const line = "END-OF-SELECTION. TEST-INJECTION. END-TEST-SEAM.";
        size_t index = 0u;
        index = expect_token(
            context,
            tokenizer,
            line,
            index,
            code_editor::SyntaxTokenKind::KEYWORD,
            "END-OF-SELECTION"
        );
        while (index < line.size() && line[index] != 'T') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "TEST-INJECTION"
        );
        while (index < line.size() && line[index] != 'E') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "END-TEST-SEAM"
        );
        TEST_EXPECT(context, index < line.size());
    }

    TEST_CASE(abap_tokenizer_keeps_slash_classes_and_field_symbols) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::abap_syntax_tokenizer();
        StrRef const class_line = "/FOO/CL_BAR=>factory( ).";
        size_t index = expect_token(
            context, tokenizer, class_line, 0u, code_editor::SyntaxTokenKind::TEXT, "/FOO/CL_BAR"
        );
        TEST_EXPECT(context, index < class_line.size());

        StrRef const selector_line = "/FOO/CL_BAR-method( ).";
        index = expect_token(
            context, tokenizer, selector_line, 0u, code_editor::SyntaxTokenKind::TEXT, "/FOO/CL_BAR"
        );
        index = expect_token(
            context, tokenizer, selector_line, index, code_editor::SyntaxTokenKind::PUNCTUATION, "-"
        );
        TEST_EXPECT(context, index < selector_line.size());

        StrRef const field_symbol_line = "<fs_item> = <fs_item>.";
        index = expect_token(
            context,
            tokenizer,
            field_symbol_line,
            0u,
            code_editor::SyntaxTokenKind::TEXT,
            "<fs_item>"
        );
        while (index < field_symbol_line.size() && field_symbol_line[index] != '<') {
            index = code_editor::syntax_next_token(tokenizer, field_symbol_line, index).end;
        }
        index = expect_token(
            context,
            tokenizer,
            field_symbol_line,
            index,
            code_editor::SyntaxTokenKind::TEXT,
            "<fs_item>"
        );
        TEST_EXPECT(context, index < field_symbol_line.size());

        StrRef const field_selector_line = "<fs_item>-component.";
        index = expect_token(
            context,
            tokenizer,
            field_selector_line,
            0u,
            code_editor::SyntaxTokenKind::TEXT,
            "<fs_item>"
        );
        index = expect_token(
            context,
            tokenizer,
            field_selector_line,
            index,
            code_editor::SyntaxTokenKind::PUNCTUATION,
            "-"
        );
        TEST_EXPECT(context, index < field_selector_line.size());
    }

    TEST_CASE(abap_tokenizer_classifies_line_comment_and_template) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::abap_syntax_tokenizer();
        StrRef const comment = "* comment";
        code_editor::SyntaxToken const comment_token =
            code_editor::syntax_next_token(tokenizer, comment, 0u);
        TEST_EXPECT(context, comment_token.kind == code_editor::SyntaxTokenKind::COMMENT);
        TEST_EXPECT(context, comment_token.end == comment.size());

        StrRef const line = "WRITE |sum \\| { value }|.";
        size_t index = 0u;
        index = expect_token(
            context, tokenizer, line, index, code_editor::SyntaxTokenKind::KEYWORD, "WRITE"
        );
        while (index < line.size() && line[index] != '|') {
            index = code_editor::syntax_next_token(tokenizer, line, index).end;
        }
        index = expect_token(
            context,
            tokenizer,
            line,
            index,
            code_editor::SyntaxTokenKind::STRING,
            "|sum \\| { value }|"
        );
        TEST_EXPECT(context, index < line.size());
    }

    TEST_CASE(tokenizer_selection_uses_abap_extension) {
        code_editor::SyntaxTokenizer const tokenizer =
            code_editor::syntax_tokenizer_for_file_name("report.abap");
        code_editor::SyntaxToken const token =
            code_editor::syntax_next_token(tokenizer, "select-options", 0u);
        TEST_EXPECT(context, token.kind == code_editor::SyntaxTokenKind::KEYWORD);
    }

    TEST_CASE(abap_tokenizer_classifies_indexed_keyword_additions) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::abap_syntax_tokenizer();
        StrRef const select_line =
            "SELECT DISTINCT * FROM db WHERE value BETWEEN 1 AND 2 ORDER BY key DESCENDING.";
        expect_line_token(
            context, tokenizer, select_line, code_editor::SyntaxTokenKind::KEYWORD, "DISTINCT"
        );
        expect_line_token(
            context, tokenizer, select_line, code_editor::SyntaxTokenKind::KEYWORD, "BETWEEN"
        );
        expect_line_token(
            context, tokenizer, select_line, code_editor::SyntaxTokenKind::KEYWORD, "ORDER"
        );
        expect_line_token(
            context, tokenizer, select_line, code_editor::SyntaxTokenKind::KEYWORD, "DESCENDING"
        );

        StrRef const selection_line = "SELECTION-SCREEN PUSHBUTTON /1(20) text USER-COMMAND run.";
        expect_line_token(
            context, tokenizer, selection_line, code_editor::SyntaxTokenKind::KEYWORD, "PUSHBUTTON"
        );

        StrRef const assign_line =
            "ASSIGN COMPONENT name OF STRUCTURE row TO FIELD-SYMBOL(<fs>) CASTING.";
        expect_line_token(
            context, tokenizer, assign_line, code_editor::SyntaxTokenKind::KEYWORD, "COMPONENT"
        );
        expect_line_token(
            context, tokenizer, assign_line, code_editor::SyntaxTokenKind::KEYWORD, "FIELD-SYMBOL"
        );
        expect_line_token(
            context, tokenizer, assign_line, code_editor::SyntaxTokenKind::KEYWORD, "CASTING"
        );

        StrRef const parameter_line = "PARAMETERS p AS CHECKBOX DEFAULT abap_true NO-DISPLAY.";
        expect_line_token(
            context, tokenizer, parameter_line, code_editor::SyntaxTokenKind::KEYWORD, "CHECKBOX"
        );
        expect_line_token(
            context, tokenizer, parameter_line, code_editor::SyntaxTokenKind::KEYWORD, "NO-DISPLAY"
        );
    }

    TEST_CASE(cpp_pair_detection_ignores_escaped_strings_and_comments) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::cpp_syntax_tokenizer();
        StrRef const line = "auto text = \"\\)\"; value[0]; // {";
        code_editor::SyntaxPair pair = {};

        TEST_EXPECT(context, !code_editor::syntax_pair_at(tokenizer, line, line.find(')'), pair));

        size_t const open_bracket = line.find('[');
        TEST_EXPECT(context, code_editor::syntax_pair_at(tokenizer, line, open_bracket, pair));
        TEST_EXPECT(context, pair.open == '[');
        TEST_EXPECT(context, pair.close == ']');
        TEST_EXPECT(context, pair.direction == 1);

        size_t const close_bracket = line.find(']');
        TEST_EXPECT(context, code_editor::syntax_pair_at(tokenizer, line, close_bracket, pair));
        TEST_EXPECT(context, pair.open == '[');
        TEST_EXPECT(context, pair.close == ']');
        TEST_EXPECT(context, pair.direction == -1);

        TEST_EXPECT(context, !code_editor::syntax_pair_at(tokenizer, line, line.find('{'), pair));
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

    TEST_CASE(json_pair_detection_ignores_escaped_strings) {
        code_editor::SyntaxTokenizer const tokenizer = code_editor::json_syntax_tokenizer();
        StrRef const line = "{\"text\":\"\\]\",\"items\":[1]}";
        code_editor::SyntaxPair pair = {};

        size_t const string_bracket = line.find(']');
        TEST_EXPECT(context, !code_editor::syntax_pair_at(tokenizer, line, string_bracket, pair));

        size_t const open_bracket = line.find('[', string_bracket + 1u);
        TEST_EXPECT(context, code_editor::syntax_pair_at(tokenizer, line, open_bracket, pair));
        TEST_EXPECT(context, pair.open == '[');
        TEST_EXPECT(context, pair.close == ']');
        TEST_EXPECT(context, pair.direction == 1);

        code_editor::SyntaxTokenizer const text_tokenizer =
            code_editor::syntax_tokenizer_for_file_name("notes.txt");
        TEST_EXPECT(context, !code_editor::syntax_pair_at(text_tokenizer, "value[0]", 5u, pair));
    }

} // namespace

TEST_MAIN()
