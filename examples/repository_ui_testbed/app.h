#pragma once

#include "shared.h"

namespace repository_ui_testbed {

#if defined(_WIN32)
    [[nodiscard]] auto repository_ui_testbed_module_api() -> ModuleApi const*;
#endif

} // namespace repository_ui_testbed
