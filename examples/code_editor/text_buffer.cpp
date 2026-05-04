#include "text_buffer.h"

#include <algorithm>
#include <cstring>

namespace code_editor {

    struct SplitTree {
        EditorPieceNode* left = nullptr;
        EditorPieceNode* right = nullptr;
    };

    struct EditorPieceNode {
        EditorPiece piece = {};
        EditorPieceNode* left = nullptr;
        EditorPieceNode* right = nullptr;
        uint32_t priority = 0u;
        size_t total_size = 0u;
        size_t total_newline_count = 0u;
    };

    [[nodiscard]] auto count_newlines(StrRef text) -> size_t {
        size_t count = 0u;
        for (char ch : text) {
            if (ch == '\n') {
                count += 1u;
            }
        }
        return count;
    }

    [[nodiscard]] auto subtree_size(EditorPieceNode const* node) -> size_t {
        return node != nullptr ? node->total_size : 0u;
    }

    [[nodiscard]] auto subtree_newline_count(EditorPieceNode const* node) -> size_t {
        return node != nullptr ? node->total_newline_count : 0u;
    }

    auto refresh(EditorPieceNode* node) -> void {
        if (node == nullptr) {
            return;
        }
        node->total_size = subtree_size(node->left) + node->piece.size + subtree_size(node->right);
        node->total_newline_count = subtree_newline_count(node->left) + node->piece.newline_count +
                                    subtree_newline_count(node->right);
    }

    [[nodiscard]] auto next_priority(EditorText& text) -> uint32_t {
        text.node_seed += 0x9e3779b9u;
        uint32_t value = text.node_seed;
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    [[nodiscard]] auto piece_text(EditorText const& text, EditorPiece piece) -> StrRef {
        Vec<char> const& source =
            piece.source == EditorPieceSource::ORIGINAL ? text.original : text.added;
        DEBUG_ASSERT(piece.start + piece.size <= source.size());
        return StrRef(source.data() + piece.start, piece.size);
    }

    [[nodiscard]] auto make_piece(EditorPieceSource source, size_t start, StrRef text)
        -> EditorPiece {
        return {
            .source = source,
            .start = start,
            .size = text.size(),
            .newline_count = count_newlines(text),
        };
    }

    [[nodiscard]] auto
    make_piece(EditorPieceSource source, size_t start, size_t size, size_t newline_count)
        -> EditorPiece {
        return {.source = source, .start = start, .size = size, .newline_count = newline_count};
    }

    [[nodiscard]] auto split_piece(EditorText const& text, EditorPiece piece, size_t start)
        -> EditorPiece {
        StrRef const source = piece_text(text, piece).drop_prefix(start);
        return make_piece(piece.source, piece.start + start, source);
    }

    [[nodiscard]] auto
    split_piece(EditorText const& text, EditorPiece piece, size_t start, size_t size)
        -> EditorPiece {
        StrRef const source = piece_text(text, piece).slice(start, size);
        return make_piece(piece.source, piece.start + start, source);
    }

    [[nodiscard]] auto make_node(EditorText& text, EditorPiece piece) -> EditorPieceNode* {
        if (piece.size == 0u) {
            return nullptr;
        }
        EditorPieceNode* const node = arena_new<EditorPieceNode>(*text.arena);
        node->piece = piece;
        node->priority = next_priority(text);
        refresh(node);
        return node;
    }

    [[nodiscard]] auto merge_raw(EditorPieceNode* left, EditorPieceNode* right)
        -> EditorPieceNode* {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }
        if (left->priority >= right->priority) {
            left->right = merge_raw(left->right, right);
            refresh(left);
            return left;
        }
        right->left = merge_raw(left, right->left);
        refresh(right);
        return right;
    }

    auto append_leaf(EditorText& text, EditorPiece piece) -> void {
        text.root = merge_raw(text.root, make_node(text, piece));
    }

    [[nodiscard]] auto pop_last(EditorPieceNode* node, EditorPieceNode*& out_node)
        -> EditorPieceNode* {
        DEBUG_ASSERT(node != nullptr);
        if (node->right == nullptr) {
            EditorPieceNode* const left = node->left;
            node->left = nullptr;
            refresh(node);
            out_node = node;
            return left;
        }
        node->right = pop_last(node->right, out_node);
        refresh(node);
        return node;
    }

    [[nodiscard]] auto pop_first(EditorPieceNode* node, EditorPieceNode*& out_node)
        -> EditorPieceNode* {
        DEBUG_ASSERT(node != nullptr);
        if (node->left == nullptr) {
            EditorPieceNode* const right = node->right;
            node->right = nullptr;
            refresh(node);
            out_node = node;
            return right;
        }
        node->left = pop_first(node->left, out_node);
        refresh(node);
        return node;
    }

    [[nodiscard]] auto can_coalesce(EditorPiece const& left, EditorPiece const& right) -> bool {
        return left.source == right.source && left.start + left.size == right.start;
    }

    [[nodiscard]] auto concat(EditorPieceNode* left, EditorPieceNode* right) -> EditorPieceNode* {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }

        EditorPieceNode* last = nullptr;
        EditorPieceNode* first = nullptr;
        left = pop_last(left, last);
        right = pop_first(right, first);

        if (can_coalesce(last->piece, first->piece)) {
            last->piece.size += first->piece.size;
            last->piece.newline_count += first->piece.newline_count;
            refresh(last);
            return merge_raw(merge_raw(left, last), right);
        }
        return merge_raw(merge_raw(merge_raw(left, last), first), right);
    }

    auto build_original_tree(EditorText& text) -> void {
        size_t piece_start = 0u;
        for (size_t index = 0u; index < text.original.size(); ++index) {
            if (text.original[index] == '\n') {
                StrRef const piece_text(
                    text.original.data() + piece_start, index + 1u - piece_start
                );
                append_leaf(text, make_piece(EditorPieceSource::ORIGINAL, piece_start, piece_text));
                piece_start = index + 1u;
            }
        }
        if (piece_start < text.original.size()) {
            StrRef const piece_text(
                text.original.data() + piece_start, text.original.size() - piece_start
            );
            append_leaf(text, make_piece(EditorPieceSource::ORIGINAL, piece_start, piece_text));
        }
    }

    [[nodiscard]] auto split_tree(EditorText& text, EditorPieceNode* node, size_t offset)
        -> SplitTree {
        if (node == nullptr) {
            return {};
        }

        size_t const left_size = subtree_size(node->left);
        if (offset < left_size) {
            SplitTree split = split_tree(text, node->left, offset);
            node->left = split.right;
            refresh(node);
            split.right = node;
            return split;
        }

        size_t const piece_end = left_size + node->piece.size;
        if (offset > piece_end) {
            SplitTree split = split_tree(text, node->right, offset - piece_end);
            node->right = split.left;
            refresh(node);
            split.left = node;
            return split;
        }

        if (offset == left_size) {
            EditorPieceNode* const left = node->left;
            node->left = nullptr;
            refresh(node);
            return {left, node};
        }
        if (offset == piece_end) {
            EditorPieceNode* const right = node->right;
            node->right = nullptr;
            refresh(node);
            return {node, right};
        }

        size_t const local = offset - left_size;
        EditorPiece const old_piece = node->piece;
        EditorPieceNode* const old_left = node->left;
        EditorPieceNode* const old_right = node->right;
        node->left = nullptr;
        node->right = nullptr;
        node->piece = split_piece(text, old_piece, 0u, local);
        refresh(node);

        EditorPieceNode* const right_piece = make_node(text, split_piece(text, old_piece, local));
        return {merge_raw(old_left, node), merge_raw(right_piece, old_right)};
    }

    auto append_bytes(Vec<char>& target, StrRef text) -> size_t {
        size_t const start = target.size();
        size_t const copied = target.append(Slice<char const>(text.data(), text.size()));
        DEBUG_ASSERT(copied == text.size());
        (void)copied;
        return start;
    }

    auto append_byte(Vec<char>& target, char ch) -> void {
        bool const ok = target.push_back(ch);
        DEBUG_ASSERT(ok);
        (void)ok;
    }

    auto copy_storage(Vec<char> const& source, Vec<char>& target) -> void {
        target.clear();
        size_t const copied = target.append(Slice<char const>(source.data(), source.size()));
        DEBUG_ASSERT(copied == source.size());
        (void)copied;
    }

    [[nodiscard]] auto clone_tree(EditorText& target, EditorPieceNode const* source)
        -> EditorPieceNode* {
        if (source == nullptr) {
            return nullptr;
        }
        EditorPieceNode* const node = arena_new<EditorPieceNode>(*target.arena);
        node->piece = source->piece;
        node->priority = source->priority;
        node->left = clone_tree(target, source->left);
        node->right = clone_tree(target, source->right);
        refresh(node);
        return node;
    }

    [[nodiscard]] auto copy_range_to_node(
        EditorText const& text, EditorPieceNode const* node, size_t start, size_t end, char* out
    ) -> char* {
        if (node == nullptr || start == end) {
            return out;
        }

        size_t const left_size = subtree_size(node->left);
        if (start < left_size) {
            out = copy_range_to_node(text, node->left, start, std::min(end, left_size), out);
        }

        size_t const piece_start = left_size;
        size_t const piece_end = piece_start + node->piece.size;
        if (start < piece_end && end > piece_start) {
            size_t const local_start = start > piece_start ? start - piece_start : 0u;
            size_t const local_end = std::min(end, piece_end) - piece_start;
            size_t const local_size = local_end - local_start;
            StrRef const source = piece_text(text, node->piece);
            std::memcpy(out, source.data() + local_start, local_size);
            out += local_size;
        }

        if (end > piece_end) {
            size_t const right_start = start > piece_end ? start - piece_end : 0u;
            size_t const right_end = end - piece_end;
            out = copy_range_to_node(text, node->right, right_start, right_end, out);
        }
        return out;
    }

    auto copy_range_to(EditorText const& text, size_t start, size_t size, char* out) -> void {
        char* const end = copy_range_to_node(text, text.root, start, start + size, out);
        DEBUG_ASSERT(end == out + size);
        (void)end;
    }

    [[nodiscard]] auto find_newline_offset(
        EditorText const& text, EditorPieceNode const* node, size_t newline, size_t offset
    ) -> size_t {
        DEBUG_ASSERT(node != nullptr);
        size_t const left_newlines = subtree_newline_count(node->left);
        if (newline < left_newlines) {
            return find_newline_offset(text, node->left, newline, offset);
        }

        size_t const left_size = subtree_size(node->left);
        newline -= left_newlines;
        if (newline < node->piece.newline_count) {
            StrRef const source = piece_text(text, node->piece);
            for (size_t index = 0u; index < source.size(); ++index) {
                if (source[index] == '\n') {
                    if (newline == 0u) {
                        return offset + left_size + index;
                    }
                    newline -= 1u;
                }
            }
        }

        newline -= node->piece.newline_count;
        return find_newline_offset(
            text, node->right, newline, offset + left_size + node->piece.size
        );
    }

    [[nodiscard]] auto find_line_start(EditorText const& text, size_t line) -> size_t {
        if (line == 0u) {
            return 0u;
        }
        return find_newline_offset(text, text.root, line - 1u, 0u) + 1u;
    }

    [[nodiscard]] auto line_range(EditorText const& text, size_t line, size_t& start) -> size_t {
        size_t const line_count = text_buffer_line_count(text);
        DEBUG_ASSERT(line < line_count);
        start = find_line_start(text, line);
        if (line + 1u < line_count) {
            return find_line_start(text, line + 1u) - start - 1u;
        }
        return text_buffer_size(text) - start;
    }

    [[nodiscard]] auto
    direct_line(EditorText const& text, EditorPieceNode const* node, size_t start, size_t size)
        -> EditorLine {
        if (node == nullptr || size == 0u) {
            return {};
        }

        size_t const left_size = subtree_size(node->left);
        if (start + size <= left_size) {
            return direct_line(text, node->left, start, size);
        }

        size_t const piece_end = left_size + node->piece.size;
        if (start >= piece_end) {
            return direct_line(text, node->right, start - piece_end, size);
        }
        if (start >= left_size && start + size <= piece_end) {
            StrRef const source = piece_text(text, node->piece);
            return {source.data() + start - left_size, size};
        }
        return {};
    }

    auto scan_position(
        EditorText const& text,
        EditorPieceNode const* node,
        size_t& remaining,
        EditorTextPosition& position
    ) -> void {
        if (node == nullptr || remaining == 0u) {
            return;
        }

        scan_position(text, node->left, remaining, position);
        if (remaining == 0u) {
            return;
        }

        StrRef const source = piece_text(text, node->piece);
        for (size_t index = 0u; index < source.size() && remaining != 0u; ++index) {
            if (source[index] == '\n') {
                position.line += 1u;
                position.column = 0u;
            } else {
                position.column += 1u;
            }
            remaining -= 1u;
        }

        scan_position(text, node->right, remaining, position);
    }

    auto text_buffer_init(EditorText& text, Arena& arena) -> void {
        if (text.arena != nullptr) {
            return;
        }
        text.arena = &arena;

        bool ok = text.original.init(0u, arena.resource());
        DEBUG_ASSERT(ok);
        ok = text.added.init(0u, arena.resource());
        DEBUG_ASSERT(ok);
        ok = text.line_cache.init(0u, arena.resource());
        DEBUG_ASSERT(ok);
        (void)ok;
    }

    auto text_buffer_set(EditorText& text, StrRef value) -> void {
        DEBUG_ASSERT(text.arena != nullptr);
        text.original.clear();
        text.added.clear();
        text.root = nullptr;
        text.line_cache.clear();
        text.node_seed = 1u;

        bool first = true;
        for (StrRef line : value.lines()) {
            if (!first) {
                append_byte(text.original, '\n');
            }
            size_t const copied = text.original.append(Slice<char const>(line.data(), line.size()));
            DEBUG_ASSERT(copied == line.size());
            (void)copied;
            first = false;
        }

        if (!text.original.empty()) {
            build_original_tree(text);
        }
        text.revision += 1u;
    }

    auto text_buffer_clone(EditorText const& source, EditorText& target) -> void {
        DEBUG_ASSERT(source.arena != nullptr);
        text_buffer_init(target, *source.arena);
        copy_storage(source.original, target.original);
        copy_storage(source.added, target.added);
        target.root = clone_tree(target, source.root);
        target.line_cache.clear();
        target.node_seed = source.node_seed;
        target.revision += 1u;
    }

    auto text_buffer_insert(EditorText& text, size_t offset, StrRef value) -> void {
        if (value.empty()) {
            return;
        }
        DEBUG_ASSERT(text.arena != nullptr);
        offset = std::min(offset, text_buffer_size(text));

        size_t const newline_count = count_newlines(value);
        size_t const added_start = append_bytes(text.added, value);
        EditorPieceNode* const inserted = make_node(
            text, make_piece(EditorPieceSource::ADDED, added_start, value.size(), newline_count)
        );

        SplitTree split = split_tree(text, text.root, offset);
        text.root = concat(concat(split.left, inserted), split.right);
        text.line_cache.clear();
        text.revision += 1u;
    }

    auto text_buffer_erase(EditorText& text, size_t start, size_t end) -> void {
        size_t const text_size = text_buffer_size(text);
        start = std::min(start, text_size);
        end = std::min(end, text_size);
        if (end < start) {
            size_t const temp = start;
            start = end;
            end = temp;
        }
        if (start == end) {
            return;
        }

        SplitTree left_split = split_tree(text, text.root, start);
        SplitTree right_split = split_tree(text, left_split.right, end - start);
        text.root = concat(left_split.left, right_split.right);
        text.line_cache.clear();
        text.revision += 1u;
    }

    [[nodiscard]] auto text_buffer_size(EditorText const& text) -> size_t {
        return subtree_size(text.root);
    }

    [[nodiscard]] auto text_buffer_line_count(EditorText const& text) -> size_t {
        return subtree_newline_count(text.root) + 1u;
    }

    [[nodiscard]] auto text_buffer_line_size(EditorText const& text, size_t line) -> size_t {
        size_t start = 0u;
        return line_range(text, line, start);
    }

    [[nodiscard]] auto text_buffer_line(EditorText const& text, size_t line) -> EditorLine {
        size_t start = 0u;
        size_t const size = line_range(text, line, start);
        EditorLine const direct = direct_line(text, text.root, start, size);
        if (direct.text != nullptr || size == 0u) {
            return direct;
        }

        bool const ok = text.line_cache.non_zero_resize(size + 1u);
        DEBUG_ASSERT(ok);
        (void)ok;
        copy_range_to(text, start, size, text.line_cache.data());
        text.line_cache[size] = '\0';
        return {text.line_cache.data(), size};
    }

    [[nodiscard]] auto text_buffer_line_text(EditorLine line) -> StrRef {
        return StrRef(line.text, line.size);
    }

    [[nodiscard]] auto
    text_buffer_position_to_offset(EditorText const& text, EditorTextPosition position) -> size_t {
        position.line = std::min(position.line, text_buffer_line_count(text) - 1u);
        size_t start = 0u;
        size_t const size = line_range(text, position.line, start);
        return start + std::min(position.column, size);
    }

    [[nodiscard]] auto text_buffer_offset_to_position(EditorText const& text, size_t offset)
        -> EditorTextPosition {
        offset = std::min(offset, text_buffer_size(text));
        EditorTextPosition position = {};
        scan_position(text, text.root, offset, position);
        return position;
    }

    [[nodiscard]] auto text_buffer_copy(EditorText const& text, Arena& arena) -> StrRef {
        return text_buffer_copy_range(text, arena, 0u, text_buffer_size(text));
    }

    [[nodiscard]] auto
    text_buffer_copy_range(EditorText const& text, Arena& arena, size_t start, size_t end)
        -> StrRef {
        size_t const text_size = text_buffer_size(text);
        start = std::min(start, text_size);
        end = std::min(end, text_size);
        if (end < start) {
            size_t const temp = start;
            start = end;
            end = temp;
        }
        size_t const size = end - start;
        if (size == 0u) {
            return {};
        }

        char* const data = arena_alloc<char>(arena, size + 1u);
        copy_range_to(text, start, size, data);
        data[size] = '\0';
        return StrRef(data, size);
    }

} // namespace code_editor
