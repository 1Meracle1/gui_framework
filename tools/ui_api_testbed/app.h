#pragma once

#include "shared.h"

namespace ui_api_testbed {

#if defined(_WIN32)
    [[nodiscard]] auto ui_api_testbed_module_api() -> ModuleApi const*;
#endif

    auto run_console_fallback() -> int;

} // namespace ui_api_testbed
