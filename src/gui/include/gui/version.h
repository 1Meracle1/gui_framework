#pragma once

#include <cstdint>

namespace gui {

    struct Version {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
    };

    [[nodiscard]] auto version() -> Version;
    [[nodiscard]] auto version_string() -> char const*;
    [[nodiscard]] auto build_compiler() -> char const*;

} // namespace gui
