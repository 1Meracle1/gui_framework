#pragma once

#include <cstdint>

namespace gui {

    struct Version {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
    };

    [[nodiscard]] Version version();
    [[nodiscard]] char const* version_string();
    [[nodiscard]] char const* build_compiler();

} // namespace gui
