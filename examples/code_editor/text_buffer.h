#pragma once

#include <base/memory.h>
#include <base/str_ref.h>
#include <base/vec.h>
#include <cstddef>
#include <cstdint>

namespace code_editor {

    enum class EditorPieceSource : uint8_t {
        ORIGINAL,
        ADDED,
    };

    struct EditorPiece {
        EditorPieceSource source = EditorPieceSource::ORIGINAL;
        size_t start = 0u;
        size_t size = 0u;
        size_t newline_count = 0u;
    };

    struct EditorLine {
        char const* text = nullptr;
        size_t size = 0u;
    };

    struct EditorTextPosition {
        size_t line = 0u;
        size_t column = 0u;
    };

    struct EditorPieceNode;

    struct EditorText {
        Vec<char> original = {};
        Vec<char> added = {};
        EditorPieceNode* root = nullptr;
        mutable Vec<char> line_cache = {};
        Arena* arena = nullptr;
        uint32_t node_seed = 1u;
        uint64_t revision = 0u;
    };

    auto text_buffer_init(EditorText& text, Arena& arena) -> void;
    auto text_buffer_set(EditorText& text, StrRef value) -> void;
    auto text_buffer_clone(EditorText const& source, EditorText& target, Arena& arena) -> void;
    auto text_buffer_insert(EditorText& text, size_t offset, StrRef value) -> void;
    auto text_buffer_erase(EditorText& text, size_t start, size_t end) -> void;

    [[nodiscard]] auto text_buffer_size(EditorText const& text) -> size_t;
    [[nodiscard]] auto text_buffer_line_count(EditorText const& text) -> size_t;
    [[nodiscard]] auto text_buffer_line_size(EditorText const& text, size_t line) -> size_t;
    [[nodiscard]] auto text_buffer_line(EditorText const& text, size_t line) -> EditorLine;
    [[nodiscard]] auto text_buffer_line_text(EditorLine line) -> StrRef;
    [[nodiscard]] auto
    text_buffer_position_to_offset(EditorText const& text, EditorTextPosition position) -> size_t;
    [[nodiscard]] auto text_buffer_offset_to_position(EditorText const& text, size_t offset)
        -> EditorTextPosition;
    [[nodiscard]] auto text_buffer_copy(EditorText const& text, Arena& arena) -> StrRef;
    [[nodiscard]] auto
    text_buffer_copy_range(EditorText const& text, Arena& arena, size_t start, size_t end)
        -> StrRef;

} // namespace code_editor
