#include <base/crash.h>
#include <base/fmt.h>
#include <gui/gui.h>

namespace {

    struct TestbedState {
        bool enabled = true;
        bool preview = true;
        bool read_only_value = false;
        float scale = 1.25f;
        size_t selected_index = 12u;
        gui::Signal header_signal = {};
        gui::Signal selected_row_signal = {};
    };

    auto bool_text(bool value) -> char const* {
        return value ? "true" : "false";
    }

    auto kind_name(gui::BoxKind kind) -> char const* {
        switch (kind) {
        case gui::BoxKind::ROOT:
            return "root";
        case gui::BoxKind::ROW:
            return "row";
        case gui::BoxKind::COLUMN:
            return "column";
        case gui::BoxKind::OVERLAY:
            return "overlay";
        case gui::BoxKind::LABEL:
            return "label";
        case gui::BoxKind::BUTTON:
            return "button";
        case gui::BoxKind::CHECKBOX:
            return "checkbox";
        case gui::BoxKind::TOGGLE:
            return "toggle";
        case gui::BoxKind::SLIDER_FLOAT:
            return "slider_float";
        case gui::BoxKind::SPACER:
            return "spacer";
        case gui::BoxKind::SCROLL_PANEL:
            return "scroll_panel";
        case gui::BoxKind::LIST:
            return "list";
        case gui::BoxKind::COUNT:
            return "count";
        }
        return "unknown";
    }

    auto source_name(gui::BoxIdSource source) -> char const* {
        switch (source) {
        case gui::BoxIdSource::STRUCTURAL:
            return "structural";
        case gui::BoxIdSource::TEXT:
            return "text";
        case gui::BoxIdSource::EXPLICIT:
            return "explicit";
        }
        return "unknown";
    }

    auto row_id(size_t index) -> gui::Id {
        return gui::id(0xA1100000ull + static_cast<uint64_t>(index));
    }

    auto draw_scroll_lines(gui::Frame& ui, StrRef prefix, size_t count) -> void {
        for (size_t index = 0u; index < count; ++index) {
            ui.label(fmt::tprintf("%s %02zu", prefix, index));
        }
    }

    auto draw_ui(gui::Frame& ui, TestbedState& state) -> void {
        gui::Id const list_id = gui::id("asset_list");
        gui::Id const notes_id = gui::id("notes_scroll");
        gui::Id const log_id = gui::id("log_scroll");

        ui.scroll_to_index(list_id, state.selected_index, gui::ScrollReveal::CENTER);
        ui.set_scroll_y(notes_id, 18.0f);
        ui.scroll_to_end(log_id);
        state.selected_row_signal = {};

        if (auto root = ui.column(
                gui::id("ui_api_testbed_root"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .padding = gui::insets(8.0f),
                            .gap = 8.0f,
                            .align_x = gui::Align::STRETCH,
                        },
                    .style =
                        {
                            .role = gui::StyleRole::CANVAS,
                            .background = gui::rgb(22, 24, 28),
                            .foreground = gui::rgb(235, 238, 242),
                        },
                    .debug_name = "ui_api_testbed_root",
                }
            )) {

            if (auto header = ui.row(
                    gui::id("header_bar"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(34.0f),
                                .padding = gui::insets(6.0f, 8.0f),
                                .gap = 8.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        .style =
                            {
                                .role = gui::StyleRole::PANEL,
                                .background = gui::rgb(34, 38, 44),
                                .border = gui::rgba(255, 255, 255, 28),
                                .border_thickness = 1.0f,
                                .radius = 4.0f,
                            },
                        .debug_name = "clickable_header_bar",
                    }
                )) {
                state.header_signal = header.signal();

                ui.label(
                    "V2 UI API Testbed", {.layout = {.width = gui::text(), .height = gui::text()}}
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                if (ui.button(
                          gui::id("reset_scale"),
                          "Reset",
                          {
                              .layout = {.width = gui::px(72.0f), .height = gui::px(24.0f)},
                              .style = {.role = gui::StyleRole::DANGER, .radius = 3.0f},
                              .debug_name = "reset_scale_button",
                          }
                    )
                        .activated) {
                    state.scale = 1.0f;
                }
            }

            if (auto body = ui.row(
                    gui::id("body"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::fill(),
                                .gap = 8.0f,
                                .align_y = gui::Align::STRETCH,
                            },
                        .debug_name = "body_row",
                    }
                )) {

                if (auto sidebar = ui.column(
                        gui::id("sidebar"),
                        {
                            .layout =
                                {
                                    .width = gui::px(220.0f),
                                    .height = gui::fill(),
                                    .padding = gui::insets(8.0f),
                                    .gap = 6.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .style =
                                {
                                    .role = gui::StyleRole::PANEL,
                                    .background = gui::rgb(30, 34, 40),
                                    .radius = 4.0f,
                                },
                            .debug_name = "sidebar",
                        }
                    )) {

                    ui.label(
                        "Virtualized Assets",
                        {.layout = {.width = gui::text(), .height = gui::text()}}
                    );

                    auto rows = ui.list_fixed(
                        list_id,
                        {
                            .item_count = 48u,
                            .item_height = 24.0f,
                            .box = {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(198.0f),
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(24, 27, 32),
                                    },
                                .debug_name = "asset_list",
                            },
                        }
                    );
                    for (size_t index = rows.first; index < rows.end; ++index) {
                        bool const selected = index == state.selected_index;
                        auto row = rows.row(
                            row_id(index),
                            {
                                .layout =
                                    {
                                        .padding = gui::insets(2.0f, 6.0f),
                                        .gap = 6.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = selected ? gui::StyleRole::ACCENT
                                                         : gui::StyleRole::CONTROL,
                                        .background = selected ? gui::rgb(58, 108, 220)
                                                               : gui::rgba(255, 255, 255, 10),
                                        .radius = 3.0f,
                                    },
                                .debug_name = "asset_row",
                            }
                        );
                        gui::Signal const signal = row.signal();
                        if (signal.activated) {
                            state.selected_index = index;
                        }
                        if (index == state.selected_index) {
                            state.selected_row_signal = signal;
                        }
                        ui.label(
                            fmt::tprintf("Asset %02zu", index),
                            {.layout = {.width = gui::fill(), .height = gui::text()}}
                        );
                    }

                    if (auto notes = ui.scroll_panel(
                            notes_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(84.0f),
                                        .padding = gui::insets(6.0f),
                                        .gap = 4.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(25, 28, 34),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "notes_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Note line", 8u);
                    }
                }

                if (auto main_panel = ui.column(
                        gui::id("main_panel"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .gap = 8.0f,
                                    .align_x = gui::Align::STRETCH,
                                },
                            .debug_name = "main_panel",
                        }
                    )) {

                    if (auto controls = ui.row(
                            gui::id("controls"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::children(),
                                        .padding = gui::insets(8.0f),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(30, 34, 40),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "controls",
                            }
                        )) {
                        ui.checkbox(
                            gui::id("enabled_checkbox"),
                            "Enabled",
                            &state.enabled,
                            {.layout = {.width = gui::px(112.0f), .height = gui::px(24.0f)}}
                        );
                        ui.toggle(
                            gui::id("preview_toggle"),
                            "Preview",
                            &state.preview,
                            {.layout = {.width = gui::px(120.0f), .height = gui::px(24.0f)}}
                        );
                        ui.slider_float(
                            gui::id("scale_slider"),
                            "Scale",
                            &state.scale,
                            {
                                .box =
                                    {
                                        .layout =
                                            {
                                                .width = gui::px(160.0f),
                                                .height = gui::px(24.0f),
                                            },
                                    },
                                .min = 0.5f,
                                .max = 2.0f,
                                .step = 0.05f,
                            }
                        );
                        ui.checkbox(
                            gui::id("read_only_checkbox"),
                            "Read-only",
                            &state.read_only_value,
                            {
                                .layout =
                                    {
                                        .width = gui::px(122.0f),
                                        .height = gui::px(24.0f),
                                    },
                                .flags = gui::BOX_FLAG_READ_ONLY,
                            }
                        );
                        ui.button(
                            gui::id("disabled_button"),
                            "Disabled",
                            {
                                .layout =
                                    {
                                        .width = gui::px(92.0f),
                                        .height = gui::px(24.0f),
                                    },
                                .flags = gui::BOX_FLAG_DISABLED,
                            }
                        );
                    }

                    if (auto preview = ui.overlay(
                            gui::id("preview_overlay"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .padding = gui::insets(10.0f),
                                        .align_x = gui::Align::END,
                                        .align_y = gui::Align::START,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(26, 29, 34),
                                        .border = gui::rgba(255, 255, 255, 24),
                                        .border_thickness = 1.0f,
                                        .radius = 4.0f,
                                    },
                                .debug_name = "preview_overlay",
                            }
                        )) {
                        ui.label(
                            "Preview fills the overlay",
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::fill(),
                                        .padding = gui::insets(12.0f),
                                    },
                                .style = {.foreground = gui::rgb(175, 188, 204)},
                            }
                        );
                        if (auto badge = ui.row(
                                gui::id("overlay_badge"),
                                {
                                    .layout =
                                        {
                                            .width = gui::children(),
                                            .height = gui::children(),
                                            .margin = gui::insets(6.0f),
                                            .padding = gui::insets(4.0f, 8.0f),
                                        },
                                    .style =
                                        {
                                            .role = gui::StyleRole::ACCENT,
                                            .radius = 3.0f,
                                        },
                                    .debug_name = "overlay_badge",
                                }
                            )) {
                            ui.label("Overlay");
                        }
                    }

                    if (auto log = ui.scroll_panel(
                            log_id,
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(74.0f),
                                        .padding = gui::insets(6.0f),
                                        .gap = 3.0f,
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .role = gui::StyleRole::PANEL,
                                        .background = gui::rgb(30, 34, 40),
                                        .radius = 4.0f,
                                    },
                                .debug_name = "log_scroll",
                            }
                        )) {
                        draw_scroll_lines(ui, "Log entry", 8u);
                    }
                }
            }
        }
    }

    auto run_frame(gui::Context context, TestbedState& state, gui::InputState input) -> gui::Frame {
        gui::Frame ui = gui::begin_frame(
            context, {.size = {640.0f, 400.0f}, .delta_time = 1.0f / 60.0f, .input = input}
        );
        draw_ui(ui, state);
        gui::end_frame(ui);
        return ui;
    }

    auto print_scroll_state(StrRef name, gui::ScrollState state) -> void {
        fmt::printf(
            "%s: valid=%s y=%.1f max=%.1f viewport=%.1f content=%.1f\n",
            name,
            bool_text(state.valid),
            state.y,
            state.max_y,
            state.viewport_height,
            state.content_height
        );
    }

    auto print_box(StrRef label, gui::BoxInfo const& box) -> void {
        StrRef const text = box.text.empty() ? StrRef("-") : box.text;
        StrRef const debug_name = box.debug_name.empty() ? StrRef("-") : box.debug_name;
        fmt::printf(
            "%s kind=%s text=%s debug=%s id=%llu authored=%llu source=%s stable=%s duplicate=%s "
            "rect=(%.1f,%.1f)-(%.1f,%.1f)\n",
            label,
            kind_name(box.kind),
            text,
            debug_name,
            static_cast<unsigned long long>(box.id.value),
            static_cast<unsigned long long>(box.authored_id.value),
            source_name(box.id_source),
            bool_text(box.stable_id),
            bool_text(box.duplicate_id),
            box.rect.min.x,
            box.rect.min.y,
            box.rect.max.x,
            box.rect.max.y
        );
    }

    auto find_authored_box(gui::Frame const& ui, gui::Id authored_id, gui::BoxKind kind)
        -> gui::BoxInfo const* {
        for (size_t index = 0u; index < ui.box_info_count(); ++index) {
            gui::BoxInfo const* const box = ui.box_info(index);
            if (box != nullptr && box->authored_id.value == authored_id.value &&
                box->kind == kind) {
                return box;
            }
        }
        return nullptr;
    }

    auto print_lookup(StrRef label, gui::BoxInfo const* box) -> void {
        if (box == nullptr) {
            fmt::printf("%s: not found\n", label);
            return;
        }
        print_box(label, *box);
    }

    auto inspect_metadata(gui::Frame const& ui) -> void {
        size_t duplicate_count = 0u;
        fmt::printf("box_info_count=%zu\n", ui.box_info_count());
        for (size_t index = 0u; index < ui.box_info_count(); ++index) {
            gui::BoxInfo const* const box = ui.box_info(index);
            if (box == nullptr) {
                continue;
            }
            if (box->duplicate_id) {
                duplicate_count += 1u;
            }
            if (!box->debug_name.empty() || box->duplicate_id) {
                print_box(fmt::tprintf("box[%zu]", index), *box);
            }
        }
        fmt::printf("duplicate_id boxes=%zu\n", duplicate_count);

        gui::BoxInfo const* const controls =
            find_authored_box(ui, gui::id("controls"), gui::BoxKind::ROW);
        gui::BoxInfo const* const list =
            find_authored_box(ui, gui::id("asset_list"), gui::BoxKind::LIST);
        print_lookup(
            "find_box(resolved controls)", controls != nullptr ? ui.find_box(controls->id) : nullptr
        );
        print_lookup(
            "find_box(resolved list, kind)",
            list != nullptr ? ui.find_box(list->id, gui::BoxKind::LIST) : nullptr
        );
        print_lookup("hit_test(30,178)", ui.hit_test({30.0f, 178.0f}));
    }

} // namespace

auto main() -> int {
    base::install_crash_handlers();

    Arena arena = {};
    arena.init();

    gui::ThemeDesc theme = gui::default_theme();
    gui::theme_role(theme, gui::StyleRole::ACCENT).normal.background = gui::rgb(58, 108, 220);
    gui::theme_role(theme, gui::StyleRole::DANGER).normal.background = gui::rgb(150, 54, 58);

    gui::Context ui_context = {};
    gui::create_context(arena, {.theme = &theme}, ui_context);

    gui::draw::Context draw_context = {};
    gui::draw::create_context(arena, {}, draw_context);

    TestbedState state = {};
    gui::Frame ui = run_frame(ui_context, state, {});

    gui::InputState input = {};
    input.mouse_pos = {30.0f, 178.0f};
    input.mouse_down[0u] = true;
    ui = run_frame(ui_context, state, input);

    input.mouse_down[0u] = false;
    ui = run_frame(ui_context, state, input);

    gui::draw::begin_frame(draw_context);
    gui::render_frame(ui, draw_context);
    gui::draw::end_frame(draw_context);

    fmt::printf("ui_api_testbed\n");
    fmt::printf(
        "state: enabled=%s preview=%s read_only=%s scale=%.2f selected=%zu\n",
        bool_text(state.enabled),
        bool_text(state.preview),
        bool_text(state.read_only_value),
        state.scale,
        state.selected_index
    );
    fmt::printf(
        "header signal: hovered=%s active=%s activated=%s\n",
        bool_text(state.header_signal.hovered),
        bool_text(state.header_signal.active),
        bool_text(state.header_signal.activated)
    );
    fmt::printf(
        "selected row signal: hovered=%s active=%s activated=%s\n",
        bool_text(state.selected_row_signal.hovered),
        bool_text(state.selected_row_signal.active),
        bool_text(state.selected_row_signal.activated)
    );
    fmt::printf(
        "draw: commands=%zu styled_rects=%zu text=%zu\n",
        gui::draw::command_count(draw_context),
        gui::draw::styled_rect_command_count(draw_context),
        gui::draw::text_command_count(draw_context)
    );

    print_scroll_state("notes_scroll", ui.scroll_state(gui::id("notes_scroll")));
    print_scroll_state("log_scroll", ui.scroll_state(gui::id("log_scroll")));
    print_scroll_state("asset_list", ui.scroll_state(gui::id("asset_list")));
    inspect_metadata(ui);

    gui::draw::destroy_context(draw_context);
    gui::destroy_context(ui_context);
    return 0;
}
