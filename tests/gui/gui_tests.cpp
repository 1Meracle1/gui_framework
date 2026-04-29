#include <gui/gui.h>
#include <test/test.h>

namespace {

    auto expect_vec(test::Context* context, gui::Vec2 value, float x, float y) -> void {
        TEST_EXPECT(context, value.x == x);
        TEST_EXPECT(context, value.y == y);
    }

    auto expect_rect(test::Context* context, gui::Rect value, gui::Rect expected) -> void {
        expect_vec(context, value.min, expected.min.x, expected.min.y);
        expect_vec(context, value.max, expected.max.x, expected.max.y);
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

    TEST_CASE(row_layout_splits_fixed_and_fill_children) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 20.0f}});
        auto row = ui.row({
            .layout =
                {
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
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 40.0f}, .input = input});
        signal =
            ui.button("Apply", {.layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, signal.released_left);
        TEST_EXPECT(context, signal.clicked_left);
        TEST_EXPECT(context, !signal.active);
        gui::end_frame(ui);

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
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(1u);
        gui::BoxInfo const* second = ui.box_info(2u);
        TEST_EXPECT(context, first != nullptr && second != nullptr);
        if (first != nullptr && second != nullptr) {
            TEST_EXPECT(context, first->id.value != second->id.value);
            TEST_EXPECT(context, !first->duplicate_id);
            TEST_EXPECT(context, !second->duplicate_id);
        }

        gui::destroy_context(gui_context);
    }

    TEST_CASE(fixed_list_returns_visible_row_range_and_scopes_rows) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        auto rows =
            ui.list_fixed(gui::id("files"),
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
        TEST_EXPECT(context, rows.first == 0u);
        TEST_EXPECT(context, rows.end == 4u);
        for (size_t row_index = rows.first; row_index < rows.end; ++row_index) {
            ui.label(gui::id(static_cast<uint64_t>(row_index)),
                     "row",
                     {.layout = {.height = gui::px(10.0f)}});
        }
        gui::end_frame(ui);

        TEST_EXPECT(context, ui.box_info_count() == 6u);
        gui::BoxInfo const* list = ui.box_info(1u);
        gui::BoxInfo const* first_row = ui.box_info(2u);
        if (list != nullptr && first_row != nullptr) {
            TEST_EXPECT(context, list->kind == gui::BoxKind::LIST);
            TEST_EXPECT(context, first_row->parent_id.value == list->id.value);
            expect_rect(context, list->rect, {{0.0f, 0.0f}, {100.0f, 30.0f}});
        }

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.scroll_delta_y = -15.0f;
        ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}, .input = input});
        auto scrolled_rows =
            ui.list_fixed(gui::id("files"),
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
        TEST_EXPECT(context, scrolled_rows.first == 1u);
        TEST_EXPECT(context, scrolled_rows.end == 5u);
        for (size_t row_index = scrolled_rows.first; row_index < scrolled_rows.end; ++row_index) {
            ui.label(gui::id(static_cast<uint64_t>(row_index)),
                     "row",
                     {.layout = {.height = gui::px(10.0f)}});
        }
        gui::end_frame(ui);

        first_row = ui.box_info(2u);
        if (first_row != nullptr) {
            expect_rect(context, first_row->rect, {{0.0f, -5.0f}, {24.0f, 5.0f}});
        }

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
        ui.button("Paint",
                  {
                      .layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)},
                      .style = {.background = gui::rgb(80, 90, 100), .radius = 3.0f},
                  });
        gui::end_frame(ui);

        gui::draw::begin_frame(draw_context);
        gui::render(ui, draw_context);

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
