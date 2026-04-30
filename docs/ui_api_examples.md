# UI API Examples

Compact examples for the current v2 immediate-mode API shape.

## Frame Lifecycle

```cpp
Arena arena = {};
arena.init();

gui::Context ui_context = {};
gui::create_context(arena, {}, ui_context);

gui::Frame ui = gui::begin_frame(ui_context,
                                  {
                                      .size = {1280.0f, 720.0f},
                                      .delta_time = dt,
                                      .input = input,
                                  });

draw_app_ui(ui);

gui::end_frame(ui);
gui::render_frame(ui, draw_context);
```

## Layout Nesting

```cpp
auto root = ui.column(gui::id("root"),
                      {
                          .layout =
                              {
                                  .width = gui::fill(),
                                  .height = gui::fill(),
                                  .padding = gui::insets(8.0f),
                                  .gap = 8.0f,
                                  .align_x = gui::Align::STRETCH,
                              },
                      });

ui.label("Project");

{
    auto body = ui.row(gui::id("body"),
                       {.layout = {.width = gui::fill(), .height = gui::fill(), .gap = 8.0f}});

    {
        auto sidebar = ui.column(gui::id("sidebar"),
                                 {.layout = {.width = gui::px(240.0f), .height = gui::fill()}});
        draw_file_tree(ui);
    }

    {
        auto content = ui.column(gui::id("content"),
                                 {.layout = {.width = gui::fill(), .height = gui::fill()}});
        draw_editor(ui);
    }
}
```

## Style Overrides

```cpp
gui::ThemeDesc theme = gui::default_theme();
gui::theme_role(theme, gui::StyleRole::ACCENT).normal.background = gui::rgb(62, 116, 255);
gui::set_theme(ui_context, theme);

ui.button("Save",
          {
              .layout = {.padding = gui::insets(6.0f, 10.0f)},
              .style =
                  {
                      .role = gui::StyleRole::ACCENT,
                      .radius = 4.0f,
                  },
          });

{
    auto panel = ui.column({
        .layout = {.padding = gui::insets(10.0f), .gap = 6.0f},
        .style =
            {
                .role = gui::StyleRole::PANEL,
                .border = gui::rgba(255, 255, 255, 28),
            },
        .debug_name = "properties_panel",
    });
    ui.label("Inspector");
}
```

## Widgets

```cpp
if (ui.button("Run").activated) {
    run_build();
}

bool enabled = settings.enabled;
if (ui.checkbox("Enabled", &enabled).changed) {
    settings.enabled = enabled;
}

ui.toggle("Preview", &preview_enabled);

float scale = settings.scale;
if (ui.slider_float(gui::id("scale"),
                    "Scale",
                    &scale,
                    {
                        .box = {.layout = {.width = gui::px(220.0f), .height = gui::px(24.0f)}},
                        .min = 0.5f,
                        .max = 2.0f,
                        .step = 0.05f,
                    })
        .changed) {
    settings.scale = scale;
}

if (ui.input_text(gui::id("name"),
                  "Name",
                  settings.name,
                  sizeof(settings.name),
                  {.layout = {.width = gui::px(220.0f), .height = gui::px(24.0f)}})
        .changed) {
    rename_item(settings.name);
}
```

## List Virtualization

```cpp
gui::Id const files_id = gui::id("files");

if (selection_changed) {
    ui.scroll_to_index(files_id, selected_index, gui::ScrollReveal::KEEP_VISIBLE);
}

auto rows = ui.list_fixed(files_id,
                          {
                              .item_count = file_count,
                              .item_height = 24.0f,
                              .box = {.layout = {.width = gui::fill(), .height = gui::fill()}},
                          });

for (size_t index = rows.first; index < rows.end; ++index) {
    auto row = rows.row(gui::id(file_ids[index]));
    if (row.signal().activated) {
        selected_index = index;
    }
    ui.label(file_names[index], {.layout = {.width = gui::fill(), .height = gui::fill()}});
}

gui::ScrollState const scroll = ui.scroll_state(files_id);
```

## Metadata And Editor Inspection

```cpp
gui::end_frame(ui);

gui::BoxInfo const* hovered = ui.hit_test(input.mouse_pos);
if (hovered != nullptr && hovered->stable_id) {
    editor.selected_id = hovered->id;
}

gui::BoxInfo const* selected = ui.find_box(editor.selected_id);
if (selected != nullptr) {
    inspect_id(selected->id);
    inspect_label(selected->debug_name.empty() ? selected->text : selected->debug_name);
    inspect_rect(selected->rect);
    inspect_layout(selected->layout);
    inspect_resolved_style(selected->style);
}

for (size_t index = 0u; index < ui.box_info_count(); ++index) {
    gui::BoxInfo const* box = ui.box_info(index);
    if (box != nullptr && box->duplicate_id) {
        report_duplicate_id(box->id);
    }
}
```

Live editor overrides, serialization, text editing, and variable-height list
virtualization remain outside this v2 surface.
