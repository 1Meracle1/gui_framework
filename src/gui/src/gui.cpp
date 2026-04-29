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
            LayoutDesc layout = {};
            StyleDesc style = {};
            StyleDesc resolved_style = {};
            Rect rect = {};
            Vec2 measured_size = {};
            float scroll_offset_y = 0.0f;
            Signal signal = {};
            BoxFlags flags = BOX_FLAG_NONE;
            bool duplicate_id = false;
            StateEntry* state = nullptr;
        };

        struct ContextImpl {
            Arena frame_arena = {};
            BoxNode* boxes = nullptr;
            BoxInfo* infos = nullptr;
            size_t* parent_stack = nullptr;
            StateEntry* state_table = nullptr;
            size_t box_capacity = 0u;
            size_t state_table_size = 0u;
            size_t box_count = 0u;
            size_t parent_stack_count = 0u;
            FrameDesc frame_desc = {};
            InputState previous_input = {};
            Id active_id = {};
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

        [[nodiscard]] auto color_or(Color value, Color fallback) -> Color {
            return color_set(value) ? value : fallback;
        }

        [[nodiscard]] auto color_mul_alpha(Color value, float opacity) -> Color {
            if (color_set(value)) {
                value.a *= opacity;
            }
            return value;
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

        auto apply_scroll_delta(ContextImpl* impl, BoxNode& box) -> void {
            if (box.state == nullptr || box.state->last_frame == 0u ||
                impl->frame_desc.input.scroll_delta_y == 0.0f ||
                !rect_contains(box.state->rect, impl->frame_desc.input.mouse_pos)) {
                return;
            }
            box.state->scroll_y =
                std::max(0.0f, box.state->scroll_y - impl->frame_desc.input.scroll_delta_y);
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

        [[nodiscard]] auto text_size(BoxNode const& box) -> Vec2 {
            float const font_size =
                box.resolved_style.font_size > 0.0f ? box.resolved_style.font_size : 16.0f;
            return {static_cast<float>(box.text.size()) * font_size * 0.5f, font_size * 1.25f};
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

        [[nodiscard]] auto make_signal(ContextImpl* impl, BoxNode const& box) -> Signal {
            Signal result = {};
            if (box.state == nullptr || box.state->last_frame == 0u ||
                (box.flags & BOX_FLAG_DISABLED) != 0u) {
                return result;
            }

            Rect const rect = box.state->rect;
            bool const hovered = rect_contains(rect, impl->frame_desc.input.mouse_pos);
            bool const mouse_down = impl->frame_desc.input.mouse_down[0u];
            bool const previous_mouse_down = impl->previous_input.mouse_down[0u];

            result.hovered = hovered;
            result.active = impl->active_id.value == box.id.value;
            result.pressed_left = hovered && mouse_down && !previous_mouse_down;
            result.released_left =
                previous_mouse_down && !mouse_down && impl->active_id.value == box.id.value;
            result.clicked_left = result.released_left && hovered;

            if (result.pressed_left) {
                impl->active_id = box.id;
                result.active = true;
            }
            if (result.released_left) {
                impl->active_id = {};
                result.active = false;
            }
            return result;
        }

        [[nodiscard]] auto append_box(ContextImpl* impl,
                                      BoxKind kind,
                                      Id id_value,
                                      StrRef text,
                                      BoxDesc const& desc,
                                      bool interactive) -> size_t {
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
            box->flags = desc.flags;
            box->id = id_value;
            box->state = state_entry(impl, box->id);

            for (size_t child = parent.first_child; child != INVALID_INDEX;
                 child = impl->boxes[child].next_sibling) {
                if (impl->boxes[child].id.value == box->id.value) {
                    box->duplicate_id = true;
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

            if (interactive) {
                box->signal = make_signal(impl, *box);
            }

            return box_index;
        }

        auto resolve_styles(ContextImpl* impl, size_t index, StyleDesc const& parent_style)
            -> void {
            BoxNode& box = impl->boxes[index];
            StyleDesc resolved = box.style;
            resolved.foreground = color_or(resolved.foreground, parent_style.foreground);
            resolved.font =
                font_cache::font_valid(resolved.font) ? resolved.font : parent_style.font;
            resolved.font_size =
                resolved.font_size > 0.0f ? resolved.font_size : parent_style.font_size;
            resolved.opacity = parent_style.opacity * resolved.opacity;
            if (box.kind == BoxKind::BUTTON && !color_set(resolved.background)) {
                if (box.signal.active) {
                    resolved.background = rgb(64, 68, 78);
                } else if (box.signal.hovered) {
                    resolved.background = rgb(54, 58, 68);
                } else {
                    resolved.background = rgb(42, 46, 56);
                }
            }
            if (box.kind == BoxKind::BUTTON && resolved.radius < 0.0f) {
                resolved.radius = 4.0f;
            }
            if (resolved.radius < 0.0f) {
                resolved.radius = 0.0f;
            }
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
                measure_node(impl, child);
            }

            Vec2 size = {};
            Vec2 const text_dim = text_size(box);
            bool const is_leaf = box.first_child == INVALID_INDEX;

            if (box.layout.width.kind == SizeKind::PIXELS) {
                size.x = box.layout.width.value;
            } else if (box.layout.width.kind == SizeKind::TEXT ||
                       (box.layout.width.kind == SizeKind::AUTO && is_leaf)) {
                size.x = text_dim.x + inset_width(box.layout.padding);
            }

            if (box.layout.height.kind == SizeKind::PIXELS) {
                size.y = box.layout.height.value;
            } else if (box.layout.height.kind == SizeKind::TEXT ||
                       (box.layout.height.kind == SizeKind::AUTO && is_leaf)) {
                size.y = text_dim.y + inset_height(box.layout.padding);
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

        auto layout_children(ContextImpl* impl, size_t index, Axis axis) -> void;

        auto layout_node(ContextImpl* impl, size_t index, Rect rect) -> void {
            BoxNode& box = impl->boxes[index];
            box.rect = rect;
            if (box.first_child == INVALID_INDEX) {
                return;
            }

            if (box.kind == BoxKind::ROW) {
                layout_children(impl, index, Axis::X);
            } else if (box.kind == BoxKind::COLUMN || box.kind == BoxKind::ROOT ||
                       box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::LIST) {
                layout_children(impl, index, Axis::Y);
            } else {
                Rect const content = content_rect(rect, box.layout.padding);
                for (size_t child_index = box.first_child; child_index != INVALID_INDEX;
                     child_index = impl->boxes[child_index].next_sibling) {
                    BoxNode const& child = impl->boxes[child_index];
                    Rect child_rect = content;
                    child_rect.min.x += child.layout.margin.left;
                    child_rect.min.y += child.layout.margin.top;
                    child_rect.max.x -= child.layout.margin.right;
                    child_rect.max.y -= child.layout.margin.bottom;
                    layout_node(impl, child_index, child_rect);
                }
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
            float cursor = axis == Axis::X ? content.min.x : content.min.y + box.scroll_offset_y;
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
                info.kind = box.kind;
                info.text = box.text;
                info.debug_name = box.debug_name;
                info.rect = box.rect;
                info.depth = box.depth;
                info.flags = box.flags;
                info.layout = box.layout;
                info.style = box.resolved_style;
                info.duplicate_id = box.duplicate_id;
                if (box.state != nullptr) {
                    box.state->rect = box.rect;
                    box.state->last_frame = impl->frame_index;
                }
            }
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

        auto render_box(ContextImpl const* impl, draw::Context draw_context, size_t index) -> void {
            BoxNode const& box = impl->boxes[index];
            bool const clips = box.layout.clip || box.layout.scroll_x || box.layout.scroll_y ||
                               box.kind == BoxKind::SCROLL_PANEL || box.kind == BoxKind::LIST;
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

                if ((box.kind == BoxKind::LABEL || box.kind == BoxKind::BUTTON) &&
                    font_cache::font_valid(box.resolved_style.font)) {
                    draw::TextStyle text_style = {};
                    text_style.font = box.resolved_style.font;
                    text_style.size =
                        box.resolved_style.font_size > 0.0f ? box.resolved_style.font_size : 16.0f;
                    text_style.color = to_draw_color(
                        color_mul_alpha(box.resolved_style.foreground, box.resolved_style.opacity));
                    Vec2 const text_dim = text_size(box);
                    Vec2 const text_pos = {
                        box.rect.min.x + box.layout.padding.left,
                        box.rect.min.y + box.layout.padding.top +
                            std::max(0.0f,
                                     rect_height(box.rect) - inset_height(box.layout.padding) -
                                         text_dim.y) *
                                0.5f,
                    };
                    draw::draw_text(
                        draw_context, {text_pos.x, text_pos.y}, text_style, box.text, nullptr);
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

    auto create_context(Arena& arena, ContextDesc const& desc, Context& out_context) -> void {
        ContextImpl* const impl = arena_new<ContextImpl>(arena);
        size_t const capacity = std::max(desc.initial_box_capacity, 16u);
        impl->box_capacity = capacity;
        impl->state_table_size = next_power_of_two(capacity * 4u);
        impl->boxes = arena_alloc<BoxNode>(arena, capacity);
        impl->infos = arena_alloc<BoxInfo>(arena, capacity);
        impl->parent_stack = arena_alloc<size_t>(arena, capacity);
        impl->state_table = arena_alloc<StateEntry>(arena, impl->state_table_size);
        std::memset(impl->boxes, 0, sizeof(BoxNode) * capacity);
        std::memset(impl->infos, 0, sizeof(BoxInfo) * capacity);
        std::memset(impl->parent_stack, 0, sizeof(size_t) * capacity);
        std::memset(impl->state_table, 0, sizeof(StateEntry) * impl->state_table_size);
        impl->frame_arena.init({desc.frame_arena_reserve_size, desc.frame_arena_commit_size});
        out_context.handle = impl;
    }

    auto destroy_context(Context& context) -> void {
        ContextImpl* const impl = impl_from_context(context);
        if (impl != nullptr) {
            impl->frame_arena.destroy();
        }
        context.handle = nullptr;
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

    auto Scope::operator bool() const -> bool {
        return m_frame != nullptr;
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

    auto ListScope::range() const -> ListRange {
        return {first, end};
    }

    ListScope::ListScope(Scope&& scope, ListRange range)
        : first(range.first), end(range.end), m_scope(std::move(scope)) {}

    Frame::Frame(void* handle) : m_handle(handle) {}

    namespace detail {
        auto frame_handle(Frame const& frame) -> void* {
            return frame.m_handle;
        }
    } // namespace detail

    auto Frame::row(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(
            impl, BoxKind::ROW, structural_id(impl, parent, BoxKind::ROW), {}, desc, false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::row(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(
            impl, BoxKind::ROW, explicit_id(impl, parent, BoxKind::ROW, id_value), {}, desc, false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::column(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(
            impl, BoxKind::COLUMN, structural_id(impl, parent, BoxKind::COLUMN), {}, desc, false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::column(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::COLUMN,
                                        explicit_id(impl, parent, BoxKind::COLUMN, id_value),
                                        {},
                                        desc,
                                        false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::overlay(BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(
            impl, BoxKind::OVERLAY, structural_id(impl, parent, BoxKind::OVERLAY), {}, desc, false);
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::overlay(Id id_value, BoxDesc const& desc) -> Scope {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::OVERLAY,
                                        explicit_id(impl, parent, BoxKind::OVERLAY, id_value),
                                        {},
                                        desc,
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
                                        {},
                                        panel_desc,
                                        false);
        BoxNode& panel = impl->boxes[index];
        apply_scroll_delta(impl, panel);
        panel.scroll_offset_y = -panel.state->scroll_y;
        push_parent(impl, index);
        return {this, index};
    }

    auto Frame::spacer(BoxDesc const& desc) -> void {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        BASE_UNUSED(append_box(
            impl, BoxKind::SPACER, structural_id(impl, parent, BoxKind::SPACER), {}, desc, false));
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
                                        text_value,
                                        desc,
                                        false);
        return impl->boxes[index].signal;
    }

    auto Frame::label(Id id_value, StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::LABEL,
                                        explicit_id(impl, parent, BoxKind::LABEL, id_value),
                                        text_value,
                                        desc,
                                        false);
        return impl->boxes[index].signal;
    }

    auto Frame::button(StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::BUTTON,
                                        text_id(impl, parent, BoxKind::BUTTON, text_value),
                                        text_value,
                                        desc,
                                        true);
        return impl->boxes[index].signal;
    }

    auto Frame::button(Id id_value, StrRef text_value, BoxDesc const& desc) -> Signal {
        ContextImpl* const impl = impl_from_frame(*this);
        size_t const parent = top_parent_index(impl);
        size_t const index = append_box(impl,
                                        BoxKind::BUTTON,
                                        explicit_id(impl, parent, BoxKind::BUTTON, id_value),
                                        text_value,
                                        desc,
                                        true);
        return impl->boxes[index].signal;
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
                                        {},
                                        box_desc,
                                        false);

        BoxNode& list = impl->boxes[index];
        float viewport_height = rect_height(list.state->rect);
        if (viewport_height <= 0.0f && box_desc.layout.height.kind == SizeKind::PIXELS) {
            viewport_height = box_desc.layout.height.value;
        }
        if (viewport_height <= 0.0f) {
            viewport_height = impl->frame_desc.size.y;
        }

        float const item_height = std::max(desc.item_height, 1.0f);
        float const max_scroll =
            std::max(0.0f, static_cast<float>(desc.item_count) * item_height - viewport_height);
        apply_scroll_delta(impl, list);
        list.state->scroll_y = std::clamp(list.state->scroll_y, 0.0f, max_scroll);

        size_t first = static_cast<size_t>(list.state->scroll_y / item_height);
        first = std::min(first, desc.item_count);
        size_t const visible_count =
            static_cast<size_t>(std::ceil(viewport_height / item_height)) + 1u;
        ListRange const range = {first, std::min(desc.item_count, first + visible_count)};
        list.scroll_offset_y = -(list.state->scroll_y - static_cast<float>(first) * item_height);

        push_parent(impl, index);
        return {Scope(this, index), range};
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

    auto begin_frame(Context context, FrameDesc const& desc) -> Frame {
        ContextImpl* const impl = impl_from_context(context);
        ASSERT(impl != nullptr);
        impl->frame_arena.reset();
        impl->box_count = 0u;
        impl->parent_stack_count = 0u;
        impl->frame_desc = desc;
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
        measure_node(impl, 0u);
        layout_node(impl, 0u, {{0.0f, 0.0f}, {impl->frame_desc.size.x, impl->frame_desc.size.y}});
        publish_infos(impl);
        impl->previous_input = impl->frame_desc.input;
        impl->parent_stack_count = 1u;
        impl->building = false;
    }

    auto render(Frame const& frame, draw::Context draw_context) -> void {
        ContextImpl const* const impl = impl_from_frame(frame);
        if (impl == nullptr || !draw::context_valid(draw_context) || impl->box_count == 0u) {
            return;
        }
        render_box(impl, draw_context, 0u);
    }

} // namespace gui
