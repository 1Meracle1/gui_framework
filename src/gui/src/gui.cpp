#include <algorithm>
#include <base/assert.h>
#include <base/config.h>
#include <base/memory.h>
#include <cmath>
#include <cstring>
#include <gui/gui.h>
#include <utility>

namespace gui {
    namespace {

        inline constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);
        inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
        inline constexpr uint64_t FNV_PRIME = 1099511628211ull;
        inline constexpr float SCROLLBAR_MARGIN = 2.0f;
        inline constexpr float SCROLLBAR_WIDTH = 6.0f;
        inline constexpr float SCROLLBAR_MIN_THUMB_HEIGHT = 12.0f;
        inline constexpr float TEXT_RASTER_PADDING = 2.0f;

        struct TextUndoEntry {
            TextUndoEntry* previous = nullptr;
            char* text = nullptr;
            size_t text_size = 0u;
            size_t cursor = 0u;
            TextSelection selection = {};
        };

        struct StateEntry {
            Id id = {};
            Rect rect = {};
            uint64_t last_frame = 0u;
            float scroll_y = 0.0f;
            float scroll_max_y = 0.0f;
            float scroll_viewport_height = 0.0f;
            float scroll_content_height = 0.0f;
            float scroll_request_y = 0.0f;
            float floating_offset_x = 0.0f;
            float floating_offset_y = 0.0f;
            size_t scroll_request_index = 0u;
            size_t text_selection_anchor = 0u;
            size_t text_selection_start = 0u;
            size_t text_selection_end = 0u;
            size_t text_selection_word_start = 0u;
            size_t text_selection_word_end = 0u;
            size_t text_cursor = 0u;
            TextUndoEntry* text_undo_stack = nullptr;
            font_cache::Font font = {};
            float font_size = 0.0f;
            ScrollReveal scroll_request_reveal = ScrollReveal::KEEP_VISIBLE;
            bool scroll_request_set = false;
            bool scroll_request_end = false;
            bool scroll_request_index_set = false;
            bool scroll_valid = false;
            bool text_selection_word_active = false;
            bool occupied = false;
        };

        struct BoxNode {
            Id id = {};
            Id parent_id = {};
            BoxKind kind = BoxKind::ROOT;
            size_t parent_index = INVALID_INDEX;
            size_t first_child = INVALID_INDEX;
            size_t last_child = INVALID_INDEX;
            size_t next_sibling = INVALID_INDEX;
            size_t child_count = 0u;
            size_t depth = 0u;
            StrRef text = {};
            StrRef debug_name = {};
            TextSelection text_selection = {};
            LayoutDesc layout = {};
            StyleDesc style = {};
            StyleDesc resolved_style = {};
            Rect rect = {};
            Vec2 measured_size = {};
            float scroll_content_height = 0.0f;
            float scroll_offset_y = 0.0f;
            float widget_value = 0.0f;
            Signal signal = {};
            size_t table_column_span = 1u;
            size_t table_row_span = 1u;
            BoxFlags flags = BOX_FLAG_NONE;
            bool duplicate_id = false;
            bool interactive = false;
            bool focusable = false;
            bool focus_ordered = false;
            Id authored_id = {};
            BoxIdSource id_source = BoxIdSource::STRUCTURAL;
            bool stable_id = false;
            StateEntry* state = nullptr;
            StateEntry* scroll_state = nullptr;
        };

        struct TableCellPlacement {
            size_t box_index = INVALID_INDEX;
            size_t row = 0u;
            size_t column = 0u;
            size_t row_span = 1u;
            size_t column_span = 1u;
        };

        struct TableLayout {
            float* columns = nullptr;
            float* rows = nullptr;
            bool* occupied = nullptr;
            TableCellPlacement* cells = nullptr;
            size_t row_count = 0u;
            size_t column_capacity = 0u;
            size_t column_count = 0u;
            size_t cell_count = 0u;
        };

        struct TextLine {
            StrRef text = {};
            size_t start = 0u;
            size_t end = 0u;
        };

        struct TextEditBuffer {
            char* data = nullptr;
            size_t size = 0u;
            size_t capacity = 0u;
            StringBuffer* string = nullptr;
        };

        struct ContextImpl {
            Arena* context_arena = nullptr;
            Arena frame_arena = {};
            BoxNode* boxes = nullptr;
            BoxInfo* infos = nullptr;
            size_t* parent_stack = nullptr;
            Id* focus_order = nullptr;
            StateEntry* state_table = nullptr;
            size_t box_capacity = 0u;
            size_t state_table_size = 0u;
            size_t box_count = 0u;
            size_t parent_stack_count = 0u;
            size_t focus_order_count = 0u;
            FrameDesc frame_desc = {};
            InputState previous_input = {};
            ThemeDesc theme = {};
            SetClipboardTextFn set_clipboard_text = nullptr;
            GetClipboardTextFn get_clipboard_text = nullptr;
            void* clipboard_user_data = nullptr;
            Id hot_id = {};
            Id active_id = {};
            Id active_scroll_id = {};
            Id focused_id = {};
            Id frame_start_focus_id = {};
            Id focus_request_id = {};
            Id text_selection_owner_id = {};
            float active_scroll_thumb_offset_y = 0.0f;
            uint64_t frame_index = 0u;
            bool building = false;
        };

        [[nodiscard]] auto impl_from_context(Context context) -> ContextImpl* {
            return static_cast<ContextImpl*>(context.handle);
        }

        [[nodiscard]] auto impl_from_frame(Frame const& frame) -> ContextImpl* {
            return static_cast<ContextImpl*>(detail::frame_handle(frame));
        }

        [[nodiscard]] constexpr auto hash_u64(uint64_t value, uint64_t seed = FNV_OFFSET)
            -> uint64_t {
            uint64_t hash = seed;
            for (uint32_t byte_index = 0u; byte_index < 8u; ++byte_index) {
                hash ^= (value >> (byte_index * 8u)) & 0xffu;
                hash *= FNV_PRIME;
            }
            return hash != 0u ? hash : 1u;
        }

        [[nodiscard]] constexpr auto hash_combine(uint64_t seed, uint64_t value) -> uint64_t {
            return hash_u64(value, seed != 0u ? seed : FNV_OFFSET);
        }

        [[nodiscard]] auto next_power_of_two(size_t value) -> size_t {
            size_t result = 1u;
            while (result < value) {
                result *= 2u;
            }
            return result;
        }

        [[nodiscard]] auto color_set(Color color) -> bool {
            return color.a >= 0.0f;
        }

        [[nodiscard]] auto color_visible(Color color) -> bool {
            return color_set(color) && color.a > 0.0f;
        }

        [[nodiscard]] auto color_mul_alpha(Color value, float opacity) -> Color {
            if (color_set(value)) {
                value.a *= opacity;
            }
            return value;
        }

        [[nodiscard]] auto boolean_widget(BoxKind kind) -> bool {
            return kind == BoxKind::CHECKBOX || kind == BoxKind::TOGGLE;
        }

        [[nodiscard]] auto floating_box(BoxKind kind) -> bool {
            return kind == BoxKind::POPUP || kind == BoxKind::MODAL;
        }

        [[nodiscard]] auto input_text_box(BoxKind kind) -> bool {
            return kind == BoxKind::INPUT_TEXT || kind == BoxKind::INPUT_TEXT_MULTILINE;
        }

        [[nodiscard]] auto multiline_input_text_box(BoxKind kind) -> bool {
            return kind == BoxKind::INPUT_TEXT_MULTILINE;
        }

        [[nodiscard]] auto table_row_box(BoxKind kind) -> bool {
            return kind == BoxKind::TABLE_ROW || kind == BoxKind::TABLE_HEADER_ROW;
        }

        [[nodiscard]] auto table_cell_box(BoxKind kind) -> bool {
            return kind == BoxKind::TABLE_CELL || kind == BoxKind::TABLE_HEADER_CELL;
        }

        [[nodiscard]] auto role_styled(StyleRole role) -> bool {
            return role != StyleRole::AUTO && role != StyleRole::NONE && role != StyleRole::COUNT;
        }

        auto merge_style(StyleDesc& dst, StyleDesc const& src) -> void {
            if (color_set(src.background)) {
                dst.background = src.background;
            }
            if (color_set(src.foreground)) {
                dst.foreground = src.foreground;
            }
            if (color_set(src.border)) {
                dst.border = src.border;
            }
            if (src.border_thickness != 0.0f) {
                dst.border_thickness = src.border_thickness;
            }
            if (src.radius >= 0.0f) {
                dst.radius = src.radius;
            }
            if (color_set(src.shadow.color)) {
                dst.shadow = src.shadow;
            }
            dst.opacity *= src.opacity;
            if (font_cache::font_valid(src.font)) {
                dst.font = src.font;
            }
            if (src.font_size > 0.0f) {
                dst.font_size = src.font_size;
            }
        }

        auto merge_theme_style(StyleDesc& dst, ThemeStyle const& src, StyleStateFlags state)
            -> void {
            merge_style(dst, src.normal);
            if ((state & STYLE_STATE_CHECKED) != 0u) {
                merge_style(dst, src.checked);
            }
            if ((state & STYLE_STATE_FOCUSED) != 0u) {
                merge_style(dst, src.focused);
            }
            if ((state & STYLE_STATE_HOVERED) != 0u) {
                merge_style(dst, src.hovered);
            }
            if ((state & STYLE_STATE_ACTIVE) != 0u) {
                merge_style(dst, src.active);
            }
            if ((state & STYLE_STATE_READ_ONLY) != 0u) {
                merge_style(dst, src.read_only);
            }
            if ((state & STYLE_STATE_DISABLED) != 0u) {
                merge_style(dst, src.disabled);
            }
        }

        [[nodiscard]] auto rect_contains(Rect rect, Vec2 point) -> bool {
            return point.x >= rect.min.x && point.x <= rect.max.x && point.y >= rect.min.y &&
                   point.y <= rect.max.y;
        }

        [[nodiscard]] auto rect_width(Rect rect) -> float {
            return rect.max.x - rect.min.x;
        }

        [[nodiscard]] auto rect_height(Rect rect) -> float {
            return rect.max.y - rect.min.y;
        }

        [[nodiscard]] auto box_clips(BoxNode const& box) -> bool {
            return box.layout.clip || box.layout.scroll_x || box.layout.scroll_y ||
                   box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::LIST ||
                   input_text_box(box.kind) || table_cell_box(box.kind);
        }

        [[nodiscard]] auto top_layer_box(ContextImpl const* impl, size_t index) -> bool {
            for (size_t box_index = index; box_index != INVALID_INDEX;
                 box_index = impl->boxes[box_index].parent_index) {
                if (floating_box(impl->boxes[box_index].kind)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto scrollbar_corner_inset(float radius) -> float {
            if (radius <= SCROLLBAR_MARGIN) {
                return SCROLLBAR_MARGIN;
            }

            float const x = radius - SCROLLBAR_MARGIN;
            float const y = radius - std::sqrt(std::max(0.0f, radius * radius - x * x)) + 1.0f;
            return std::max(SCROLLBAR_MARGIN, y);
        }

        [[nodiscard]] auto scrollbar_track_rect(Rect rect, float radius) -> Rect {
            float const width = std::min(
                SCROLLBAR_WIDTH, std::max(0.0f, rect_width(rect) - SCROLLBAR_MARGIN * 2.0f)
            );
            float const y_inset =
                std::min(rect_height(rect) * 0.5f, scrollbar_corner_inset(radius));
            return {
                {rect.max.x - SCROLLBAR_MARGIN - width, rect.min.y + y_inset},
                {rect.max.x - SCROLLBAR_MARGIN, rect.max.y - y_inset}
            };
        }

        [[nodiscard]] auto scrollbar_track_valid(Rect track) -> bool {
            return rect_width(track) > 0.0f && rect_height(track) > 0.0f;
        }

        [[nodiscard]] auto scrollbar_thumb_rect(Rect track,
                                                float scroll_y,
                                                float max_y,
                                                float viewport_height,
                                                float content_height) -> Rect {
            float const height = rect_height(track);
            float const min_thumb = std::min(SCROLLBAR_MIN_THUMB_HEIGHT, height);
            float const thumb_height =
                std::clamp(height * viewport_height / content_height, min_thumb, height);
            float const thumb_y =
                track.min.y + (height - thumb_height) * (scroll_y / max_y);
            return {{track.min.x, thumb_y}, {track.max.x, thumb_y + thumb_height}};
        }

        [[nodiscard]] auto scrollbar_visible(StateEntry const* state) -> bool {
            return state != nullptr && state->scroll_valid && state->scroll_max_y > 0.0f &&
                   state->scroll_content_height > 0.0f;
        }

        [[nodiscard]] auto hit_passes_clips(ContextImpl const* impl, size_t index, Vec2 point)
            -> bool {
            bool const top_layer = top_layer_box(impl, index);
            for (size_t parent = impl->boxes[index].parent_index; parent != INVALID_INDEX;
                 parent = impl->boxes[parent].parent_index) {
                if (top_layer && !top_layer_box(impl, parent)) {
                    break;
                }
                BoxNode const& box = impl->boxes[parent];
                if (box_clips(box) && !rect_contains(box.rect, point)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto scrollbar_hit(ContextImpl const* impl, size_t index, Vec2 point)
            -> bool {
            BoxNode const& box = impl->boxes[index];
            if (!scrollbar_visible(box.scroll_state) || !hit_passes_clips(impl, index, point)) {
                return false;
            }
            Rect const track = scrollbar_track_rect(box.rect, box.resolved_style.radius);
            return scrollbar_track_valid(track) && rect_contains(track, point);
        }

        [[nodiscard]] auto mouse_over_scrollbar(ContextImpl const* impl,
                                                Vec2 point,
                                                bool top_layer) -> bool {
            for (size_t index = impl->box_count; index > 0u; --index) {
                size_t const box_index = index - 1u;
                if (top_layer_box(impl, box_index) == top_layer &&
                    scrollbar_hit(impl, box_index, point)) {
                    return true;
                }
            }
            return false;
        }

        auto apply_scroll_delta(ContextImpl* impl, StateEntry* state, Rect rect) -> void {
            if (state == nullptr || impl->frame_desc.input.scroll_delta_y == 0.0f ||
                !rect_contains(rect, impl->frame_desc.input.mouse_pos)) {
                return;
            }
            state->scroll_y =
                std::max(0.0f, state->scroll_y - impl->frame_desc.input.scroll_delta_y);
        }

        [[nodiscard]] auto scrollbar_scroll_y(Rect track,
                                              float thumb_height,
                                              float max_y,
                                              float mouse_y,
                                              float thumb_offset_y) -> float {
            float const movable_height = rect_height(track) - thumb_height;
            if (movable_height <= 0.0f) {
                return 0.0f;
            }

            float const thumb_y = std::clamp(
                mouse_y - thumb_offset_y, track.min.y, track.max.y - thumb_height);
            return (thumb_y - track.min.y) * max_y / movable_height;
        }

        auto apply_scrollbar_input(
            ContextImpl* impl,
            StateEntry* state,
            Rect rect,
            float radius,
            float viewport_height,
            float content_height,
            float max_y
        ) -> void {
            if (state == nullptr || max_y <= 0.0f || content_height <= 0.0f) {
                return;
            }

            Rect const track = scrollbar_track_rect(rect, radius);
            if (!scrollbar_track_valid(track)) {
                return;
            }

            InputState const& input = impl->frame_desc.input;
            Rect const thumb = scrollbar_thumb_rect(
                track, state->scroll_y, max_y, viewport_height, content_height
            );
            bool const mouse_pressed = input.mouse_down[0u] && !impl->previous_input.mouse_down[0u];
            if (mouse_pressed && impl->active_scroll_id.value == 0u &&
                rect_contains(track, input.mouse_pos)) {
                impl->active_scroll_id = state->id;
                impl->active_scroll_thumb_offset_y =
                    rect_contains(thumb, input.mouse_pos)
                        ? input.mouse_pos.y - thumb.min.y
                        : rect_height(thumb) * 0.5f;
            }

            if (impl->active_scroll_id.value == state->id.value && input.mouse_down[0u]) {
                state->scroll_y = scrollbar_scroll_y(track,
                                                     rect_height(thumb),
                                                     max_y,
                                                     input.mouse_pos.y,
                                                     impl->active_scroll_thumb_offset_y);
            }
        }

        [[nodiscard]] auto inset_width(Insets insets) -> float {
            return insets.left + insets.right;
        }

        [[nodiscard]] auto inset_height(Insets insets) -> float {
            return insets.top + insets.bottom;
        }

        [[nodiscard]] auto content_rect(Rect rect, Insets padding) -> Rect {
            rect.min.x += padding.left;
            rect.min.y += padding.top;
            rect.max.x -= padding.right;
            rect.max.y -= padding.bottom;
            if (rect.max.x < rect.min.x) {
                rect.max.x = rect.min.x;
            }
            if (rect.max.y < rect.min.y) {
                rect.max.y = rect.min.y;
            }
            return rect;
        }

        [[nodiscard]] auto text_font_size(BoxNode const& box) -> float {
            if (box.resolved_style.font_size > 0.0f) {
                return box.resolved_style.font_size;
            }
            if (box.state != nullptr && box.state->font_size > 0.0f) {
                return box.state->font_size;
            }
            return box.style.font_size > 0.0f ? box.style.font_size : 16.0f;
        }

        [[nodiscard]] auto text_font(BoxNode const& box) -> font_cache::Font {
            if (font_cache::font_valid(box.resolved_style.font)) {
                return box.resolved_style.font;
            }
            if (box.state != nullptr && font_cache::font_valid(box.state->font)) {
                return box.state->font;
            }
            return box.style.font;
        }

        [[nodiscard]] auto text_advance(BoxNode const& box, StrRef text) -> float {
            if (text.empty()) {
                return 0.0f;
            }
            font_cache::Font const font = text_font(box);
            float const font_size = text_font_size(box);
            return font_cache::font_valid(font)
                       ? font_cache::text_advance(font, font_size, text)
                       : static_cast<float>(text.size()) * font_size * 0.5f;
        }

        [[nodiscard]] auto rendered_text_height(font_cache::Font font, float font_size) -> float {
            font_provider::Metrics metrics = {};
            font_cache::metrics_from_font(font, font_size, metrics);
            return std::ceil(metrics.ascent + metrics.descent + TEXT_RASTER_PADDING * 2.0f);
        }

        [[nodiscard]] auto text_line_height(BoxNode const& box) -> float {
            float const font_size = text_font_size(box);
            font_cache::Font const font = text_font(box);
            return font_cache::font_valid(font)
                       ? rendered_text_height(font, font_size)
                       : font_size * 1.25f;
        }

        [[nodiscard]] auto text_multiline(StrRef text) -> bool {
            return text.find('\n') != StrRef::NPOS;
        }

        [[nodiscard]] auto next_text_line(StrRef text, size_t& offset, TextLine& out_line)
            -> bool {
            if (text.empty() || offset > text.size()) {
                return false;
            }
            if (offset == text.size()) {
                if (offset > 0u && text[offset - 1u] == '\n') {
                    out_line = {StrRef(text.data() + offset, size_t{0u}), offset, offset};
                    offset += 1u;
                    return true;
                }
                return false;
            }

            size_t const start = offset;
            size_t const newline = text.find('\n', start);
            size_t end = newline == StrRef::NPOS ? text.size() : newline;
            if (end > start && text[end - 1u] == '\r') {
                end -= 1u;
            }

            out_line = {text.substr(start, end - start), start, end};
            offset = newline == StrRef::NPOS ? text.size() : newline + 1u;
            return true;
        }

        [[nodiscard]] auto text_size(BoxNode const& box) -> Vec2 {
            if (box.text.empty()) {
                return {0.0f, text_line_height(box)};
            }

            float const line_height = text_line_height(box);
            Vec2 size = {};
            size_t line_count = 0u;
            size_t offset = 0u;
            TextLine line = {};
            while (next_text_line(box.text, offset, line)) {
                size.x = std::max(size.x, text_advance(box, line.text));
                line_count += 1u;
            }
            size.y = line_height * static_cast<float>(line_count);
            return size;
        }

        [[nodiscard]] auto text_x_offset(BoxNode const& box, float content_width, float text_width)
            -> float {
            if (box.kind == BoxKind::BUTTON) {
                return std::max(0.0f, content_width - text_width) * 0.5f;
            }
            if (box.kind == BoxKind::CHECKBOX) {
                return 24.0f;
            }
            if (box.kind == BoxKind::TOGGLE) {
                return 44.0f;
            }
            return 0.0f;
        }

        [[nodiscard]] auto input_text_scroll_x(BoxNode const& box, float content_width) -> float {
            if (box.kind != BoxKind::INPUT_TEXT || box.state == nullptr) {
                return 0.0f;
            }

            size_t const cursor = std::min(box.state->text_cursor, box.text.size());
            float const visible_width = std::max(0.0f, content_width - 1.0f);
            float const cursor_x = text_advance(box, box.text.prefix(cursor));
            return std::max(0.0f, cursor_x - visible_width);
        }

        [[nodiscard]] auto text_position(BoxNode const& box, Rect rect) -> Vec2 {
            Vec2 const text_dim = text_size(box);
            float const content_width =
                std::max(0.0f, rect_width(rect) - inset_width(box.layout.padding));
            float const x_offset = text_x_offset(box, content_width, text_dim.x);
            bool const multiline = text_multiline(box.text) || multiline_input_text_box(box.kind);
            float const scroll_y = box.layout.scroll_y && box.scroll_state != nullptr && multiline
                                       ? box.scroll_state->scroll_y
                                       : 0.0f;
            float const extra_y = rect_height(rect) - inset_height(box.layout.padding) - text_dim.y;
            return {
                rect.min.x + box.layout.padding.left + x_offset -
                    input_text_scroll_x(box, content_width),
                rect.min.y + box.layout.padding.top - scroll_y +
                    (multiline ? std::max(0.0f, extra_y) : extra_y) * 0.5f,
            };
        }

        [[nodiscard]] auto text_position(BoxNode const& box) -> Vec2 {
            return text_position(box, box.rect);
        }

        [[nodiscard]] auto copy_frame_str(ContextImpl* impl, StrRef value) -> StrRef {
            if (value.empty()) {
                return {};
            }
            char* const data = arena_alloc<char>(impl->frame_arena, value.size());
            std::memcpy(data, value.data(), value.size());
            return {data, value.size()};
        }

        [[nodiscard]] auto state_entry(ContextImpl* impl, Id id_value) -> StateEntry* {
            ASSERT(id_value.value != 0u);
            size_t const mask = impl->state_table_size - 1u;
            size_t slot = static_cast<size_t>(id_value.value) & mask;
            for (size_t probe = 0u; probe < impl->state_table_size; ++probe) {
                StateEntry* const entry = impl->state_table + slot;
                if (!entry->occupied) {
                    *entry = {};
                    entry->occupied = true;
                    entry->id = id_value;
                    return entry;
                }
                if (entry->id.value == id_value.value) {
                    return entry;
                }
                slot = (slot + 1u) & mask;
            }
            ASSERT_MSG(false, "UI state table is full");
            return nullptr;
        }

        [[nodiscard]] auto find_state_entry(ContextImpl const* impl, Id id_value)
            -> StateEntry const* {
            if (impl == nullptr || id_value.value == 0u) {
                return nullptr;
            }

            size_t const mask = impl->state_table_size - 1u;
            size_t slot = static_cast<size_t>(id_value.value) & mask;
            for (size_t probe = 0u; probe < impl->state_table_size; ++probe) {
                StateEntry const* const entry = impl->state_table + slot;
                if (!entry->occupied) {
                    return nullptr;
                }
                if (entry->id.value == id_value.value) {
                    return entry;
                }
                slot = (slot + 1u) & mask;
            }
            return nullptr;
        }

        [[nodiscard]] auto top_parent_index(ContextImpl const* impl) -> size_t {
            ASSERT(impl->parent_stack_count > 0u);
            return impl->parent_stack[impl->parent_stack_count - 1u];
        }

        auto push_parent(ContextImpl* impl, size_t box_index) -> void {
            ASSERT(impl->parent_stack_count < impl->box_capacity);
            impl->parent_stack[impl->parent_stack_count] = box_index;
            impl->parent_stack_count += 1u;
        }

        auto pop_parent_to(ContextImpl* impl, size_t box_index) -> void {
            if (!impl->building) {
                return;
            }
            while (impl->parent_stack_count > 1u) {
                size_t const top = impl->parent_stack[impl->parent_stack_count - 1u];
                impl->parent_stack_count -= 1u;
                if (top == box_index) {
                    break;
                }
            }
        }

        [[nodiscard]] auto
        id_from_parts(ContextImpl const* impl, size_t parent_index, uint64_t local_id, BoxKind kind)
            -> Id {
            BoxNode const& parent = impl->boxes[parent_index];
            uint64_t hash = hash_combine(parent.id.value, static_cast<uint64_t>(kind) + 1u);
            hash = hash_combine(hash, local_id);
            return {hash};
        }

        [[nodiscard]] auto structural_id(ContextImpl const* impl, size_t parent_index, BoxKind kind)
            -> Id {
            BoxNode const& parent = impl->boxes[parent_index];
            uint64_t hash = hash_combine(parent.id.value, static_cast<uint64_t>(kind) + 1u);
            hash = hash_combine(hash, parent.child_count + 1u);
            return {hash};
        }

        [[nodiscard]] auto
        text_id(ContextImpl const* impl, size_t parent_index, BoxKind kind, StrRef text) -> Id {
            return text.empty() ? structural_id(impl, parent_index, kind)
                                : id_from_parts(impl, parent_index, text.hash64(), kind);
        }

        [[nodiscard]] auto
        explicit_id(ContextImpl const* impl, size_t parent_index, BoxKind kind, Id id_value) -> Id {
            return id_value.value == 0u ? structural_id(impl, parent_index, kind)
                                        : id_from_parts(impl, parent_index, id_value.value, kind);
        }

        [[nodiscard]] auto scroll_state_key(Id id_value) -> Id {
            return id_value.value == 0u ? Id{} : Id{hash_combine(id_value.value, 0x7d9f4a7c15ull)};
        }

        auto set_scroll_state(ContextImpl* impl, BoxNode& box, Id id_value) -> void {
            Id const key = scroll_state_key(id_value);
            box.scroll_state = key.value != 0u ? state_entry(impl, key) : box.state;
        }

        [[nodiscard]] auto box_disabled(BoxNode const& box) -> bool {
            return (box.flags & BOX_FLAG_DISABLED) != 0u;
        }

        [[nodiscard]] auto box_read_only(BoxNode const& box) -> bool {
            return (box.flags & BOX_FLAG_READ_ONLY) != 0u;
        }

        [[nodiscard]] auto box_style_state(BoxNode const& box) -> StyleStateFlags {
            StyleStateFlags result = STYLE_STATE_NONE;
            if (box.signal.hovered) {
                result |= STYLE_STATE_HOVERED;
            }
            if (box.signal.active) {
                result |= STYLE_STATE_ACTIVE;
            }
            if (box.signal.focused) {
                result |= STYLE_STATE_FOCUSED;
            }
            if (box_disabled(box)) {
                result |= STYLE_STATE_DISABLED;
            }
            if (box_read_only(box)) {
                result |= STYLE_STATE_READ_ONLY;
            }
            if (boolean_widget(box.kind) && box.widget_value > 0.5f) {
                result |= STYLE_STATE_CHECKED;
            }
            return result;
        }

        [[nodiscard]] auto key_pressed(ContextImpl const* impl, Key key_value, bool repeat)
            -> bool {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr) {
                return false;
            }
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                KeyEvent const& event = input.key_events[index];
                if (event.key == key_value && (event.kind == KeyEventKind::PRESS ||
                                               (repeat && event.kind == KeyEventKind::REPEAT))) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto shortcut_key_pressed(KeyEvent const& event, Key key) -> bool {
            return event.key == key && event.kind == KeyEventKind::PRESS &&
                   (event.mods & KEY_MOD_CTRL) != 0u;
        }

        [[nodiscard]] auto copy_shortcut_pressed(ContextImpl const* impl) -> bool {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr) {
                return false;
            }
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                if (shortcut_key_pressed(input.key_events[index], Key::C)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto tab_reverse(KeyEvent const& event) -> bool {
            return (event.mods & KEY_MOD_SHIFT) != 0u;
        }

        auto move_focus(ContextImpl* impl, bool reverse) -> void {
            if (impl->focus_order_count == 0u) {
                return;
            }

            size_t current = impl->focus_order_count;
            for (size_t index = 0u; index < impl->focus_order_count; ++index) {
                if (impl->focus_order[index].value == impl->focused_id.value) {
                    current = index;
                    break;
                }
            }

            if (current == impl->focus_order_count) {
                current = reverse ? impl->focus_order_count - 1u : 0u;
            } else if (reverse) {
                current = current == 0u ? impl->focus_order_count - 1u : current - 1u;
            } else {
                current = (current + 1u) % impl->focus_order_count;
            }
            impl->focused_id = impl->focus_order[current];
        }

        [[nodiscard]] auto focused_box_captures_tab(ContextImpl const* impl,
                                                    KeyEvent const& event) -> bool {
            if ((event.mods & (KEY_MOD_CTRL | KEY_MOD_ALT | KEY_MOD_SUPER)) != 0u) {
                return false;
            }
            for (size_t index = 0u; index < impl->box_count; ++index) {
                BoxNode const& box = impl->boxes[index];
                if (box.id.value == impl->focused_id.value &&
                    multiline_input_text_box(box.kind)) {
                    return true;
                }
            }
            return false;
        }

        auto process_focus_keys(ContextImpl* impl) -> void {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr) {
                return;
            }
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                KeyEvent const& event = input.key_events[index];
                if (event.key == Key::TAB &&
                    (event.kind == KeyEventKind::PRESS || event.kind == KeyEventKind::REPEAT)) {
                    if (!focused_box_captures_tab(impl, event)) {
                        move_focus(impl, tab_reverse(event));
                    }
                }
            }
        }

        [[nodiscard]] auto compute_hot_id(ContextImpl const* impl) -> Id {
            Vec2 const mouse_pos = impl->frame_desc.input.mouse_pos;
            for (uint32_t pass = 0u; pass < 2u; ++pass) {
                bool const top_layer = pass == 0u;
                if (mouse_over_scrollbar(impl, mouse_pos, top_layer)) {
                    return {};
                }
                for (size_t index = impl->box_count; index > 0u; --index) {
                    size_t const box_index = index - 1u;
                    BoxNode const& box = impl->boxes[box_index];
                    if (top_layer_box(impl, box_index) != top_layer) {
                        continue;
                    }
                    if (box.interactive && !box_disabled(box) && box.state != nullptr &&
                        box.state->last_frame != 0u && rect_contains(box.state->rect, mouse_pos) &&
                        hit_passes_clips(impl, box_index, mouse_pos)) {
                        return box.id;
                    }
                }
            }
            return {};
        }

        auto add_focus_order(ContextImpl* impl, BoxNode& box) -> void {
            if (box_disabled(box)) {
                if (impl->focused_id.value == box.id.value) {
                    impl->focused_id = {};
                }
                return;
            }
            if (!box.focusable || box.focus_ordered) {
                return;
            }
            ASSERT(impl->focus_order_count < impl->box_capacity);
            impl->focus_order[impl->focus_order_count] = box.id;
            impl->focus_order_count += 1u;
            box.focus_ordered = true;
            if (box.authored_id.value != 0u &&
                box.authored_id.value == impl->focus_request_id.value) {
                impl->focused_id = box.id;
                impl->focus_request_id = {};
            }
        }

        [[nodiscard]] auto make_signal(ContextImpl* impl, BoxNode const& box) -> Signal {
            Signal result = {};
            if (box.state == nullptr || box_disabled(box)) {
                return result;
            }

            if (box.state->last_frame != 0u) {
                bool const hovered = impl->hot_id.value == box.id.value;
                bool const mouse_down = impl->frame_desc.input.mouse_down[0u];
                bool const previous_mouse_down = impl->previous_input.mouse_down[0u];

                result.hovered = hovered;
                result.pressed_left = hovered && mouse_down && !previous_mouse_down;
                result.released_left =
                    previous_mouse_down && !mouse_down && impl->active_id.value == box.id.value;
                result.clicked_left = result.released_left && hovered;

                if (result.pressed_left) {
                    impl->active_id = box.id;
                    if (box.focusable) {
                        impl->focused_id = box.id;
                    }
                }
                if (result.released_left) {
                    impl->active_id = {};
                }
                result.active = impl->active_id.value == box.id.value;
            }
            result.focused = box.focusable && impl->focused_id.value == box.id.value;
            result.focus_gained =
                result.focused && impl->frame_start_focus_id.value != box.id.value;
            result.focus_lost = box.focusable && !result.focused &&
                                impl->frame_start_focus_id.value == box.id.value;
            return result;
        }

        [[nodiscard]] auto append_box(ContextImpl* impl,
                                      BoxKind kind,
                                      Id id_value,
                                      Id authored_id,
                                      StrRef text,
                                      BoxDesc const& desc,
                                      bool interactive,
                                      bool focusable) -> size_t {
            ASSERT(impl != nullptr);
            ASSERT(impl->building);
            ASSERT(impl->box_count < impl->box_capacity);

            size_t const parent_index = top_parent_index(impl);
            BoxNode& parent = impl->boxes[parent_index];
            size_t const box_index = impl->box_count;
            BoxNode* const box = impl->boxes + box_index;
            *box = {};
            box->first_child = INVALID_INDEX;
            box->last_child = INVALID_INDEX;
            box->next_sibling = INVALID_INDEX;
            box->kind = kind;
            box->parent_index = parent_index;
            box->parent_id = parent.id;
            box->depth = parent.depth + 1u;
            box->text = copy_frame_str(impl, text);
            box->debug_name = copy_frame_str(impl, desc.debug_name);
            box->layout = desc.layout;
            box->style = desc.style;
            box->flags = parent.flags | desc.flags;
            box->id = id_value;
            box->authored_id = authored_id;
            box->id_source = authored_id.value != 0u ? BoxIdSource::EXPLICIT
                             : !text.empty()         ? BoxIdSource::TEXT
                                                     : BoxIdSource::STRUCTURAL;
            box->stable_id = parent.stable_id && box->id_source == BoxIdSource::EXPLICIT;
            box->interactive = interactive;
            box->focusable = focusable;
            box->state = state_entry(impl, box->id);

            for (size_t child = parent.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (impl->boxes[child].id.value == box->id.value) {
                    box->duplicate_id = true;
                    box->stable_id = false;
                    DEBUG_ASSERT_MSG(
                        false, "duplicate sibling UI id; use explicit gui::id or an id scope");
                    break;
                }
            }

            if (parent.last_child != INVALID_INDEX) {
                impl->boxes[parent.last_child].next_sibling = box_index;
            } else {
                parent.first_child = box_index;
            }
            parent.last_child = box_index;
            parent.child_count += 1u;
            impl->box_count += 1u;

            add_focus_order(impl, *box);
            if (interactive) {
                box->signal = make_signal(impl, *box);
            }

            return box_index;
        }

        auto resolve_styles(ContextImpl* impl, size_t index, StyleDesc const& parent_style)
            -> void {
            BoxNode& box = impl->boxes[index];
            StyleDesc resolved = {};
            resolved.foreground = parent_style.foreground;
            resolved.font = parent_style.font;
            resolved.font_size = parent_style.font_size;
            resolved.opacity = parent_style.opacity;
            if (index == 0u) {
                merge_style(resolved, impl->theme.root);
            }

            StyleRole const role = box.style.role == StyleRole::AUTO
                                       ? impl->theme.kinds[static_cast<size_t>(box.kind)].role
                                       : box.style.role;
            StyleStateFlags const state = box_style_state(box);
            if (role_styled(role)) {
                merge_theme_style(resolved, impl->theme.roles[static_cast<size_t>(role)], state);
            }
            merge_theme_style(
                resolved, impl->theme.kinds[static_cast<size_t>(box.kind)].style, state);
            merge_style(resolved, box.style);
            if (resolved.radius < 0.0f) {
                resolved.radius = 0.0f;
            }
            if (!color_set(resolved.foreground)) {
                resolved.foreground = rgb(255, 255, 255);
            }
            if (resolved.font_size <= 0.0f) {
                resolved.font_size = 16.0f;
            }
            resolved.role = role;
            box.resolved_style = resolved;

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                resolve_styles(impl, child, resolved);
            }
        }

        auto apply_min_max(BoxNode const& box, Vec2& size) -> void {
            if (box.layout.min_width.kind == SizeKind::PIXELS) {
                size.x = std::max(size.x, box.layout.min_width.value);
            }
            if (box.layout.min_height.kind == SizeKind::PIXELS) {
                size.y = std::max(size.y, box.layout.min_height.value);
            }
            if (box.layout.max_width.kind == SizeKind::PIXELS) {
                size.x = std::min(size.x, box.layout.max_width.value);
            }
            if (box.layout.max_height.kind == SizeKind::PIXELS) {
                size.y = std::min(size.y, box.layout.max_height.value);
            }
        }

        [[nodiscard]] auto table_span(size_t value) -> size_t {
            return std::max(value, static_cast<size_t>(1u));
        }

        auto count_table(ContextImpl const* impl,
                         size_t table_index,
                         size_t& out_rows,
                         size_t& out_cells,
                         size_t& out_column_capacity) -> void {
            out_rows = 0u;
            out_cells = 0u;
            out_column_capacity = 0u;
            for (size_t row_index = impl->boxes[table_index].first_child;
                 row_index != INVALID_INDEX;
                 row_index = impl->boxes[row_index].next_sibling) {
                BoxNode const& row = impl->boxes[row_index];
                if (!table_row_box(row.kind)) {
                    continue;
                }
                out_rows += 1u;
                for (size_t cell_index = row.first_child; cell_index != INVALID_INDEX;
                     cell_index = impl->boxes[cell_index].next_sibling) {
                    BoxNode const& cell = impl->boxes[cell_index];
                    if (table_cell_box(cell.kind)) {
                        out_cells += 1u;
                        out_column_capacity += table_span(cell.table_column_span);
                    }
                }
            }
        }

        auto init_table_layout(Arena& arena,
                               ContextImpl const* impl,
                               size_t table_index,
                               TableLayout& layout) -> void {
            size_t rows = 0u;
            size_t cells = 0u;
            size_t columns = 0u;
            count_table(impl, table_index, rows, cells, columns);
            layout.row_count = rows;
            layout.column_capacity = columns;
            layout.cell_count = cells;
            if (rows != 0u) {
                layout.rows = arena_alloc<float>(arena, rows);
                for (size_t index = 0u; index < rows; ++index) {
                    layout.rows[index] = 0.0f;
                }
            }
            if (columns != 0u) {
                layout.columns = arena_alloc<float>(arena, columns);
                for (size_t index = 0u; index < columns; ++index) {
                    layout.columns[index] = 0.0f;
                }
            }
            if (rows != 0u && columns != 0u) {
                size_t const slot_count = rows * columns;
                layout.occupied = arena_alloc<bool>(arena, slot_count);
                for (size_t index = 0u; index < slot_count; ++index) {
                    layout.occupied[index] = false;
                }
            }
            if (cells != 0u) {
                layout.cells = arena_alloc<TableCellPlacement>(arena, cells);
            }
        }

        auto build_table_layout(ContextImpl const* impl, size_t table_index, TableLayout& layout)
            -> void {
            if (layout.row_count == 0u) {
                return;
            }

            BoxNode const& table = impl->boxes[table_index];
            float const gap = table.layout.gap;
            size_t row = 0u;
            size_t placement_count = 0u;

            for (size_t row_index = table.first_child; row_index != INVALID_INDEX;
                 row_index = impl->boxes[row_index].next_sibling) {
                BoxNode const& row_box = impl->boxes[row_index];
                if (!table_row_box(row_box.kind)) {
                    continue;
                }

                if (row_box.layout.height.kind == SizeKind::PIXELS) {
                    layout.rows[row] = std::max(layout.rows[row], row_box.layout.height.value);
                } else if (row_box.first_child == INVALID_INDEX) {
                    layout.rows[row] = std::max(layout.rows[row], row_box.measured_size.y);
                }

                size_t column = 0u;
                for (size_t cell_index = row_box.first_child; cell_index != INVALID_INDEX;
                     cell_index = impl->boxes[cell_index].next_sibling) {
                    BoxNode const& cell = impl->boxes[cell_index];
                    if (!table_cell_box(cell.kind) || layout.column_capacity == 0u) {
                        continue;
                    }

                    while (column < layout.column_capacity &&
                           layout.occupied[row * layout.column_capacity + column]) {
                        column += 1u;
                    }
                    if (column >= layout.column_capacity) {
                        break;
                    }

                    size_t const column_span = std::min(
                        table_span(cell.table_column_span), layout.column_capacity - column);
                    size_t const row_span =
                        std::min(table_span(cell.table_row_span), layout.row_count - row);
                    for (size_t y = row; y < row + row_span; ++y) {
                        for (size_t x = column; x < column + column_span; ++x) {
                            layout.occupied[y * layout.column_capacity + x] = true;
                        }
                    }

                    float const outer_x =
                        cell.measured_size.x + inset_width(cell.layout.margin);
                    float const outer_y =
                        cell.measured_size.y + inset_height(cell.layout.margin);
                    float const column_width =
                        std::max(0.0f,
                                 outer_x - gap * static_cast<float>(column_span - 1u)) /
                        static_cast<float>(column_span);
                    float const row_height =
                        std::max(0.0f, outer_y - gap * static_cast<float>(row_span - 1u)) /
                        static_cast<float>(row_span);
                    for (size_t x = column; x < column + column_span; ++x) {
                        layout.columns[x] = std::max(layout.columns[x], column_width);
                    }
                    for (size_t y = row; y < row + row_span; ++y) {
                        layout.rows[y] = std::max(layout.rows[y], row_height);
                    }

                    layout.column_count = std::max(layout.column_count, column + column_span);
                    layout.cells[placement_count] = {cell_index, row, column, row_span, column_span};
                    placement_count += 1u;
                    column += column_span;
                }
                row += 1u;
            }

            layout.cell_count = placement_count;
        }

        [[nodiscard]] auto table_axis_size(float const* values, size_t count, float gap) -> float {
            float total = 0.0f;
            for (size_t index = 0u; index < count; ++index) {
                total += values[index];
            }
            if (count > 1u) {
                total += gap * static_cast<float>(count - 1u);
            }
            return total;
        }

        [[nodiscard]] auto measure_table(ContextImpl* impl, size_t index) -> Vec2 {
            BoxNode const& box = impl->boxes[index];
            ArenaTemp temp(impl->frame_arena);
            Arena* const arena = temp.arena();
            TableLayout table = {};
            init_table_layout(*arena, impl, index, table);
            build_table_layout(impl, index, table);

            float const content_x =
                table_axis_size(table.columns, table.column_count, box.layout.gap);
            float const content_y = table_axis_size(table.rows, table.row_count, box.layout.gap);
            Vec2 size = {};
            if (box.layout.width.kind == SizeKind::PIXELS) {
                size.x = box.layout.width.value;
            } else if (box.layout.width.kind != SizeKind::FILL) {
                size.x = content_x + inset_width(box.layout.padding);
            }
            if (box.layout.height.kind == SizeKind::PIXELS) {
                size.y = box.layout.height.value;
            } else if (box.layout.height.kind != SizeKind::FILL) {
                size.y = content_y + inset_height(box.layout.padding);
            }
            return size;
        }

        [[nodiscard]] auto measure_node(ContextImpl* impl, size_t index) -> Vec2 {
            BoxNode& box = impl->boxes[index];

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                BASE_UNUSED(measure_node(impl, child));
            }

            Vec2 size = {};
            Vec2 const text_dim = text_size(box);
            bool const is_leaf = box.first_child == INVALID_INDEX;
            float const widget_extra_x = box.kind == BoxKind::CHECKBOX       ? 24.0f
                                         : box.kind == BoxKind::TOGGLE       ? 44.0f
                                         : box.kind == BoxKind::SLIDER_FLOAT ? 160.0f
                                         : input_text_box(box.kind)          ? 160.0f
                                                                             : 0.0f;
            float const widget_min_y = box.kind == BoxKind::CHECKBOX ||
                                               box.kind == BoxKind::TOGGLE ||
                                               box.kind == BoxKind::SLIDER_FLOAT ||
                                               input_text_box(box.kind)
                                           ? 20.0f
                                           : 0.0f;

            if (box.kind == BoxKind::TABLE) {
                size = measure_table(impl, index);
                apply_min_max(box, size);
                box.measured_size = size;
                return size;
            }

            if (box.layout.width.kind == SizeKind::PIXELS) {
                size.x = box.layout.width.value;
            } else if (box.layout.width.kind == SizeKind::TEXT ||
                       (box.layout.width.kind == SizeKind::AUTO && is_leaf)) {
                size.x = text_dim.x + widget_extra_x + inset_width(box.layout.padding);
            }

            if (box.layout.height.kind == SizeKind::PIXELS) {
                size.y = box.layout.height.value;
            } else if (box.layout.height.kind == SizeKind::TEXT ||
                       (box.layout.height.kind == SizeKind::AUTO && is_leaf)) {
                size.y = std::max(text_dim.y, widget_min_y) + inset_height(box.layout.padding);
            }

            if (!is_leaf && (box.layout.width.kind == SizeKind::AUTO ||
                             box.layout.width.kind == SizeKind::CHILDREN ||
                             box.layout.height.kind == SizeKind::AUTO ||
                             box.layout.height.kind == SizeKind::CHILDREN)) {
                float total_x = 0.0f;
                float total_y = 0.0f;
                float max_x = 0.0f;
                float max_y = 0.0f;
                size_t child_count = 0u;

                for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                     child_index = impl->boxes[child_index].next_sibling) {
                    BoxNode const& child = impl->boxes[child_index];
                    if (floating_box(child.kind)) {
                        continue;
                    }
                    float const child_outer_x =
                        child.measured_size.x + inset_width(child.layout.margin);
                    float const child_outer_y =
                        child.measured_size.y + inset_height(child.layout.margin);
                    total_x += child_outer_x;
                    total_y += child_outer_y;
                    max_x = std::max(max_x, child_outer_x);
                    max_y = std::max(max_y, child_outer_y);
                    child_count += 1u;
                }

                float const gaps =
                    child_count > 1u ? box.layout.gap * static_cast<float>(child_count - 1u) : 0.0f;
                if (box.kind == BoxKind::ROW || box.kind == BoxKind::TAB_BAR ||
                    box.kind == BoxKind::TAB) {
                    if (box.layout.width.kind == SizeKind::AUTO ||
                        box.layout.width.kind == SizeKind::CHILDREN) {
                        size.x = total_x + gaps + inset_width(box.layout.padding);
                    }
                    if (box.layout.height.kind == SizeKind::AUTO ||
                        box.layout.height.kind == SizeKind::CHILDREN) {
                        size.y = max_y + inset_height(box.layout.padding);
                    }
                } else if (box.kind == BoxKind::COLUMN || box.kind == BoxKind::POPUP ||
                           box.kind == BoxKind::LIST || box.kind == BoxKind::SCROLL_PANEL ||
                           box.kind == BoxKind::TAB_VIEW || box.kind == BoxKind::TAB_BODY ||
                           box.kind == BoxKind::ROOT) {
                    if (box.layout.width.kind == SizeKind::AUTO ||
                        box.layout.width.kind == SizeKind::CHILDREN) {
                        size.x = max_x + inset_width(box.layout.padding);
                    }
                    if (box.layout.height.kind == SizeKind::AUTO ||
                        box.layout.height.kind == SizeKind::CHILDREN) {
                        size.y = total_y + gaps + inset_height(box.layout.padding);
                    }
                } else {
                    if (box.layout.width.kind == SizeKind::AUTO ||
                        box.layout.width.kind == SizeKind::CHILDREN) {
                        size.x = max_x + inset_width(box.layout.padding);
                    }
                    if (box.layout.height.kind == SizeKind::AUTO ||
                        box.layout.height.kind == SizeKind::CHILDREN) {
                        size.y = max_y + inset_height(box.layout.padding);
                    }
                }
            }

            apply_min_max(box, size);
            box.measured_size = size;
            return size;
        }

        [[nodiscard]] auto child_axis_size(BoxNode const& child, Axis axis) -> float {
            Size const size = axis == Axis::X ? child.layout.width : child.layout.height;
            return size.kind == SizeKind::PIXELS
                       ? size.value
                       : (axis == Axis::X ? child.measured_size.x : child.measured_size.y);
        }

        [[nodiscard]] auto child_margin_main(BoxNode const& child, Axis axis) -> float {
            return axis == Axis::X ? child.layout.margin.left + child.layout.margin.right
                                   : child.layout.margin.top + child.layout.margin.bottom;
        }

        [[nodiscard]] auto child_margin_cross(BoxNode const& child, Axis axis) -> float {
            return axis == Axis::X ? child.layout.margin.top + child.layout.margin.bottom
                                   : child.layout.margin.left + child.layout.margin.right;
        }

        [[nodiscard]] auto
        stack_content_main_size(ContextImpl const* impl, BoxNode const& box, Axis axis) -> float {
            float total = 0.0f;
            size_t child_count = 0u;
            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode const& child = impl->boxes[child_index];
                if (floating_box(child.kind)) {
                    continue;
                }
                total += child_axis_size(child, axis) + child_margin_main(child, axis);
                child_count += 1u;
            }
            if (child_count > 1u) {
                total += box.layout.gap * static_cast<float>(child_count - 1u);
            }
            return total;
        }

        [[nodiscard]] auto align_offset(Align align, float available, float size) -> float {
            float const free_space = available - size;
            switch (align) {
            case Align::CENTER:
                return free_space * 0.5f;
            case Align::END:
                return free_space;
            case Align::START:
            case Align::STRETCH:
                return 0.0f;
            }
            return 0.0f;
        }

        auto clear_scroll_requests(StateEntry* state) -> void {
            state->scroll_request_set = false;
            state->scroll_request_end = false;
            state->scroll_request_index_set = false;
        }

        auto consume_scroll_request(StateEntry* state, float max_y) -> void {
            if (state->scroll_request_end) {
                state->scroll_y = max_y;
            } else if (state->scroll_request_set) {
                state->scroll_y = state->scroll_request_y;
            }
            clear_scroll_requests(state);
        }

        [[nodiscard]] auto list_reveal_scroll_y(StateEntry const* state,
                                                size_t item_count,
                                                float item_height,
                                                float viewport_height) -> float {
            if (item_count == 0u) {
                return 0.0f;
            }

            size_t const index = std::min(state->scroll_request_index, item_count - 1u);
            float const item_start = static_cast<float>(index) * item_height;
            float const item_end = item_start + item_height;
            switch (state->scroll_request_reveal) {
            case ScrollReveal::KEEP_VISIBLE:
                if (item_start < state->scroll_y) {
                    return item_start;
                }
                if (item_end > state->scroll_y + viewport_height) {
                    return item_end - viewport_height;
                }
                return state->scroll_y;
            case ScrollReveal::START:
                return item_start;
            case ScrollReveal::CENTER:
                return item_start + item_height * 0.5f - viewport_height * 0.5f;
            case ScrollReveal::END:
                return item_end - viewport_height;
            }
            return state->scroll_y;
        }

        auto consume_list_scroll_request(StateEntry* state,
                                         size_t item_count,
                                         float item_height,
                                         float viewport_height,
                                         float max_y) -> void {
            if (state->scroll_request_index_set) {
                state->scroll_y =
                    list_reveal_scroll_y(state, item_count, item_height, viewport_height);
                clear_scroll_requests(state);
            } else {
                consume_scroll_request(state, max_y);
            }
        }

        auto clamp_multiline_cursor_to_scroll(BoxNode& box) -> void;

        auto update_scroll_metrics(
            ContextImpl* impl,
            StateEntry* state,
            Rect rect,
            float radius,
            float content_height,
            bool apply_input,
            bool consume_request
        ) -> void {
            if (state == nullptr) {
                return;
            }

            float const viewport_height = std::max(0.0f, rect_height(rect));
            float const scroll_content_height = std::max(0.0f, content_height);
            float const max_y = std::max(0.0f, scroll_content_height - viewport_height);
            if (apply_input) {
                apply_scroll_delta(impl, state, rect);
            }
            if (consume_request) {
                consume_scroll_request(state, max_y);
            }
            state->scroll_y = std::clamp(state->scroll_y, 0.0f, max_y);
            apply_scrollbar_input(
                impl, state, rect, radius, viewport_height, scroll_content_height, max_y
            );
            state->scroll_y = std::clamp(state->scroll_y, 0.0f, max_y);
            state->scroll_max_y = max_y;
            state->scroll_viewport_height = viewport_height;
            state->scroll_content_height = scroll_content_height;
            state->scroll_valid = true;
            state->rect = rect;
            state->last_frame = impl->frame_index;
        }

        auto layout_children(ContextImpl* impl, size_t index, Axis axis) -> void;
        auto layout_overlay(ContextImpl* impl, size_t index) -> void;
        auto layout_table(ContextImpl* impl, size_t index) -> void;
        auto layout_floating_children(ContextImpl* impl, size_t index) -> void;

        auto layout_node(ContextImpl* impl, size_t index, Rect rect) -> void {
            BoxNode& box = impl->boxes[index];
            box.rect = rect;
            if (box.kind == BoxKind::SCROLL_PANEL) {
                float const content_height =
                    stack_content_main_size(impl, box, Axis::Y) + inset_height(box.layout.padding);
                update_scroll_metrics(
                    impl,
                    box.scroll_state,
                    rect,
                    box.resolved_style.radius,
                    content_height,
                    true,
                    true
                );
                if (box.scroll_state != nullptr) {
                    box.scroll_offset_y = -box.scroll_state->scroll_y;
                }
            } else if ((box.kind == BoxKind::SELECTABLE_LABEL ||
                        multiline_input_text_box(box.kind)) &&
                       box.layout.scroll_y) {
                float content_height = rect_height(rect);
                if (text_multiline(box.text) || multiline_input_text_box(box.kind)) {
                    content_height = text_size(box).y + inset_height(box.layout.padding);
                }
                InputState const& input = impl->frame_desc.input;
                bool const wheel_scroll =
                    input.scroll_delta_y != 0.0f && rect_contains(rect, input.mouse_pos);
                float const scroll_y_before =
                    box.scroll_state != nullptr ? box.scroll_state->scroll_y : 0.0f;
                update_scroll_metrics(
                    impl,
                    box.scroll_state,
                    rect,
                    box.resolved_style.radius,
                    content_height,
                    true,
                    true
                );
                bool const scrollbar_scroll =
                    box.scroll_state != nullptr &&
                    impl->active_scroll_id.value == box.scroll_state->id.value &&
                    input.mouse_down[0u];
                bool const input_scroll = wheel_scroll || scrollbar_scroll;
                if (input_scroll && box.scroll_state != nullptr &&
                    box.scroll_state->scroll_y != scroll_y_before &&
                    multiline_input_text_box(box.kind)) {
                    clamp_multiline_cursor_to_scroll(box);
                }
            } else if (box.kind == BoxKind::LIST) {
                update_scroll_metrics(
                    impl,
                    box.scroll_state,
                    rect,
                    box.resolved_style.radius,
                    box.scroll_content_height,
                    false,
                    false
                );
            }
            if (box.first_child == INVALID_INDEX) {
                return;
            }

            if (box.kind == BoxKind::TABLE) {
                layout_table(impl, index);
            } else if (box.kind == BoxKind::ROW || box.kind == BoxKind::TAB_BAR ||
                       box.kind == BoxKind::TAB) {
                layout_children(impl, index, Axis::X);
            } else if (box.kind == BoxKind::COLUMN || box.kind == BoxKind::POPUP ||
                       box.kind == BoxKind::TAB_VIEW || box.kind == BoxKind::TAB_BODY ||
                       box.kind == BoxKind::ROOT || box.kind == BoxKind::SCROLL_PANEL ||
                       box.kind == BoxKind::LIST) {
                layout_children(impl, index, Axis::Y);
            } else {
                layout_overlay(impl, index);
            }
            layout_floating_children(impl, index);
        }

        auto layout_overlay(ContextImpl* impl, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            Rect const content = content_rect(box.rect, box.layout.padding);
            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode& child = impl->boxes[child_index];
                if (floating_box(child.kind)) {
                    continue;
                }
                float const available_x = std::max(0.0f,
                                                   rect_width(content) - child.layout.margin.left -
                                                       child.layout.margin.right);
                float const available_y = std::max(0.0f,
                                                   rect_height(content) - child.layout.margin.top -
                                                       child.layout.margin.bottom);

                Vec2 child_size = child.measured_size;
                if (child.layout.width.kind == SizeKind::PIXELS) {
                    child_size.x = child.layout.width.value;
                } else if (child.layout.width.kind == SizeKind::FILL) {
                    child_size.x = available_x;
                }
                if (child.layout.height.kind == SizeKind::PIXELS) {
                    child_size.y = child.layout.height.value;
                } else if (child.layout.height.kind == SizeKind::FILL) {
                    child_size.y = available_y;
                }
                apply_min_max(child, child_size);

                float const x = content.min.x + child.layout.margin.left +
                                align_offset(box.layout.align_x, available_x, child_size.x);
                float const y = content.min.y + child.layout.margin.top +
                                align_offset(box.layout.align_y, available_y, child_size.y);
                layout_node(impl, child_index, {{x, y}, {x + child_size.x, y + child_size.y}});
            }
        }

        auto layout_floating_children(ContextImpl* impl, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            Rect const content = content_rect(box.rect, box.layout.padding);
            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode& child = impl->boxes[child_index];
                if (!floating_box(child.kind)) {
                    continue;
                }

                Rect const root_bounds = impl->boxes[0].rect;
                Rect const bounds = child.kind == BoxKind::MODAL ? root_bounds : content;
                float const available_x = std::max(0.0f,
                                                   rect_width(bounds) - child.layout.margin.left -
                                                       child.layout.margin.right);
                float const available_y = std::max(0.0f,
                                                   rect_height(bounds) - child.layout.margin.top -
                                                       child.layout.margin.bottom);

                Vec2 child_size = child.measured_size;
                if (child.kind == BoxKind::MODAL || child.layout.width.kind == SizeKind::FILL) {
                    child_size.x = available_x;
                } else if (child.layout.width.kind == SizeKind::PIXELS) {
                    child_size.x = child.layout.width.value;
                }
                if (child.kind == BoxKind::MODAL || child.layout.height.kind == SizeKind::FILL) {
                    child_size.y = available_y;
                } else if (child.layout.height.kind == SizeKind::PIXELS) {
                    child_size.y = child.layout.height.value;
                }
                apply_min_max(child, child_size);

                float const base_x = bounds.min.x + child.layout.margin.left +
                                     align_offset(box.layout.align_x, available_x, child_size.x);
                float const base_y = bounds.min.y + child.layout.margin.top +
                                     align_offset(box.layout.align_y, available_y, child_size.y);
                float x = base_x;
                float y = base_y;
                if (child.kind == BoxKind::POPUP && child.state != nullptr) {
                    StateEntry& state = *child.state;
                    if (child.signal.active && impl->frame_desc.input.mouse_down[0u] &&
                        impl->previous_input.mouse_down[0u]) {
                        state.floating_offset_x +=
                            impl->frame_desc.input.mouse_pos.x - impl->previous_input.mouse_pos.x;
                        state.floating_offset_y +=
                            impl->frame_desc.input.mouse_pos.y - impl->previous_input.mouse_pos.y;
                    }

                    x += state.floating_offset_x;
                    y += state.floating_offset_y;
                    float const max_x = std::max(root_bounds.min.x, root_bounds.max.x - child_size.x);
                    float const max_y = std::max(root_bounds.min.y, root_bounds.max.y - child_size.y);
                    x = std::clamp(x, root_bounds.min.x, max_x);
                    y = std::clamp(y, root_bounds.min.y, max_y);
                    state.floating_offset_x = x - base_x;
                    state.floating_offset_y = y - base_y;
                }
                layout_node(impl, child_index, {{x, y}, {x + child_size.x, y + child_size.y}});
            }
        }

        auto expand_table_axis(float* values, size_t count, float available, float gap) -> void {
            if (count == 0u) {
                return;
            }
            float const current = table_axis_size(values, count, gap);
            float const extra = std::max(0.0f, available - current) / static_cast<float>(count);
            for (size_t index = 0u; index < count; ++index) {
                values[index] += extra;
            }
        }

        [[nodiscard]] auto table_axis_offset(float const* values, size_t index, float gap)
            -> float {
            float result = 0.0f;
            for (size_t cursor = 0u; cursor < index; ++cursor) {
                result += values[cursor] + gap;
            }
            return result;
        }

        [[nodiscard]] auto table_axis_span(float const* values,
                                           size_t start,
                                           size_t count,
                                           float gap) -> float {
            float result = 0.0f;
            for (size_t index = 0u; index < count; ++index) {
                result += values[start + index];
            }
            if (count > 1u) {
                result += gap * static_cast<float>(count - 1u);
            }
            return result;
        }

        auto layout_table(ContextImpl* impl, size_t index) -> void {
            BoxNode& box = impl->boxes[index];
            ArenaTemp temp(impl->frame_arena);
            Arena* const arena = temp.arena();
            TableLayout table = {};
            init_table_layout(*arena, impl, index, table);
            build_table_layout(impl, index, table);

            Rect const content = content_rect(box.rect, box.layout.padding);
            float const gap = box.layout.gap;
            expand_table_axis(table.columns, table.column_count, rect_width(content), gap);
            expand_table_axis(table.rows, table.row_count, rect_height(content), gap);

            size_t row = 0u;
            for (size_t row_index = box.first_child; row_index != INVALID_INDEX;
                 row_index = impl->boxes[row_index].next_sibling) {
                BoxNode& row_box = impl->boxes[row_index];
                if (!table_row_box(row_box.kind)) {
                    continue;
                }
                float const y = content.min.y + table_axis_offset(table.rows, row, gap);
                float const height = table.rows != nullptr ? table.rows[row] : 0.0f;
                row_box.rect = {{content.min.x, y}, {content.max.x, y + height}};
                row += 1u;
            }

            for (size_t placement_index = 0u; placement_index < table.cell_count;
                 ++placement_index) {
                TableCellPlacement const& cell = table.cells[placement_index];
                BoxNode& box_cell = impl->boxes[cell.box_index];
                float const x =
                    content.min.x + table_axis_offset(table.columns, cell.column, gap);
                float const y = content.min.y + table_axis_offset(table.rows, cell.row, gap);
                float const width =
                    table_axis_span(table.columns, cell.column, cell.column_span, gap);
                float const height = table_axis_span(table.rows, cell.row, cell.row_span, gap);
                Rect cell_rect = {{x + box_cell.layout.margin.left, y + box_cell.layout.margin.top},
                                  {x + width - box_cell.layout.margin.right,
                                   y + height - box_cell.layout.margin.bottom}};
                if (cell_rect.max.x < cell_rect.min.x) {
                    cell_rect.max.x = cell_rect.min.x;
                }
                if (cell_rect.max.y < cell_rect.min.y) {
                    cell_rect.max.y = cell_rect.min.y;
                }
                layout_node(impl, cell.box_index, cell_rect);
            }
        }

        auto layout_children(ContextImpl* impl, size_t index, Axis axis) -> void {
            BoxNode& box = impl->boxes[index];
            Rect const content = content_rect(box.rect, box.layout.padding);
            float const content_main = axis == Axis::X ? rect_width(content) : rect_height(content);
            float const content_cross =
                axis == Axis::X ? rect_height(content) : rect_width(content);
            size_t child_count = 0u;
            float fixed = 0.0f;
            float fill_weight = 0.0f;

            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode const& child = impl->boxes[child_index];
                if (floating_box(child.kind)) {
                    continue;
                }
                Size const main_size = axis == Axis::X ? child.layout.width : child.layout.height;
                if (main_size.kind == SizeKind::FILL) {
                    fill_weight += std::max(main_size.value, 0.0f);
                    fixed += child_margin_main(child, axis);
                } else {
                    fixed += child_axis_size(child, axis) + child_margin_main(child, axis);
                }
                child_count += 1u;
            }

            float const gaps =
                child_count > 1u ? box.layout.gap * static_cast<float>(child_count - 1u) : 0.0f;
            float const fill_space = std::max(0.0f, content_main - fixed - gaps);
            float const run_main = fixed + gaps + (fill_weight > 0.0f ? fill_space : 0.0f);
            Align const main_align = axis == Axis::X ? box.layout.align_x : box.layout.align_y;
            float const main_offset =
                fill_weight > 0.0f ? 0.0f : align_offset(main_align, content_main, run_main);
            float cursor = (axis == Axis::X ? content.min.x : content.min.y) + main_offset;
            if (axis == Axis::Y) {
                cursor += box.scroll_offset_y;
            }
            Align const cross_align = axis == Axis::X ? box.layout.align_y : box.layout.align_x;

            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode& child = impl->boxes[child_index];
                if (floating_box(child.kind)) {
                    continue;
                }
                Size const main_size = axis == Axis::X ? child.layout.width : child.layout.height;
                Size const cross_size = axis == Axis::X ? child.layout.height : child.layout.width;
                float main = child_axis_size(child, axis);
                if (main_size.kind == SizeKind::FILL && fill_weight > 0.0f) {
                    main = fill_space * (std::max(main_size.value, 0.0f) / fill_weight);
                }

                float cross = axis == Axis::X ? child.measured_size.y : child.measured_size.x;
                bool const stretch =
                    cross_size.kind == SizeKind::FILL || cross_align == Align::STRETCH;
                if (cross_size.kind == SizeKind::PIXELS) {
                    cross = cross_size.value;
                } else if (stretch) {
                    cross = std::max(0.0f, content_cross - child_margin_cross(child, axis));
                }

                Vec2 child_size = axis == Axis::X ? Vec2{main, cross} : Vec2{cross, main};
                apply_min_max(child, child_size);
                if (axis == Axis::X) {
                    float const cross_available =
                        content_cross - child.layout.margin.top - child.layout.margin.bottom;
                    float const cross_start =
                        content.min.y + child.layout.margin.top +
                        align_offset(cross_align, cross_available, child_size.y);
                    Rect child_rect = {{cursor + child.layout.margin.left, cross_start},
                                       {cursor + child.layout.margin.left + child_size.x,
                                        cross_start + child_size.y}};
                    layout_node(impl, child_index, child_rect);
                    cursor += child.layout.margin.left + child_size.x + child.layout.margin.right +
                              box.layout.gap;
                } else {
                    float const cross_available =
                        content_cross - child.layout.margin.left - child.layout.margin.right;
                    float const cross_start =
                        content.min.x + child.layout.margin.left +
                        align_offset(cross_align, cross_available, child_size.x);
                    Rect child_rect = {{cross_start, cursor + child.layout.margin.top},
                                       {cross_start + child_size.x,
                                        cursor + child.layout.margin.top + child_size.y}};
                    layout_node(impl, child_index, child_rect);
                    cursor += child.layout.margin.top + child_size.y + child.layout.margin.bottom +
                              box.layout.gap;
                }
            }
        }

        auto publish_infos(ContextImpl* impl) -> void {
            for (size_t index = 0u; index < impl->box_count; ++index) {
                BoxNode const& box = impl->boxes[index];
                BoxInfo& info = impl->infos[index];
                info = {};
                info.id = box.id;
                info.parent_id = box.parent_id;
                info.authored_id = box.authored_id;
                info.id_source = box.id_source;
                info.kind = box.kind;
                info.text = box.text;
                info.debug_name = box.debug_name;
                info.rect = box.rect;
                info.depth = box.depth;
                info.flags = box.flags;
                info.layout = box.layout;
                info.style = box.resolved_style;
                info.duplicate_id = box.duplicate_id;
                info.stable_id = box.stable_id && !box.duplicate_id;
                if (box.state != nullptr) {
                    box.state->rect = box.rect;
                    box.state->font = box.resolved_style.font;
                    box.state->font_size = box.resolved_style.font_size;
                    box.state->last_frame = impl->frame_index;
                }
            }
        }

        auto apply_button_activation(ContextImpl const* impl, BoxNode& box) -> Signal {
            Signal signal = box.signal;
            signal.activated =
                signal.clicked_left || (signal.focused && (key_pressed(impl, Key::ENTER, false) ||
                                                           key_pressed(impl, Key::SPACE, false)));
            box.signal = signal;
            return signal;
        }

        [[nodiscard]] auto tab_flag(TabFlags flags, TabFlags flag) -> bool {
            return (flags & flag) != 0u;
        }

        [[nodiscard]] auto tab_count(TabViewDesc const& desc) -> size_t {
            size_t const count = desc.tab_count != nullptr ? *desc.tab_count : desc.tabs.size();
            return std::min(count, desc.tabs.size());
        }

        [[nodiscard]] auto clamp_tab_index(size_t index, size_t count) -> size_t {
            return count == 0u ? TAB_INDEX_NONE : std::min(index, count - 1u);
        }

        [[nodiscard]] auto selected_tab_index(TabViewDesc const& desc, size_t count) -> size_t {
            size_t const selected = desc.selected_index != nullptr ? *desc.selected_index : 0u;
            return clamp_tab_index(selected, count);
        }

        [[nodiscard]] auto tab_child_id(Id parent, uint64_t salt) -> Id {
            return {hash_combine(parent.value, salt)};
        }

        [[nodiscard]] auto ensure_tab_id(TabViewDesc const& desc, Id view_id, size_t index) -> Id {
            TabItem& tab = desc.tabs[index];
            if (tab.id.value == 0u) {
                tab.id = tab_child_id(view_id, index + 1u);
            }
            return tab.id;
        }

        auto move_tab(Slice<TabItem> tabs, size_t from, size_t to) -> void {
            if (from == to || from >= tabs.size() || to >= tabs.size()) {
                return;
            }

            TabItem const item = tabs[from];
            if (from < to) {
                for (size_t index = from; index < to; ++index) {
                    tabs[index] = tabs[index + 1u];
                }
            } else {
                for (size_t index = from; index > to; --index) {
                    tabs[index] = tabs[index - 1u];
                }
            }
            tabs[to] = item;
        }

        auto close_tab(Slice<TabItem> tabs, size_t* count, size_t index) -> void {
            if (count == nullptr || index >= *count || *count > tabs.size()) {
                return;
            }
            for (size_t next = index + 1u; next < *count; ++next) {
                tabs[next - 1u] = tabs[next];
            }
            *count -= 1u;
            tabs[*count] = {};
        }

        [[nodiscard]] auto selected_after_close(size_t selected, size_t closed, size_t count)
            -> size_t {
            if (count == 0u) {
                return TAB_INDEX_NONE;
            }
            if (selected == closed) {
                return std::min(closed, count - 1u);
            }
            return selected > closed ? selected - 1u : selected;
        }

        [[nodiscard]] auto selected_after_move(size_t selected, size_t from, size_t to) -> size_t {
            if (selected == from) {
                return to;
            }
            if (from < to && selected > from && selected <= to) {
                return selected - 1u;
            }
            if (to < from && selected >= to && selected < from) {
                return selected + 1u;
            }
            return selected;
        }

        [[nodiscard]] auto drag_moves_tab(
            ContextImpl const* impl,
            Id view_id,
            TabViewDesc const& desc,
            size_t index,
            size_t count,
            size_t& out_to
        ) -> bool {
            if (!tab_flag(desc.flags, TAB_FLAG_MOVABLE) || !impl->frame_desc.input.mouse_down[0u] ||
                !impl->previous_input.mouse_down[0u]) {
                return false;
            }

            float const mouse_x = impl->frame_desc.input.mouse_pos.x;
            if (index > 0u) {
                StateEntry const* const state = find_state_entry(
                    impl,
                    explicit_id(
                        impl,
                        top_parent_index(impl),
                        BoxKind::TAB,
                        ensure_tab_id(desc, view_id, index - 1u)
                    )
                );
                if (state != nullptr && state->last_frame != 0u &&
                    mouse_x < (state->rect.min.x + state->rect.max.x) * 0.5f) {
                    out_to = index - 1u;
                    return true;
                }
            }
            if (index + 1u < count) {
                StateEntry const* const state = find_state_entry(
                    impl,
                    explicit_id(
                        impl,
                        top_parent_index(impl),
                        BoxKind::TAB,
                        ensure_tab_id(desc, view_id, index + 1u)
                    )
                );
                if (state != nullptr && state->last_frame != 0u &&
                    mouse_x > (state->rect.min.x + state->rect.max.x) * 0.5f) {
                    out_to = index + 1u;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto clamp_text_selection(TextSelection selection, size_t text_size)
            -> TextSelection {
            selection.start = std::min(selection.start, text_size);
            selection.end = std::min(selection.end, text_size);
            return selection;
        }

        [[nodiscard]] auto ordered_text_selection(TextSelection selection) -> TextSelection {
            return selection.start <= selection.end ? selection : TextSelection{selection.end,
                                                                                selection.start};
        }

        [[nodiscard]] auto utf8_trailing_byte(char value) -> bool {
            return (static_cast<uint8_t>(value) & 0xc0u) == 0x80u;
        }

        [[nodiscard]] auto next_text_offset(StrRef text, size_t offset) -> size_t {
            offset = std::min(offset + 1u, text.size());
            while (offset < text.size() && utf8_trailing_byte(text[offset])) {
                offset += 1u;
            }
            return offset;
        }

        [[nodiscard]] auto previous_text_offset(StrRef text, size_t offset) -> size_t {
            offset = std::min(offset, text.size());
            if (offset == 0u) {
                return 0u;
            }
            offset -= 1u;
            while (offset > 0u && utf8_trailing_byte(text[offset])) {
                offset -= 1u;
            }
            return offset;
        }

        [[nodiscard]] auto text_word_char(StrRef text, size_t offset) -> bool {
            uint8_t const c = static_cast<uint8_t>(text[offset]);
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_' || c >= 0x80u;
        }

        [[nodiscard]] auto previous_word_offset(StrRef text, size_t offset) -> size_t {
            offset = std::min(offset, text.size());
            while (offset > 0u) {
                size_t const previous = previous_text_offset(text, offset);
                if (text_word_char(text, previous)) {
                    break;
                }
                offset = previous;
            }
            while (offset > 0u) {
                size_t const previous = previous_text_offset(text, offset);
                if (!text_word_char(text, previous)) {
                    break;
                }
                offset = previous;
            }
            return offset;
        }

        [[nodiscard]] auto next_word_offset(StrRef text, size_t offset) -> size_t {
            offset = std::min(offset, text.size());
            while (offset < text.size() && text_word_char(text, offset)) {
                offset = next_text_offset(text, offset);
            }
            while (offset < text.size() && !text_word_char(text, offset)) {
                offset = next_text_offset(text, offset);
            }
            return offset;
        }

        [[nodiscard]] auto text_cursor_key_offset(StrRef text, size_t cursor, KeyEvent const& event)
            -> size_t {
            bool const word = (event.mods & KEY_MOD_CTRL) != 0u;
            if (event.key == Key::RIGHT) {
                return word ? next_word_offset(text, cursor) : next_text_offset(text, cursor);
            }
            return word ? previous_word_offset(text, cursor) : previous_text_offset(text, cursor);
        }

        [[nodiscard]] auto text_selection_key_event(KeyEvent const& event) -> bool {
            return (event.mods & KEY_MOD_SHIFT) != 0u &&
                   (event.kind == KeyEventKind::PRESS || event.kind == KeyEventKind::REPEAT) &&
                   (event.key == Key::LEFT || event.key == Key::RIGHT);
        }

        [[nodiscard]] auto
        apply_text_selection_key_event(StrRef text, TextSelection selection, KeyEvent const& event)
            -> TextSelection {
            selection = ordered_text_selection(clamp_text_selection(selection, text.size()));
            if (!text_selection_key_event(event)) {
                return selection;
            }

            if (event.key == Key::RIGHT) {
                selection.end = text_cursor_key_offset(text, selection.end, event);
            } else if (selection.start != selection.end) {
                selection.end =
                    std::max(text_cursor_key_offset(text, selection.end, event), selection.start);
            }
            return selection;
        }

        [[nodiscard]] auto apply_text_cursor_selection_key_event(
            StrRef text, TextSelection selection, size_t& cursor, KeyEvent const& event
        ) -> TextSelection {
            selection = ordered_text_selection(clamp_text_selection(selection, text.size()));
            cursor = std::min(cursor, text.size());
            if (!text_selection_key_event(event)) {
                return selection;
            }

            size_t const anchor = selection.start == selection.end
                                      ? cursor
                                      : (cursor == selection.start ? selection.end
                                                                   : selection.start);
            cursor = text_cursor_key_offset(text, cursor, event);
            return ordered_text_selection({anchor, cursor});
        }

        [[nodiscard]] auto text_word_selection(StrRef text, size_t cursor) -> TextSelection {
            if (text.empty()) {
                return {};
            }

            cursor = std::min(cursor, text.size());
            size_t offset = cursor;
            if (offset == text.size() ||
                (offset > 0u && !text_word_char(text, offset) &&
                 text_word_char(text, previous_text_offset(text, offset)))) {
                offset = previous_text_offset(text, offset);
            }
            if (!text_word_char(text, offset)) {
                return {cursor, cursor};
            }

            size_t start = offset;
            while (start > 0u) {
                size_t const previous = previous_text_offset(text, start);
                if (!text_word_char(text, previous)) {
                    break;
                }
                start = previous;
            }

            size_t end = next_text_offset(text, offset);
            while (end < text.size() && text_word_char(text, end)) {
                end = next_text_offset(text, end);
            }
            return {start, end};
        }

        auto
        copy_selected_text(ContextImpl const* impl, StrRef text, TextSelection selection) -> void {
            if (impl->set_clipboard_text == nullptr || selection.start == selection.end) {
                return;
            }
            impl->set_clipboard_text(
                impl->clipboard_user_data,
                text.substr(selection.start, selection.end - selection.start)
            );
        }

        [[nodiscard]] auto text_line_index_from_x(BoxNode const& box,
                                                  TextLine const& line,
                                                  float text_x) -> size_t {
            if (line.text.empty()) {
                return line.start;
            }
            if (text_x <= 0.0f) {
                return line.start;
            }

            size_t previous = 0u;
            float previous_x = 0.0f;
            for (size_t offset = next_text_offset(line.text, 0u); offset <= line.text.size();
                 offset = next_text_offset(line.text, offset)) {
                float const advance = text_advance(box, line.text.prefix(offset));
                if (text_x < (previous_x + advance) * 0.5f) {
                    return line.start + previous;
                }
                if (offset == line.text.size()) {
                    break;
                }
                previous = offset;
                previous_x = advance;
            }
            return line.end;
        }

        [[nodiscard]] auto vertical_text_cursor_offset(BoxNode const& box,
                                                       StrRef text,
                                                       size_t cursor,
                                                       Key key) -> size_t {
            cursor = std::min(cursor, text.size());
            size_t offset = 0u;
            TextLine previous = {};
            bool has_previous = false;
            TextLine line = {};
            while (next_text_line(text, offset, line)) {
                if (cursor <= line.end) {
                    TextLine target = {};
                    if (key == Key::UP) {
                        if (!has_previous) {
                            return cursor;
                        }
                        target = previous;
                    } else if (!next_text_line(text, offset, target)) {
                        return cursor;
                    }
                    size_t const line_cursor = std::min(cursor, line.end) - line.start;
                    float const x = text_advance(box, line.text.prefix(line_cursor));
                    return text_line_index_from_x(box, target, x);
                }
                previous = line;
                has_previous = true;
            }
            return cursor;
        }

        [[nodiscard]] auto text_index_from_mouse(BoxNode const& box, Vec2 mouse_pos) -> size_t {
            if (box.text.empty()) {
                return 0u;
            }

            Vec2 const pos = text_position(box, box.state != nullptr ? box.state->rect : box.rect);
            float const line_height = text_line_height(box);
            size_t const target_line =
                mouse_pos.y <= pos.y
                    ? 0u
                    : static_cast<size_t>(std::floor((mouse_pos.y - pos.y) / line_height));
            float const text_x = mouse_pos.x - pos.x;

            size_t line_index = 0u;
            size_t offset = 0u;
            TextLine line = {};
            TextLine last_line = {};
            while (next_text_line(box.text, offset, line)) {
                if (line_index == target_line) {
                    return text_line_index_from_x(box, line, text_x);
                }
                last_line = line;
                line_index += 1u;
            }
            return text_line_index_from_x(box, last_line, text_x);
        }

        [[nodiscard]] auto text_cursor_line_index(StrRef text, size_t cursor) -> size_t {
            cursor = std::min(cursor, text.size());
            size_t line_index = 0u;
            size_t offset = 0u;
            TextLine line = {};
            while (next_text_line(text, offset, line)) {
                if (cursor <= line.end) {
                    return line_index;
                }
                line_index += 1u;
            }
            return line_index;
        }

        [[nodiscard]] auto text_cursor_position(BoxNode const& box, size_t cursor) -> Vec2 {
            Vec2 const pos = text_position(box);
            if (box.text.empty()) {
                return pos;
            }

            cursor = std::min(cursor, box.text.size());
            float const line_height = text_line_height(box);
            size_t line_index = 0u;
            size_t offset = 0u;
            TextLine line = {};
            while (next_text_line(box.text, offset, line)) {
                if (cursor <= line.end) {
                    size_t const line_cursor = std::min(cursor, line.end) - line.start;
                    return {pos.x + text_advance(box, line.text.prefix(line_cursor)),
                            pos.y + line_height * static_cast<float>(line_index)};
                }
                line_index += 1u;
            }
            return pos;
        }

        auto apply_pointer_text_selection(ContextImpl* impl, BoxNode& box, TextSelection selection)
            -> TextSelection {
            TextSelection next =
                ordered_text_selection(clamp_text_selection(selection, box.text.size()));
            if (box.state != nullptr) {
                Signal const signal = box.signal;
                size_t const cursor =
                    text_index_from_mouse(box, impl->frame_desc.input.mouse_pos);
                bool const triple_clicked =
                    signal.hovered && impl->frame_desc.input.mouse_triple_clicked[0u];
                bool const double_clicked =
                    signal.hovered && impl->frame_desc.input.mouse_double_clicked[0u];
                if (signal.pressed_left || triple_clicked || double_clicked) {
                    impl->text_selection_owner_id = box.id;
                }
                if (triple_clicked || double_clicked) {
                    next = triple_clicked ? TextSelection{0u, box.text.size()}
                                          : text_word_selection(box.text, cursor);
                    box.state->text_selection_anchor = next.start;
                    box.state->text_selection_word_start = next.start;
                    box.state->text_selection_word_end = next.end;
                    box.state->text_selection_word_active = next.start != next.end;
                } else {
                    if (signal.pressed_left) {
                        box.state->text_selection_anchor = cursor;
                        box.state->text_selection_word_active = false;
                    }
                    if (signal.active && impl->frame_desc.input.mouse_down[0u]) {
                        if (box.state->text_selection_word_active) {
                            size_t const start = box.state->text_selection_word_start;
                            size_t const end = box.state->text_selection_word_end;
                            if (cursor < start) {
                                next = {cursor, end};
                            } else if (cursor > end) {
                                next = {start, cursor};
                            } else {
                                next = {start, end};
                            }
                        } else {
                            size_t const anchor = box.state->text_selection_anchor;
                            if (cursor != anchor) {
                                next = ordered_text_selection({anchor, cursor});
                            }
                        }
                    }
                    if (signal.released_left) {
                        if (box.state->text_selection_word_active) {
                            box.state->text_selection_word_active = false;
                        } else {
                            size_t const anchor = box.state->text_selection_anchor;
                            next = cursor == anchor ? TextSelection{cursor, cursor}
                                                    : ordered_text_selection({anchor, cursor});
                        }
                    }
                }
            }

            return ordered_text_selection(clamp_text_selection(next, box.text.size()));
        }

        [[nodiscard]] auto apply_keyboard_text_selection(
            ContextImpl const* impl, BoxNode const& box, TextSelection selection
        ) -> TextSelection {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr ||
                (impl->text_selection_owner_id.value != box.id.value && !box.signal.focused)) {
                return ordered_text_selection(clamp_text_selection(selection, box.text.size()));
            }

            TextSelection next = selection;
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                next = apply_text_selection_key_event(box.text, next, input.key_events[index]);
            }
            return ordered_text_selection(clamp_text_selection(next, box.text.size()));
        }

        auto
        apply_text_selection_owner(ContextImpl* impl, BoxNode const& box, TextSelection selection)
            -> void {
            if (selection.start != selection.end && impl->text_selection_owner_id.value == 0u) {
                impl->text_selection_owner_id = box.id;
            } else if (selection.start == selection.end &&
                       impl->text_selection_owner_id.value == box.id.value) {
                impl->text_selection_owner_id = {};
            }
            if (impl->text_selection_owner_id.value == box.id.value &&
                copy_shortcut_pressed(impl)) {
                copy_selected_text(impl, box.text, selection);
            }
        }

        auto apply_selectable_label(ContextImpl* impl, BoxNode& box, TextSelection* selection)
            -> Signal {
            ASSERT(selection != nullptr);

            TextSelection const previous = *selection;
            TextSelection next = apply_pointer_text_selection(impl, box, previous);
            if (next.start != next.end && impl->text_selection_owner_id.value == 0u) {
                impl->text_selection_owner_id = box.id;
            }
            next = apply_keyboard_text_selection(impl, box, next);
            Signal signal = box.signal;
            signal.changed = previous.start != next.start || previous.end != next.end;
            apply_text_selection_owner(impl, box, next);
            *selection = next;
            box.text_selection = next;
            box.signal = signal;
            return signal;
        }

        [[nodiscard]] auto stepped_value(float value, float min_value, float max_value, float step)
            -> float {
            if (step > 0.0f) {
                value = min_value + std::round((value - min_value) / step) * step;
            }
            return std::clamp(value, min_value, max_value);
        }

        [[nodiscard]] auto value_t(ContextImpl const* impl, BoxNode const& box) -> float {
            Rect const rect = box.state != nullptr ? box.state->rect : box.rect;
            float const width = std::max(1.0f, rect_width(rect));
            return std::clamp(
                (impl->frame_desc.input.mouse_pos.x - rect.min.x) / width, 0.0f, 1.0f);
        }

        [[nodiscard]] auto text_buffer_size(char const* buffer, size_t buffer_size) -> size_t {
            if (buffer == nullptr || buffer_size == 0u) {
                return 0u;
            }
            size_t size = 0u;
            while (size < buffer_size && buffer[size] != '\0') {
                size += 1u;
            }
            return std::min(size, buffer_size - 1u);
        }

        [[nodiscard]] auto input_codepoint_allowed(uint32_t codepoint) -> bool {
            return codepoint >= 0x20u && codepoint != 0x7fu && codepoint <= 0x10ffffu &&
                   (codepoint < 0xd800u || codepoint > 0xdfffu);
        }

        [[nodiscard]] auto utf8_from_codepoint(uint32_t codepoint, char out[4]) -> size_t {
            if (!input_codepoint_allowed(codepoint)) {
                return 0u;
            }
            if (codepoint <= 0x7fu) {
                out[0] = static_cast<char>(codepoint);
                return 1u;
            }
            if (codepoint <= 0x7ffu) {
                out[0] = static_cast<char>(0xc0u | (codepoint >> 6u));
                out[1] = static_cast<char>(0x80u | (codepoint & 0x3fu));
                return 2u;
            }
            if (codepoint <= 0xffffu) {
                out[0] = static_cast<char>(0xe0u | (codepoint >> 12u));
                out[1] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
                out[2] = static_cast<char>(0x80u | (codepoint & 0x3fu));
                return 3u;
            }
            out[0] = static_cast<char>(0xf0u | (codepoint >> 18u));
            out[1] = static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu));
            out[2] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
            out[3] = static_cast<char>(0x80u | (codepoint & 0x3fu));
            return 4u;
        }

        [[nodiscard]] auto fixed_text_edit_buffer(char* buffer, size_t buffer_size)
            -> TextEditBuffer {
            return {buffer, text_buffer_size(buffer, buffer_size), buffer_size - 1u, nullptr};
        }

        [[nodiscard]] auto string_text_edit_buffer(StringBuffer* buffer) -> TextEditBuffer {
            return {buffer->data(), buffer->size(), buffer->capacity(), buffer};
        }

        [[nodiscard]] auto text_edit_text(TextEditBuffer const& buffer) -> StrRef {
            return {buffer.data, buffer.size};
        }

        auto text_edit_refresh(TextEditBuffer& buffer) -> void {
            if (buffer.string != nullptr) {
                buffer.data = buffer.string->data();
                buffer.size = buffer.string->size();
                buffer.capacity = buffer.string->capacity();
            }
        }

        [[nodiscard]] auto text_edit_resize(TextEditBuffer& buffer, size_t size) -> bool {
            if (buffer.string != nullptr) {
                if (!buffer.string->resize(size)) {
                    return false;
                }
                text_edit_refresh(buffer);
                return true;
            }
            if (size > buffer.capacity) {
                return false;
            }
            buffer.size = size;
            buffer.data[size] = '\0';
            return true;
        }

        [[nodiscard]] auto text_edit_can_insert(TextEditBuffer const& buffer,
                                                size_t selected_size,
                                                size_t insert_size) -> bool {
            if (buffer.string != nullptr) {
                return true;
            }
            return insert_size <= buffer.capacity - (buffer.size - selected_size);
        }

        auto erase_text_range(TextEditBuffer& buffer, size_t start, size_t end) -> bool {
            start = std::min(start, buffer.size);
            end = std::min(end, buffer.size);
            if (end <= start) {
                return false;
            }
            size_t const size = buffer.size;
            std::memmove(buffer.data + start, buffer.data + end, size - end);
            return text_edit_resize(buffer, size - (end - start));
        }

        auto insert_text_bytes(TextEditBuffer& buffer,
                               size_t& cursor,
                               char const* text,
                               size_t text_size) -> bool {
            cursor = std::min(cursor, buffer.size);
            if (text_size == 0u) {
                return false;
            }
            size_t const size = buffer.size;
            if (!text_edit_resize(buffer, size + text_size)) {
                return false;
            }
            std::memmove(buffer.data + cursor + text_size, buffer.data + cursor, size - cursor);
            std::memcpy(buffer.data + cursor, text, text_size);
            cursor += text_size;
            buffer.data[buffer.size] = '\0';
            return true;
        }

        auto save_text_undo(ContextImpl* impl,
                            StateEntry& state,
                            StrRef text,
                            size_t cursor,
                            TextSelection selection) -> void {
            ASSERT(impl->context_arena != nullptr);

            TextUndoEntry* const entry = arena_new<TextUndoEntry>(*impl->context_arena);
            entry->text = arena_alloc<char>(*impl->context_arena, text.size() + 1u);
            std::memcpy(entry->text, text.data(), text.size());
            entry->text[text.size()] = '\0';
            entry->text_size = text.size();
            entry->cursor = std::min(cursor, text.size());
            entry->selection =
                ordered_text_selection(clamp_text_selection(selection, text.size()));
            entry->previous = state.text_undo_stack;
            state.text_undo_stack = entry;
        }

        auto restore_text_undo(StateEntry& state, TextEditBuffer& buffer, TextSelection& selection)
            -> bool {
            if (state.text_undo_stack == nullptr) {
                return false;
            }

            TextUndoEntry const* const entry = state.text_undo_stack;
            state.text_undo_stack = entry->previous;
            size_t const size =
                buffer.string != nullptr ? entry->text_size : std::min(entry->text_size,
                                                                        buffer.capacity);
            if (!text_edit_resize(buffer, size)) {
                return false;
            }
            std::memcpy(buffer.data, entry->text, size);
            buffer.data[size] = '\0';
            state.text_cursor = std::min(entry->cursor, size);
            selection = ordered_text_selection(clamp_text_selection(entry->selection, size));
            state.text_selection_word_active = false;
            return true;
        }

        auto replace_text_selection_with_bytes(ContextImpl* impl,
                                               StateEntry& state,
                                               TextEditBuffer& buffer,
                                               TextSelection& selection,
                                               char const* text,
                                               size_t text_size) -> bool {
            size_t const selected_size = selection.end - selection.start;
            if (text_size == 0u && selected_size == 0u) {
                return false;
            }
            if (!text_edit_can_insert(buffer, selected_size, text_size)) {
                return false;
            }

            save_text_undo(impl, state, text_edit_text(buffer), state.text_cursor, selection);
            bool changed = false;
            if (selected_size != 0u) {
                changed |= erase_text_range(buffer, selection.start, selection.end);
                state.text_cursor = selection.start;
            }
            changed |= insert_text_bytes(buffer, state.text_cursor, text, text_size);
            selection = {state.text_cursor, state.text_cursor};
            return changed;
        }

        auto apply_bool_widget(ContextImpl const* impl, BoxNode& box, bool* value) -> Signal {
            ASSERT(value != nullptr);
            Signal signal = box.signal;
            signal.activated =
                signal.clicked_left || (signal.focused && key_pressed(impl, Key::SPACE, false));
            if (signal.activated && !box_read_only(box)) {
                *value = !*value;
                signal.changed = true;
            }
            box.widget_value = *value ? 1.0f : 0.0f;
            box.signal = signal;
            return signal;
        }

        auto apply_slider_widget(ContextImpl const* impl,
                                 BoxNode& box,
                                 float* value,
                                 SliderFloatDesc const& desc) -> Signal {
            ASSERT(value != nullptr);
            Signal signal = box.signal;
            float const min_value = std::min(desc.min, desc.max);
            float const max_value = std::max(desc.min, desc.max);
            float next = std::clamp(*value, min_value, max_value);
            bool changed = next != *value;

            bool const writable = !box_read_only(box) && !box_disabled(box);
            bool const dragging = signal.active || signal.pressed_left || signal.released_left;
            if (writable && dragging) {
                next = min_value + value_t(impl, box) * (max_value - min_value);
            }
            if (writable && signal.focused) {
                float const step = desc.step > 0.0f ? desc.step : 0.0f;
                if (step > 0.0f &&
                    (key_pressed(impl, Key::LEFT, true) || key_pressed(impl, Key::DOWN, true))) {
                    next -= step;
                }
                if (step > 0.0f &&
                    (key_pressed(impl, Key::RIGHT, true) || key_pressed(impl, Key::UP, true))) {
                    next += step;
                }
                if (key_pressed(impl, Key::HOME, false)) {
                    next = min_value;
                }
                if (key_pressed(impl, Key::END, false)) {
                    next = max_value;
                }
            }

            next = stepped_value(next, min_value, max_value, desc.step);
            if (writable && next != *value) {
                *value = next;
                changed = true;
            }
            signal.changed = changed && writable;
            box.widget_value = max_value > min_value
                                   ? (std::clamp(*value, min_value, max_value) - min_value) /
                                         (max_value - min_value)
                                   : 0.0f;
            box.signal = signal;
            return signal;
        }

        auto clamp_multiline_cursor_to_scroll(BoxNode& box) -> void {
            if (box.state == nullptr || box.scroll_state == nullptr || box.text.empty()) {
                return;
            }

            TextSelection const selection = ordered_text_selection(clamp_text_selection(
                {box.state->text_selection_start, box.state->text_selection_end}, box.text.size()
            ));
            if (selection.start != selection.end) {
                return;
            }

            float const viewport = std::max(
                0.0f, rect_height(box.scroll_state->rect) - inset_height(box.layout.padding)
            );
            if (viewport <= 0.0f) {
                return;
            }

            float const line_height = text_line_height(box);
            float const scroll_y = box.scroll_state->scroll_y;
            bool moved = false;
            for (;;) {
                size_t const cursor = std::min(box.state->text_cursor, box.text.size());
                float const cursor_y =
                    line_height * static_cast<float>(text_cursor_line_index(box.text, cursor));
                Key const key =
                    cursor_y < scroll_y
                        ? Key::DOWN
                        : (cursor_y + line_height > scroll_y + viewport ? Key::UP : Key::UNKNOWN);
                if (key == Key::UNKNOWN) {
                    break;
                }

                size_t const next = vertical_text_cursor_offset(box, box.text, cursor, key);
                if (next == cursor) {
                    break;
                }
                box.state->text_cursor = next;
                moved = true;
            }
            if (!moved) {
                return;
            }

            box.state->text_selection_anchor = box.state->text_cursor;
            box.state->text_selection_start = box.state->text_cursor;
            box.state->text_selection_end = box.state->text_cursor;
            box.state->text_selection_word_active = false;
            box.text_selection = {box.state->text_cursor, box.state->text_cursor};
        }

        auto request_multiline_cursor_visible(BoxNode const& box) -> void {
            if (!multiline_input_text_box(box.kind) || box.scroll_state == nullptr ||
                box.state == nullptr || box.scroll_state->last_frame == 0u) {
                return;
            }

            float const viewport =
                std::max(0.0f, rect_height(box.scroll_state->rect) -
                                   inset_height(box.layout.padding));
            if (viewport <= 0.0f) {
                return;
            }

            float const line_height = text_line_height(box);
            float const cursor_y =
                line_height * static_cast<float>(text_cursor_line_index(
                                  box.text, std::min(box.state->text_cursor, box.text.size())));
            float const scroll_y = box.scroll_state->scroll_y;
            if (cursor_y < scroll_y) {
                box.scroll_state->scroll_request_y = cursor_y;
                box.scroll_state->scroll_request_set = true;
            } else if (cursor_y + line_height > scroll_y + viewport) {
                box.scroll_state->scroll_request_y = cursor_y + line_height - viewport;
                box.scroll_state->scroll_request_set = true;
            }
        }

        auto apply_input_text_widget(ContextImpl* impl,
                                     BoxNode& box,
                                     TextEditBuffer& buffer,
                                     StrRef tab_text = {}) -> Signal {
            ASSERT(buffer.data != nullptr || buffer.size == 0u);

            Signal signal = box.signal;
            bool const multiline = multiline_input_text_box(box.kind);
            size_t text_size = buffer.size;
            if (box.state != nullptr) {
                StateEntry& state = *box.state;
                state.text_cursor = std::min(state.text_cursor, text_size);
                TextSelection selection = ordered_text_selection(clamp_text_selection(
                    {state.text_selection_start, state.text_selection_end}, text_size));
                if (selection.start == selection.end) {
                    selection = {state.text_cursor, state.text_cursor};
                }
                if (signal.focus_gained) {
                    state.text_cursor = text_size;
                    selection = {text_size, text_size};
                    state.text_selection_word_active = false;
                }
                TextSelection const pointer_selection =
                    apply_pointer_text_selection(impl, box, selection);
                if (signal.pressed_left || signal.active || signal.released_left) {
                    state.text_cursor = state.text_selection_anchor == pointer_selection.end
                                            ? pointer_selection.start
                                            : pointer_selection.end;
                }
                selection = pointer_selection;

                bool changed = false;
                size_t skip_text_newlines = 0u;
                size_t skip_text_tabs = 0u;
                InputState const& input = impl->frame_desc.input;
                if (signal.focused && input.key_events != nullptr) {
                    bool const writable = !box_read_only(box) && !box_disabled(box);
                    for (size_t index = 0u; index < input.key_event_count; ++index) {
                        KeyEvent const& event = input.key_events[index];
                        text_size = buffer.size;
                        state.text_cursor = std::min(state.text_cursor, text_size);
                        selection =
                            ordered_text_selection(clamp_text_selection(selection, text_size));
                        StrRef const text = text_edit_text(buffer);
                        if (event.kind == KeyEventKind::TEXT) {
                            if (writable && multiline &&
                                (event.codepoint == '\n' || event.codepoint == '\r')) {
                                if (skip_text_newlines != 0u) {
                                    skip_text_newlines -= 1u;
                                } else {
                                    changed |= replace_text_selection_with_bytes(
                                        impl, state, buffer, selection, "\n", 1u);
                                }
                            } else if (writable && multiline && event.codepoint == '\t') {
                                if (skip_text_tabs != 0u) {
                                    skip_text_tabs -= 1u;
                                } else if (!tab_text.empty()) {
                                    changed |= replace_text_selection_with_bytes(impl,
                                                                                 state,
                                                                                 buffer,
                                                                                 selection,
                                                                                 tab_text.data(),
                                                                                 tab_text.size());
                                }
                            } else if (writable) {
                                char insert_text[4] = {};
                                size_t const insert_size =
                                    utf8_from_codepoint(event.codepoint, insert_text);
                                if (insert_size != 0u) {
                                    changed |= replace_text_selection_with_bytes(impl,
                                                                                 state,
                                                                                 buffer,
                                                                                 selection,
                                                                                 insert_text,
                                                                                 insert_size);
                                }
                            }
                            continue;
                        }
                        if (event.kind != KeyEventKind::PRESS &&
                            event.kind != KeyEventKind::REPEAT) {
                            continue;
                        }
                        if (shortcut_key_pressed(event, Key::Z)) {
                            if (writable && restore_text_undo(state, buffer, selection)) {
                                changed = true;
                            }
                            continue;
                        }
                        if (shortcut_key_pressed(event, Key::A)) {
                            state.text_cursor = text_size;
                            selection = {0u, text_size};
                            state.text_selection_word_active = false;
                            continue;
                        }
                        if (shortcut_key_pressed(event, Key::V)) {
                            if (writable && impl->get_clipboard_text != nullptr) {
                                StrRef const paste = impl->get_clipboard_text(
                                    impl->clipboard_user_data, impl->frame_arena);
                                if (!paste.empty()) {
                                    changed |= replace_text_selection_with_bytes(impl,
                                                                                 state,
                                                                                 buffer,
                                                                                 selection,
                                                                                 paste.data(),
                                                                                 paste.size());
                                }
                            }
                            continue;
                        }
                        if (shortcut_key_pressed(event, Key::X)) {
                            if (writable && impl->set_clipboard_text != nullptr &&
                                selection.start != selection.end) {
                                save_text_undo(
                                    impl, state, text, state.text_cursor, selection);
                                copy_selected_text(impl, text, selection);
                                changed |=
                                    erase_text_range(buffer, selection.start, selection.end);
                                state.text_cursor = selection.start;
                                selection = {state.text_cursor, state.text_cursor};
                            }
                            continue;
                        }
                        if (multiline && event.key == Key::ENTER) {
                            if (writable) {
                                changed |= replace_text_selection_with_bytes(
                                    impl, state, buffer, selection, "\n", 1u);
                                skip_text_newlines += 1u;
                            }
                            continue;
                        }
                        if (multiline && event.key == Key::TAB &&
                            (event.mods & (KEY_MOD_CTRL | KEY_MOD_ALT | KEY_MOD_SUPER)) == 0u) {
                            if (writable && !tab_text.empty()) {
                                changed |= replace_text_selection_with_bytes(impl,
                                                                             state,
                                                                             buffer,
                                                                             selection,
                                                                             tab_text.data(),
                                                                             tab_text.size());
                            }
                            skip_text_tabs += 1u;
                            continue;
                        }
                        if (text_selection_key_event(event)) {
                            selection =
                                apply_text_cursor_selection_key_event(
                                    text, selection, state.text_cursor, event);
                            continue;
                        }
                        switch (event.key) {
                        case Key::LEFT:
                            state.text_cursor =
                                selection.start != selection.end
                                    ? selection.start
                                    : text_cursor_key_offset(text, state.text_cursor, event);
                            selection = {state.text_cursor, state.text_cursor};
                            break;
                        case Key::RIGHT:
                            state.text_cursor =
                                selection.start != selection.end
                                    ? selection.end
                                    : text_cursor_key_offset(text, state.text_cursor, event);
                            selection = {state.text_cursor, state.text_cursor};
                            break;
                        case Key::UP:
                        case Key::DOWN:
                            if ((event.mods & KEY_MOD_SHIFT) == 0u) {
                                state.text_cursor =
                                    selection.start != selection.end
                                        ? (event.key == Key::UP ? selection.start
                                                                : selection.end)
                                        : vertical_text_cursor_offset(
                                              box, text, state.text_cursor, event.key);
                                selection = {state.text_cursor, state.text_cursor};
                            }
                            break;
                        case Key::HOME:
                            state.text_cursor = 0u;
                            selection = {};
                            break;
                        case Key::END:
                            state.text_cursor = text_size;
                            selection = {text_size, text_size};
                            break;
                        case Key::BACKSPACE:
                            if (writable && selection.start != selection.end) {
                                save_text_undo(
                                    impl, state, text, state.text_cursor, selection);
                                changed |=
                                    erase_text_range(buffer, selection.start, selection.end);
                                state.text_cursor = selection.start;
                                selection = {state.text_cursor, state.text_cursor};
                            } else if (writable && state.text_cursor > 0u) {
                                size_t const start = (event.mods & KEY_MOD_CTRL) != 0u
                                                         ? previous_word_offset(text,
                                                                                state.text_cursor)
                                                         : previous_text_offset(text,
                                                                                state.text_cursor);
                                save_text_undo(
                                    impl, state, text, state.text_cursor, selection);
                                changed |= erase_text_range(buffer, start, state.text_cursor);
                                state.text_cursor = start;
                                selection = {state.text_cursor, state.text_cursor};
                            }
                            break;
                        case Key::DELETE_KEY:
                            if (writable && selection.start != selection.end) {
                                save_text_undo(
                                    impl, state, text, state.text_cursor, selection);
                                changed |=
                                    erase_text_range(buffer, selection.start, selection.end);
                                state.text_cursor = selection.start;
                                selection = {state.text_cursor, state.text_cursor};
                            } else if (writable && state.text_cursor < text_size) {
                                size_t const end = (event.mods & KEY_MOD_CTRL) != 0u
                                                       ? next_word_offset(text, state.text_cursor)
                                                       : next_text_offset(text, state.text_cursor);
                                save_text_undo(
                                    impl, state, text, state.text_cursor, selection);
                                changed |= erase_text_range(buffer, state.text_cursor, end);
                                selection = {state.text_cursor, state.text_cursor};
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
                text_size = buffer.size;
                selection = ordered_text_selection(clamp_text_selection(selection, text_size));
                state.text_cursor = std::min(state.text_cursor, text_size);
                state.text_selection_start = selection.start;
                state.text_selection_end = selection.end;
                box.text_selection = selection;
                signal.changed = changed;
            }
            signal.activated = !multiline && signal.focused && key_pressed(impl, Key::ENTER, false);
            box.text = copy_frame_str(impl, text_edit_text(buffer));
            request_multiline_cursor_visible(box);
            apply_text_selection_owner(impl, box, box.text_selection);
            box.signal = signal;
            return signal;
        }

        [[nodiscard]] auto to_draw_rect(Rect rect) -> draw::Rect {
            return {{rect.min.x, rect.min.y}, {rect.max.x, rect.max.y}};
        }

        [[nodiscard]] auto to_draw_color(Color color) -> draw::Color {
            if (!color_set(color)) {
                return {0.0f, 0.0f, 0.0f, 0.0f};
            }
            return {color.r, color.g, color.b, color.a};
        }

        auto draw_widget_rect(draw::Context draw_context,
                              Rect rect,
                              Color fill,
                              Color border,
                              float border_thickness,
                              float radius,
                              float opacity) -> void {
            draw::BoxStyle style = {};
            style.fill_color = to_draw_color(color_mul_alpha(fill, opacity));
            style.texture = {};
            style.border_color = to_draw_color(color_mul_alpha(border, opacity));
            style.border_thickness = border_thickness;
            style.radius = radius;
            style.shadow = {};
            draw::draw_rect_styled(draw_context, to_draw_rect(rect), style);
        }

        [[nodiscard]] auto inset_rect(Rect rect, float value) -> Rect {
            rect.min.x += value;
            rect.min.y += value;
            rect.max.x -= value;
            rect.max.y -= value;
            return rect;
        }

        auto render_widget_parts(ContextImpl const* impl,
                                 BoxNode const& box,
                                 draw::Context draw_context) -> void {
            float const opacity = box.resolved_style.opacity;
            ThemeTokens const& tokens = impl->theme.tokens;
            if (box.kind == BoxKind::CHECKBOX) {
                float const side = std::min(16.0f, std::max(0.0f, rect_height(box.rect) - 4.0f));
                Rect const mark = {
                    {box.rect.min.x + 2.0f, box.rect.min.y + (rect_height(box.rect) - side) * 0.5f},
                    {box.rect.min.x + 2.0f + side,
                     box.rect.min.y + (rect_height(box.rect) + side) * 0.5f}};
                draw_widget_rect(draw_context,
                                 mark,
                                 tokens.panel,
                                 box.resolved_style.border,
                                 box.resolved_style.border_thickness,
                                 3.0f,
                                 opacity);
                if (box.widget_value > 0.5f) {
                    draw_widget_rect(draw_context,
                                     inset_rect(mark, 4.0f),
                                     tokens.accent,
                                     {},
                                     0.0f,
                                     2.0f,
                                     opacity);
                }
            } else if (box.kind == BoxKind::TOGGLE) {
                float const height = std::min(18.0f, std::max(0.0f, rect_height(box.rect) - 4.0f));
                float const width = std::min(36.0f, std::max(height, rect_width(box.rect) - 4.0f));
                Rect const track = {{box.rect.min.x + 2.0f,
                                     box.rect.min.y + (rect_height(box.rect) - height) * 0.5f},
                                    {box.rect.min.x + 2.0f + width,
                                     box.rect.min.y + (rect_height(box.rect) + height) * 0.5f}};
                draw_widget_rect(draw_context,
                                 track,
                                 box.widget_value > 0.5f ? tokens.accent : tokens.panel,
                                 box.resolved_style.border,
                                 box.resolved_style.border_thickness,
                                 height * 0.5f,
                                 opacity);
                float const knob_size = std::max(0.0f, height - 4.0f);
                float const knob_x =
                    track.min.x + 2.0f + (width - knob_size - 4.0f) * box.widget_value;
                Rect const knob = {{knob_x, track.min.y + 2.0f},
                                   {knob_x + knob_size, track.min.y + 2.0f + knob_size}};
                draw_widget_rect(draw_context, knob, tokens.text, {}, 0.0f, knob_size, opacity);
            } else if (box.kind == BoxKind::SLIDER_FLOAT) {
                float const center_y = (box.rect.min.y + box.rect.max.y) * 0.5f;
                Rect const track = {{box.rect.min.x + 4.0f, center_y - 2.0f},
                                    {box.rect.max.x - 4.0f, center_y + 2.0f}};
                Rect fill = track;
                fill.max.x = fill.min.x + rect_width(track) * box.widget_value;
                draw_widget_rect(draw_context, track, tokens.panel, {}, 0.0f, 2.0f, opacity);
                draw_widget_rect(draw_context, fill, tokens.accent, {}, 0.0f, 2.0f, opacity);
                float const thumb_x = std::clamp(fill.max.x, track.min.x, track.max.x);
                Rect const thumb = {{thumb_x - 4.0f, center_y - 8.0f},
                                    {thumb_x + 4.0f, center_y + 8.0f}};
                draw_widget_rect(
                    draw_context, thumb, tokens.text, tokens.canvas, 1.0f, 4.0f, opacity);
            } else if (input_text_box(box.kind) && box.signal.focused && box.state != nullptr) {
                size_t const cursor = std::min(box.state->text_cursor, box.text.size());
                Vec2 const pos = text_cursor_position(box, cursor);
                Rect const caret = {
                    {pos.x, pos.y}, {pos.x + 1.0f, pos.y + text_line_height(box)}};
                draw_widget_rect(draw_context, caret, tokens.text, {}, 0.0f, 0.0f, opacity);
            }
        }

        auto render_text_selection(ContextImpl const* impl,
                                   BoxNode const& box,
                                   draw::Context draw_context) -> void {
            if ((box.kind != BoxKind::SELECTABLE_LABEL && !input_text_box(box.kind)) ||
                box.text.empty()) {
                return;
            }

            TextSelection const selection =
                ordered_text_selection(clamp_text_selection(box.text_selection, box.text.size()));
            if (selection.start == selection.end) {
                return;
            }

            Vec2 const pos = text_position(box);
            Color color = impl->theme.tokens.accent;
            color.a *= 0.45f;
            float const line_height = text_line_height(box);
            size_t line_index = 0u;
            size_t offset = 0u;
            TextLine line = {};
            while (next_text_line(box.text, offset, line)) {
                size_t const start = std::max(selection.start, line.start);
                size_t const end = std::min(selection.end, line.end);
                if (start < end) {
                    float const selection_start =
                        text_advance(box, line.text.prefix(start - line.start));
                    float const selection_end =
                        text_advance(box, line.text.prefix(end - line.start));
                    Rect const selection_rect = {
                        {pos.x + selection_start,
                         pos.y + line_height * static_cast<float>(line_index)},
                        {pos.x + selection_end,
                         pos.y + line_height * static_cast<float>(line_index + 1u)},
                    };
                    draw_widget_rect(draw_context,
                                     selection_rect,
                                     color,
                                     {},
                                     0.0f,
                                     0.0f,
                                     box.resolved_style.opacity);
                }
                line_index += 1u;
            }
        }

        auto render_scrollbar(ContextImpl const* impl,
                              BoxNode const& box,
                              draw::Context draw_context) -> void {
            StateEntry const* const state = box.scroll_state;
            if (state == nullptr || !state->scroll_valid || state->scroll_max_y <= 0.0f) {
                return;
            }

            Rect const track = scrollbar_track_rect(box.rect, box.resolved_style.radius);
            if (!scrollbar_track_valid(track)) {
                return;
            }
            Rect const thumb = scrollbar_thumb_rect(track,
                                                    state->scroll_y,
                                                    state->scroll_max_y,
                                                    state->scroll_viewport_height,
                                                    state->scroll_content_height);
            float const width = rect_width(track);

            ThemeTokens const& tokens = impl->theme.tokens;
            float const opacity = box.resolved_style.opacity;
            draw_widget_rect(draw_context, track, tokens.panel, {}, 0.0f, width * 0.5f, opacity);
            draw_widget_rect(
                draw_context, thumb, tokens.text_muted, {}, 0.0f, width * 0.5f, opacity);
        }

        [[nodiscard]] auto text_draw_y(float line_y) -> float {
            return std::round(line_y - TEXT_RASTER_PADDING);
        }

        [[nodiscard]] auto text_draw_position(float x, float line_y) -> draw::Vec2 {
            return {std::round(x), text_draw_y(line_y)};
        }

        auto render_box(ContextImpl const* impl, draw::Context draw_context, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = box_clips(box);
            bool const clips_cell_content = table_cell_box(box.kind);
            if (clips && !clips_cell_content) {
                draw::push_clip_rect(draw_context, to_draw_rect(box.rect));
            }

            if (box.kind != BoxKind::ROOT) {
                draw::BoxStyle style = {};
                style.fill_color = to_draw_color(
                    color_mul_alpha(box.resolved_style.background, box.resolved_style.opacity));
                style.border_color = to_draw_color(
                    color_mul_alpha(box.resolved_style.border, box.resolved_style.opacity));
                style.border_thickness = box.resolved_style.border_thickness;
                style.radius = box.resolved_style.radius;
                style.shadow.offset = {box.resolved_style.shadow.offset.x,
                                       box.resolved_style.shadow.offset.y};
                style.shadow.blur_radius = box.resolved_style.shadow.blur_radius;
                style.shadow.spread = box.resolved_style.shadow.spread;
                style.shadow.color = to_draw_color(
                    color_mul_alpha(box.resolved_style.shadow.color, box.resolved_style.opacity));

                if (color_visible(box.resolved_style.background) ||
                    color_visible(box.resolved_style.border) ||
                    color_visible(box.resolved_style.shadow.color)) {
                    draw::draw_rect_styled(draw_context, to_draw_rect(box.rect), style);
                }

                render_widget_parts(impl, box, draw_context);
                render_text_selection(impl, box, draw_context);

                bool const text_kind = box.kind == BoxKind::LABEL ||
                                       box.kind == BoxKind::SELECTABLE_LABEL ||
                                       box.kind == BoxKind::BUTTON ||
                                       box.kind == BoxKind::CHECKBOX ||
                                       box.kind == BoxKind::TOGGLE ||
                                       box.kind == BoxKind::SLIDER_FLOAT ||
                                       input_text_box(box.kind);
                if (text_kind && font_cache::font_valid(box.resolved_style.font)) {
                    draw::TextStyle text_style = {};
                    text_style.font = box.resolved_style.font;
                    text_style.size = text_font_size(box);
                    text_style.color = to_draw_color(
                        color_mul_alpha(box.resolved_style.foreground, box.resolved_style.opacity));
                    Vec2 const text_pos = text_position(box);
                    float const line_height = text_line_height(box);
                    size_t line_index = 0u;
                    size_t offset = 0u;
                    TextLine line = {};
                    while (next_text_line(box.text, offset, line)) {
                        if (!line.text.empty()) {
                            font_cache::TextRun run = {};
                            draw::measure_text(draw_context, text_style, line.text, run);
                            float const line_y =
                                text_pos.y + line_height * static_cast<float>(line_index);
                            draw::draw_text(draw_context,
                                            text_draw_position(text_pos.x, line_y),
                                            text_style,
                                            line.text,
                                            nullptr);
                        }
                        line_index += 1u;
                    }
                }
            }

            if (clips_cell_content) {
                draw::push_clip_rect(
                    draw_context, to_draw_rect(content_rect(box.rect, box.layout.padding)));
            }

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (floating_box(impl->boxes[child].kind)) {
                    continue;
                }
                render_box(impl, draw_context, child);
            }

            if (clips) {
                draw::pop_clip_rect(draw_context);
            }
        }

        auto render_floating_boxes(ContextImpl const* impl,
                                   draw::Context draw_context,
                                   size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = top_layer_box(impl, index) && box_clips(box);
            if (clips) {
                draw::push_clip_rect(draw_context, to_draw_rect(box.rect));
            }

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (floating_box(impl->boxes[child].kind)) {
                    render_box(impl, draw_context, child);
                    render_floating_boxes(impl, draw_context, child);
                } else {
                    render_floating_boxes(impl, draw_context, child);
                }
            }

            if (clips) {
                draw::pop_clip_rect(draw_context);
            }
        }

        auto render_scrollbars(ContextImpl const* impl, draw::Context draw_context, size_t index)
            -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = box_clips(box);
            if (clips) {
                Rect const clip = table_cell_box(box.kind)
                                      ? content_rect(box.rect, box.layout.padding)
                                      : box.rect;
                draw::push_clip_rect(draw_context, to_draw_rect(clip));
            }

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (floating_box(impl->boxes[child].kind)) {
                    continue;
                }
                render_scrollbars(impl, draw_context, child);
            }

            render_scrollbar(impl, box, draw_context);

            if (clips) {
                draw::pop_clip_rect(draw_context);
            }
        }

        auto render_floating_scrollbars(ContextImpl const* impl,
                                        draw::Context draw_context,
                                        size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = top_layer_box(impl, index) && box_clips(box);
            if (clips) {
                draw::push_clip_rect(draw_context, to_draw_rect(box.rect));
            }

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (floating_box(impl->boxes[child].kind)) {
                    render_scrollbars(impl, draw_context, child);
                    render_floating_scrollbars(impl, draw_context, child);
                } else {
                    render_floating_scrollbars(impl, draw_context, child);
                }
            }

            if (clips) {
                draw::pop_clip_rect(draw_context);
            }
        }

    } // namespace

    auto id(StrRef value) -> Id {
        uint64_t const hash = value.hash64();
        return {hash != 0u ? hash : 1u};
    }

    auto id(uint64_t value) -> Id {
        return {hash_u64(value)};
    }

    auto context_valid(Context context) -> bool {
        return context.handle != nullptr;
    }

    auto default_theme() -> ThemeDesc {
        ThemeDesc theme = {};
        ThemeTokens& tokens = theme.tokens;
        tokens.canvas = rgb(20, 24, 32);
        tokens.panel = rgb(30, 34, 42);
        tokens.control = rgb(42, 46, 56);
        tokens.control_hovered = rgb(54, 58, 68);
        tokens.control_active = rgb(64, 68, 78);
        tokens.accent = rgb(120, 170, 255);
        tokens.danger = rgb(190, 70, 70);
        tokens.text = rgb(240, 242, 247);
        tokens.text_muted = rgba(240, 242, 247, 150);
        tokens.border = rgba(255, 255, 255, 40);
        tokens.disabled_text = rgba(240, 242, 247, 110);

        theme.root = {.foreground = tokens.text, .font_size = 16.0f};
        theme_role(theme, StyleRole::CANVAS).normal = {.background = tokens.canvas,
                                                       .foreground = tokens.text};
        theme_role(theme, StyleRole::PANEL).normal = {
            .background = tokens.panel,
            .foreground = tokens.text,
            .border = tokens.border,
            .border_thickness = tokens.border_thickness,
            .radius = tokens.radius_md,
        };
        theme_role(theme, StyleRole::CONTROL) = {
            .normal =
                {
                    .background = tokens.control,
                    .foreground = tokens.text,
                    .border = tokens.border,
                    .border_thickness = tokens.border_thickness,
                    .radius = tokens.radius_md,
                },
            .hovered = {.background = tokens.control_hovered},
            .active = {.background = tokens.control_active},
            .disabled =
                {
                    .background = tokens.panel,
                    .foreground = tokens.disabled_text,
                },
        };
        theme_role(theme, StyleRole::ACCENT) = {
            .normal =
                {
                    .background = tokens.accent,
                    .foreground = rgb(255, 255, 255),
                    .radius = tokens.radius_md,
                },
            .hovered = {.background = rgb(138, 185, 255)},
            .active = {.background = rgb(96, 145, 225)},
        };
        theme_role(theme, StyleRole::DANGER) = {
            .normal =
                {
                    .background = tokens.danger,
                    .foreground = rgb(255, 255, 255),
                    .radius = tokens.radius_md,
                },
            .hovered = {.background = rgb(210, 82, 82)},
            .active = {.background = rgb(170, 58, 58)},
        };

        theme_kind(theme, BoxKind::ROOT).role = StyleRole::CANVAS;
        theme_kind(theme, BoxKind::POPUP).role = StyleRole::PANEL;
        theme_kind(theme, BoxKind::POPUP).style.normal.shadow = {
            .offset = {0.0f, 8.0f},
            .blur_radius = 18.0f,
            .color = rgba(0, 0, 0, 130),
        };
        theme_kind(theme, BoxKind::MODAL).style.normal.background = rgba(0, 0, 0, 150);
        theme_kind(theme, BoxKind::LABEL).role = StyleRole::TEXT;
        theme_kind(theme, BoxKind::SELECTABLE_LABEL).role = StyleRole::TEXT;
        theme_kind(theme, BoxKind::TABLE).role = StyleRole::PANEL;
        theme_kind(theme, BoxKind::TABLE_CELL).style.normal = {
            .border = tokens.border,
            .border_thickness = tokens.border_thickness,
        };
        theme_kind(theme, BoxKind::TABLE_HEADER_CELL).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::TAB_VIEW).role = StyleRole::PANEL;
        theme_kind(theme, BoxKind::TAB).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::TAB_BODY).role = StyleRole::PANEL;
        theme_kind(theme, BoxKind::BUTTON).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::CHECKBOX).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::TOGGLE).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::SLIDER_FLOAT).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::INPUT_TEXT).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::INPUT_TEXT_MULTILINE).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::INPUT_TEXT).style.hovered.background = tokens.control;
        theme_kind(theme, BoxKind::INPUT_TEXT).style.active.background = tokens.control;
        theme_kind(theme, BoxKind::INPUT_TEXT_MULTILINE).style.hovered.background = tokens.control;
        theme_kind(theme, BoxKind::INPUT_TEXT_MULTILINE).style.active.background = tokens.control;
        return theme;
    }

    auto theme_role(ThemeDesc& theme, StyleRole role) -> ThemeStyle& {
        ASSERT(role != StyleRole::COUNT);
        return theme.roles[static_cast<size_t>(role)];
    }

    auto theme_kind(ThemeDesc& theme, BoxKind kind) -> ThemeKindStyle& {
        ASSERT(kind != BoxKind::COUNT);
        return theme.kinds[static_cast<size_t>(kind)];
    }

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void {
        ContextImpl* const impl = arena_new<ContextImpl>(arena);
        size_t const capacity = std::max(desc.initial_box_capacity, size_t{16u});
        impl->context_arena = &arena;
        impl->box_capacity = capacity;
        impl->state_table_size = next_power_of_two(capacity * 4u);
        impl->boxes = arena_alloc<BoxNode>(arena, capacity);
        impl->infos = arena_alloc<BoxInfo>(arena, capacity);
        impl->parent_stack = arena_alloc<size_t>(arena, capacity);
        impl->focus_order = arena_alloc<Id>(arena, capacity);
        impl->state_table = arena_alloc<StateEntry>(arena, impl->state_table_size);
        std::memset(impl->boxes, 0, sizeof(BoxNode) * capacity);
        std::memset(impl->infos, 0, sizeof(BoxInfo) * capacity);
        std::memset(impl->parent_stack, 0, sizeof(size_t) * capacity);
        std::memset(impl->focus_order, 0, sizeof(Id) * capacity);
        std::memset(impl->state_table, 0, sizeof(StateEntry) * impl->state_table_size);
        impl->frame_arena.init({desc.frame_arena_reserve_size, desc.frame_arena_commit_size});
        impl->theme = desc.theme != nullptr ? *desc.theme : default_theme();
        impl->set_clipboard_text = desc.set_clipboard_text;
        impl->get_clipboard_text = desc.get_clipboard_text;
        impl->clipboard_user_data = desc.clipboard_user_data;
        out_context.handle = impl;
    }

    auto destroy_context(Context& context) -> void {
        ContextImpl* const impl = impl_from_context(context);
        if (impl != nullptr) {
            impl->frame_arena.destroy();
        }
        context.handle = nullptr;
    }

    auto set_theme(Context context, ThemeDesc const& theme) -> void {
        ContextImpl* const impl = impl_from_context(context);
        if (impl != nullptr) {
            impl->theme = theme;
        }
    }

    Scope::Scope(Frame* frame, size_t box_index) : m_frame(frame), m_box_index(box_index) {}

    Scope::~Scope() {
        close();
    }

    Scope::Scope(Scope&& other) noexcept : m_frame(other.m_frame), m_box_index(other.m_box_index) {
        other.m_frame = nullptr;
        other.m_box_index = 0u;
    }

    auto Scope::operator=(Scope&& other) noexcept -> Scope& {
        if (this != &other) {
            close();
            m_frame = other.m_frame;
            m_box_index = other.m_box_index;
            other.m_frame = nullptr;
            other.m_box_index = 0u;
        }
        return *this;
    }

    Scope::operator bool() const {
        return m_frame != nullptr;
    }

    auto Scope::signal() const -> Signal {
        if (m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_frame);
        if (impl == nullptr || m_box_index >= impl->box_count) {
            return {};
        }
        BoxNode& box = impl->boxes[m_box_index];
        box.interactive = true;
        box.focusable = true;
        add_focus_order(impl, box);
        box.signal = make_signal(impl, box);
        return apply_button_activation(impl, box);
    }

    auto Scope::close() -> void {
        if (m_frame != nullptr) {
            ContextImpl* const impl = impl_from_frame(*m_frame);
            if (impl != nullptr) {
                pop_parent_to(impl, m_box_index);
            }
            m_frame = nullptr;
        }
    }

    auto ListScope::row(Id id_value, BoxDesc const& desc) -> Scope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        BoxDesc row_desc = desc;
        if (row_desc.layout.width.kind == SizeKind::AUTO) {
            row_desc.layout.width = fill();
        }
        if (row_desc.layout.height.kind == SizeKind::AUTO) {
            row_desc.layout.height = px(m_item_height);
        }
        return m_scope.m_frame->row(id_value, row_desc);
    }

    ListScope::ListScope(Scope&& scope, size_t first_index, size_t end_index, float item_height)
        : first(first_index), end(end_index), m_scope(std::move(scope)),
          m_item_height(item_height) {}

    TableRowScope::TableRowScope(Scope&& scope) : m_scope(std::move(scope)) {}

    TableRowScope::operator bool() const {
        return static_cast<bool>(m_scope);
    }

    auto TableRowScope::cell(TableCellDesc const& desc) -> Scope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        BoxKind const kind = impl->boxes[parent].kind == BoxKind::TABLE_HEADER_ROW
                                 ? BoxKind::TABLE_HEADER_CELL
                                 : BoxKind::TABLE_CELL;
        size_t const index = append_box(impl,
                                        kind,
                                        structural_id(impl, parent, kind),
                                        {},
                                        {},
                                        desc.box,
                                        false,
                                        false);
        BoxNode& box = impl->boxes[index];
        box.table_column_span = table_span(desc.column_span);
        box.table_row_span = table_span(desc.row_span);
        push_parent(impl, index);
        return {m_scope.m_frame, index};
    }

    auto TableRowScope::cell(Id id_value, TableCellDesc const& desc) -> Scope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        BoxKind const kind = impl->boxes[parent].kind == BoxKind::TABLE_HEADER_ROW
                                 ? BoxKind::TABLE_HEADER_CELL
                                 : BoxKind::TABLE_CELL;
        size_t const index = append_box(impl,
                                        kind,
                                        explicit_id(impl, parent, kind, id_value),
                                        id_value,
                                        {},
                                        desc.box,
                                        false,
                                        false);
        BoxNode& box = impl->boxes[index];
        box.table_column_span = table_span(desc.column_span);
        box.table_row_span = table_span(desc.row_span);
        push_parent(impl, index);
        return {m_scope.m_frame, index};
    }

    TableScope::TableScope(Scope&& scope) : m_scope(std::move(scope)) {}

    TableScope::operator bool() const {
        return static_cast<bool>(m_scope);
    }

    auto TableScope::header_row(BoxDesc const& desc) -> TableRowScope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TABLE_HEADER_ROW,
                                        structural_id(impl, parent, BoxKind::TABLE_HEADER_ROW),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return TableRowScope(Scope(m_scope.m_frame, index));
    }

    auto TableScope::header_row(Id id_value, BoxDesc const& desc) -> TableRowScope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        size_t const index =
            append_box(impl,
                       BoxKind::TABLE_HEADER_ROW,
                       explicit_id(impl, parent, BoxKind::TABLE_HEADER_ROW, id_value),
                       id_value,
                       {},
                       desc,
                       false,
                       false);
        push_parent(impl, index);
        return TableRowScope(Scope(m_scope.m_frame, index));
    }

    auto TableScope::row(BoxDesc const& desc) -> TableRowScope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TABLE_ROW,
                                        structural_id(impl, parent, BoxKind::TABLE_ROW),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return TableRowScope(Scope(m_scope.m_frame, index));
    }

    auto TableScope::row(Id id_value, BoxDesc const& desc) -> TableRowScope {
        if (m_scope.m_frame == nullptr) {
            return {};
        }
        ContextImpl* const impl = impl_from_frame(*m_scope.m_frame);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TABLE_ROW,
                                        explicit_id(impl, parent, BoxKind::TABLE_ROW, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return TableRowScope(Scope(m_scope.m_frame, index));
    }

    TabViewScope::TabViewScope(Scope&& root, Scope&& body, TabViewResult result)
        : m_root(std::move(root)), m_body(std::move(body)), m_result(result) {}

    auto TabViewScope::operator=(TabViewScope&& other) noexcept -> TabViewScope& {
        if (this != &other) {
            m_body = {};
            m_root = {};
            m_root = std::move(other.m_root);
            m_body = std::move(other.m_body);
            m_result = other.m_result;
        }
        return *this;
    }

    TabViewScope::operator bool() const {
        return static_cast<bool>(m_body) && m_result.selected_index != TAB_INDEX_NONE;
    }

    auto TabViewScope::result() const -> TabViewResult {
        return m_result;
    }

    auto TabViewScope::selected_index() const -> size_t {
        return m_result.selected_index;
    }

    Frame::Frame(void* handle) : m_handle(handle) {}

    namespace detail {
        auto frame_handle(Frame const& frame) -> void* {
            return frame.m_handle;
        }
    } // namespace detail

    auto Frame::row(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::ROW,
                                        structural_id(impl, parent, BoxKind::ROW),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::row(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::ROW,
                                        explicit_id(impl, parent, BoxKind::ROW, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::column(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::COLUMN,
                                        structural_id(impl, parent, BoxKind::COLUMN),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::column(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::COLUMN,
                                        explicit_id(impl, parent, BoxKind::COLUMN, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::overlay(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::OVERLAY,
                                        structural_id(impl, parent, BoxKind::OVERLAY),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::overlay(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::OVERLAY,
                                        explicit_id(impl, parent, BoxKind::OVERLAY, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::popup(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::POPUP,
                                        explicit_id(impl, parent, BoxKind::POPUP, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        true,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::modal(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc modal_desc = desc;
        modal_desc.layout.width = fill();
        modal_desc.layout.height = fill();
        size_t const index = append_box(impl,
                                        BoxKind::MODAL,
                                        explicit_id(impl, parent, BoxKind::MODAL, id_value),
                                        id_value,
                                        {},
                                        modal_desc,
                                        true,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::scroll_panel(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc panel_desc = desc;
        panel_desc.layout.clip = true;
        panel_desc.layout.scroll_y = true;
        size_t const index = append_box(impl,
                                        BoxKind::SCROLL_PANEL,
                                        explicit_id(impl, parent, BoxKind::SCROLL_PANEL, id_value),
                                        id_value,
                                        {},
                                        panel_desc,
                                        false,
                                        false);
        BoxNode& panel = impl->boxes[index];
        set_scroll_state(impl, panel, id_value);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::table(BoxDesc const& desc) -> TableScope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TABLE,
                                        structural_id(impl, parent, BoxKind::TABLE),
                                        {},
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return TableScope(Scope(this, index));
    }

    auto Frame::table(Id id_value, BoxDesc const& desc) -> TableScope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TABLE,
                                        explicit_id(impl, parent, BoxKind::TABLE, id_value),
                                        id_value,
                                        {},
                                        desc,
                                        false,
                                        false);
        push_parent(impl, index);
        return TableScope(Scope(this, index));
    }

    auto Frame::tab_view(Id id_value, TabViewDesc const& desc) -> TabViewScope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);

        BoxDesc view_desc = desc.box;
        if (view_desc.layout.width.kind == SizeKind::AUTO) {
            view_desc.layout.width = fill();
        }
        if (view_desc.layout.height.kind == SizeKind::AUTO) {
            view_desc.layout.height = fill();
        }
        if (view_desc.layout.align_x == Align::START) {
            view_desc.layout.align_x = Align::STRETCH;
        }

        size_t const view_index = append_box(
            impl,
            BoxKind::TAB_VIEW,
            explicit_id(impl, parent, BoxKind::TAB_VIEW, id_value),
            id_value,
            {},
            view_desc,
            false,
            false
        );
        push_parent(impl, view_index);

        Id const view_id = impl->boxes[view_index].id;
        Id const bar_id = tab_child_id(view_id, 0x7ab0u);
        BoxDesc bar_desc = desc.tab_bar_box;
        if (bar_desc.layout.width.kind == SizeKind::AUTO) {
            bar_desc.layout.width = fill();
        }
        if (bar_desc.layout.height.kind == SizeKind::AUTO) {
            bar_desc.layout.height = px(std::max(desc.tab_bar_height, 1.0f));
        }
        if (bar_desc.layout.gap == 0.0f) {
            bar_desc.layout.gap = 2.0f;
        }
        bar_desc.layout.align_y = Align::CENTER;
        bar_desc.layout.clip = true;
        size_t const bar_index = append_box(
            impl,
            BoxKind::TAB_BAR,
            explicit_id(impl, view_index, BoxKind::TAB_BAR, bar_id),
            bar_id,
            {},
            bar_desc,
            false,
            false
        );
        push_parent(impl, bar_index);

        size_t count = tab_count(desc);
        size_t selected = selected_tab_index(desc, count);
        TabViewResult result = {.selected_index = selected};

        for (size_t index = 0u; index < count; ++index) {
            Id const tab_id = ensure_tab_id(desc, view_id, index);
            bool const active = index == selected;
            BoxDesc tab_desc = desc.tab_box;
            if (tab_desc.layout.width.kind == SizeKind::AUTO) {
                tab_desc.layout.width = children();
            }
            if (tab_desc.layout.height.kind == SizeKind::AUTO) {
                tab_desc.layout.height = fill();
            }
            if (tab_desc.layout.min_width.kind == SizeKind::AUTO && desc.tab_min_width > 0.0f) {
                tab_desc.layout.min_width = px(desc.tab_min_width);
            }
            if (tab_desc.layout.padding.left == 0.0f && tab_desc.layout.padding.right == 0.0f) {
                tab_desc.layout.padding = insets(0.0f, 8.0f);
            }
            if (tab_desc.layout.gap == 0.0f) {
                tab_desc.layout.gap = 4.0f;
            }
            tab_desc.layout.align_y = Align::CENTER;
            if (tab_desc.style.role == StyleRole::AUTO) {
                tab_desc.style.role = active ? StyleRole::ACCENT : StyleRole::CONTROL;
            }

            size_t const tab_index = append_box(
                impl,
                BoxKind::TAB,
                explicit_id(impl, bar_index, BoxKind::TAB, tab_id),
                tab_id,
                {},
                tab_desc,
                true,
                true
            );
            BoxNode& tab = impl->boxes[tab_index];
            Signal const tab_signal = apply_button_activation(impl, tab);
            if (tab_signal.activated) {
                selected = index;
                result.selected_index = selected;
            }
            if (!result.moved && tab_signal.active) {
                size_t to = index;
                if (drag_moves_tab(impl, view_id, desc, index, count, to)) {
                    result.moved = true;
                    result.moved_from = index;
                    result.moved_to = to;
                }
            }

            push_parent(impl, tab_index);
            label(desc.tabs[index].title, {.layout = {.width = text(), .height = fill()}});
            spacer({.layout = {.width = fill(), .height = px(1.0f)}});
            if (tab_flag(desc.flags, TAB_FLAG_CLOSABLE)) {
                Id const close_id = tab_child_id(tab_id, 0xc105e0u);
                Signal const close_signal = button(
                    close_id,
                    "x",
                    {.layout = {
                         .width = px(20.0f),
                         .height = fill(),
                         .padding = insets(0.0f),
                     }}
                );
                if (close_signal.activated && !result.closed) {
                    result.closed = true;
                    result.closed_index = index;
                }
            }
            pop_parent_to(impl, tab_index);
        }

        if (tab_flag(desc.flags, TAB_FLAG_ADDABLE)) {
            Id const add_id = tab_child_id(view_id, 0xadd0u);
            Signal const add_signal = button(
                add_id,
                "+",
                {.layout = {
                     .width = px(std::max(desc.tab_bar_height, 1.0f)),
                     .height = fill(),
                     .padding = insets(0.0f),
                 }}
            );
            if (add_signal.activated) {
                result.added = true;
                result.added_index = count;
            }
        }

        if (result.closed && result.closed_index < count) {
            size_t const next_count = count - 1u;
            selected = selected_after_close(selected, result.closed_index, next_count);
            close_tab(desc.tabs, desc.tab_count, result.closed_index);
            count = desc.tab_count != nullptr ? tab_count(desc) : next_count;
        } else if (result.moved) {
            selected = selected_after_move(selected, result.moved_from, result.moved_to);
            move_tab(desc.tabs, result.moved_from, result.moved_to);
        } else if (result.added && desc.tab_count != nullptr &&
                   *desc.tab_count < desc.tabs.size()) {
            size_t const index = *desc.tab_count;
            TabItem item = desc.new_tab;
            if (item.id.value == 0u) {
                item.id = tab_child_id(view_id, index + 1u);
            }
            if (item.title.empty()) {
                item.title = "New Tab";
            }
            desc.tabs[index] = item;
            *desc.tab_count += 1u;
            count = *desc.tab_count;
            selected = index;
        }

        selected = clamp_tab_index(selected, count);
        result.selected_index = selected;
        if (desc.selected_index != nullptr) {
            *desc.selected_index = selected;
        }

        pop_parent_to(impl, bar_index);

        Id const body_id = tab_child_id(view_id, 0xb0d7u);
        BoxDesc body_desc = desc.body_box;
        if (body_desc.layout.width.kind == SizeKind::AUTO) {
            body_desc.layout.width = fill();
        }
        if (body_desc.layout.height.kind == SizeKind::AUTO) {
            body_desc.layout.height = fill();
        }
        if (body_desc.layout.align_x == Align::START) {
            body_desc.layout.align_x = Align::STRETCH;
        }
        body_desc.layout.clip = true;
        size_t const body_index = append_box(
            impl,
            BoxKind::TAB_BODY,
            explicit_id(impl, view_index, BoxKind::TAB_BODY, body_id),
            body_id,
            {},
            body_desc,
            false,
            false
        );
        push_parent(impl, body_index);
        return TabViewScope(Scope(this, view_index), Scope(this, body_index), result);
    }

    auto Frame::spacer(BoxDesc const& desc) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BASE_UNUSED(append_box(impl,
                               BoxKind::SPACER,
                               structural_id(impl, parent, BoxKind::SPACER),
                               {},
                               {},
                               desc,
                               false,
                               false));
    }

    auto Frame::spacer(float size) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        BoxNode const& parent = impl->boxes[top_parent_index(impl)];
        BoxDesc desc = {};
        if (parent.kind == BoxKind::ROW) {
            desc.layout.width = px(size);
            desc.layout.height = px(0.0f);
        } else {
            desc.layout.width = px(0.0f);
            desc.layout.height = px(size);
        }
        spacer(desc);
    }

    auto Frame::label(StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::LABEL,
                                        text_id(impl, parent, BoxKind::LABEL, text_value),
                                        {},
                                        text_value,
                                        desc,
                                        false,
                                        false);
        return impl->boxes[index].signal;
    }

    auto Frame::label(Id id_value, StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::LABEL,
                                        explicit_id(impl, parent, BoxKind::LABEL, id_value),
                                        id_value,
                                        text_value,
                                        desc,
                                        false,
                                        false);
        return impl->boxes[index].signal;
    }

    auto Frame::selectable_label(StrRef text_value,
                                 TextSelection* selection,
                                 BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc label_desc = desc;
        label_desc.layout.clip = true;
        label_desc.layout.scroll_y = true;
        size_t const index = append_box(impl,
                                        BoxKind::SELECTABLE_LABEL,
                                        text_id(impl, parent, BoxKind::SELECTABLE_LABEL, text_value),
                                        {},
                                        text_value,
                                        label_desc,
                                        true,
                                        false);
        impl->boxes[index].scroll_state = impl->boxes[index].state;
        return apply_selectable_label(impl, impl->boxes[index], selection);
    }

    auto Frame::selectable_label(Id id_value,
                                 StrRef text_value,
                                 TextSelection* selection,
                                 BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc label_desc = desc;
        label_desc.layout.clip = true;
        label_desc.layout.scroll_y = true;
        size_t const index =
            append_box(impl,
                       BoxKind::SELECTABLE_LABEL,
                       explicit_id(impl, parent, BoxKind::SELECTABLE_LABEL, id_value),
                       id_value,
                       text_value,
                       label_desc,
                       true,
                       false);
        set_scroll_state(impl, impl->boxes[index], id_value);
        return apply_selectable_label(impl, impl->boxes[index], selection);
    }

    auto Frame::button(StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::BUTTON,
                                        text_id(impl, parent, BoxKind::BUTTON, text_value),
                                        {},
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_button_activation(impl, impl->boxes[index]);
    }

    auto Frame::button(Id id_value, StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::BUTTON,
                                        explicit_id(impl, parent, BoxKind::BUTTON, id_value),
                                        id_value,
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_button_activation(impl, impl->boxes[index]);
    }

    auto Frame::checkbox(StrRef text_value, bool* value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::CHECKBOX,
                                        text_id(impl, parent, BoxKind::CHECKBOX, text_value),
                                        {},
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_bool_widget(impl, impl->boxes[index], value);
    }

    auto Frame::checkbox(Id id_value, StrRef text_value, bool* value, BoxDesc const& desc)
        -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::CHECKBOX,
                                        explicit_id(impl, parent, BoxKind::CHECKBOX, id_value),
                                        id_value,
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_bool_widget(impl, impl->boxes[index], value);
    }

    auto Frame::toggle(StrRef text_value, bool* value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TOGGLE,
                                        text_id(impl, parent, BoxKind::TOGGLE, text_value),
                                        {},
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_bool_widget(impl, impl->boxes[index], value);
    }

    auto Frame::toggle(Id id_value, StrRef text_value, bool* value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::TOGGLE,
                                        explicit_id(impl, parent, BoxKind::TOGGLE, id_value),
                                        id_value,
                                        text_value,
                                        desc,
                                        true,
                                        true);
        return apply_bool_widget(impl, impl->boxes[index], value);
    }

    auto Frame::slider_float(StrRef text_value, float* value, SliderFloatDesc const& desc)
        -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::SLIDER_FLOAT,
                                        text_id(impl, parent, BoxKind::SLIDER_FLOAT, text_value),
                                        {},
                                        text_value,
                                        desc.box,
                                        true,
                                        true);
        return apply_slider_widget(impl, impl->boxes[index], value, desc);
    }

    auto
    Frame::slider_float(Id id_value, StrRef text_value, float* value, SliderFloatDesc const& desc)
        -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::SLIDER_FLOAT,
                                        explicit_id(impl, parent, BoxKind::SLIDER_FLOAT, id_value),
                                        id_value,
                                        text_value,
                                        desc.box,
                                        true,
                                        true);
        return apply_slider_widget(impl, impl->boxes[index], value, desc);
    }

    auto Frame::input_text(StrRef label, char* buffer, size_t buffer_size, BoxDesc const& desc)
        -> Signal {
        ASSERT(buffer != nullptr);
        ASSERT(buffer_size > 0u);

        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc box_desc = desc;
        box_desc.layout.clip = true;
        size_t const index =
            append_box(impl,
                       BoxKind::INPUT_TEXT,
                       text_id(impl, parent, BoxKind::INPUT_TEXT, label),
                       {},
                       {buffer, text_buffer_size(buffer, buffer_size)},
                       box_desc,
                       true,
                       true);
        BoxNode& box = impl->boxes[index];
        box.id_source = label.empty() ? BoxIdSource::STRUCTURAL : BoxIdSource::TEXT;
        box.stable_id = false;
        TextEditBuffer edit = fixed_text_edit_buffer(buffer, buffer_size);
        return apply_input_text_widget(impl, box, edit);
    }

    auto Frame::input_text(Id id_value,
                           StrRef label,
                           char* buffer,
                           size_t buffer_size,
                           BoxDesc const& desc) -> Signal {
        ASSERT(buffer != nullptr);
        ASSERT(buffer_size > 0u);

        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc box_desc = desc;
        box_desc.layout.clip = true;
        size_t const index =
            append_box(impl,
                       BoxKind::INPUT_TEXT,
                       explicit_id(impl, parent, BoxKind::INPUT_TEXT, id_value),
                       id_value,
                       {buffer, text_buffer_size(buffer, buffer_size)},
                       box_desc,
                       true,
                       true);
        BASE_UNUSED(label);
        TextEditBuffer edit = fixed_text_edit_buffer(buffer, buffer_size);
        return apply_input_text_widget(impl, impl->boxes[index], edit);
    }

    auto Frame::input_text_multiline(StrRef label,
                                     StringBuffer* buffer,
                                     InputTextMultilineDesc const& desc) -> Signal {
        ASSERT(buffer != nullptr);

        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc box_desc = desc.box;
        box_desc.layout.clip = true;
        box_desc.layout.scroll_y = true;
        size_t const index =
            append_box(impl,
                       BoxKind::INPUT_TEXT_MULTILINE,
                       text_id(impl, parent, BoxKind::INPUT_TEXT_MULTILINE, label),
                       {},
                       buffer->str(),
                       box_desc,
                       true,
                       true);
        BoxNode& box = impl->boxes[index];
        box.id_source = label.empty() ? BoxIdSource::STRUCTURAL : BoxIdSource::TEXT;
        box.stable_id = false;
        box.scroll_state = box.state;
        TextEditBuffer edit = string_text_edit_buffer(buffer);
        return apply_input_text_widget(impl, box, edit, desc.tab_text);
    }

    auto Frame::input_text_multiline(Id id_value,
                                     StrRef label,
                                     StringBuffer* buffer,
                                     InputTextMultilineDesc const& desc) -> Signal {
        ASSERT(buffer != nullptr);

        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc box_desc = desc.box;
        box_desc.layout.clip = true;
        box_desc.layout.scroll_y = true;
        size_t const index =
            append_box(impl,
                       BoxKind::INPUT_TEXT_MULTILINE,
                       explicit_id(impl, parent, BoxKind::INPUT_TEXT_MULTILINE, id_value),
                       id_value,
                       buffer->str(),
                       box_desc,
                       true,
                       true);
        set_scroll_state(impl, impl->boxes[index], id_value);
        TextEditBuffer edit = string_text_edit_buffer(buffer);
        BASE_UNUSED(label);
        return apply_input_text_widget(impl, impl->boxes[index], edit, desc.tab_text);
    }

    auto Frame::list_fixed(Id id_value, ListFixedDesc const& desc) -> ListScope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BoxDesc box_desc = desc.box;
        box_desc.layout.clip = true;
        box_desc.layout.scroll_y = true;
        size_t const index = append_box(impl,
                                        BoxKind::LIST,
                                        explicit_id(impl, parent, BoxKind::LIST, id_value),
                                        id_value,
                                        {},
                                        box_desc,
                                        false,
                                        false);

        BoxNode& list = impl->boxes[index];
        set_scroll_state(impl, list, id_value);
        float viewport_height = rect_height(list.scroll_state->rect);
        if (viewport_height <= 0.0f && box_desc.layout.height.kind == SizeKind::PIXELS) {
            viewport_height = box_desc.layout.height.value;
        }
        if (viewport_height <= 0.0f) {
            viewport_height = impl->frame_desc.size.y;
        }

        float const item_height = std::max(desc.item_height, 1.0f);
        float const content_height = static_cast<float>(desc.item_count) * item_height;
        float const max_scroll = std::max(0.0f, content_height - viewport_height);
        if (list.scroll_state->last_frame != 0u) {
            apply_scroll_delta(impl, list.scroll_state, list.scroll_state->rect);
        }
        consume_list_scroll_request(
            list.scroll_state, desc.item_count, item_height, viewport_height, max_scroll);
        list.scroll_state->scroll_y = std::clamp(list.scroll_state->scroll_y, 0.0f, max_scroll);
        if (list.scroll_state->last_frame != 0u) {
            apply_scrollbar_input(impl,
                                  list.scroll_state,
                                  list.scroll_state->rect,
                                  0.0f,
                                  viewport_height,
                                  content_height,
                                  max_scroll);
        }
        list.scroll_state->scroll_y = std::clamp(list.scroll_state->scroll_y, 0.0f, max_scroll);
        list.scroll_state->scroll_max_y = max_scroll;
        list.scroll_state->scroll_viewport_height = viewport_height;
        list.scroll_state->scroll_content_height = content_height;
        list.scroll_state->scroll_valid = true;
        list.scroll_content_height = content_height;

        size_t first = static_cast<size_t>(list.scroll_state->scroll_y / item_height);
        first = std::min(first, desc.item_count);
        size_t const visible_count =
            static_cast<size_t>(std::ceil(viewport_height / item_height)) + 1u;
        size_t const end = std::min(desc.item_count, first + visible_count);
        list.scroll_offset_y =
            -(list.scroll_state->scroll_y - static_cast<float>(first) * item_height);

        push_parent(impl, index);
        return {Scope(this, index), first, end, item_height};
    }

    auto Frame::scroll_state(Id id_value) const -> ScrollState {
        ContextImpl const* const impl = impl_from_frame(*this);
        StateEntry const* const state = find_state_entry(impl, scroll_state_key(id_value));
        if (state == nullptr || !state->scroll_valid) {
            return {};
        }
        return {state->scroll_y,
                state->scroll_max_y,
                state->scroll_viewport_height,
                state->scroll_content_height,
                true};
    }

    auto Frame::set_scroll_y(Id id_value, float y) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        Id const scroll_key = scroll_state_key(id_value);
        if (impl == nullptr || scroll_key.value == 0u) {
            return;
        }
        StateEntry* const state = state_entry(impl, scroll_key);
        state->scroll_request_y = y;
        state->scroll_request_set = true;
        state->scroll_request_end = false;
        state->scroll_request_index_set = false;
    }

    auto Frame::scroll_to_end(Id id_value) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        Id const scroll_key = scroll_state_key(id_value);
        if (impl == nullptr || scroll_key.value == 0u) {
            return;
        }
        StateEntry* const state = state_entry(impl, scroll_key);
        state->scroll_request_set = false;
        state->scroll_request_end = true;
        state->scroll_request_index_set = false;
    }

    auto Frame::scroll_to_index(Id id_value, size_t index, ScrollReveal reveal) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        Id const scroll_key = scroll_state_key(id_value);
        if (impl == nullptr || scroll_key.value == 0u) {
            return;
        }
        StateEntry* const state = state_entry(impl, scroll_key);
        state->scroll_request_index = index;
        state->scroll_request_reveal = reveal;
        state->scroll_request_set = false;
        state->scroll_request_end = false;
        state->scroll_request_index_set = true;
    }

    auto Frame::request_focus(Id id_value) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        if (impl != nullptr) {
            impl->focus_request_id = id_value;
        }
    }

    auto Frame::clear_focus() -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        if (impl != nullptr) {
            impl->focused_id = {};
            impl->focus_request_id = {};
        }
    }

    auto Frame::box_info_count() const -> size_t {
        ContextImpl const* const impl = impl_from_frame(*this);
        return impl != nullptr ? impl->box_count : 0u;
    }

    auto Frame::box_info(size_t index) const -> BoxInfo const* {
        ContextImpl const* const impl = impl_from_frame(*this);
        if (impl == nullptr || index >= impl->box_count) {
            return nullptr;
        }
        return impl->infos + index;
    }

    auto Frame::find_box(Id id_value) const -> BoxInfo const* {
        ContextImpl const* const impl = impl_from_frame(*this);
        if (impl == nullptr || id_value.value == 0u) {
            return nullptr;
        }
        for (size_t index = 0u; index < impl->box_count; ++index) {
            if (impl->infos[index].id.value == id_value.value) {
                return impl->infos + index;
            }
        }
        for (size_t index = 0u; index < impl->box_count; ++index) {
            if (impl->infos[index].authored_id.value == id_value.value) {
                return impl->infos + index;
            }
        }
        return nullptr;
    }

    auto Frame::find_box(Id id_value, BoxKind kind) const -> BoxInfo const* {
        BoxInfo const* const info = find_box(id_value);
        return info != nullptr && info->kind == kind ? info : nullptr;
    }

    auto Frame::hit_test(Vec2 point) const -> BoxInfo const* {
        ContextImpl const* const impl = impl_from_frame(*this);
        if (impl == nullptr) {
            return nullptr;
        }
        for (uint32_t pass = 0u; pass < 2u; ++pass) {
            bool const top_layer = pass == 0u;
            for (size_t index = impl->box_count; index > 0u; --index) {
                size_t const box_index = index - 1u;
                BoxNode const& box = impl->boxes[box_index];
                if (top_layer_box(impl, box_index) != top_layer) {
                    continue;
                }
                if (rect_contains(box.rect, point) && hit_passes_clips(impl, box_index, point)) {
                    return impl->infos + box_index;
                }
            }
        }
        return nullptr;
    }

    auto begin_frame(Context context, FrameDesc const& desc) -> Frame {
        ContextImpl* const impl = impl_from_context(context);
        ASSERT(impl != nullptr);
        impl->frame_desc = desc;
        impl->frame_start_focus_id = impl->focused_id;
        impl->hot_id = compute_hot_id(impl);
        process_focus_keys(impl);
        impl->frame_arena.reset();
        impl->box_count = 0u;
        impl->parent_stack_count = 0u;
        impl->focus_order_count = 0u;
        impl->frame_index += 1u;
        impl->building = true;

        BoxNode* const root = impl->boxes;
        *root = {};
        root->first_child = INVALID_INDEX;
        root->last_child = INVALID_INDEX;
        root->next_sibling = INVALID_INDEX;
        root->kind = BoxKind::ROOT;
        root->parent_index = INVALID_INDEX;
        root->id = id("root");
        root->stable_id = true;
        root->layout.width = px(desc.size.x);
        root->layout.height = px(desc.size.y);
        root->resolved_style.foreground = rgb(255, 255, 255);
        root->resolved_style.opacity = 1.0f;
        root->resolved_style.font_size = 16.0f;
        root->state = state_entry(impl, root->id);
        impl->box_count = 1u;
        push_parent(impl, 0u);
        return Frame(impl);
    }

    auto end_frame(Frame& frame) -> void {
        ContextImpl* const impl = impl_from_frame(frame);
        ASSERT(impl != nullptr);
        StyleDesc root_style = {};
        root_style.foreground = rgb(255, 255, 255);
        root_style.opacity = 1.0f;
        root_style.font_size = 16.0f;
        resolve_styles(impl, 0u, root_style);
        BASE_UNUSED(measure_node(impl, 0u));
        layout_node(impl, 0u, {{0.0f, 0.0f}, {impl->frame_desc.size.x, impl->frame_desc.size.y}});
        publish_infos(impl);
        if (!impl->frame_desc.input.mouse_down[0u]) {
            impl->active_id = {};
            impl->active_scroll_id = {};
        }
        impl->previous_input = impl->frame_desc.input;
        impl->parent_stack_count = 1u;
        impl->building = false;
    }

    auto render_frame(Frame const& frame, draw::Context draw_context) -> void {
        ContextImpl const* const impl = impl_from_frame(frame);
        if (impl == nullptr || !draw::context_valid(draw_context) || impl->box_count == 0u) {
            return;
        }
        render_box(impl, draw_context, 0u);
        render_scrollbars(impl, draw_context, 0u);
        render_floating_boxes(impl, draw_context, 0u);
        render_floating_scrollbars(impl, draw_context, 0u);
    }

} // namespace gui
