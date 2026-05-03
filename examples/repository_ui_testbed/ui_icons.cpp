#include "ui_icons.h"

#include "ui_common.h"

namespace repository_ui_testbed {

    namespace draw = gui::draw;

    [[nodiscard]] auto to_draw(gui::Color color) -> draw::Color {
        return {color.r, color.g, color.b, color.a};
    }

    [[nodiscard]] auto to_draw(gui::Rect rect) -> draw::Rect {
        return {{rect.min.x, rect.min.y}, {rect.max.x, rect.max.y}};
    }

    [[nodiscard]] auto rect_intersects(gui::Rect lhs, gui::Rect rhs) -> bool {
        return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x && lhs.min.y < rhs.max.y &&
               lhs.max.y > rhs.min.y;
    }

    auto draw_magnifier(draw::Context context, draw::Vec2 center, draw::Color color) -> void {
        draw::draw_circle(context, center, 5.0f, color, 1.4f, 18);
        draw::draw_line(
            context,
            {center.x + 4.0f, center.y + 4.0f},
            {center.x + 8.0f, center.y + 8.0f},
            color,
            1.4f
        );
    }

    auto draw_folder(draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent)
        -> void {
        float const x = rect.min.x + 38.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f - 7.0f;
        draw::draw_line(context, {x, y + 4.0f}, {x + 5.0f, y + 4.0f}, color, 1.2f);
        draw::draw_line(context, {x + 5.0f, y + 4.0f}, {x + 7.5f, y + 6.0f}, color, 1.2f);
        draw::draw_rect(context, {{x, y + 6.0f}, {x + 16.0f, y + 16.0f}}, color, 1.2f, 2.0f);
    }

    auto draw_file_leaf(draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent)
        -> void {
        float const x = rect.min.x + 38.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f - 8.0f;
        draw::draw_rect(context, {{x + 2.0f, y}, {x + 14.0f, y + 16.0f}}, color, 1.1f, 2.0f);
        draw::draw_line(context, {x + 5.0f, y + 5.0f}, {x + 11.0f, y + 5.0f}, color, 1.0f);
        draw::draw_line(context, {x + 5.0f, y + 9.0f}, {x + 11.0f, y + 9.0f}, color, 1.0f);
    }

    auto draw_tree_caret(
        draw::Context context, gui::Rect rect, draw::Color color, uint8_t indent, bool open
    ) -> void {
        float const x = rect.min.x + 22.0f + 18.0f * static_cast<float>(indent);
        float const y = (rect.min.y + rect.max.y) * 0.5f;
        if (open) {
            draw::draw_triangle_filled(
                context, {x - 4.0f, y - 2.0f}, {x + 4.0f, y - 2.0f}, {x, y + 4.0f}, color
            );
        } else {
            draw::draw_triangle_filled(
                context, {x - 2.0f, y - 5.0f}, {x - 2.0f, y + 5.0f}, {x + 4.0f, y}, color
            );
        }
    }

    auto draw_logo(draw::Context context, gui::Rect rect, RepositorySpec const& spec) -> void {
        draw::Vec2 const center = {rect.min.x + 16.0f, (rect.min.y + rect.max.y) * 0.5f};
        draw::Color const color = to_draw(spec.text);
        draw::draw_circle(context, center, 13.0f, to_draw(spec.border), 1.0f, 24);
        draw::draw_triangle_filled(
            context,
            {center.x, center.y - 6.0f},
            {center.x - 6.0f, center.y + 5.0f},
            {center.x + 6.0f, center.y + 5.0f},
            color
        );
    }

    auto draw_user_mark(draw::Context context, gui::Rect rect, RepositorySpec const& spec) -> void {
        float const x = rect.min.x + 16.0f;
        float const y = (rect.min.y + rect.max.y) * 0.5f;
        draw::Color const color = to_draw(spec.text);
        draw::draw_triangle_filled(
            context, {x - 5.0f, y - 5.0f}, {x, y + 4.0f}, {x - 1.0f, y + 9.0f}, color
        );
        draw::draw_triangle_filled(
            context, {x + 5.0f, y - 5.0f}, {x, y + 4.0f}, {x + 1.0f, y + 9.0f}, color
        );
        draw::draw_circle_filled(context, {x, y + 7.0f}, 4.0f, color, 16);
    }

    auto draw_repository_icons(
        gui::Frame const& ui,
        draw::Context context,
        RepositorySection selected_section,
        size_t selected_tab,
        RepoTree const& tree
    ) -> void {
        RepositorySpec const spec = {};
        draw::Color const muted = to_draw(spec.muted);

        if (gui::BoxInfo const* box = ui.find_box(gui::id("workspace_header"))) {
            draw_logo(context, box->rect, spec);
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("user_switcher"))) {
            draw_user_mark(context, box->rect, spec);
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("search_box"))) {
            draw_magnifier(
                context,
                {box->rect.min.x + 22.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                muted
            );
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("go_to_file"))) {
            draw_magnifier(
                context,
                {box->rect.min.x + 18.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                muted
            );
        }
        if (gui::BoxInfo const* box = ui.find_box(gui::id("environment"))) {
            draw::draw_circle_filled(
                context,
                {box->rect.min.x + 6.0f, (box->rect.min.y + box->rect.max.y) * 0.5f},
                3.0f,
                to_draw(spec.green),
                12
            );
        }

        if (selected_section == RepositorySection::CODE && selected_tab == 0u) {
            gui::Rect clip_rect = {};
            bool clipped = false;
            if (gui::BoxInfo const* scroll = ui.find_box(tab_scroll_id(0u))) {
                clip_rect = scroll->rect;
                clipped = true;
                draw::push_clip_rect(context, to_draw(clip_rect));
            }
            for (size_t index = 0u; index < tree.visible_count; ++index) {
                int32_t const node_index = tree.visible[index];
                RepoNode const& node = tree.nodes[node_index];
                if (gui::BoxInfo const* box =
                        ui.find_box(file_row_id(static_cast<size_t>(node_index)))) {
                    if (clipped && !rect_intersects(box->rect, clip_rect)) {
                        continue;
                    }
                    if (node.directory) {
                        draw_tree_caret(context, box->rect, muted, node.indent, node.open);
                        draw_folder(context, box->rect, muted, node.indent);
                    } else {
                        draw_file_leaf(context, box->rect, muted, node.indent);
                    }
                }
            }
            if (ui.find_box(tab_scroll_id(0u)) != nullptr) {
                draw::pop_clip_rect(context);
            }
        }
    }

} // namespace repository_ui_testbed
