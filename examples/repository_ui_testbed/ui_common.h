#pragma once

#include "shared.h"

#include <gui/gui.h>

namespace repository_ui_testbed {

    [[nodiscard]] inline auto indexed_id(StrRef scope, uint64_t index) -> gui::Id {
        return gui::id(gui::id(scope), index);
    }

    [[nodiscard]] inline auto indexed_local_id(StrRef scope, uint64_t index, StrRef local)
        -> gui::Id {
        return gui::id(indexed_id(scope, index), local);
    }

    [[nodiscard]] inline auto file_row_id(size_t index) -> gui::Id {
        gui::Id const tab_scope = indexed_id("tab_scroll", 0u);
        gui::Id const row_scope =
            gui::id(tab_scope, indexed_id("file_row", static_cast<uint64_t>(index)));
        return gui::id(row_scope, "row");
    }

    [[nodiscard]] inline auto tab_scroll_id(size_t index) -> gui::Id {
        return indexed_local_id("tab_scroll", static_cast<uint64_t>(index), "content");
    }

} // namespace repository_ui_testbed
