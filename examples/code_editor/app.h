#pragma once

#include "shared.h"

namespace code_editor {

#if defined(_WIN32)
    [[nodiscard]] auto code_editor_module_api() -> ModuleApi const*;
#endif

    auto run_console_fallback() -> int;

} // namespace code_editor
