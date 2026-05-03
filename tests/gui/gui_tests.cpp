#include <base/config.h>
#include <gui/gui.h>
#include <test/test.h>

namespace {

    constexpr char SORT_ICON_TEXT[] = "";

    [[nodiscard]] auto parse_test_int(StrRef text) -> int {
        int value = 0;
        for (char ch : text) {
            if (ch >= '0' && ch <= '9') {
                value = value * 10 + ch - '0';
            }
        }
        return value;
    }

    [[nodiscard]] auto compare_test_ints(void*, size_t, StrRef lhs, StrRef rhs) -> int {
        int const lhs_value = parse_test_int(lhs);
        int const rhs_value = parse_test_int(rhs);
        return lhs_value < rhs_value ? -1 : (lhs_value > rhs_value ? 1 : 0);
    }

    auto add_table_text_row(
        gui::Frame& ui, gui::TableScope& table, gui::Id row_id, StrRef text, float width
    ) -> void {
        if (auto row = table.row(row_id)) {
            if (auto cell = row.cell(
                    {.box = {.layout = {.width = gui::px(width), .height = gui::px(20.0f)}}}
                )) {
                ui.label(text, {.layout = {.width = gui::fill(), .height = gui::fill()}});
            }
        }
    }

    auto expect_vec(test::Context* context, gui::Vec2 value, float x, float y) -> void {
        TEST_EXPECT(context, value.x == x);
        TEST_EXPECT(context, value.y == y);
    }

    auto expect_rect(test::Context* context, gui::Rect value, gui::Rect expected) -> void {
        expect_vec(context, value.min, expected.min.x, expected.min.y);
        expect_vec(context, value.max, expected.max.x, expected.max.y);
    }

    auto expect_color(test::Context* context, gui::Color value, gui::Color expected) -> void {
        TEST_EXPECT(context, value.r == expected.r);
        TEST_EXPECT(context, value.g == expected.g);
        TEST_EXPECT(context, value.b == expected.b);
        TEST_EXPECT(context, value.a == expected.a);
    }

    struct ClipboardCapture {
        char text[32] = {};
        size_t text_size = 0u;
        size_t call_count = 0u;
        size_t read_count = 0u;
    };

    auto capture_clipboard_text(void* user_data, StrRef text) -> void {
        ClipboardCapture* const capture = static_cast<ClipboardCapture*>(user_data);
        capture->text_size = text.copy_to(capture->text, sizeof(capture->text));
        capture->call_count += 1u;
    }

    auto read_clipboard_text(void* user_data, Arena&) -> StrRef {
        ClipboardCapture* const capture = static_cast<ClipboardCapture*>(user_data);
        capture->read_count += 1u;
        return {capture->text, capture->text_size};
    }

    auto add_basic_scroll_panel(test::Context* context, gui::Frame& ui, gui::Id id_value) -> void {
        auto panel =
            ui.scroll_panel(id_value, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}});
        TEST_EXPECT(context, panel);
        for (size_t index = 0u; index < 3u; ++index) {
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
    }

    auto box_center(gui::BoxInfo const* box) -> gui::Vec2 {
        if (box == nullptr) {
            return {};
        }
        return {
            (box->rect.min.x + box->rect.max.x) * 0.5f, (box->rect.min.y + box->rect.max.y) * 0.5f
        };
    }

    auto find_box_text(gui::Frame const& ui, gui::BoxKind kind, StrRef text)
        -> gui::BoxInfo const* {
        for (size_t index = 0u; index < ui.box_info_count(); ++index) {
            gui::BoxInfo const* const box = ui.box_info(index);
            if (box != nullptr && box->kind == kind && box->text == text) {
                return box;
            }
        }
        return nullptr;
    }

    auto find_child_text(gui::Frame const& ui, gui::Id parent_id, gui::BoxKind kind, StrRef text)
        -> gui::BoxInfo const* {
        for (size_t index = 0u; index < ui.box_info_count(); ++index) {
            gui::BoxInfo const* const box = ui.box_info(index);
            if (box != nullptr && box->parent_id.value == parent_id.value && box->kind == kind &&
                box->text == text) {
                return box;
            }
        }
        return nullptr;
    }

    auto find_text_command(gui::draw::Context draw_context, StrRef text)
        -> gui::draw::TextCommand const* {
        for (size_t index = 0u; index < gui::draw::text_command_count(draw_context); ++index) {
            gui::draw::TextCommand const* const command =
                gui::draw::text_command(draw_context, index);
            if (command != nullptr && command->text == text) {
                return command;
            }
        }
        return nullptr;
    }

    TEST_CASE(version_is_available) {
        gui::Version const gui_version = gui::version();

        TEST_EXPECT(context, gui_version.major == 0u);
        TEST_EXPECT(context, gui_version.minor == 1u);
        TEST_EXPECT(context, gui_version.patch == 0u);
        TEST_EXPECT(context, gui::version_string()[0] != '\0');
        TEST_EXPECT(context, gui::build_compiler()[0] != '\0');
    }

    TEST_CASE(context_and_empty_frame_are_valid) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        TEST_EXPECT(context, gui::context_valid(gui_context));

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {64.0f, 32.0f}});
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 1u);
        gui::BoxInfo const* root = ui.box_info(0u);
        TEST_EXPECT(context, root != nullptr);
        TEST_EXPECT(context, root != nullptr && root->kind == gui::BoxKind::ROOT);
        if (root != nullptr) {
            expect_rect(context, root->rect, {{0.0f, 0.0f}, {64.0f, 32.0f}});
        }

        gui::destroy_context(gui_context);
        TEST_EXPECT(context, !gui::context_valid(gui_context));
    }

    TEST_CASE(column_layout_applies_fill_padding_gap_and_metadata) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        auto page = ui.column({
            .layout =
                {
                    .width = gui::fill(),
                    .height = gui::fill(),
                    .padding = gui::insets(10.0f),
                    .gap = 5.0f,
                },
            .style = {.background = gui::rgb(10, 20, 30)},
            .debug_name = "page",
        });
        TEST_EXPECT(context, page);
        ui.label("A", {.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}});
        ui.label("B", {.layout = {.width = gui::px(30.0f), .height = gui::px(15.0f)}});
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 4u);

        gui::BoxInfo const* panel = ui.box_info(1u);
        gui::BoxInfo const* first = ui.box_info(2u);
        gui::BoxInfo const* second = ui.box_info(3u);
        TEST_EXPECT(context, panel != nullptr && panel->kind == gui::BoxKind::COLUMN);
        TEST_EXPECT(context, panel != nullptr && panel->debug_name == StrRef("page"));
        TEST_EXPECT(context, first != nullptr && first->text == StrRef("A"));
        TEST_EXPECT(context, second != nullptr && second->text == StrRef("B"));
        if (panel != nullptr && first != nullptr && second != nullptr) {
            expect_rect(context, panel->rect, {{0.0f, 0.0f}, {100.0f, 60.0f}});
            expect_rect(context, first->rect, {{10.0f, 10.0f}, {30.0f, 20.0f}});
            expect_rect(context, second->rect, {{10.0f, 25.0f}, {40.0f, 40.0f}});
            TEST_EXPECT(context, panel->style.background.b == (30.0f / 255.0f));
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(style_inheritance_keeps_local_fields_and_publishes_resolved_metadata) {
        Arena arena = {};
        arena.init();

        gui::ThemeDesc theme = gui::default_theme();
        gui::theme_kind(theme, gui::BoxKind::LABEL).style.normal = {.font_size = 18.0f};

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        {
            auto panel = ui.column({
                .style = {
                    .background = gui::rgb(10, 20, 30),
                    .foreground = gui::rgb(210, 220, 230),
                },
            });
            TEST_EXPECT(context, panel);
            ui.label("Child");
        }
        gui::end_frame(ui);

        gui::BoxInfo const* panel = ui.box_info(1u);
        gui::BoxInfo const* label = ui.box_info(2u);
        TEST_EXPECT(context, panel != nullptr && label != nullptr);
        if (panel != nullptr && label != nullptr) {
            expect_color(context, label->style.foreground, panel->style.foreground);
            TEST_EXPECT(context, label->style.background.a < 0.0f);
            TEST_EXPECT(context, label->style.font_size == 18.0f);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(button_theme_defaults_follow_hover_and_active_state) {
        Arena arena = {};
        arena.init();

        gui::ThemeDesc theme = gui::default_theme();
        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        gui::Signal signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::BoxInfo const* button = ui.box_info(1u);
        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, button != nullptr);
        if (button != nullptr) {
            expect_color(
                context,
                button->style.background,
                gui::theme_role(theme, gui::StyleRole::CONTROL).hovered.background
            );
        }

        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        button = ui.box_info(1u);
        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, button != nullptr);
        if (button != nullptr) {
            expect_color(
                context,
                button->style.background,
                gui::theme_role(theme, gui::StyleRole::CONTROL).active.background
            );
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(button_signal_reports_hover_edges) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const button_id = gui::id("button");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.button(
            button_id, "Info", {.layout = {.width = gui::px(60.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.button(
            button_id, "Info", {.layout = {.width = gui::px(60.0f), .height = gui::px(20.0f)}}
        );
        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, signal.hover_entered);
        TEST_EXPECT(context, !signal.hover_exited);
        gui::end_frame(ui);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal = ui.button(
            button_id, "Info", {.layout = {.width = gui::px(60.0f), .height = gui::px(20.0f)}}
        );
        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, !signal.hover_entered);
        TEST_EXPECT(context, !signal.hover_exited);
        gui::end_frame(ui);

        input.mouse_pos = {80.0f, 30.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal = ui.button(
            button_id, "Info", {.layout = {.width = gui::px(60.0f), .height = gui::px(20.0f)}}
        );
        TEST_EXPECT(context, !signal.hovered);
        TEST_EXPECT(context, !signal.hover_entered);
        TEST_EXPECT(context, signal.hover_exited);
        gui::end_frame(ui);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tree_node_default_open_builds_indented_body) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const tree_id = gui::id("assets");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {140.0f, 80.0f}});
        if (auto tree = ui.tree_node(
                tree_id,
                "Assets",
                {
                    .box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}},
                    .default_open = true,
                }
            )) {
            ui.label("Texture", {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::BoxInfo const* node = ui.find_box(tree_id, gui::BoxKind::TREE_NODE);
        gui::BoxInfo const* body = ui.box_info(2u);
        gui::BoxInfo const* child = find_box_text(ui, gui::BoxKind::LABEL, "Texture");
        TEST_EXPECT(context, node != nullptr);
        TEST_EXPECT(context, body != nullptr && body->kind == gui::BoxKind::COLUMN);
        TEST_EXPECT(context, child != nullptr);
        if (node != nullptr && body != nullptr && child != nullptr) {
            TEST_EXPECT(context, body->rect.min.x == node->rect.min.x + 12.0f);
            TEST_EXPECT(context, child->parent_id.value == body->id.value);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tree_node_toggles_open_on_click) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const tree_id = gui::id("assets");
        gui::Signal signal = {};
        bool open = false;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {140.0f, 80.0f}});
        {
            auto tree = ui.tree_node(
                tree_id,
                "Assets",
                {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
            );
            signal = tree.signal();
            open = tree.open();
            if (tree) {
                ui.label("Texture", {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            }
        }
        gui::end_frame(ui);

        gui::BoxInfo const* node = ui.find_box(tree_id, gui::BoxKind::TREE_NODE);
        TEST_EXPECT(context, node != nullptr);
        TEST_EXPECT(context, !open);
        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::LABEL, "Texture") == nullptr);

        gui::InputState input = {};
        input.mouse_pos = box_center(node);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {140.0f, 80.0f}, .input = input});
        {
            auto tree = ui.tree_node(
                tree_id,
                "Assets",
                {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
            );
            if (tree) {
                ui.label("Texture", {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            }
        }
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {140.0f, 80.0f}, .input = input});
        {
            auto tree = ui.tree_node(
                tree_id,
                "Assets",
                {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
            );
            signal = tree.signal();
            open = tree.open();
            if (tree) {
                ui.label("Texture", {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            }
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, open);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::LABEL, "Texture") != nullptr);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tree_node_wrapped_children_push_following_nodes_down) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {112.0f, 120.0f}});
        if (auto tree = ui.tree_node(
                gui::id("root"),
                "Root",
                {
                    .box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}},
                    .default_open = true,
                }
            )) {
            ui.label(
                gui::id("wrapped"),
                "ui_api_testbed_texture.png",
                {.layout = {
                     .width = gui::fill(),
                     .height = gui::text(),
                     .word_wrap = true,
                 }}
            );
        }
        auto next_tree = ui.tree_node(
            gui::id("next"),
            "Next",
            {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
        );
        BASE_UNUSED(next_tree);
        gui::end_frame(ui);

        gui::BoxInfo const* wrapped = ui.find_box(gui::id("wrapped"), gui::BoxKind::LABEL);
        gui::BoxInfo const* next = ui.find_box(gui::id("next"), gui::BoxKind::TREE_NODE);
        TEST_EXPECT(context, wrapped != nullptr);
        TEST_EXPECT(context, next != nullptr);
        if (wrapped != nullptr && next != nullptr) {
            TEST_EXPECT(context, wrapped->rect.max.y <= next->rect.min.y);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tree_node_body_spaces_sibling_tree_nodes) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {140.0f, 100.0f}});
        if (auto tree = ui.tree_node(
                gui::id("root"),
                "Root",
                {
                    .box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}},
                    .default_open = true,
                }
            )) {
            auto first_tree = ui.tree_node(
                gui::id("first"),
                "First",
                {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
            );
            BASE_UNUSED(first_tree);
            auto second_tree = ui.tree_node(
                gui::id("second"),
                "Second",
                {.box = {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}}
            );
            BASE_UNUSED(second_tree);
        }
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.find_box(gui::id("first"), gui::BoxKind::TREE_NODE);
        gui::BoxInfo const* second = ui.find_box(gui::id("second"), gui::BoxKind::TREE_NODE);
        TEST_EXPECT(context, first != nullptr);
        TEST_EXPECT(context, second != nullptr);
        if (first != nullptr && second != nullptr) {
            TEST_EXPECT(context, second->rect.min.y >= first->rect.max.y + 4.0f);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_theme_defaults_keep_background_on_actions) {
        gui::ThemeDesc theme = gui::default_theme();
        gui::Color const background =
            gui::theme_role(theme, gui::StyleRole::CONTROL).normal.background;

        gui::ThemeStyle const& single = gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT).style;
        expect_color(context, single.hovered.background, background);
        expect_color(context, single.active.background, background);

        gui::ThemeStyle const& multiline =
            gui::theme_kind(theme, gui::BoxKind::INPUT_TEXT_MULTILINE).style;
        expect_color(context, multiline.hovered.background, background);
        expect_color(context, multiline.active.background, background);
    }

    TEST_CASE(boxdesc_style_overrides_theme_role_and_kind_defaults) {
        Arena arena = {};
        arena.init();

        gui::ThemeDesc theme = gui::default_theme();
        gui::theme_role(theme, gui::StyleRole::DANGER).normal = {
            .background = gui::rgb(160, 20, 20),
            .foreground = gui::rgb(250, 230, 230),
            .radius = 9.0f,
        };
        gui::theme_kind(theme, gui::BoxKind::BUTTON).style.normal = {
            .background = gui::rgb(20, 40, 60),
            .radius = 6.0f,
        };

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.button(
            "Delete",
            {
                .layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)},
                .style = {
                    .role = gui::StyleRole::DANGER,
                    .background = gui::rgb(1, 2, 3),
                    .border = gui::rgba(0, 0, 0, 0),
                    .radius = 2.0f,
                },
            }
        );
        gui::end_frame(ui);

        gui::BoxInfo const* button = ui.box_info(1u);
        TEST_EXPECT(context, button != nullptr);
        if (button != nullptr) {
            expect_color(context, button->style.foreground, gui::rgb(250, 230, 230));
            expect_color(context, button->style.background, gui::rgb(1, 2, 3));
            expect_color(context, button->style.border, gui::rgba(0, 0, 0, 0));
            TEST_EXPECT(context, button->style.radius == 2.0f);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(row_layout_splits_fixed_and_fill_children) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 20.0f}});
        auto row = ui.row({
            .layout = {
                .width = gui::fill(),
                .height = gui::fill(),
                .gap = 4.0f,
                .align_y = gui::Align::STRETCH,
            },
        });
        TEST_EXPECT(context, row);
        ui.spacer({.layout = {.width = gui::px(20.0f), .height = gui::fill()}});
        ui.spacer({.layout = {.width = gui::fill(), .height = gui::fill()}});
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 4u);
        gui::BoxInfo const* fixed = ui.box_info(2u);
        gui::BoxInfo const* fill = ui.box_info(3u);
        if (fixed != nullptr && fill != nullptr) {
            expect_rect(context, fixed->rect, {{0.0f, 0.0f}, {20.0f, 20.0f}});
            expect_rect(context, fill->rect, {{24.0f, 0.0f}, {100.0f, 20.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(multiline_label_measures_lines) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        ui.label("AB\nCDE", {.layout = {.width = gui::text(), .height = gui::text()}});
        gui::end_frame(ui);

        gui::BoxInfo const* label = ui.box_info(1u);
        TEST_EXPECT(context, label != nullptr && label->kind == gui::BoxKind::LABEL);
        if (label != nullptr) {
            expect_rect(context, label->rect, {{0.0f, 0.0f}, {24.0f, 40.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(word_wrapped_label_measures_lines) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        ui.label(
            "AB CD",
            {.layout = {
                 .width = gui::px(16.0f),
                 .height = gui::text(),
                 .word_wrap = true,
             }}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* label = ui.box_info(1u);
        TEST_EXPECT(context, label != nullptr && label->kind == gui::BoxKind::LABEL);
        if (label != nullptr) {
            expect_rect(context, label->rect, {{0.0f, 0.0f}, {16.0f, 40.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_word_wrap_measures_lines) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("AB CD") == 5u);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {.box = {
                 .layout = {
                     .width = gui::px(16.0f),
                     .height = gui::text(),
                     .word_wrap = true,
                 }
             }}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* field = ui.find_box(field_id, gui::BoxKind::INPUT_TEXT_MULTILINE);
        TEST_EXPECT(context, field != nullptr);
        if (field != nullptr) {
            expect_rect(context, field->rect, {{0.0f, 0.0f}, {16.0f, 40.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(row_layout_aligns_child_run_when_no_fill_consumes_space) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 20.0f}});
        auto row = ui.row({
            .layout = {
                .width = gui::fill(),
                .height = gui::fill(),
                .gap = 4.0f,
                .align_x = gui::Align::CENTER,
            },
        });
        TEST_EXPECT(context, row);
        ui.spacer({.layout = {.width = gui::px(10.0f), .height = gui::px(10.0f)}});
        ui.spacer({.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}});
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(2u);
        gui::BoxInfo const* second = ui.box_info(3u);
        if (first != nullptr && second != nullptr) {
            expect_rect(context, first->rect, {{33.0f, 0.0f}, {43.0f, 10.0f}});
            expect_rect(context, second->rect, {{47.0f, 0.0f}, {67.0f, 10.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(empty_table_uses_authored_size) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("empty_table");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        {
            auto table =
                ui.table(table_id, {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
            TEST_EXPECT(context, table);
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 2u);
        gui::BoxInfo const* table = ui.find_box(table_id, gui::BoxKind::TABLE);
        TEST_EXPECT(context, table != nullptr);
        if (table != nullptr) {
            expect_rect(context, table->rect, {{0.0f, 0.0f}, {80.0f, 20.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tab_view_switches_selected_body_content) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const tab_a = gui::id("tab_a");
        gui::Id const tab_b = gui::id("tab_b");
        size_t selected = 0u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {240.0f, 100.0f}});
        if (auto tab_view = ui.tab_view(
                gui::id("tabs"),
                {.read_only_tabs = {{tab_a, "A"}, {tab_b, "B"}},
                 .selected_index = &selected,
                 .flags = 0u,
                 .box = {.layout = {.width = gui::px(220.0f), .height = gui::px(90.0f)}}}
            )) {
            ui.label(selected == 0u ? "Alpha" : "Beta");
            TEST_EXPECT(context, tab_view.selected_index() == 0u);
        }
        gui::end_frame(ui);

        gui::BoxInfo const* second_tab = ui.find_box(tab_b, gui::BoxKind::TAB);
        TEST_EXPECT(context, second_tab != nullptr);
        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::LABEL, "Alpha") != nullptr);

        gui::InputState input = {};
        input.mouse_pos = box_center(second_tab);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {240.0f, 100.0f}, .input = input});
        BASE_UNUSED(ui.tab_view(
            gui::id("tabs"),
            {.read_only_tabs = {{tab_a, "A"}, {tab_b, "B"}},
             .selected_index = &selected,
             .flags = 0u,
             .box = {.layout = {.width = gui::px(220.0f), .height = gui::px(90.0f)}}}
        ));
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {240.0f, 100.0f}, .input = input});
        if (auto tab_view = ui.tab_view(
                gui::id("tabs"),
                {.read_only_tabs = {{tab_a, "A"}, {tab_b, "B"}},
                 .selected_index = &selected,
                 .flags = 0u,
                 .box = {.layout = {.width = gui::px(220.0f), .height = gui::px(90.0f)}}}
            )) {
            ui.label(selected == 0u ? "Alpha" : "Beta");
            TEST_EXPECT(context, tab_view.result().selected_index == 1u);
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, selected == 1u);
        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::LABEL, "Beta") != nullptr);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tab_view_adds_closes_and_moves_app_owned_tabs) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TabItem tabs[4] = {
            {gui::id("tab_a"), "A"},
            {gui::id("tab_b"), "B"},
        };
        size_t tab_count = 2u;
        size_t selected = 0u;
        gui::TabViewDesc desc = {
            .tabs = slice(tabs),
            .tab_count = &tab_count,
            .selected_index = &selected,
            .new_tab = {gui::id("tab_new"), "New"},
            .box = {.layout = {.width = gui::px(260.0f), .height = gui::px(100.0f)}},
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}});
        BASE_UNUSED(ui.tab_view(gui::id("tabs"), desc));
        gui::end_frame(ui);

        gui::BoxInfo const* add = find_box_text(ui, gui::BoxKind::BUTTON, "+");
        TEST_EXPECT(context, add != nullptr);

        gui::InputState input = {};
        input.mouse_pos = box_center(add);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        BASE_UNUSED(ui.tab_view(gui::id("tabs"), desc));
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        gui::TabViewResult result = {};
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        if (auto tab_view = ui.tab_view(gui::id("tabs"), desc)) {
            result = tab_view.result();
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, result.added);
        TEST_EXPECT(context, tab_count == 3u);
        TEST_EXPECT(context, selected == 2u);
        TEST_EXPECT(context, tabs[2u].title == StrRef("New"));

        gui::BoxInfo const* new_tab = ui.find_box(tabs[2u].id, gui::BoxKind::TAB);
        TEST_EXPECT(context, new_tab != nullptr);
        gui::BoxInfo const* close =
            new_tab != nullptr ? find_child_text(ui, new_tab->id, gui::BoxKind::BUTTON, "x")
                               : nullptr;
        TEST_EXPECT(context, close != nullptr);

        input.mouse_pos = box_center(close);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        BASE_UNUSED(ui.tab_view(gui::id("tabs"), desc));
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        result = {};
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        if (auto tab_view = ui.tab_view(gui::id("tabs"), desc)) {
            result = tab_view.result();
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, result.closed);
        TEST_EXPECT(context, result.closed_index == 2u);
        TEST_EXPECT(context, tab_count == 2u);
        TEST_EXPECT(context, selected == 1u);

        gui::BoxInfo const* first_tab = ui.find_box(tabs[0u].id, gui::BoxKind::TAB);
        gui::BoxInfo const* second_tab = ui.find_box(tabs[1u].id, gui::BoxKind::TAB);
        TEST_EXPECT(context, first_tab != nullptr && second_tab != nullptr);

        input.mouse_pos = box_center(first_tab);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        BASE_UNUSED(ui.tab_view(gui::id("tabs"), desc));
        gui::end_frame(ui);

        input.mouse_pos.x = second_tab != nullptr ? second_tab->rect.max.x + 2.0f : 0.0f;
        result = {};
        ui = gui::begin_frame(gui_context, {.size = {280.0f, 120.0f}, .input = input});
        if (auto tab_view = ui.tab_view(gui::id("tabs"), desc)) {
            result = tab_view.result();
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, result.moved);
        TEST_EXPECT(context, result.moved_from == 0u);
        TEST_EXPECT(context, result.moved_to == 1u);
        TEST_EXPECT(context, tabs[0u].title == StrRef("B"));
        TEST_EXPECT(context, tabs[1u].title == StrRef("A"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_layout_supports_header_and_column_span) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const header_left_id = gui::id("header_left");
        gui::Id const header_right_id = gui::id("header_right");
        gui::Id const cell_a_id = gui::id("cell_a");
        gui::Id const cell_b_id = gui::id("cell_b");
        gui::Id const cell_c_id = gui::id("cell_c");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 80.0f}});
        if (auto table = ui.table(
                table_id,
                {.layout = {
                     .width = gui::children(),
                     .height = gui::children(),
                     .gap = 2.0f,
                 }}
            )) {
            if (auto header = table.header_row()) {
                {
                    auto cell = header.cell(
                        header_left_id,
                        {
                            .column_span = 2u,
                            .box = {.layout = {.width = gui::px(50.0f), .height = gui::px(10.0f)}},
                        }
                    );
                    TEST_EXPECT(context, cell);
                }
                {
                    auto cell = header.cell(
                        header_right_id,
                        {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}}}
                    );
                    TEST_EXPECT(context, cell);
                }
            }
            if (auto row = table.row()) {
                {
                    auto cell = row.cell(
                        cell_a_id,
                        {.box = {.layout = {.width = gui::px(10.0f), .height = gui::px(12.0f)}}}
                    );
                    TEST_EXPECT(context, cell);
                }
                {
                    auto cell = row.cell(
                        cell_b_id,
                        {.box = {.layout = {.width = gui::px(30.0f), .height = gui::px(12.0f)}}}
                    );
                    TEST_EXPECT(context, cell);
                }
                {
                    auto cell = row.cell(
                        cell_c_id,
                        {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(12.0f)}}}
                    );
                    TEST_EXPECT(context, cell);
                }
            }
        }
        gui::end_frame(ui);

        gui::BoxInfo const* table = ui.find_box(table_id, gui::BoxKind::TABLE);
        gui::BoxInfo const* header_left =
            ui.find_box(header_left_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* header_right =
            ui.find_box(header_right_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* cell_a = ui.find_box(cell_a_id, gui::BoxKind::TABLE_CELL);
        gui::BoxInfo const* cell_b = ui.find_box(cell_b_id, gui::BoxKind::TABLE_CELL);
        gui::BoxInfo const* cell_c = ui.find_box(cell_c_id, gui::BoxKind::TABLE_CELL);

        TEST_EXPECT(context, table != nullptr);
        TEST_EXPECT(context, header_left != nullptr && header_right != nullptr);
        TEST_EXPECT(context, cell_a != nullptr && cell_b != nullptr && cell_c != nullptr);
        if (table != nullptr && header_left != nullptr && header_right != nullptr &&
            cell_a != nullptr && cell_b != nullptr && cell_c != nullptr) {
            expect_rect(context, table->rect, {{0.0f, 0.0f}, {78.0f, 24.0f}});
            expect_rect(context, header_left->rect, {{0.0f, 0.0f}, {56.0f, 10.0f}});
            expect_rect(context, header_right->rect, {{58.0f, 0.0f}, {78.0f, 10.0f}});
            expect_rect(context, cell_a->rect, {{0.0f, 12.0f}, {24.0f, 24.0f}});
            expect_rect(context, cell_b->rect, {{26.0f, 12.0f}, {56.0f, 24.0f}});
            expect_rect(context, cell_c->rect, {{58.0f, 12.0f}, {78.0f, 24.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_sort_button_updates_multi_column_sort_state) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);
        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const header_a_id = gui::id("header_a");
        gui::Id const header_b_id = gui::id("header_b");
        gui::Id const header_c_id = gui::id("header_c");
        gui::TableSortColumn sort_columns[3] = {};
        size_t sort_count = 0u;
        bool selected[3] = {true, false, true};
        gui::Signal sort_signals[3] = {};

        auto add_sort_table = [&](gui::Frame& ui) -> void {
            sort_signals[0u] = {};
            sort_signals[1u] = {};
            sort_signals[2u] = {};
            gui::TableSortDesc const sort_desc = {
                .columns = slice(sort_columns),
                .column_count = &sort_count,
                .selected_columns = slice(selected),
            };
            if (auto table = ui.table(
                    table_id, {.layout = {.width = gui::children(), .height = gui::children()}}
                )) {
                if (auto header = table.header_row()) {
                    if (auto cell = header.cell(header_a_id)) {
                        BASE_UNUSED(cell);
                        sort_signals[0u] = table.sort_button(0u, sort_desc);
                    }
                    if (auto cell = header.cell(header_b_id)) {
                        BASE_UNUSED(cell);
                        sort_signals[1u] = table.sort_button(1u, sort_desc);
                    }
                    if (auto cell = header.cell(header_c_id)) {
                        BASE_UNUSED(cell);
                        sort_signals[2u] = table.sort_button(2u, sort_desc);
                    }
                }
            }
        };

        gui::InputState input = {};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}});
        add_sort_table(ui);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) != 0u);

        gui::BoxInfo const* header_a = ui.find_box(header_a_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* header_c = ui.find_box(header_c_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* sort_a =
            header_a != nullptr
                ? find_child_text(ui, header_a->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                : nullptr;
        gui::BoxInfo const* sort_c =
            header_c != nullptr
                ? find_child_text(ui, header_c->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                : nullptr;
        TEST_EXPECT(context, sort_a != nullptr);
        TEST_EXPECT(context, sort_c != nullptr);

        input.mouse_pos = box_center(sort_c);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        add_sort_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        add_sort_table(ui);
        gui::end_frame(ui);

        TEST_EXPECT(context, sort_signals[2u].changed);
        TEST_EXPECT(context, sort_count == 2u);
        TEST_EXPECT(context, sort_columns[0u].column == 2u);
        TEST_EXPECT(context, sort_columns[0u].direction == gui::TableSortDirection::ASCENDING);
        TEST_EXPECT(context, sort_columns[1u].column == 0u);
        TEST_EXPECT(context, sort_columns[1u].direction == gui::TableSortDirection::ASCENDING);

        header_a = ui.find_box(header_a_id, gui::BoxKind::TABLE_HEADER_CELL);
        header_c = ui.find_box(header_c_id, gui::BoxKind::TABLE_HEADER_CELL);
        sort_a = header_a != nullptr
                     ? find_child_text(ui, header_a->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                     : nullptr;
        sort_c = header_c != nullptr
                     ? find_child_text(ui, header_c->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                     : nullptr;
        TEST_EXPECT(context, sort_a != nullptr);
        TEST_EXPECT(context, sort_c != nullptr);

        input.mouse_pos = box_center(sort_c);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        add_sort_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        add_sort_table(ui);
        gui::end_frame(ui);

        TEST_EXPECT(context, sort_signals[2u].changed);
        TEST_EXPECT(context, sort_count == 2u);
        TEST_EXPECT(context, sort_columns[0u].column == 2u);
        TEST_EXPECT(context, sort_columns[0u].direction == gui::TableSortDirection::DESCENDING);
        TEST_EXPECT(context, sort_columns[1u].column == 0u);
        TEST_EXPECT(context, sort_columns[1u].direction == gui::TableSortDirection::DESCENDING);

        header_a = ui.find_box(header_a_id, gui::BoxKind::TABLE_HEADER_CELL);
        header_c = ui.find_box(header_c_id, gui::BoxKind::TABLE_HEADER_CELL);
        sort_a = header_a != nullptr
                     ? find_child_text(ui, header_a->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                     : nullptr;
        sort_c = header_c != nullptr
                     ? find_child_text(ui, header_c->id, gui::BoxKind::BUTTON, SORT_ICON_TEXT)
                     : nullptr;
        TEST_EXPECT(context, sort_a != nullptr);
        TEST_EXPECT(context, sort_c != nullptr);

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_desc_sorts_rows_by_cell_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const header_name_id = gui::id("header_name");
        gui::Id const row_beta_id = gui::id("row_beta");
        gui::Id const row_gamma_id = gui::id("row_gamma");
        gui::Id const row_alpha_id = gui::id("row_alpha");
        gui::TableSortColumn sort_columns[1] = {};
        size_t sort_count = 0u;
        bool selected[1] = {true};

        auto add_table = [&](gui::Frame& ui) -> void {
            gui::TableSortDesc const sort_desc = {
                .columns = slice(sort_columns),
                .column_count = &sort_count,
                .selected_columns = slice(selected),
            };
            if (auto table = ui.table(
                    table_id,
                    {
                        .box =
                            {
                                .layout =
                                    {
                                        .width = gui::children(),
                                        .height = gui::children(),
                                        .gap = 2.0f,
                                    },
                            },
                        .sort = sort_desc,
                    }
                )) {
                if (auto header = table.header_row()) {
                    BASE_UNUSED(header);
                    BASE_UNUSED(table.sortable_header_cell(
                        header_name_id,
                        0u,
                        "Name",
                        {.box = {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}}}
                    ));
                }
                add_table_text_row(ui, table, row_beta_id, "Beta", 80.0f);
                add_table_text_row(ui, table, row_gamma_id, "Gamma", 80.0f);
                add_table_text_row(ui, table, row_alpha_id, "Alpha", 80.0f);
            }
        };

        gui::InputState input = {};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 120.0f}});
        add_table(ui);
        gui::end_frame(ui);

        gui::BoxInfo const* header = ui.find_box(header_name_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* sort =
            header != nullptr ? find_box_text(ui, gui::BoxKind::BUTTON, SORT_ICON_TEXT) : nullptr;
        TEST_EXPECT(context, sort != nullptr);

        input.mouse_pos = box_center(sort);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 120.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 120.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        gui::BoxInfo const* row_beta = ui.find_box(row_beta_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_gamma = ui.find_box(row_gamma_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_alpha = ui.find_box(row_alpha_id, gui::BoxKind::TABLE_ROW);
        TEST_EXPECT(context, sort_count == 1u);
        TEST_EXPECT(context, sort_columns[0u].direction == gui::TableSortDirection::ASCENDING);
        TEST_EXPECT(context, row_beta != nullptr);
        TEST_EXPECT(context, row_gamma != nullptr);
        TEST_EXPECT(context, row_alpha != nullptr);
        if (row_beta != nullptr && row_gamma != nullptr && row_alpha != nullptr) {
            TEST_EXPECT(context, row_alpha->rect.min.y < row_beta->rect.min.y);
            TEST_EXPECT(context, row_beta->rect.min.y < row_gamma->rect.min.y);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(sortable_header_cell_uses_selected_column_checkbox) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const header_id = gui::id("header");
        gui::TableSortColumn sort_columns[1] = {};
        size_t sort_count = 0u;
        bool selected[1] = {};
        gui::Signal header_signal = {};

        auto add_table = [&](gui::Frame& ui) -> void {
            header_signal = {};
            gui::TableSortDesc const sort_desc = {
                .columns = slice(sort_columns),
                .column_count = &sort_count,
                .selected_columns = slice(selected),
            };
            if (auto table = ui.table(table_id, {.sort = sort_desc})) {
                if (auto header = table.header_row()) {
                    BASE_UNUSED(header);
                    header_signal = table.sortable_header_cell(
                        header_id,
                        0u,
                        "Name",
                        {.box = {.layout = {.width = gui::px(120.0f), .height = gui::px(28.0f)}}}
                    );
                }
            }
        };

        gui::InputState input = {};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 60.0f}});
        add_table(ui);
        gui::end_frame(ui);

        gui::BoxInfo const* header = ui.find_box(header_id, gui::BoxKind::TABLE_HEADER_CELL);
        gui::BoxInfo const* sort = find_box_text(ui, gui::BoxKind::BUTTON, SORT_ICON_TEXT);
        gui::BoxInfo const* checkbox = find_box_text(ui, gui::BoxKind::CHECKBOX, "Name");
        TEST_EXPECT(context, header != nullptr);
        TEST_EXPECT(context, sort != nullptr);
        TEST_EXPECT(context, checkbox != nullptr);
        TEST_EXPECT(context, !selected[0u]);
        if (header != nullptr && sort != nullptr) {
            TEST_EXPECT(context, sort->rect.min.x == header->rect.min.x + 10.0f);
        }

        input.mouse_pos = box_center(checkbox);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 60.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 60.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        TEST_EXPECT(context, selected[0u]);
        TEST_EXPECT(context, header_signal.changed);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_desc_accepts_custom_string_compare) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const row_ten_id = gui::id("row_ten");
        gui::Id const row_two_id = gui::id("row_two");
        gui::TableSortColumn sort_columns[1] = {{0u, gui::TableSortDirection::ASCENDING}};
        size_t sort_count = 1u;
        gui::TableSortDesc const sort_desc = {
            .columns = slice(sort_columns),
            .column_count = &sort_count,
            .compare = compare_test_ints,
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 80.0f}});
        if (auto table = ui.table(
                table_id,
                {
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::children(),
                                    .height = gui::children(),
                                    .gap = 2.0f,
                                },
                        },
                    .sort = sort_desc,
                }
            )) {
            add_table_text_row(ui, table, row_ten_id, "10", 40.0f);
            add_table_text_row(ui, table, row_two_id, "2", 40.0f);
        }
        gui::end_frame(ui);

        gui::BoxInfo const* row_ten = ui.find_box(row_ten_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_two = ui.find_box(row_two_id, gui::BoxKind::TABLE_ROW);
        TEST_EXPECT(context, row_ten != nullptr);
        TEST_EXPECT(context, row_two != nullptr);
        if (row_ten != nullptr && row_two != nullptr) {
            TEST_EXPECT(context, row_two->rect.min.y < row_ten->rect.min.y);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_desc_filters_rows_by_fuzzy_search_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const row_alpha_a_id = gui::id("row_alpha_a");
        gui::Id const row_beta_id = gui::id("row_beta");
        gui::Id const row_alpha_b_id = gui::id("row_alpha_b");
        char search_text[16] = "aa";
        gui::TableFilterColumn filters[1] = {
            {
                .column = 0u,
                .search_text = search_text,
                .search_text_buffer_size = sizeof(search_text),
            },
        };
        gui::TableFilterDesc const filter_desc = {.columns = slice(filters)};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 100.0f}});
        if (auto table = ui.table(
                table_id,
                {
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::children(),
                                    .height = gui::children(),
                                    .gap = 2.0f,
                                },
                        },
                    .filter = filter_desc,
                }
            )) {
            add_table_text_row(ui, table, row_alpha_a_id, "Alpha", 80.0f);
            add_table_text_row(ui, table, row_beta_id, "Beta", 80.0f);
            add_table_text_row(ui, table, row_alpha_b_id, "Alpha", 80.0f);
        }
        gui::end_frame(ui);

        gui::BoxInfo const* row_alpha_a = ui.find_box(row_alpha_a_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_beta = ui.find_box(row_beta_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_alpha_b = ui.find_box(row_alpha_b_id, gui::BoxKind::TABLE_ROW);
        TEST_EXPECT(context, row_alpha_a != nullptr);
        TEST_EXPECT(context, row_beta != nullptr);
        TEST_EXPECT(context, row_alpha_b != nullptr);
        if (row_alpha_a != nullptr && row_beta != nullptr && row_alpha_b != nullptr) {
            TEST_EXPECT(context, row_alpha_a->rect.max.y > row_alpha_a->rect.min.y);
            TEST_EXPECT(context, row_beta->rect.max.y == 0.0f);
            TEST_EXPECT(context, row_alpha_b->rect.min.y > row_alpha_a->rect.min.y);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_filter_search_hides_non_matching_values) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const row_alpha_id = gui::id("row_alpha");
        gui::Id const row_beta_id = gui::id("row_beta");
        char search_text[16] = "ap";
        bool filter_open = true;
        gui::TableFilterValue values[2] = {{"Alpha"}, {"Beta"}};
        gui::TableFilterColumn filters[1] = {
            {
                .column = 0u,
                .search_text = search_text,
                .search_text_buffer_size = sizeof(search_text),
                .values = slice(values),
                .popup_open = &filter_open,
            },
        };
        gui::TableFilterDesc const filter_desc = {.columns = slice(filters)};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}});
        if (auto table = ui.table(
                table_id,
                {
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::children(),
                                    .height = gui::children(),
                                    .gap = 2.0f,
                                },
                        },
                    .filter = filter_desc,
                }
            )) {
            if (auto header = table.header_row()) {
                BASE_UNUSED(header);
                if (auto cell = header.cell(
                        {.box = {.layout = {.width = gui::px(90.0f), .height = gui::px(28.0f)}}}
                    )) {
                    BASE_UNUSED(cell);
                    BASE_UNUSED(table.filter_button(0u, filter_desc));
                }
            }
            add_table_text_row(ui, table, row_alpha_id, "Alpha", 80.0f);
            add_table_text_row(ui, table, row_beta_id, "Beta", 80.0f);
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::CHECKBOX, "Alpha") != nullptr);
        TEST_EXPECT(context, find_box_text(ui, gui::BoxKind::CHECKBOX, "Beta") == nullptr);
        gui::BoxInfo const* row_alpha = ui.find_box(row_alpha_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_beta = ui.find_box(row_beta_id, gui::BoxKind::TABLE_ROW);
        TEST_EXPECT(context, row_alpha != nullptr);
        TEST_EXPECT(context, row_beta != nullptr);
        if (row_alpha != nullptr && row_beta != nullptr) {
            TEST_EXPECT(context, row_alpha->rect.max.y > row_alpha->rect.min.y);
            TEST_EXPECT(context, row_beta->rect.max.y == 0.0f);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_filter_button_popup_updates_value_filter) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const header_id = gui::id("header");
        gui::Id const row_alpha_id = gui::id("row_alpha");
        gui::Id const row_beta_id = gui::id("row_beta");
        char search_text[16] = {};
        bool filter_open = false;
        gui::TableFilterValue values[2] = {{"Alpha"}, {"Beta"}};
        gui::TableFilterColumn filters[1] = {
            {
                .column = 0u,
                .search_text = search_text,
                .search_text_buffer_size = sizeof(search_text),
                .values = slice(values),
                .popup_open = &filter_open,
            },
        };
        gui::Signal filter_signal = {};

        auto add_table = [&](gui::Frame& ui) -> void {
            filter_signal = {};
            gui::TableFilterDesc const filter_desc = {.columns = slice(filters)};
            if (auto table = ui.table(
                    table_id,
                    {
                        .box =
                            {
                                .layout =
                                    {
                                        .width = gui::children(),
                                        .height = gui::children(),
                                        .gap = 2.0f,
                                    },
                            },
                        .filter = filter_desc,
                    }
                )) {
                if (auto header = table.header_row()) {
                    BASE_UNUSED(header);
                    if (auto cell = header.cell(
                            header_id,
                            {.box = {.layout = {.width = gui::px(90.0f), .height = gui::px(28.0f)}}}
                        )) {
                        BASE_UNUSED(cell);
                        filter_signal = table.filter_button(0u, filter_desc);
                    }
                }
                add_table_text_row(ui, table, row_alpha_id, "Alpha", 80.0f);
                add_table_text_row(ui, table, row_beta_id, "Beta", 80.0f);
            }
        };

        gui::InputState input = {};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}});
        add_table(ui);
        gui::end_frame(ui);

        gui::BoxInfo const* filter_button = find_box_text(ui, gui::BoxKind::BUTTON, "");
        TEST_EXPECT(context, filter_button != nullptr);

        input.mouse_pos = box_center(filter_button);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        TEST_EXPECT(context, filter_open);
        TEST_EXPECT(context, filter_signal.changed);
        gui::BoxInfo const* beta_check = find_box_text(ui, gui::BoxKind::CHECKBOX, "Beta");
        TEST_EXPECT(context, beta_check != nullptr);

        input.mouse_pos = box_center(beta_check);
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 140.0f}, .input = input});
        add_table(ui);
        gui::end_frame(ui);

        gui::BoxInfo const* row_alpha = ui.find_box(row_alpha_id, gui::BoxKind::TABLE_ROW);
        gui::BoxInfo const* row_beta = ui.find_box(row_beta_id, gui::BoxKind::TABLE_ROW);
        TEST_EXPECT(context, !values[1].selected);
        TEST_EXPECT(context, filter_signal.changed);
        TEST_EXPECT(context, row_alpha != nullptr);
        TEST_EXPECT(context, row_beta != nullptr);
        if (row_alpha != nullptr && row_beta != nullptr) {
            TEST_EXPECT(context, row_alpha->rect.max.y > row_alpha->rect.min.y);
            TEST_EXPECT(context, row_beta->rect.max.y == 0.0f);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_layout_supports_row_span_without_header) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const table_id = gui::id("table");
        gui::Id const span_id = gui::id("span");
        gui::Id const top_id = gui::id("top");
        gui::Id const bottom_id = gui::id("bottom");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 80.0f}});
        if (auto table = ui.table(
                table_id,
                {.layout = {
                     .width = gui::children(),
                     .height = gui::children(),
                     .gap = 1.0f,
                 }}
            )) {
            if (auto row = table.row()) {
                {
                    auto cell = row.cell(
                        span_id,
                        {
                            .row_span = 2u,
                            .box = {.layout = {.width = gui::px(10.0f), .height = gui::px(30.0f)}},
                        }
                    );
                    TEST_EXPECT(context, cell);
                }
                {
                    auto cell = row.cell(
                        top_id,
                        {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}}}
                    );
                    TEST_EXPECT(context, cell);
                }
            }
            if (auto row = table.row()) {
                auto cell = row.cell(
                    bottom_id,
                    {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}}}
                );
                TEST_EXPECT(context, cell);
            }
        }
        gui::end_frame(ui);

        gui::BoxInfo const* table = ui.find_box(table_id, gui::BoxKind::TABLE);
        gui::BoxInfo const* span = ui.find_box(span_id, gui::BoxKind::TABLE_CELL);
        gui::BoxInfo const* top = ui.find_box(top_id, gui::BoxKind::TABLE_CELL);
        gui::BoxInfo const* bottom = ui.find_box(bottom_id, gui::BoxKind::TABLE_CELL);

        TEST_EXPECT(context, table != nullptr);
        TEST_EXPECT(context, span != nullptr && top != nullptr && bottom != nullptr);
        if (table != nullptr && span != nullptr && top != nullptr && bottom != nullptr) {
            expect_rect(context, table->rect, {{0.0f, 0.0f}, {31.0f, 30.0f}});
            expect_rect(context, span->rect, {{0.0f, 0.0f}, {10.0f, 30.0f}});
            expect_rect(context, top->rect, {{11.0f, 0.0f}, {31.0f, 14.5f}});
            expect_rect(context, bottom->rect, {{11.0f, 15.5f}, {31.0f, 30.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_cells_clip_overflowing_children) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {80.0f, 40.0f}});
        if (auto table =
                ui.table({.layout = {.width = gui::children(), .height = gui::children()}})) {
            if (auto row = table.row()) {
                if (auto cell = row.cell(
                        {.box = {
                             .layout = {
                                 .width = gui::px(20.0f),
                                 .height = gui::px(20.0f),
                                 .padding = gui::insets(2.0f, 4.0f)
                             }
                         }}
                    )) {
                    BASE_UNUSED(cell);
                    ui.spacer(
                        {.layout = {.width = gui::px(40.0f), .height = gui::px(10.0f)},
                         .style = {.background = gui::rgb(255, 0, 0)}}
                    );
                }
            }
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        gui::draw::StyledRectCommand const* overflow = nullptr;
        for (size_t index = 0u; index < gui::draw::styled_rect_command_count(draw_context);
             ++index) {
            gui::draw::StyledRectCommand const* command =
                gui::draw::styled_rect_command(draw_context, index);
            if (command != nullptr && command->rect.max.x - command->rect.min.x == 40.0f) {
                overflow = command;
            }
        }

        TEST_EXPECT(context, overflow != nullptr);
        if (overflow != nullptr) {
            TEST_EXPECT(context, overflow->clip_rect.min.x == 4.0f);
            TEST_EXPECT(context, overflow->clip_rect.min.y == 2.0f);
            TEST_EXPECT(context, overflow->clip_rect.max.x == 16.0f);
            TEST_EXPECT(context, overflow->clip_rect.max.y == 18.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(table_text_draw_positions_snap_after_fractional_column_expansion) {
        Arena arena = {};
        arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, "Segoe UI", font);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = font;
        theme.root.font_size = 13.0f;

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {.font_cache = cache}, draw_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {60.0f, 30.0f}});
        if (auto table = ui.table(
                {.layout = {.width = gui::px(44.0f), .height = gui::children(), .gap = 1.0f}}
            )) {
            if (auto row = table.row()) {
                if (auto cell = row.cell(
                        {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(20.0f)}}}
                    )) {
                    BASE_UNUSED(cell);
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::fill()}});
                }
                if (auto cell = row.cell(
                        {.box = {.layout = {.width = gui::px(20.0f), .height = gui::px(20.0f)}}}
                    )) {
                    BASE_UNUSED(cell);
                    ui.label("X", {.layout = {.width = gui::fill(), .height = gui::fill()}});
                }
            }
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 1u);
        gui::draw::TextCommand const* command = gui::draw::text_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        if (command != nullptr) {
            TEST_EXPECT(context, command->position.x == 23.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(table_column_and_cell_alignment_position_text) {
        Arena arena = {};
        arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, "Segoe UI", font);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = font;
        theme.root.font_size = 13.0f;

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {.font_cache = cache}, draw_context);

        gui::TableColumnDesc columns[2] = {
            {
                .alignment =
                    {
                        .horizontal = gui::TableAlign::END,
                        .vertical = gui::TableAlign::START,
                    },
            },
            {
                .alignment = {
                    .horizontal = gui::TableAlign::CENTER,
                    .vertical = gui::TableAlign::END,
                },
            },
        };
        gui::Id const cell_a_id = gui::id("align_a");
        gui::Id const cell_b_id = gui::id("align_b");
        gui::Id const cell_c_id = gui::id("align_c");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 100.0f}});
        if (auto table = ui.table({
                .box = {.layout = {.width = gui::children(), .height = gui::children()}},
                .columns = slice(columns),
            })) {
            if (auto row = table.row()) {
                if (auto cell = row.cell(
                        cell_a_id,
                        {.box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}}}
                    )) {
                    BASE_UNUSED(cell);
                    ui.label("A", {.layout = {.width = gui::fill(), .height = gui::fill()}});
                }
                if (auto cell = row.cell(
                        cell_b_id,
                        {.box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}}}
                    )) {
                    BASE_UNUSED(cell);
                    ui.label("B", {.layout = {.width = gui::fill(), .height = gui::fill()}});
                }
            }
            if (auto row = table.row()) {
                if (auto cell = row.cell(
                        cell_c_id,
                        {
                            .box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}},
                            .alignment = {
                                .horizontal = gui::TableAlign::CENTER,
                                .vertical = gui::TableAlign::END,
                            },
                        }
                    )) {
                    BASE_UNUSED(cell);
                    ui.label("C", {.layout = {.width = gui::fill(), .height = gui::fill()}});
                }
            }
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        gui::draw::TextCommand const* a = find_text_command(draw_context, "A");
        gui::draw::TextCommand const* b = find_text_command(draw_context, "B");
        gui::draw::TextCommand const* c = find_text_command(draw_context, "C");
        gui::BoxInfo const* cell_b = ui.find_box(cell_b_id, gui::BoxKind::TABLE_CELL);

        TEST_EXPECT(context, a != nullptr);
        TEST_EXPECT(context, b != nullptr);
        TEST_EXPECT(context, c != nullptr);
        TEST_EXPECT(context, cell_b != nullptr);
        if (a != nullptr && b != nullptr && c != nullptr && cell_b != nullptr) {
            TEST_EXPECT(context, a->position.x > c->position.x);
            TEST_EXPECT(context, c->position.y > a->position.y);
            TEST_EXPECT(context, b->position.y > a->position.y);
            TEST_EXPECT(context, b->position.x > cell_b->rect.min.x + 20.0f);
            TEST_EXPECT(context, b->position.x < cell_b->rect.max.x - 20.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(overlay_places_measured_and_fill_children) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        auto overlay = ui.overlay({
            .layout = {
                .width = gui::fill(),
                .height = gui::fill(),
                .padding = gui::insets(10.0f),
                .align_x = gui::Align::END,
                .align_y = gui::Align::CENTER,
            },
        });
        TEST_EXPECT(context, overlay);
        ui.spacer({.layout = {.width = gui::fill(), .height = gui::fill()}});
        ui.spacer({.layout = {.width = gui::px(20.0f), .height = gui::px(10.0f)}});
        gui::end_frame(ui);

        gui::BoxInfo const* fill = ui.box_info(2u);
        gui::BoxInfo const* fixed = ui.box_info(3u);
        if (fill != nullptr && fixed != nullptr) {
            expect_rect(context, fill->rect, {{10.0f, 10.0f}, {90.0f, 50.0f}});
            expect_rect(context, fixed->rect, {{70.0f, 25.0f}, {90.0f, 35.0f}});
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(popup_floats_over_stack_layout_and_hit_testing) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const popup_id = gui::id("popup");
        gui::Id const button_id = gui::id("behind");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto page = ui.column({.layout = {.width = gui::fill(), .height = gui::fill()}});
            TEST_EXPECT(context, page);
            {
                auto popup = ui.popup(
                    popup_id, {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
                );
                TEST_EXPECT(context, popup);
            }
            ui.button(
                button_id, "Behind", {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::BoxInfo const* popup = ui.find_box(popup_id, gui::BoxKind::POPUP);
        gui::BoxInfo const* button = ui.find_box(button_id, gui::BoxKind::BUTTON);
        gui::BoxInfo const* hit = ui.hit_test({5.0f, 5.0f});
        TEST_EXPECT(context, popup != nullptr && button != nullptr);
        if (popup != nullptr && button != nullptr) {
            expect_rect(context, popup->rect, {{0.0f, 0.0f}, {40.0f, 20.0f}});
            expect_rect(context, button->rect, {{0.0f, 0.0f}, {40.0f, 20.0f}});
            TEST_EXPECT(context, hit != nullptr && hit->id.value == popup->id.value);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(popup_drags_by_background_and_clamps_to_root) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const popup_id = gui::id("popup");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto panel = ui.overlay(
                gui::id("clipped_panel"),
                {.layout = {.width = gui::px(50.0f), .height = gui::px(40.0f), .clip = true}}
            );
            TEST_EXPECT(context, panel);
            {
                auto popup = ui.popup(
                    popup_id, {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
                );
                TEST_EXPECT(context, popup);
            }
        }
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}, .input = input});
        {
            auto panel = ui.overlay(
                gui::id("clipped_panel"),
                {.layout = {.width = gui::px(50.0f), .height = gui::px(40.0f), .clip = true}}
            );
            TEST_EXPECT(context, panel);
            {
                auto popup = ui.popup(
                    popup_id, {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
                );
                TEST_EXPECT(context, popup.signal().active);
            }
        }
        gui::end_frame(ui);

        input.mouse_pos = {95.0f, 75.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}, .input = input});
        {
            auto panel = ui.overlay(
                gui::id("clipped_panel"),
                {.layout = {.width = gui::px(50.0f), .height = gui::px(40.0f), .clip = true}}
            );
            TEST_EXPECT(context, panel);
            {
                auto popup = ui.popup(
                    popup_id, {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
                );
                TEST_EXPECT(context, popup.signal().active);
            }
        }
        gui::end_frame(ui);

        gui::BoxInfo const* popup = ui.find_box(popup_id, gui::BoxKind::POPUP);
        gui::BoxInfo const* hit = ui.hit_test({95.0f, 75.0f});
        TEST_EXPECT(context, popup != nullptr);
        if (popup != nullptr) {
            expect_rect(context, popup->rect, {{60.0f, 60.0f}, {100.0f, 80.0f}});
            TEST_EXPECT(context, hit != nullptr && hit->id.value == popup->id.value);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(hover_popup_stays_open_over_source_or_popup) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const source_id = gui::id("source");
        gui::Id const popup_id = gui::id("hover_popup");
        auto add_ui = [&](gui::Frame& frame, gui::Signal* out_source) -> void {
            gui::Signal const source = frame.button(
                source_id, "Info", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}}
            );
            if (out_source != nullptr) {
                *out_source = source;
            }
            if (auto popup = frame.hover_popup(
                    popup_id,
                    source,
                    {.layout = {
                         .width = gui::px(70.0f),
                         .height = gui::px(20.0f),
                         .margin = gui::insets(20.0f, 0.0f, 0.0f, 0.0f),
                     }}
                )) {
                frame.label("Details", {.layout = {.width = gui::fill(), .height = gui::fill()}});
            }
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        add_ui(ui, nullptr);
        gui::end_frame(ui);
        TEST_EXPECT(context, ui.find_box(popup_id, gui::BoxKind::POPUP) == nullptr);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        gui::Signal source = {};
        add_ui(ui, &source);
        gui::end_frame(ui);
        TEST_EXPECT(context, source.hovered);
        TEST_EXPECT(context, source.hover_entered);
        TEST_EXPECT(context, ui.find_box(popup_id, gui::BoxKind::POPUP) != nullptr);

        input.mouse_pos = {5.0f, 25.0f};
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        add_ui(ui, &source);
        gui::end_frame(ui);
        TEST_EXPECT(context, !source.hovered);
        TEST_EXPECT(context, source.hover_exited);
        TEST_EXPECT(context, ui.find_box(popup_id, gui::BoxKind::POPUP) != nullptr);

        input.mouse_pos = {100.0f, 50.0f};
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        add_ui(ui, nullptr);
        gui::end_frame(ui);
        TEST_EXPECT(context, ui.find_box(popup_id, gui::BoxKind::POPUP) == nullptr);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(modal_fills_root_and_blocks_later_normal_hits) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const modal_id = gui::id("modal");
        gui::Id const ok_id = gui::id("ok");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto page = ui.column({.layout = {.width = gui::fill(), .height = gui::fill()}});
            TEST_EXPECT(context, page);
            {
                auto modal = ui.modal(
                    modal_id,
                    {.layout = {
                         .align_x = gui::Align::CENTER,
                         .align_y = gui::Align::CENTER,
                     }}
                );
                TEST_EXPECT(context, modal);
                ui.button(
                    ok_id, "OK", {.layout = {.width = gui::px(20.0f), .height = gui::px(20.0f)}}
                );
            }
            ui.button("Behind", {.layout = {.width = gui::px(100.0f), .height = gui::px(80.0f)}});
        }
        gui::end_frame(ui);

        gui::BoxInfo const* modal = ui.find_box(modal_id, gui::BoxKind::MODAL);
        gui::BoxInfo const* ok = ui.find_box(ok_id, gui::BoxKind::BUTTON);
        TEST_EXPECT(context, modal != nullptr && ok != nullptr);
        if (modal != nullptr && ok != nullptr) {
            expect_rect(context, modal->rect, {{0.0f, 0.0f}, {100.0f, 80.0f}});
            expect_rect(context, ok->rect, {{40.0f, 30.0f}, {60.0f, 50.0f}});

            gui::BoxInfo const* backdrop_hit = ui.hit_test({5.0f, 5.0f});
            gui::BoxInfo const* button_hit = ui.hit_test({45.0f, 35.0f});
            TEST_EXPECT(
                context, backdrop_hit != nullptr && backdrop_hit->id.value == modal->id.value
            );
            TEST_EXPECT(context, button_hit != nullptr && button_hit->id.value == ok->id.value);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(scroll_panel_reports_metrics_and_applies_programmatic_scroll) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const thread_id = gui::id("thread");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        ui.set_scroll_y(thread_id, 22.0f);
        {
            auto panel = ui.scroll_panel(
                thread_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}}
            );
            TEST_EXPECT(context, panel);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(thread_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 22.0f);
        TEST_EXPECT(context, state.max_y == 30.0f);
        TEST_EXPECT(context, state.viewport_height == 30.0f);
        TEST_EXPECT(context, state.content_height == 60.0f);

        gui::BoxInfo const* first = ui.box_info(2u);
        if (first != nullptr) {
            expect_rect(context, first->rect, {{0.0f, -22.0f}, {100.0f, -2.0f}});
        }

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        ui.scroll_to_end(thread_id);
        {
            auto panel = ui.scroll_panel(
                thread_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}}
            );
            TEST_EXPECT(context, panel);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        state = ui.scroll_state(thread_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 30.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(scroll_panel_renders_vertical_scrollbar_when_content_overflows) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Id const panel_id = gui::id("panel");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        ui.set_scroll_y(panel_id, 15.0f);
        {
            auto panel = ui.scroll_panel(
                panel_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}}
            );
            TEST_EXPECT(context, panel);
            ui.spacer(
                {.layout = {.width = gui::fill(), .height = gui::px(20.0f)},
                 .style = {.background = gui::rgb(1, 2, 3)}}
            );
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 3u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == 3u);
        gui::draw::Command const* content_command = gui::draw::command(draw_context, 0u);
        gui::draw::Command const* track_command = gui::draw::command(draw_context, 1u);
        gui::draw::Command const* thumb_command = gui::draw::command(draw_context, 2u);
        TEST_EXPECT(
            context,
            content_command != nullptr &&
                content_command->kind == gui::draw::CommandKind::STYLED_RECT
        );
        TEST_EXPECT(
            context,
            track_command != nullptr && track_command->kind == gui::draw::CommandKind::STYLED_RECT
        );
        TEST_EXPECT(
            context,
            thumb_command != nullptr && thumb_command->kind == gui::draw::CommandKind::STYLED_RECT
        );
        gui::draw::StyledRectCommand const* track =
            gui::draw::styled_rect_command(draw_context, 1u);
        gui::draw::StyledRectCommand const* thumb =
            gui::draw::styled_rect_command(draw_context, 2u);
        TEST_EXPECT(context, track != nullptr);
        TEST_EXPECT(context, thumb != nullptr);
        if (track != nullptr && thumb != nullptr) {
            TEST_EXPECT(context, track->rect.min.x == 92.0f);
            TEST_EXPECT(context, track->rect.max.x == 98.0f);
            TEST_EXPECT(context, track->rect.min.y == 2.0f);
            TEST_EXPECT(context, track->rect.max.y == 28.0f);
            TEST_EXPECT(context, thumb->rect.min.x == 92.0f);
            TEST_EXPECT(context, thumb->rect.max.x == 98.0f);
            TEST_EXPECT(context, thumb->rect.min.y == 8.5f);
            TEST_EXPECT(context, thumb->rect.max.y == 21.5f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(scroll_panel_insets_scrollbar_inside_rounded_corners) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Id const panel_id = gui::id("panel");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        {
            auto panel = ui.scroll_panel(
                panel_id,
                {.layout = {.width = gui::fill(), .height = gui::px(30.0f)},
                 .style = {.radius = 14.0f}}
            );
            TEST_EXPECT(context, panel);
            for (size_t index = 0u; index < 3u; ++index) {
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            }
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 2u);
        gui::draw::StyledRectCommand const* track =
            gui::draw::styled_rect_command(draw_context, 0u);
        gui::draw::StyledRectCommand const* thumb =
            gui::draw::styled_rect_command(draw_context, 1u);
        TEST_EXPECT(context, track != nullptr);
        TEST_EXPECT(context, thumb != nullptr);
        if (track != nullptr && thumb != nullptr) {
            TEST_EXPECT(context, track->rect.min.y > 2.0f);
            TEST_EXPECT(context, track->rect.max.y < 28.0f);
            TEST_EXPECT(context, thumb->rect.min.y >= track->rect.min.y);
            TEST_EXPECT(context, thumb->rect.max.y <= track->rect.max.y);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(scroll_panel_scrollbar_click_and_drag_updates_scroll) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const panel_id = gui::id("panel");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {95.0f, 21.5f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}, .input = input});
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(panel_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 30.0f);

        input = {};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}, .input = input});
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}});
        ui.set_scroll_y(panel_id, 15.0f);
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        input = {};
        input.mouse_pos = {95.0f, 10.5f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}, .input = input});
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        input.mouse_pos = {95.0f, 17.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 50.0f}, .input = input});
        add_basic_scroll_panel(context, ui, panel_id);
        gui::end_frame(ui);

        state = ui.scroll_state(panel_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 30.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(hit_test_returns_deepest_box) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto page = ui.column(
                gui::id("page"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::fill(),
                        .padding = gui::insets(10.0f),
                    },
                }
            );
            TEST_EXPECT(context, page);
            auto toolbar = ui.row(
                gui::id("toolbar"), {.layout = {.width = gui::px(80.0f), .height = gui::px(30.0f)}}
            );
            TEST_EXPECT(context, toolbar);
            ui.button(
                gui::id("run"),
                "Run",
                {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::BoxInfo const* button = ui.box_info(3u);
        gui::BoxInfo const* hit = ui.hit_test({15.0f, 15.0f});
        TEST_EXPECT(context, button != nullptr && button->kind == gui::BoxKind::BUTTON);
        TEST_EXPECT(
            context, hit != nullptr && button != nullptr && hit->id.value == button->id.value
        );

        gui::destroy_context(gui_context);
    }

    TEST_CASE(hit_test_honors_scroll_panel_clip) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const panel_id = gui::id("panel");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        ui.set_scroll_y(panel_id, 15.0f);
        {
            auto panel = ui.scroll_panel(
                panel_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}}
            );
            TEST_EXPECT(context, panel);
            ui.label(
                gui::id("first"),
                "First",
                {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}
            );
            ui.label(
                gui::id("second"),
                "Second",
                {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}
            );
            ui.label(
                gui::id("third"),
                "Third",
                {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(2u);
        gui::BoxInfo const* second = ui.box_info(3u);
        gui::BoxInfo const* visible_hit = ui.hit_test({5.0f, 10.0f});
        gui::BoxInfo const* clipped_hit = ui.hit_test({5.0f, 40.0f});
        TEST_EXPECT(context, first != nullptr && first->kind == gui::BoxKind::LABEL);
        TEST_EXPECT(context, second != nullptr && second->kind == gui::BoxKind::LABEL);
        TEST_EXPECT(
            context,
            visible_hit != nullptr && second != nullptr && visible_hit->id.value == second->id.value
        );
        TEST_EXPECT(context, clipped_hit != nullptr && clipped_hit->kind == gui::BoxKind::ROOT);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(metadata_lookup_stays_stable_across_frames) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const run_id = gui::id("run");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        {
            auto page = ui.column(gui::id("page"));
            TEST_EXPECT(context, page);
            ui.button(
                run_id, "Run", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(2u);
        TEST_EXPECT(context, first != nullptr);
        gui::Id const resolved_id = first != nullptr ? first->id : gui::Id{};
        if (first != nullptr) {
            TEST_EXPECT(context, first->kind == gui::BoxKind::BUTTON);
            TEST_EXPECT(context, first->authored_id.value == run_id.value);
            TEST_EXPECT(context, first->id_source == gui::BoxIdSource::EXPLICIT);
            TEST_EXPECT(context, first->stable_id);
            TEST_EXPECT(context, ui.find_box(resolved_id, gui::BoxKind::BUTTON) == first);
        }

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        {
            auto page = ui.column(gui::id("page"));
            TEST_EXPECT(context, page);
            ui.label("Intro", {.layout = {.height = gui::px(10.0f)}});
            ui.button(
                run_id, "Run", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::BoxInfo const* second = ui.find_box(resolved_id, gui::BoxKind::BUTTON);
        TEST_EXPECT(context, second != nullptr);
        if (second != nullptr) {
            TEST_EXPECT(context, second->id.value == resolved_id.value);
            TEST_EXPECT(context, second->stable_id);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(metadata_query_failures_return_null) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {80.0f, 30.0f}});
        ui.button("Run", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::BoxInfo const* button = ui.box_info(1u);
        TEST_EXPECT(context, button != nullptr && button->kind == gui::BoxKind::BUTTON);
        TEST_EXPECT(context, ui.box_info(99u) == nullptr);
        TEST_EXPECT(context, ui.find_box({}) == nullptr);
        TEST_EXPECT(context, ui.find_box(gui::id("missing")) == nullptr);
        if (button != nullptr) {
            TEST_EXPECT(context, ui.find_box(button->id, gui::BoxKind::LABEL) == nullptr);
        }
        TEST_EXPECT(context, ui.hit_test({-1.0f, 5.0f}) == nullptr);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(button_signal_uses_text_identity_across_frames) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        gui::Signal signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, !signal.pressed_left);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.focus_gained);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, signal.clicked_left);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, !signal.active);
        gui::end_frame(ui);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(tab_focus_moves_through_previous_frame_order) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.button("One", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        ui.button("Two", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.button("One", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        ui.button("Two", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.button("One", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        ui.button("Two", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {{.key = gui::Key::TAB}};
        input = {};
        input.key_events = events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        gui::Signal const first =
            ui.button("One", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::Signal const second =
            ui.button("Two", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        TEST_EXPECT(context, first.focus_lost);
        TEST_EXPECT(context, second.focused);
        TEST_EXPECT(context, second.focus_gained);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(checkbox_and_slider_mutate_app_owned_values) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        bool enabled = false;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
        );
        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, !enabled);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
        );
        TEST_EXPECT(context, signal.clicked_left);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, enabled);
        gui::end_frame(ui);

        float value = 0.0f;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.slider_float(
            "Scale",
            &value,
            {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}}
        );
        gui::end_frame(ui);

        input = {};
        input.mouse_pos = {50.0f, 10.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.slider_float(
            "Scale",
            &value,
            {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}}
        );
        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, value == 0.5f);
        gui::end_frame(ui);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(radio_button_mutates_selected_index) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        size_t selected = 0u;
        gui::BoxDesc const option_box = {
            .layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}
        };
        gui::Id const small_id = gui::id("small");
        gui::Id const large_id = gui::id("large");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.radio_button(small_id, "Small", &selected, 0u, option_box);
        ui.radio_button(large_id, "Large", &selected, 1u, option_box);
        gui::end_frame(ui);

        gui::BoxInfo const* small = ui.find_box(small_id, gui::BoxKind::RADIO_BUTTON);
        gui::BoxInfo const* large = ui.find_box(large_id, gui::BoxKind::RADIO_BUTTON);
        TEST_EXPECT(context, small != nullptr && large != nullptr);
        if (small != nullptr && large != nullptr) {
            TEST_EXPECT(context, small->text == StrRef("Small"));
            TEST_EXPECT(context, large->text == StrRef("Large"));
        }

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 25.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.radio_button(small_id, "Small", &selected, 0u, option_box);
        gui::Signal signal = ui.radio_button(large_id, "Large", &selected, 1u, option_box);
        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selected == 0u);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.radio_button(small_id, "Small", &selected, 0u, option_box);
        signal = ui.radio_button(large_id, "Large", &selected, 1u, option_box);
        TEST_EXPECT(context, signal.clicked_left);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selected == 1u);
        gui::end_frame(ui);

        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.radio_button(small_id, "Small", &selected, 0u, option_box);
        signal = ui.radio_button(large_id, "Large", &selected, 1u, option_box);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selected == 1u);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.radio_button(small_id, "Small", &selected, 0u, option_box);
        signal = ui.radio_button(large_id, "Large", &selected, 1u, option_box);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selected == 1u);
        gui::end_frame(ui);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(slider_uses_explicit_focus_and_keyboard_step) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const scale_id = gui::id("scale");
        float value = 0.5f;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.slider_float(
            scale_id,
            "Scale",
            &value,
            {
                .box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}},
                .step = 0.25f,
            }
        );
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {{.key = gui::Key::RIGHT}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        ui.request_focus(scale_id);
        gui::Signal const signal = ui.slider_float(
            scale_id,
            "Scale",
            &value,
            {
                .box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}},
                .step = 0.25f,
            }
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.focus_gained);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, value == 0.75f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_mutates_app_owned_buffer) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("name");
        char buffer[16] = "Hi";
        gui::KeyEvent const events[] = {
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '!'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Name",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("Hi!"));

        gui::BoxInfo const* field = ui.find_box(field_id, gui::BoxKind::INPUT_TEXT);
        TEST_EXPECT(context, field != nullptr);
        if (field != nullptr) {
            TEST_EXPECT(context, field->text == StrRef("Hi!"));
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_pastes_clipboard_text_with_ctrl_v) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        clipboard.text_size = StrRef("there").copy_to(clipboard.text, sizeof(clipboard.text));

        gui::Context gui_context = {};
        gui::create_context(
            arena,
            {.get_clipboard_text = read_clipboard_text, .clipboard_user_data = &clipboard},
            gui_context
        );

        gui::Id const field_id = gui::id("field");
        char buffer[16] = "Hi ";
        gui::KeyEvent const events[] = {{.key = gui::Key::V, .mods = gui::KEY_MOD_CTRL}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Field",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, clipboard.read_count == 1u);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("Hi there"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_activates_on_enter) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        char buffer[16] = "Hi";
        gui::KeyEvent const events[] = {
            {.key = gui::Key::ENTER},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '\r'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 2u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Field",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("Hi"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_ctrl_a_selects_all_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        char buffer[16] = "alpha";
        gui::KeyEvent const events[] = {
            {.key = gui::Key::A, .mods = gui::KEY_MOD_CTRL},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 2u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Field",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("X"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_cuts_selected_text_with_ctrl_x) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        gui::Context gui_context = {};
        gui::create_context(
            arena,
            {.set_clipboard_text = capture_clipboard_text, .clipboard_user_data = &clipboard},
            gui_context
        );

        gui::Id const field_id = gui::id("field");
        char buffer[16] = "alpha beta";
        gui::KeyEvent const events[] = {
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::X, .mods = gui::KEY_MOD_CTRL},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 5u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Field",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(160.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == StrRef("beta"));
        TEST_EXPECT(context, StrRef(buffer) == StrRef("alpha "));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_ctrl_z_reverts_changes_one_at_a_time) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}};
        char buffer[16] = "Hi";
        gui::KeyEvent const text_events[] = {
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '!'},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '?'},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '*'},
        };
        gui::InputState input = {};
        input.key_events = text_events;
        input.key_event_count = 3u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("Hi!?*"));

        gui::KeyEvent const undo_events[] = {{.key = gui::Key::Z, .mods = gui::KEY_MOD_CTRL}};
        input = {};
        input.key_events = undo_events;
        input.key_event_count = 1u;

        char const* expected[] = {"Hi!?", "Hi!", "Hi"};
        for (size_t index = 0u; index < 3u; ++index) {
            ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
            signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == StrRef(expected[index]));
        }

        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("Hi"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_handles_cursor_keys_and_backspace) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        char buffer[16] = "abc";
        gui::KeyEvent const events[] = {
            {.key = gui::Key::LEFT},
            {.key = gui::Key::BACKSPACE},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 3u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text(
            field_id,
            "Field",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("aXc"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_ctrl_word_navigation_and_editing) {
        Arena arena = {};
        arena.init();

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(160.0f), .height = gui::px(20.0f)}};

        auto expect_edit = [&](char* buffer,
                               size_t buffer_size,
                               gui::KeyEvent const* events,
                               size_t event_count,
                               StrRef expected) -> void {
            gui::Context gui_context = {};
            gui::create_context(arena, {}, gui_context);

            gui::InputState input = {};
            input.key_events = events;
            input.key_event_count = event_count;

            gui::Frame ui =
                gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
            ui.request_focus(field_id);
            gui::Signal const signal = ui.input_text(field_id, "Field", buffer, buffer_size, box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == expected);

            gui::destroy_context(gui_context);
        };

        {
            char buffer[32] = "alpha beta gamma";
            gui::KeyEvent const events[] = {
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_CTRL},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            expect_edit(buffer, sizeof(buffer), events, 2u, "alpha beta Xgamma");
        }

        {
            char buffer[32] = "alpha beta gamma";
            gui::KeyEvent const events[] = {
                {.key = gui::Key::HOME},
                {.key = gui::Key::RIGHT, .mods = gui::KEY_MOD_CTRL},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            expect_edit(buffer, sizeof(buffer), events, 3u, "alpha Xbeta gamma");
        }

        {
            char buffer[32] = "alpha beta gamma";
            gui::KeyEvent const events[] = {
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_CTRL | gui::KEY_MOD_SHIFT},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            expect_edit(buffer, sizeof(buffer), events, 2u, "alpha beta X");
        }

        {
            char buffer[32] = "alpha beta gamma";
            gui::KeyEvent const events[] = {
                {.key = gui::Key::BACKSPACE, .mods = gui::KEY_MOD_CTRL},
            };
            expect_edit(buffer, sizeof(buffer), events, 1u, "alpha beta ");
        }

        {
            char buffer[32] = "alpha beta gamma";
            gui::KeyEvent const events[] = {
                {.key = gui::Key::HOME},
                {.key = gui::Key::DELETE_KEY, .mods = gui::KEY_MOD_CTRL},
            };
            expect_edit(buffer, sizeof(buffer), events, 2u, "beta gamma");
        }
    }

    TEST_CASE(input_text_drag_selection_replaces_selected_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}};
        char buffer[16] = "ABCDE";

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {9.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("ABCDE"));

        input.mouse_pos = {34.0f, 5.0f};
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("ABCDE"));

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        input = {};
        input.key_events = events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
        signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("AXE"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_double_click_selects_word_for_copy_and_replace) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        gui::Context gui_context = {};
        gui::create_context(
            arena,
            {.set_clipboard_text = capture_clipboard_text, .clipboard_user_data = &clipboard},
            gui_context
        );

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(160.0f), .height = gui::px(20.0f)}};
        char buffer[16] = "alpha beta";

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {54.0f, 5.0f};
        input.mouse_down[0u] = true;
        input.mouse_double_clicked[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::KeyEvent const copy_events[] = {{.key = gui::Key::C, .mods = gui::KEY_MOD_CTRL}};
        input = {};
        input.key_events = copy_events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == StrRef("beta"));

        gui::KeyEvent const text_events[] = {
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        input = {};
        input.key_events = text_events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef("alpha X"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_triple_click_selects_all_for_delete) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(160.0f), .height = gui::px(20.0f)}};
        char buffer[16] = "alpha beta";

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {54.0f, 5.0f};
        input.mouse_down[0u] = true;
        input.mouse_double_clicked[0u] = true;
        input.mouse_triple_clicked[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {{.key = gui::Key::BACKSPACE}};
        input = {};
        input.key_events = events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 40.0f}, .input = input});
        gui::Signal const signal = ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, StrRef(buffer) == StrRef(""));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_shift_arrows_expand_and_shrink_selection) {
        Arena arena = {};
        arena.init();

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}};
        auto select_bcd =
            [&](gui::Context current_context, char* buffer, size_t buffer_size) -> void {
            gui::Frame ui = gui::begin_frame(current_context, {.size = {160.0f, 40.0f}});
            ui.input_text(field_id, "Field", buffer, buffer_size, box);
            gui::end_frame(ui);

            gui::InputState input = {};
            input.mouse_pos = {9.0f, 5.0f};
            input.mouse_down[0u] = true;
            ui = gui::begin_frame(current_context, {.size = {160.0f, 40.0f}, .input = input});
            ui.input_text(field_id, "Field", buffer, buffer_size, box);
            gui::end_frame(ui);

            input.mouse_pos = {34.0f, 5.0f};
            ui = gui::begin_frame(current_context, {.size = {160.0f, 40.0f}, .input = input});
            ui.input_text(field_id, "Field", buffer, buffer_size, box);
            gui::end_frame(ui);

            input.mouse_down[0u] = false;
            ui = gui::begin_frame(current_context, {.size = {160.0f, 40.0f}, .input = input});
            ui.input_text(field_id, "Field", buffer, buffer_size, box);
            gui::end_frame(ui);
        };

        {
            gui::Context gui_context = {};
            gui::create_context(arena, {}, gui_context);
            char buffer[16] = "ABCDE";

            gui::KeyEvent const events[] = {
                {.key = gui::Key::LEFT},
                {.key = gui::Key::LEFT},
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            gui::InputState input = {};
            input.key_events = events;
            input.key_event_count = 4u;
            gui::Frame ui =
                gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
            ui.request_focus(field_id);
            gui::Signal const signal =
                ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == StrRef("ABXDE"));

            gui::destroy_context(gui_context);
        }

        {
            gui::Context gui_context = {};
            gui::create_context(arena, {}, gui_context);
            char buffer[16] = "ABCDE";

            gui::KeyEvent const events[] = {
                {.key = gui::Key::LEFT},
                {.key = gui::Key::LEFT},
                {.key = gui::Key::RIGHT, .mods = gui::KEY_MOD_SHIFT},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            gui::InputState input = {};
            input.key_events = events;
            input.key_event_count = 4u;
            gui::Frame ui =
                gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
            ui.request_focus(field_id);
            gui::Signal const signal =
                ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == StrRef("ABCXE"));

            gui::destroy_context(gui_context);
        }

        {
            gui::Context gui_context = {};
            gui::create_context(arena, {}, gui_context);
            char buffer[16] = "ABCDE";

            select_bcd(gui_context, buffer, sizeof(buffer));

            gui::KeyEvent const events[] = {
                {.key = gui::Key::RIGHT, .mods = gui::KEY_MOD_SHIFT},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            gui::InputState input = {};
            input.key_events = events;
            input.key_event_count = 2u;
            gui::Frame ui =
                gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
            gui::Signal const signal =
                ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == StrRef("AX"));

            gui::destroy_context(gui_context);
        }

        {
            gui::Context gui_context = {};
            gui::create_context(arena, {}, gui_context);
            char buffer[16] = "ABCDE";

            select_bcd(gui_context, buffer, sizeof(buffer));

            gui::KeyEvent const events[] = {
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
                {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
                {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            };
            gui::InputState input = {};
            input.key_events = events;
            input.key_event_count = 4u;
            gui::Frame ui =
                gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}, .input = input});
            gui::Signal const signal =
                ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
            gui::end_frame(ui);

            TEST_EXPECT(context, signal.changed);
            TEST_EXPECT(context, StrRef(buffer) == StrRef("AXBCDE"));

            gui::destroy_context(gui_context);
        }
    }

    TEST_CASE(input_text_uses_label_identity_across_value_changes) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        char buffer[16] = "first";
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}});
        ui.input_text(
            "Name",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(1u);
        gui::Id const field_id = first != nullptr ? first->id : gui::Id{};
        TEST_EXPECT(context, first != nullptr && first->text == StrRef("first"));

        buffer[0] = 's';
        buffer[1] = 'e';
        buffer[2] = 'c';
        buffer[3] = 'o';
        buffer[4] = 'n';
        buffer[5] = 'd';
        buffer[6] = '\0';

        ui = gui::begin_frame(gui_context, {.size = {160.0f, 40.0f}});
        ui.input_text(
            "Name",
            buffer,
            sizeof(buffer),
            {.layout = {.width = gui::px(120.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* second = ui.box_info(1u);
        TEST_EXPECT(context, second != nullptr && second->id.value == field_id.value);
        TEST_EXPECT(context, second != nullptr && second->text == StrRef("second"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_keeps_vertical_position_when_empty_and_text_changes) {
        Arena arena = {};
        arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, "Segoe UI", font);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = font;
        theme.root.font_size = 13.0f;

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {.font_cache = cache}, draw_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {
            .layout = {
                .width = gui::px(160.0f),
                .height = gui::px(30.0f),
                .padding = gui::insets(5.0f, 8.0f),
            },
        };

        char empty[16] = "";
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 50.0f}});
        ui.request_focus(field_id);
        ui.input_text(field_id, "Field", empty, sizeof(empty), box);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        gui::draw::StyledRectCommand const* empty_caret =
            gui::draw::styled_rect_command(draw_context, 1u);
        TEST_EXPECT(context, empty_caret != nullptr);
        bool const has_empty_caret = empty_caret != nullptr;
        float const empty_caret_y = has_empty_caret ? empty_caret->rect.min.y : 0.0f;

        char lower[16] = "a";
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 50.0f}});
        ui.input_text(field_id, "Field", lower, sizeof(lower), box);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        gui::draw::StyledRectCommand const* lower_caret =
            gui::draw::styled_rect_command(draw_context, 1u);
        gui::draw::TextCommand const* lower_text = gui::draw::text_command(draw_context, 0u);
        TEST_EXPECT(context, lower_caret != nullptr);
        TEST_EXPECT(context, lower_text != nullptr);
        bool const has_lower_caret = lower_caret != nullptr;
        bool const has_lower_text = lower_text != nullptr;
        float const lower_caret_y = has_lower_caret ? lower_caret->rect.min.y : 0.0f;
        float const lower_text_y = has_lower_text ? lower_text->position.y : 0.0f;

        char tall[16] = "ad";
        ui = gui::begin_frame(gui_context, {.size = {180.0f, 50.0f}});
        ui.input_text(field_id, "Field", tall, sizeof(tall), box);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        gui::draw::TextCommand const* tall_text = gui::draw::text_command(draw_context, 0u);
        TEST_EXPECT(context, tall_text != nullptr);
        bool const has_tall_text = tall_text != nullptr;
        float const tall_text_y = has_tall_text ? tall_text->position.y : 0.0f;

        if (has_empty_caret && has_lower_caret) {
            TEST_EXPECT(context, empty_caret_y == lower_caret_y);
        }
        if (has_lower_text && has_tall_text) {
            TEST_EXPECT(context, lower_text_y == tall_text_y);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(input_text_scrolls_horizontally_to_keep_caret_visible) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}};
        char buffer[32] = "abcdefghi";

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {80.0f, 40.0f}});
        ui.request_focus(field_id);
        ui.input_text(field_id, "Field", buffer, sizeof(buffer), box);
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        gui::draw::StyledRectCommand const* caret =
            gui::draw::styled_rect_command(draw_context, 1u);
        TEST_EXPECT(context, caret != nullptr);
        if (caret != nullptr) {
            TEST_EXPECT(context, caret->rect.min.x >= 0.0f);
            TEST_EXPECT(context, caret->rect.max.x <= 40.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_expands_and_inserts_newline_without_activation) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.init(2u));
        TEST_EXPECT(context, buffer.write_string("Hi") == 2u);

        gui::KeyEvent const events[] = {
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '!'},
            {.key = gui::Key::ENTER},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = '\r'},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 4u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 80.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {.box = {.layout = {.width = gui::px(140.0f), .height = gui::px(48.0f)}}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, !signal.activated);
        TEST_EXPECT(context, buffer.capacity() > 2u);
        TEST_EXPECT(context, buffer.str() == StrRef("Hi!\nX"));

        gui::BoxInfo const* field = ui.find_box(field_id, gui::BoxKind::INPUT_TEXT_MULTILINE);
        TEST_EXPECT(context, field != nullptr);
        if (field != nullptr) {
            TEST_EXPECT(context, field->text == StrRef("Hi!\nX"));
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_tab_inserts_text_and_keeps_focus) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("a") == 1u);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {220.0f, 80.0f}});
        ui.request_focus(field_id);
        ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {.box = {.layout = {.width = gui::px(140.0f), .height = gui::px(48.0f)}}}
        );
        ui.button(
            gui::id("next"), "Next", {.layout = {.width = gui::px(60.0f), .height = gui::px(24.0f)}}
        );
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {{.key = gui::Key::TAB}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;

        ui = gui::begin_frame(gui_context, {.size = {220.0f, 80.0f}, .input = input});
        gui::Signal const field = ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {.box = {.layout = {.width = gui::px(140.0f), .height = gui::px(48.0f)}}}
        );
        gui::Signal const next = ui.button(
            gui::id("next"), "Next", {.layout = {.width = gui::px(60.0f), .height = gui::px(24.0f)}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, field.focused);
        TEST_EXPECT(context, !next.focused);
        TEST_EXPECT(context, field.changed);
        TEST_EXPECT(context, buffer.str() == StrRef("a    "));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_uses_configured_tab_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("a") == 1u);

        gui::KeyEvent const events[] = {{.key = gui::Key::TAB}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 80.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {
                .box = {.layout = {.width = gui::px(140.0f), .height = gui::px(48.0f)}},
                .tab_text = "\t",
            }
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, buffer.str() == StrRef("a\t"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_up_down_moves_cursor_by_line) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("abc\nde\nghi") == 10u);

        gui::KeyEvent const events[] = {
            {.key = gui::Key::UP},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
            {.key = gui::Key::DOWN},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'Y'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 4u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {180.0f, 80.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal = ui.input_text_multiline(
            field_id,
            "Field",
            &buffer,
            {.box = {.layout = {.width = gui::px(140.0f), .height = gui::px(48.0f)}}}
        );
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, buffer.str() == StrRef("abc\ndeX\nghiY"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_word_wrap_up_down_moves_cursor_by_visual_line) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {
            .layout = {
                .width = gui::px(16.0f),
                .height = gui::px(40.0f),
                .word_wrap = true,
            }
        };
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("AB CD") == 5u);

        gui::KeyEvent const events[] = {
            {.key = gui::Key::UP},
            {.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'},
        };
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 2u;

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}, .input = input});
        ui.request_focus(field_id);
        gui::Signal const signal =
            ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, buffer.str() == StrRef("ABX CD"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_scroll_moves_hidden_cursor_to_visible_line) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}};
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("A\nB\nC\nD\nE") == 9u);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        ui.request_focus(field_id);
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(field_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 60.0f);

        gui::InputState scroll = {};
        scroll.mouse_pos = {5.0f, 5.0f};
        scroll.scroll_delta_y = 20.0f;

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = scroll});
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        state = ui.scroll_state(field_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 40.0f);

        gui::KeyEvent const events[] = {{.kind = gui::KeyEventKind::TEXT, .codepoint = 'X'}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        TEST_EXPECT(context, buffer.str() == StrRef("A\nB\nC\nDX\nE"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_text_multiline_drag_selection_scrolls_beyond_bounds) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const field_id = gui::id("field");
        gui::BoxDesc const box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}};
        StringBuffer buffer;
        TEST_EXPECT(context, buffer.write_string("A\nB\nC\nD\nE") == 9u);

        gui::KeyEvent const home[] = {{.key = gui::Key::HOME}};
        gui::InputState input = {};
        input.key_events = home;
        input.key_event_count = 1u;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.request_focus(field_id);
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        input.mouse_pos = {5.0f, 45.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.input_text_multiline(field_id, "Field", &buffer, {.box = box});
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(field_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y >= 19.0f && state.y <= 21.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(explicit_ids_disambiguate_same_text_widgets) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.button(gui::id("primary"), "Apply");
        ui.button(gui::id("secondary"), "Apply");
        bool first_value = false;
        bool second_value = false;
        ui.checkbox(gui::id("first_check"), "Enabled", &first_value);
        ui.checkbox(gui::id("second_check"), "Enabled", &second_value);
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(1u);
        gui::BoxInfo const* second = ui.box_info(2u);
        gui::BoxInfo const* first_check = ui.box_info(3u);
        gui::BoxInfo const* second_check = ui.box_info(4u);
        TEST_EXPECT(context, first != nullptr && second != nullptr);
        if (first != nullptr && second != nullptr) {
            TEST_EXPECT(context, first->id.value != second->id.value);
            TEST_EXPECT(context, !first->duplicate_id);
            TEST_EXPECT(context, !second->duplicate_id);
        }
        TEST_EXPECT(context, first_check != nullptr && second_check != nullptr);
        if (first_check != nullptr && second_check != nullptr) {
            TEST_EXPECT(context, first_check->id.value != second_check->id.value);
            TEST_EXPECT(context, !first_check->duplicate_id);
            TEST_EXPECT(context, !second_check->duplicate_id);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(id_scopes_disambiguate_repeated_local_ids) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const rows_id = gui::id("rows");
        gui::Id const row_id = gui::id("row");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto rows_scope = ui.id_scope(rows_id);
            BASE_UNUSED(rows_scope);
            for (size_t index = 0u; index < 2u; ++index) {
                auto item_scope = ui.id_scope(gui::id(static_cast<uint64_t>(index)));
                BASE_UNUSED(item_scope);
                auto row = ui.row(row_id);
                TEST_EXPECT(context, row);
                ui.button("Open");
            }
        }
        gui::end_frame(ui);

        gui::Id const first_scope = gui::id(rows_id, 0u);
        gui::Id const second_scope = gui::id(rows_id, 1u);
        gui::BoxInfo const* first_row = ui.find_box(gui::id(first_scope, row_id));
        gui::BoxInfo const* second_row = ui.find_box(gui::id(second_scope, row_id));
        gui::BoxInfo const* first_button = ui.box_info(2u);
        gui::BoxInfo const* second_button = ui.box_info(4u);
        TEST_EXPECT(context, first_row != nullptr && second_row != nullptr);
        if (first_row != nullptr && second_row != nullptr) {
            TEST_EXPECT(context, first_row->id.value != second_row->id.value);
            TEST_EXPECT(context, !first_row->duplicate_id);
            TEST_EXPECT(context, !second_row->duplicate_id);
        }
        TEST_EXPECT(context, first_button != nullptr && second_button != nullptr);
        if (first_button != nullptr && second_button != nullptr) {
            TEST_EXPECT(context, first_button->id.value != second_button->id.value);
            TEST_EXPECT(context, !first_button->duplicate_id);
            TEST_EXPECT(context, !second_button->duplicate_id);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(disabled_widgets_ignore_pointer_focus_and_value_changes) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        bool enabled = false;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        {
            auto group = ui.column({.flags = gui::BOX_FLAG_DISABLED});
            TEST_EXPECT(context, group);
            ui.checkbox(
                "Enabled",
                &enabled,
                {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
            );
        }
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        {
            auto group = ui.column({.flags = gui::BOX_FLAG_DISABLED});
            TEST_EXPECT(context, group);
            gui::Signal const signal = ui.checkbox(
                "Enabled",
                &enabled,
                {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
            );
            TEST_EXPECT(context, !signal.hovered);
            TEST_EXPECT(context, !signal.pressed_left);
            TEST_EXPECT(context, !signal.focused);
            TEST_EXPECT(context, !signal.changed);
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, !enabled);
        gui::BoxInfo const* checkbox = ui.box_info(2u);
        TEST_EXPECT(context, checkbox != nullptr);
        if (checkbox != nullptr) {
            TEST_EXPECT(context, (checkbox->flags & gui::BOX_FLAG_DISABLED) != 0u);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(fixed_list_row_scopes_allow_duplicate_visible_text_and_publish_parentage) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        auto rows = ui.list_fixed(
            gui::id("files"),
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, rows.first == 0u);
        TEST_EXPECT(context, rows.end == 4u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            auto row = rows.row(gui::id(static_cast<uint64_t>(row_index)));
            TEST_EXPECT(context, row);
            ui.button("Open", {.layout = {.width = gui::fill(), .height = gui::fill()}});
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 10u);
        gui::BoxInfo const* list = ui.box_info(1u);
        gui::BoxInfo const* first_row = ui.box_info(2u);
        gui::BoxInfo const* first_button = ui.box_info(3u);
        gui::BoxInfo const* second_row = ui.box_info(4u);
        gui::BoxInfo const* second_button = ui.box_info(5u);
        if (list != nullptr && first_row != nullptr && first_button != nullptr &&
            second_row != nullptr && second_button != nullptr) {
            TEST_EXPECT(context, list->kind == gui::BoxKind::LIST);
            TEST_EXPECT(context, first_row->kind == gui::BoxKind::ROW);
            TEST_EXPECT(context, first_button->kind == gui::BoxKind::BUTTON);
            TEST_EXPECT(context, first_row->parent_id.value == list->id.value);
            TEST_EXPECT(context, first_button->parent_id.value == first_row->id.value);
            TEST_EXPECT(context, second_row->parent_id.value == list->id.value);
            TEST_EXPECT(context, second_button->parent_id.value == second_row->id.value);
            TEST_EXPECT(context, first_button->text == StrRef("Open"));
            TEST_EXPECT(context, second_button->text == StrRef("Open"));
            TEST_EXPECT(context, !first_button->duplicate_id);
            TEST_EXPECT(context, !second_button->duplicate_id);
            expect_rect(context, list->rect, {{0.0f, 0.0f}, {100.0f, 30.0f}});
            expect_rect(context, first_row->rect, {{0.0f, 0.0f}, {100.0f, 10.0f}});
        }

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.scroll_delta_y = -15.0f;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}, .input = input});
        auto scrolled_rows = ui.list_fixed(
            gui::id("files"),
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, scrolled_rows.first == 1u);
        TEST_EXPECT(context, scrolled_rows.end == 5u);
        for (size_t row_index = scrolled_rows.first; row_index < scrolled_rows.end; ++row_index) {
            auto row = scrolled_rows.row(gui::id(static_cast<uint64_t>(row_index)));
            TEST_EXPECT(context, row);
            ui.button("Open", {.layout = {.width = gui::fill(), .height = gui::fill()}});
        }
        gui::end_frame(ui);

        first_row = ui.box_info(2u);
        if (first_row != nullptr) {
            expect_rect(context, first_row->rect, {{0.0f, -5.0f}, {100.0f, 5.0f}});
        }

        gui::ScrollState state = ui.scroll_state(gui::id("files"));
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 15.0f);
        TEST_EXPECT(context, state.max_y == 970.0f);
        TEST_EXPECT(context, state.viewport_height == 30.0f);
        TEST_EXPECT(context, state.content_height == 1000.0f);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.set_scroll_y(gui::id("files"), 25.0f);
        auto requested_rows = ui.list_fixed(
            gui::id("files"),
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, requested_rows.first == 2u);
        TEST_EXPECT(context, requested_rows.end == 6u);
        for (size_t row_index = requested_rows.first; row_index < requested_rows.end; ++row_index) {
            auto row = requested_rows.row(gui::id(static_cast<uint64_t>(row_index)));
            TEST_EXPECT(context, row);
            ui.button("Open", {.layout = {.width = gui::fill(), .height = gui::fill()}});
        }
        gui::end_frame(ui);

        state = ui.scroll_state(gui::id("files"));
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 25.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(input_hit_testing_honors_fixed_list_clip) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const label_id = gui::id("title");
        gui::Id const list_id = gui::id("files");
        gui::TextSelection selection = {};

        auto add_ui =
            [&](gui::Frame& frame, gui::Signal* out_label, gui::Signal* out_first_row) -> void {
            frame.set_scroll_y(list_id, 15.0f);
            gui::Signal const label = frame.selectable_label(
                label_id,
                "Virtualized Assets",
                &selection,
                {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
            );
            if (out_label != nullptr) {
                *out_label = label;
            }

            auto rows = frame.list_fixed(
                list_id,
                {
                    .item_count = 8u,
                    .item_height = 20.0f,
                    .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                }
            );
            for (size_t index = rows.first; index < rows.end; ++index) {
                auto row = rows.row(gui::id(static_cast<uint64_t>(index + 1u)));
                TEST_EXPECT(context, row);
                gui::Signal const signal = row.signal();
                if (out_first_row != nullptr && index == rows.first) {
                    *out_first_row = signal;
                }
                frame.label("Asset", {.layout = {.width = gui::fill(), .height = gui::fill()}});
            }
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        add_ui(ui, nullptr, nullptr);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 10.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        gui::Signal label_signal = {};
        gui::Signal first_row_signal = {};
        add_ui(ui, &label_signal, &first_row_signal);
        gui::end_frame(ui);

        TEST_EXPECT(context, label_signal.hovered);
        TEST_EXPECT(context, label_signal.pressed_left);
        TEST_EXPECT(context, !first_row_signal.hovered);
        TEST_EXPECT(context, !first_row_signal.pressed_left);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(scroll_to_index_updates_fixed_list_range_for_each_reveal_mode) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const files_id = gui::id("files");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.scroll_to_index(files_id, 20u, gui::ScrollReveal::START);
        auto rows = ui.list_fixed(
            files_id,
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, rows.first == 20u);
        TEST_EXPECT(context, rows.end == 24u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            TEST_EXPECT(context, rows.row(gui::id(static_cast<uint64_t>(row_index))));
        }
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(files_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 200.0f);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.scroll_to_index(files_id, 20u, gui::ScrollReveal::CENTER);
        rows = ui.list_fixed(
            files_id,
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, rows.first == 19u);
        TEST_EXPECT(context, rows.end == 23u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            TEST_EXPECT(context, rows.row(gui::id(static_cast<uint64_t>(row_index))));
        }
        gui::end_frame(ui);

        state = ui.scroll_state(files_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 190.0f);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.scroll_to_index(files_id, 20u, gui::ScrollReveal::END);
        rows = ui.list_fixed(
            files_id,
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, rows.first == 18u);
        TEST_EXPECT(context, rows.end == 22u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            TEST_EXPECT(context, rows.row(gui::id(static_cast<uint64_t>(row_index))));
        }
        gui::end_frame(ui);

        state = ui.scroll_state(files_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 180.0f);

        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.scroll_to_index(files_id, 22u);
        rows = ui.list_fixed(
            files_id,
            {
                .item_count = 100u,
                .item_height = 10.0f,
                .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
            }
        );
        TEST_EXPECT(context, rows.first == 20u);
        TEST_EXPECT(context, rows.end == 24u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            TEST_EXPECT(context, rows.row(gui::id(static_cast<uint64_t>(row_index))));
        }
        gui::end_frame(ui);

        state = ui.scroll_state(files_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 200.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_preserves_selection_until_click_release_or_drag) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {1u, 4u};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {42.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, !signal.focused);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 4u);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 5u);
        TEST_EXPECT(context, selection.end == 5u);

        input.mouse_pos = {9.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 5u);
        TEST_EXPECT(context, selection.end == 5u);

        input.mouse_pos = {34.0f, 5.0f};
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 4u);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 4u);

        gui::BoxInfo const* label = ui.box_info(1u);
        TEST_EXPECT(context, label != nullptr && label->kind == gui::BoxKind::SELECTABLE_LABEL);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_double_click_selects_word) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(200.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}});
        ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {54.0f, 5.0f};
        input.mouse_down[0u] = true;
        input.mouse_double_clicked[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 6u);
        TEST_EXPECT(context, selection.end == 10u);

        input.mouse_double_clicked[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 6u);
        TEST_EXPECT(context, selection.end == 10u);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 6u);
        TEST_EXPECT(context, selection.end == 10u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_triple_click_selects_all_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(200.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}});
        ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {54.0f, 5.0f};
        input.mouse_down[0u] = true;
        input.mouse_double_clicked[0u] = true;
        input.mouse_triple_clicked[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.hovered);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 0u);
        TEST_EXPECT(context, selection.end == 10u);

        input.mouse_double_clicked[0u] = false;
        input.mouse_triple_clicked[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 0u);
        TEST_EXPECT(context, selection.end == 10u);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "alpha beta", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 0u);
        TEST_EXPECT(context, selection.end == 10u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_copies_selected_text_on_ctrl_c) {
        Arena arena = {};
        arena.init();

        ClipboardCapture clipboard = {};
        gui::Context gui_context = {};
        gui::create_context(
            arena,
            {.set_clipboard_text = capture_clipboard_text, .clipboard_user_data = &clipboard},
            gui_context
        );

        gui::TextSelection selection = {1u, 4u};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        gui::KeyEvent const events[] = {{.key = gui::Key::C, .mods = gui::KEY_MOD_CTRL}};
        gui::InputState input = {};
        input.key_events = events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        gui::Signal const signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 4u);
        TEST_EXPECT(context, clipboard.call_count == 1u);
        TEST_EXPECT(context, StrRef(clipboard.text, clipboard.text_size) == StrRef("BCD"));

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_shift_arrows_expand_and_shrink_selection) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {1u, 3u};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        gui::KeyEvent const expand_events[] = {
            {.key = gui::Key::RIGHT, .mods = gui::KEY_MOD_SHIFT},
        };
        gui::InputState input = {};
        input.key_events = expand_events;
        input.key_event_count = 1u;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 4u);

        gui::KeyEvent const shrink_events[] = {
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
            {.key = gui::Key::LEFT, .mods = gui::KEY_MOD_SHIFT},
        };
        input = {};
        input.key_events = shrink_events;
        input.key_event_count = 4u;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.selectable_label(label_id, "ABCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 1u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_renders_selection_highlight_without_font) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::TextSelection selection = {1u, 4u};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.selectable_label(
            gui::id("copyable"),
            "ABCDE",
            &selection,
            {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 1u);
        gui::draw::StyledRectCommand const* command =
            gui::draw::styled_rect_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        if (command != nullptr) {
            TEST_EXPECT(context, command->rect.min.x == 8.0f);
            TEST_EXPECT(context, command->rect.max.x == 32.0f);
            TEST_EXPECT(context, command->rect.min.y == 0.0f);
            TEST_EXPECT(context, command->rect.max.y == 20.0f);
            TEST_EXPECT(context, command->style.fill_color.a > 0.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_does_not_scroll_single_line_overflow_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("title");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.set_scroll_y(label_id, 20.0f);
        ui.selectable_label(
            label_id,
            "Virtualized Assets",
            &selection,
            {.layout = {.width = gui::px(80.0f), .height = gui::px(10.0f)}}
        );
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(label_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 0.0f);
        TEST_EXPECT(context, state.max_y == 0.0f);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 0u);

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_scrolls_overflow_text_and_renders_scrollbar) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.set_scroll_y(label_id, 20.0f);
        ui.selectable_label(
            label_id,
            "A\nB\nC",
            &selection,
            {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}}
        );
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(label_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 20.0f);
        TEST_EXPECT(context, state.max_y == 40.0f);
        TEST_EXPECT(context, state.viewport_height == 20.0f);
        TEST_EXPECT(context, state.content_height == 60.0f);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 2u);
        gui::draw::StyledRectCommand const* track =
            gui::draw::styled_rect_command(draw_context, 0u);
        gui::draw::StyledRectCommand const* thumb =
            gui::draw::styled_rect_command(draw_context, 1u);
        TEST_EXPECT(context, track != nullptr);
        TEST_EXPECT(context, thumb != nullptr);
        if (track != nullptr && thumb != nullptr) {
            TEST_EXPECT(context, track->rect.min.x == 72.0f);
            TEST_EXPECT(context, track->rect.max.x == 78.0f);
            TEST_EXPECT(context, track->rect.min.y == 2.0f);
            TEST_EXPECT(context, track->rect.max.y == 18.0f);
            TEST_EXPECT(context, thumb->rect.min.x == 72.0f);
            TEST_EXPECT(context, thumb->rect.max.x == 78.0f);
            TEST_EXPECT(context, thumb->rect.min.y == 4.0f);
            TEST_EXPECT(context, thumb->rect.max.y == 16.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(word_wrapped_selectable_label_scrolls_overflow_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.set_scroll_y(label_id, 20.0f);
        ui.selectable_label(
            label_id,
            "AB CD EF",
            &selection,
            {.layout = {
                 .width = gui::px(16.0f),
                 .height = gui::px(20.0f),
                 .word_wrap = true,
             }}
        );
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(label_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 20.0f);
        TEST_EXPECT(context, state.max_y == 40.0f);
        TEST_EXPECT(context, state.viewport_height == 20.0f);
        TEST_EXPECT(context, state.content_height == 60.0f);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_scrollbar_click_scrolls_without_changing_selection) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {1u, 2u};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.selectable_label(label_id, "A\nB\nC", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {75.0f, 16.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        gui::Signal const signal = ui.selectable_label(label_id, "A\nB\nC", &selection, box);
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(label_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y == 40.0f);
        TEST_EXPECT(context, selection.start == 1u);
        TEST_EXPECT(context, selection.end == 2u);
        TEST_EXPECT(context, !signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_drag_selection_scrolls_beyond_bounds) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        ui.selectable_label(label_id, "A\nB\nC\nD\nE", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "A\nB\nC\nD\nE", &selection, box);
        gui::end_frame(ui);

        input.mouse_pos = {5.0f, 45.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "A\nB\nC\nD\nE", &selection, box);
        gui::end_frame(ui);

        gui::ScrollState state = ui.scroll_state(label_id);
        TEST_EXPECT(context, state.valid);
        TEST_EXPECT(context, state.y >= 19.0f && state.y <= 21.0f);
        TEST_EXPECT(context, selection.start < selection.end);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_drag_selection_scrolls_parent_panel_beyond_bounds) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const panel_id = gui::id("panel");
        gui::Id const label_id = gui::id("copyable");

        auto add_label = [&](gui::Frame& frame) -> void {
            if (auto panel = frame.scroll_panel(
                    panel_id, {.layout = {.width = gui::px(80.0f), .height = gui::px(40.0f)}}
                )) {
                BASE_UNUSED(panel);
                frame.selectable_label(
                    label_id,
                    "A\nB\nC\nD\nE",
                    &selection,
                    {.layout = {.width = gui::fill(), .height = gui::text()}}
                );
            }
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        add_label(ui);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        add_label(ui);
        gui::end_frame(ui);

        input.mouse_pos = {5.0f, 45.0f};
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}, .input = input});
        add_label(ui);
        gui::end_frame(ui);

        gui::ScrollState panel_state = ui.scroll_state(panel_id);
        gui::ScrollState label_state = ui.scroll_state(label_id);
        TEST_EXPECT(context, panel_state.valid);
        TEST_EXPECT(context, !label_state.valid);
        TEST_EXPECT(context, panel_state.y >= 19.0f && panel_state.y <= 21.0f);
        TEST_EXPECT(context, selection.start < selection.end);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_hit_tests_multiline_text) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(100.0f), .height = gui::px(40.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {13.0f, 25.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        gui::Signal const signal = ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, selection.start == 5u);
        TEST_EXPECT(context, selection.end == 5u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(word_wrapped_selectable_label_hit_tests_visual_lines) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {
            .layout = {
                .width = gui::px(16.0f),
                .height = gui::px(40.0f),
                .word_wrap = true,
            }
        };

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.selectable_label(label_id, "AB CD", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {13.0f, 25.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "AB CD", &selection, box);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        gui::Signal const signal = ui.selectable_label(label_id, "AB CD", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, selection.start == 5u);
        TEST_EXPECT(context, selection.end == 5u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_clicking_multiline_selection_resets_selection) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::TextSelection selection = {3u, 5u};
        gui::Id const label_id = gui::id("copyable");
        gui::BoxDesc const box = {.layout = {.width = gui::px(100.0f), .height = gui::px(40.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {9.0f, 25.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        gui::Signal signal = ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, selection.start == 3u);
        TEST_EXPECT(context, selection.end == 5u);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}, .input = input});
        signal = ui.selectable_label(label_id, "AB\nCDE", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, selection.start == 4u);
        TEST_EXPECT(context, selection.end == 4u);

        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_renders_multiline_selection_highlight_without_font) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::TextSelection selection = {1u, 5u};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 60.0f}});
        ui.selectable_label(
            gui::id("copyable"),
            "ABC\nDE",
            &selection,
            {.layout = {.width = gui::px(100.0f), .height = gui::px(40.0f)}}
        );
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 2u);
        gui::draw::StyledRectCommand const* first =
            gui::draw::styled_rect_command(draw_context, 0u);
        gui::draw::StyledRectCommand const* second =
            gui::draw::styled_rect_command(draw_context, 1u);
        TEST_EXPECT(context, first != nullptr);
        TEST_EXPECT(context, second != nullptr);
        if (first != nullptr && second != nullptr) {
            TEST_EXPECT(context, first->rect.min.x == 8.0f);
            TEST_EXPECT(context, first->rect.max.x == 24.0f);
            TEST_EXPECT(context, first->rect.min.y == 0.0f);
            TEST_EXPECT(context, first->rect.max.y == 20.0f);
            TEST_EXPECT(context, second->rect.min.x == 0.0f);
            TEST_EXPECT(context, second->rect.max.x == 8.0f);
            TEST_EXPECT(context, second->rect.min.y == 20.0f);
            TEST_EXPECT(context, second->rect.max.y == 40.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(selectable_label_uses_font_advance_for_hit_testing) {
        Arena arena = {};
        arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, {}, font);

        float const narrow_advance = gui::font_cache::text_advance(font, 18.0f, "i");
        float const wide_advance = gui::font_cache::text_advance(font, 18.0f, "W");
        TEST_EXPECT(context, narrow_advance < wide_advance);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = font;
        theme.root.font_size = 18.0f;

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::TextSelection selection = {};
        gui::Id const label_id = gui::id("variable_width");
        gui::BoxDesc const box = {.layout = {.width = gui::px(200.0f), .height = gui::px(28.0f)}};

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {220.0f, 60.0f}});
        ui.selectable_label(label_id, "iW", &selection, box);
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {narrow_advance + wide_advance * 0.65f, 8.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "iW", &selection, box);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {220.0f, 60.0f}, .input = input});
        ui.selectable_label(label_id, "iW", &selection, box);
        gui::end_frame(ui);

        TEST_EXPECT(context, selection.start == 2u);
        TEST_EXPECT(context, selection.end == 2u);

        gui::destroy_context(gui_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(header_label_and_button_text_share_vertical_position) {
        Arena arena = {};
        arena.init();

        gui::font_provider::Context provider = {};
        gui::font_provider::Result const provider_result =
            gui::font_provider::create_context(arena, {}, provider);

#if BASE_PLATFORM_WINDOWS
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::OK);

        gui::font_cache::Cache cache = {};
        gui::font_cache::create_cache(arena, provider, {}, cache);

        gui::font_cache::Font font = {};
        gui::font_cache::open_system_font(cache, "Segoe UI", font);

        gui::ThemeDesc theme = gui::default_theme();
        theme.root.font = font;
        theme.root.font_size = 13.0f;

        gui::Context gui_context = {};
        gui::create_context(arena, {.theme = &theme}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {.font_cache = cache}, draw_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {944.0f, 52.0f}});
        if (auto root = ui.column(
                gui::id("root"),
                {.layout = {
                     .width = gui::fill(),
                     .height = gui::fill(),
                     .padding = gui::insets(8.0f),
                     .align_x = gui::Align::STRETCH,
                 }}
            )) {
            if (auto header = ui.row(
                    gui::id("header"),
                    {.layout = {
                         .width = gui::fill(),
                         .height = gui::px(34.0f),
                         .padding = gui::insets(6.0f, 8.0f),
                         .gap = 8.0f,
                         .align_y = gui::Align::CENTER,
                     }}
                )) {
                BASE_UNUSED(header);
                ui.label(
                    "V2 UI API Testbed", {.layout = {.width = gui::text(), .height = gui::fill()}}
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                ui.button(
                    gui::id("reset"),
                    "Reset",
                    {
                        .layout = {
                            .width = gui::px(72.0f),
                            .height = gui::px(26.0f),
                            .padding = gui::insets(2.0f, 6.0f),
                        },
                    }
                );
            }
            BASE_UNUSED(root);
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 2u);
        gui::draw::TextCommand const* title = gui::draw::text_command(draw_context, 0u);
        gui::draw::TextCommand const* reset = gui::draw::text_command(draw_context, 1u);
        gui::BoxInfo const* reset_box = ui.find_box(gui::id("reset"), gui::BoxKind::BUTTON);
        TEST_EXPECT(context, title != nullptr);
        TEST_EXPECT(context, reset != nullptr);
        TEST_EXPECT(context, reset_box != nullptr);
        if (title != nullptr && reset != nullptr && reset_box != nullptr) {
            float const title_center =
                title->position.y + title->run.offset_y + title->run.height * 0.5f;
            float const reset_center =
                reset->position.y + reset->run.offset_y + reset->run.height * 0.5f;
            float const delta = title_center - reset_center;
            TEST_EXPECT(context, delta >= -0.5f && delta <= 0.5f);
            float const reset_box_center = (reset_box->rect.min.y + reset_box->rect.max.y) * 0.5f;
            float const reset_box_delta = reset_center - reset_box_center;
            TEST_EXPECT(context, reset_box_delta >= -0.5f && reset_box_delta <= 0.5f);
        }

        gui::draw::end_frame(draw_context);
        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
        gui::font_cache::destroy_cache(cache);
        gui::font_provider::destroy_context(provider);
#else
        TEST_EXPECT(context, provider_result == gui::font_provider::Result::UNSUPPORTED_PLATFORM);
#endif
    }

    TEST_CASE(image_widget_measures_and_emits_textured_quad) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture texture = {&texture_storage};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.image(
            gui::id("photo"),
            texture,
            {.tint = gui::rgba(128, 255, 64, 200), .size = {40.0f, 20.0f}}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* image = ui.find_box(gui::id("photo"), gui::BoxKind::IMAGE);
        TEST_EXPECT(context, image != nullptr);
        if (image != nullptr) {
            expect_rect(context, image->rect, {{0.0f, 0.0f}, {40.0f, 20.0f}});
        }

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 1u);
        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        if (command != nullptr) {
            TEST_EXPECT(context, command->texture.handle == texture.handle);
            TEST_EXPECT(context, command->vertex_count == 6u);
            TEST_EXPECT(context, command->vertices[0u].position.x == 0.0f);
            TEST_EXPECT(context, command->vertices[0u].position.y == 0.0f);
            TEST_EXPECT(context, command->vertices[2u].position.x == 40.0f);
            TEST_EXPECT(context, command->vertices[2u].position.y == 20.0f);
            TEST_EXPECT(context, command->vertices[2u].uv.x == 1.0f);
            TEST_EXPECT(context, command->vertices[2u].uv.y == 1.0f);
            TEST_EXPECT(context, command->vertices[0u].color.r == (128.0f / 255.0f));
            TEST_EXPECT(context, command->vertices[0u].color.a == (200.0f / 255.0f));
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(image_contain_fit_preserves_source_aspect) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture texture = {&texture_storage};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.image(
            texture,
            {
                .box = {.layout = {.width = gui::px(80.0f), .height = gui::px(80.0f)}},
                .size = {40.0f, 20.0f},
                .fit = gui::ImageFit::CONTAIN,
            }
        );
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        gui::draw::PrimitiveCommand const* command = gui::draw::primitive_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        if (command != nullptr) {
            TEST_EXPECT(context, command->vertices[0u].position.x == 0.0f);
            TEST_EXPECT(context, command->vertices[0u].position.y == 20.0f);
            TEST_EXPECT(context, command->vertices[2u].position.x == 80.0f);
            TEST_EXPECT(context, command->vertices[2u].position.y == 60.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(icon_widgets_and_button_icons_emit_tinted_quads) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        int texture_storage = 0;
        gui::render::Texture texture = {&texture_storage};
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {120.0f, 50.0f}});
        ui.icon(
            gui::id("tool_icon"),
            texture,
            {.style = {.foreground = gui::rgb(10, 20, 30)}, .icon = {.size = 20.0f}}
        );
        ui.button("Run", {.icon = {.texture = texture, .size = 16.0f, .gap = 4.0f}});
        gui::end_frame(ui);

        gui::BoxInfo const* icon = ui.find_box(gui::id("tool_icon"), gui::BoxKind::ICON);
        gui::BoxInfo const* button = find_box_text(ui, gui::BoxKind::BUTTON, "Run");
        TEST_EXPECT(context, icon != nullptr);
        TEST_EXPECT(context, button != nullptr);
        if (icon != nullptr && button != nullptr) {
            expect_rect(context, icon->rect, {{0.0f, 0.0f}, {20.0f, 20.0f}});
            expect_rect(context, button->rect, {{0.0f, 20.0f}, {44.0f, 40.0f}});
        }

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 2u);
        gui::draw::PrimitiveCommand const* standalone =
            gui::draw::primitive_command(draw_context, 0u);
        gui::draw::PrimitiveCommand const* button_icon =
            gui::draw::primitive_command(draw_context, 1u);
        TEST_EXPECT(context, standalone != nullptr);
        TEST_EXPECT(context, button_icon != nullptr);
        if (standalone != nullptr) {
            TEST_EXPECT(context, standalone->vertices[0u].color.r == (10.0f / 255.0f));
            TEST_EXPECT(context, standalone->vertices[0u].color.g == (20.0f / 255.0f));
            TEST_EXPECT(context, standalone->vertices[0u].color.b == (30.0f / 255.0f));
        }
        if (button_icon != nullptr) {
            TEST_EXPECT(context, button_icon->vertices[0u].position.x == 0.0f);
            TEST_EXPECT(context, button_icon->vertices[0u].position.y == 22.0f);
            TEST_EXPECT(context, button_icon->vertices[2u].position.x == 16.0f);
            TEST_EXPECT(context, button_icon->vertices[2u].position.y == 38.0f);
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(widget_metadata_and_draw_output_are_published) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        bool enabled = true;
        float scale = 0.25f;
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {140.0f, 50.0f}});
        ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}
        );
        ui.slider_float(
            "Scale",
            &scale,
            {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}}
        );
        gui::end_frame(ui);

        gui::BoxInfo const* checkbox = ui.box_info(1u);
        gui::BoxInfo const* slider = ui.box_info(2u);
        TEST_EXPECT(context, checkbox != nullptr && slider != nullptr);
        if (checkbox != nullptr && slider != nullptr) {
            TEST_EXPECT(context, checkbox->kind == gui::BoxKind::CHECKBOX);
            TEST_EXPECT(context, checkbox->text == StrRef("Enabled"));
            TEST_EXPECT(context, checkbox->style.background.a == 1.0f);
            TEST_EXPECT(context, slider->kind == gui::BoxKind::SLIDER_FLOAT);
            TEST_EXPECT(context, slider->text == StrRef("Scale"));
        }

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);
        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) >= 4u);

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(dense_controls_panel_renders_only_batchable_styled_rects_without_font) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        bool checks[] = {true, false, true, false};
        bool toggles[] = {false, true, false, true};
        float values[] = {0.2f, 0.4f, 0.6f, 0.8f};
        gui::BoxDesc const control_box = {
            .layout = {.width = gui::px(200.0f), .height = gui::px(20.0f)}
        };
        gui::Frame ui = gui::begin_frame(gui_context, {.size = {240.0f, 320.0f}});
        {
            auto panel = ui.column({.style = {.role = gui::StyleRole::PANEL}});
            TEST_EXPECT(context, panel);
            for (size_t index = 0u; index < 4u; ++index) {
                ui.checkbox(gui::id(100u + index), "Check", checks + index, control_box);
                ui.toggle(gui::id(200u + index), "Toggle", toggles + index, control_box);
                ui.slider_float(
                    gui::id(300u + index), "Slider", values + index, {.box = control_box}
                );
            }
        }
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        constexpr size_t expected_styled_rects = 39u;
        TEST_EXPECT(
            context, gui::draw::styled_rect_command_count(draw_context) == expected_styled_rects
        );
        TEST_EXPECT(context, gui::draw::text_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::primitive_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::layer_command_count(draw_context) == 0u);
        TEST_EXPECT(context, gui::draw::command_count(draw_context) == expected_styled_rects);

        for (size_t index = 0u; index < expected_styled_rects; ++index) {
            gui::draw::Command const* draw_command = gui::draw::command(draw_context, index);
            TEST_EXPECT(context, draw_command != nullptr);
            TEST_EXPECT(
                context,
                draw_command != nullptr && draw_command->kind == gui::draw::CommandKind::STYLED_RECT
            );

            gui::draw::StyledRectCommand const* styled =
                gui::draw::styled_rect_command(draw_context, index);
            TEST_EXPECT(context, styled != nullptr);
            if (styled != nullptr) {
                TEST_EXPECT(context, !gui::render::texture_valid(styled->style.texture));
                TEST_EXPECT(context, styled->style.shadow.color.a == 0.0f);
                TEST_EXPECT(context, styled->style.shadow.blur_radius == 0.0f);
                TEST_EXPECT(context, styled->style.shadow.spread == 0.0f);
            }
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

    TEST_CASE(render_emits_styled_rects_for_visible_box_styles) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::draw::Context draw_context = {};
        gui::draw::create_context(arena, {}, draw_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}});
        ui.button(
            "Paint",
            {
                .layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)},
                .style = {.background = gui::rgb(80, 90, 100), .radius = 3.0f},
            }
        );
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render_frame(ui, draw_context);

        TEST_EXPECT(context, gui::draw::styled_rect_command_count(draw_context) == 1u);
        gui::draw::StyledRectCommand const* command =
            gui::draw::styled_rect_command(draw_context, 0u);
        TEST_EXPECT(context, command != nullptr);
        if (command != nullptr) {
            TEST_EXPECT(context, command->rect.max.x == 80.0f);
            TEST_EXPECT(context, command->style.radius == 3.0f);
            TEST_EXPECT(context, command->style.fill_color.r == (80.0f / 255.0f));
        }

        gui::draw::destroy_context(draw_context);
        gui::destroy_context(gui_context);
    }

} // namespace

TEST_MAIN()
