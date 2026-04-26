#include <base/crash.h>
#include <base/fmt.h>
#include <cstdio>
#include <gui/gui.h>

auto check_crash() -> void {
    int const* ptr = nullptr;
    fmt::printf("result: %d\n", *ptr);
}

auto main() -> int {
    base::install_crash_handlers();

    gui::Version const gui_version = gui::version();

    fmt::printf("gui %s\n", gui::version_string());
    fmt::printf("version: %u.%u.%u\n", gui_version.major, gui_version.minor, gui_version.patch);
    fmt::printf("compiler: %s\n", gui::build_compiler());
    check_crash();
    return 0;
}
