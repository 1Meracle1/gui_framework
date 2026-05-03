#include "ui_repository.h"

#include "repo_data.h"
#include "ui_common.h"

#include <algorithm>

namespace repository_ui_testbed {

    namespace font_cache = gui::font_cache;

    uint64_t decorative_label_index = 0u;

    constexpr RepositoryTab REPOSITORY_TABS[] = {
        {"Files", "", 70.0f, 0.0f},
        {"Commits", "", 146.0f, 52.0f},
        {"Branches", "23", 122.0f, 30.0f},
        {"Tags", "1,247", 116.0f, 46.0f},
        {"Releases", "412", 126.0f, 38.0f},
        {"Contributors", "3.2k", 158.0f, 42.0f},
    };

    constexpr StrRef SECTION_TITLES[] = {
        "Code",
        "Issues",
        "Pull requests",
        "Actions",
        "Projects",
        "Security",
        "Insights",
        "Wiki",
        "Settings",
    };
    static_assert(
        sizeof(SECTION_TITLES) / sizeof(SECTION_TITLES[0]) ==
        static_cast<size_t>(RepositorySection::COUNT)
    );

    constexpr StrRef SECTION_ROWS[] = {"Overview", "Open items", "Recent activity", "Timeline"};

    [[nodiscard]] auto section_title(RepositorySection section) -> StrRef {
        return SECTION_TITLES[static_cast<size_t>(section)];
    }

    [[nodiscard]] auto decorative_label_id() -> gui::Id {
        gui::Id const result = indexed_id("decorative_label", decorative_label_index);
        decorative_label_index += 1u;
        return result;
    }

    [[nodiscard]] auto separator_y(RepositorySpec const& spec) -> gui::BoxDesc {
        return {
            .layout = {.width = gui::fill(), .height = gui::px(1.0f)},
            .style = {.background = spec.border},
        };
    }

    [[nodiscard]] auto separator_x(RepositorySpec const& spec) -> gui::BoxDesc {
        return {
            .layout = {.width = gui::px(1.0f), .height = gui::fill()},
            .style = {.background = spec.border},
        };
    }

    auto label(gui::Frame& ui, StrRef text, float size, gui::StyleRole role) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::text(), .height = gui::fill()},
                .style = {.role = role, .font_size = size},
            }
        );
    }

    auto label_fill(gui::Frame& ui, StrRef text, float size, gui::StyleRole role) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::fill(), .height = gui::fill()},
                .style = {.role = role, .font_size = size},
            }
        );
    }

    auto label_color(gui::Frame& ui, StrRef text, float size, gui::Color color) -> void {
        ui.label(
            decorative_label_id(),
            text,
            {
                .layout = {.width = gui::text(), .height = gui::fill()},
                .style = {.role = gui::StyleRole::NONE, .foreground = color, .font_size = size},
            }
        );
    }

    auto icon_label(
        gui::Frame& ui,
        font_cache::Font icon_font,
        StrRef icon,
        gui::Color color,
        float size,
        float width
    ) -> void {
        ui.label(
            decorative_label_id(),
            icon,
            {
                .layout = {.width = gui::px(width), .height = gui::px(20.0f)},
                .style = {
                    .role = gui::StyleRole::NONE,
                    .foreground = color,
                    .font = icon_font,
                    .font_size = size,
                },
            }
        );
    }

    auto draw_nav_item(
        gui::Frame& ui,
        RepositorySpec const& spec,
        font_cache::Font icon_font,
        NavItem item,
        RepositorySection& selected_section
    ) -> void {
        bool const selected = selected_section == item.section;
        gui::Color const color = selected ? spec.text : spec.muted;
        gui::BoxDesc desc = {
            .layout =
                {
                    .width = gui::fill(),
                    .height = gui::px(36.0f),
                    .margin = gui::insets(0.0f, 8.0f),
                    .padding = gui::insets(0.0f, 12.0f),
                    .gap = 12.0f,
                    .align_y = gui::Align::CENTER,
                },
            .style = {
                .background = selected ? spec.selected : gui::unset_color(),
                .foreground = color,
                .radius = 6.0f,
            },
        };
        if (auto row = ui.row(item.id, desc)) {
            if (row.signal().activated) {
                selected_section = item.section;
            }
            icon_label(ui, icon_font, item.icon, color, 16.0f, 20.0f);
            label_fill(
                ui, item.label, 13.5f, selected ? gui::StyleRole::TEXT : gui::StyleRole::TEXT_MUTED
            );
            if (!item.count.empty()) {
                label(ui, item.count, 12.5f, gui::StyleRole::TEXT_MUTED);
            } else if (item.chevron) {
                icon_label(ui, icon_font, ICON_CHEVRON_RIGHT, spec.muted, 16.0f, 14.0f);
            }
        }
    }

    auto draw_sidebar(
        gui::Frame& ui,
        RepositorySpec const& spec,
        font_cache::Font icon_font,
        RepositorySection& selected_section
    ) -> void {
        if (auto sidebar = ui.column(
                gui::id("sidebar"),
                {
                    .layout = {.width = gui::px(SIDEBAR_WIDTH), .height = gui::fill()},
                    .style = {.background = spec.shell},
                }
            )) {
            if (auto header = ui.row(
                    gui::id("workspace_header"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 16.0f),
                            .gap = 8.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                ui.spacer({.layout = {.width = gui::px(26.0f), .height = gui::px(1.0f)}});
                label(ui, "Acme", 14.0f, gui::StyleRole::TEXT);
                ui.label(
                    "Pro",
                    {
                        .layout =
                            {
                                .width = gui::px(42.0f),
                                .height = gui::px(20.0f),
                                .padding = gui::insets(0.0f, 8.0f),
                            },
                        .style = {
                            .background = spec.blue,
                            .foreground = gui::rgb(82, 175, 255),
                            .radius = 10.0f,
                            .font_size = 12.0f,
                        },
                    }
                );
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, "v", 16.0f, gui::StyleRole::TEXT_MUTED);
            }

            ui.spacer(separator_y(spec));
            if (auto search = ui.row(
                    gui::id("search_box"),
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(32.0f),
                                .margin = gui::insets(12.0f, 10.0f),
                                .padding = gui::insets(0.0f, 12.0f, 0.0f, 42.0f),
                                .align_y = gui::Align::CENTER,
                            },
                        .style = {
                            .background = spec.control,
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 6.0f,
                        },
                    }
                )) {
                label_fill(ui, "Find...", 13.0f, gui::StyleRole::TEXT_MUTED);
                ui.label(
                    "F",
                    {
                        .layout = {.width = gui::px(22.0f), .height = gui::px(22.0f)},
                        .style = {
                            .background = gui::rgb(13, 13, 13),
                            .foreground = spec.faint,
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 5.0f,
                            .font_size = 11.0f,
                        },
                    }
                );
            }

            NavItem const items[] = {
                {
                    gui::id("nav_code"),
                    RepositorySection::CODE,
                    ICON_CODE,
                    "Code",
                    "",
                },
                {
                    gui::id("nav_issues"),
                    RepositorySection::ISSUES,
                    ICON_ISSUES,
                    "Issues",
                    "1.2k",
                },
                {
                    gui::id("nav_prs"),
                    RepositorySection::PULL_REQUESTS,
                    ICON_PULL_REQUESTS,
                    "Pull requests",
                    "247",
                },
                {
                    gui::id("nav_actions"),
                    RepositorySection::ACTIONS,
                    ICON_ACTIONS,
                    "Actions",
                    "",
                    true,
                },
                {
                    gui::id("nav_projects"),
                    RepositorySection::PROJECTS,
                    ICON_PROJECTS,
                    "Projects",
                    "8",
                },
                {
                    gui::id("nav_security"),
                    RepositorySection::SECURITY,
                    ICON_SECURITY,
                    "Security",
                    "",
                    true,
                },
                {
                    gui::id("nav_insights"),
                    RepositorySection::INSIGHTS,
                    ICON_INSIGHTS,
                    "Insights",
                    "",
                    true,
                },
                {gui::id("nav_wiki"), RepositorySection::WIKI, ICON_WIKI, "Wiki", ""},
                {
                    gui::id("nav_settings"),
                    RepositorySection::SETTINGS,
                    ICON_SETTINGS,
                    "Settings",
                    "",
                },
            };
            for (size_t index = 0u; index < sizeof(items) / sizeof(items[0]); ++index) {
                draw_nav_item(ui, spec, icon_font, items[index], selected_section);
            }

            ui.spacer({.layout = {.width = gui::fill(), .height = gui::fill()}});
            ui.spacer(separator_y(spec));
            if (auto user = ui.row(
                    gui::id("user_switcher"),
                    {
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::px(62.0f),
                            .padding = gui::insets(0.0f, 16.0f),
                            .gap = 10.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                ui.spacer({.layout = {.width = gui::px(26.0f), .height = gui::px(1.0f)}});
                if (auto names = ui.column(
                        gui::id("user_name_block"),
                        {
                            .layout = {
                                .width = gui::children(),
                                .height = gui::children(),
                                .gap = 1.0f,
                            },
                        }
                    )) {
                    ui.label(
                        "Evil Rabbit", {.layout = {.width = gui::text(), .height = gui::px(18.0f)}}
                    );
                    ui.label(
                        "Hobby",
                        {
                            .layout = {.width = gui::text(), .height = gui::px(16.0f)},
                            .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                        }
                    );
                }
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, "v", 16.0f, gui::StyleRole::TEXT_MUTED);
            }
        }
    }

    auto draw_tab(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepositoryTab tab_data,
        size_t tab_index,
        size_t& selected_tab
    ) -> void {
        bool const selected = selected_tab == tab_index;
        auto tab_scope =
            ui.id_scope(indexed_id("repository_tab", static_cast<uint64_t>(tab_index)));
        BASE_UNUSED(tab_scope);
        if (auto tab = ui.column(
                gui::id("tab"),
                {
                    .layout =
                        {
                            .width = gui::px(tab_data.width),
                            .height = gui::fill(),
                            .gap = 0.0f,
                        },
                    .style = {.foreground = selected ? spec.text : spec.muted},
                }
            )) {
            if (tab.signal().activated) {
                selected_tab = tab_index;
            }
            if (auto row = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::fill(),
                        .gap = 8.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label(
                    ui,
                    tab_data.title,
                    14.0f,
                    selected ? gui::StyleRole::TEXT : gui::StyleRole::TEXT_MUTED
                );
                if (!tab_data.count.empty()) {
                    if (auto badge = ui.row(
                            decorative_label_id(),
                            {
                                .layout =
                                    {
                                        .width = gui::px(tab_data.badge_width),
                                        .height = gui::px(18.0f),
                                        .align_x = gui::Align::CENTER,
                                        .align_y = gui::Align::CENTER,
                                    },
                                .style = {
                                    .background = spec.control,
                                    .border = spec.border,
                                    .border_thickness = 1.0f,
                                    .radius = 5.0f,
                                },
                            }
                        )) {
                        ui.label(
                            decorative_label_id(),
                            tab_data.count,
                            {
                                .layout =
                                    {
                                        .width = gui::text(),
                                        .height = gui::fill(),
                                        .margin = gui::insets(0.0f, 2.0f, 0.0f, 0.0f),
                                    },
                                .style = {
                                    .role = gui::StyleRole::NONE,
                                    .foreground = selected ? spec.text : spec.muted,
                                    .font_size = 11.0f,
                                },
                            }
                        );
                    }
                }
            }
            ui.spacer({
                .layout = {.width = gui::fill(), .height = gui::px(1.0f)},
                .style = {.background = selected ? spec.text : gui::rgba(0, 0, 0, 0)},
            });
        }
    }

    auto draw_top_bar(
        gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details, size_t& selected_tab
    ) -> void {
        if (auto top = ui.row(
                gui::id("top_bar"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 20.0f),
                            .gap = 16.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            if (auto env = ui.row(
                    gui::id("environment"),
                    {
                        .layout = {
                            .width = gui::children(),
                            .height = gui::fill(),
                            .padding = gui::insets(0.0f, 0.0f, 0.0f, 18.0f),
                            .align_y = gui::Align::CENTER,
                        },
                    }
                )) {
                label(ui, "Production", 13.0f, gui::StyleRole::TEXT_MUTED);
            }
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            ui.label(
                details.branch,
                {
                    .layout = {.width = gui::text(), .height = gui::fill()},
                    .style = {.font_size = 13.5f},
                }
            );
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.short_hash, 13.0f, gui::StyleRole::TEXT_MUTED);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
            label(ui, "Local", 12.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.activity, 12.5f, gui::StyleRole::TEXT_MUTED);
        }
        ui.spacer(separator_y(spec));

        if (auto tabs = ui.row(
                gui::id("tabs"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(46.0f),
                        .padding = gui::insets(0.0f, 24.0f),
                        .gap = 28.0f,
                    },
                }
            )) {
            for (size_t index = 0u; index < sizeof(REPOSITORY_TABS) / sizeof(REPOSITORY_TABS[0]);
                 ++index) {
                RepositoryTab tab = REPOSITORY_TABS[index];
                if (index == 1u) {
                    tab.count = StrRef(details.commit_count);
                    tab.badge_width = std::max(
                        30.0f, 18.0f + 7.0f * static_cast<float>(details.commit_count.size())
                    );
                    tab.width = 94.0f + tab.badge_width;
                }
                draw_tab(ui, spec, tab, index, selected_tab);
            }
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_stat_cell(
        gui::Frame& ui, RepositorySpec const& spec, StrRef label_text, StrRef value, bool last
    ) -> void {
        if (auto cell = ui.column({
                .layout = {
                    .width = gui::fill(),
                    .height = gui::fill(),
                    .padding = gui::insets(15.0f, 20.0f),
                    .gap = 8.0f,
                },
            })) {
            label(ui, label_text, 12.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, value, 14.0f, gui::StyleRole::TEXT);
        }
        if (!last) {
            ui.spacer(separator_x(spec));
        }
    }

    auto draw_repo_summary(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto summary = ui.column(
                gui::id("summary"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children(), .gap = 6.0f},
                }
            )) {
            if (auto crumb = ui.row(
                    {.layout = {.width = gui::fill(), .height = gui::px(20.0f), .gap = 8.0f}}
                )) {
                label(ui, "Repository", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "/", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "Files", 12.5f, gui::StyleRole::TEXT);
            }
            ui.label(
                details.name,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(28.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.label(
                details.description,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(24.0f)},
                    .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.5f},
                }
            );
        }

        if (auto stats = ui.row(
                gui::id("stats"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(76.0f),
                            .margin = gui::insets(16.0f, 0.0f, 0.0f, 0.0f),
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            draw_stat_cell(ui, spec, "Current Branch", details.branch, false);
            draw_stat_cell(ui, spec, "Last Commit", details.last_commit_age, false);
            draw_stat_cell(ui, spec, "Latest Tag", details.latest_release, false);
            draw_stat_cell(ui, spec, "Repository Size", details.package_size, false);
            draw_stat_cell(ui, spec, "License", details.license, false);
            draw_stat_cell(ui, spec, "Activity", details.activity, true);
        }
    }

    auto draw_latest_commit(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto panel = ui.column(
                gui::id("latest_commit"),
                {
                    .layout = {.width = gui::fill(), .height = gui::px(142.0f)},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(40.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label_fill(ui, "Latest commit", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "View history ->", 12.5f, gui::StyleRole::TEXT_MUTED);
            }
            ui.spacer(separator_y(spec));
            if (auto body = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::fill(),
                        .padding = gui::insets(18.0f, 20.0f),
                        .gap = 16.0f,
                    },
                })) {
                if (auto avatar = ui.row({
                        .layout =
                            {
                                .width = gui::px(32.0f),
                                .height = gui::px(32.0f),
                                .align_x = gui::Align::CENTER,
                                .align_y = gui::Align::CENTER,
                            },
                        .style = {
                            .background = gui::rgb(12, 12, 12),
                            .border = spec.border,
                            .border_thickness = 1.0f,
                            .radius = 16.0f,
                        },
                    })) {
                    ui.label(
                        details.author_initial,
                        {
                            .layout = {.width = gui::text(), .height = gui::fill()},
                            .style = {.role = gui::StyleRole::TEXT, .font_size = 12.0f},
                        }
                    );
                }
                if (auto commit_text = ui.column({
                        .layout = {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .gap = 8.0f,
                        },
                    })) {
                    if (auto byline = ui.row({
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(20.0f),
                                .gap = 8.0f,
                            },
                        })) {
                        label(ui, details.author, 13.0f, gui::StyleRole::TEXT);
                        label(ui, "authored", 12.5f, gui::StyleRole::TEXT_MUTED);
                        label(ui, details.last_commit_age, 12.5f, gui::StyleRole::TEXT_MUTED);
                    }
                    ui.label(
                        details.last_commit_subject,
                        {.layout = {.width = gui::fill(), .height = gui::px(22.0f)}}
                    );
                    if (auto meta = ui.row({
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(20.0f),
                                .gap = 12.0f,
                            },
                        })) {
                        label(ui, details.short_hash, 12.0f, gui::StyleRole::TEXT_MUTED);
                        label(ui, details.branch, 12.0f, gui::StyleRole::TEXT_MUTED);
                        label_color(ui, details.insertion_count, 12.0f, spec.green);
                        label_color(ui, details.deletion_count, 12.0f, spec.red);
                    }
                }
            }
        }
    }

    auto draw_file_row(gui::Frame& ui, RepositorySpec const& spec, RepoTree& tree, size_t index)
        -> void {
        int32_t const node_index = tree.visible[index];
        RepoNode& node = tree.nodes[node_index];
        auto row_scope = ui.id_scope(indexed_id("file_row", static_cast<uint64_t>(node_index)));
        BASE_UNUSED(row_scope);
        if (auto item = ui.row(
                gui::id("row"),
                {
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(44.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .gap = 0.0f,
                        .align_y = gui::Align::CENTER,
                    },
                }
            )) {
            if (item.signal().activated && node.directory) {
                node.open = !node.open;
            }
            ui.label(
                node.name,
                {
                    .layout =
                        {
                            .width = gui::px(340.0f),
                            .height = gui::fill(),
                            .padding = gui::insets(
                                0.0f, 0.0f, 0.0f, 56.0f + 18.0f * static_cast<float>(node.indent)
                            ),
                        },
                    .style = {.font_size = 13.5f},
                }
            );
            ui.label(
                node.message,
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                }
            );
            if (auto age = ui.row({
                    .layout = {
                        .width = gui::px(132.0f),
                        .height = gui::fill(),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                label(ui, node.age, 12.5f, gui::StyleRole::TEXT_MUTED);
            }
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_file_table(gui::Frame& ui, RepositorySpec const& spec, RepoTree& tree) -> void {
        rebuild_visible_tree(tree);
        if (auto table = ui.column(
                gui::id("file_table"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto toolbar = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(50.0f),
                        .padding = gui::insets(10.0f, 14.0f),
                        .gap = 8.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                if (auto search = ui.row(
                        gui::id("go_to_file"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(0.0f, 10.0f, 0.0f, 32.0f),
                                    .align_y = gui::Align::CENTER,
                                },
                            .style = {
                                .background = spec.control,
                                .border = spec.border,
                                .border_thickness = 1.0f,
                                .radius = 6.0f,
                            },
                        }
                    )) {
                    label_fill(ui, "Go to file", 13.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, "T", 11.0f, gui::StyleRole::TEXT_MUTED);
                }
                ui.button(
                    "+ Add file",
                    {
                        .layout =
                            {
                                .width = gui::px(108.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {.font_size = 13.0f},
                    }
                );
                ui.button(
                    "Clone",
                    {
                        .layout =
                            {
                                .width = gui::px(100.0f),
                                .height = gui::fill(),
                                .padding = gui::insets(0.0f, 10.0f),
                            },
                        .style = {
                            .role = gui::StyleRole::NONE,
                            .background = gui::rgb(239, 239, 239),
                            .foreground = gui::rgb(24, 24, 24),
                            .border = gui::rgb(255, 255, 255),
                            .border_thickness = 1.0f,
                            .radius = 7.0f,
                            .font_size = 13.0f,
                        },
                    }
                );
            }
            ui.spacer(separator_y(spec));
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(36.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                ui.label(
                    "NAME",
                    {
                        .layout = {.width = gui::px(340.0f), .height = gui::fill()},
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                    }
                );
                ui.label(
                    "LAST COMMIT MESSAGE",
                    {
                        .layout = {.width = gui::fill(), .height = gui::fill()},
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 11.0f},
                    }
                );
                if (auto age =
                        ui.row({.layout = {.width = gui::px(132.0f), .height = gui::fill()}})) {
                    ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
                    label(ui, "LAST COMMIT", 11.0f, gui::StyleRole::TEXT_MUTED);
                }
            }
            ui.spacer(separator_y(spec));
            if (!tree.loaded) {
                ui.label(
                    "No git repository tree found",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                            },
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                    }
                );
            }
            for (size_t index = 0u; index < tree.visible_count; ++index) {
                draw_file_row(ui, spec, tree, index);
            }
        }
    }

    auto draw_commits_tab(gui::Frame& ui, RepositorySpec const& spec, RepoDetails const& details)
        -> void {
        if (auto panel = ui.column(
                gui::id("commits_tab_content"),
                {
                    .layout = {.width = gui::fill(), .height = gui::children()},
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto header = ui.row({
                    .layout = {
                        .width = gui::fill(),
                        .height = gui::px(58.0f),
                        .padding = gui::insets(0.0f, 20.0f),
                        .gap = 10.0f,
                        .align_y = gui::Align::CENTER,
                    },
                })) {
                label_fill(ui, "Commits", 23.0f, gui::StyleRole::TEXT);
                label(ui, details.commit_count, 13.0f, gui::StyleRole::TEXT_MUTED);
            }
            ui.spacer(separator_y(spec));
            if (details.shown_commit_count == 0u) {
                ui.label(
                    "No commits found",
                    {
                        .layout =
                            {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                            },
                        .style = {.role = gui::StyleRole::TEXT_MUTED, .font_size = 13.0f},
                    }
                );
                return;
            }

            for (size_t index = 0u; index < details.shown_commit_count; ++index) {
                RepoCommit const& commit = details.commits[index];
                auto row_scope =
                    ui.id_scope(indexed_id("commit_row", static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(46.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, commit.subject, 13.5f, gui::StyleRole::TEXT);
                    label(ui, commit.hash, 12.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, commit.author, 12.0f, gui::StyleRole::TEXT_MUTED);
                    label(ui, commit.age, 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_secondary_tab_content(gui::Frame& ui, RepositorySpec const& spec, size_t selected_tab)
        -> void {
        RepositoryTab const tab = REPOSITORY_TABS[selected_tab];
        if (auto panel = ui.column(
                gui::id("secondary_tab_content"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(20.0f),
                            .gap = 16.0f,
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            ui.label(
                tab.title,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(30.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.spacer(separator_y(spec));
            for (size_t index = 0u; index < 18u; ++index) {
                auto row_scope =
                    ui.id_scope(indexed_id("secondary_row", static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(42.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, tab.title, 13.5f, gui::StyleRole::TEXT);
                    label(ui, "Updated 11 minutes ago", 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_section_top_bar(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepoDetails const& details,
        RepositorySection section
    ) -> void {
        StrRef const title = section_title(section);
        if (auto top = ui.row(
                gui::id("section_top_bar"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::px(58.0f),
                            .padding = gui::insets(0.0f, 20.0f),
                            .gap = 12.0f,
                            .align_y = gui::Align::CENTER,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            label(ui, details.name, 13.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, "/", 14.0f, gui::StyleRole::TEXT_MUTED);
            label(ui, title, 13.5f, gui::StyleRole::TEXT);
            ui.spacer({.layout = {.width = gui::fill(), .height = gui::px(1.0f)}});
            label(ui, "Local", 12.5f, gui::StyleRole::TEXT_MUTED);
            label(ui, details.activity, 12.5f, gui::StyleRole::TEXT_MUTED);
        }
        ui.spacer(separator_y(spec));
    }

    auto draw_section_page(gui::Frame& ui, RepositorySpec const& spec, RepositorySection section)
        -> void {
        StrRef const title = section_title(section);
        if (auto panel = ui.column(
                gui::id("section_panel"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::children(),
                            .padding = gui::insets(20.0f),
                            .gap = 16.0f,
                        },
                    .style = {
                        .background = spec.panel,
                        .border = spec.border,
                        .border_thickness = 1.0f,
                        .radius = 8.0f,
                    },
                }
            )) {
            if (auto crumb = ui.row(
                    {.layout = {.width = gui::fill(), .height = gui::px(20.0f), .gap = 8.0f}}
                )) {
                label(ui, "Repository", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, "/", 12.5f, gui::StyleRole::TEXT_MUTED);
                label(ui, title, 12.5f, gui::StyleRole::TEXT);
            }
            ui.label(
                title,
                {
                    .layout = {.width = gui::fill(), .height = gui::px(30.0f)},
                    .style = {.font_size = 23.0f},
                }
            );
            ui.spacer(separator_y(spec));
            for (size_t index = 0u; index < sizeof(SECTION_ROWS) / sizeof(SECTION_ROWS[0]);
                 ++index) {
                gui::Id const scope = indexed_id("section_row", static_cast<uint64_t>(section));
                auto row_scope = ui.id_scope(gui::id(scope, static_cast<uint64_t>(index)));
                BASE_UNUSED(row_scope);
                if (auto row = ui.row(
                        gui::id("row"),
                        {
                            .layout = {
                                .width = gui::fill(),
                                .height = gui::px(44.0f),
                                .padding = gui::insets(0.0f, 20.0f),
                                .gap = 12.0f,
                                .align_y = gui::Align::CENTER,
                            },
                        }
                    )) {
                    label_fill(ui, SECTION_ROWS[index], 13.5f, gui::StyleRole::TEXT);
                    label(ui, "Updated now", 12.5f, gui::StyleRole::TEXT_MUTED);
                }
                ui.spacer(separator_y(spec));
            }
        }
    }

    auto draw_main_content(
        gui::Frame& ui,
        RepositorySpec const& spec,
        RepoDetails const& details,
        RepositorySection selected_section,
        size_t& selected_tab,
        RepoTree& tree
    ) -> void {
        if (auto main = ui.column(
                gui::id("main_content"),
                {
                    .layout = {.width = gui::fill(), .height = gui::fill()},
                    .style = {.background = spec.shell},
                }
            )) {
            if (selected_section == RepositorySection::CODE) {
                draw_top_bar(ui, spec, details, selected_tab);
                auto scroll_scope =
                    ui.id_scope(indexed_id("tab_scroll", static_cast<uint64_t>(selected_tab)));
                BASE_UNUSED(scroll_scope);
                if (auto content = ui.scroll_panel(
                        gui::id("content"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(24.0f),
                                    .gap = 20.0f,
                                },
                            .debug_name = "repository_tab_content",
                        }
                    )) {
                    if (selected_tab == 0u) {
                        draw_repo_summary(ui, spec, details);
                        draw_latest_commit(ui, spec, details);
                        draw_file_table(ui, spec, tree);
                    } else if (selected_tab == 1u) {
                        draw_commits_tab(ui, spec, details);
                    } else {
                        draw_secondary_tab_content(ui, spec, selected_tab);
                    }
                }
            } else {
                draw_section_top_bar(ui, spec, details, selected_section);
                auto scroll_scope = ui.id_scope(
                    indexed_id("section_scroll", static_cast<uint64_t>(selected_section))
                );
                BASE_UNUSED(scroll_scope);
                if (auto content = ui.scroll_panel(
                        gui::id("content"),
                        {
                            .layout =
                                {
                                    .width = gui::fill(),
                                    .height = gui::fill(),
                                    .padding = gui::insets(24.0f),
                                    .gap = 20.0f,
                                },
                            .debug_name = "repository_section_content",
                        }
                    )) {
                    draw_section_page(ui, spec, selected_section);
                }
            }
        }
    }

    auto draw_repository_ui(
        gui::Frame& ui,
        font_cache::Font icon_font,
        RepositorySection& selected_section,
        size_t& selected_tab,
        RepoTree& tree,
        RepoDetails const& details
    ) -> void {
        decorative_label_index = 0u;
        RepositorySpec const spec = {};
        if (auto shell = ui.row(
                gui::id("app_shell"),
                {
                    .layout =
                        {
                            .width = gui::fill(),
                            .height = gui::fill(),
                            .clip = true,
                        },
                    .style = {.background = spec.shell},
                }
            )) {
            draw_sidebar(ui, spec, icon_font, selected_section);
            ui.spacer(separator_x(spec));
            draw_main_content(ui, spec, details, selected_section, selected_tab, tree);
        }
    }

} // namespace repository_ui_testbed
