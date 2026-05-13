#pragma once

#include "editor_model.h"

namespace code_editor {

    [[nodiscard]] auto save_editor_session_state(EditorState const& editor, StrRef state_cache_path)
        -> bool;
    [[nodiscard]] auto load_editor_session_state(EditorState& editor, StrRef state_cache_path)
        -> bool;

} // namespace code_editor
