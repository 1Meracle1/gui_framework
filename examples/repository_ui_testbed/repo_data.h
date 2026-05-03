#pragma once

#include "shared.h"

#include <base/memory.h>

namespace repository_ui_testbed {

    [[nodiscard]] auto repo_tree_open_hash(RepoTree const& tree) -> uint64_t;
    auto load_repo_tree(Arena& arena, RepoTree& tree) -> void;
    auto load_repo_details(Arena& arena, RepoTree const& tree, RepoDetails& details) -> void;
    auto rebuild_visible_tree(RepoTree& tree) -> void;

} // namespace repository_ui_testbed
