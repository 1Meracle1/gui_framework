#include <gui/version.h>

#ifndef GUI_VERSION_MAJOR
#define GUI_VERSION_MAJOR 0
#endif

#ifndef GUI_VERSION_MINOR
#define GUI_VERSION_MINOR 0
#endif

#ifndef GUI_VERSION_PATCH
#define GUI_VERSION_PATCH 0
#endif

#ifndef GUI_VERSION_STRING
#define GUI_VERSION_STRING "0.0.0"
#endif

namespace {

    constexpr gui::Version VERSION = {
        static_cast<uint32_t>(GUI_VERSION_MAJOR),
        static_cast<uint32_t>(GUI_VERSION_MINOR),
        static_cast<uint32_t>(GUI_VERSION_PATCH),
    };

} // namespace

namespace gui {

    auto version() -> Version {
        return VERSION;
    }

    auto version_string() -> char const* {
        return GUI_VERSION_STRING;
    }

    auto build_compiler() -> char const* {
#if defined(__clang__) && defined(_MSC_VER)
        return "clang-cl";
#elif defined(_MSC_VER)
        return "msvc";
#elif defined(__apple_build_version__)
        return "apple-clang";
#elif defined(__clang__)
        return "clang";
#else
        return "unknown";
#endif
    }

} // namespace gui
