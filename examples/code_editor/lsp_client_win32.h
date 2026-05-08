#pragma once

#include "lsp.h"

#include <base/memory.h>
#include <base/str_ref.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace code_editor {

#if defined(_WIN32)

    inline constexpr size_t LSP_PIPE_READ_BUFFER_SIZE = 4096u;

    struct LspClientPendingRequest {
        int32_t id = 0;
        LspRequestKind kind = LspRequestKind::NONE;
        StrRef path = {};
        LspPosition position = {};
        uint64_t revision = 0u;
    };

    struct LspPipeRead {
        OVERLAPPED overlapped = {};
        HANDLE event = nullptr;
        char buffer[LSP_PIPE_READ_BUFFER_SIZE] = {};
        bool pending = false;
    };

    struct LspPipeWrite {
        OVERLAPPED overlapped = {};
        HANDLE event = nullptr;
        Vec<char> bytes = {};
        Vec<char> backlog = {};
        size_t offset = 0u;
        bool pending = false;
    };

    struct LspClient {
        Arena arena = {};
        Arena document_arena = {};
        Arena message_arena = {};
        Arena result_arena = {};
        LspBridge bridge = {};
        PROCESS_INFORMATION process = {};
        HANDLE stdin_write = nullptr;
        HANDLE stdout_read = nullptr;
        HANDLE stderr_read = nullptr;
        LspPipeWrite stdin_io = {};
        LspPipeRead stdout_io = {};
        LspPipeRead stderr_io = {};
        LspFramer framer = {};
        Vec<LspClientPendingRequest> pending = {};
        Vec<LspDiagnostic> diagnostics = {};
        Vec<LspCompletionItem> completions = {};
        Vec<LspLocation> locations = {};
        Vec<LspCodeAction> code_actions = {};
        Vec<LspDocumentSymbol> symbols = {};
        Vec<LspTextEdit> text_edits = {};
        Vec<LspSemanticToken> semantic_tokens = {};
        Vec<LspFoldingRange> folding_ranges = {};
        Vec<StrRef> semantic_token_types = {};
        StrRef root_path = {};
        StrRef compile_commands_dir = {};
        StrRef current_path = {};
        StrRef current_uri = {};
        StrRef current_text = {};
        uint64_t current_revision = 0u;
        int32_t next_id = 1;
        bool started = false;
        bool initialized = false;
        bool semantic_tokens_supported = false;
        bool folding_ranges_supported = false;
    };

    [[nodiscard]] auto lsp_client_init(LspClient& client) -> bool;
    auto lsp_client_shutdown(LspClient& client) -> void;
    [[nodiscard]] auto lsp_client_start(LspClient& client, StrRef root_path, StrRef source_path)
        -> bool;
    auto lsp_client_poll(LspClient& client) -> void;
    auto lsp_client_send_editor_request(void* user_data, LspEditorRequest const& request) -> void;
    [[nodiscard]] auto lsp_client_bridge(LspClient& client) -> LspBridge*;

#endif

} // namespace code_editor
