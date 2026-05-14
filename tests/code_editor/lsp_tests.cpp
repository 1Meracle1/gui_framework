#include "lsp.h"

#include <base/string_buffer.h>
#include <test/test.h>

namespace {

    struct JsonRangeCapture {
        size_t count = 0u;
        size_t start_line[4] = {};
        size_t end_line[4] = {};
    };

    auto capture_json_range(void* user_data, size_t start_line, size_t end_line) -> void {
        auto& capture = *static_cast<JsonRangeCapture*>(user_data);
        if (capture.count >= 4u) {
            return;
        }
        capture.start_line[capture.count] = start_line;
        capture.end_line[capture.count] = end_line;
        capture.count += 1u;
    }

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

    TEST_CASE(encoding_json_validate_reports_errors_and_ranges) {
        JsonRangeCapture capture = {};
        encoding::JsonParseError error = {};

        TEST_EXPECT(
            context,
            encoding::json_validate(
                "{\n  \"items\": [\n    1\n  ]\n}", &error, capture_json_range, &capture
            )
        );
        TEST_EXPECT(context, capture.count == 2u);
        TEST_EXPECT(context, capture.start_line[0u] == 1u);
        TEST_EXPECT(context, capture.end_line[0u] == 3u);
        TEST_EXPECT(context, capture.start_line[1u] == 0u);
        TEST_EXPECT(context, capture.end_line[1u] == 4u);

        TEST_EXPECT(context, !encoding::json_validate("{\"ok\": true,}", &error, nullptr, nullptr));
        TEST_EXPECT(context, error.line == 0u);
        TEST_EXPECT(context, error.column == 12u);
        TEST_EXPECT(context, error.message == "Expected string");
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

    TEST_CASE(lsp_non_file_uri_stays_encoded) {
        Arena arena = {};
        arena.init();

        StrRef const uri =
            "abapls-cache:///sttp_e_evtid.abap?workspace=file%3A%2F%2F%2Fd%3A%2Fdev%2Fabap%"
            "2FZATTP_OBD_EPCIS_REPROC&artifact=545&name=%2Fsttp%2Fe_evtid&kind=ddic-data-"
            "element";
        TEST_EXPECT(context, code_editor::lsp_file_uri_to_path(arena, uri) == uri);
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

    TEST_CASE(lsp_server_selection_uses_enabled_extensions) {
        Arena arena = {};
        arena.init();

        code_editor::LspServerConfig servers[2] = {};
        StrRef* const cpp_extensions = arena_alloc<StrRef>(arena, 1u);
        cpp_extensions[0u] = arena_copy_str(arena, ".cpp");
        servers[0u].id = arena_copy_str(arena, "clangd");
        servers[0u].extensions = Slice<StrRef>(cpp_extensions, 1u);
        servers[0u].enabled = true;

        StrRef* const rust_extensions = arena_alloc<StrRef>(arena, 1u);
        rust_extensions[0u] = arena_copy_str(arena, ".rs");
        servers[1u].id = arena_copy_str(arena, "disabled");
        servers[1u].extensions = Slice<StrRef>(rust_extensions, 1u);
        servers[1u].enabled = false;

        Slice<code_editor::LspServerConfig const> const slice(servers, 2u);
        TEST_EXPECT(context, code_editor::lsp_server_for_file(slice, "main.cpp") == &servers[0u]);
        TEST_EXPECT(context, code_editor::lsp_server_for_file(slice, "lib.rs") == nullptr);
        TEST_EXPECT(context, code_editor::lsp_server_for_file(slice, "README.md") == nullptr);
    }

    TEST_CASE(lsp_snippet_expansion_keeps_first_placeholder_selection) {
        Arena arena = {};
        arena.init();

        code_editor::LspSnippetExpansion const expansion = code_editor::lsp_expand_snippet(
            arena, "for (${1:int i = 0}; ${2:i < count}; ${3:++i}) {\n    $0\n}"
        );

        TEST_EXPECT(context, expansion.text == "for (int i = 0; i < count; ++i) {\n    \n}");
        TEST_EXPECT(context, expansion.has_selection);
        TEST_EXPECT(context, expansion.selection.start.line == 0u);
        TEST_EXPECT(context, expansion.selection.start.column == 5u);
        TEST_EXPECT(context, expansion.selection.end.line == 0u);
        TEST_EXPECT(context, expansion.selection.end.column == 14u);
    }

    TEST_CASE(lsp_snippet_expansion_uses_choice_and_final_tabstop) {
        Arena arena = {};
        arena.init();

        code_editor::LspSnippetExpansion const expansion =
            code_editor::lsp_expand_snippet(arena, "${1|public,private|}:\n$0");

        TEST_EXPECT(context, expansion.text == "public:\n");
        TEST_EXPECT(context, expansion.has_selection);
        TEST_EXPECT(context, expansion.selection.start.line == 0u);
        TEST_EXPECT(context, expansion.selection.start.column == 0u);
        TEST_EXPECT(context, expansion.selection.end.line == 0u);
        TEST_EXPECT(context, expansion.selection.end.column == 6u);
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
