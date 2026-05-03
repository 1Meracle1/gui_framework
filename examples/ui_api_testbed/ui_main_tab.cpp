#include "ui_main_tab.h"

#include "ui_common.h"

#include <base/config.h>
#include <base/fmt.h>

namespace ui_api_testbed {

    auto draw_preview_table_sort_control(
        gui::Frame& ui,
        gui::TableScope& table,
        TestbedState& state,
        gui::TableSortDesc const& desc,
        gui::Id checkbox_id,
        StrRef label,
        size_t column,
        float checkbox_width
    ) -> void {
        BASE_UNUSED(table.sort_button(column, desc));
        ui.checkbox(
            checkbox_id,
            label,
            state.preview_table_selected_columns + column,
            {.layout = {.width = gui::px(checkbox_width), .height = gui::fill()}}
        );
    }

    auto draw_main_tab(
        gui::Frame& ui,
        TestbedState& state,
        LiquidGlassSpec const& spec,
        TestbedTextures const& textures,
        TestbedChrome const& chrome
    ) -> void {
        gui::Id const list_id = gui::id("asset_list");
        gui::Id const notes_id = gui::id("notes_scroll");
        gui::Id const body_text_id = gui::id("body_text_scroll");
        gui::Id const log_id = gui::id("log_scroll");
        gui::ThemeTokens const& tokens = spec.tokens;

        if (auto body = ui.row(
                gui::id("body"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .gap = 10.0f,
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
                                .width = gui::px(216.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(10.0f),
                                .gap = 7.0f,
                                .align_x = gui::Align::STRETCH,
                            },
                        .style =
                            {
                                .background = spec.sidebar_background,
                                .border = spec.sidebar_border,
                                .border_thickness = 1.0f,
                                .radius = 20.0f,
                                .shadow =
                                    {.offset = {0.0f, 10.0f},
                                     .blur_radius = 28.0f,
                                     .color = spec.sidebar_shadow},
                            },
                        .debug_name = "sidebar",
                    }
                )) {
                ui.selectable_label(
                    "Virtualized Assets",
                    &state.title_selection,
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(22.0f),
                                .padding = gui::insets(0.0f, 6.0f),
                            },
                        .style = {
                            .foreground = tokens.text_muted,
                            .font_size = 12.0f,
                        },
                    }
                );

                gui::TreeNodeDesc tree_desc = {
                    .box =
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(24.0f),
                                    .padding = gui::insets(0.0f, 6.0f),
                                },
                            .style = {.role = gui::StyleRole::CONTROL, .radius = 9.0f},
                        },
                    .default_open = true,
                };
                gui::BoxDesc const tree_leaf = {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::text(),
                            .min_height = gui::px(20.0f),
                            .padding = gui::insets(0.0f, 6.0f),
                            .word_wrap = true,
                        },
                    .style = {.foreground = tokens.text_muted},
                };
                if (auto root_tree =
                        ui.tree_node(gui::id("asset_tree_root"), "Asset Browser", tree_desc)) {
                    if (auto textures_tree =
                            ui.tree_node(gui::id("asset_tree_textures"), "Textures", tree_desc)) {
                        ui.label(
                            gui::id("asset_tree_disk"), "ui_api_testbed_texture.png", tree_leaf
                        );
                        ui.label(gui::id("asset_tree_embedded"), "embedded_texture", tree_leaf);
                    }
                    tree_desc.default_open = false;
                    if (auto runtime_tree =
                            ui.tree_node(gui::id("asset_tree_runtime"), "Runtime", tree_desc)) {
                        ui.label(gui::id("asset_tree_draw"), "draw_context", tree_leaf);
                        ui.label(gui::id("asset_tree_font"), "font_cache", tree_leaf);
                    }
                }

                {
                    auto rows = ui.list_fixed(
                        list_id,
                        {
                            .item_count = 48u,
                            .item_height = 28.0f,
                            .box = {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(342.0f),
                                        .clip = true,
                                    },
                                .style =
                                    {
                                        .background = spec.list_background,
                                        .border = spec.list_border,
                                        .border_thickness = 1.0f,
                                        .radius = 14.0f,
                                    },
                                .debug_name = "asset_list",
                            },
                        }
                    );
                    for (size_t index = rows.first; index < rows.end; ++index) {
                        bool const selected = index == state.selected_index;
                        auto row_scope =
                            ui.id_scope(gui::id("asset_row", static_cast<uint64_t>(index)));
                        BASE_UNUSED(row_scope);
                        auto row = rows.row(
                            gui::id("row"),
                            {
                                .layout =
                                    {
                                        .padding = gui::insets(0.0f, 18.0f, 0.0f, 8.0f),
                                        .gap = 6.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style =
                                    {
                                        .role = selected ? gui::StyleRole::ACCENT
                                                         : gui::StyleRole::CONTROL,
                                        .border = selected ? spec.selected_row_border
                                                           : gui::rgba(255, 255, 255, 0),
                                        .radius = 9.0f,
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
                            {.layout = {.width = gui::fill(), .height = gui::fill()}}
                        );
                    }
                }

                if (auto notes = ui.scroll_panel(
                        notes_id,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(116.0f),
                                    .padding = gui::insets(8.0f),
                                    .gap = 4.0f,
                                    .clip = true,
                                },
                            .style =
                                {
                                    .background = spec.notes_background,
                                    .foreground = tokens.text_muted,
                                    .border = spec.notes_border,
                                    .border_thickness = 1.0f,
                                    .radius = 14.0f,
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

                if (auto controls = ui.row(gui::id("controls"), chrome.controls_bar)) {
                    if (auto switches = ui.row(gui::id("control_switches"), chrome.toolbar_group)) {
                        ui.checkbox(
                            gui::id("enabled_checkbox"),
                            "Enabled",
                            &state.enabled,
                            chrome.enabled_checkbox
                        );
                        ui.toggle(
                            gui::id("preview_toggle"),
                            "Preview",
                            &state.preview,
                            {
                                .layout = {.width = gui::px(112.0f), .height = gui::fill()},
                            }
                        );
                    }
                    if (auto scale = ui.row(gui::id("scale_control"), chrome.toolbar_group)) {
                        ui.label(
                            "Scale",
                            {
                                .layout = {.width = gui::px(38.0f), .height = gui::fill()},
                                .style = chrome.toolbar_label,
                            }
                        );
                        ui.slider_float(
                            gui::id("scale_slider"),
                            " ",
                            &state.scale,
                            {
                                .box =
                                    {
                                        .layout =
                                            {
                                                .width = gui::px(126.0f),
                                                .height = gui::fill(),
                                            },
                                    },
                                .min = 0.5f,
                                .max = 2.0f,
                                .step = 0.05f,
                            }
                        );
                    }
                    if (auto size =
                            ui.row(gui::id("chrome.size_radio_group"), chrome.toolbar_group)) {
                        ui.label(
                            "Size",
                            {
                                .layout = {.width = gui::px(32.0f), .height = gui::fill()},
                                .style = chrome.toolbar_label,
                            }
                        );
                        ui.radio_button(
                            gui::id("size_small_radio"),
                            "Small",
                            &state.size_mode,
                            0u,
                            chrome.size_radio
                        );
                        ui.radio_button(
                            gui::id("size_medium_radio"),
                            "Med",
                            &state.size_mode,
                            1u,
                            chrome.size_radio
                        );
                        ui.radio_button(
                            gui::id("size_large_radio"),
                            "Large",
                            &state.size_mode,
                            2u,
                            chrome.size_radio
                        );
                    }
                    if (auto edit = ui.row(gui::id("control_edit"), chrome.toolbar_group)) {
                        ui.checkbox(
                            gui::id("read_only_checkbox"),
                            "Read-only",
                            &state.read_only_value,
                            chrome.read_only_checkbox
                        );
                        ui.input_text(
                            gui::id("name_input"),
                            "Name",
                            state.name,
                            sizeof(state.name),
                            {
                                .layout =
                                    {
                                        .width = gui::px(138.0f),
                                        .height = gui::fill(),
                                        .padding = gui::insets(4.0f, 8.0f),
                                    },
                                .debug_name = "name_input",
                            }
                        );
                        ui.button(
                            gui::id("disabled_button"),
                            "Disabled",
                            {
                                .layout =
                                    {
                                        .width = gui::px(82.0f),
                                        .height = gui::fill(),
                                        .padding = gui::insets(3.0f, 6.0f),
                                    },
                                .flags = gui::BOX_FLAG_DISABLED,
                            }
                        );
                    }
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                    if (auto actions = ui.row(gui::id("control_actions"), chrome.toolbar_group)) {
                        if (ui.button(
                                  gui::id("popup_button"),
                                  "Popup",
                                  {
                                      .layout =
                                          {
                                              .width = gui::px(84.0f),
                                              .height = gui::fill(),
                                              .padding = gui::insets(3.0f, 6.0f),
                                          },
                                      .icon = {.texture = textures.embedded.texture, .size = 14.0f},
                                      .debug_name = "popup_button",
                                  }
                            )
                                .activated) {
                            state.popup_open = !state.popup_open;
                        }
                        gui::Signal const info_signal = ui.button(
                            gui::id("hover_info_button"),
                            "Info",
                            {
                                .layout =
                                    {
                                        .width = gui::px(58.0f),
                                        .height = gui::fill(),
                                        .padding = gui::insets(3.0f, 6.0f),
                                    },
                                .debug_name = "hover_info_button",
                            }
                        );
                        if (auto info = ui.hover_popup(
                                gui::id("hover_info_popup"),
                                info_signal,
                                {
                                    .layout =
                                        {
                                            .width = gui::px(270.0f),
                                            .height = gui::children(),
                                            .margin = gui::insets(28.0f, 0.0f, 0.0f, 0.0f),
                                            .padding = gui::insets(10.0f, 12.0f),
                                            .gap = 6.0f,
                                            .align_x = gui::Align::STRETCH,
                                        },
                                    .debug_name = "hover_info_popup",
                                }
                            )) {
                            ui.label(
                                "Hover popup",
                                {
                                    .layout = {.width = gui::fill(), .height = gui::px(22.0f)},
                                    .style = {.foreground = tokens.text, .font_size = 14.0f},
                                }
                            );
                            ui.label(
                                "Stays open while the Info button or this popup is hovered.",
                                {
                                    .layout = {.width = gui::fill(), .height = gui::text()},
                                    .style = {.foreground = tokens.text_muted},
                                }
                            );
                        }
                        if (ui.button(
                                  gui::id("modal_button"),
                                  "Modal",
                                  {
                                      .layout =
                                          {
                                              .width = gui::px(68.0f),
                                              .height = gui::fill(),
                                              .padding = gui::insets(3.0f, 6.0f),
                                          },
                                      .style = {.role = gui::StyleRole::ACCENT},
                                      .debug_name = "modal_button",
                                  }
                            )
                                .activated) {
                            state.modal_open = true;
                        }
                    }
                }

                if (auto body_text = ui.scroll_panel(
                        body_text_id,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(148.0f),
                                    .padding = gui::insets(12.0f, 18.0f, 12.0f, 12.0f),
                                    .clip = true,
                                },
                            .style =
                                {
                                    .background = spec.body_text_background,
                                    .foreground = tokens.text,
                                    .border = spec.body_text_border,
                                    .border_thickness = 1.0f,
                                    .radius = 14.0f,
                                },
                            .debug_name = "body_text_scroll",
                        }
                    )) {
                    ui.selectable_label(
                        gui::id("body_text"),
                        BODY_TEXT,
                        &state.body_selection,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::text(),
                                    // .word_wrap = true,
                                },
                            .style = {.foreground = tokens.text},
                            .debug_name = "body_text",
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
                                    .align_x = gui::Align::START,
                                    .align_y = gui::Align::START,
                                    .clip = true,
                                },
                            .style =
                                {
                                    .role = gui::StyleRole::PANEL,
                                    .shadow = {.color = gui::rgba(0, 0, 0, 0)},
                                },
                            .debug_name = "preview_overlay",
                        }
                    )) {
                    if (auto top = ui.row(
                            gui::id("preview_top"),
                            {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(28.0f),
                                        .gap = 8.0f,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .debug_name = "preview_top",
                            }
                        )) {
                        ui.icon(
                            gui::id("sample_icon"),
                            textures.embedded.texture,
                            {
                                .style = {.foreground = tokens.text_muted},
                                .icon = {.size = 18.0f},
                            }
                        );
                        ui.label(
                            "Preview fills the overlay",
                            {
                                .layout = {.width = gui::text(), .height = gui::fill()},
                                .style = {.foreground = tokens.text_muted},
                            }
                        );
                        ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                        ui.image(
                            gui::id("sample_image"),
                            textures.disk.texture,
                            {
                                .box = {
                                    .layout = {
                                        .width = gui::px(48.0f),
                                        .height = gui::px(24.0f),
                                    },
                                },
                            }
                        );
                        if (auto badge = ui.row(
                                gui::id("overlay_badge"),
                                {
                                    .layout =
                                        {
                                            .width = gui::children(),
                                            .height = gui::px(28.0f),
                                            .padding = gui::insets(3.0f, 8.0f),
                                            .align_y = gui::Align::CENTER,
                                        },
                                    .style = {.role = gui::StyleRole::CONTROL},
                                    .debug_name = "overlay_badge",
                                }
                            )) {
                            ui.label(
                                "Overlay", {.layout = {.width = gui::text(), .height = gui::fill()}}
                            );
                        }
                    }
                    ui.input_text_multiline(
                        gui::id("preview_multiline_input"),
                        "Preview Notes",
                        &state.multiline_text_buffer,
                        {
                            .box = {
                                .layout =
                                    {
                                        .width = gui::fill(),
                                        .height = gui::px(108.0f),
                                        .margin = gui::insets(36.0f, 0.0f, 0.0f, 0.0f),
                                        .padding = gui::insets(9.0f, 12.0f),
                                        // .word_wrap = true,
                                    },
                                .style =
                                    {
                                        .background = spec.preview_input_background,
                                        .foreground = tokens.text,
                                        .border = spec.preview_input_border,
                                        .border_thickness = 1.0f,
                                        .radius = 12.0f,
                                    },
                                .debug_name = "preview_multiline_input",
                            },
                        }
                    );
                    gui::Color status_background = tokens.accent;
                    status_background.a = 0.20f;
                    gui::Color status_border = tokens.accent;
                    status_border.a = 0.36f;
                    gui::StyleDesc const table_key_cell_style = {
                        .background = spec.table_header_background,
                        .foreground = tokens.text_muted,
                    };
                    gui::StyleDesc const table_status_cell_style = {
                        .background = status_background,
                        .foreground = tokens.text,
                        .border = status_border,
                    };
                    gui::TableSortDesc const table_sort_desc = {
                        .columns = slice(state.preview_table_sort_columns),
                        .column_count = &state.preview_table_sort_count,
                        .selected_columns = slice(state.preview_table_selected_columns),
                        .box = {
                            .layout = {
                                .width = gui::px(22.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f),
                            },
                        },
                    };
                    if (auto table = ui.table(
                            gui::id("preview_table"),
                            {
                                .box =
                                    {
                                        .layout =
                                            {
                                                .width = gui::children(),
                                                .height = gui::children(),
                                                .margin = gui::insets(154.0f, 0.0f, 0.0f, 0.0f),
                                                .padding = gui::insets(4.0f),
                                                .gap = 3.0f,
                                            },
                                        .debug_name = "preview_table",
                                    },
                                .sort = table_sort_desc,
                            }
                        )) {
                        if (auto header = table.header_row()) {
                            if (auto cell = header.cell(
                                    gui::id("preview_table_header_plan"),
                                    {
                                        .column_span = 2u,
                                    }
                                )) {
                                if (auto sort_row = ui.row(
                                        gui::id("preview_table_plan_sort"),
                                        {
                                            .layout = {
                                                .width = gui::children(),
                                                .height = gui::fill(),
                                                .gap = 4.0f,
                                                .align_y = gui::Align::CENTER,
                                            },
                                        }
                                    )) {
                                    draw_preview_table_sort_control(
                                        ui,
                                        table,
                                        state,
                                        table_sort_desc,
                                        gui::id("preview_table_select_group"),
                                        "Group",
                                        0u,
                                        78.0f
                                    );
                                    draw_preview_table_sort_control(
                                        ui,
                                        table,
                                        state,
                                        table_sort_desc,
                                        gui::id("preview_table_select_task"),
                                        "Task",
                                        1u,
                                        68.0f
                                    );
                                }
                            }
                            if (auto cell = header.cell(
                                    gui::id("preview_table_header_status"),
                                    {
                                        .box = {
                                            .layout = {
                                                .width = gui::px(128.0f),
                                            },
                                        },
                                    }
                                )) {
                                if (auto sort_row = ui.row(
                                        gui::id("preview_table_status_sort"),
                                        {
                                            .layout = {
                                                .width = gui::children(),
                                                .height = gui::fill(),
                                                .gap = 4.0f,
                                                .align_y = gui::Align::CENTER,
                                            },
                                        }
                                    )) {
                                    draw_preview_table_sort_control(
                                        ui,
                                        table,
                                        state,
                                        table_sort_desc,
                                        gui::id("preview_table_select_status"),
                                        "Status",
                                        2u,
                                        82.0f
                                    );
                                }
                            }
                        }
                        for (size_t source = 0u; source < PREVIEW_TABLE_ROW_COUNT; ++source) {
                            PreviewTableRow const& item = PREVIEW_TABLE_ROWS[source];
                            auto row_scope = ui.id_scope(
                                gui::id("preview_table_row", static_cast<uint64_t>(source))
                            );
                            BASE_UNUSED(row_scope);
                            if (auto row = table.row(gui::id("row"))) {
                                if (auto cell = row.cell(
                                        gui::id("group"),
                                        {
                                            .box = {
                                                .layout =
                                                    {
                                                        .width = gui::px(112.0f),
                                                    },
                                                .style = table_key_cell_style,
                                            },
                                        }
                                    )) {
                                    ui.label(
                                        item.group,
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                                if (auto cell = row.cell(
                                        gui::id("task"),
                                        {
                                            .box = {
                                                .layout = {
                                                    .width = gui::px(204.0f),
                                                },
                                            },
                                        }
                                    )) {
                                    ui.label(
                                        item.task,
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                                if (auto cell = row.cell(
                                        gui::id("status"),
                                        {
                                            .box = {
                                                .layout =
                                                    {
                                                        .width = gui::px(112.0f),
                                                    },
                                                .style = table_status_cell_style,
                                            },
                                        }
                                    )) {
                                    ui.label(
                                        item.status,
                                        {.layout = {.width = gui::fill(), .height = gui::fill()}}
                                    );
                                }
                            }
                        }
                    }
                    if (state.popup_open) {
                        if (auto popup = ui.popup(
                                gui::id("sample_popup"),
                                {
                                    .layout =
                                        {
                                            .width = gui::px(248.0f),
                                            .height = gui::children(),
                                            .margin = gui::insets(58.0f, 0.0f, 0.0f, 690.0f),
                                            .padding = gui::insets(10.0f, 12.0f),
                                            .gap = 8.0f,
                                            .align_x = gui::Align::STRETCH,
                                        },
                                    .debug_name = "sample_popup",
                                }
                            )) {
                            if (auto header = ui.row(
                                    gui::id("popup_header"),
                                    {
                                        .layout = {
                                            .width = gui::fill(),
                                            .height = gui::px(30.0f),
                                            .gap = 8.0f,
                                            .align_y = gui::Align::CENTER,
                                        },
                                    }
                                )) {
                                ui.label(
                                    "Floating popup",
                                    {
                                        .layout = {.width = gui::fill(), .height = gui::fill()},
                                        .style = {
                                            .foreground = tokens.text,
                                            .font_size = 14.0f,
                                        },
                                    }
                                );
                                if (ui.button(
                                          gui::id("popup_close"),
                                          CLOSE_GLYPH,
                                          {
                                              .layout =
                                                  {
                                                      .width = gui::px(28.0f),
                                                      .height = gui::px(28.0f),
                                                      .padding = gui::insets(0.0f),
                                                  },
                                              .style =
                                                  {
                                                      .role = gui::StyleRole::DANGER,
                                                      .font_size = 15.0f,
                                                  },
                                          }
                                    )
                                        .activated) {
                                    state.popup_open = false;
                                }
                            }
                        }
                    }
                }

                if (auto log = ui.scroll_panel(
                        log_id,
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::px(104.0f),
                                    .padding = gui::insets(10.0f, 18.0f, 10.0f, 10.0f),
                                    .gap = 4.0f,
                                    .clip = true,
                                },
                            .style =
                                {
                                    .background = spec.log_background,
                                    .foreground = spec.log_foreground,
                                    .border = spec.log_border,
                                    .border_thickness = 1.0f,
                                    .radius = 14.0f,
                                },
                            .debug_name = "log_scroll",
                        }
                    )) {
                    draw_scroll_lines(ui, "Log entry", 8u);
                }
            }
        }
    }

} // namespace ui_api_testbed
