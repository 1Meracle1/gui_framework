#pragma once

#include "syntax.h"
#include "text_buffer.h"

#include <base/memory.h>
#include <base/slice.h>
#include <base/str_ref.h>
#include <base/vec.h>
#include <cstddef>
#include <cstdint>

class StringBuffer;

namespace code_editor {

    enum class LspStatusKind : uint8_t {
        OFF,
        STARTING,
        READY,
        WARNING,
        FAILED,
    };

    enum class LspRequestKind : uint8_t {
        NONE,
        DID_OPEN,
        DID_CHANGE,
        DID_CLOSE,
        COMPLETION,
        HOVER,
        DEFINITION,
        DECLARATION,
        REFERENCES,
        RENAME,
        FORMATTING,
        CODE_ACTION,
        DOCUMENT_SYMBOL,
        SEMANTIC_TOKENS,
    };

    enum class LspDiagnosticSeverity : uint8_t {
        ERROR_DIAGNOSTIC = 1u,
        WARNING = 2u,
        INFORMATION = 3u,
        HINT = 4u,
    };

    struct LspPosition {
        size_t line = 0u;
        size_t column = 0u;
    };

    struct LspRange {
        LspPosition start = {};
        LspPosition end = {};
    };

    struct LspDiagnostic {
        StrRef path = {};
        LspRange range = {};
        LspDiagnosticSeverity severity = LspDiagnosticSeverity::INFORMATION;
        StrRef code = {};
        StrRef source = {};
        StrRef message = {};
    };

    struct LspCompletionItem {
        StrRef label = {};
        StrRef detail = {};
        StrRef insert_text = {};
        LspRange edit_range = {};
        bool has_edit = false;
    };

    struct LspLocation {
        StrRef path = {};
        LspRange range = {};
    };

    struct LspHover {
        StrRef path = {};
        LspRange range = {};
        StrRef text = {};
    };

    struct LspTextEdit {
        StrRef path = {};
        LspRange range = {};
        StrRef new_text = {};
    };

    struct LspCodeAction {
        StrRef title = {};
        StrRef kind = {};
        Slice<LspTextEdit> edits = {};
    };

    struct LspDocumentSymbol {
        StrRef name = {};
        StrRef detail = {};
        LspRange range = {};
        LspRange selection_range = {};
        uint32_t kind = 0u;
    };

    struct LspSemanticToken {
        LspRange range = {};
        SyntaxTokenKind kind = SyntaxTokenKind::TEXT;
    };

    struct LspBridge {
        LspStatusKind status = LspStatusKind::OFF;
        StrRef server_name = {};
        StrRef status_text = {};
        Slice<LspDiagnostic> diagnostics = {};
        Slice<LspCompletionItem> completions = {};
        Slice<LspLocation> locations = {};
        Slice<LspCodeAction> code_actions = {};
        Slice<LspDocumentSymbol> symbols = {};
        Slice<LspTextEdit> text_edits = {};
        Slice<LspSemanticToken> semantic_tokens = {};
        LspHover hover = {};
        StrRef progress_text = {};
        StrRef semantic_tokens_path = {};
        LspRequestKind locations_kind = LspRequestKind::NONE;
        uint64_t semantic_tokens_revision = 0u;
        bool progress_active = false;
        uint64_t status_generation = 0u;
        uint64_t progress_generation = 0u;
        uint64_t diagnostics_generation = 0u;
        uint64_t completions_generation = 0u;
        uint64_t hover_generation = 0u;
        uint64_t locations_generation = 0u;
        uint64_t code_actions_generation = 0u;
        uint64_t symbols_generation = 0u;
        uint64_t text_edits_generation = 0u;
        uint64_t semantic_tokens_generation = 0u;
    };

    struct LspEditorRequest {
        LspRequestKind kind = LspRequestKind::NONE;
        StrRef path = {};
        StrRef text = {};
        StrRef new_name = {};
        LspPosition position = {};
        LspRange range = {};
        uint64_t revision = 0u;
    };

    using LspSendEditorRequestFn = auto (*)(void* user_data, LspEditorRequest const& request)
        -> void;

    enum class LspJsonKind : uint8_t {
        NULL_VALUE,
        BOOL,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT,
    };

    struct LspJsonValue;

    struct LspJsonMember {
        StrRef key = {};
        LspJsonValue const* value = nullptr;
    };

    struct LspJsonValue {
        LspJsonKind kind = LspJsonKind::NULL_VALUE;
        bool bool_value = false;
        double number = 0.0;
        StrRef string = {};
        Slice<LspJsonValue const*> array = {};
        Slice<LspJsonMember> object = {};
    };

    struct LspFramer {
        Vec<char> bytes = {};
    };

    [[nodiscard]] auto lsp_cpp_file_name(StrRef file_name) -> bool;
    [[nodiscard]] auto lsp_position_less(LspPosition lhs, LspPosition rhs) -> bool;
    [[nodiscard]] auto lsp_range_valid(LspRange range) -> bool;

    [[nodiscard]] auto lsp_offset_from_position(StrRef text, LspPosition position) -> size_t;
    [[nodiscard]] auto lsp_byte_column_to_utf16(StrRef line, size_t byte_column) -> size_t;
    [[nodiscard]] auto lsp_utf16_column_to_byte(StrRef line, size_t utf16_column) -> size_t;
    [[nodiscard]] auto lsp_position_byte_to_utf16(StrRef text, LspPosition position) -> LspPosition;
    [[nodiscard]] auto lsp_position_utf16_to_byte(StrRef text, LspPosition position) -> LspPosition;
    [[nodiscard]] auto lsp_range_utf16_to_byte(StrRef text, LspRange range) -> LspRange;

    [[nodiscard]] auto
    lsp_apply_text_edits(Arena& arena, StrRef text, Slice<LspTextEdit const> edits, StrRef path)
        -> StrRef;

    [[nodiscard]] auto lsp_path_to_file_uri(Arena& arena, StrRef path) -> StrRef;
    [[nodiscard]] auto lsp_file_uri_to_path(Arena& arena, StrRef uri) -> StrRef;

    auto lsp_json_write_escaped_string(StringBuffer& buffer, StrRef text) -> void;
    auto lsp_write_json_rpc_message(StringBuffer& buffer, StrRef json) -> bool;

    [[nodiscard]] auto lsp_json_parse(Arena& arena, StrRef text, LspJsonValue const*& out_value)
        -> bool;
    [[nodiscard]] auto lsp_json_object_get(LspJsonValue const* value, StrRef key)
        -> LspJsonValue const*;
    [[nodiscard]] auto lsp_json_string(LspJsonValue const* value, StrRef& out) -> bool;
    [[nodiscard]] auto lsp_json_int(LspJsonValue const* value, int32_t& out) -> bool;
    [[nodiscard]] auto lsp_json_size(LspJsonValue const* value, size_t& out) -> bool;
    [[nodiscard]] auto lsp_json_bool(LspJsonValue const* value, bool& out) -> bool;

    [[nodiscard]] auto lsp_framer_init(LspFramer& framer, MemoryResource* resource) -> bool;
    auto lsp_framer_reset(LspFramer& framer) -> void;
    [[nodiscard]] auto lsp_framer_append(LspFramer& framer, StrRef bytes) -> bool;
    [[nodiscard]] auto lsp_framer_next_message(LspFramer& framer, Arena& arena, StrRef& out_message)
        -> bool;

} // namespace code_editor
