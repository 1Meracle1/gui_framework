#include "lsp.h"

#include <base/string_buffer.h>
#include <test/test.h>

namespace {

    TEST_CASE(lsp_json_parses_objects_and_strings) {
        Arena arena = {};
        arena.init();

        code_editor::LspJsonValue const* root = nullptr;
        TEST_EXPECT(
            context,
            code_editor::lsp_json_parse(
                arena, "{\"name\":\"a\\n b\",\"items\":[1,true,null]}", root
            )
        );

        StrRef name = {};
        TEST_EXPECT(
            context,
            code_editor::lsp_json_string(code_editor::lsp_json_object_get(root, "name"), name)
        );
        TEST_EXPECT(context, name == "a\n b");
        code_editor::LspJsonValue const* items = code_editor::lsp_json_object_get(root, "items");
        TEST_EXPECT(context, items != nullptr);
        TEST_EXPECT(context, items->array.size() == 3u);
    }

    TEST_CASE(lsp_framer_reads_split_messages) {
        Arena arena = {};
        arena.init();

        code_editor::LspFramer framer = {};
        TEST_EXPECT(context, code_editor::lsp_framer_init(framer, arena.resource()));
        TEST_EXPECT(context, code_editor::lsp_framer_append(framer, "Content-Length: 7\r\n"));

        StrRef message = {};
        TEST_EXPECT(context, !code_editor::lsp_framer_next_message(framer, arena, message));
        TEST_EXPECT(context, code_editor::lsp_framer_append(framer, "\r\n{\"a\":1}tail"));
        TEST_EXPECT(context, code_editor::lsp_framer_next_message(framer, arena, message));
        TEST_EXPECT(context, message == "{\"a\":1}");
        TEST_EXPECT(context, framer.bytes.size() == 4u);
    }

    TEST_CASE(lsp_file_uri_round_trips_windows_path) {
        Arena arena = {};
        arena.init();

        StrRef const path = "D:\\dev\\cpp\\gui framework\\main.cpp";
        StrRef const uri = code_editor::lsp_path_to_file_uri(arena, path);
        TEST_EXPECT(context, uri == "file:///D:/dev/cpp/gui%20framework/main.cpp");
        TEST_EXPECT(context, code_editor::lsp_file_uri_to_path(arena, uri) == path);
    }

    TEST_CASE(lsp_utf16_columns_convert_utf8_text) {
        StrRef const line = "a"
                            "\xe2\x82\xac"
                            "\xf0\x9f\x98\x80"
                            "z";
        TEST_EXPECT(context, code_editor::lsp_byte_column_to_utf16(line, 1u) == 1u);
        TEST_EXPECT(context, code_editor::lsp_byte_column_to_utf16(line, 4u) == 2u);
        TEST_EXPECT(context, code_editor::lsp_byte_column_to_utf16(line, 8u) == 4u);
        TEST_EXPECT(context, code_editor::lsp_utf16_column_to_byte(line, 4u) == 8u);
    }

    TEST_CASE(lsp_text_edits_apply_from_end_to_start) {
        Arena arena = {};
        arena.init();

        code_editor::LspTextEdit edits[] = {
            {
                .path = "main.cpp",
                .range = {{0u, 1u}, {0u, 3u}},
                .new_text = "XX",
            },
            {
                .path = "main.cpp",
                .range = {{1u, 0u}, {1u, 1u}},
                .new_text = "Y",
            },
        };

        StrRef const result = code_editor::lsp_apply_text_edits(
            arena, "abcd\nef", Slice<code_editor::LspTextEdit const>(edits), "main.cpp"
        );
        TEST_EXPECT(context, result == "aXXd\nYf");
    }

} // namespace

TEST_MAIN()
