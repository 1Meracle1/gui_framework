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

        struct StateEntry {
            Id id = {};
            Rect rect = {};
            uint64_t last_frame = 0u;
            float scroll_y = 0.0f;
            float scroll_max_y = 0.0f;
            float scroll_viewport_height = 0.0f;
            float scroll_content_height = 0.0f;
            float scroll_request_y = 0.0f;
            size_t scroll_request_index = 0u;
            size_t text_selection_anchor = 0u;
            size_t text_selection_word_start = 0u;
            size_t text_selection_word_end = 0u;
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

        struct TextLine {
            StrRef text = {};
            size_t start = 0u;
            size_t end = 0u;
        };

        struct ContextImpl {
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
            void* clipboard_user_data = nullptr;
            Id hot_id = {};
            Id active_id = {};
            Id focused_id = {};
            Id frame_start_focus_id = {};
            Id focus_request_id = {};
            Id text_selection_owner_id = {};
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
                   box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::LIST;
        }

        auto apply_scroll_delta(ContextImpl* impl, StateEntry* state, Rect rect) -> void {
            if (state == nullptr || impl->frame_desc.input.scroll_delta_y == 0.0f ||
                !rect_contains(rect, impl->frame_desc.input.mouse_pos)) {
                return;
            }
            state->scroll_y =
                std::max(0.0f, state->scroll_y - impl->frame_desc.input.scroll_delta_y);
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
            return std::ceil(metrics.ascent + metrics.descent + 4.0f);
        }

        [[nodiscard]] auto text_line_height(BoxNode const& box) -> float {
            float const font_size = text_font_size(box);
            font_cache::Font const font = text_font(box);
            return font_cache::font_valid(font)
                       ? rendered_text_height(font, font_size)
                       : font_size * 1.25f;
        }

        [[nodiscard]] auto next_text_line(StrRef text, size_t& offset, TextLine& out_line)
            -> bool {
            if (text.empty() || offset >= text.size()) {
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
                float const font_size = text_font_size(box);
                return {0.0f, font_size * 1.25f};
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

        [[nodiscard]] auto text_position(BoxNode const& box, Rect rect) -> Vec2 {
            Vec2 const text_dim = text_size(box);
            float const content_width =
                std::max(0.0f, rect_width(rect) - inset_width(box.layout.padding));
            float const x_offset = text_x_offset(box, content_width, text_dim.x);
            return {
                rect.min.x + box.layout.padding.left + x_offset,
                rect.min.y + box.layout.padding.top +
                    std::max(0.0f,
                             rect_height(rect) - inset_height(box.layout.padding) - text_dim.y) *
                        0.5f,
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

        [[nodiscard]] auto copy_shortcut_pressed(ContextImpl const* impl) -> bool {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr) {
                return false;
            }
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                KeyEvent const& event = input.key_events[index];
                if (event.key == Key::C && event.kind == KeyEventKind::PRESS &&
                    (event.mods & KEY_MOD_CTRL) != 0u) {
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

        auto process_focus_keys(ContextImpl* impl) -> void {
            InputState const& input = impl->frame_desc.input;
            if (input.key_events == nullptr) {
                return;
            }
            for (size_t index = 0u; index < input.key_event_count; ++index) {
                KeyEvent const& event = input.key_events[index];
                if (event.key == Key::TAB &&
                    (event.kind == KeyEventKind::PRESS || event.kind == KeyEventKind::REPEAT)) {
                    move_focus(impl, tab_reverse(event));
                }
            }
        }

        [[nodiscard]] auto compute_hot_id(ContextImpl const* impl) -> Id {
            Vec2 const mouse_pos = impl->frame_desc.input.mouse_pos;
            for (size_t index = impl->box_count; index > 0u; --index) {
                BoxNode const& box = impl->boxes[index - 1u];
                if (box.interactive && !box_disabled(box) && box.state != nullptr &&
                    box.state->last_frame != 0u && rect_contains(box.state->rect, mouse_pos)) {
                    return box.id;
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
                                                                             : 0.0f;
            float const widget_min_y = box.kind == BoxKind::CHECKBOX ||
                                               box.kind == BoxKind::TOGGLE ||
                                               box.kind == BoxKind::SLIDER_FLOAT
                                           ? 20.0f
                                           : 0.0f;

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
                if (box.kind == BoxKind::ROW) {
                    if (box.layout.width.kind == SizeKind::AUTO ||
                        box.layout.width.kind == SizeKind::CHILDREN) {
                        size.x = total_x + gaps + inset_width(box.layout.padding);
                    }
                    if (box.layout.height.kind == SizeKind::AUTO ||
                        box.layout.height.kind == SizeKind::CHILDREN) {
                        size.y = max_y + inset_height(box.layout.padding);
                    }
                } else if (box.kind == BoxKind::COLUMN || box.kind == BoxKind::LIST ||
                           box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::ROOT) {
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
                total += child_axis_size(child, axis) + child_margin_main(child, axis);
                child_count += 1u;
            }
            if (child_count > 1u) {
                total += box.layout.gap * static_cast<float>(child_count - 1u);
            }
            return total;
        }

        [[nodiscard]] auto align_offset(Align align, float available, float size) -> float {
            float const free_space = std::max(0.0f, available - size);
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

        auto update_scroll_metrics(ContextImpl* impl,
                                   StateEntry* state,
                                   Rect rect,
                                   float content_height,
                                   bool apply_input,
                                   bool consume_request) -> void {
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
            state->scroll_max_y = max_y;
            state->scroll_viewport_height = viewport_height;
            state->scroll_content_height = scroll_content_height;
            state->scroll_valid = true;
            state->rect = rect;
            state->last_frame = impl->frame_index;
        }

        auto layout_children(ContextImpl* impl, size_t index, Axis axis) -> void;
        auto layout_overlay(ContextImpl* impl, size_t index) -> void;

        auto layout_node(ContextImpl* impl, size_t index, Rect rect) -> void {
            BoxNode& box = impl->boxes[index];
            box.rect = rect;
            if (box.kind == BoxKind::SCROLL_PANEL) {
                float const content_height =
                    stack_content_main_size(impl, box, Axis::Y) + inset_height(box.layout.padding);
                update_scroll_metrics(impl, box.scroll_state, rect, content_height, true, true);
                if (box.scroll_state != nullptr) {
                    box.scroll_offset_y = -box.scroll_state->scroll_y;
                }
            } else if (box.kind == BoxKind::LIST) {
                update_scroll_metrics(
                    impl, box.scroll_state, rect, box.scroll_content_height, false, false);
            }
            if (box.first_child == INVALID_INDEX) {
                return;
            }

            if (box.kind == BoxKind::ROW) {
                layout_children(impl, index, Axis::X);
            } else if (box.kind == BoxKind::COLUMN || box.kind == BoxKind::ROOT ||
                       box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::LIST) {
                layout_children(impl, index, Axis::Y);
            } else {
                layout_overlay(impl, index);
            }
        }

        auto layout_overlay(ContextImpl* impl, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            Rect const content = content_rect(box.rect, box.layout.padding);
            for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                 child_index = impl->boxes[child_index].next_sibling) {
                BoxNode& child = impl->boxes[child_index];
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
        copy_selected_text(ContextImpl const* impl, BoxNode const& box, TextSelection selection)
            -> void {
            if (impl->set_clipboard_text == nullptr || selection.start == selection.end) {
                return;
            }
            impl->set_clipboard_text(
                impl->clipboard_user_data,
                box.text.substr(selection.start, selection.end - selection.start)
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

        auto apply_selectable_label(ContextImpl* impl, BoxNode& box, TextSelection* selection)
            -> Signal {
            ASSERT(selection != nullptr);

            TextSelection const previous = *selection;
            TextSelection next =
                ordered_text_selection(clamp_text_selection(previous, box.text.size()));
            Signal signal = box.signal;

            if (box.state != nullptr) {
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
                            bool const clicked_selected_text = cursor == anchor &&
                                                               previous.start < cursor &&
                                                               cursor < previous.end;
                            if (!clicked_selected_text) {
                                next = cursor == anchor ? TextSelection{cursor, cursor}
                                                        : ordered_text_selection({anchor, cursor});
                            }
                        }
                    }
                }
            }

            next = ordered_text_selection(clamp_text_selection(next, box.text.size()));
            signal.changed = previous.start != next.start || previous.end != next.end;
            if (next.start != next.end && impl->text_selection_owner_id.value == 0u) {
                impl->text_selection_owner_id = box.id;
            } else if (next.start == next.end &&
                       impl->text_selection_owner_id.value == box.id.value) {
                impl->text_selection_owner_id = {};
            }
            if (impl->text_selection_owner_id.value == box.id.value &&
                copy_shortcut_pressed(impl)) {
                copy_selected_text(impl, box, next);
            }
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
            }
        }

        auto render_text_selection(ContextImpl const* impl,
                                   BoxNode const& box,
                                   draw::Context draw_context) -> void {
            if (box.kind != BoxKind::SELECTABLE_LABEL || box.text.empty()) {
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

        [[nodiscard]] auto hit_passes_clips(ContextImpl const* impl, size_t index, Vec2 point)
            -> bool {
            for (size_t parent = impl->boxes[index].parent_index; parent != INVALID_INDEX;
                 parent = impl->boxes[parent].parent_index) {
                BoxNode const& box = impl->boxes[parent];
                if (box_clips(box) && !rect_contains(box.rect, point)) {
                    return false;
                }
            }
            return true;
        }

        auto render_box(ContextImpl const* impl, draw::Context draw_context, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = box_clips(box);
            if (clips) {
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
                                       box.kind == BoxKind::SLIDER_FLOAT;
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
                            draw::draw_text(draw_context,
                                            {text_pos.x,
                                             text_pos.y +
                                                 line_height * static_cast<float>(line_index)},
                                            text_style,
                                            line.text,
                                            nullptr);
                        }
                        line_index += 1u;
                    }
                }
            }

            for (size_t child = box.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                render_box(impl, draw_context, child);
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
        theme_kind(theme, BoxKind::LABEL).role = StyleRole::TEXT;
        theme_kind(theme, BoxKind::SELECTABLE_LABEL).role = StyleRole::TEXT;
        theme_kind(theme, BoxKind::BUTTON).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::CHECKBOX).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::TOGGLE).role = StyleRole::CONTROL;
        theme_kind(theme, BoxKind::SLIDER_FLOAT).role = StyleRole::CONTROL;
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
        Id const scroll_key = scroll_state_key(id_value);
        panel.scroll_state = scroll_key.value != 0u ? state_entry(impl, scroll_key) : panel.state;
        push_parent(impl, index);
        return {this, index};
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
        size_t const index = append_box(impl,
                                        BoxKind::SELECTABLE_LABEL,
                                        text_id(impl, parent, BoxKind::SELECTABLE_LABEL, text_value),
                                        {},
                                        text_value,
                                        desc,
                                        true,
                                        false);
        return apply_selectable_label(impl, impl->boxes[index], selection);
    }

    auto Frame::selectable_label(Id id_value,
                                 StrRef text_value,
                                 TextSelection* selection,
                                 BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index =
            append_box(impl,
                       BoxKind::SELECTABLE_LABEL,
                       explicit_id(impl, parent, BoxKind::SELECTABLE_LABEL, id_value),
                       id_value,
                       text_value,
                       desc,
                       true,
                       false);
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
        Id const scroll_key = scroll_state_key(id_value);
        list.scroll_state = scroll_key.value != 0u ? state_entry(impl, scroll_key) : list.state;
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
        for (size_t index = impl->box_count; index > 0u; --index) {
            size_t const box_index = index - 1u;
            BoxNode const& box = impl->boxes[box_index];
            if (rect_contains(box.rect, point) && hit_passes_clips(impl, box_index, point)) {
                return impl->infos + box_index;
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
    }

} // namespace gui
