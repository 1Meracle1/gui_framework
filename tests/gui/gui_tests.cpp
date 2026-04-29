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

    auto expect_color(test::Context* context, gui::Color value, gui::Color expected) -> void {
        TEST_EXPECT(context, value.r == expected.r);
        TEST_EXPECT(context, value.g == expected.g);
        TEST_EXPECT(context, value.b == expected.b);
        TEST_EXPECT(context, value.a == expected.a);
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
                .style =
                    {
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
            expect_color(context,
                         button->style.background,
                         gui::theme_role(theme, gui::StyleRole::CONTROL).hovered.background);
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
            expect_color(context,
                         button->style.background,
                         gui::theme_role(theme, gui::StyleRole::CONTROL).active.background);
        }

        gui::destroy_context(gui_context);
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
        ui.button("Delete",
                  {
                      .layout = {.width = gui::px(80.0f), .height = gui::px(20.0f)},
                      .style =
                          {
                              .role = gui::StyleRole::DANGER,
                              .background = gui::rgb(1, 2, 3),
                              .border = gui::rgba(0, 0, 0, 0),
                              .radius = 2.0f,
                          },
                  });
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

    TEST_CASE(row_layout_aligns_child_run_when_no_fill_consumes_space) {
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

    TEST_CASE(overlay_places_measured_and_fill_children) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 60.0f}});
        auto overlay = ui.overlay({
            .layout =
                {
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
                thread_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}});
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
                thread_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}});
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

    TEST_CASE(hit_test_returns_deepest_box) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 80.0f}});
        {
            auto page = ui.column(gui::id("page"),
                                  {
                                      .layout =
                                          {
                                              .width = gui::fill(),
                                              .height = gui::fill(),
                                              .padding = gui::insets(10.0f),
                                          },
                                  });
            TEST_EXPECT(context, page);
            auto toolbar = ui.row(gui::id("toolbar"),
                                  {.layout = {.width = gui::px(80.0f), .height = gui::px(30.0f)}});
            TEST_EXPECT(context, toolbar);
            ui.button(gui::id("run"),
                      "Run",
                      {.layout = {.width = gui::px(40.0f), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::BoxInfo const* button = ui.box_info(3u);
        gui::BoxInfo const* hit = ui.hit_test({15.0f, 15.0f});
        TEST_EXPECT(context, button != nullptr && button->kind == gui::BoxKind::BUTTON);
        TEST_EXPECT(context,
                    hit != nullptr && button != nullptr && hit->id.value == button->id.value);

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
                panel_id, {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}});
            TEST_EXPECT(context, panel);
            ui.label(gui::id("first"),
                     "First",
                     {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.label(gui::id("second"),
                     "Second",
                     {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
            ui.label(gui::id("third"),
                     "Third",
                     {.layout = {.width = gui::fill(), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::BoxInfo const* first = ui.box_info(2u);
        gui::BoxInfo const* second = ui.box_info(3u);
        gui::BoxInfo const* visible_hit = ui.hit_test({5.0f, 10.0f});
        gui::BoxInfo const* clipped_hit = ui.hit_test({5.0f, 40.0f});
        TEST_EXPECT(context, first != nullptr && first->kind == gui::BoxKind::LABEL);
        TEST_EXPECT(context, second != nullptr && second->kind == gui::BoxKind::LABEL);
        TEST_EXPECT(context,
                    visible_hit != nullptr && second != nullptr &&
                        visible_hit->id.value == second->id.value);
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
                run_id, "Run", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
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
                run_id, "Run", {.layout = {.width = gui::px(50.0f), .height = gui::px(20.0f)}});
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
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        gui::Signal signal = ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, signal.pressed_left);
        TEST_EXPECT(context, !signal.changed);
        TEST_EXPECT(context, !enabled);
        gui::end_frame(ui);

        input.mouse_down[0u] = false;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.checkbox(
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
        TEST_EXPECT(context, signal.clicked_left);
        TEST_EXPECT(context, signal.activated);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, enabled);
        gui::end_frame(ui);

        float value = 0.0f;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}});
        ui.slider_float("Scale",
                        &value,
                        {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}});
        gui::end_frame(ui);

        input = {};
        input.mouse_pos = {50.0f, 10.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        signal = ui.slider_float(
            "Scale",
            &value,
            {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}});
        TEST_EXPECT(context, signal.active);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, value == 0.5f);
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
        ui.slider_float(scale_id,
                        "Scale",
                        &value,
                        {
                            .box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}},
                            .step = 0.25f,
                        });
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
            });
        gui::end_frame(ui);

        TEST_EXPECT(context, signal.focused);
        TEST_EXPECT(context, signal.focus_gained);
        TEST_EXPECT(context, signal.changed);
        TEST_EXPECT(context, value == 0.75f);

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
            ui.checkbox("Enabled",
                        &enabled,
                        {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
        }
        gui::end_frame(ui);

        gui::InputState input = {};
        input.mouse_pos = {5.0f, 5.0f};
        input.mouse_down[0u] = true;
        ui = gui::begin_frame(gui_context, {.size = {120.0f, 40.0f}, .input = input});
        {
            auto group = ui.column({.flags = gui::BOX_FLAG_DISABLED});
            TEST_EXPECT(context, group);
            gui::Signal const signal =
                ui.checkbox("Enabled",
                            &enabled,
                            {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
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
        auto requested_rows =
            ui.list_fixed(gui::id("files"),
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
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

    TEST_CASE(scroll_to_index_updates_fixed_list_range_for_each_reveal_mode) {
        Arena arena = {};
        arena.init();

        gui::Context gui_context = {};
        gui::create_context(arena, {}, gui_context);

        gui::Id const files_id = gui::id("files");

        gui::Frame ui = gui::begin_frame(gui_context, {.size = {100.0f, 100.0f}});
        ui.scroll_to_index(files_id, 20u, gui::ScrollReveal::START);
        auto rows =
            ui.list_fixed(files_id,
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
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
        rows =
            ui.list_fixed(files_id,
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
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
        rows =
            ui.list_fixed(files_id,
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
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
        rows =
            ui.list_fixed(files_id,
                          {
                              .item_count = 100u,
                              .item_height = 10.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::px(30.0f)}},
                          });
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
            "Enabled", &enabled, {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}});
        ui.slider_float("Scale",
                        &scale,
                        {.box = {.layout = {.width = gui::px(100.0f), .height = gui::px(20.0f)}}});
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
