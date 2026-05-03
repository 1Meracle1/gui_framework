#pragma once

#include "shared.h"

#include <font_cache/font_cache.h>
#include <gui/gui.h>

namespace repository_ui_testbed {

    auto draw_repository_ui(
        gui::Frame& ui,
        gui::font_cache::Font icon_font,
        RepositorySection& selected_section,
        size_t& selected_tab,
        RepoTree& tree,
        RepoDetails const& details
    ) -> void;

} // namespace repository_ui_testbed
