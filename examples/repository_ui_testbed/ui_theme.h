#pragma once

#include "shared.h"

#include <font_cache/font_cache.h>
#include <gui/gui.h>

namespace repository_ui_testbed {

    [[nodiscard]] auto repo_theme(gui::font_cache::Font font, RepositorySpec const& spec)
        -> gui::ThemeDesc;

} // namespace repository_ui_testbed
