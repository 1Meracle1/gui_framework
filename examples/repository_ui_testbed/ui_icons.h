#pragma once

#include "shared.h"

#include <draw/draw.h>
#include <gui/gui.h>

namespace repository_ui_testbed {

    auto draw_repository_icons(
        gui::Frame const& ui,
        gui::draw::Context context,
        RepositorySection selected_section,
        size_t selected_tab,
        RepoTree const& tree
    ) -> void;

} // namespace repository_ui_testbed
